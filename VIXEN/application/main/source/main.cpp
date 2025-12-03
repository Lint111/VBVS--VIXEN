#include "Headers.h"
#include "VulkanApplicationBase.h"
#include "VulkanGraphApplication.h"
#include "VulkanGlobalNames.h"

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
    std::cout << "[main] Starting VulkanGraphApplication..." << std::endl;

    try {
        VulkanApplicationBase* appObj = VulkanGraphApplication::GetInstance();

        std::cout << "[main] Calling Initialize..." << std::endl;
        appObj -> Initialize();

        std::cout << "[main] Calling Prepare..." << std::endl;
        appObj -> Prepare();

        std::cout << "[main] Entering render loop..." << std::endl;
        bool isWindowOpen = true;
        while(isWindowOpen) {
            appObj -> Update();
            isWindowOpen = appObj->Render();
        }

        std::cout << "[main] Cleaning up..." << std::endl;
        appObj -> DeInitialize();
        std::cout << "[main] DeInitialize complete" << std::endl;
    }
    catch(const std::exception& e) {
        std::cerr << "[main] Exception caught: " << e.what() << std::endl;
        return -1;
    }
    catch(...) {
        std::cerr << "[main] Unknown exception caught!" << std::endl;
        return -1;
    }

    std::cout << "[main] Exiting normally" << std::endl;
    return 0;
}