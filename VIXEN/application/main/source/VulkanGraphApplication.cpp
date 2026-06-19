#include "VulkanGraphApplication.h"
#include "VulkanSwapChain.h"
#include "MeshData.h"
#include "Logger.h"
#include <cstdlib>     // std::getenv / ::setenv for the WSL2 Dozen ICD selection
#include <filesystem>  // std::filesystem::exists for the WSL2 Dozen ICD selection

#define GLFW_INCLUDE_NONE   // don't pull in <GL/gl.h> (absent on headless/WSL); Vulkan-only below
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "Core/TypedConnection.h"  // Typed slot connection helpers
#include "Connection/ConnectionModifier.h"  // ConnectionMeta
#include "Connection/Modifiers/FieldExtractionModifier.h"  // ExtractField
#include "CommandBufferUtility.h"  // MVP: File reading utility
#include "MainCacher.h"  // Cache system initialization
#include "Core/LoopManager.h"  // Phase 0.4: Loop system
#include "Core/NodeRegistration.h"  // M3: RegisterAllNodes (decentralized node self-registration)
// M4: graph construction + its ~37 node includes moved to source/graph/Build*Graph.cpp.
// The lifecycle code here needs only WindowNode (live window lookup after the de-own refactor).
#include "Nodes/WindowNode.h"

#include <ShaderBundleBuilder.h>  // Phase G: Shader builder API (includes preprocessor support)
// NOTE: "VoxelRayMarch_CompressedNames.h" was an orphaned include — that combined
// SDI header is no longer generated (the tool now emits per-stage headers such as
// VoxelRayMarch_Compressed_ComputeNames.h) and no symbol from it is referenced here.
#include "VulkanGlobalNames.h"  // Global Vulkan extension/layer name lists

// ============================================================================
// SHADER VARIANT SELECTION (A/B Testing)
// ============================================================================
// Set to 1 to use DXT-compressed shader variant (uses compressed buffers at bindings 7, 8)
// Set to 0 to use uncompressed baseline (uses raw brick data at binding 2)
//
// Performance Testing:
//   Uncompressed baseline: ~200-247 Mrays/sec, 2.01-2.59 ms dispatch @ 800x600
//   Compressed variant: TBD (run A/B comparison)
//
// Memory Impact:
//   Uncompressed: ~5 MB (OctreeBricks at 4 bytes/voxel)
//   Compressed:   ~942 KB (DXT1 colors + DXT normals) = 5.3:1 compression
// ============================================================================
#ifndef USE_COMPRESSED_SHADER
#define USE_COMPRESSED_SHADER 1  // Default: compressed baseline
#endif

VulkanGraphApplication::VulkanGraphApplication()
    : VulkanApplicationBase(),
      currentFrame(0),
      graphCompiled(false),
      width(500),
      height(500) {

    // Enable main logger for application-level logging
    if (mainLogger) {
        mainLogger->SetEnabled(true);
        mainLogger->SetTerminalOutput(true);  // Enable real-time logs to debug recompilation
        mainLogger->Info("VulkanGraphApplication (Graph-based) Starting");
    }
}

VulkanGraphApplication::~VulkanGraphApplication() {
    DeInitialize();
}

namespace {
// On WSL2 the GPU is reachable only via Mesa Dozen (Vulkan-over-D3D12). If the build provisioned
// Dozen (the VIXEN_WSL_DZN_ICD compile-def, set by cmake/ProvisionWslVulkan.cmake) and the user
// hasn't already chosen an ICD, point the Vulkan loader at it before any instance/ICD work. No-op
// off WSL (no /dev/dxg), when VK_ICD_FILENAMES is already set, or when the manifest is missing.
// libd3d12.so is already on the loader path via WSL's ld.wsl.conf, so no LD_LIBRARY_PATH change is
// needed. Native (non-WSL) hosts have no /dev/dxg, so this never alters their behaviour. Returns the
// ICD path it selected (so the caller can log it in member scope), or nullptr if it did nothing.
const char* SelectWslGpuIcd() {
#if defined(__linux__) && defined(VIXEN_WSL_DZN_ICD)
    const char* icd = VIXEN_WSL_DZN_ICD;
    if (icd && icd[0] != '\0'
        && std::filesystem::exists("/dev/dxg")
        && std::getenv("VK_ICD_FILENAMES") == nullptr
        && std::filesystem::exists(icd)) {
        ::setenv("VK_ICD_FILENAMES", icd, /*overwrite=*/0);
        return icd;
    }
#endif
    return nullptr;
}
}  // namespace

