#include "RenderGraph/Nodes/CreateSwapChainNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "RenderGraph/NodeLogging.h"

namespace Vixen::RenderGraph {

// ====== CreateSwapChainNodeType ======

CreateSwapChainNodeType::CreateSwapChainNodeType() {
    typeId = 202; // Arbitrary unique ID
    typeName = "CreateSwapChain";
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics;
    supportsInstancing = false;
    maxInstances = 1;

    // Populate schema from config
    SwapChainNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();

    workloadMetrics.estimatedMemoryFootprint = 32 * 1024 * 1024;
    workloadMetrics.canRunInParallel = false;
}

std::unique_ptr<NodeInstance> CreateSwapChainNodeType::CreateInstance(
    const std::string& instanceName,
    Vixen::Vulkan::Resources::VulkanDevice* device
) const {
    return std::make_unique<CreateSwapChainNode>(instanceName, const_cast<CreateSwapChainNodeType*>(this), device);
}

// ====== CreateSwapChainNode ======

CreateSwapChainNode::CreateSwapChainNode(
    const std::string& instanceName,
    NodeType* nodeType,
    Vixen::Vulkan::Resources::VulkanDevice* device
) : TypedNode<SwapChainNodeConfig>(instanceName, nodeType, device) {
}

CreateSwapChainNode::~CreateSwapChainNode() {
    Cleanup();
}

void CreateSwapChainNode::Setup() {
    // Nothing heavy here; swapchain will be initialized in Compile
}

void CreateSwapChainNode::Compile() {
    // Expect SURFACE input from WindowNode
    // Retrieve VkSurfaceKHR from connected resource
    // Using TypedNode GetInput to extract VkSurfaceKHR
    VkSurfaceKHR surface = GetInput<VkSurfaceKHR>(SwapChainNodeConfig::SURFACE, 0);

    if (surface == VK_NULL_HANDLE) {
        throw std::runtime_error("CreateSwapChainNode: Surface input required");
    }

    // Create VulkanSwapChain instance owned by this node
    swapchain = std::make_unique<VulkanSwapChain>(nullptr);

    // Set the public surface directly so VulkanSwapChain::Initialize/CreateSwapChain operate
    swapchain->scPublicVars.surface = surface;

    // Initialize function pointers and supported formats
    swapchain->Initialize();

    // Set extent from WindowNode parameters if available via global state
    // For now, rely on VulkanSwapChain to query surface capabilities during CreateSwapChain

    // Create the swapchain (no command buffer at this point)
    swapchain->CreateSwapChain(VK_NULL_HANDLE);

    // Create semaphores if required and publish outputs
    // Publish swapchain images (as typed outputs: VkImage array)
    uint32_t imageCount = swapchain->scPublicVars.swapChainImageCount;

    // Ensure output slot has space and set resource handles
    for (uint32_t i = 0; i < imageCount; ++i) {
        // Create a Resource to hold the VkImage
        // Use RenderGraph's resource system: create a new Resource and attach
        // We'll use NodeInstance::SetOutput via TypedNode::SetOutput which expects a handle type
        SetOutput(SwapChainNodeConfig::SWAPCHAIN_IMAGES, i, swapchain->scPublicVars.colorBuffers[i].image);
    }

    // Create semaphores and publish optional outputs
    // Image available semaphore
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;

    VkResult res = vkCreateSemaphore(device->device, &semInfo, nullptr, &imageAvailable);
    if (res == VK_SUCCESS) {
        SetOutput(SwapChainNodeConfig::IMAGE_AVAILABLE_SEM, 0, imageAvailable);
    }

    res = vkCreateSemaphore(device->device, &semInfo, nullptr, &renderFinished);
    if (res == VK_SUCCESS) {
        SetOutput(SwapChainNodeConfig::RENDER_FINISHED_SEM, 0, renderFinished);
    }
}

void CreateSwapChainNode::Execute(VkCommandBuffer commandBuffer) {
    // No per-frame work for creation node; swapchain lives across frames
}

void CreateSwapChainNode::Cleanup() {
    // Destroy semaphores published in outputs
    VkSemaphore& semAvail = Out(SwapChainNodeConfig::IMAGE_AVAILABLE_SEM);
    VkSemaphore& semFinish = Out(SwapChainNodeConfig::RENDER_FINISHED_SEM);
    if (semAvail != VK_NULL_HANDLE) {
        vkDestroySemaphore(device->device, semAvail, nullptr);
        semAvail = VK_NULL_HANDLE;
    }
    if (semFinish != VK_NULL_HANDLE) {
        vkDestroySemaphore(device->device, semFinish, nullptr);
        semFinish = VK_NULL_HANDLE;
    }

    // Destroy swapchain and surface via wrapper
    if (swapchain) {
        swapchain->DestroySwapChain();
        swapchain->DestroySurface();
        swapchain.reset();
    }
}

} // namespace Vixen::RenderGraph
