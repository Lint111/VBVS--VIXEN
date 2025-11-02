#include "Nodes/SwapChainNode.h"
#include "Nodes/FrameSyncNodeConfig.h"  // Phase 0.4: For CURRENT_FRAME_INDEX input
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "EventTypes/RenderGraphEvents.h"
#include "EventBus/Message.h"

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

void SwapChainNode::SetupImpl() {
    SetDevice(In(SwapChainNodeConfig::VULKAN_DEVICE_IN));

    if (GetDevice() == nullptr) {
        std::string errorMsg = "SwapChainNode: VulkanDevice input is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Subscribe to window resize events
    if (GetMessageBus()) {
        SubscribeToMessage(
            EventTypes::WindowResizedMessage::TYPE,
            [this](const EventBus::BaseEventMessage& msg) -> bool {
                // Mark this node for recompilation
                std::cout << "[SwapChainNode] Received WindowResizedMessage - marking self for recompilation" << std::endl;
                MarkNeedsRecompile();
                return true; // Message handled
            }
        );
    }

    if (!swapChainWrapper) {
        // Create a new VulkanSwapChain wrapper
        swapChainWrapper = new VulkanSwapChain();
		swapChainWrapper->Initialize();
        NODE_LOG_INFO("SwapChainNode::Setup - Created swapchain wrapper");
    }

    currentFrame = 0;
}

void SwapChainNode::CompileImpl() {
    std::cout << "[SwapChainNode::Compile] START" << std::endl;

    // Publish render pause starting event
    if (GetMessageBus()) {
        GetMessageBus()->Publish(
            std::make_unique<EventTypes::RenderPauseEvent>(
                instanceId,
                EventTypes::RenderPauseEvent::Reason::SwapChainRecreation,
                EventTypes::RenderPauseEvent::Action::PAUSE_START
            )
        );
    }

    // Get input resources from connected nodes
    std::cout << "[SwapChainNode::Compile] Reading HWND..." << std::endl;
    HWND hwnd = In(SwapChainNodeConfig::HWND);
    std::cout << "[SwapChainNode::Compile] Reading HINSTANCE..." << std::endl;
    HINSTANCE hinstance = In(SwapChainNodeConfig::HINSTANCE);
    std::cout << "[SwapChainNode::Compile] Reading WIDTH..." << std::endl;
    width = In(SwapChainNodeConfig::WIDTH);
    std::cout << "[SwapChainNode::Compile] WIDTH = " << width << std::endl;
    std::cout << "[SwapChainNode::Compile] Reading HEIGHT..." << std::endl;
    height = In(SwapChainNodeConfig::HEIGHT);
    std::cout << "[SwapChainNode::Compile] HEIGHT = " << height << std::endl;
    std::cout << "[SwapChainNode::Compile] Reading INSTANCE..." << std::endl;
    VkInstance instance = In(SwapChainNodeConfig::INSTANCE);

    VkResult result = VK_SUCCESS;  // Declare result variable for error checking


    // Validate inputs
    std::cout << "[SwapChainNode::Compile] Validating dimensions..." << std::endl;
    if (width == 0 || height == 0) {
        std::string errorMsg = "SwapChainNode: width and height must be greater than 0 (got " + 
                               std::to_string(width) + "x" + std::to_string(height) + ")";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    
    std::cout << "[SwapChainNode::Compile] Validating HWND..." << std::endl;
    if (hwnd == nullptr) {
        std::string errorMsg = "SwapChainNode: HWND is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    
    std::cout << "[SwapChainNode::Compile] Validating HINSTANCE..." << std::endl;
    if (hinstance == nullptr) {
        std::string errorMsg = "SwapChainNode: HINSTANCE is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    
    std::cout << "[SwapChainNode::Compile] Validating VkInstance..." << std::endl;
    if (instance == VK_NULL_HANDLE) {
        std::string errorMsg = "SwapChainNode: VkInstance is null";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    std::cout << "[SwapChainNode::Compile] Checking swapchain wrapper..." << std::endl;
    if (swapChainWrapper == nullptr) {
        std::string errorMsg = "SwapChainNode: swapchain wrapper not set - call SetSwapChainWrapper() before Compile()";
        std::cout << "[SwapChainNode::Compile] ERROR: " << errorMsg << std::endl;
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    std::cout << "[SwapChainNode::Compile] Swapchain wrapper OK" << std::endl;

    // === SWAPCHAIN CREATION PROCESS ===

    // Step 1: Load swapchain extension function pointers
    std::cout << "[SwapChainNode::Compile] Loading swapchain extensions..." << std::endl;
    std::cout << "[SwapChainNode::Compile] Instance handle: " << std::hex << instance << std::dec << std::endl;

    result = swapChainWrapper->CreateSwapChainExtensions(instance, GetDevice()->device);
    if (result != VK_SUCCESS) {
        std::string errorMsg = "SwapChainNode: Failed to load swapchain extension function pointers";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    std::cout << "[SwapChainNode::Compile] Extension function pointers loaded successfully" << std::endl;

    // Step 2: Create the platform-specific surface
    // Note: Old resources were already destroyed in CleanupImpl() before Setup() created fresh wrapper
    result = swapChainWrapper->CreateSurface(instance, hwnd, hinstance);
    if (result != VK_SUCCESS) {
        std::string errorMsg = "SwapChainNode: Failed to create VkSurfaceKHR";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Step 3: Get supported surface formats
    swapChainWrapper->GetSupportedFormats(*GetDevice()->gpu);

    auto graphicsQueueIndex = GetDevice()->GetGraphicsQueueHandle();


    if (!graphicsQueueIndex.has_value()) {
        std::string errorMsg = "SwapChainNode: No queue family supports both graphics and presentation";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Step 5: Query surface capabilities and present modes
    swapChainWrapper->GetSurfaceCapabilitiesAndPresentMode(*GetDevice()->gpu, width, height);

    // Step 6: Select optimal present mode
    swapChainWrapper->ManagePresentMode();

    // Step 7: Create the swapchain with configured settings
    swapChainWrapper->CreateSwapChainColorImages(GetDevice()->device);

    // Step 8: Create image views for each swapchain image
    // Note: We pass VK_NULL_HANDLE for command buffer since image views don't need it
    VkCommandBuffer dummyCmd = VK_NULL_HANDLE;
    swapChainWrapper->CreateColorImageView(GetDevice()->device, dummyCmd);

    // Verify colorBuffers were populated
    std::cout << "[SwapChainNode::Compile] ColorBuffers populated: "
              << swapChainWrapper->scPublicVars.colorBuffers.size() << " buffers" << std::endl;

    // Verify swapchain was created successfully
    if (swapChainWrapper->scPublicVars.swapChain == VK_NULL_HANDLE) {
        std::string errorMsg = "SwapChainNode: Swapchain handle is null after creation";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

	uint32_t imageCount = swapChainWrapper->scPublicVars.swapChainImageCount;

    if (imageCount == 0) {
        std::string errorMsg = "SwapChainNode: No swapchain images were created";
        NODE_LOG_ERROR(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Set swapchain extent for external reference
    swapChainWrapper->SetSwapChainExtent(width, height);

    // === SET ALL OUTPUTS ===

    // Output 1: Swapchain handle (NEW VARIANT API)
    Out(SwapChainNodeConfig::SWAPCHAIN_HANDLE, swapChainWrapper->scPublicVars.swapChain);

    // Output 2: Pointer to public swapchain variables (NEW VARIANT API)
    Out(SwapChainNodeConfig::SWAPCHAIN_PUBLIC, &swapChainWrapper->scPublicVars);

    // Phase 0.2: Semaphores now managed by FrameSyncNode (per-flight pattern)
    // No need to create per-swapchain-image semaphores anymore
    NODE_LOG_INFO("SwapChainNode::Compile - Swapchain created with " + std::to_string(imageCount) + " images");

    // Publish render pause ending event
    if (GetMessageBus()) {
        GetMessageBus()->Publish(
            std::make_unique<EventTypes::RenderPauseEvent>(
                instanceId,
                EventTypes::RenderPauseEvent::Reason::SwapChainRecreation,
                EventTypes::RenderPauseEvent::Action::PAUSE_END
            )
        );
    }
}

void SwapChainNode::ExecuteImpl() {
    // Phase 0.5: Get semaphore arrays from FrameSyncNode
    const VkSemaphore* imageAvailableSemaphores = In(SwapChainNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    const VkSemaphore* renderCompleteSemaphores = In(SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);

    if (!imageAvailableSemaphores || !renderCompleteSemaphores) {
        throw std::runtime_error("SwapChainNode: Semaphore arrays are null");
    }

    // Phase 0.7: Get present fences array (VK_EXT_swapchain_maintenance1)
    VkFenceVector presentFencesArray = In(SwapChainNodeConfig::PRESENT_FENCES_ARRAY);

    // Phase 0.6: CORRECT two-tier semaphore indexing (Vulkan guide pattern)
    // https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
    //
    // - imageAvailable: Indexed by FRAME INDEX (per-flight) - used for acquisition
    // - renderComplete: Indexed by IMAGE INDEX (per-image) - used by GeometryRender/Present
    //
    // This indexing is done in GeometryRenderNode after we know both frame and image indices.

    uint32_t currentFrameIndex = In(SwapChainNodeConfig::CURRENT_FRAME_INDEX);

    // Acquisition semaphore indexed by flight
    VkSemaphore acquireSemaphore = imageAvailableSemaphores[currentFrameIndex];

    // Phase 0.7: Wait for previous present to finish with the image we're about to acquire
    // Note: We don't know which image index we'll get yet, so we'll wait after acquisition
    // This is safe because presentFences are per-image and we wait before reusing that image

    // Acquire the next available image using the per-FLIGHT semaphore
    currentImageIndex = AcquireNextImage(acquireSemaphore);

    // Phase 0.7: Now that we know which image we got, wait for presentation to finish with it
    // This ensures the presentation engine has released the image before we start rendering to it
    if (currentImageIndex != UINT32_MAX && presentFencesArray != nullptr && !presentFencesArray->empty()) {
        VkFence presentFence = (*presentFencesArray)[currentImageIndex];
        if (presentFence != VK_NULL_HANDLE && GetDevice() != nullptr) {
            vkWaitForFences(GetDevice()->device, 1, &presentFence, VK_TRUE, UINT64_MAX);
            vkResetFences(GetDevice()->device, 1, &presentFence);
        }
    }

    // If swapchain is out of date, skip this frame
    if (currentImageIndex == UINT32_MAX) {
        NODE_LOG_INFO("SwapChainNode: Skipping frame due to out-of-date swapchain");
        return;
    }

    // Output the acquired image index
    Out(SwapChainNodeConfig::IMAGE_INDEX, currentImageIndex);

    NODE_LOG_INFO("Frame " + std::to_string(currentFrame) + ": acquired image " + std::to_string(currentImageIndex)
                  + ", frameIdx=" + std::to_string(currentFrameIndex)
                  + ", acquireSem[" + std::to_string(currentFrameIndex) + "]=0x" + std::to_string(reinterpret_cast<uint64_t>(acquireSemaphore))
                  + ", renderCompleteSem[" + std::to_string(currentImageIndex) + "]=0x" + std::to_string(reinterpret_cast<uint64_t>(renderCompleteSemaphores[currentImageIndex])));

    currentFrame++;
}

void SwapChainNode::CleanupImpl() {
    std::cout << "[SwapChainNode::CleanupImpl] Called" << std::endl;

    // Phase 0.2: Semaphores now managed by FrameSyncNode - no cleanup needed here

    // Cleanup swapchain wrapper if we own it
    if (swapChainWrapper) {
        // Get VkInstance for surface destruction
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;

        try {
            // Cleanup-time access only - config defines this as Dependency
            instance = In(SwapChainNodeConfig::INSTANCE);
        } catch (...) {
            // Instance might not be available during shutdown - that's ok
        }

        auto* dev = GetDevice();
        if (dev != nullptr) {
            device = dev->device;
        }

        // Destroy all Vulkan resources (wrapper loads extension pointers automatically if needed)
        swapChainWrapper->Destroy(device, instance);

        // Delete wrapper object
        delete swapChainWrapper;
        swapChainWrapper = nullptr;
    }
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

    auto* devicePtr = GetDevice();
    VkResult result = swapChainWrapper->fpAcquireNextImageKHR(
        devicePtr->device,
        swapChainWrapper->scPublicVars.swapChain,
        UINT64_MAX, // Timeout
        presentCompleteSemaphore,
        VK_NULL_HANDLE, // Fence
        &currentImageIndex
    );

    // Handle out-of-date swapchain
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        NODE_LOG_INFO("SwapChainNode: Swapchain out of date/suboptimal, marking for recreation");

        // Publish render pause event for swapchain recreation
        if (GetMessageBus()) {
            GetMessageBus()->Publish(
                std::make_unique<EventTypes::RenderPauseEvent>(
                    instanceId,
                    EventTypes::RenderPauseEvent::Reason::SwapChainRecreation,
                    EventTypes::RenderPauseEvent::Action::PAUSE_START
                )
            );
        }

        // Mark node as needing recompilation - will be handled in next Update()
        MarkNeedsRecompile();

        // Return invalid index to skip this frame
        // Recompilation will happen in the next Update() cycle
        return UINT32_MAX;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
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
    swapChainWrapper->DestroySwapChain(GetDevice()->device);
    swapChainWrapper->SetSwapChainExtent(newWidth, newHeight);

    // Note: Swapchain recreation would need full orchestration
    // This should be coordinated with the render graph execution
}

} // namespace Vixen::RenderGraph
