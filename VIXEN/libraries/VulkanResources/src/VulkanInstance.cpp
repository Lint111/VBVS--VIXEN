#include "VulkanInstance.h"

namespace Vixen::Vulkan::Resources {

VulkanInstance::VulkanInstance() {
	// constructor
}

VulkanInstance::~VulkanInstance() {
	DestroyInstance();
}

VkResult VulkanInstance::CreateInstance(std::vector<const char*>& layerNames,
										std::vector<const char*>& extensionNames,
										char const*const appName) {
	// Drop any requested instance extensions the active ICD does not advertise, so a limited driver
	// (e.g. Mesa Dozen on WSL2, which lacks VK_EXT_surface_maintenance1) cannot make vkCreateInstance
	// fail with VK_ERROR_EXTENSION_NOT_PRESENT. VK_KHR_surface is mandatory -- without it there is no
	// presentation path, so its absence stays a hard error rather than being silently swallowed.
	layerExtension.FilterUnsupportedExtensions(extensionNames, {VK_KHR_SURFACE_EXTENSION_NAME});

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
	appInfo.apiVersion = VK_API_VERSION_1_3;  // Request Vulkan 1.3 (RTX 3060 supports up to 1.4)

	//define the vulkan instance create info structure
	VkInstanceCreateInfo instInfo = {};
	instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instInfo.pNext = nullptr;  // Don't create debug callback during instance creation
	instInfo.flags = 0;
	instInfo.pApplicationInfo = &appInfo;
	// specify the list of layers to be enabled
	instInfo.enabledLayerCount = static_cast<uint32_t>(layerNames.size());
	instInfo.ppEnabledLayerNames = layerNames.data();
	// specify the list of extensions to be enabled
	instInfo.enabledExtensionCount = static_cast<uint32_t>(extensionNames.size());
	instInfo.ppEnabledExtensionNames = extensionNames.data();

	VkResult res = vkCreateInstance(&instInfo, nullptr, &instance);
	return res;
}

void VulkanInstance::DestroyInstance() {

	if(layerExtension.DebugReportCallback != VK_NULL_HANDLE) {
		layerExtension.DestroyDebugReportCallback(instance);
	}

	if(instance != VK_NULL_HANDLE) {
		vkDestroyInstance(instance, nullptr); // Destroy the Vulkan instance
		instance = VK_NULL_HANDLE;  // Set to null to prevent double-destruction
	}
}

}