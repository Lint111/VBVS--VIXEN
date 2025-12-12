#include "VulkanLayerAndExtension.h"
#include "VulkanInstance.h"

namespace Vixen::Vulkan::Resources {

// Define static members
PFN_vkCreateDebugReportCallbackEXT VulkanLayerAndExtension::dbgCreateDebugReportCallback = nullptr;
PFN_vkDestroyDebugReportCallbackEXT VulkanLayerAndExtension::dbgDestroyDebugReportCallback = nullptr;
VkDebugReportCallbackEXT VulkanLayerAndExtension::DebugReportCallback = nullptr;
VkDebugReportCallbackCreateInfoEXT VulkanLayerAndExtension::dbgReportCreateInfo = {
	VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT,
	nullptr,
	VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
	VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT,
	VulkanLayerAndExtension::DebugFunction,
	nullptr
};

VulkanLayerAndExtension::VulkanLayerAndExtension() {
	InitializeLogger("VulkanLayerAndExtension");
}

VulkanLayerAndExtension::~VulkanLayerAndExtension() {
	// destructor
}

VkResult VulkanLayerAndExtension::GetInstanceLayerProperties() {
	// stores number of instance layers
	uint32_t instanceLayerCount;
	//vector to store layer properties
	std::vector<VkLayerProperties> layerProperties;
	// check vulkan API result status
	VkResult result;

	// quary all the layers

	do {
		result = vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr);

		if(result)
			return result;

		if(instanceLayerCount == 0)
			return VK_INCOMPLETE; // return fail

		layerProperties.resize(instanceLayerCount);
		result = vkEnumerateInstanceLayerProperties(&instanceLayerCount, 
													layerProperties.data());

	} while (result == VK_INCOMPLETE);

	// query all extensions for each layer and store it.
	LOG_INFO("\nInstanced Layers");
	LOG_INFO("================");
	for (auto& globalLayerProp : layerProperties) {
		//print layer name and its properties

		LOG_INFO("\n" + std::string(globalLayerProp.description) +
					 "\n\t|\n\t\\ --- [Layer Name] --> " +
					 globalLayerProp.layerName + "\n");

		LayerProperties layerProps;
		layerProps.properties = globalLayerProp;

		//get instance level extensions for corresponding layer properties

		result = GetExtentionProperties(layerProps);

		if(result)
			return result;

		layerPropertyList.push_back(layerProps);

		// print extension name for each instance layer
		for (auto j : layerProps.extensions) {
			LOG_INFO("\t\t|\n\t\t|--- [LayerExtension] --> " +
				std::string(j.extensionName) + "\n");
		}
	}
	return result;
}

// This function rerieves extensions and its 
// properties at instasnce and device level.
// Pass a valid physical device pointer (gpu) to retrieve
// device level extensions. otherwise use nullptr to retrieve 
// extension specific to instance level.
VkResult VulkanLayerAndExtension::GetExtentionProperties(LayerProperties& layerProps, const VkPhysicalDevice* const gpu) {
	// stores number of extensions per layer
	uint32_t extensionCount;
	VkResult result;

	//Name of the layer
	const char* layerName = layerProps.properties.layerName;

	do {
		// Get the total number of extensions in this layer
		if (gpu) {
			result = vkEnumerateDeviceExtensionProperties(*gpu,
				layerName,
				&extensionCount,
				nullptr);
		}
		else {
			result = vkEnumerateInstanceExtensionProperties(
				layerName,
				&extensionCount,
				nullptr);
		}

		if (result || extensionCount == 0)
			continue;

		layerProps.extensions.resize(extensionCount);

		// Get the extension properties
		if (gpu) {
			result = vkEnumerateDeviceExtensionProperties(*gpu,
				layerName,
				&extensionCount,
				layerProps.extensions.data());
		}
		else {
			result = vkEnumerateInstanceExtensionProperties(
				layerName,
				&extensionCount,
				layerProps.extensions.data());
		}

	} while (result == VK_INCOMPLETE);

	return result;
}

VkResult VulkanLayerAndExtension::GetDeviceExtentionProperties(const VkPhysicalDevice* const gpu, VulkanInstance& instance) {
	VkResult result; // result status

	LOG_INFO("\nDevice Extensions");
	LOG_INFO("=================");
	std::vector<LayerProperties>* instanceLayerProp = &instance.layerExtension.layerPropertyList;
	
	for (auto& globalLayerProp : *instanceLayerProp) {
		LayerProperties layerProps;
		layerProps.properties = globalLayerProp.properties;

		if(result = GetExtentionProperties(layerProps, gpu))
			return result;
		
		layerPropertyList.push_back(layerProps);
	}

	return result;	
}

