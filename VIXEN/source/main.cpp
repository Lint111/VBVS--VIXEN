#include "Headers.h"
#include "VulkanApplication.h"

std::vector<const char*> deviceExtensionNames = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME  // Optional: enables live resize scaling
};

std::vector<const char*> instanceExtensionNames = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#ifdef _DEBUG
    , VK_EXT_DEBUG_REPORT_EXTENSION_NAME  // Debug extension for validation callbacks
#endif
};

std::vector<const char*> layerNames = {
#ifdef _DEBUG
    "VK_LAYER_KHRONOS_validation"  // Only enable validation in debug builds
#endif
};

int main(int argc, char** argv) {
    try {
        VulkanApplication* appObj = VulkanApplication::GetInstance();

        appObj -> Initialize();
        appObj -> Prepare();

        bool isWindowOpen = true;
        while(isWindowOpen) {
            isWindowOpen = appObj->render();
        }

        appObj -> DeInitialize();
    }
    catch(const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << std::endl;
        return -1;
    }
    catch(...) {
        std::cerr << "Unknown exception caught!" << std::endl;
        return -1;
    }

    return 0;
}