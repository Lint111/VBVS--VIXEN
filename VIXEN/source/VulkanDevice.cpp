#include "VulkanDevice.h"

VulkanDevice::VulkanDevice(VkPhysicalDevice* physicalDevice) {
    gpu = physicalDevice;
}

VulkanDevice::~VulkanDevice() {
    DestroyDevice();
}

VulkanSuccess VulkanDevice::CreateDevice(std::vector<const char*>& layers,
                                         std::vector<const char*>& extensions) {

    layerExtension.appRequestedLayerNames = layers;
    layerExtension.appRequestedExtensionNames = extensions;

    float queuePriorities[1] = { 0.0 };

    // Create the object information
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.queueFamilyIndex = graphicsQueueIndex;
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.pNext = nullptr;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = queuePriorities;

    // Check if swapchain maintenance extension is requested
    bool hasSwapchainMaintenance = false;
    for (const char* ext : extensions) {
        if (strcmp(ext, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) == 0) {
            hasSwapchainMaintenance = true;
            break;
        }
    }

    // Enable swapchainMaintenance1 feature if extension is present
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchainMaintenance1Features = {};
    if (hasSwapchainMaintenance) {
        swapchainMaintenance1Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        swapchainMaintenance1Features.pNext = nullptr;
        swapchainMaintenance1Features.swapchainMaintenance1 = VK_TRUE;
    }

    // Create the logical device representation
    VkDeviceCreateInfo deviceInfo = {};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = hasSwapchainMaintenance ? &swapchainMaintenance1Features : nullptr;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledLayerCount = 0;

    // deprecated and should not be used
    deviceInfo.ppEnabledLayerNames = nullptr;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    deviceInfo.ppEnabledExtensionNames = extensions.size() ? extensions.data() : nullptr;
    deviceInfo.pEnabledFeatures = nullptr;

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
            return i;
        }
    }
    return std::unexpected(VulkanError{VK_ERROR_FEATURE_NOT_PRESENT, "No graphics queue family found"});
}

void VulkanDevice::GetDeviceQueue() {
    vkGetDeviceQueue(device, graphicsQueueIndex, 0, &queue);
}