#include "Headers.h"
#include "VulkanApplicationBase.h"
#include "VulkanGraphApplication.h"
#include "VulkanGlobalNames.h"
#include <Logger.h>
#include <cstdlib>  // std::getenv for VIXEN_LOG_LEVEL
#include <string>
#include <array>      // frame-time rolling window (perf instrumentation)
#include <algorithm>  // std::sort for p99
#include <chrono>     // steady_clock frame timing
#include <cstdio>     // std::snprintf

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
        // Perf measurement instrumentation (perf sweep 2026-07):
        //   - rolling CPU frame-time summary (avg/p99/FPS) every 120 frames
        //   - VIXEN_EXIT_AFTER_FRAMES=<n>: close cleanly after n frames (unattended A/B runs;
        //     exits through the same path as a window close so logs flush via ExtractLogs)
        long exitAfterFrames = 0;
        if (const char* env = std::getenv("VIXEN_EXIT_AFTER_FRAMES")) {
            exitAfterFrames = std::strtol(env, nullptr, 10);
        }
        constexpr size_t kFrameWindow = 120;
        std::array<double, kFrameWindow> frameTimesMs{};
        uint64_t frameCounter = 0;
        auto lastFrameStart = std::chrono::steady_clock::now();
        bool isWindowOpen = true;
        // M5.1: outlier-frame logging. lastWindowMedian_ persists the PREVIOUS completed window's
        // median frame time; any frame costing >3x that AND >5ms absolute gets its own log line the
        // instant it happens, instead of waiting to be buried in the next window's avg/p99 summary.
        // The absolute floor keeps sub-millisecond noise (e.g. a ~0.45ms median) from producing a
        // flood of "outliers" that are really just measurement jitter. 0 (no prior window yet)
        // disables the check for the first kFrameWindow frames.
        double lastWindowMedian = 0.0;
        while(isWindowOpen) {
            appObj -> Update();
            isWindowOpen = appObj->Render();

            const auto now = std::chrono::steady_clock::now();
            const double thisFrameMs = std::chrono::duration<double, std::milli>(now - lastFrameStart).count();
            frameTimesMs[frameCounter % kFrameWindow] = thisFrameMs;
            lastFrameStart = now;
            ++frameCounter;

            if (lastWindowMedian > 0.0 && thisFrameMs > 3.0 * lastWindowMedian && thisFrameMs > 5.0) {
                char obuf[128];
                std::snprintf(obuf, sizeof(obuf), "[FrameTimer] OUTLIER frame %llu: %.3f ms",
                              static_cast<unsigned long long>(frameCounter), thisFrameMs);
                mainLogger->Info(obuf);
            }

            if (frameCounter % kFrameWindow == 0) {
                std::array<double, kFrameWindow> sorted = frameTimesMs;
                std::sort(sorted.begin(), sorted.end());
                double sum = 0.0;
                for (double v : sorted) sum += v;
                const double avg = sum / static_cast<double>(kFrameWindow);
                const double p99 = sorted[(kFrameWindow * 99) / 100];
                const double median = sorted[kFrameWindow / 2];
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "[FrameTimer] frames %llu-%llu: avg %.3f ms (%.1f FPS) | p99 %.3f ms",
                              static_cast<unsigned long long>(frameCounter - kFrameWindow),
                              static_cast<unsigned long long>(frameCounter),
                              avg, avg > 0.0 ? 1000.0 / avg : 0.0, p99);
                mainLogger->Info(buf);
                lastWindowMedian = median;
            }

            if (exitAfterFrames > 0 && frameCounter >= static_cast<uint64_t>(exitAfterFrames)) {
                mainLogger->Info("[FrameTimer] VIXEN_EXIT_AFTER_FRAMES=" + std::to_string(exitAfterFrames)
                                 + " reached - closing");
                isWindowOpen = false;
            }
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