void VulkanGraphApplication::Initialize() {
    mainLogger->Debug("VulkanGraphApplication::Initialize() - START");
    mainLogger->Info("VulkanGraphApplication Initialize START");

    // WSL2: select the provisioned Mesa Dozen ICD before the base creates the Vulkan instance below
    // (no-op off WSL / when already configured). Must precede VulkanApplicationBase::Initialize().
    if (const char* dznIcd = SelectWslGpuIcd()) {
        mainLogger->Info(std::string("[SelectWslGpuIcd] WSL2 GPU: selected Dozen ICD ") + dznIcd);
    }

    mainLogger->Debug("About to call VulkanApplicationBase::Initialize()");
    // Initialize base Vulkan core (instance, device)
    VulkanApplicationBase::Initialize();
    mainLogger->Debug("VulkanApplicationBase::Initialize() returned");

    mainLogger->Info("VulkanGraphApplication Base initialized");

    mainLogger->Debug("Instance exported globally");
    mainLogger->Info("VulkanGraphApplication Instance exported globally");

    // AR#7: stand up the engine subsystems via an instantiable EngineContext (was four separate
    // members + the manual create-order here). The context owns registry/bus/graph + the autonomous
    // CalibrationStore and wires MainCacher; the app keeps non-owning views for existing call sites.
    mainLogger->Debug("Creating EngineContext (registry + bus + graph + calibration)");
    Vixen::RenderGraph::EngineConfig engineCfg;
    engineCfg.logger = mainLogger.get();
    engineCfg.calibrationDir = "calibration";
    // M3: nodes self-register into a global manifest (RenderGraphNodes is whole-archived);
    // RegisterAllNodes replays the manifest into this EngineContext's fresh registry. No
    // hand-maintained list — adding a node needs only its own VIXEN_REGISTER_NODE line.
    engineCfg.registerNodeTypes = [this](NodeTypeRegistry& reg) {
        Vixen::RenderGraph::RegisterAllNodes(reg);
        mainLogger->Info("Registered " + std::to_string(reg.GetNodeTypeCount()) +
                         " built-in node types (self-registration)");
    };
    engine_ = std::make_unique<Vixen::RenderGraph::EngineContext>(engineCfg);
    nodeRegistry = &engine_->Registry();
    messageBus   = &engine_->Bus();
    renderGraph  = &engine_->Graph();
    mainLogger->Info("EngineContext created (MainCacher initialized, autonomous CalibrationStore active)");

    mainLogger->Debug("Subscribing to WindowCloseEvent");
    // Subscribe to shutdown events
    messageBus->Subscribe(
        Vixen::EventBus::WindowCloseEvent::TYPE,
        [this](const Vixen::EventBus::BaseEventMessage& msg) -> bool {
            HandleShutdownRequest();
            return true;
        }
    );
    mainLogger->Debug("WindowCloseEvent subscription complete");

    mainLogger->Debug("Subscribing to ShutdownAckEvent");
    messageBus->Subscribe(
        Vixen::EventBus::ShutdownAckEvent::TYPE,
        [this](const Vixen::EventBus::BaseEventMessage& msg) -> bool {
            try {
                const auto* ackMsg = dynamic_cast<const Vixen::EventBus::ShutdownAckEvent*>(&msg);
                if (ackMsg && shutdownRequested) {
                    // Copy string immediately to avoid any lifetime issues
                    std::string systemName = ackMsg->systemName;
                    HandleShutdownAck(systemName);
                }
            } catch (...) {
                // Ignore any errors during shutdown handling
            }
            return true;
        }
    );
    mainLogger->Debug("ShutdownAckEvent subscription complete");

    if (mainLogger) {
        mainLogger->Info("RenderGraph created successfully");
    }

    // (CalibrationStore — autonomous, event-driven: load on DeviceMetadataEvent, save on
    // ApplicationShuttingDownEvent — is now created inside EngineContext above.)

    mainLogger->Debug("Registering physics loop");
    // Phase 0.4: Register loops with the graph
    // Physics loop at 60Hz with multiple-step catchup
    physicsLoopID = renderGraph->RegisterLoop(LoopConfig{
        1.0 / 60.0,  // 60Hz timestep
        "PhysicsLoop",
        LoopCatchupMode::MultipleSteps,
        0.25  // Max 250ms catchup
    });
    mainLogger->Debug("Physics loop registered with ID: " + std::to_string(physicsLoopID));
    mainLogger->Info("Registered PhysicsLoop (60Hz) with ID: " + std::to_string(physicsLoopID));

    // Register sim logic loop at 30Hz (decoupled from render fps; drives the embedded sim)
    simLoopID = renderGraph->RegisterLoop(LoopConfig{
        1.0 / 30.0,                                  // 30Hz logic cadence
        "SimLoop",
        LoopCatchupMode::MultipleSteps,
        0.25  // Max 250ms catchup
    });
    mainLogger->Debug("Sim loop registered with ID: " + std::to_string(simLoopID));
    mainLogger->Info("Registered SimLoop (30Hz) with ID: " + std::to_string(simLoopID));

    if (mainLogger) {
        mainLogger->Info("VulkanGraphApplication initialized successfully");
    }
    mainLogger->Debug("VulkanGraphApplication::Initialize() - COMPLETE");
}

