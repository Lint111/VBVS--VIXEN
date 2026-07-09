#pragma once

#include <Headers.h>
#include "error/VulkanError.h"
#include "VulkanInstance.h"
#include "VulkanDevice.h"
#include "Logger.h"

using namespace Vixen::Vulkan::Resources;

// Why the loop stopped (or that it should keep running). Returned by Tick().
// Rich enough that a host (e.g. undertow) driving Tick() directly can branch on the reason,
// where today VulkanGraphApplication::Render()'s single bool collapses all of these.
enum class TickStatus {
    Running,                  // keep looping
    WindowClosed,             // shutdownRequested (user close / WindowCloseEvent); clean exit
    FrameLimitReached,        // RunOptions.exitAfterFrames hit; clean exit
    RenderError,              // Render() returned false for a non-recoverable frame failure
    DeviceLostUnrecoverable,  // device lost and RecoverFromDeviceLoss() gave up
};

// Configures an engine-owned Run().
struct RunOptions {
    uint64_t exitAfterFrames = 0;      // 0 = unlimited. Absorbs the VIXEN_EXIT_AFTER_FRAMES knob.
    bool     enableFrameTimer = false; // opt-in p99/FPS rolling-window logging (standalone main only)
};

/**
 * @brief Base class for Vulkan applications
 * 
 * Provides core Vulkan initialization, device management, and lifecycle methods.
 * Derived classes implement specific rendering strategies (e.g., traditional renderer or graph-based).
 */
class VulkanApplicationBase {
protected:
    VulkanApplicationBase();
    
public:
    virtual ~VulkanApplicationBase();

    // Prevent copying
    VulkanApplicationBase(const VulkanApplicationBase&) = delete;
    VulkanApplicationBase& operator=(const VulkanApplicationBase&) = delete;

    // ====== Core Lifecycle Methods ======

    /**
     * @brief Initialize the Vulkan application
     * 
     * Sets up Vulkan instance, devices, and prepares the rendering subsystem.
     */
    virtual void Initialize();

    /**
     * @brief Prepare the application for rendering
     * 
     * Called after initialization to set up rendering resources.
     */
    virtual void Prepare() = 0;

    /**
     * @brief Update application state
     * 
     * Called each frame to update application logic.
     */
    virtual void Update() = 0;

    /**
     * @brief Render a frame
     * @return true if rendering succeeded, false otherwise
     */
    virtual bool Render() = 0;

    /**
     * @brief Clean up and destroy all Vulkan resources
     */
    virtual void DeInitialize();

    // ====== Canonical run surface (graph.Run() consolidation) ======

    // Per-frame host-injection hooks. Default no-op. A host subclass overrides these to run a
    // prologue/epilogue (e.g. the editor's scripted-action injector, or undertow's sim tick) without
    // the engine knowing about host code. PreTick() runs BEFORE Update(); PostTick() AFTER Render().
    virtual void PreTick()  {}
    virtual void PostTick() {}

    // One loop iteration: PreTick -> Update -> Render -> PostTick, then classify the outcome.
    // Behavior-identical to the old hand-rolled loop body; the ONLY addition is the descriptive return.
    TickStatus Tick();

    // Engine-owned loop: Initialize -> Prepare -> IsPrepared guard -> while(Tick()==Running) ->
    // DeInitialize. One try/catch (never throws past the entry point — UB across the undertow C ABI).
    // Returns a process exit code: 0 clean, -1 on RenderError/DeviceLostUnrecoverable/Prepare-fail.
    int Run(const RunOptions& opts = {});

    // ====== Getters ======

    inline bool IsPrepared() const { return isPrepared; }
    // Last failure message, set when Prepare() (or a frame) fails instead of throwing to the host.
    // Host-facing: a C# host (UNDERTOW) reads IsPrepared()==false + GetLastError() rather than catching
    // a C++ exception (which is UB across the boundary). Empty on success.
    inline const std::string& GetLastError() const { return lastError_; }
    inline VulkanInstance* GetInstance() { return &instanceObj; }
    inline std::shared_ptr<Logger> GetLogger() const { return mainLogger; }

    // Public access for compatibility with existing code
    VulkanInstance instanceObj;                    // Vulkan instance
    std::shared_ptr<Logger> mainLogger;            // Application logger

protected:
    // ====== Protected Helper Methods ======

    /**
     * @brief Create Vulkan instance
     */
    VulkanStatus CreateVulkanInstance(std::vector<const char*>& layers,
                                       std::vector<const char*>& extensions,
                                       const char* applicationName);

    

    /**
     * @brief Enumerate available physical devices
     */
    VulkanStatus EnumeratePhysicalDevices(std::vector<VkPhysicalDevice>& gpuList);


    /**
     * @brief Initialize core Vulkan (instance and device)
     */
    void InitializeVulkanCore();

    // Classification predicates Tick() consults. Base returns false; VulkanGraphApplication overrides
    // to expose its shutdownRequested flag and RenderGraph::IsDeviceLost(). Kept here (not concrete)
    // so Tick()/Run() live on the base without the base depending on RenderGraph.
    virtual bool IsShutdownRequested() const { return false; }
    virtual bool IsDeviceLostState()   const { return false; }

    // Test-only: lets an offline stub set the frame limit without going through Run().
    void SetExitAfterFramesForTest(uint64_t n) { exitAfterFrames_ = n; }

protected:
    // ====== State ======
    bool debugFlag;                                 // Debug mode enabled
    bool isPrepared;                                // Ready to render
    std::string lastError_;                         // Last Prepare/frame failure message (host-readable; empty = ok)
    uint64_t frameCounter_    = 0;   // incremented once per Tick(); replaces the mains' local counter
    uint64_t exitAfterFrames_ = 0;   // set by Run() from RunOptions; 0 = unlimited; read by Tick()
};
