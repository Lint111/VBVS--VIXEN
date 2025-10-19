#pragma once

#include <Headers.h>

namespace Vixen::Vulkan::Resources {

struct LayerProperties {
    VkLayerProperties properties;
    std::vector<VkExtensionProperties> extensions;
};

class VulkanLayerAndExtension {
public:
    VulkanLayerAndExtension();
    ~VulkanLayerAndExtension();

    // Layer and extension member function and variables
    std::vector<const char*> appRequestedLayerNames;
    std::vector<const char*> appRequestedExtensionNames;
    std::vector<LayerProperties> layerPropertyList;

    VkResult GetInstanceLayerProperties();
    VkResult GetExtentionProperties(LayerProperties& layerProps, VkPhysicalDevice* gpu = nullptr);
    VkResult GetDeviceExtentionProperties(VkPhysicalDevice* gpu);

    VkBool32 AreLayersSupported(std::vector<const char *> &layerNames);
    VkResult CreateDebugReportCallBack(VkInstance instance);

    static uint32_t DebugFunction(VkFlags msgFlags,
                                  VkDebugReportObjectTypeEXT objType,
                                  uint64_t srcObject,
                                  size_t location,
                                  int32_t msgCode,
                                  const char *pLayerPrefix,
                                  const char *pMsg,
                                  void *pUserData);

    static void DestroyDebugReportCallback();

    static PFN_vkCreateDebugReportCallbackEXT dbgCreateDebugReportCallback;
    static PFN_vkDestroyDebugReportCallbackEXT dbgDestroyDebugReportCallback;
    static VkDebugReportCallbackEXT DebugReportCallback;
    static VkDebugReportCallbackCreateInfoEXT dbgReportCreateInfo;
};

} // namespace Vixen::Vulkan::Resources
