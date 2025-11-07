#include "Nodes/PresentNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"

namespace Vixen::RenderGraph {

// ====== PresentNodeType ======

std::unique_ptr<NodeInstance> PresentNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<PresentNode>(
        instanceName,
        const_cast<PresentNodeType*>(this)
    );
}

// ====== PresentNode ======

PresentNode::PresentNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<PresentNodeConfig>(instanceName, nodeType)
{
}

void PresentNode::SetupImpl(SetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("PresentNode: Setup (graph-scope initialization)");
}

void PresentNode::CompileImpl(CompileContext& ctx) {
    // Access device input (compile-time dependency)
    VulkanDevicePtr devicePtr = ctx.In(PresentNodeConfig::VULKAN_DEVICE_IN);
    if (devicePtr == nullptr) {
        throw std::runtime_error("PresentNode: Invalid device handle");
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    // Get parameters using config constants
    waitForIdle = GetParameterValue<bool>(PresentNodeConfig::WAIT_FOR_IDLE, true);

    // Validate inputs using typed slot access
    VkSwapchainKHR swapchain = ctx.In(PresentNodeConfig::SWAPCHAIN);
    if (swapchain == VK_NULL_HANDLE) {
        throw std::runtime_error("PresentNode: swapchain input not connected or invalid");
    }

    // Note: PRESENT_FUNCTION input is optional - if not provided, we use vkQueuePresentKHR directly
}

void PresentNode::ExecuteImpl(ExecuteContext& ctx) {
    Present(ctx);
}

void PresentNode::CleanupImpl(CleanupContext& ctx) {
    // No resources to clean up
}

VkResult PresentNode::Present(Context& ctx) {
    // Get inputs on-demand via typed slots (SlotRole from config)
    VkSwapchainKHR swapchain = ctx.In(PresentNodeConfig::SWAPCHAIN);
    uint32_t imageIndex = ctx.In(PresentNodeConfig::IMAGE_INDEX);
    VkSemaphore renderCompleteSemaphore = ctx.In(PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE);
    const std::vector<VkFence>& presentFenceArray = ctx.In(PresentNodeConfig::PRESENT_FENCE_ARRAY);

    // Guard against invalid image index (swapchain out of date)
    if (imageIndex == UINT32_MAX) {
        NODE_LOG_WARNING("PresentNode: Invalid image index - skipping present");
        return VK_ERROR_OUT_OF_DATE_KHR;
    }
    
    // Get present function pointer - prefer device-provided function, fallback to input connection
    PFN_vkQueuePresentKHR fpQueuePresent = nullptr;
    if (device != nullptr && device->HasPresentSupport()) {
        fpQueuePresent = device->GetPresentFunction();
    } else {
        // Fallback: try to get from input connection
        fpQueuePresent = ctx.In(PresentNodeConfig::PRESENT_FUNCTION);
    }

    if (fpQueuePresent == nullptr) {
        throw std::runtime_error("PresentNode: No present function available");
    }

    VkSwapchainPresentFenceInfoEXT presentFenceInfo{};
    presentFenceInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
    presentFenceInfo.pNext = nullptr;
    if (presentFenceArray.empty()) {
        presentFenceInfo.swapchainCount = 0;
        presentFenceInfo.pFences = nullptr;
    } else {
        presentFenceInfo.swapchainCount = 1;
        presentFenceInfo.pFences = &presentFenceArray[imageIndex];
    }

    // Setup present info
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = &presentFenceInfo;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    
    // Wait for rendering to complete if semaphore is provided
    if (renderCompleteSemaphore != VK_NULL_HANDLE) {
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderCompleteSemaphore;
    } else {
        presentInfo.waitSemaphoreCount = 0;
        presentInfo.pWaitSemaphores = nullptr;
    }
    
    presentInfo.pResults = nullptr;


    // Queue present
    lastResult = fpQueuePresent(device->queue, &presentInfo);

    // Wait for device idle if requested (for compatibility with current behavior)
    if (waitForIdle && lastResult == VK_SUCCESS && device != nullptr) {
        vkDeviceWaitIdle(device->device);
    }

    // Set outputs
    ctx.Out(PresentNodeConfig::PRESENT_RESULT, &lastResult);
    ctx.Out(PresentNodeConfig::VULKAN_DEVICE_OUT, device);

    return lastResult;
}

} // namespace Vixen::RenderGraph
