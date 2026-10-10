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

    uint32_t physicalDeviceApiVersion = 0;
    if (gpu && *gpu != VK_NULL_HANDLE) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(*gpu, &properties);
        physicalDeviceApiVersion = properties.apiVersion;
        capabilityGraph_.SetVersion(
            Vixen::CapabilityVersionSource::VulkanApi,
            Vixen::CapabilityVersionValue::FromVulkanApi(properties.apiVersion));
    }

    // Create the object information
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.queueFamilyIndex = graphicsQueueIndex;
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = queuePriorities;

    // Centralise extension, feature, API and identity observations through CapabilityGraph so
    // configure-time and runtime decisions use the same graph nodes and feature query.
    capabilityGraph_.BuildStandardCapabilities();
    if (gpu && *gpu != VK_NULL_HANDLE) {
        capabilityGraph_.ObservePhysicalDevice(*gpu);
    }

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
        },
        {
            VK_KHR_RAY_QUERY_EXTENSION_NAME,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
            sizeof(VkPhysicalDeviceRayQueryFeaturesKHR)
        },
        // VIXEN_PIPELINE_STATS opt-in (see DeviceNode::CreateLogicalDevice): only reaches
        // `extensions` when the env var is set AND the device advertises the extension, so this
        // row is a no-op in the default (unset) case -- HasExtension below never matches.
        {
            VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR,
            sizeof(VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR)
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
        } else if (mapping.structType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR) {
            VkPhysicalDeviceRayQueryFeaturesKHR* rqFeatures = reinterpret_cast<VkPhysicalDeviceRayQueryFeaturesKHR*>(featureStruct.get());
            rqFeatures->rayQuery = VK_TRUE;
        } else if (mapping.structType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR) {
            VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR* pipeStatsFeatures = reinterpret_cast<VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR*>(featureStruct.get());
            pipeStatsFeatures->pipelineExecutableInfo = VK_TRUE;
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
    // hostQueryReset (Vulkan 1.2 core): lets the CPU reset a query pool via vkResetQueryPool without
    // a command buffer. GPUTimestampQuery resets its per-frame timestamp pools on the host at
    // creation so the very first vkGetQueryPoolResults (the startup/first-frames window, before each
    // per-flight pool has completed its first cmd-buffer reset→write→submit cycle) reads an
    // initialized pool instead of an unreset one — the VUID-vkGetQueryPoolResults-None-09401 startup
    // burst. When the GPU lacks the feature, GPUTimestampQuery falls back to GPU-side resets only
    // (HasCapability check there), so this is a strict improvement, never a hard dependency.
    if (capabilityGraph_.IsCapabilityAvailable("DeviceFeature:hostQueryReset")) {
        vulkan12Features.hostQueryReset = VK_TRUE;
        needVulkan12Features = true;
    }
    if (needVulkan12Features) {
        pNextChainEnd = reinterpret_cast<void**>(AppendToPNext(pNextChainEnd, &vulkan12Features));
    }

    // synchronization2 is REQUIRED. CapabilityGraph resolves the promotion rule: Vulkan 1.3 core,
    // or VK_KHR_synchronization2 on Vulkan 1.2. Query and enable the matching feature struct so
    // WSL's Vulkan 1.2 Dozen path and Vulkan 1.3+ drivers share the same graph decision.
    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2Features{};
    synchronization2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    if (!capabilityGraph_.IsCapabilityAvailable("DeviceFeature:synchronization2")) {
        throw std::runtime_error(
            "GPU does not support required synchronization2 (Vulkan 1.3 core or Vulkan 1.2 "
            "with VK_KHR_synchronization2) - the renderer records all GPU barriers via "
            "vkCmdPipelineBarrier2KHR.");
    }
    if (physicalDeviceApiVersion >= VK_API_VERSION_1_3) {
        vulkan13Features.synchronization2 = VK_TRUE;
        pNextChainEnd = reinterpret_cast<void**>(AppendToPNext(pNextChainEnd, &vulkan13Features));
    } else {
        synchronization2Features.synchronization2 = VK_TRUE;
        pNextChainEnd = reinterpret_cast<void**>(AppendToPNext(pNextChainEnd, &synchronization2Features));
    }

    if (physicalDeviceApiVersion < VK_API_VERSION_1_3 &&
        capabilityGraph_.IsDeviceExtensionAvailable(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) &&
        !HasExtension(extensions, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    }

    vkGetPhysicalDeviceFeatures(*gpu, &deviceFeatures);

    // B2's preferred writer uses fragment-stage SSBO atomics. Support was queried once
    // by QueryAvailableDeviceFeatures() and published into the capability graph above;
    // enable the core feature from that central verdict. Unsupported devices keep the
    // bit disabled and select B2's compute-writer twin instead.
    if (capabilityGraph_.IsCapabilityAvailable(
            "DeviceFeature:fragmentStoresAndAtomics")) {
        deviceFeatures2.features.fragmentStoresAndAtomics = VK_TRUE;
    }

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

    // Resolve the promoted synchronization2 entry points for this device. On 1.2 the KHR extension
    // must be enabled and its names are used; on 1.3+ use the core names, with KHR aliases as a
    // driver compatibility fallback. A null result is an unexpected dispatch failure, not a
    // capability fallback: every renderer barrier uses synchronization2.
    fpCmdPipelineBarrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(
        vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2KHR"));
    fpQueueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2KHR>(
        vkGetDeviceProcAddr(device, "vkQueueSubmit2KHR"));
    if (physicalDeviceApiVersion >= VK_API_VERSION_1_3) {
        if (!fpCmdPipelineBarrier2) {
            fpCmdPipelineBarrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(
                vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2"));
        }
        if (!fpQueueSubmit2) {
            fpQueueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2KHR>(
                vkGetDeviceProcAddr(device, "vkQueueSubmit2"));
        }
    }
    if (!fpCmdPipelineBarrier2 || !fpQueueSubmit2) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(*gpu, &props);
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
        throw std::runtime_error(
            std::string("GPU driver '") + props.deviceName + "' reports synchronization2 support "
            "but " + (!fpCmdPipelineBarrier2 ? "vkCmdPipelineBarrier2KHR" : "vkQueueSubmit2KHR") +
            " failed to resolve through the promoted Vulkan API or extension entry points. "
            "The renderer requires a real synchronization2 implementation; there is "
            "no legacy vkCmdPipelineBarrier/vkQueueSubmit fallback path.");
    }

    // Resolve the optional ray-query lighting AS entry points on this logical device. A
    // non-RT device is expected to leave these null; BodyOctreeSceneNode then follows the
    // permanent composed-DDA twin selected by CapabilityGraph. Keeping the dispatch table here
    // also makes device-loss recreation safe: no AS call site caches a process-global function.
    fpCreateAccelerationStructure = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR"));
    fpDestroyAccelerationStructure = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR"));
    fpGetAccelerationStructureBuildSizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR"));
    fpCmdBuildAccelerationStructures = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR"));
    fpGetAccelerationStructureDeviceAddress = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR"));

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

    // VIXEN_PIPELINE_STATS: record whether the extension actually made it into the enabled set
    // (DeviceNode only requests it when the env var is set AND the device supports it), and
    // resolve its functions -- same per-device vkGetDeviceProcAddr rationale as fpCmdPipelineBarrier2.
    pipelineStatsEnabled_ = HasExtension(extensions, VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME);
    if (pipelineStatsEnabled_) {
        fpGetPipelineExecutableProperties = reinterpret_cast<PFN_vkGetPipelineExecutablePropertiesKHR>(
            vkGetDeviceProcAddr(device, "vkGetPipelineExecutablePropertiesKHR"));
        fpGetPipelineExecutableStatistics = reinterpret_cast<PFN_vkGetPipelineExecutableStatisticsKHR>(
            vkGetDeviceProcAddr(device, "vkGetPipelineExecutableStatisticsKHR"));
    }

    return {};
}

void VulkanDevice::DestroyDevice()
{
    if (device == VK_NULL_HANDLE) {
        fpCreateAccelerationStructure = nullptr;
        fpDestroyAccelerationStructure = nullptr;
        fpGetAccelerationStructureBuildSizes = nullptr;
        fpCmdBuildAccelerationStructures = nullptr;
        fpGetAccelerationStructureDeviceAddress = nullptr;
        return;
    }

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
    fpCreateAccelerationStructure = nullptr;
    fpDestroyAccelerationStructure = nullptr;
    fpGetAccelerationStructureBuildSizes = nullptr;
    fpCmdBuildAccelerationStructures = nullptr;
    fpGetAccelerationStructureDeviceAddress = nullptr;
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
            // Presentation is NOT asserted here. It is negotiated against a real surface by
            // NegotiatePresentQueue(); until then graphicsQueueWithPresentIndex stays
            // kPresentQueueNotNegotiated so HasPresentSupport() cannot report a fabricated true.
            return i;
        }
    }
    return std::unexpected(VulkanError{VK_ERROR_FEATURE_NOT_PRESENT, "No graphics queue family found"});
}

