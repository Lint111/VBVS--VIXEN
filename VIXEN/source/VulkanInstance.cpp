#include "VulkanInstance.h"

VulkanInstance::VulkanInstance() {
	// constructor
}

VulkanInstance::~VulkanInstance() {
	DestroyInstance();
}

VkResult VulkanInstance::CreateInstance(std::vector<const char*>& layerNames,
										std::vector<const char*>& extensionNames,
										char const*const appName) {
	//set the instance specific layer and extension information
	layerExtension.appRequestedExtensionNames = extensionNames;
	layerExtension.appRequestedLayerNames = layerNames;

	//define the vulkan application structure
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pNext = nullptr;
	appInfo.pApplicationName = appName;
	appInfo.applicationVersion = 1;
	appInfo.pEngineName = appName;
	appInfo.engineVersion = 1;
	appInfo.apiVersion = VK_API_VERSION_1_0;

	//define the vulkan instance create info structure
	VkInstanceCreateInfo instInfo = {};
	instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instInfo.pNext = nullptr;  // Don't create debug callback during instance creation
	instInfo.flags = 0;
	instInfo.pApplicationInfo = &appInfo;
	// specify the list of layers to be enabled
	instInfo.enabledLayerCount = layerNames.size();
	instInfo.ppEnabledLayerNames = layerNames.data();
	// specify the list of extensions to be enabled
	instInfo.enabledExtensionCount = extensionNames.size();
	instInfo.ppEnabledExtensionNames = extensionNames.data();

	VkResult res = vkCreateInstance(&instInfo, nullptr, &instance);
	return res;
}

void VulkanInstance::DestroyInstance() {

	if(layerExtension.DebugReportCallback != VK_NULL_HANDLE) {
		layerExtension.DestroyDebugReportCallback();
	}

	if(instance != VK_NULL_HANDLE) {
		vkDestroyInstance(instance, nullptr); // Destroy the Vulkan instance
		instance = VK_NULL_HANDLE;  // Set to null to prevent double-destruction
	}
}