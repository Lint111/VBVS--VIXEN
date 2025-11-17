#include "VulkanDevice.h"

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

        // Append to pNext chain
        pNextChainEnd = reinterpret_cast<void**>(AppendToPNext(pNextChainEnd, featureStruct.get()));

        // Store the unique_ptr to keep the memory alive
        deviceFeatureStorage.push_back(std::move(featureStruct));
    }

    vkGetPhysicalDeviceFeatures(*gpu, &deviceFeatures);

    // When using VkPhysicalDeviceFeatures2 in pNext, features go in deviceFeatures2.features, not pEnabledFeatures
    deviceFeatures2.features.samplerAnisotropy = deviceFeatures.samplerAnisotropy;

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