void VulkanGraphApplication::Prepare() {
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[VulkanGraphApplication::Prepare] START");
    }
    isPrepared = false;

    try {
        // PHASE 1: Nodes manage their own resources
        // Build the render graph - nodes allocate their own resources
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[VulkanGraphApplication::Prepare] Calling BuildRenderGraph...");
        }
        BuildRenderGraph();
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[VulkanGraphApplication::Prepare] BuildRenderGraph complete");
        }

        // Compile the render graph - nodes set up their pipelines
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[VulkanGraphApplication::Prepare] Calling CompileRenderGraph...");
        }
        CompileRenderGraph();
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[VulkanGraphApplication::Prepare] CompileRenderGraph complete");
        }

        // The application does NOT own or cache the GLFW window. WindowNode owns it entirely (created
        // in CompileImpl, destroyed only at final teardown). The render loop exits on the
        // shutdownRequested flag, which is set when the WindowNode-published WindowCloseEvent is
        // handled -- the app just runs the graph and listens for the event, never touching the window.

        isPrepared = true;

        if (mainLogger) {
            mainLogger->Info("VulkanGraphApplication prepared and ready to render");
        }
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[VulkanGraphApplication::Prepare] SUCCESS - isPrepared = true");
        }
    }
    catch (const std::exception& e) {
        // Phase 2b (AR#1): do NOT rethrow. Prepare() is host-facing -- the C# UNDERTOW host calls it,
        // and a C++ exception crossing that boundary is undefined behaviour; rethrowing also took the
        // standalone app down via main() -> exit -1. Record the failure and leave isPrepared=false so
        // the caller reports it (GetLastError()) and aborts/retries gracefully instead of crashing.
        lastError_ = std::string("Prepare failed: ") + e.what();
        if (mainLogger) {
            mainLogger->Error("[VulkanGraphApplication::Prepare] " + lastError_);
        }
        isPrepared = false;
    }
    catch (...) {
        lastError_ = "Prepare failed: unknown (non-std) exception";
        if (mainLogger) {
            mainLogger->Error("[VulkanGraphApplication::Prepare] " + lastError_);
        }
        isPrepared = false;
    }
}

