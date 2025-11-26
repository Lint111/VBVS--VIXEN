#include "Nodes/FramebufferNode.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include "VulkanSwapChain.h"  // For SwapChainPublicVariables definition
#include "NodeHelpers/VulkanStructHelpers.h"

using namespace RenderGraph::NodeHelpers;

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
    NODE_LOG_DEBUG("FramebufferNode: Setup (graph-scope initialization)");
}

void FramebufferNode::UpdateVectorView() {
    // Sync vector view with bounded array for API compatibility
    framebuffersView_.assign(framebuffers_.begin(), framebuffers_.end());
}

void FramebufferNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("Compile: Creating framebuffers");

    // Validate and set up device
    VulkanDevice* devicePtr = ctx.In(FramebufferNodeConfig::VULKAN_DEVICE_IN);
    VkRenderPass renderPass = ctx.In(FramebufferNodeConfig::RENDER_PASS);
    ValidateInputs(devicePtr, renderPass);
    SetDevice(devicePtr);

    // Get depth attachment
    VkImageView depthView = ctx.In(FramebufferNodeConfig::DEPTH_ATTACHMENT);
    hasDepth = (depthView != VK_NULL_HANDLE);
    NODE_LOG_DEBUG("Depth attachment: " + std::string(hasDepth ? "enabled" : "disabled"));

    // Get parameters
    uint32_t layers = GetParameterValue<uint32_t>(FramebufferNodeConfig::PARAM_LAYERS, 1);

    // Get swapchain info
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

    if (colorAttachmentCount > MAX_SWAPCHAIN_IMAGES) {
        throw std::runtime_error("FramebufferNode: Too many swapchain images (" + 
            std::to_string(colorAttachmentCount) + " > " + std::to_string(MAX_SWAPCHAIN_IMAGES) + ")");
    }

    NODE_LOG_INFO("[FramebufferNode::Compile] Creating " + std::to_string(colorAttachmentCount) + " framebuffers from swapchain");

    // Phase H: Clear bounded array
    framebuffers_.Clear();

    // Create one framebuffer per swapchain image
    for (size_t i = 0; i < colorAttachmentCount; i++) {
        VkImageView colorView = swapchainInfo->colorBuffers[i].view;
        NODE_LOG_DEBUG("[FramebufferNode::Compile] Processing attachment " + std::to_string(i));

        // Phase H: Stack-allocated attachment array
        auto attachments = BuildAttachmentArray(colorView, depthView);

        try {
            VkFramebuffer fb = CreateSingleFramebuffer(renderPass, attachments, swapchainInfo->Extent, layers);
            framebuffers_.Add(fb);
            NODE_LOG_DEBUG("[FramebufferNode::Compile] Created framebuffer[" + std::to_string(i) + "]");
        } catch (const std::exception&) {
            CleanupPartialFramebuffers(framebuffers_.Size());
            throw;
        }
    }

    // Update vector view for API compatibility
    UpdateVectorView();

    // Output framebuffers
    ctx.Out(FramebufferNodeConfig::FRAMEBUFFERS, framebuffersView_);
    ctx.Out(FramebufferNodeConfig::VULKAN_DEVICE_OUT, device);
    NODE_LOG_INFO("Compile complete: Created " + std::to_string(framebuffers_.Size()) + " framebuffers");
}

void FramebufferNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // No-op - framebuffers are created in Compile phase
}

void FramebufferNode::CleanupImpl(TypedCleanupContext& ctx) {
    if (!framebuffers_.Empty() && device != nullptr) {
        NODE_LOG_DEBUG("Cleanup: Destroying " + std::to_string(framebuffers_.Size()) + " framebuffers");

        for (auto& framebuffer : framebuffers_) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device->device, framebuffer, nullptr);
            }
        }
        framebuffers_.Clear();
        framebuffersView_.clear();
    }
}

// ====== Private Helper Methods ======

void FramebufferNode::ValidateInputs(VulkanDevice* devicePtr, VkRenderPass renderPass) {
    if (devicePtr == nullptr) {
        std::string errorMsg = "FramebufferNode: VkDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
}

ResourceManagement::BoundedArray<VkImageView, FramebufferNode::MAX_ATTACHMENTS> 
FramebufferNode::BuildAttachmentArray(VkImageView colorView, VkImageView depthView) {
    // Phase H: Stack-allocated attachment array
    ResourceManagement::BoundedArray<VkImageView, MAX_ATTACHMENTS> attachments;
    attachments.Add(colorView);

    if (hasDepth) {
        attachments.Add(depthView);
    }

    return attachments;
}

VkFramebuffer FramebufferNode::CreateSingleFramebuffer(
    VkRenderPass renderPass,
    const ResourceManagement::BoundedArray<VkImageView, MAX_ATTACHMENTS>& attachments,
    const VkExtent2D& extent,
    uint32_t layers
) {

    VkFramebufferCreateInfo framebufferInfo = CreateFramebufferInfo(
        renderPass,
        attachments.Data(),
        static_cast<uint32_t>(attachments.Size()),
        extent.width,
        extent.height,
        layers
    );

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkResult result = vkCreateFramebuffer(device->device, &framebufferInfo, nullptr, &framebuffer);

    if (result != VK_SUCCESS) {
        VulkanError error{result, "Failed to create framebuffer"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }

    return framebuffer;
}

void FramebufferNode::CleanupPartialFramebuffers(size_t count) {
    for (size_t j = 0; j < count; j++) {
        if (framebuffers_[j] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device->device, framebuffers_[j], nullptr);
        }
    }
    framebuffers_.Clear();
}

} // namespace Vixen::RenderGraph
