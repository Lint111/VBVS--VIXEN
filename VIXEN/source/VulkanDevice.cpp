#include "VulkanDevice.h"

VulkanDevice::VulkanDevice(VkPhysicalDevice* physicalDevice) {
    gpu = physicalDevice;
}

VulkanDevice::~VulkanDevice() {
    DestroyDevice();
}

VkResult VulkanDevice::CreateDevice(std::vector<const char*>& layers,
                                    std::vector<const char*>& extensions) {

    layerExtension.appRequestedLayerNames = layers;
    layerExtension.appRequestedExtensionNames = extensions;

    VkResult result;
    float queuePriorities[1] = { 0.0 };

    // Create the object information
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.queueFamilyIndex = graphicsQueueIndex;
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.pNext = NULL;
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
        swapchainMaintenance1Features.pNext = NULL;
        swapchainMaintenance1Features.swapchainMaintenance1 = VK_TRUE;
    }

    // Create the logical device representation
    VkDeviceCreateInfo deviceInfo = {};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = hasSwapchainMaintenance ? &swapchainMaintenance1Features : NULL;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledLayerCount = 0;

    // deprecated and should not be used
    deviceInfo.ppEnabledLayerNames = NULL;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    deviceInfo.ppEnabledExtensionNames = extensions.size() ? extensions.data() : NULL;
    deviceInfo.pEnabledFeatures = NULL;

    result = vkCreateDevice(*gpu, &deviceInfo, nullptr, &device);
    assert(result == VK_SUCCESS);
    return result;
}

void VulkanDevice::DestroyDevice()
{
    if (device == VK_NULL_HANDLE)
        return;
        
    vkDestroyDevice(device, NULL);
    device = VK_NULL_HANDLE;
}

bool VulkanDevice::MemoryTypeFromProperties(uint32_t typeBits, VkFlags requirementsMask, uint32_t *typeIndex)
{
    for (uint32_t i = 0; i < 32; i++) {
        if ((typeBits & 1) == 1) {
            // Type is available, does it match user properties?
            if ((gpuMemoryProperties.memoryTypes[i].propertyFlags & requirementsMask) == requirementsMask) {
                *typeIndex = i;
                return true;
            }
        }
        typeBits >>= 1;
    }

    return false;
}

void VulkanDevice::GetPhysicalDeviceQueuesAndProperties()
{
    // query queue families count by passing NULL
    vkGetPhysicalDeviceQueueFamilyProperties(*gpu, &queueFamilyCount, NULL);
    queueFamilyProperties.resize(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(*gpu, &queueFamilyCount, queueFamilyProperties.data());
}

uint32_t VulkanDevice::GetGraphicsQueueHandle() {
    bool found = false;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsQueueIndex = i;
            found = true;
            break;
        }
    }
    return 0;
}

void VulkanDevice::GetDeviceQueue() {
    vkGetDeviceQueue(device, graphicsQueueIndex, 0, &queue);
}