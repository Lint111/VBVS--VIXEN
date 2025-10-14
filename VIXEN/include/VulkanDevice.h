#pragma once 

#include "Headers.h"
#include "VulkanLayerAndExtension.h"

class VulkanDevice {
public:
    VulkanDevice(VkPhysicalDevice* gpu);
    ~VulkanDevice();

    // device related meber variables
    VkDevice device; // logical device
    VkPhysicalDevice* gpu; // physical device
    VkPhysicalDeviceProperties gpuProperties; // physical device properties
    VkPhysicalDeviceMemoryProperties gpuMemoryProperties; // physical device mem properties

    VkQueue queue;
    std::vector<VkQueueFamilyProperties> queueFamilyProperties;    
    uint32_t graphicsQueueIndex;
    uint32_t graphicsQueueWithPresentIndex;
    uint32_t queueFamilyCount;
    
    VulkanLayerAndExtension layerExtension;

    //this class exposes the below functions to the outer world
    VkResult CreateDevice(std::vector<const char *> &layers, std::vector<const char *> &extensions);
    void DestroyDevice();

    bool MemoryTypeFromProperties(uint32_t typeBits, VkFlags requirementsMask, uint32_t *typeIndex);
    void GetPhysicalDeviceQueuesAndProperties();
    uint32_t GetGraphicsQueueHandle();
    void GetDeviceQueue();
};
