#pragma once

#include "Headers.h"
#include "VulkanLayerAndExtension.h"
#include "error/VulkanError.h"

namespace Vixen::Vulkan::Resources {

class VulkanDevice {
public:
    VulkanDevice(VkPhysicalDevice* gpu);
    ~VulkanDevice();

    // device related member variables
    VkDevice device; // logical device
    VkPhysicalDevice* gpu; // physical device
    VkPhysicalDeviceProperties gpuProperties; // physical device properties
    VkPhysicalDeviceMemoryProperties gpuMemoryProperties; // physical device mem properties

    VkQueue queue;
    std::vector<VkQueueFamilyProperties> queueFamilyProperties;    
    uint32_t graphicsQueueIndex;
    uint32_t graphicsQueueWithPresentIndex;
    uint32_t queueFamilyCount;
    VkPhysicalDeviceFeatures deviceFeatures; // physical device features
    
    VulkanLayerAndExtension layerExtension;

    //this class exposes the below functions to the outer world
    VulkanStatus CreateDevice(std::vector<const char *> &layers, std::vector<const char *> &extensions);
    void DestroyDevice();

    VulkanResult<uint32_t> MemoryTypeFromProperties(uint32_t typeBits, VkFlags requirementsMask);
    void GetPhysicalDeviceQueuesAndProperties();
    VulkanResult<uint32_t> GetGraphicsQueueHandle();
    void GetDeviceQueue();
    
    // Present queue support
    bool HasPresentSupport() const;
    PFN_vkQueuePresentKHR GetPresentFunction() const;

    struct DeviceFeatureMapping {
        const char* extensionName;
        VkStructureType structType;
        size_t structSize;
    };

    std::vector<std::unique_ptr<uint8_t[]>> deviceFeatureStorage; // to hold extension feature structures

    private:
    // Helper to append a feature struct to the pNext chain
    inline void* AppendToPNext(void** chainEnd, void* featureStruct);

    inline bool HasExtension(const std::vector<const char*>& extensions, const char* name);

};

} // namespace Vixen::Vulkan::Resources