#include "VulkanDevice.h"

// Upload infrastructure (Sprint 5 Phase 2.5.3)
#include "Memory/BatchedUploader.h"
#include "Memory/DeviceBudgetManager.h"

// Update infrastructure (Sprint 5 Phase 3.5)
#include "Updates/BatchedUpdater.h"

// Allocation infrastructure (Sprint 5 Phase 3.5)
#include "Memory/IMemoryAllocator.h"

using namespace Vixen::Vulkan::Resources;

VulkanDevice::VulkanDevice(VkPhysicalDevice* physicalDevice) {
    gpu = physicalDevice;
}

VulkanDevice::~VulkanDevice() {
    DestroyDevice();
}

VulkanStatus VulkanDevice::CreateDevice(std::vector<const char*>& layers,
                                         std::vector<const char*>& extensions) {

    layerExtension.appRequestedLayerNames = layers;
    layerExtension.appRequestedExtensionNames = extensions;

    float queuePriorities[1] = { 0.0 };

    // Create the object information
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.queueFamilyIndex = graphicsQueueIndex;
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = queuePriorities;

    // Centralise non-concrete capability checks through the capability graph: register the
    // standard capability nodes and populate the physical device's supported features, so the
    // feature-enablement decisions below are gated via capabilityGraph_.IsCapabilityAvailable()
    // (same convention as device extensions) rather than ad-hoc inline queries.
    capabilityGraph_.BuildStandardCapabilities();
    capabilityGraph_.SetAvailableDeviceFeatures(QueryAvailableDeviceFeatures());  // AR#8: per-graph, was static

    VkPhysicalDeviceFeatures2 deviceFeatures2{};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    void** pNextChainEnd = &deviceFeatures2.pNext;

    // Enable swapchainMaintenance1 feature if extension is present

    std::vector<DeviceFeatureMapping> deviceExtentionMappings = {
        {
            VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
            sizeof(VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT)
        },
        {
            VK_KHR_MAINTENANCE_6_EXTENSION_NAME,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES_KHR,
            sizeof(VkPhysicalDeviceMaintenance6FeaturesKHR)
        },
        // RTX Extensions (Phase K)
        {
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
            sizeof(VkPhysicalDeviceAccelerationStructureFeaturesKHR)
        },
        {
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
            sizeof(VkPhysicalDeviceRayTracingPipelineFeaturesKHR)
        }
        // NOTE: buffer_device_address is intentionally NOT mapped here. It was promoted to
        // Vulkan 1.2 core, so it is enabled below via VkPhysicalDeviceVulkan12Features rather
        // than a standalone VkPhysicalDeviceBufferDeviceAddressFeatures struct -- the two are
        // mutually exclusive in one pNext chain (VUID-VkDeviceCreateInfo-pNext-02830).
    };

    for (auto& mapping : deviceExtentionMappings) {
        if (!HasExtension(extensions, mapping.extensionName)) {
            continue;
        }
        auto featureStruct = std::make_unique<uint8_t[]>(mapping.structSize);
        memset(featureStruct.get(), 0, mapping.structSize);

        VkBaseOutStructure* baseStruct = reinterpret_cast<VkBaseOutStructure*>(featureStruct.get());
        baseStruct->sType = mapping.structType;
        baseStruct->pNext = nullptr;

        if (mapping.structType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT) {
            VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT* swapchainFeatures = reinterpret_cast<VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT*>(featureStruct.get());
            swapchainFeatures->swapchainMaintenance1 = VK_TRUE;
        } else if (mapping.structType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES_KHR) {
            VkPhysicalDeviceMaintenance6FeaturesKHR* maintenance6Features = reinterpret_cast<VkPhysicalDeviceMaintenance6FeaturesKHR*>(featureStruct.get());
            maintenance6Features->maintenance6 = VK_TRUE;
        }
        // RTX feature enabling (Phase K)
        else if (mapping.structType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR) {
            VkPhysicalDeviceAccelerationStructureFeaturesKHR* asFeatures = reinterpret_cast<VkPhysicalDeviceAccelerationStructureFeaturesKHR*>(featureStruct.get());
            asFeatures->accelerationStructure = VK_TRUE;
        } else if (mapping.structType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR) {
            VkPhysicalDeviceRayTracingPipelineFeaturesKHR* rtFeatures = reinterpret_cast<VkPhysicalDeviceRayTracingPipelineFeaturesKHR*>(featureStruct.get());
            rtFeatures->rayTracingPipeline = VK_TRUE;
        }

        // Append to pNext chain
        pNextChainEnd = reinterpret_cast<void**>(AppendToPNext(pNextChainEnd, featureStruct.get()));

        // Store the unique_ptr to keep the memory alive
        deviceFeatureStorage.push_back(std::move(featureStruct));
    }

    // Enable the non-concrete timelineSemaphore feature, gated through the capability graph
    // (populated above from the physical device). The BatchedUploader needs it for timeline-
    // based upload synchronization; when unsupported it falls back to its non-timeline path
    // (BatchedUploader::useTimelineSemaphores_). Enabling it (when supported) also resolves the
    // "timelineSemaphore feature not enabled" validation error from vkCreateSemaphore(TIMELINE).
    // This local must outlive vkCreateDevice() below; it is scoped to this function.
    VkPhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    bool needVulkan12Features = false;
    if (capabilityGraph_.IsCapabilityAvailable("DeviceFeature:timelineSemaphore")) {
        vulkan12Features.timelineSemaphore = VK_TRUE;
        needVulkan12Features = true;
    } else {
        std::cerr << "[VulkanDevice] WARNING: timelineSemaphore not supported by this GPU - "
                     "uploads will use the non-timeline synchronization fallback" << std::endl;
    }
    // buffer_device_address was promoted to Vulkan 1.2 core; enable it through Vulkan12Features
    // (when the extension is present) rather than a standalone feature struct, which is illegal
    // alongside Vulkan12Features in one pNext chain (VUID-VkDeviceCreateInfo-pNext-02830).
    if (HasExtension(extensions, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)) {
        vulkan12Features.bufferDeviceAddress = VK_TRUE;
        needVulkan12Features = true;
    }
    if (needVulkan12Features) {
        pNextChainEnd = reinterpret_cast<void**>(AppendToPNext(pNextChainEnd, &vulkan12Features));
    }

    // synchronization2 is REQUIRED — the renderer records all GPU barriers via
    // vkCmdPipelineBarrier2KHR (ComputeDispatchNode, MultiDispatchNode, PassRecorder, and 6 more
    // call sites; see the per-device VulkanDevice::fpCmdPipelineBarrier2). Gated through the
    // capability graph like timelineSemaphore/bufferDeviceAddress; unlike those it is mandatory,
    // so a device that lacks it is a hard error (cf. shaderStorageImageWriteWithoutFormat below).
    //
    // Requested BOTH ways deliberately, not redundantly: the VkPhysicalDeviceVulkan13Features
    // struct below only takes effect when the negotiated apiVersion is >= 1.3 (a 1.2 driver
    // silently ignores it, even though vkGetPhysicalDeviceFeatures2 still reports the feature bit
    // true via the same query path a pre-1.3 driver uses for its extension-level struct) --
    // discovered via Mesa Dozen (WSL2's Vulkan-over-D3D12 driver): it reports apiVersion 1.2.318,
    // synchronization2=TRUE, and implements VK_KHR_synchronization2 as a genuine, working
    // extension (confirmed live: vkGetDeviceProcAddr("vkCmdPipelineBarrier2KHR") resolves to a
    // valid, callable pointer once the extension is in ppEnabledExtensionNames) -- it was VIXEN
    // never asking for it as an extension, not a driver bug, that left the promotion unresolved
    // and crashed the first render frame on a null vkCmdPipelineBarrier2 (bare core name, which a
    // 1.2 device's loader correctly returns null for per spec). Explicitly requesting the
    // extension makes this correct on 1.2-plus-extension drivers (Dozen today) and is a no-op
    // pNext addition (safely ignored) on genuine 1.3-core drivers.
    // This local must outlive vkCreateDevice() below; it is scoped to this function.
    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    if (!capabilityGraph_.IsCapabilityAvailable("DeviceFeature:synchronization2")) {
        throw std::runtime_error(
            "GPU does not support synchronization2 (Vulkan 1.3 core or VK_KHR_synchronization2 "
            "extension) - required: the renderer records all GPU barriers via "
            "vkCmdPipelineBarrier2KHR.");
    }
    vulkan13Features.synchronization2 = VK_TRUE;
    pNextChainEnd = reinterpret_cast<void**>(AppendToPNext(pNextChainEnd, &vulkan13Features));

    if (!HasExtension(extensions, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    }

    vkGetPhysicalDeviceFeatures(*gpu, &deviceFeatures);

    // Validate and enable device features
    // CRITICAL: shaderStorageImageWriteWithoutFormat is required for compute shaders
    if (!deviceFeatures.shaderStorageImageWriteWithoutFormat) {
        throw std::runtime_error(
            "GPU does not support shaderStorageImageWriteWithoutFormat - "
            "required for format-less storage image writes in compute shaders. "
            "This feature is unavailable on older integrated GPUs (Intel HD 4000-5000 era).");
    }
    deviceFeatures2.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

    // OPTIONAL: samplerAnisotropy - enable if supported, warn if not
    if (deviceFeatures.samplerAnisotropy) {
        deviceFeatures2.features.samplerAnisotropy = VK_TRUE;
    } else {
        // Log warning but continue - anisotropic filtering is optional
        // This may occur on very old hardware or emulated/virtualized GPUs
        std::cerr << "[VulkanDevice] WARNING: Anisotropic filtering not supported on this GPU - textures will use standard filtering" << std::endl;
        deviceFeatures2.features.samplerAnisotropy = VK_FALSE;
    }

    // Create the logical device representation
    VkDeviceCreateInfo deviceInfo = {};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &deviceFeatures2;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());

    deviceInfo.ppEnabledLayerNames = layers.size() ? layers.data() : nullptr;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    deviceInfo.ppEnabledExtensionNames = extensions.size() ? extensions.data() : nullptr;
    deviceInfo.pEnabledFeatures = nullptr;  // Must be NULL when using VkPhysicalDeviceFeatures2

    VK_CHECK(vkCreateDevice(*gpu, &deviceInfo, nullptr, &device), "Failed to create logical device");

    // Resolve THIS device's promoted-or-extension synchronization2 entry points (per-instance
    // members — see fpCmdPipelineBarrier2/fpQueueSubmit2 in VulkanDevice.h) via the KHR-suffixed
    // names -- correct whether the driver negotiated real 1.3 core (where core and KHR names alias
    // the same pointer) or is 1.2-plus-extension like Dozen (where only the KHR name resolves; the
    // bare core name's dispatch-table entry is null per spec on a non-1.3 apiVersion). A null
    // result here means the extensions.push_back() above didn't take -- e.g. HasExtension's
    // driver-side enumeration disagreeing with the request -- a genuine, unexpected environment
    // problem worth failing loudly on rather than segfaulting three frames into the first render.
    fpCmdPipelineBarrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(
        vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2KHR"));
    // Same extension bundle, same promotion gap, same resolution strategy -- vkQueueSubmit2 is
    // part of VK_KHR_synchronization2 alongside vkCmdPipelineBarrier2, not a separate capability.
    fpQueueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2KHR>(
        vkGetDeviceProcAddr(device, "vkQueueSubmit2KHR"));
    if (!fpCmdPipelineBarrier2 || !fpQueueSubmit2) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(*gpu, &props);
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
        throw std::runtime_error(
            std::string("GPU driver '") + props.deviceName + "' reports synchronization2 support "
            "but " + (!fpCmdPipelineBarrier2 ? "vkCmdPipelineBarrier2KHR" : "vkQueueSubmit2KHR") +
            " failed to resolve even with VK_KHR_synchronization2 requested as a device "
            "extension. The renderer requires a real synchronization2 implementation; there is "
            "no legacy vkCmdPipelineBarrier/vkQueueSubmit fallback path.");
    }

    // The capability graph was built before device creation (so feature enablement could be
    // gated through it). Now record the device extensions that were actually enabled, then
    // invalidate cached results so subsequent queries see the populated extension set.
    std::vector<std::string> extensionStrings;
    extensionStrings.reserve(extensions.size());
    for (const char* ext : extensions) {
        extensionStrings.emplace_back(ext);
    }
    capabilityGraph_.SetAvailableDeviceExtensions(extensionStrings);  // AR#8: per-graph, was static

    // Invalidate graph to force recheck with new extensions
    capabilityGraph_.InvalidateAll();

    // Check if RTX was enabled and cache capabilities
    rtxEnabled_ = HasExtension(extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
                  HasExtension(extensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    if (rtxEnabled_) {
        rtxCapabilities_ = CheckRTXSupport();
    }

    return {};
}

void VulkanDevice::DestroyDevice()
{
    if (device == VK_NULL_HANDLE)
        return;

    // Release device-owned subsystems (staging buffers, upload command buffers, the upload
    // timeline semaphore, and the budget allocator's buffers) BEFORE destroying the device.
    // These are VulkanDevice members; their destructors would otherwise run after this
    // function returns — i.e. after vkDestroyDevice — freeing child objects against an
    // already-destroyed device (validation: "child objects ... not destroyed prior to
    // destroying device", and undefined behaviour).
    uploader_.reset();
    // The GPU query manager is shared with render-graph nodes (so resetting our reference alone
    // may not destroy it). Explicitly release its query pools here, while the device is alive.
    if (queryManagerRelease_) {
        queryManagerRelease_();
    }
    queryManager_.reset();
    budgetManager_.reset();

    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
}

VulkanResult<uint32_t> VulkanDevice::MemoryTypeFromProperties(uint32_t typeBits, VkFlags requirementsMask)
{
    constexpr uint32_t MAX_MEMORY_TYPES = 32;
    for (uint32_t i = 0; i < MAX_MEMORY_TYPES; i++) {
        if ((typeBits & 1) == 1) {
            // Type is available, does it match user properties?
            if ((gpuMemoryProperties.memoryTypes[i].propertyFlags & requirementsMask) == requirementsMask) {
                return i;
            }
        }
        typeBits >>= 1;
    }

    return std::unexpected(VulkanError{VK_ERROR_FORMAT_NOT_SUPPORTED, "No suitable memory type found"});
}

void VulkanDevice::GetPhysicalDeviceQueuesAndProperties()
{
    // query queue families count by passing nullptr
    vkGetPhysicalDeviceQueueFamilyProperties(*gpu, &queueFamilyCount, nullptr);
    queueFamilyProperties.resize(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(*gpu, &queueFamilyCount, queueFamilyProperties.data());
}

VulkanResult<uint32_t> VulkanDevice::GetGraphicsQueueHandle() {
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsQueueIndex = i;
            // Assume graphics queue supports present (verified during swapchain creation)
            graphicsQueueWithPresentIndex = i;
            return i;
        }
    }
    return std::unexpected(VulkanError{VK_ERROR_FEATURE_NOT_PRESENT, "No graphics queue family found"});
}

void VulkanDevice::GetDeviceQueue() {
    vkGetDeviceQueue(device, graphicsQueueIndex, 0, &queue);
}

bool VulkanDevice::HasPresentSupport() const {
    // Present support is determined during queue family selection
    // graphicsQueueWithPresentIndex == graphicsQueueIndex means present is supported
    return (graphicsQueueWithPresentIndex == graphicsQueueIndex);
}

bool VulkanDevice::RequiresFullImageTransfers() const {
    if (graphicsQueueIndex >= queueFamilyProperties.size()) {
        return false;
    }
    const VkExtent3D& granularity = queueFamilyProperties[graphicsQueueIndex].minImageTransferGranularity;
    return granularity.width == 0 && granularity.height == 0 && granularity.depth == 0;
}

PFN_vkQueuePresentKHR VulkanDevice::GetPresentFunction() const {
    // vkQueuePresentKHR is always available when VK_KHR_swapchain extension is enabled
    // Return the standard function pointer
    return vkQueuePresentKHR;
}

// Helper to append a feature struct to the pNext chain
inline void* VulkanDevice::AppendToPNext(void** chainEnd, void* featureStruct) {
    *chainEnd = featureStruct;
    return &reinterpret_cast<VkBaseOutStructure*>(featureStruct)->pNext;
}

inline bool VulkanDevice::HasExtension(const std::vector<const char*>& extensions, const char* name) {
    for (const auto& ext : extensions) {
        if (strcmp(ext, name) == 0) {
            return true;
        }
    }
    return false;
}

// ===== RTX Support Implementation (Phase K) =====

std::vector<const char*> VulkanDevice::GetRTXExtensions() {
    return {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME  // Required by SPIRV 1.4
    };
}

RTXCapabilities VulkanDevice::CheckRTXSupport() const {
    RTXCapabilities caps{};

    if (!gpu || *gpu == VK_NULL_HANDLE) {
        return caps;
    }

    // 1. Check extension availability
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(*gpu, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availableExts(extCount);
    vkEnumerateDeviceExtensionProperties(*gpu, nullptr, &extCount, availableExts.data());

    auto hasExt = [&availableExts](const char* name) {
        for (const auto& ext : availableExts) {
            if (strcmp(ext.extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    };

    // Check required extensions
    bool hasAccelStructExt = hasExt(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    bool hasRTPipelineExt = hasExt(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    bool hasDeferredOpsExt = hasExt(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    bool hasBufferAddrExt = hasExt(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    bool hasSpirv14Ext = hasExt(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
    bool hasRayQueryExt = hasExt(VK_KHR_RAY_QUERY_EXTENSION_NAME);

    // All core RTX extensions must be present
    if (!hasAccelStructExt || !hasRTPipelineExt || !hasDeferredOpsExt || !hasBufferAddrExt) {
        return caps;  // Not supported
    }

    // 2. Check feature support
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures{};
    rtPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelStructFeatures{};
    accelStructFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelStructFeatures.pNext = &rtPipelineFeatures;

    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR bufferAddrFeatures{};
    bufferAddrFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
    bufferAddrFeatures.pNext = &accelStructFeatures;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &bufferAddrFeatures;

    vkGetPhysicalDeviceFeatures2(*gpu, &features2);

    caps.accelerationStructure = (accelStructFeatures.accelerationStructure == VK_TRUE);
    caps.rayTracingPipeline = (rtPipelineFeatures.rayTracingPipeline == VK_TRUE);

    // Check if all required features are supported
    if (!caps.accelerationStructure || !caps.rayTracingPipeline ||
        bufferAddrFeatures.bufferDeviceAddress != VK_TRUE) {
        caps.supported = false;
        return caps;
    }

    // 3. Query RT properties
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtPipelineProps{};
    rtPipelineProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelStructProps{};
    accelStructProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    accelStructProps.pNext = &rtPipelineProps;

    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &accelStructProps;

    vkGetPhysicalDeviceProperties2(*gpu, &props2);

    // Populate capabilities
    caps.supported = true;
    caps.rayQuery = hasRayQueryExt;

    caps.shaderGroupHandleSize = rtPipelineProps.shaderGroupHandleSize;
    caps.maxRayRecursionDepth = rtPipelineProps.maxRayRecursionDepth;
    caps.shaderGroupBaseAlignment = rtPipelineProps.shaderGroupBaseAlignment;
    caps.shaderGroupHandleAlignment = rtPipelineProps.shaderGroupHandleAlignment;

    caps.maxGeometryCount = accelStructProps.maxGeometryCount;
    caps.maxInstanceCount = accelStructProps.maxInstanceCount;
    caps.maxPrimitiveCount = accelStructProps.maxPrimitiveCount;

    return caps;
}

std::vector<std::string> VulkanDevice::QueryAvailableDeviceFeatures() const {
    // Query the physical device for the non-concrete features tracked by the capability graph
    // and return the names it reports as supported. Feed into
    // CapabilityGraph::SetAvailableDeviceFeatures() so feature enablement is gated centrally.
    // Add further VkPhysicalDeviceVulkan1x / extension feature structs to the pNext chain (and
    // a matching name push_back) as more non-concrete features are adopted.
    std::vector<std::string> supported;

    VkPhysicalDeviceVulkan12Features vulkan12{};
    vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

    VkPhysicalDeviceVulkan13Features vulkan13{};
    vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan12.pNext = &vulkan13;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &vulkan12;

    vkGetPhysicalDeviceFeatures2(*gpu, &features2);

    if (vulkan12.timelineSemaphore) {
        supported.emplace_back("timelineSemaphore");
    }
    if (vulkan13.synchronization2) {
        supported.emplace_back("synchronization2");
    }

    return supported;
}

// ============================================================================
// Upload Infrastructure (Sprint 5 Phase 2.5.3)
// ============================================================================

void VulkanDevice::SetUploader(std::unique_ptr<ResourceManagement::BatchedUploader> uploader) {
    uploader_ = std::move(uploader);
}

void VulkanDevice::SetBudgetManager(std::shared_ptr<ResourceManagement::DeviceBudgetManager> manager) {
    budgetManager_ = std::move(manager);
}

ResourceManagement::UploadHandle VulkanDevice::Upload(
    const void* data,
    VkDeviceSize size,
    VkBuffer dstBuffer,
    VkDeviceSize dstOffset) {

    if (!uploader_) {
        return ResourceManagement::InvalidUploadHandle;
    }
    return uploader_->Upload(data, size, dstBuffer, dstOffset);
}

void VulkanDevice::WaitAllUploads() {
    if (uploader_) {
        uploader_->WaitIdle();
    }
}

ResourceManagement::DeviceBudgetManager* VulkanDevice::GetBudgetManager() const {
    return budgetManager_.get();
}

bool VulkanDevice::HasUploadSupport() const {
    return uploader_ != nullptr && budgetManager_ != nullptr;
}

// ============================================================================
// Update Infrastructure (Sprint 5 Phase 3.5)
// ============================================================================

void VulkanDevice::SetUpdater(std::unique_ptr<ResourceManagement::BatchedUpdater> updater) {
    updater_ = std::move(updater);
}

void VulkanDevice::QueueUpdate(ResourceManagement::UpdateRequestPtr request) {
    if (!updater_ || !request) {
        return;
    }
    updater_->Queue(std::move(request));
}

uint32_t VulkanDevice::RecordUpdates(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (!updater_ || !cmd) {
        return 0;
    }
    return updater_->RecordAll(cmd, imageIndex, fpCmdPipelineBarrier2);
}

bool VulkanDevice::HasUpdateSupport() const {
    return updater_ != nullptr;
}

// ============================================================================
// Allocation Infrastructure (Sprint 5 Phase 3.5)
// ============================================================================

std::optional<ResourceManagement::BufferAllocation> VulkanDevice::AllocateBuffer(
    const ResourceManagement::BufferAllocationRequest& request) {

    auto* allocator = GetAllocator();
    if (!allocator) {
        return std::nullopt;
    }

    auto result = allocator->AllocateBuffer(request);
    if (result.has_value()) {
        return *result;
    }
    return std::nullopt;
}

void VulkanDevice::FreeBuffer(ResourceManagement::BufferAllocation& allocation) {
    auto* allocator = GetAllocator();
    if (allocator && allocation.buffer != VK_NULL_HANDLE) {
        allocator->FreeBuffer(allocation);
    }
}

void* VulkanDevice::MapBuffer(ResourceManagement::BufferAllocation& allocation) {
    // Check if already persistently mapped
    if (allocation.mappedData) {
        return allocation.mappedData;
    }

    auto* allocator = GetAllocator();
    if (!allocator) {
        return nullptr;
    }
    return allocator->MapBuffer(allocation);
}

void VulkanDevice::UnmapBuffer(ResourceManagement::BufferAllocation& allocation) {
    // Don't unmap if persistently mapped
    if (allocation.mappedData) {
        return;
    }

    auto* allocator = GetAllocator();
    if (allocator) {
        allocator->UnmapBuffer(allocation);
    }
}

ResourceManagement::IMemoryAllocator* VulkanDevice::GetAllocator() const {
    if (!budgetManager_) {
        return nullptr;
    }
    return budgetManager_->GetAllocator();
}

// ============================================================================
// GPU Query Infrastructure (Sprint 6.3 Phase 0)
// ============================================================================

void VulkanDevice::InitializeQueryManager(uint32_t framesInFlight, uint32_t maxConsumers) {
    // This function is a no-op placeholder
    // The actual creation happens in DeviceNode using SetQueryManagerInternal
    // This avoids circular dependency between VulkanResources and RenderGraph
    (void)framesInFlight;
    (void)maxConsumers;
}

void* VulkanDevice::GetQueryManager() const {
    return queryManager_.get();
}

bool VulkanDevice::HasQuerySupport() const {
    return queryManager_ != nullptr;
}
