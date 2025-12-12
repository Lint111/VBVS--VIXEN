#include "Headers.h"
#include "VulkanApplicationBase.h"
#include "VulkanGraphApplication.h"
#include "VulkanGlobalNames.h"
#include <Logger.h>

// Initialize global Vulkan extension/layer lists (defined inline in VulkanGlobalNames.h)
static bool initGlobalNames = []() {
    deviceExtensionNames = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,  // Optional: enables live resize scaling
        VK_KHR_MAINTENANCE_6_EXTENSION_NAME,  // Required for VK_EXT_swapchain_maintenance1
    };

    instanceExtensionNames = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME, // Dependency for VK_EXT_swapchain_maintenance1
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, // For querying surface capabilities
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#ifdef _DEBUG
        , VK_EXT_DEBUG_REPORT_EXTENSION_NAME  // Debug extension for validation callbacks
#endif
    };

    layerNames = {
#ifdef _DEBUG
        "VK_LAYER_KHRONOS_validation"  // Only enable validation in debug builds
#endif
    };

    return true;
}();

int main(int argc, char** argv) {
    // Create main logger for application-level diagnostics
    auto mainLogger = std::make_shared<Vixen::Log::Logger>("main", true);
    mainLogger->SetTerminalOutput(true);
    mainLogger->Info("Starting VulkanGraphApplication...");

    try {
        VulkanApplicationBase* appObj = VulkanGraphApplication::GetInstance();

        mainLogger->Info("Calling Initialize...");
        appObj -> Initialize();

        mainLogger->Info("Calling Prepare...");
        appObj -> Prepare();

        mainLogger->Info("Entering render loop...");
        bool isWindowOpen = true;
        while(isWindowOpen) {
            appObj -> Update();
            isWindowOpen = appObj->Render();
        }

        mainLogger->Info("Cleaning up...");
        appObj -> DeInitialize();
        mainLogger->Info("DeInitialize complete");
    }
    catch(const std::exception& e) {
        mainLogger->Error(std::string("Exception caught: ") + e.what());
        return -1;
    }
    catch(...) {
        mainLogger->Error("Unknown exception caught!");
        return -1;
    }

    mainLogger->Info("Exiting normally");
    return 0;
}