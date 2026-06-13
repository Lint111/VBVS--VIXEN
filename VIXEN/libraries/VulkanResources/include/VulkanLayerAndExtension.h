#pragma once

#include <Headers.h>
#include "ILoggable.h"

namespace Vixen::Vulkan::Resources {

class VulkanInstance;

struct LayerProperties {
    VkLayerProperties properties;
    std::vector<VkExtensionProperties> extensions;
};

class VulkanLayerAndExtension : public ILoggable {
public:
    VulkanLayerAndExtension();
    ~VulkanLayerAndExtension();

    // Layer and extension member function and variables
    std::vector<const char*> appRequestedLayerNames;
    std::vector<const char*> appRequestedExtensionNames;
    std::vector<LayerProperties> layerPropertyList;

    VkResult GetInstanceLayerProperties();
    VkResult GetExtentionProperties(LayerProperties& layerProps, const VkPhysicalDevice* const gpu = nullptr);
    VkResult GetDeviceExtentionProperties(const VkPhysicalDevice* const gpu, VulkanInstance& instance);

    VkBool32 AreLayersSupported(std::vector<const char *> &layerNames);

    // Filter a requested instance-extension list down to what the active ICD actually exposes
    // (queried via vkEnumerateInstanceExtensionProperties). Unsupported names are dropped with a
    // warning so vkCreateInstance cannot fail with VK_ERROR_EXTENSION_NOT_PRESENT on limited
    // drivers (e.g. Mesa Dozen on WSL2, which lacks VK_EXT_surface_maintenance1). Names listed in
    // `mandatory` are kept regardless: their absence is a real, fatal misconfiguration, so leaving
    // them in lets vkCreateInstance report it rather than masking it.
    void FilterUnsupportedExtensions(std::vector<const char *> &extensionNames,
                                     const std::vector<const char *> &mandatory = {});

    VkResult CreateDebugReportCallBack(VkInstance instance);

    static uint32_t DebugFunction(VkFlags msgFlags,
                                  VkDebugReportObjectTypeEXT objType,
                                  uint64_t srcObject,
                                  size_t location,
                                  int32_t msgCode,
                                  const char *pLayerPrefix,
                                  const char *pMsg,
                                  void *pUserData);

    static void DestroyDebugReportCallback(VkInstance instance);

    static PFN_vkCreateDebugReportCallbackEXT dbgCreateDebugReportCallback;
    static PFN_vkDestroyDebugReportCallbackEXT dbgDestroyDebugReportCallback;
    static VkDebugReportCallbackEXT DebugReportCallback;
    static VkDebugReportCallbackCreateInfoEXT dbgReportCreateInfo;
};

} // namespace Vixen::Vulkan::Resources
