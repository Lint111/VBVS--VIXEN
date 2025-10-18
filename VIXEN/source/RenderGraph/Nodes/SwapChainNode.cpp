#include "RenderGraph/Nodes/SwapChainNode.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// ====== SwapChainNodeType ======

SwapChainNodeType::SwapChainNodeType() {
    typeId = 102; // Unique ID
    typeName = "SwapChain";
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics; // Uses graphics queue for presentation
    supportsInstancing = false; // Only one swapchain per render graph
    maxInstances = 1;

    // No inputs

    // Outputs are opaque (accessed via Get methods)

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 32 * 1024 * 1024; // ~32MB for swapchain images
    workloadMetrics.estimatedComputeCost = 0.2f;
    workloadMetrics.estimatedBandwidthCost = 0.1f;
    workloadMetrics.canRunInParallel = false; // Swapchain operations are sequential
}

std::unique_ptr<NodeInstance> SwapChainNodeType::CreateInstance(
    const std::string& instanceName,
    Vixen::Vulkan::Resources::VulkanDevice* device
) const {
    return std::make_unique<SwapChainNode>(
        instanceName,
        const_cast<SwapChainNodeType*>(this),
        device
    );
}

// ====== SwapChainNode ======

SwapChainNode::SwapChainNode(
    const std::string& instanceName,
    NodeType* nodeType,
    Vixen::Vulkan::Resources::VulkanDevice* device
)
    : NodeInstance(instanceName, nodeType, device)
{
}

SwapChainNode::~SwapChainNode() {
    Cleanup();
}

void SwapChainNode::Setup() {
    // Swapchain setup happens via SetSwapChainWrapper
    // This node wraps the existing VulkanSwapChain infrastructure
}

void SwapChainNode::Compile() {
    // Get parameters
    width = GetParameterValue<uint32_t>("width", 0);
    height = GetParameterValue<uint32_t>("height", 0);

    if (width == 0 || height == 0) {
        throw std::runtime_error("SwapChainNode: width and height parameters are required");
    }

    // Validate that swapchain wrapper is set
    if (swapChainWrapper == nullptr) {
        throw std::runtime_error("SwapChainNode: swapchain wrapper not set");
    }

    // Swapchain should already be created by VulkanSwapChain::CreateSwapChain
    // This node primarily provides graph-based access to the swapchain
}

void SwapChainNode::Execute(VkCommandBuffer commandBuffer) {
    // Swapchain doesn't execute commands per se
    // Image acquisition and presentation happen via AcquireNextImage and PresentNode
}

void SwapChainNode::Cleanup() {
    // SwapChain cleanup is handled by VulkanSwapChain wrapper
    // Don't destroy it here as it may be owned externally
    swapChainWrapper = nullptr;
}

VkSwapchainKHR SwapChainNode::GetSwapchain() const {
    if (swapChainWrapper) {
        return swapChainWrapper->scPublicVars.swapChain;
    }
    return VK_NULL_HANDLE;
}

const std::vector<VkImageView>& SwapChainNode::GetColorImageViews() const {
    static std::vector<VkImageView> emptyViews;
    
    if (!swapChainWrapper) {
        return emptyViews;
    }

    // Extract image views from swapchain buffers
    static thread_local std::vector<VkImageView> views;
    views.clear();
    
    for (const auto& buffer : swapChainWrapper->scPublicVars.colorBuffers) {
        views.push_back(buffer.view);
    }
    
    return views;
}

uint32_t SwapChainNode::GetImageCount() const {
    if (swapChainWrapper) {
        return swapChainWrapper->scPublicVars.swapChainImageCount;
    }
    return 0;
}

VkFormat SwapChainNode::GetFormat() const {
    if (swapChainWrapper) {
        return swapChainWrapper->scPublicVars.Format;
    }
    return VK_FORMAT_UNDEFINED;
}

void SwapChainNode::SetSwapChainWrapper(VulkanSwapChain* swapchain) {
    swapChainWrapper = swapchain;
}

uint32_t SwapChainNode::AcquireNextImage(VkSemaphore presentCompleteSemaphore) {
    if (!swapChainWrapper) {
        throw std::runtime_error("SwapChainNode: swapchain wrapper not set");
    }

    VkResult result = swapChainWrapper->fpAcquireNextImageKHR(
        device->device,
        swapChainWrapper->scPublicVars.swapChain,
        UINT64_MAX, // Timeout
        presentCompleteSemaphore,
        VK_NULL_HANDLE, // Fence
        &currentImageIndex
    );

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("SwapChainNode: failed to acquire swapchain image");
    }

    return currentImageIndex;
}

void SwapChainNode::Recreate(uint32_t newWidth, uint32_t newHeight) {
    if (!swapChainWrapper) {
        throw std::runtime_error("SwapChainNode: swapchain wrapper not set");
    }

    width = newWidth;
    height = newHeight;

    // Destroy and recreate swapchain
    swapChainWrapper->DestroySwapChain();
    swapChainWrapper->SetSwapChainExtent(newWidth, newHeight);
    
    // Note: CreateSwapChain requires a command buffer parameter
    // This should be coordinated with the render graph execution
}

} // namespace Vixen::RenderGraph
