#include "Nodes/FramebufferNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include "VulkanSwapChain.h"  // For SwapChainPublicVariables definition

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

void FramebufferNode::SetupImpl(TypedNode<FramebufferNodeConfig>::TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("FramebufferNode: Setup (graph-scope initialization)");
}

void FramebufferNode::CompileImpl(TypedNode<FramebufferNodeConfig>::TypedCompileContext& ctx) {
    NODE_LOG_INFO("Compile: Creating framebuffers");

    // Access device input (compile-time dependency)
    VulkanDevicePtr devicePtr = In(FramebufferNodeConfig::VULKAN_DEVICE_IN);

    if (devicePtr == nullptr) {
        std::string errorMsg = "FramebufferNode: VkDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    // Get typed inputs
    VkRenderPass renderPass = In(FramebufferNodeConfig::RENDER_PASS);   

    // Check for depth attachment
    VkImageView depthView = In(FramebufferNodeConfig::DEPTH_ATTACHMENT);
    hasDepth = (depthView != VK_NULL_HANDLE);

    NODE_LOG_DEBUG("Depth attachment: " + std::string(hasDepth ? "enabled" : "disabled"));

    // Get typed parameter
    uint32_t layers = GetParameterValue<uint32_t>(
        FramebufferNodeConfig::PARAM_LAYERS, 1);

    // Get swapchain public variables to access ALL color buffers
    SwapChainPublicVariables* swapchainInfo = In(FramebufferNodeConfig::SWAPCHAIN_INFO);
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
    std::cout << "[FramebufferNode::Compile] Creating " << colorAttachmentCount << " framebuffers from swapchain" << std::endl;

    // Note: RenderGraph calls node->Cleanup() before recompilation, so we don't need to call it here
    // Clear the framebuffer vector to prepare for new framebuffers
    framebuffers.clear();

    // Resize framebuffer array
    framebuffers.resize(colorAttachmentCount);

    // Create one framebuffer per color attachment (swapchain image)
    for (size_t i = 0; i < colorAttachmentCount; i++) {
        // Get color attachment from swapchain public variables
        VkImageView colorView = swapchainInfo->colorBuffers[i].view;
        std::cout << "[FramebufferNode::Compile] Processing attachment " << i << ", view=" << colorView << std::endl;

        // Setup attachments array
        std::vector<VkImageView> attachments;
        attachments.push_back(colorView);

        // Add depth attachment if present
        if (hasDepth) {
            attachments.push_back(depthView);
        }

        // Create framebuffer info
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.pNext = nullptr;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapchainInfo->Extent.width;
        framebufferInfo.height = swapchainInfo->Extent.height;
        framebufferInfo.layers = layers;
        framebufferInfo.flags = 0;

        VkResult result = vkCreateFramebuffer(
            device->device,
            &framebufferInfo,
            nullptr,
            &framebuffers[i]
        );

        if (result != VK_SUCCESS) {
            // Clean up already created framebuffers
            for (size_t j = 0; j < i; j++) {
                if (framebuffers[j] != VK_NULL_HANDLE) {
                    vkDestroyFramebuffer(device->device, framebuffers[j], nullptr);
                }
            }
            framebuffers.clear();

            VulkanError error{result, "Failed to create framebuffer " + std::to_string(i)};
            NODE_LOG_ERROR(error.toString());
            throw std::runtime_error(error.toString());
        }

        std::cout << "[FramebufferNode::Compile] Created framebuffer[" << i << "]=" << framebuffers[i] << std::endl;
        NODE_LOG_DEBUG("Created framebuffer " + std::to_string(i) + ": " +
                      std::to_string(reinterpret_cast<uint64_t>(framebuffers[i])));
    }

    // Output all framebuffers as a vector in ONE bundle
    Out(FramebufferNodeConfig::FRAMEBUFFERS, framebuffers);
    std::cout << "[FramebufferNode::Compile] Output " << framebuffers.size() << " framebuffers as vector" << std::endl;

    Out(FramebufferNodeConfig::VULKAN_DEVICE_OUT, device);

    NODE_LOG_INFO("Compile complete: Created " + std::to_string(framebuffers.size()) + " framebuffers");
}

void FramebufferNode::ExecuteImpl(TypedNode<FramebufferNodeConfig>::TypedExecuteContext& ctx) {
    // No-op - framebuffers are created in Compile phase
}

void FramebufferNode::CleanupImpl(TypedNode<FramebufferNodeConfig>::TypedCleanupContext& ctx) {
    if (!framebuffers.empty() && device != nullptr) {
        NODE_LOG_DEBUG("Cleanup: Destroying " + std::to_string(framebuffers.size()) + " framebuffers");

        for (VkFramebuffer framebuffer : framebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device->device, framebuffer, nullptr);
            }
        }
        framebuffers.clear();
    }
}

} // namespace Vixen::RenderGraph