VulkanResult<uint32_t> VulkanDevice::NegotiatePresentQueue(VkSurfaceKHR surface) {
    if (surface == VK_NULL_HANDLE) {
        return std::unexpected(VulkanError{VK_ERROR_INITIALIZATION_FAILED,
            "NegotiatePresentQueue: surface is VK_NULL_HANDLE - presentation cannot be negotiated "
            "without a surface to query against"});
    }
    if (queueFamilyCount == 0) {
        return std::unexpected(VulkanError{VK_ERROR_INITIALIZATION_FAILED,
            "NegotiatePresentQueue: queue families not enumerated - call "
            "GetPhysicalDeviceQueuesAndProperties() first"});
    }

    // vkGetPhysicalDeviceSurfaceSupportKHR is a VK_KHR_surface (instance-level) entry point, so it
    // is callable here -- before the logical device exists. That is precisely why the negotiation
    // can inform device creation rather than being discovered after it.
    uint32_t graphicsFamilies = 0;
    uint32_t presentFamilies = 0;
    uint32_t selected = kPresentQueueNotNegotiated;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        const bool isGraphics = (queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        VkBool32 supportsPresent = VK_FALSE;
        const VkResult r = vkGetPhysicalDeviceSurfaceSupportKHR(*gpu, i, surface, &supportsPresent);
        if (r != VK_SUCCESS) {
            return std::unexpected(VulkanError{r,
                "NegotiatePresentQueue: vkGetPhysicalDeviceSurfaceSupportKHR failed for queue family "
                + std::to_string(i)});
        }
        if (isGraphics) graphicsFamilies++;
        if (supportsPresent) presentFamilies++;
        if (isGraphics && supportsPresent && selected == kPresentQueueNotNegotiated) {
            selected = i;
        }
    }

    if (selected == kPresentQueueNotNegotiated) {
        // 0ej: name the gap precisely rather than degrading silently. The two counts distinguish
        // "this GPU cannot present to this surface at all" from "it can present, but only on a
        // family without graphics" -- different problems with different fixes.
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(*gpu, &props);
        return std::unexpected(VulkanError{VK_ERROR_FEATURE_NOT_PRESENT,
            std::string("GPU '") + props.deviceName + "' has no queue family that supports BOTH "
            "graphics and presentation to this surface (" + std::to_string(queueFamilyCount) +
            " families: " + std::to_string(graphicsFamilies) + " graphics-capable, " +
            std::to_string(presentFamilies) + " present-capable)"});
    }

    graphicsQueueWithPresentIndex = selected;
    // The rendering queue must be the one that can present: a swapchain acquired and presented on
    // a family that cannot present is exactly the late, obscure failure this negotiation exists to
    // prevent. Keep graphicsQueueIndex in sync with the proven family.
    graphicsQueueIndex = selected;
    return selected;
}

