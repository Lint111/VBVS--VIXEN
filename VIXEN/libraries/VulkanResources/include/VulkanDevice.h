#pragma once

#include "Headers.h"
#include "VulkanLayerAndExtension.h"
#include "error/VulkanError.h"

namespace Vixen::Vulkan::Resources {

/**
 * @brief Ray tracing capability information
 */
struct RTXCapabilities {
    bool supported = false;                    // All required extensions available
    bool accelerationStructure = false;        // VK_KHR_acceleration_structure
    bool rayTracingPipeline = false;           // VK_KHR_ray_tracing_pipeline
    bool rayQuery = false;                     // VK_KHR_ray_query (optional)

    // Properties from VkPhysicalDeviceRayTracingPipelinePropertiesKHR
    uint32_t shaderGroupHandleSize = 0;
    uint32_t maxRayRecursionDepth = 0;
    uint32_t shaderGroupBaseAlignment = 0;
    uint32_t shaderGroupHandleAlignment = 0;

    // Properties from VkPhysicalDeviceAccelerationStructurePropertiesKHR
    uint64_t maxGeometryCount = 0;
    uint64_t maxInstanceCount = 0;
    uint64_t maxPrimitiveCount = 0;
};

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

    // ===== RTX Support (Phase K) =====

    /**
     * @brief Check if hardware ray tracing is supported
     * @return RTXCapabilities struct with support flags and properties
     *
     * Queries support for:
     * - VK_KHR_acceleration_structure
     * - VK_KHR_ray_tracing_pipeline
     * - VK_KHR_deferred_host_operations
     * - VK_KHR_buffer_device_address
     */
    RTXCapabilities CheckRTXSupport() const;

    /**
     * @brief Get required device extensions for RTX
     * @return Vector of extension names to enable
     */
    static std::vector<const char*> GetRTXExtensions();

    /**
     * @brief Check if RTX was enabled during device creation
     */
    bool IsRTXEnabled() const { return rtxEnabled_; }

    /**
     * @brief Get cached RTX capabilities (valid after CreateDevice)
     */
    const RTXCapabilities& GetRTXCapabilities() const { return rtxCapabilities_; }

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

    // RTX state
    bool rtxEnabled_ = false;
    RTXCapabilities rtxCapabilities_;

};

} // namespace Vixen::Vulkan::Resources