bool VulkanGraphApplication::Render() {
    // Exit the render loop once a graceful shutdown has been requested. WindowNode publishes a
    // WindowCloseEvent when the user closes the window; handling it sets shutdownRequested. That flag
    // is the single, window-independent stop signal -- the app neither polls nor owns the GLFW window
    // (without it the loop would keep calling RenderFrame(), a no-op after cleanup per AR#16, forever).
    if (!isPrepared || !graphCompiled || !renderGraph || shutdownRequested) {
        return false;
    }

    // Phase 2c (AR#1): the host-facing tick must NEVER throw -- a C++ exception across a C# host
    // (UNDERTOW) boundary is undefined behaviour. RenderFrame() already catches node-Execute failures
    // (2a); this guard covers anything else (the event-callback handlers fired by glfwPollEvents, etc.).
    try {
        // Pump the OS event queue (main thread, every iteration -- including while minimized). This fires
        // the WindowNode GLFW callbacks that publish WindowCloseEvent / WindowResizeEvent; WindowNode owns
        // the window and its lifecycle.
        glfwPollEvents();

        // Render a complete frame via the graph (it internally handles event processing + deferred
        // recompilation, image acquisition, command recording, queue submission with semaphores, present).
        VkResult result = renderGraph->RenderFrame();

        // Event-driven swapchain recreation is handled internally; VK_ERROR_OUT_OF_DATE_KHR triggers
        // events that mark nodes for recompilation.
        if (result == VK_ERROR_DEVICE_LOST) {
            // AR#1 Phase 3 (Increment 2): self-heal. Rebuild the whole graph on a fresh device and keep
            // rendering — the host just sees a hitched frame. The recovery system lives in the graph; the
            // app merely drives the tick (as it does RecompileDirtyNodes). If the device is genuinely gone
            // the rebuild fails terminally — report it and stop (the host reads GetLastError()).
            mainLogger->Warning("[VulkanGraphApplication::Render] GPU device lost — attempting recovery (rebuild on a fresh device)");
            if (renderGraph->RecoverFromDeviceLoss()) {
                mainLogger->Info("[VulkanGraphApplication::Render] Device recovery succeeded — resuming rendering");
                currentFrame++;
                return true;  // keep the render loop alive
            }
            lastError_ = "GPU device lost (VK_ERROR_DEVICE_LOST) and could not be recovered — the device is unavailable";
            mainLogger->Error("[VulkanGraphApplication::Render] " + lastError_);
            return false;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            mainLogger->Error("Frame rendering failed with result: " + std::to_string(result));
            return false;
        }

        currentFrame++;
        return true;
    } catch (const std::exception& e) {
        lastError_ = std::string("Render failed: ") + e.what();
        if (mainLogger) mainLogger->Error("[VulkanGraphApplication::Render] " + lastError_);
        return false;  // stop the loop; the host reads GetLastError()
    } catch (...) {
        lastError_ = "Render failed: unknown (non-std) exception";
        if (mainLogger) mainLogger->Error("[VulkanGraphApplication::Render] " + lastError_);
        return false;
    }
}

void VulkanGraphApplication::Update() {
    if (!isPrepared) {
        return;
    }

    // Phase 2c: like Render(), the Update tick must not throw to a C# host. ProcessEvents() runs event
    // handlers and RecompileDirtyNodes() already catches per-node compile failures; this guards the rest.
    try {
        // Application time (legacy - kept for compatibility)
        time.Update();

        // Graph time (used by nodes for frame-rate independent animations)
        if (renderGraph) {
            renderGraph->UpdateTime();
        }

        // Process events + deferred recompilation here (not in render) so updates run without rendering
        // (minimized windows), at a different rate, and event-driven invalidation is handled.
        if (renderGraph) {
            renderGraph->ProcessEvents();
            renderGraph->RecompileDirtyNodes();
        }
    } catch (const std::exception& e) {
        // Record + continue: a fatal condition also surfaces via the next Render() (which returns false).
        lastError_ = std::string("Update failed: ") + e.what();
        if (mainLogger) mainLogger->Error("[VulkanGraphApplication::Update] " + lastError_);
    } catch (...) {
        lastError_ = "Update failed: unknown (non-std) exception";
        if (mainLogger) mainLogger->Error("[VulkanGraphApplication::Update] " + lastError_);
    }

    // Note: MVP matrix updates now handled by DescriptorSetNode during Execute()
    // using graph's centralized time system for frame-rate independent rotation
}

