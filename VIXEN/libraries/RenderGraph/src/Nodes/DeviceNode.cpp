#include "Headers.h"
#include "Nodes/DeviceNode.h"
#include "Core/RenderGraph.h"
#include "Message.h"
#include "MessageBus.h"
#include "Core/NodeLogging.h"
#include "ShaderModuleCacher.h"
#include "PipelineCacher.h"
#include "PipelineLayoutCacher.h"
#include "DescriptorSetLayoutCacher.h"
#include "TextureCacher.h"
#include "Memory/DirectAllocator.h"
#include "Memory/DeviceBudgetManager.h"
#include "Memory/BatchedUploader.h"
#include "Updates/BatchedUpdater.h"
extern std::vector<const char*> deviceExtensionNames;
extern std::vector<const char*> layerNames;

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// ============================================================================
// DeviceNodeType
// ============================================================================

std::unique_ptr<NodeInstance> DeviceNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<DeviceNode>(instanceName, const_cast<DeviceNodeType*>(this), nullptr);
}

// ============================================================================
// DeviceNode
// ============================================================================

DeviceNode::DeviceNode(
    const std::string& instanceName,
    NodeType* nodeType,
    VulkanDevice* device
)
    : TypedNode<DeviceNodeConfig>(instanceName, nodeType)
{
}

void DeviceNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[DeviceNode] Setup: Preparing device creation");

    // Note: VkInstance will be read from input during CompileImpl
    // Setup phase doesn't have access to input values yet

    // Use global extension/layer lists
    deviceExtensions = deviceExtensionNames;
    deviceLayers = layerNames;

    NODE_LOG_INFO("[DeviceNode] Requested " + std::to_string(deviceExtensions.size()) + " device extensions");
    NODE_LOG_INFO("[DeviceNode] Requested " + std::to_string(deviceLayers.size()) + " device layers");

    NODE_LOG_INFO("[DeviceNode] Setup complete");
}

void DeviceNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[DeviceNode] Compile: Creating Vulkan device");

    // Read VkInstance from input slot (dependency injection from InstanceNode)
    instance = ctx.In(DeviceNodeConfig::INSTANCE_IN);

    if (instance == VK_NULL_HANDLE) {
        NODE_LOG_ERROR("[DeviceNode] ERROR: VkInstance is null!");
        return;
    }

    NODE_LOG_INFO("[DeviceNode] Received VkInstance from InstanceNode: " +
                  std::to_string(reinterpret_cast<uint64_t>(instance)));

    // If device already exists, publish invalidation event before recreation
    if (vulkanDevice && vulkanDevice.get()) {
        auto* messageBus = GetOwningGraph()->GetMessageBus();

        // Create and publish device invalidation event
        // SenderID 0 = system/device (no specific sender node ID)
        auto invalidationEvent = std::make_unique<Vixen::EventBus::DeviceInvalidationEvent>(
            0,  // System sender
            vulkanDevice.get(),
            Vixen::EventBus::DeviceInvalidationEvent::Reason::DeviceRecompilation,
            "DeviceNode recompilation"
        );

        messageBus->Publish(std::move(invalidationEvent));
        NODE_LOG_INFO("[DeviceNode] Published device invalidation event (recompilation)");
    }

    // Get gpu_index parameter (default to 0 if not set)
    selectedGPUIndex = GetParameterValue<uint32_t>(
        DeviceNodeConfig::PARAM_GPU_INDEX,
        0  // Default: first GPU
    );

    // Enumerate all available physical devices
    EnumeratePhysicalDevices();

    // Select the physical device
    SelectPhysicalDevice();

    // Create logical device using VulkanDevice wrapper
    CreateLogicalDevice();

    // Publish device metadata for other systems to configure themselves
    PublishDeviceMetadata();

    // Register device with MainCacher to create device registry
    auto& mainCacher = GetOwningGraph()->GetMainCacher();
    auto& deviceRegistry = mainCacher.GetOrCreateDeviceRegistry(vulkanDevice.get());
    NODE_LOG_INFO("[DeviceNode] Registered device with MainCacher");

    // Create memory allocator and budget manager for THIS device
    // Must be after GetOrCreateDeviceRegistry so we can set it on the specific registry
    CreateDeviceBudgetManager(deviceRegistry);

    // NOTE: Cache loading will happen AFTER graph compilation completes
    // This ensures all nodes have registered their cachers first

    // Store outputs - output VulkanDevice pointer (contains device, gpu, memory properties, queues, etc.)
    ctx.Out(DeviceNodeConfig::VULKAN_DEVICE_OUT, vulkanDevice.get());
    // Passthrough VkInstance to downstream nodes
    ctx.Out(DeviceNodeConfig::INSTANCE_OUT, instance);

    NODE_LOG_INFO("[DeviceNode] Compile complete - VulkanDevice* and instance stored in outputs");
}

void DeviceNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // DeviceNode doesn't record commands - it just provides the device
}

void DeviceNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[DeviceNode] Cleanup: Cleaning device-dependent caches");

    // Cleanup all device-dependent caches BEFORE destroying the device
    // DeviceNode manages the device lifecycle, so it's responsible for cache cleanup
    if (vulkanDevice) {
        auto& mainCacher = GetOwningGraph()->GetMainCacher();
        mainCacher.ClearDeviceCaches(vulkanDevice.get());
        NODE_LOG_INFO("[DeviceNode] Cleared device-dependent caches for device");
    }

    // VulkanDevice destructor handles device destruction
    vulkanDevice.reset();

    availableGPUs.clear();
    instance = VK_NULL_HANDLE;

    NODE_LOG_INFO("[DeviceNode] Cleanup complete");
}

// ============================================================================
// Helper Methods
// ============================================================================

void DeviceNode::EnumeratePhysicalDevices() {
    uint32_t deviceCount = 0;

    // Get device count
    VkResult result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (result != VK_SUCCESS) {
        // log error

        NODE_LOG_ERROR("[DeviceNode] ERROR: Failed to enumerate physical device count: " + std::to_string(result));
        return;
    }

    if (deviceCount == 0) {
        NODE_LOG_ERROR("[DeviceNode] ERROR: No Vulkan-capable GPUs found!");
        return;
    }

    NODE_LOG_INFO("[DeviceNode] Found " + std::to_string(deviceCount) + " physical device(s)");

    // Get all devices
    availableGPUs.resize(deviceCount);
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, availableGPUs.data());
    if (result != VK_SUCCESS) {
        NODE_LOG_ERROR("[DeviceNode] ERROR: Failed to enumerate physical devices: " + std::to_string(result));
        availableGPUs.clear();
        return;
    }

    // Log all available GPUs
    for (uint32_t i = 0; i < deviceCount; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(availableGPUs[i], &props);
        NODE_LOG_INFO("[DeviceNode]   GPU " + std::to_string(i) + ": " + props.deviceName);
    }
}

void DeviceNode::SelectPhysicalDevice() {
    // Validate selected index
    if (selectedGPUIndex >= availableGPUs.size()) {
        NODE_LOG_WARNING("[DeviceNode] WARNING: Requested GPU index " + std::to_string(selectedGPUIndex)
                    + " but only " + std::to_string(availableGPUs.size()) + " GPUs available. Using GPU 0.");
        selectedGPUIndex = 0;
    }

    VkPhysicalDevice selectedGPU = availableGPUs[selectedGPUIndex];

    // Log selected device
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(selectedGPU, &props);
    NODE_LOG_INFO("[DeviceNode] Selected GPU " + std::to_string(selectedGPUIndex) + ": " + props.deviceName);
    NODE_LOG_INFO("[DeviceNode]   Vendor ID: 0x" + std::to_string(props.vendorID));
    NODE_LOG_INFO("[DeviceNode]   Device ID: 0x" + std::to_string(props.deviceID));
    NODE_LOG_INFO("[DeviceNode]   Driver Version: " + std::to_string(props.driverVersion));
    NODE_LOG_INFO("[DeviceNode]   API Version: " + std::to_string(VK_VERSION_MAJOR(props.apiVersion)) + "."
              + std::to_string(VK_VERSION_MINOR(props.apiVersion)) + "."
              + std::to_string(VK_VERSION_PATCH(props.apiVersion)));
}

