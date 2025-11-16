#include "Nodes/FramebufferNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include "VulkanResources/VulkanSwapChain.h"  // For SwapChainPublicVariables definition
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
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("FramebufferNode: Setup (graph-scope initialization)");
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

    NODE_LOG_INFO("[FramebufferNode::Compile] Creating " + std::to_string(colorAttachmentCount) + " framebuffers from swapchain");

    // Clear and resize framebuffer vector
    framebuffers.clear();
    framebuffers.resize(colorAttachmentCount);

    // Create one framebuffer per swapchain image
    for (size_t i = 0; i < colorAttachmentCount; i++) {
        VkImageView colorView = swapchainInfo->colorBuffers[i].view;
        NODE_LOG_DEBUG("[FramebufferNode::Compile] Processing attachment " + std::to_string(i) + ", view=" + std::to_string(reinterpret_cast<uint64_t>(colorView)));

        std::vector<VkImageView> attachments = BuildAttachmentArray(colorView, depthView);

        try {
            framebuffers[i] = CreateSingleFramebuffer(renderPass, attachments, swapchainInfo->Extent, layers);
            NODE_LOG_DEBUG("[FramebufferNode::Compile] Created framebuffer[" + std::to_string(i) + "]=" + std::to_string(reinterpret_cast<uint64_t>(framebuffers[i])));
        } catch (const std::exception&) {
            CleanupPartialFramebuffers(i);
            throw;
        }
    }

    // Output framebuffers
    ctx.Out(FramebufferNodeConfig::FRAMEBUFFERS, framebuffers);
    ctx.Out(FramebufferNodeConfig::VULKAN_DEVICE_OUT, device);
    NODE_LOG_INFO("Compile complete: Created " + std::to_string(framebuffers.size()) + " framebuffers");
}

void FramebufferNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // No-op - framebuffers are created in Compile phase
}

void FramebufferNode::CleanupImpl(TypedCleanupContext& ctx) {
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

// ====== Private Helper Methods ======

void FramebufferNode::ValidateInputs(VulkanDevice* devicePtr, VkRenderPass renderPass) {
    if (devicePtr == nullptr) {
        std::string errorMsg = "FramebufferNode: VkDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
}

std::vector<VkImageView> FramebufferNode::BuildAttachmentArray(VkImageView colorView, VkImageView depthView) {
    std::vector<VkImageView> attachments;
    attachments.push_back(colorView);

    if (hasDepth) {
        attachments.push_back(depthView);
    }

    return attachments;
}

VkFramebuffer FramebufferNode::CreateSingleFramebuffer(
    VkRenderPass renderPass,
    const std::vector<VkImageView>& attachments,
    const VkExtent2D& extent,
    uint32_t layers
) {

    VkFramebufferCreateInfo framebufferInfo = CreateFramebufferInfo(
        renderPass,
        attachments.data(),
        static_cast<uint32_t>(attachments.size()),
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
        if (framebuffers[j] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device->device, framebuffers[j], nullptr);
        }
    }
    framebuffers.clear();
}

} // namespace Vixen::RenderGraph