#include "Nodes/PresentNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"

namespace Vixen::RenderGraph {

// ====== PresentNodeType ======

PresentNodeType::PresentNodeType(const std::string& typeName) : NodeType(typeName) {
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics; // Uses graphics queue for presentation
    supportsInstancing = false; // Only one present operation at a time
    maxInstances = 1;

    // Populate schemas from Config
    PresentNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 512; // Minimal
    workloadMetrics.estimatedComputeCost = 0.1f;
    workloadMetrics.estimatedBandwidthCost = 0.5f; // Display bandwidth
    workloadMetrics.canRunInParallel = false; // Presentation is sequential
}

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

PresentNode::~PresentNode() {
    Cleanup();
}

void PresentNode::SetupImpl() {
    // Read and validate device input
    VulkanDevicePtr devicePtr = In(PresentNodeConfig::VULKAN_DEVICE_IN);
    if (devicePtr == nullptr) {
        throw std::runtime_error("PresentNode: Invalid device handle");
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);
}

void PresentNode::CompileImpl() {
    // Get parameters using config constants
    waitForIdle = GetParameterValue<bool>(PresentNodeConfig::WAIT_FOR_IDLE, true);

    // Validate inputs using typed slot access
    VkSwapchainKHR swapchain = In(PresentNodeConfig::SWAPCHAIN);
    if (swapchain == VK_NULL_HANDLE) {
        throw std::runtime_error("PresentNode: swapchain input not connected or invalid");
    }

    // Note: PRESENT_FUNCTION input is optional - if not provided, we use vkQueuePresentKHR directly
}

void PresentNode::ExecuteImpl() {
    // Call Present() during graph execution
    // Swapchain and imageIndex must be set before this is called
    Present();
}

void PresentNode::CleanupImpl() {
    // No resources to clean up
}

VkResult PresentNode::Present() {
    // Get inputs on-demand via typed slots
    VkSwapchainKHR swapchain = In(PresentNodeConfig::SWAPCHAIN, NodeInstance::SlotRole::ExecuteOnly);
    uint32_t imageIndex = In(PresentNodeConfig::IMAGE_INDEX, NodeInstance::SlotRole::ExecuteOnly);
    VkSemaphore renderCompleteSemaphore = In(PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE, NodeInstance::SlotRole::ExecuteOnly);

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
    fpQueuePresent = In(PresentNodeConfig::PRESENT_FUNCTION, NodeInstance::SlotRole::ExecuteOnly);
    }

    if (fpQueuePresent == nullptr) {
        throw std::runtime_error("PresentNode: No present function available");
    }

    // Setup present info
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
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
    Out(PresentNodeConfig::PRESENT_RESULT, &lastResult);
    Out(PresentNodeConfig::VULKAN_DEVICE_OUT, device);

    return lastResult;
}

} // namespace Vixen::RenderGraph