VkBool32 VulkanLayerAndExtension::AreLayersSupported(std::vector<const char*>& layerNames) {
	uint32_t checkCount = static_cast<uint32_t>(layerNames.size());
	uint32_t layerCount = static_cast<uint32_t>(layerPropertyList.size());
	std::vector<const char*> unsupportedLayerNames;

	for (uint32_t i = 0; i < checkCount; i++) {
		VkBool32 isSupported = 0;
		for (uint32_t j = 0; j < layerCount; j++) {
			if (!strcmp(layerNames[i], layerPropertyList[j].properties.layerName)) {
				isSupported = 1;
			}
		}

		if(!isSupported) {
			LOG_INFO("No Layer support found, removed from layer: " + std::string(layerNames[i]));
			unsupportedLayerNames.push_back(layerNames[i]);
		} else {
			LOG_INFO("Layer support found, keep the layer: " + std::string(layerNames[i]));
		}
	}

	for(auto i : unsupportedLayerNames) {
		auto it = std::find(layerNames.begin(), layerNames.end(), i);
		if(it != layerNames.end()) {
			layerNames.erase(it);
		}
	}

	return true;
}

VkResult VulkanLayerAndExtension::CreateDebugReportCallBack(VkInstance instance) {
	VkResult result;

	dbgCreateDebugReportCallback = (PFN_vkCreateDebugReportCallbackEXT)
	vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");

	if(!dbgCreateDebugReportCallback) {
		LOG_ERROR("GetProcAddr: Unable to find vkCreateDebugReportCallbackEXT function.");
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	LOG_INFO("GetProcAddr: Found vkCreateDebugReportCallbackEXT function.");



	dbgDestroyDebugReportCallback = (PFN_vkDestroyDebugReportCallbackEXT)
	vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");

	if(!dbgDestroyDebugReportCallback) {
		LOG_ERROR("GetProcAddr: Unable to find vkDestroyDebugReportCallbackEXT function.");
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	LOG_INFO("GetProcAddr: Found vkDestroyDebugReportCallbackEXT function.");

	//define the debug report control structure
	// provide the references of 'debugFunction'
	// this function prints the debug information on the console
	dbgReportCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
	dbgReportCreateInfo.pfnCallback = DebugFunction;
	dbgReportCreateInfo.pUserData = nullptr;
	dbgReportCreateInfo.pNext = nullptr;
	dbgReportCreateInfo.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT |
								VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
								VK_DEBUG_REPORT_ERROR_BIT_EXT |
								VK_DEBUG_REPORT_DEBUG_BIT_EXT; 
	
	result = dbgCreateDebugReportCallback(instance,
										 &dbgReportCreateInfo,
										 nullptr,
										 &DebugReportCallback);

	if (result == VK_SUCCESS) {
		LOG_INFO("Debug Callback: Successfully created.");
	}
	return result;
}

uint32_t VulkanLayerAndExtension::DebugFunction(
	VkFlags msgFlags,
	VkDebugReportObjectTypeEXT objType,
	uint64_t srcObject,
	size_t location,
	int32_t msgCode,
	const char* pLayerPrefix,
	const char* pMsg,
	void* pUserData) {

	if(msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
		std::string msg = "[VK_DEBUG_REPORT] ERROR: [" + std::string(pLayerPrefix) + "] Code "
				  + std::to_string(msgCode) + " : " + pMsg;
	} else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
		std::string msg = "[VK_DEBUG_REPORT] WARNING: [" + std::string(pLayerPrefix) + "] Code "
				  + std::to_string(msgCode) + " : " + pMsg;
	} else if( msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
		std::string msg = "[VK_DEBUG_REPORT] INFO: [" + std::string(pLayerPrefix) + "] Code "
				  + std::to_string(msgCode) + " : " + pMsg;
	} else if (msgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
		std::string msg = "[VK_DEBUG_REPORT] PERFORMANCE: [" + std::string(pLayerPrefix) + "] Code "
				  + std::to_string(msgCode) + " : " + pMsg;
	} else if (msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
		std::string msg = "[VK_DEBUG_REPORT] DEBUG: [" + std::string(pLayerPrefix) + "] Code "
				  + std::to_string(msgCode) + " : " + pMsg;
	} else {
		std::string msg = "[VK_DEBUG_REPORT] UNKNOWN REPORT: [" + std::string(pLayerPrefix) + "] Code "
				  + std::to_string(msgCode) + " : " + pMsg;

		return VK_FALSE;
	}

	return VK_SUCCESS;
}

void VulkanLayerAndExtension::DestroyDebugReportCallback(VkInstance instance) {
	if (dbgDestroyDebugReportCallback && DebugReportCallback != VK_NULL_HANDLE) {
		dbgDestroyDebugReportCallback(instance, DebugReportCallback, nullptr);
		DebugReportCallback = VK_NULL_HANDLE;
	}
}

} // namespace Vixen::Vulkan::Resources
