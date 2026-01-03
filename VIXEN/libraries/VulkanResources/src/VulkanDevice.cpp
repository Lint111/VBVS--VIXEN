#include "VulkanDevice.h"

// Upload infrastructure (Sprint 5 Phase 2.5.3)
#include "Memory/BatchedUploader.h"
#include "Memory/DeviceBudgetManager.h"

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
            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR,
            sizeof(VkPhysicalDeviceBufferDeviceAddressFeaturesKHR)
        }
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
        } else if (mapping.structType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR) {
            VkPhysicalDeviceBufferDeviceAddressFeaturesKHR* bdaFeatures = reinterpret_cast<VkPhysicalDeviceBufferDeviceAddressFeaturesKHR*>(featureStruct.get());
            bdaFeatures->bufferDeviceAddress = VK_TRUE;
        }

        // Append to pNext chain
        pNextChainEnd = reinterpret_cast<void**>(AppendToPNext(pNextChainEnd, featureStruct.get()));

        // Store the unique_ptr to keep the memory alive
        deviceFeatureStorage.push_back(std::move(featureStruct));
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

    // Initialize capability graph
    capabilityGraph_.BuildStandardCapabilities();

    // Convert extension names to strings for capability graph
    std::vector<std::string> extensionStrings;
    extensionStrings.reserve(extensions.size());
    for (const char* ext : extensions) {
        extensionStrings.emplace_back(ext);
    }
    Vixen::DeviceExtensionCapability::SetAvailableExtensions(extensionStrings);

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