void VulkanGraphApplication::DeInitialize() {
    // Prevent double cleanup (called from both main and destructor)
    if (deinitialized) {
        return;
    }
    deinitialized = true;

    // Sprint 6.3: Publish shutdown event BEFORE any cleanup
    // CalibrationStore subscribes and saves automatically
    if (messageBus) {
        auto shutdownEvent = std::make_unique<Vixen::EventBus::ApplicationShuttingDownEvent>(0);
        messageBus->PublishImmediate(*shutdownEvent);  // Immediate - no queue processing
        mainLogger->Info("Published ApplicationShuttingDownEvent");
    }

    // Extract logs BEFORE destroying the render graph
    // With shared_ptr ownership:
    // 1. Nodes still alive → refcount = 2 (node + parent)
    // 2. Extract logs safely
    // 3. Destroy render graph → node destructors drop their refs (refcount 2→1)
    // 4. Loggers stay alive under main logger (refcount = 1)
    // 5. Clear children → refcount 1→0, automatic cleanup
    if (mainLogger && mainLogger->IsEnabled()) {
        try {
            std::string logs = mainLogger->ExtractLogs();
            // Write logs into the binaries folder so logs are colocated with the build artifacts.
            std::ofstream logFile("binaries\\vulkan_app_log.txt");
            if (logFile.is_open()) {
                logFile << logs;
                logFile.close();
                mainLogger->Info("Logs written to binaries\\vulkan_app_log.txt");
            }
        } catch (...) {
            // Best-effort: don't throw during cleanup
        }
    }

    // Clear all child logger references (drops refcount 1→0, automatic cleanup)
    if (mainLogger) {
        mainLogger->ClearChildren();
    }

    // CRITICAL: Destroy render graph (triggers CleanupStack execution) BEFORE base class destroys device
    // NOTE: RenderGraph destructor handles cache saving automatically
    // This ensures all node-owned Vulkan resources (buffers, images, views, shaders) are destroyed
    // while the VkDevice is still valid.
    // Note: ConstantNode's cleanup callback will destroy the shader via registered callback
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[DeInitialize] Destroying render graph...");
    }
    // AR#7: one reset tears down the whole EngineContext in order — calibration -> graph
    // (node cleanup, while the base-class VkDevice below is still valid) -> bus -> registry.
    engine_.reset();
    nodeRegistry = nullptr;
    messageBus = nullptr;
    renderGraph = nullptr;
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[DeInitialize] EngineContext (graph + registry + bus + calibration) destroyed");
    }

    // Graph nodes handle their own cleanup (including window)

    // Call base class cleanup (destroys device and instance)
    if (mainLogger) {
        mainLogger->Info("[DeInitialize] Calling base class DeInitialize...");
    }
    VulkanApplicationBase::DeInitialize();
    if (mainLogger) {
        mainLogger->Info("[DeInitialize] Base class DeInitialize complete");
    }

    if (mainLogger) {
        mainLogger->Info("VulkanGraphApplication deinitialized");
    }
}

void VulkanGraphApplication::CompileRenderGraph() {
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[CompileRenderGraph] START");
    }
    if (!renderGraph) {
        mainLogger->Error("Cannot compile render graph: RenderGraph not initialized");
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Error("[CompileRenderGraph] ERROR: renderGraph is null");
        }
        return;
    }

    // Field extraction now integrated into RenderGraph::Compile() via post-node-compile callbacks
    // Callbacks are registered during RegisterAll() and executed automatically as nodes compile
    renderGraph->Compile();
    graphCompiled = true;

    // Validate final graph
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[CompileRenderGraph] Validating graph...");
    }
    std::string errorMessage;
    if (!renderGraph->Validate(errorMessage)) {
        mainLogger->Error("Render graph validation failed: " + errorMessage);
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Error("[CompileRenderGraph] VALIDATION FAILED: " + errorMessage);
        }
        return;
    }
    mainLogger->Info("Render graph compiled and validated successfully");
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[CompileRenderGraph] Complete - " + std::to_string(renderGraph->GetNodeCount()) + " nodes");
    }
}

