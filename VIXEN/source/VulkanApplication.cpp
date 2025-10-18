#include "VulkanApplication.h"
#include "VulkanRenderer.h"
#include "VulkanSwapChain.h"

using namespace Vixen::Vulkan::Resources;

std::unique_ptr<VulkanApplication> VulkanApplication::instance;
std::once_flag VulkanApplication::onlyOnce;

extern std::vector<const char*> instanceExtensionNames;
extern std::vector<const char*> layerNames;
extern std::vector<const char*> deviceExtensionNames;

VulkanApplication::VulkanApplication() : VulkanApplicationBase() {
    if (mainLogger) {
        mainLogger->Info("VulkanApplication (Renderer-based) Starting");
    }
}

VulkanApplication::~VulkanApplication() {
    DeInitialize();
}

VulkanApplication* VulkanApplication::GetInstance() {
    std::call_once(onlyOnce, [](){instance.reset(new VulkanApplication());});
    return instance.get();
}

void VulkanApplication::Initialize() {
    // Initialize base Vulkan core
    VulkanApplicationBase::Initialize();

    // Create renderer
    if (!renderObj) {
        renderObj = std::make_unique<VulkanRenderer>(this, deviceObj.get());
        renderObj->Initialize();
    }

    if (mainLogger) {
        mainLogger->Info("VulkanApplication initialized with renderer");
    }
}

void VulkanApplication::Prepare() {
    isPrepared = false;
    
    if (renderObj) {
        renderObj->Prepare();
    }
    
    isPrepared = true;
}

bool VulkanApplication::Render() {
    if (!isPrepared || !renderObj)
        return false;

    return renderObj->Render();
}

void VulkanApplication::Update() {
    if (!isPrepared || !renderObj)
        return;

    renderObj->Update();
}

void VulkanApplication::DeInitialize() {
    // Wait for device to finish all operations
    if (deviceObj && deviceObj->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(deviceObj->device);
    }

    // Destroy renderer
    renderObj.reset();

    // Call base class cleanup
    VulkanApplicationBase::DeInitialize();
}
