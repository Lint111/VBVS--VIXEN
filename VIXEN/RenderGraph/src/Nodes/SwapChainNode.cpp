#include "Nodes/SwapChainNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"

namespace Vixen::RenderGraph {

// ====== SwapChainNodeType ======

SwapChainNodeType::SwapChainNodeType(const std::string& typeName) : NodeType(typeName) {
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics; // Uses graphics queue for presentation
    supportsInstancing = false; // Only one swapchain per render graph
    maxInstances = 1;

    // Populate schema from config
    SwapChainNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 32 * 1024 * 1024; // ~32MB for swapchain images
    workloadMetrics.estimatedComputeCost = 0.2f;
    workloadMetrics.estimatedBandwidthCost = 0.1f;
    workloadMetrics.canRunInParallel = false; // Swapchain operations are sequential
}

std::unique_ptr<NodeInstance> SwapChainNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<SwapChainNode>(
        instanceName,
        const_cast<NodeType*>(static_cast<const NodeType*>(this))
    );
}

// ====== SwapChainNode ======

SwapChainNode::SwapChainNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<SwapChainNodeConfig>(instanceName, nodeType)
{
}

SwapChainNode::~SwapChainNode() {
    Cleanup();
}

void SwapChainNode::Setup() {
    // Setup is called after Compile()
    // Swapchain is already created, outputs already set
    // This method creates synchronization primitives

    if (!swapChainWrapper) {
        std::string errorMsg = "SwapChainNode::Setup - swapchain wrapper is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    if (swapChainWrapper->scPublicVars.swapChainImageCount == 0) {
        std::string errorMsg = "SwapChainNode::Setup - swapchain has no images";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Create semaphores for image acquisition
    // We need one per swapchain image for proper frame pacing
    uint32_t imageCount = swapChainWrapper->scPublicVars.swapChainImageCount;
    imageAvailableSemaphores.resize(imageCount);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < imageCount; i++) {
        VkResult result = vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]);
        if (result != VK_SUCCESS) {
            std::string errorMsg = "SwapChainNode::Setup - Failed to create image available semaphore";
            NODE_LOG_ERROR(errorMsg);
            throw std::runtime_error(errorMsg);
        }
    }

    currentFrame = 0;
}

