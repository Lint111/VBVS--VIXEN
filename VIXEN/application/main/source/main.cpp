#include "Headers.h"
#include "VulkanApplicationBase.h"
#include "VulkanGraphApplication.h"
#include "VulkanGlobalNames.h"
#include <Logger.h>

// Validation layers/extensions are gated by the cross-platform VIXEN_VULKAN_VALIDATION
// symbol (set by cmake/ProvisionVulkan.cmake from the build type), NOT the MSVC-only
// _DEBUG macro -- _DEBUG is undefined on GCC/Clang, which silently disabled validation
// for non-MSVC consumers (UNDERTOW FR-1). Default off if the symbol is absent.
#ifndef VIXEN_VULKAN_VALIDATION
#define VIXEN_VULKAN_VALIDATION 0
#endif

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
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME // For querying surface capabilities
        // The platform surface extension (Win32/Xlib/Wayland) is added at runtime by InstanceNode
        // via glfwGetRequiredInstanceExtensions — cross-platform, no hardcoded VK_KHR_win32_surface.
#if VIXEN_VULKAN_VALIDATION
        , VK_EXT_DEBUG_REPORT_EXTENSION_NAME  // Debug extension for validation callbacks
#endif
    };

    layerNames = {
#if VIXEN_VULKAN_VALIDATION
        "VK_LAYER_KHRONOS_validation"  // Enabled via VIXEN_VULKAN_VALIDATION (cross-platform)
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
        // Instantiate the application directly (AR#7: the singleton is gone). unique_ptr so
        // teardown (~VulkanGraphApplication -> DeInitialize) runs deterministically on scope exit.
        auto app = std::make_unique<VulkanGraphApplication>();
        VulkanApplicationBase* appObj = app.get();

        mainLogger->Info("Calling Initialize...");
        appObj -> Initialize();

        mainLogger->Info("Calling Prepare...");
        appObj -> Prepare();
        if (!appObj->IsPrepared()) {
            // Phase 2b: Prepare() now reports failure via IsPrepared()/GetLastError() instead of
            // throwing (so a C# host gets a status, not a C++ exception). Abort the run gracefully.
            mainLogger->Error("Prepare failed: " + appObj->GetLastError() + " - aborting before render loop");
            appObj -> DeInitialize();
            return -1;
        }

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