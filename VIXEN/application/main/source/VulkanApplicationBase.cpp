#include "VulkanApplicationBase.h"
#include "VulkanGlobalNames.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace {
// Encapsulates the standalone app's rolling CPU frame-time instrumentation (relocated from
// application/main/source/main.cpp so it isn't copy-pasted per entry point). Behavior-identical:
// avg/p99/FPS summary every 120 frames + a per-frame OUTLIER line when a frame costs >3x the prior
// window's median AND >5ms. Enabled only when RunOptions.enableFrameTimer is set.
class FrameTimer {
public:
    // Call once per rendered frame. frameCounter is the post-increment count (1-based).
    void Record(uint64_t frameCounter, Logger* logger) {
        const auto now = std::chrono::steady_clock::now();
        const double thisFrameMs = std::chrono::duration<double, std::milli>(now - lastFrameStart_).count();
        frameTimesMs_[(frameCounter - 1) % kFrameWindow] = thisFrameMs;
        lastFrameStart_ = now;

        if (lastWindowMedian_ > 0.0 && thisFrameMs > 3.0 * lastWindowMedian_ && thisFrameMs > 5.0 && logger) {
            char obuf[128];
            std::snprintf(obuf, sizeof(obuf), "[FrameTimer] OUTLIER frame %llu: %.3f ms",
                          static_cast<unsigned long long>(frameCounter), thisFrameMs);
            logger->Info(obuf);
        }
        if (frameCounter % kFrameWindow == 0 && logger) {
            std::array<double, kFrameWindow> sorted = frameTimesMs_;
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
            logger->Info(buf);
            lastWindowMedian_ = median;
        }
    }
private:
    static constexpr size_t kFrameWindow = 120;
    std::array<double, kFrameWindow> frameTimesMs_{};
    std::chrono::steady_clock::time_point lastFrameStart_ = std::chrono::steady_clock::now();
    double lastWindowMedian_ = 0.0;
};
}  // namespace

VulkanApplicationBase::VulkanApplicationBase()
    : debugFlag(true), isPrepared(false) {
    instanceObj.layerExtension.GetInstanceLayerProperties();

    // Create main logger (disabled by default, enable in derived class as needed)
    mainLogger = std::make_shared<Logger>("VulkanAppBase", false);
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
   
    instanceObj.DestroyInstance();
}

VulkanStatus VulkanApplicationBase::CreateVulkanInstance(std::vector<const char*>& layers,
                                                          std::vector<const char*>& extensions,
                                                          const char* applicationName) {
    // Propagate the VkResult: previously this swallowed it and always returned success, so a failed
    // vkCreateInstance produced a null VkInstance that the next vkGetInstanceProcAddr call aborted on
    // (SIGABRT, "Invalid instance"). Surfacing it lets InitializeVulkanCore report and exit cleanly.
    VkResult result = instanceObj.CreateInstance(layers, extensions, applicationName);
    if (result != VK_SUCCESS) {
        return std::unexpected(VulkanError{result, "vkCreateInstance failed"});
    }
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


    mainLogger->Info("Vulkan core initialized successfully");
}

TickStatus VulkanApplicationBase::Tick() {
    PreTick();                 // host prologue (default no-op)
    Update();                  // existing per-frame update (derived)
    const bool ok = Render();  // existing per-frame render + device-loss policy (derived)
    PostTick();                // host epilogue (default no-op)
    ++frameCounter_;

    if (ok) {
        if (exitAfterFrames_ > 0 && frameCounter_ >= exitAfterFrames_) {
            return TickStatus::FrameLimitReached;
        }
        return TickStatus::Running;
    }
    // ok == false: name why, from state the derived class exposes. No new policy.
    if (IsShutdownRequested())  return TickStatus::WindowClosed;
    if (IsDeviceLostState())    return TickStatus::DeviceLostUnrecoverable;
    return TickStatus::RenderError;
}

int VulkanApplicationBase::Run(const RunOptions& opts) {
    exitAfterFrames_ = opts.exitAfterFrames;
    try {
        Initialize();
        Prepare();
        if (!IsPrepared()) {
            if (mainLogger) mainLogger->Error("[Run] Prepare failed: " + GetLastError() + " - aborting before render loop");
            DeInitialize();
            return -1;
        }
        if (mainLogger) mainLogger->Info("[Run] Entering render loop...");

        // do/while (not while(Tick()==Running)): the frame-timer must record the tick that HITS
        // the limit too, matching the old main.cpp where the timer block ran unconditionally
        // before the exitAfterFrames check. A condition-gated while would skip recording on the
        // terminal tick, silently dropping the "frames 0-120" summary line on an exact-120 run.
        FrameTimer frameTimer;
        TickStatus st;
        do {
            st = Tick();
            if (opts.enableFrameTimer) frameTimer.Record(frameCounter_, mainLogger.get());
        } while (st == TickStatus::Running);

        if (mainLogger) {
            const char* reason =
                st == TickStatus::WindowClosed           ? "window closed" :
                st == TickStatus::FrameLimitReached      ? "frame limit reached" :
                st == TickStatus::DeviceLostUnrecoverable? "device lost (unrecoverable)" :
                                                           "render error";
            mainLogger->Info(std::string("[Run] Render loop exited: ") + reason);
        }
        DeInitialize();
        return (st == TickStatus::RenderError || st == TickStatus::DeviceLostUnrecoverable) ? -1 : 0;
    } catch (const std::exception& e) {
        if (mainLogger) mainLogger->Error(std::string("[Run] Uncaught exception: ") + e.what());
        return -1;
    } catch (...) {
        if (mainLogger) mainLogger->Error("[Run] Uncaught unknown exception");
        return -1;
    }
}
