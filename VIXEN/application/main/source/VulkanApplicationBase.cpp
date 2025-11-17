#include "VulkanApplicationBase.h"

extern std::vector<const char*> instanceExtensionNames;
extern std::vector<const char*> layerNames;
extern std::vector<const char*> deviceExtensionNames;

VulkanApplicationBase::VulkanApplicationBase() 
    : debugFlag(true), isPrepared(false) {
    instanceObj.layerExtension.GetInstanceLayerProperties();

    // Create main logger (disabled by default, enable in derived class as needed)
    mainLogger = std::make_shared<Logger>("VulkanAppBase", false);
    mainLogger->Info("Vulkan Application Base Starting");
}

VulkanApplicationBase::~VulkanApplicationBase() {
    // Base destructor should not write logs because derived classes
    // may have already cleaned up objects that registered with the
    // main logger (child loggers owned by nodes). Log extraction
    // must happen while those child loggers are still alive. The
    // application-level class (VulkanGraphApplication) will perform
    // log extraction at the correct time before destroying the
    // render graph.

    DeInitialize();
}

void VulkanApplicationBase::Initialize() {
    InitializeVulkanCore();
}

void VulkanApplicationBase::DeInitialize() {
   
    instanceObj.DestroyInstance();
}

VulkanStatus VulkanApplicationBase::CreateVulkanInstance(std::vector<const char*>& layers,
                                                          std::vector<const char*>& extensions,
                                                          const char* applicationName) {
    instanceObj.CreateInstance(layers, extensions, applicationName);
    return {};
}



VulkanStatus VulkanApplicationBase::EnumeratePhysicalDevices(std::vector<VkPhysicalDevice>& gpuList) {
    // Holds GPU count
    uint32_t gpuDeviceCount;
    
    // Get physical device count
    VK_CHECK(vkEnumeratePhysicalDevices(instanceObj.instance, &gpuDeviceCount, nullptr),
             "Failed to get physical device count");

    if (gpuDeviceCount == 0) {
        return std::unexpected(VulkanError{VK_ERROR_INITIALIZATION_FAILED, "No Vulkan-capable devices found"});
    }

    // Make space to hold all devices
    gpuList.resize(gpuDeviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(instanceObj.instance, &gpuDeviceCount, gpuList.data()),
             "Failed to enumerate physical devices");

    return {};
}

void VulkanApplicationBase::InitializeVulkanCore() {
    char title[] = "Vulkan Application";

    if (debugFlag)
        instanceObj.layerExtension.AreLayersSupported(layerNames);

    // Create Vulkan instance
    if (auto result = CreateVulkanInstance(layerNames, instanceExtensionNames, title); !result) {
        mainLogger->Error("Failed to create Vulkan instance: " + result.error().toString());
        exit(1);
    }

    if (debugFlag)
        instanceObj.layerExtension.CreateDebugReportCallBack(instanceObj.instance);


    mainLogger->Info("Vulkan core initialized successfully");
}