void DeviceNode::CreateLogicalDevice() {
    VkPhysicalDevice selectedGPU = availableGPUs[selectedGPUIndex];
    selectedPhysicalDevice = selectedGPU;  // Store for later use

    // Create VulkanDevice wrapper - pass pointer to our member variable
    vulkanDevice = std::make_unique<VulkanDevice>(&selectedPhysicalDevice);

    // Query queue families and properties
    vulkanDevice->GetPhysicalDeviceQueuesAndProperties();

    // Get graphics queue handle
    auto queueResult = vulkanDevice->GetGraphicsQueueHandle();
    if (!queueResult) {
        NODE_LOG_ERROR("[DeviceNode] ERROR: Failed to find graphics queue: " + queueResult.error().toString());
        return;
    }

    NODE_LOG_INFO("[DeviceNode] Graphics queue family index: " + std::to_string(queueResult.value()));

    // Get physical device properties and memory properties
    vkGetPhysicalDeviceProperties(selectedGPU, &vulkanDevice->gpuProperties);
    vkGetPhysicalDeviceMemoryProperties(selectedGPU, &vulkanDevice->gpuMemoryProperties);

    NODE_LOG_INFO("[DeviceNode] Memory heaps: " + std::to_string(vulkanDevice->gpuMemoryProperties.memoryHeapCount));
    NODE_LOG_INFO("[DeviceNode] Memory types: " + std::to_string(vulkanDevice->gpuMemoryProperties.memoryTypeCount));

    // Enumerate all available device extensions
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(selectedGPU, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availableExts(extCount);
    vkEnumerateDeviceExtensionProperties(selectedGPU, nullptr, &extCount, availableExts.data());

    NODE_LOG_INFO("[DeviceNode] Found " + std::to_string(extCount) + " available device extensions");

    auto hasExt = [&availableExts](const char* name) {
        for (const auto& ext : availableExts) {
            if (strcmp(ext.extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    };

    // Validate base device extensions - only enable those that are available
    std::vector<const char*> allExtensions;
    allExtensions.reserve(deviceExtensions.size());

    for (const char* requestedExt : deviceExtensions) {
        if (hasExt(requestedExt)) {
            allExtensions.push_back(requestedExt);
            NODE_LOG_DEBUG("[DeviceNode]   ✓ " + std::string(requestedExt) + " (available)");
        } else {
            NODE_LOG_WARNING("[DeviceNode]   ✗ " + std::string(requestedExt) + " (NOT AVAILABLE - skipping)");
        }
    }

    NODE_LOG_INFO("[DeviceNode] Validated " + std::to_string(allExtensions.size()) + " base device extensions");

    // Phase K: Auto-enable RTX extensions if available
    auto rtxExtensions = VulkanDevice::GetRTXExtensions();

    bool rtxAvailable = true;
    for (const auto& rtxExt : rtxExtensions) {
        if (!hasExt(rtxExt)) {
            rtxAvailable = false;
            NODE_LOG_INFO("[DeviceNode] RTX extension not available: " + std::string(rtxExt));
            break;
        }
    }

    if (rtxAvailable) {
        NODE_LOG_INFO("[DeviceNode] RTX extensions available - enabling hardware ray tracing");
        for (const auto& rtxExt : rtxExtensions) {
            // Avoid duplicates
            bool alreadyAdded = false;
            for (const auto& existingExt : allExtensions) {
                if (strcmp(existingExt, rtxExt) == 0) {
                    alreadyAdded = true;
                    break;
                }
            }
            if (!alreadyAdded) {
                allExtensions.push_back(rtxExt);
                NODE_LOG_INFO("[DeviceNode]   + " + std::string(rtxExt));
            }
        }
    } else {
        NODE_LOG_INFO("[DeviceNode] RTX extensions not available - hardware RT disabled");
    }

    // Create logical device with all extensions
    auto createResult = vulkanDevice->CreateDevice(deviceLayers, allExtensions);
    if (!createResult) {
        NODE_LOG_ERROR("[DeviceNode] ERROR: Failed to create logical device: " + createResult.error().toString());
        return;
    }

    // Get device queue
    vulkanDevice->GetDeviceQueue();

    NODE_LOG_INFO("[DeviceNode] Logical device created successfully");
    NODE_LOG_INFO("[DeviceNode] Device handle: " + std::to_string(reinterpret_cast<uint64_t>(vulkanDevice->device)));
    NODE_LOG_INFO("[DeviceNode] Queue handle: " + std::to_string(reinterpret_cast<uint64_t>(vulkanDevice->queue)));

    if (vulkanDevice->IsRTXEnabled()) {
        NODE_LOG_INFO("[DeviceNode] RTX enabled: YES");
        const auto& caps = vulkanDevice->GetRTXCapabilities();
        NODE_LOG_INFO("[DeviceNode]   Max ray recursion: " + std::to_string(caps.maxRayRecursionDepth));
        NODE_LOG_INFO("[DeviceNode]   Max geometry count: " + std::to_string(caps.maxGeometryCount));
    } else {
        NODE_LOG_INFO("[DeviceNode] RTX enabled: NO");
    }
}

void DeviceNode::PublishDeviceMetadata() {
    if (!vulkanDevice) {
        NODE_LOG_WARNING("[DeviceNode] Cannot publish metadata - device not created");
        return;
    }

    auto* messageBus = GetOwningGraph()->GetMessageBus();
    if (!messageBus) {
        NODE_LOG_WARNING("[DeviceNode] Cannot publish metadata - no message bus");
        return;
    }

    NODE_LOG_INFO("[DeviceNode] Gathering metadata for all " + std::to_string(availableGPUs.size()) + " detected devices...");

    // Helper lambda to convert Vulkan version to SPIR-V version
    auto GetMaxSpirvVersion = [](uint32_t apiVersion) -> uint32_t {
        uint32_t vulkanMajor = VK_VERSION_MAJOR(apiVersion);
        uint32_t vulkanMinor = VK_VERSION_MINOR(apiVersion);

        uint32_t spirvMajor = 1;
        uint32_t spirvMinor = 0;

        if (vulkanMajor == 1) {
            if (vulkanMinor == 0) spirvMinor = 0;       // Vulkan 1.0 -> SPIR-V 1.0
            else if (vulkanMinor == 1) spirvMinor = 3;  // Vulkan 1.1 -> SPIR-V 1.3
            else if (vulkanMinor == 2) spirvMinor = 5;  // Vulkan 1.2 -> SPIR-V 1.5
            else spirvMinor = 6;                         // Vulkan 1.3+ -> SPIR-V 1.6
        }

        return (spirvMajor << 16) | (spirvMinor << 8);
    };

    // Collect metadata for ALL available devices
    std::vector<Vixen::EventBus::DeviceInfo> deviceInfos;
    deviceInfos.reserve(availableGPUs.size());

    for (uint32_t i = 0; i < availableGPUs.size(); ++i) {
        VkPhysicalDevice physicalDevice = availableGPUs[i];

        // Query properties for this device
        VkPhysicalDeviceProperties deviceProps{};
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProps);
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

        // Calculate memory totals
        uint64_t dedicatedMemoryMB = 0;
        uint64_t sharedMemoryMB = 0;

        for (uint32_t j = 0; j < memProps.memoryHeapCount; ++j) {
            const auto& heap = memProps.memoryHeaps[j];
            uint64_t heapSizeMB = heap.size / (1024 * 1024);

            if (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                dedicatedMemoryMB += heapSizeMB;
            } else {
                sharedMemoryMB += heapSizeMB;
            }
        }

        // Create DeviceInfo
        Vixen::EventBus::DeviceInfo info;
        info.vulkanApiVersion = deviceProps.apiVersion;
        info.maxSpirvVersion = GetMaxSpirvVersion(deviceProps.apiVersion);
        info.dedicatedMemoryMB = dedicatedMemoryMB;
        info.sharedMemoryMB = sharedMemoryMB;
        info.deviceName = deviceProps.deviceName;
        info.vendorID = deviceProps.vendorID;
        info.deviceID = deviceProps.deviceID;
        info.isDiscreteGPU = (deviceProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
        info.deviceIndex = i;

        deviceInfos.push_back(info);

        // Log each device
        uint32_t vulkanMajor = VK_VERSION_MAJOR(deviceProps.apiVersion);
        uint32_t vulkanMinor = VK_VERSION_MINOR(deviceProps.apiVersion);
        uint32_t spirvMajor = (info.maxSpirvVersion >> 16) & 0xFF;
        uint32_t spirvMinor = (info.maxSpirvVersion >> 8) & 0xFF;

        NODE_LOG_INFO("  [" + std::to_string(i) + "] " + std::string(deviceProps.deviceName));
        NODE_LOG_INFO("      Vulkan: " + std::to_string(vulkanMajor) + "." + std::to_string(vulkanMinor) +
                      " | SPIR-V: " + std::to_string(spirvMajor) + "." + std::to_string(spirvMinor) +
                      " | Memory: " + std::to_string(dedicatedMemoryMB) + " MB" +
                      (info.isDiscreteGPU ? " (Discrete)" : " (Integrated)"));
    }

    // Create and publish metadata event with ALL devices
    auto metadataEvent = std::make_unique<Vixen::EventBus::DeviceMetadataEvent>(
        0,  // System sender
        std::move(deviceInfos),
        selectedGPUIndex,
        vulkanDevice.get()
    );

    NODE_LOG_INFO("[DeviceNode] Selected device index: " + std::to_string(selectedGPUIndex));
    NODE_LOG_INFO("[DeviceNode] Publishing metadata for " + std::to_string(metadataEvent->availableDevices.size()) + " devices");

    messageBus->Publish(std::move(metadataEvent));

    NODE_LOG_INFO("[DeviceNode] Device metadata published successfully");
}

void DeviceNode::CreateDeviceBudgetManager(CashSystem::DeviceRegistry& deviceRegistry) {
    if (!vulkanDevice || !vulkanDevice->device) {
        NODE_LOG_WARNING("[DeviceNode] Cannot create budget manager - device not created");
        return;
    }

    // Create DirectAllocator for THIS specific device (simple allocator, no VMA)
    auto allocator = std::make_shared<ResourceManagement::DirectAllocator>(
        *vulkanDevice->gpu,     // VkPhysicalDevice (dereference pointer)
        vulkanDevice->device    // VkDevice
    );

    NODE_LOG_INFO("[DeviceNode] Created DirectAllocator for device: " +
                  std::string(vulkanDevice->gpuProperties.deviceName));

    // Query device VRAM to configure budget
    uint64_t deviceLocalMemory = 0;
    for (uint32_t i = 0; i < vulkanDevice->gpuMemoryProperties.memoryHeapCount; ++i) {
        const auto& heap = vulkanDevice->gpuMemoryProperties.memoryHeaps[i];
        if (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            deviceLocalMemory += heap.size;
        }
    }

    // Configure budget manager based on actual device memory
    // Use 90% of VRAM as budget to leave headroom for system/driver
    // Warning threshold at 80%
    ResourceManagement::DeviceBudgetManager::Config budgetConfig{};
    budgetConfig.deviceMemoryBudget = static_cast<uint64_t>(deviceLocalMemory * 0.9);
    budgetConfig.deviceMemoryWarning = static_cast<uint64_t>(deviceLocalMemory * 0.8);
    budgetConfig.stagingQuota = std::min(
        static_cast<uint64_t>(256 * 1024 * 1024),  // Max 256 MB
        deviceLocalMemory / 16  // Or 6.25% of VRAM if smaller
    );
    budgetConfig.strictBudget = false;  // Allow over-budget (warn only)

    NODE_LOG_INFO("[DeviceNode] Device VRAM: " + std::to_string(deviceLocalMemory / (1024 * 1024)) + " MB");
    NODE_LOG_INFO("[DeviceNode] Budget: " + std::to_string(budgetConfig.deviceMemoryBudget / (1024 * 1024)) + " MB (90%)");
    NODE_LOG_INFO("[DeviceNode] Warning threshold: " + std::to_string(budgetConfig.deviceMemoryWarning / (1024 * 1024)) + " MB (80%)");

    // Create DeviceBudgetManager wrapping the allocator for THIS device
    auto budgetManager = std::make_shared<ResourceManagement::DeviceBudgetManager>(
        allocator,
        *vulkanDevice->gpu,     // VkPhysicalDevice (dereference pointer)
        budgetConfig
    );

    NODE_LOG_INFO("[DeviceNode] Created DeviceBudgetManager with staging quota: " +
                  std::to_string(budgetConfig.stagingQuota / (1024 * 1024)) + " MB");

    // Store budget manager in RenderGraph (for lifetime management)
    // This also propagates to MainCacher for backwards compatibility
    GetOwningGraph()->SetDeviceBudgetManager(budgetManager);

    // Set budget manager on THIS specific DeviceRegistry
    // This ensures per-device budget tracking for multi-GPU scenarios
    deviceRegistry.SetBudgetManager(budgetManager.get());

    // Sprint 5 Phase 2.5.3: Set budget manager on VulkanDevice for centralized access
    vulkanDevice->SetBudgetManager(budgetManager);

    NODE_LOG_INFO("[DeviceNode] Budget manager connected to DeviceRegistry and VulkanDevice");

    // Sprint 5 Phase 2.5.3: Create BatchedUploader and set on VulkanDevice
    // This provides the centralized device.Upload() API for all cachers
    ResourceManagement::BatchedUploader::Config uploaderConfig;
    uploaderConfig.maxPendingUploads = 64;
    uploaderConfig.flushDeadline = std::chrono::milliseconds{16};  // 1 frame at 60fps

    auto uploader = std::make_unique<ResourceManagement::BatchedUploader>(
        vulkanDevice->device,
        vulkanDevice->queue,
        vulkanDevice->graphicsQueueIndex,
        budgetManager.get(),
        uploaderConfig
    );

    // Sprint 5 Phase 4.1: Pre-warm staging buffer pool to avoid first-frame allocations
    uploader->PreWarmDefaults();
    NODE_LOG_INFO("[DeviceNode] StagingBufferPool pre-warmed with default sizes");

    vulkanDevice->SetUploader(std::move(uploader));
    NODE_LOG_INFO("[DeviceNode] BatchedUploader created and connected to VulkanDevice");

    // Sprint 5 Phase 3.5: Create BatchedUpdater for per-frame GPU updates (TLAS rebuilds, etc.)
    // Initial frame count = 3 (typical), will be resized by SwapChainNode if different
    constexpr uint32_t initialFrameCount = 3;
    ResourceManagement::BatchedUpdater::Config updaterConfig;
    updaterConfig.sortByPriority = true;
    updaterConfig.insertBarriers = true;

    auto updater = std::make_unique<ResourceManagement::BatchedUpdater>(
        initialFrameCount,
        updaterConfig
    );

    vulkanDevice->SetUpdater(std::move(updater));
    NODE_LOG_INFO("[DeviceNode] BatchedUpdater created and connected to VulkanDevice");

    // Log initial stats
    auto stats = budgetManager->GetStats();
    NODE_LOG_INFO("[DeviceNode] Device memory: " +
                  std::to_string(stats.totalDeviceMemory / (1024 * 1024)) + " MB total, " +
                  std::to_string(stats.availableDeviceMemory / (1024 * 1024)) + " MB available");
}

} // namespace Vixen::RenderGraph