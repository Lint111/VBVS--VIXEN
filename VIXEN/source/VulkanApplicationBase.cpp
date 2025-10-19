#include "VulkanApplicationBase.h"

extern std::vector<const char*> instanceExtensionNames;
extern std::vector<const char*> layerNames;
extern std::vector<const char*> deviceExtensionNames;

VulkanApplicationBase::VulkanApplicationBase() 
    : debugFlag(true), isPrepared(false) {
    instanceObj.layerExtension.GetInstanceLayerProperties();

    // Create main logger
    mainLogger = std::make_shared<Logger>("VulkanAppBase", true);
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
    // Wait for device to finish all operations
    if (deviceObj && deviceObj->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(deviceObj->device);
    }

    DestroyDevices();
    instanceObj.DestroyInstance();
}

VulkanStatus VulkanApplicationBase::CreateVulkanInstance(std::vector<const char*>& layers,
                                                          std::vector<const char*>& extensions,
                                                          const char* applicationName) {
    instanceObj.CreateInstance(layers, extensions, applicationName);
    return {};
}

VulkanStatus VulkanApplicationBase::HandShakeWithDevice(VkPhysicalDevice* gpu,
                                                         std::vector<const char*>& layers,
                                                         std::vector<const char*>& extensions) {
    deviceObj = std::make_unique<VulkanDevice>(gpu);

    // Print the devices available layer and their extensions
    deviceObj->layerExtension.GetDeviceExtentionProperties(gpu);

    // Get the physical device GPU properties
    vkGetPhysicalDeviceProperties(*gpu, &deviceObj->gpuProperties);
    
    // Get the physical device GPU memory properties
    vkGetPhysicalDeviceMemoryProperties(*gpu, &deviceObj->gpuMemoryProperties);

    // Query physical device queue and properties
    deviceObj->GetPhysicalDeviceQueuesAndProperties();

    // Get graphics queue handle
    auto queueResult = deviceObj->GetGraphicsQueueHandle();
    VK_PROPAGATE_ERROR(queueResult);

    // Create logical device
    auto deviceResult = deviceObj->CreateDevice(layers, extensions);
    VK_PROPAGATE_ERROR(deviceResult);

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

VulkanStatus VulkanApplicationBase::DestroyDevices() {
    if (deviceObj) {
        deviceObj->DestroyDevice();
        deviceObj.reset();
    }

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

    // Enumerate physical devices
    if (auto result = EnumeratePhysicalDevices(gpuList); !result) {
        mainLogger->Error("Failed to enumerate devices: " + result.error().toString());
        exit(1);
    }

    // Handshake with first device
    if (gpuList.size() > 0) {
        if (auto result = HandShakeWithDevice(&gpuList[0], layerNames, deviceExtensionNames); !result) {
            mainLogger->Error("Failed device handshake: " + result.error().toString());
            exit(1);
        }
    }

    mainLogger->Info("Vulkan core initialized successfully");
}
