#include "Nodes/PresentNode.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// ====== PresentNodeType ======

PresentNodeType::PresentNodeType(const std::string& typeName) : NodeType(typeName) {
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics; // Uses graphics queue for presentation
    supportsInstancing = false; // Only one present operation at a time
    maxInstances = 1;

    // Inputs are opaque references (set via Set methods)

    // No outputs (result accessed via GetLastResult)

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

void PresentNode::Setup() {
    // No setup needed
}

void PresentNode::Compile() {
    // Get parameters using config constants
    waitForIdle = GetParameterValue<bool>(PresentNodeConfig::WAIT_FOR_IDLE, true);

    // Validate inputs using typed slot access
    VkSwapchainKHR swapchain = In(PresentNodeConfig::SWAPCHAIN);
    if (swapchain == VK_NULL_HANDLE) {
        throw std::runtime_error("PresentNode: swapchain input not connected or invalid");
    }

    VkQueue queue = In(PresentNodeConfig::QUEUE);
    if (queue == VK_NULL_HANDLE) {
        throw std::runtime_error("PresentNode: queue input not connected or invalid");
    }

    PFN_vkQueuePresentKHR fpQueuePresent = In(PresentNodeConfig::PRESENT_FUNCTION);
    if (fpQueuePresent == nullptr) {
        throw std::runtime_error("PresentNode: present function pointer input not connected or invalid");
    }
}

void PresentNode::Execute(VkCommandBuffer commandBuffer) {
    // Call Present() during graph execution
    // Swapchain and imageIndex must be set before this is called
    Present();
}

void PresentNode::Cleanup() {
    // No resources to clean up
}

VkResult PresentNode::Present() {
    // Get inputs on-demand via typed slots
    VkSwapchainKHR swapchain = In(PresentNodeConfig::SWAPCHAIN);
    uint32_t imageIndex = In(PresentNodeConfig::IMAGE_INDEX);
    VkQueue queue = In(PresentNodeConfig::QUEUE);
    VkSemaphore renderCompleteSemaphore = In(PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE);
    PFN_vkQueuePresentKHR fpQueuePresent = In(PresentNodeConfig::PRESENT_FUNCTION);

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
    lastResult = fpQueuePresent(queue, &presentInfo);

    // Wait for device idle if requested (for compatibility with current behavior)
    if (waitForIdle && lastResult == VK_SUCCESS) {
        vkDeviceWaitIdle(device->device);
    }

    // Set output
    Out(PresentNodeConfig::PRESENT_RESULT, &lastResult);

    return lastResult;
}

} // namespace Vixen::RenderGraph
