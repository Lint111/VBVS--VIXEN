#include "VulkanInstance.h"

#include <algorithm>
#include <cstring>

namespace Vixen::Vulkan::Resources {

VulkanInstance::VulkanInstance() : instance(VK_NULL_HANDLE) {
	// Zero-init instance: without this, an object destructed before CreateInstance() ever runs
	// (e.g. an offline test that never calls Initialize()) left `instance` holding indeterminate
	// memory, so DestroyInstance()'s `if(instance != VK_NULL_HANDLE)` read garbage as a handle and
	// crashed in vkDestroyInstance. Surfaced by the offline VulkanApplicationBase::Tick() gtest.
}

VulkanInstance::~VulkanInstance() {
	DestroyInstance();
}

VkResult VulkanInstance::CreateInstance(std::vector<const char*>& layerNames,
                std::vector<const char*>& extensionNames,
                char const*const appName) {
	// The optional validation extensions are provided by the validation layer, not necessarily by
	// the active driver. Make the layer list visible to the extension filter before it queries both
	// global and layer-owned extension sets.
	layerExtension.appRequestedLayerNames = layerNames;
#if VIXEN_VULKAN_VALIDATION
	const bool validationLayerEnabled = std::any_of(
		layerNames.begin(), layerNames.end(), [](const char* layer) {
			return std::strcmp(layer, "VK_LAYER_KHRONOS_validation") == 0;
		});
	if (validationLayerEnabled && std::none_of(extensionNames.begin(), extensionNames.end(), [](const char* extension) {
			return std::strcmp(extension, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0;
		})) {
		extensionNames.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
	}
#endif

	// Drop any requested instance extensions the active driver or enabled layers do not advertise, so a limited driver
	// (e.g. Mesa Dozen on WSL2, which lacks VK_EXT_surface_maintenance1) cannot make vkCreateInstance
	// fail with VK_ERROR_EXTENSION_NOT_PRESENT. VK_KHR_surface is mandatory -- without it there is no
	// presentation path, so its absence stays a hard error rather than being silently swallowed.
	layerExtension.FilterUnsupportedExtensions(extensionNames, {VK_KHR_SURFACE_EXTENSION_NAME});

#if VIXEN_VULKAN_VALIDATION
	const bool validationFeaturesEnabled = validationLayerEnabled && std::any_of(
		extensionNames.begin(), extensionNames.end(), [](const char* extension) {
			return std::strcmp(extension, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0;
		});
#endif

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

#if VIXEN_VULKAN_VALIDATION
	if (validationFeaturesEnabled) {
		// Synchronization validation: chain VkValidationFeaturesEXT into pNext so the validation
		// layer reports missing/wrong barriers as errors. This is only legal when the optional
		// layer-owned extension survived the availability query above.
		static const VkValidationFeatureEnableEXT kSyncvalEnables[] = {
			VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
		};
		VkValidationFeaturesEXT validationFeatures{};
		validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
		validationFeatures.enabledValidationFeatureCount = 1;
		validationFeatures.pEnabledValidationFeatures = kSyncvalEnables;
		validationFeatures.pNext = instInfo.pNext;  // preserve existing chain (e.g. debug messenger)
		instInfo.pNext = &validationFeatures;
	} else if (!validationLayerEnabled) {
		LOG_WARNING("Optional Vulkan validation layer is unavailable; validation features disabled");
	}
#endif

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
