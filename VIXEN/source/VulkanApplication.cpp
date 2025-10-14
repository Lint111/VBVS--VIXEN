#include "VulkanApplication.h"
#include "VulkanRenderer.h"
#include "VulkanSwapChain.h"

std::unique_ptr<VulkanApplication> VulkanApplication::instance;
std::once_flag VulkanApplication::onlyOnce;

extern std::vector<const char*> instanceExtensionNames;
extern std::vector<const char*> layerNames;
extern std::vector<const char*> deviceExtensionNames;

VulkanApplication::VulkanApplication() {
    instanceObj.layerExtension.GetInstanceLayerProperties();
    debugFlag = true; // enable or disable debug callback
}

VulkanApplication::~VulkanApplication() {
    DeInitialize();
}

VulkanApplication* VulkanApplication::GetInstance() {
    std::call_once(onlyOnce, [](){instance.reset(new VulkanApplication());});
    return instance.get();
}

VkResult VulkanApplication::CreateVulkanInstance(std::vector<const char*>& layers,
                                                  std::vector<const char*>& extensions,
                                                  const char* applicationName) {
    instanceObj.CreateInstance(layers, extensions, applicationName);
    
    return VK_SUCCESS;
}

VkResult VulkanApplication::HandShakeWithDevice(VkPhysicalDevice* gpu,
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

    deviceObj->GetGraphicsQueueHandle();

    //create logical device
    return deviceObj->CreateDevice(layers, extensions);
}

void VulkanApplication::Initialize() {
    char title[] = "hello World!!!";

    if(debugFlag)
        instanceObj.layerExtension.AreLayersSupported(layerNames);


    CreateVulkanInstance(layerNames, instanceExtensionNames, title);

    if(debugFlag)
        instanceObj.layerExtension.CreateDebugReportCallBack();

    // Use member variable instead of local variable to keep gpuList alive
    EnumeratePhysicalDevices(gpuList);

    if(gpuList.size() > 0) {
        HandShakeWithDevice(&gpuList[0],layerNames,deviceExtensionNames);
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

void VulkanApplication::DeInitialize() {
    // Wait for device to finish all operations
    if (deviceObj && deviceObj->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(deviceObj->device);
    }

    renderObj.reset();
    deviceObj.reset();

    instanceObj.DestroyInstance();
}

VkResult VulkanApplication::EnumeratePhysicalDevices(std::vector<VkPhysicalDevice>& gpuList) {
    // holds gpu count
    uint32_t gpuDeviceCount;
    //get physical device count
    VkResult result = vkEnumeratePhysicalDevices(instanceObj.instance, &gpuDeviceCount, nullptr);
    assert(result == VK_SUCCESS);

    // make space to hold all devices
    gpuList.resize(gpuDeviceCount);
    result = vkEnumeratePhysicalDevices(instanceObj.instance, &gpuDeviceCount, gpuList.data());
    assert(result == VK_SUCCESS);

    return result;
}
