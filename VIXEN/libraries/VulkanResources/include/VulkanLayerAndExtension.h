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

    // Filter a requested instance-extension list down to what the active ICD and requested layers
    // expose (queried via vkEnumerateInstanceExtensionProperties). Unsupported names are dropped with a
    // warning so vkCreateInstance cannot fail with VK_ERROR_EXTENSION_NOT_PRESENT on limited
    // drivers (e.g. Mesa Dozen on WSL2, which lacks VK_EXT_surface_maintenance1). Names listed in
    // `mandatory` are kept regardless: their absence is a real, fatal misconfiguration, so leaving
    // them in lets vkCreateInstance report it rather than masking it.
    void FilterUnsupportedExtensions(std::vector<const char *> &extensionNames,
                                     const std::vector<const char *> &mandatory = {});

    // Drop requested instance layers the loader does not list (vkEnumerateInstanceLayerProperties),
    // so an optional layer such as VK_LAYER_KHRONOS_validation cannot make vkCreateInstance fail with
    // VK_ERROR_LAYER_NOT_PRESENT. Each dropped layer is reported once with a located message.
    void FilterUnsupportedLayers(std::vector<const char *> &layerNames);

    // Pure selection step shared by the layer and extension filters (no Vulkan calls, unit-testable):
    // keeps the `requested` names that appear in `available`, keeps `mandatory` names even when absent
    // (so vkCreateInstance reports them), and appends every dropped name to `dropped`.
    static std::vector<const char *> SelectAvailableNames(const std::vector<const char *> &requested,
                                                          const std::vector<std::string> &available,
                                                          const std::vector<const char *> &mandatory,
                                                          std::vector<const char *> &dropped);

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
