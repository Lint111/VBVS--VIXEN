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
    : NodeInstance(instanceName, nodeType)
{
}

PresentNode::~PresentNode() {
    Cleanup();
}

void PresentNode::Setup() {
    // No setup needed
}

void PresentNode::Compile() {
    // Get parameters
    waitForIdle = GetParameterValue<bool>("waitForIdle", true);

    // Validate inputs (swapchain/imageIndex are set dynamically, so only check queue and function pointer)
    if (queue == VK_NULL_HANDLE) {
        throw std::runtime_error("PresentNode: queue not set");
    }

    if (fpQueuePresent == nullptr) {
        throw std::runtime_error("PresentNode: present function pointer not set");
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

    return lastResult;
}

// Setter methods
void PresentNode::SetSwapchain(VkSwapchainKHR sc) {
    swapchain = sc;
}

void PresentNode::SetImageIndex(uint32_t index) {
    imageIndex = index;
}

void PresentNode::SetQueue(VkQueue q) {
    queue = q;
}

void PresentNode::SetRenderCompleteSemaphore(VkSemaphore semaphore) {
    renderCompleteSemaphore = semaphore;
}

void PresentNode::SetPresentFunction(PFN_vkQueuePresentKHR func) {
    fpQueuePresent = func;
}

} // namespace Vixen::RenderGraph
