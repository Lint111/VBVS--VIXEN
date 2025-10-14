#pragma once

#include <Headers.h>

struct LayerProperties {
	VkLayerProperties properties;
	std::vector<VkExtensionProperties> extensions;
};

class VulkanLayerAndExtension {
public:
	VulkanLayerAndExtension();
	~VulkanLayerAndExtension();

	// Layer and extension member function and variables

	// list of layer names requested by the application
	std::vector<const char*> appRequestedLayerNames;

	//list of extentsions names requested by the application
	std::vector<const char*> appRequestedExtensionNames;

	// list of available instance layers and corresponding extensions
	std::vector<LayerProperties> layerPropertyList;

	// layers and corresponding extensions list
	VkResult GetInstanceLayerProperties(); // Instance / Global

	//Globbal extensions
	VkResult GetExtentionProperties(LayerProperties& layerProps, VkPhysicalDevice* gpu = nullptr);

	// device extensions
	VkResult GetDeviceExtentionProperties(VkPhysicalDevice* gpu);

    VkBool32 AreLayersSupported(std::vector<const char *> &layerNames);

    VkResult CreateDebugReportCallBack();

    static uint32_t DebugFunction(VkFlags msgFlags, 
								  VkDebugReportObjectTypeEXT objType, 
								  uint64_t srcObject, 
								  size_t location, 
								  int32_t msgCode, 
								  const char *pLayerPrefix, 
								  const char *pMsg, 
								  void *pUserData);

    static void DestroyDebugReportCallback();

    // Declaration of the Create and Destroy function pointers
	static PFN_vkCreateDebugReportCallbackEXT dbgCreateDebugReportCallback;
	static PFN_vkDestroyDebugReportCallbackEXT dbgDestroyDebugReportCallback;

	//Handle of the debug report callback
	static VkDebugReportCallbackEXT DebugReportCallback;
	//Debug report callback create Information control structure
	static VkDebugReportCallbackCreateInfoEXT dbgReportCreateInfo;
};