void SwapChainNode::Compile() {
    // Get input resources from connected nodes
    HWND hwnd = In(SwapChainNodeConfig::HWND);
    HINSTANCE hinstance = In(SwapChainNodeConfig::HINSTANCE);
    width = In(SwapChainNodeConfig::WIDTH);
    height = In(SwapChainNodeConfig::HEIGHT);
    VkInstance instance = In(SwapChainNodeConfig::INSTANCE);
    VkPhysicalDevice physicalDevice = In(SwapChainNodeConfig::PHYSICAL_DEVICE);
    VkDevice logicalDevice = In(SwapChainNodeConfig::DEVICE);

    // Validate inputs
    if (width == 0 || height == 0) {
        std::string errorMsg = "SwapChainNode: width and height must be greater than 0";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    if (hwnd == nullptr) {
        std::string errorMsg = "SwapChainNode: HWND is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    if (hinstance == nullptr) {
        std::string errorMsg = "SwapChainNode: HINSTANCE is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    if (instance == VK_NULL_HANDLE) {
        std::string errorMsg = "SwapChainNode: VkInstance is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    if (physicalDevice == VK_NULL_HANDLE) {
        std::string errorMsg = "SwapChainNode: VkPhysicalDevice is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    if (logicalDevice == VK_NULL_HANDLE) {
        std::string errorMsg = "SwapChainNode: VkDevice is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Create swapchain wrapper if not set
    if (swapChainWrapper == nullptr) {
        std::string errorMsg = "SwapChainNode: swapchain wrapper not set - call SetSwapChainWrapper() before Compile()";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // === SWAPCHAIN CREATION PROCESS ===

    // Step 1: Load swapchain extension function pointers
    VkResult result = swapChainWrapper->CreateSwapChainExtensions(instance, logicalDevice);
    if (result != VK_SUCCESS) {
        std::string errorMsg = "SwapChainNode: Failed to load swapchain extensions";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Step 2: Create the platform-specific surface
    result = swapChainWrapper->CreateSurface(instance, hwnd, hinstance);
    if (result != VK_SUCCESS) {
        std::string errorMsg = "SwapChainNode: Failed to create VkSurfaceKHR";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Step 3: Get supported surface formats
    swapChainWrapper->GetSupportedFormats(physicalDevice);

    // Step 4: Verify graphics queue supports presentation
    // Get queue family properties
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueProps(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueProps.data());

    uint32_t graphicsQueueIndex = swapChainWrapper->GetGraphicsQueueWithPresentationSupport(
        physicalDevice,
        queueFamilyCount,
        queueProps
    );

    if (graphicsQueueIndex == UINT32_MAX) {
        std::string errorMsg = "SwapChainNode: No queue family supports both graphics and presentation";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Step 5: Query surface capabilities and present modes
    swapChainWrapper->GetSurfaceCapabilitiesAndPresentMode(physicalDevice, width, height);

    // Step 6: Select optimal present mode
    swapChainWrapper->ManagePresentMode();

    // Step 7: Create the swapchain with configured settings
    swapChainWrapper->CreateSwapChainColorImages(logicalDevice);

    // Step 8: Create image views for each swapchain image
    // Note: We pass VK_NULL_HANDLE for command buffer since image views don't need it
    VkCommandBuffer dummyCmd = VK_NULL_HANDLE;
    swapChainWrapper->CreateColorImageView(logicalDevice, dummyCmd);

    // Verify swapchain was created successfully
    if (swapChainWrapper->scPublicVars.swapChain == VK_NULL_HANDLE) {
        std::string errorMsg = "SwapChainNode: Swapchain handle is null after creation";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    if (swapChainWrapper->scPublicVars.swapChainImageCount == 0) {
        std::string errorMsg = "SwapChainNode: No swapchain images were created";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Set swapchain extent for external reference
    swapChainWrapper->SetSwapChainExtent(width, height);

    // === SET ALL OUTPUTS ===

    // Output 0: ALL swapchain images as an array
    const uint32_t imageCount = swapChainWrapper->scPublicVars.swapChainImageCount;
    for (uint32_t i = 0; i < imageCount; i++) {
        SetOutput(SwapChainNodeConfig::SWAPCHAIN_IMAGES, i, swapChainWrapper->scPublicVars.colorBuffers[i].image);
    }

    // Output 1: Swapchain handle (NEW VARIANT API)
    Out(SwapChainNodeConfig::SWAPCHAIN_HANDLE, swapChainWrapper->scPublicVars.swapChain);

    // Output 2: Pointer to public swapchain variables (NEW VARIANT API)
    Out(SwapChainNodeConfig::SWAPCHAIN_PUBLIC, &swapChainWrapper->scPublicVars);

    // Output 3-4: Width and height as typed primitives
    Out(SwapChainNodeConfig::WIDTH_OUT, width);
    Out(SwapChainNodeConfig::HEIGHT_OUT, height);
}

void SwapChainNode::Execute(VkCommandBuffer commandBuffer) {
    // Acquire next swapchain image
    const uint32_t frameIndex = currentFrame % imageAvailableSemaphores.size();
    currentImageIndex = AcquireNextImage(imageAvailableSemaphores[frameIndex]);

    // Store outputs for downstream nodes
    // These are accessed via GetCurrentImageIndex() and GetImageAvailableSemaphore()
    currentFrame++;
}

void SwapChainNode::Cleanup() {
    // Destroy semaphores
    for (auto& semaphore : imageAvailableSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device->device, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }
    imageAvailableSemaphores.clear();

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

SwapChainPublicVariables* SwapChainNode::GetSwapchainPublic() const {
    if (swapChainWrapper) return &swapChainWrapper->scPublicVars;
    return nullptr;
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
        std::string errorMsg = "SwapChainNode: swapchain wrapper not set";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
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
        std::string errorMsg = "SwapChainNode: failed to acquire swapchain image";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    return currentImageIndex;
}

void SwapChainNode::Recreate(uint32_t newWidth, uint32_t newHeight) {
    if (!swapChainWrapper) {
        std::string errorMsg = "SwapChainNode: swapchain wrapper not set";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    width = newWidth;
    height = newHeight;

    // Destroy and recreate swapchain
    swapChainWrapper->DestroySwapChain(device->device);
    swapChainWrapper->SetSwapChainExtent(newWidth, newHeight);

    // Note: Swapchain recreation would need full orchestration
    // This should be coordinated with the render graph execution
}

} // namespace Vixen::RenderGraph