void VulkanGraphApplication::HandleShutdownRequest() {
    if (shutdownRequested) {
        return;  // Already shutting down
    }

    mainLogger->Info("Shutdown requested - initiating graceful shutdown sequence");
    shutdownRequested = true;

    // Register systems that need to acknowledge shutdown
    // In this case, we want the RenderGraph to cleanup first
    shutdownAcksPending.insert("RenderGraph");

    // Window handle should already be cached from WindowNode during graph build
    // RenderGraph will cleanup via WindowCloseEvent subscription, then publish ShutdownAckEvent
    mainLogger->Info("Waiting for RenderGraph cleanup acknowledgment...");
}

void VulkanGraphApplication::HandleShutdownAck(const std::string& systemName) {
    if (mainLogger) {
        mainLogger->Info("Received shutdown acknowledgment from: " + systemName);
    }

    auto it = shutdownAcksPending.find(systemName);
    if (it != shutdownAcksPending.end()) {
        shutdownAcksPending.erase(it);
    }

    // Check if all systems have acknowledged
    if (shutdownAcksPending.empty()) {
        if (mainLogger) {
            mainLogger->Info("All systems acknowledged shutdown - destroying window");
        }
        CompleteShutdown();
    } else {
        if (mainLogger) {
            mainLogger->Info("Still waiting for " + std::to_string(shutdownAcksPending.size()) + " system(s) to acknowledge");
        }
    }
}

void VulkanGraphApplication::CompleteShutdown() {
    // All systems have acknowledged shutdown; the RenderGraph (including WindowNode, which destroys the
    // GLFW window in its CleanupImpl) has torn down. There is nothing to do for the window -- the app
    // does not own it. The render loop exits on the shutdownRequested flag (set in
    // HandleShutdownRequest; see Render()).
    if (mainLogger) {
        mainLogger->Info("Shutdown complete - render loop will exit");
    }
}

void VulkanGraphApplication::EnableNodeLogger(NodeHandle handle, bool enableTerminal) {
    // Handle-based API not yet implemented - use string-based version
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Warning("EnableNodeLogger(NodeHandle) not yet implemented - use string version");
    }
}

void VulkanGraphApplication::EnableNodeLogger(const std::string& nodeName, bool enableTerminal) {
    if (!renderGraph) {
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Error("EnableNodeLogger: RenderGraph not initialized");
        }
        return;
    }

    NodeInstance* node = renderGraph->GetNodeByName(nodeName);
    if (!node) {
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Error("EnableNodeLogger: Node '" + nodeName + "' not found");
        }
        return;
    }

    Logger* logger = node->GetLogger();
    if (logger) {
        logger->SetEnabled(true);
        logger->SetTerminalOutput(enableTerminal);
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("Enabled logger for node '" + nodeName + "' (terminal=" + std::to_string(enableTerminal) + ")");
        }
    } else {
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Warning("Node '" + nodeName + "' has no logger");
        }
    }
}

// --- Embedded-sim driver seams -------------------------------------------------------------------

bool VulkanGraphApplication::ShouldStepLogic(double& outDt) {
    const auto* ref = renderGraph->GetLoopManager().GetLoopReference(simLoopID);
    if (ref != nullptr && ref->shouldExecuteThisFrame) {
        outDt = 1.0 / 30.0;                         // the SimLoop's fixed timestep
        return true;
    }
    outDt = 0.0;
    return false;
}

void VulkanGraphApplication::MarkVoxelSceneDirty() {
    renderGraph->MarkNodeNeedsRecompile(voxelGridNode_);
}

GLFWwindow* VulkanGraphApplication::GetWindowHandle() const {
    // Live lookup — the WindowNode owns the window (post-de-own refactor) and persists across
    // recompiles, so never cache the pointer (that was the dangling-window bug the refactor removed).
    auto* window = static_cast<WindowNode*>(renderGraph->GetInstance(windowNode_));
    return window != nullptr ? window->GetWindow() : nullptr;
}
