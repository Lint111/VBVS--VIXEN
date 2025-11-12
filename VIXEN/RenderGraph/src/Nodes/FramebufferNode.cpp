#include "Nodes/FramebufferNode.h"
#include "Core/RenderGraph.h"
#include "Core/ResourceHash.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include "VulkanSwapChain.h"  // For SwapChainPublicVariables definition
#include "Core/VulkanLimits.h"  // For MAX_FRAMEBUFFER_ATTACHMENTS

namespace Vixen::RenderGraph {

// ====== FramebufferNodeType ======

std::unique_ptr<NodeInstance> FramebufferNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<FramebufferNode>(instanceName, const_cast<FramebufferNodeType*>(this));
}

// ====== FramebufferNode ======

FramebufferNode::FramebufferNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<FramebufferNodeConfig>(instanceName, nodeType)
{
}

void FramebufferNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("FramebufferNode: Setup (graph-scope initialization)");
}

void FramebufferNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("Compile: Creating framebuffers");

    // Access device input (compile-time dependency)
    VulkanDevicePtr devicePtr = ctx.In(FramebufferNodeConfig::VULKAN_DEVICE_IN);

    if (devicePtr == nullptr) {
        std::string errorMsg = "FramebufferNode: VkDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    // Get typed inputs
    VkRenderPass renderPass =  ctx.In(FramebufferNodeConfig::RENDER_PASS);

    // Check for depth attachment
    VkImageView depthView = ctx.In(FramebufferNodeConfig::DEPTH_ATTACHMENT);
    hasDepth = (depthView != VK_NULL_HANDLE);

    NODE_LOG_DEBUG("Depth attachment: " + std::string(hasDepth ? "enabled" : "disabled"));

    // Get typed parameter
    uint32_t layers = GetParameterValue<uint32_t>(
        FramebufferNodeConfig::PARAM_LAYERS, 1);

    // Get swapchain public variables to access ALL color buffers
    SwapChainPublicVariables* swapchainInfo = ctx.In(FramebufferNodeConfig::SWAPCHAIN_INFO);
    if (!swapchainInfo) {
        throw std::runtime_error("FramebufferNode: SwapChain info is null");
    }

    size_t colorAttachmentCount = swapchainInfo->colorBuffers.size();
    if (colorAttachmentCount == 0) {
        VulkanError error{VK_ERROR_INITIALIZATION_FAILED, "No color buffers in swapchain"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }

    NODE_LOG_DEBUG("Creating " + std::to_string(colorAttachmentCount) + " framebuffers");
    NODE_LOG_INFO("[FramebufferNode::Compile] Creating " + std::to_string(colorAttachmentCount) + " framebuffers from swapchain");

    // Phase H: Validate framebuffer count against array size
    if (colorAttachmentCount > MAX_SWAPCHAIN_IMAGES) {
        throw std::runtime_error("FramebufferNode: Swapchain image count (" +
                                 std::to_string(colorAttachmentCount) +
                                 ") exceeds MAX_SWAPCHAIN_IMAGES (" +
                                 std::to_string(MAX_SWAPCHAIN_IMAGES) + ")");
    }

    // Phase H: Request URM-managed framebuffers array using helper macro
    REQUEST_STACK_RESOURCE(ctx, VkFramebuffer, MAX_SWAPCHAIN_IMAGES, framebuffers_);

    // Note: RenderGraph calls node->Cleanup() before recompilation, so we don't need to call it here
    // Reset framebuffer count (array elements initialized to VK_NULL_HANDLE in Cleanup)
    framebufferCount = static_cast<uint32_t>(colorAttachmentCount);

    // Create one framebuffer per color attachment (swapchain image)
    for (size_t i = 0; i < colorAttachmentCount; i++) {
        // Get color attachment from swapchain public variables
        VkImageView colorView = swapchainInfo->colorBuffers[i].view;
        NODE_LOG_DEBUG("[FramebufferNode::Compile] Processing attachment " + std::to_string(i) + ", view=" + std::to_string(reinterpret_cast<uint64_t>(colorView)));

        // Phase H: Stack-allocated attachments array (compile-time, not hot path, but demonstrates URM pattern)
        auto attachmentsResult = ctx.RequestStackResource<VkImageView, MAX_FRAMEBUFFER_ATTACHMENTS>(
            "FramebufferAttachments_" + std::to_string(i)
        );

        if (!attachmentsResult) {
            NODE_LOG_ERROR("Failed to allocate framebuffer attachments array");
            throw std::runtime_error("FramebufferNode: Attachment allocation failed");
        }

        auto& attachments = *attachmentsResult;
        attachments->push_back(colorView);

        // Add depth attachment if present
        if (hasDepth) {
            attachments->push_back(depthView);
        }

        // Create framebuffer info
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.pNext = nullptr;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments->size());
        framebufferInfo.pAttachments = attachments->data();
        framebufferInfo.width = swapchainInfo->Extent.width;
        framebufferInfo.height = swapchainInfo->Extent.height;
        framebufferInfo.layers = layers;
        framebufferInfo.flags = 0;

        VkResult result = vkCreateFramebuffer(
            device->device,
            &framebufferInfo,
            nullptr,
            &(*framebuffers_)[i]
        );

        if (result != VK_SUCCESS) {
            // Clean up already created framebuffers (in URM-managed array)
            for (size_t j = 0; j < i; j++) {
                if ((*framebuffers_)[j] != VK_NULL_HANDLE) {
                    vkDestroyFramebuffer(device->device, (*framebuffers_)[j], nullptr);
                    (*framebuffers_)[j] = VK_NULL_HANDLE;
                }
            }
            framebufferCount = 0;

            VulkanError error{result, "Failed to create framebuffer " + std::to_string(i)};
            NODE_LOG_ERROR(error.toString());
            throw std::runtime_error(error.toString());
        }

        NODE_LOG_DEBUG("[FramebufferNode::Compile] Created framebuffer[" + std::to_string(i) + "]=" + std::to_string(reinterpret_cast<uint64_t>((*framebuffers_)[i])));
        NODE_LOG_DEBUG("Created framebuffer " + std::to_string(i) + ": " +
                      std::to_string(reinterpret_cast<uint64_t>((*framebuffers_)[i])));
    }

    // Phase H: URM-managed array automatically tracked, just output from handle
    // Convert array to vector for output (interface compatibility)
    // Note: This is a one-time allocation at compile-time, not per-frame
    std::vector<VkFramebuffer> framebuffersVector(framebuffers_->begin(), framebuffers_->begin() + framebufferCount);
    ctx.Out(FramebufferNodeConfig::FRAMEBUFFERS, framebuffersVector);

    // Log allocation location for profiling
    NODE_LOG_INFO("[FramebufferNode::Compile] Output " + std::to_string(framebufferCount) +
                  " framebuffers as vector (URM-managed: " +
                  (framebuffers_->isStack() ? "STACK" : "HEAP") + ")");

    ctx.Out(FramebufferNodeConfig::VULKAN_DEVICE_OUT, device);

    NODE_LOG_INFO("Compile complete: Created " + std::to_string(framebufferCount) + " framebuffers");
}

void FramebufferNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // No-op - framebuffers are created in Compile phase
}

void FramebufferNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Phase H: Cleanup URM-managed framebuffers array
    if (framebuffers_.has_value() && framebufferCount > 0 && device != nullptr) {
        NODE_LOG_DEBUG("Cleanup: Destroying " + std::to_string(framebufferCount) + " framebuffers");

        for (uint32_t i = 0; i < framebufferCount; i++) {
            if ((*framebuffers_)[i] != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device->device, (*framebuffers_)[i], nullptr);
                (*framebuffers_)[i] = VK_NULL_HANDLE;
            }
        }
        framebufferCount = 0;
        framebuffers_.reset();  // Release handle, URM reclaims memory
    }
}

} // namespace Vixen::RenderGraph