void VulkanDevice::GetDeviceQueue() {
    vkGetDeviceQueue(device, graphicsQueueIndex, 0, &queue);
}

bool VulkanDevice::HasPresentSupport() const {
    // True only once NegotiatePresentQueue() proved it against a real surface.
    return graphicsQueueWithPresentIndex != kPresentQueueNotNegotiated;
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

Vixen::LockCensus::QueueSubmitMutex& VulkanDevice::SubmitMutex(VkQueue queue) {
    std::lock_guard lock(submitMutexMapLock_);
    auto it = submitMutexes_.find(queue);
    if (it == submitMutexes_.end()) {
        it = submitMutexes_.emplace(queue, std::make_unique<Vixen::LockCensus::QueueSubmitMutex>()).first;
    }
    return *it->second;
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
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME  // Required by SPIRV 1.4
    };
}

std::vector<const char*> VulkanDevice::GetRayQueryLightingExtensions() {
    return {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME  // Required by SPIR-V 1.4
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
    return gpu && *gpu != VK_NULL_HANDLE
        ? capabilityGraph_.QuerySupportedDeviceFeatures(*gpu)
        : std::vector<std::string>{};
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

ResourceManagement::UploadHandle VulkanDevice::UploadOrdered(
    const std::vector<ResourceManagement::BatchedUploader::UploadRequest>& requests) {
    if (!uploader_) {
        return ResourceManagement::InvalidUploadHandle;
    }
    return uploader_->UploadOrdered(requests);
}

void VulkanDevice::WaitAllUploads() {
    if (uploader_) {
        uploader_->WaitIdle();
    }
}

void VulkanDevice::FlushUploads() {
    if (uploader_) {
        uploader_->Flush();
    }
}

bool VulkanDevice::IsUploadComplete(ResourceManagement::UploadHandle handle) const {
    if (!uploader_) {
        return true;  // no uploader configured — nothing to wait on
    }
    // ProcessCompletions() advances GPU-side fence/timeline polling; IsComplete() alone
    // would never transition Pending/Submitted -> Completed without something driving
    // that check, and nothing else in the app currently polls the uploader per frame.
    uploader_->ProcessCompletions();
    return uploader_->IsComplete(handle);
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
