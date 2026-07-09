#include "Headers.h"
#include "VulkanGraphApplication.h"
#include "VulkanGlobalNames.h"
#include <Logger.h>
#include <cstdlib>  // std::getenv for VIXEN_LOG_LEVEL, std::strtoull for VIXEN_EXIT_AFTER_FRAMES
#include <string>

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
    // Log verbosity: default to INFO so per-frame DEBUG diagnostics don't drown the console.
    // Override with the VIXEN_LOG_LEVEL env var: DEBUG | INFO | WARNING | ERROR | CRITICAL.
    // (Per-frame descriptor tracking is a separate, compile-time opt-in:
    //  -DVIXEN_DEBUG_DESCRIPTOR_TRACKING=1.)
    {
        Vixen::Log::LogLevel level = Vixen::Log::LogLevel::LOG_INFO;
        if (const char* env = std::getenv("VIXEN_LOG_LEVEL")) {
            const std::string lv(env);
            if      (lv == "DEBUG")                 level = Vixen::Log::LogLevel::LOG_DEBUG;
            else if (lv == "INFO")                  level = Vixen::Log::LogLevel::LOG_INFO;
            else if (lv == "WARNING" || lv == "WARN") level = Vixen::Log::LogLevel::LOG_WARNING;
            else if (lv == "ERROR")                 level = Vixen::Log::LogLevel::LOG_ERROR;
            else if (lv == "CRITICAL")              level = Vixen::Log::LogLevel::LOG_CRITICAL;
        }
        Vixen::Log::Logger::SetGlobalMinLevel(level);
    }

    // Create main logger for application-level diagnostics
    auto mainLogger = std::make_shared<Vixen::Log::Logger>("main", true);
    mainLogger->SetTerminalOutput(true);
    mainLogger->Info("Starting VulkanGraphApplication...");

    // Instantiate the application directly (AR#7: the singleton is gone). unique_ptr so
    // teardown (~VulkanGraphApplication -> DeInitialize) runs deterministically on scope exit.
    auto app = std::make_unique<VulkanGraphApplication>();

    // VIXEN_EXIT_AFTER_FRAMES=<n>: close cleanly after n frames (unattended A/B runs; exits
    // through the same path as a window close so logs flush via ExtractLogs).
    uint64_t exitAfterFrames = 0;
    if (const char* env = std::getenv("VIXEN_EXIT_AFTER_FRAMES")) {
        exitAfterFrames = static_cast<uint64_t>(std::strtoull(env, nullptr, 10));
    }

    // Engine-owned loop: Initialize -> Prepare -> loop -> DeInitialize, with the standalone
    // app's frame-timer instrumentation (rolling avg/p99/FPS + outlier logging) enabled. All
    // lifecycle + the try/catch boundary now live in Run().
    return app->Run({ .exitAfterFrames = exitAfterFrames, .enableFrameTimer = true });
}