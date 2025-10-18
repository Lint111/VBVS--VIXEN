#include "VulkanApplication.h"
#include "VulkanRenderer.h"
#include "VulkanSwapChain.h"

using namespace Vixen::Vulkan::Resources;

std::unique_ptr<VulkanApplication> VulkanApplication::instance;
std::once_flag VulkanApplication::onlyOnce;

extern std::vector<const char*> instanceExtensionNames;
extern std::vector<const char*> layerNames;
extern std::vector<const char*> deviceExtensionNames;

VulkanApplication::VulkanApplication() {
    instanceObj.layerExtension.GetInstanceLayerProperties();
    debugFlag = true; // enable or disable debug callback

    // Create main logger
    mainLogger = std::make_shared<Logger>("VulkanApp", true);
    mainLogger->Info("Vulkan Application Starting");
}

VulkanApplication::~VulkanApplication() {
    // Write logs to file before cleanup
    if (mainLogger) {
        std::string logs = mainLogger->ExtractLogs();
        std::ofstream logFile("vulkan_app_log.txt");
        if (logFile.is_open()) {
            logFile << logs;
            logFile.close();
            std::cout << "Logs written to vulkan_app_log.txt" << std::endl;
        }
    }

    DeInitialize();
}

VulkanApplication* VulkanApplication::GetInstance() {
    std::call_once(onlyOnce, [](){instance.reset(new VulkanApplication());});
    return instance.get();
}

VulkanStatus VulkanApplication::CreateVulkanInstance(std::vector<const char*>& layers,
                                                      std::vector<const char*>& extensions,
                                                      const char* applicationName) {
    instanceObj.CreateInstance(layers, extensions, applicationName);
    return {};
}

VulkanStatus VulkanApplication::HandShakeWithDevice(VkPhysicalDevice* gpu,
                                                     std::vector<const char*>& layers,
                                                     std::vector<const char*>& extensions) {
    deviceObj = std::make_unique<VulkanDevice>(gpu);

    //print the devices avilable layer and their extensions
    deviceObj->layerExtension.GetDeviceExtentionProperties(gpu);

    //get the physical device gpu properties
    vkGetPhysicalDeviceProperties(*gpu, &deviceObj->gpuProperties);
    //get the physical device gpu memory properties
    vkGetPhysicalDeviceMemoryProperties(*gpu, &deviceObj->gpuMemoryProperties);

    //query physical device queue and properties
    deviceObj->GetPhysicalDeviceQueuesAndProperties();

    // Get graphics queue handle
    auto queueResult = deviceObj->GetGraphicsQueueHandle();
    VK_PROPAGATE_ERROR(queueResult);

    //create logical device
    auto deviceResult = deviceObj->CreateDevice(layers, extensions);
    VK_PROPAGATE_ERROR(deviceResult);

    return {};
}

void VulkanApplication::Initialize() {
    char title[] = "hello World!!!";

    if(debugFlag)
        instanceObj.layerExtension.AreLayersSupported(layerNames);

    // Create Vulkan instance
    if (auto result = CreateVulkanInstance(layerNames, instanceExtensionNames, title); !result) {
        std::cerr << "Failed to create Vulkan instance: " << result.error().toString() << std::endl;
        exit(1);
    }

    if(debugFlag)
        instanceObj.layerExtension.CreateDebugReportCallBack();

    // Enumerate physical devices
    if (auto result = EnumeratePhysicalDevices(gpuList); !result) {
        std::cerr << "Failed to enumerate devices: " << result.error().toString() << std::endl;
        exit(1);
    }

    // Handshake with first device
    if(gpuList.size() > 0) {
        if (auto result = HandShakeWithDevice(&gpuList[0], layerNames, deviceExtensionNames); !result) {
            std::cerr << "Failed device handshake: " << result.error().toString() << std::endl;
            exit(1);
        }
    }

    if(!renderObj)
        renderObj = std::make_unique<VulkanRenderer>(this, deviceObj.get());
    renderObj->Initialize();
}

void VulkanApplication::Prepare() {
    isPrepared = false;
    renderObj->Prepare();
    isPrepared = true;
}

bool VulkanApplication::render()
{
    if(!isPrepared)
		return false;

    return renderObj->Render();
}

void VulkanApplication::Update() {
    if(!isPrepared)
        return;

    renderObj->Update();
}

void VulkanApplication::DeInitialize() {
    // Wait for device to finish all operations
    if (deviceObj && deviceObj->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(deviceObj->device);
    }

    renderObj.reset();
    deviceObj.reset();

    instanceObj.DestroyInstance();

    // DestroyDevices already called via deviceObj.reset(), but call anyway for safety
    (void)DestroyDevices();
}

VulkanStatus VulkanApplication::EnumeratePhysicalDevices(std::vector<VkPhysicalDevice>& gpuList) {
    // holds gpu count
    uint32_t gpuDeviceCount;
    //get physical device count
    VK_CHECK(vkEnumeratePhysicalDevices(instanceObj.instance, &gpuDeviceCount, nullptr),
             "Failed to get physical device count");

    if (gpuDeviceCount == 0) {
        return std::unexpected(VulkanError{VK_ERROR_INITIALIZATION_FAILED, "No Vulkan-capable devices found"});
    }

    // make space to hold all devices
    gpuList.resize(gpuDeviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(instanceObj.instance, &gpuDeviceCount, gpuList.data()),
             "Failed to enumerate physical devices");

    return {};
}

VulkanStatus VulkanApplication::DestroyDevices()
{
    if (deviceObj) {
        deviceObj->DestroyDevice();
        deviceObj.reset();
    }

    return {};
}
