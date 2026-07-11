#include "VulkanGraphApplication.h"
#include "VulkanSwapChain.h"
#include "MeshData.h"
#include "Logger.h"
#include <algorithm>   // std::min for the Tiered-ESVO Inc2 M5 scripted zoom clamp
#include <cmath>       // std::tan for the LOD ray-cone (raySizeCoef) computation
#include <filesystem>  // CaptureFrameToPng: exact-path rename (M4b)
#include <cstdlib>     // std::getenv/atoi for VIXEN_WINDOW_WIDTH/HEIGHT overrides

#define GLFW_INCLUDE_NONE   // don't pull in <GL/gl.h> (absent on headless/WSL); Vulkan-only below
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "Core/TypedConnection.h"  // Typed slot connection helpers
#include "Connection/ConnectionModifier.h"  // ConnectionMeta
#include "Connection/Modifiers/FieldExtractionModifier.h"  // ExtractField
#include "Connection/Modifiers/AccumulationSortConfig.h"  // SEL-P3: accumulation-connect sort key (provider fan-in)
#include "CommandBufferUtility.h"  // MVP: File reading utility
#include "MainCacher.h"  // Cache system initialization
#include "Core/LoopManager.h"  // Phase 0.4: Loop system
#include "Core/NodeRegistration.h"  // M3: RegisterAllNodes (decentralized node self-registration)
// M4: graph construction + its ~37 node includes moved to source/graph/Build*Graph.cpp.
// The lifecycle code here needs only WindowNode (live window lookup after the de-own refactor)
// plus the few concrete node types its host-feed seams downcast to: BodyOctreeSceneNode
// (SetBodyInstances), UIRenderNode (GetUiRenderNode), UISelectionProviderNode
// (GetUiSelectionProviderNode). Include-order is load-bearing: BodyOctreeSceneNode.h MUST precede
// UIRenderNode.h (and any RmlUi/robin_hood header) — BodyOctreeSceneNode.h pulls in gaia.h, whose
// std::hash<> specialisations must be visible before RmlUi's bundled robin_hood.h wraps them.
#include "Nodes/WindowNode.h"
#include "Nodes/InputNode.h"
#include "Nodes/BodyOctreeSceneNode.h"        // M-wire: SetBodyInstances() downcast target (gaia — before UIRenderNode.h)
#include "Nodes/CameraNode.h"                 // Sparse-Mip ESVO LOD Inc1 M4c: GetCurrentCameraData() downcast target
#include "Nodes/UIRenderNode.h"               // GetUiRenderNode() downcast target (RmlUi — after BodyOctreeSceneNode.h)
#include "Nodes/UISelectionProviderNode.h"    // GetUiSelectionProviderNode() downcast target
#include "Nodes/SwapChainNode.h"              // CaptureFrameToPng() downcast target (M4b)
#include "Profiler/FrameCapture.h"            // CaptureFrameToPng(): reuse the existing readback->PNG path
#include "Nodes/DeviceNode.h"                 // View Contract Inc-2 Task 5: VulkanDevice* for CaptureHudFrameToPng
#include "Debug/RenderTargetReadback.h"       // View Contract Inc-2 Task 5: IRenderTarget -> PNG readback
#include <sstream>                            // View Contract Inc-2 Task 5: VIXEN_HUD_SCRIPT/_CAPTURE_FRAMES parsing

// Sparse-Mip ESVO LOD Inc1 M4c: the combined residency trigger (M4a resolvability + M4b
// frustum, factored out as a pure/testable function — see ResidencyTrigger.h).
#include "ResidencyTrigger.h"
// Sparse-Mip ESVO LOD Inc2 M3: CPU-side residency occlusion gate (Inc1 M4b's deferred
// spec) — coarse ray-vs-sphere test of a candidate against already brick-resident trees.
#include "OcclusionGate.h"
#include <glm/gtx/norm.hpp>   // glm::distance2 (change-detection epsilon compares)

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
      height(500),
      hudView_(Vixen::App::MakeHudView()) {

    // Perf measurement (perf sweep 2026-07): fixed A/B window sizes without a rebuild.
    // VIXEN_WINDOW_WIDTH / VIXEN_WINDOW_HEIGHT override the 500x500 default when set.
    if (const char* env = std::getenv("VIXEN_WINDOW_WIDTH")) {
        const int v = std::atoi(env);
        if (v > 0) width = v;
    }
    if (const char* env = std::getenv("VIXEN_WINDOW_HEIGHT")) {
        const int v = std::atoi(env);
        if (v > 0) height = v;
    }

    // Enable main logger for application-level logging
    if (mainLogger) {
        mainLogger->SetEnabled(true);
        mainLogger->SetTerminalOutput(true);  // Enable real-time logs to debug recompilation
        mainLogger->Info("VulkanGraphApplication (Graph-based) Starting");
    }
}

VulkanGraphApplication::~VulkanGraphApplication() {
    // Destroy through the bridge (HudView is complete in HudViewBridge.cpp) -- this TU only
    // forward-declares HudView (see VulkanGraphApplication.h's rationale), so `delete hudView_`
    // here directly would be an incomplete-type-delete compile error.
    Vixen::App::DestroyHudView(hudView_);
    DeInitialize();
}

void VulkanGraphApplication::Initialize() {
    mainLogger->Debug("VulkanGraphApplication::Initialize() - START");
    mainLogger->Info("VulkanGraphApplication Initialize START");

    // WSL2: select the provisioned Mesa Dozen ICD before the base creates the Vulkan instance below
    // (no-op off WSL / when already configured). Must precede VulkanApplicationBase::Initialize().
    // Shared with vixen_benchmark (see VulkanGlobalNames.h) so every VIXEN binary uses the same
    // canonical GPU-selection path instead of silently falling back to software Vulkan.
    if (const char* dznIcd = VixenSelectWslGpuIcd()) {
        mainLogger->Info(std::string("[VixenSelectWslGpuIcd] WSL2 GPU: selected Dozen ICD ") + dznIcd);
    }
    if (const char* layerPath = VixenSelectValidationLayerPath()) {
        mainLogger->Info(std::string("[VixenSelectValidationLayerPath] validation layers active (VK_LAYER_PATH=") + layerPath + ")");
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

namespace {
// View Contract Inc-2 Task 5: parses "A@30,B@60" into (frame, payload-id) pairs, where payload-id
// selects one of two hard-coded known HudFactionIn/HudEventIn sets (see PreTick below) — the live
// gate's A/B-on-known-data proof needs a SPECIFIC known payload per capture, not free-form data,
// so the script vocabulary is deliberately just 'A'/'B' rather than the editor's richer toggle/
// undo/redo actions. Malformed tokens are skipped with a warning (mirrors ParseEditorScript's
// never-abort contract), never NULL-widening a whole run over one bad token.
std::vector<std::pair<long, char>> ParseHudScript(const std::string& spec, Vixen::Log::Logger* logger) {
    std::vector<std::pair<long, char>> actions;
    std::stringstream ss(spec);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) continue;
        const size_t at = token.find('@');
        if (at == std::string::npos) {
            if (logger) logger->Warning("[VulkanGraphApplication] VIXEN_HUD_SCRIPT: skipping token missing '@frame': " + token);
            continue;
        }
        const std::string idPart = token.substr(0, at);
        const std::string framePart = token.substr(at + 1);
        if (idPart.size() != 1 || (idPart[0] != 'A' && idPart[0] != 'B')) {
            if (logger) logger->Warning("[VulkanGraphApplication] VIXEN_HUD_SCRIPT: payload id must be 'A' or 'B': " + token);
            continue;
        }
        if (framePart.empty() || framePart.find_first_not_of("0123456789") != std::string::npos) {
            if (logger) logger->Warning("[VulkanGraphApplication] VIXEN_HUD_SCRIPT: skipping token with non-numeric frame: " + token);
            continue;
        }
        actions.emplace_back(std::strtol(framePart.c_str(), nullptr, 10), idPart[0]);
    }
    return actions;
}

// Parses "5,45,75" into frame numbers (identical shape to EditorApplication's ParseCaptureFrames).
std::vector<long> ParseHudCaptureFrames(const std::string& spec, Vixen::Log::Logger* logger) {
    std::vector<long> frames;
    std::stringstream ss(spec);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) continue;
        if (token.find_first_not_of("0123456789") != std::string::npos) {
            if (logger) logger->Warning("[VulkanGraphApplication] VIXEN_HUD_CAPTURE_FRAMES: skipping non-numeric entry: " + token);
            continue;
        }
        frames.push_back(std::strtol(token.c_str(), nullptr, 10));
    }
    return frames;
}
}  // namespace

void VulkanGraphApplication::PreTick() {
    // Own try/catch (mirrors Update()'s) so a malformed script/env value never throws across the tick.
    try {
        if (!hudScriptParsed_) {
            hudScriptParsed_ = true;
            if (const char* scriptEnv = std::getenv("VIXEN_HUD_SCRIPT")) {
                hudScript_ = ParseHudScript(scriptEnv, mainLogger.get());
            }
            if (const char* framesEnv = std::getenv("VIXEN_HUD_CAPTURE_FRAMES")) {
                hudCaptureFrames_ = ParseHudCaptureFrames(framesEnv, mainLogger.get());
            }
            if (const char* dirEnv = std::getenv("VIXEN_HUD_CAPTURE_DIR")) {
                hudCaptureDir_ = dirEnv;
            }
        }

        // Two hard-coded known payloads (A, B) so the live gate can prove the generated binding
        // drives real pixels: A vs B must differ, and the same payload must render byte-identically
        // across two captures (determinism). A is a single known/focused faction with a recent
        // event (juice pulse ON); B is a different faction, different grievance, no recent event
        // (juice pulse OFF) plus one event row — deliberately far apart, not a near-miss tweak.
        for (const auto& [frame, id] : hudScript_) {
            if (frame != hudUpdateTick_) continue;
            if (id == 'A') {
                Vixen::App::HudFactionIn factions[] = {
                    {"acme", 3.5f, true, true, true, 0}  // recentEventAge=0 -> recentChanged (juice ON)
                };
                Vixen::App::HudEventIn events[] = { {"raid", 1} };
                Vixen::App::PushHudView(*hudView_, /*tick=*/hudUpdateTick_, /*bodyCount=*/3, /*activeLens=*/2, /*activeLensCount=*/4,
                                        factions, events);
            } else {  // 'B'
                Vixen::App::HudFactionIn factions[] = {
                    {"umbra", 0.2f, false, false, false, 255}  // recentEventAge=255 -> no recent event (juice OFF)
                };
                Vixen::App::PushHudView(*hudView_, /*tick=*/hudUpdateTick_, /*bodyCount=*/3, /*activeLens=*/1, /*activeLensCount=*/1,
                                        factions, {});
            }
        }
    } catch (const std::exception& e) {
        lastError_ = std::string("PreTick failed: ") + e.what();
        if (mainLogger) mainLogger->Error("[VulkanGraphApplication::PreTick] " + lastError_);
    } catch (...) {
        lastError_ = "PreTick failed: unknown (non-std) exception";
        if (mainLogger) mainLogger->Error("[VulkanGraphApplication::PreTick] " + lastError_);
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

        // Drain the WindowNode's own GLFW callback queue FIRST, unconditionally -- independent of node
        // Execute() (RenderFrame() skips ALL node Execute() while renderPaused, including WindowNode's
        // own, so a Restore/Maximize queued by glfwPollEvents() while minimized would otherwise never
        // reach the bus and renderPaused could never clear: a permanent freeze on minimize). See
        // WindowNode::ProcessPendingEvents() for the full explanation.
        if (renderGraph) {
            if (auto* window = static_cast<WindowNode*>(renderGraph->GetInstance(windowNode_))) {
                window->ProcessPendingEvents();
            }
        }

        // Perf measurement (perf sweep 2026-07): env-gated one-shot programmatic resize that
        // reproduces the "enter wide screen mode" gesture unattended. At update tick
        // VIXEN_RESIZE_AT_FRAME, resize the live window to VIXEN_RESIZE_WIDTH x VIXEN_RESIZE_HEIGHT
        // (defaults 2560x1440) through the exact same GLFW callback -> WindowResizedMessage path a
        // user-driven maximize takes.
        if (renderGraph) {
            static const long resizeAtFrame = [] {
                const char* env = std::getenv("VIXEN_RESIZE_AT_FRAME");
                return env ? std::strtol(env, nullptr, 10) : 0L;
            }();
            static long updateTick = 0;
            ++updateTick;
            if (resizeAtFrame > 0 && updateTick == resizeAtFrame) {
                if (auto* window = static_cast<WindowNode*>(renderGraph->GetInstance(windowNode_))) {
                    if (GLFWwindow* glfwWin = window->GetWindow()) {
                        int w = 2560, h = 1440;
                        if (const char* e = std::getenv("VIXEN_RESIZE_WIDTH"))  { const int v = std::atoi(e); if (v > 0) w = v; }
                        if (const char* e = std::getenv("VIXEN_RESIZE_HEIGHT")) { const int v = std::atoi(e); if (v > 0) h = v; }
                        if (mainLogger) {
                            mainLogger->Info("[PerfProbe] programmatic resize to " + std::to_string(w) + "x" + std::to_string(h)
                                             + " at update tick " + std::to_string(updateTick));
                        }
                        glfwSetWindowSize(glfwWin, w, h);
                    }
                }
            }
        }

        // Sparse-Mip ESVO LOD Inc1 M4c live gate: env-gated scripted camera move, unattended
        // (VIXEN_RESIDENCY_GATE_DEMO=1) — mirrors VIXEN_RESIZE_AT_FRAME's "env-var-scripted
        // behavior for an automated run" shape directly above. Sweeps orbitDistance from far
        // (kOrbitDistanceMax, mip-only range) to near (kOrbitDistanceMin, brick-resolvable
        // range) and back over the run, plus a yaw sweep partway through to exercise the
        // orientation axis — driven via CameraNode::SetOrbitDistanceForTest/SetYawForTest
        // (direct live member writes CameraNode's own ExecuteImpl already reads every frame,
        // no recompile needed), NOT a new InputNode injector (that's a bigger, separate
        // mechanism — see the M4c live-gate investigation this milestone did before choosing
        // this approach).
        if (renderGraph && std::getenv("VIXEN_RESIDENCY_GATE_DEMO")) {
            static long gateTick = 0;
            ++gateTick;
            if (auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
                // 0-300: far (120) -> near (5), sweeping distance-driven residency.
                // 300-450: hold near, sweep yaw 0->2pi, sweeping orientation-driven residency.
                // 450-600: hold yaw, sweep near (5) -> far (120), back out (eviction symmetry).
                constexpr long kPhase1End = 300, kPhase2End = 450, kPhase3End = 600;
                constexpr float kFar = 120.0f, kNear = 5.0f;
                if (gateTick <= kPhase1End) {
                    const float t = static_cast<float>(gateTick) / static_cast<float>(kPhase1End);
                    camera->SetOrbitDistanceForTest(kFar + (kNear - kFar) * t);
                } else if (gateTick <= kPhase2End) {
                    const float t = static_cast<float>(gateTick - kPhase1End) / static_cast<float>(kPhase2End - kPhase1End);
                    camera->SetYawForTest(t * 2.0f * 3.14159265358979323846f);
                } else if (gateTick <= kPhase3End) {
                    const float t = static_cast<float>(gateTick - kPhase2End) / static_cast<float>(kPhase3End - kPhase2End);
                    camera->SetOrbitDistanceForTest(kNear + (kFar - kNear) * t);
                }
                if (gateTick % 60 == 0 && mainLogger) {
                    mainLogger->Info("[ResidencyGateDemo] tick " + std::to_string(gateTick));
                }
            }
        }

        // Tiered-ESVO Inc2 M5 Task 11 live gate: env-gated scripted continuous zoom-out through
        // the single tier crossing proven in M3/M4 (VIXEN_TIER_ZOOM_DEMO=1, run alongside
        // VIXEN_TIER_CROSSING_DEMO=1 + VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE=0.6). Mirrors
        // VIXEN_RESIDENCY_GATE_DEMO's shape directly above (SetOrbitDistanceForTest, no new
        // InputNode injector) but adds a SECOND scripted event this milestone's obligation
        // requires and the residency-gate demo does not: an explicit RequestBrickResidency(true)
        // partway through the SAME continuous run, so the child tree's 0->1 upload transition is
        // exercised WHILE a ray is actively crossing into it, not just proven as two separate
        // before/after runs (M4's own residency evidence was two separate processes; that is the
        // gap this milestone's validator addendum flagged as the one thing M4 did not exercise).
        //
        // Hand-computed schedule (see Progress Log for the full derivation): the demo's tier-
        // crossing leaf is exactly half the parent root's normalized [1,2) extent (scale_exp2=0.5,
        // n=16/brickDepth=3 fixture, root's 8 children all brick-level leaves), the octree's own
        // world span is kWorldGridSize=10 pre-instance-scale, and the instance's renderScale=4.8
        // -> 48 world units per 1.0 of normalized octree scale. The traversal's local ray direction
        // is world-unit-length rotated by worldToLocal (scale 1/kWorldGridSize=1/10, NOT further
        // divided by renderScale -- renderScale only rescales rayOrigin/rayDir going INTO the
        // instance's own local frame in main(), a uniform origin+direction scale that leaves t in
        // real-world-distance units per the same argument this file's instOrigin/instDir comment
        // documents), so 1 unit of the shader's local t-parameter = kWorldGridSize*renderScale=48
        // real-world units. The LOD gate fires when tv_max*raySizeCoef >= scale_exp2=0.5; with
        // VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE=0.6 and tv_max ~= worldDistance/48, this predicts
        // the gate flips at world distance 40.0 EXACTLY (0.6 * (40/48) = 0.5) -- independent of
        // the (much finer) default RaySizeCoefNode value, which would only cross at a wholly
        // unreachable ~15279 units for this 48-unit-diameter demo body. Camera orbits linearly
        // 15 (deep in the "crosses" zone) -> 100 (deep in the "declines" zone) over ticks 1-200
        // (held at 100 for 201-240), so distance=40 lands at tick ~58.8 (tick 59). The residency
        // flip is scripted at tick 24 (distance ~24, still well inside the "crosses" zone) so the
        // mip->real transition is observed BEFORE the later real->mip LOD transition, not
        // confounded with it.
        if (renderGraph && std::getenv("VIXEN_TIER_ZOOM_DEMO")) {
            static long zoomTick = 0;
            ++zoomTick;
            constexpr long  kResidencyFlipTick = 24;
            constexpr long  kPhase1End         = 200;
            constexpr float kNearDist          = 15.0f;
            constexpr float kFarDist           = 100.0f;

            if (auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
                const float t = std::min(1.0f, static_cast<float>(zoomTick) / static_cast<float>(kPhase1End));
                camera->SetOrbitDistanceForTest(kNearDist + (kFarDist - kNearDist) * t);
            }
            if (zoomTick == kResidencyFlipTick) {
                if (auto* bodyScene = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
                        renderGraph->GetInstance(bodyOctreeSceneNode_))) {
                    bodyScene->RequestBrickResidency(true);
                    if (mainLogger) {
                        mainLogger->Info("[TierZoomDemo] tick " + std::to_string(zoomTick) +
                                          ": RequestBrickResidency(true) -- mid-flight residency grant");
                    }
                }
            }
            if (zoomTick % 20 == 0 && mainLogger) {
                mainLogger->Info("[TierZoomDemo] tick " + std::to_string(zoomTick));
            }
        }

        // Tiered-ESVO Inc3 M4 Task 6 (the epic gate): the Earth-scale continuous
        // surface-to-orbit zoom, crossing BOTH real (childScale=2^-10) tier boundaries
        // mid-flight. Run alongside VIXEN_TIER_EARTH_DEMO=1 (+ optionally
        // VIXEN_TIER_EARTH_ZOOM_DEMO=1 to also exercise the mid-flight residency grant).
        //
        // LOG-SPACED (not linear) sweep: the dynamic range spans kNearDist=1e-5 (T2/
        // bedrock-scale detail, per BuildRenderGraph.cpp's own world-unit derivation) to
        // kFarDist=100 (full T0-body orbit view) -- 7 orders of magnitude. A LINEAR sweep
        // would spend ~99.9999% of ticks far from either transition and fly past the
        // hop-1 transition (predicted at world distance ~0.031, see below) in well under
        // one tick. Zooming OUT (near -> far) over kPhase1End ticks, t in [0,1] maps
        // distance = 10^(log10(kNearDist) + t*(log10(kFarDist)-log10(kNearDist))).
        //
        // Prediction-first LOD-handoff derivation (hand-computed BEFORE this schedule was
        // written; full trace in the milestone's Progress Log /
        // Tiered-ESVO-Inc3-M4-earth-scale-derivation.py): the crossing LOD gate fires
        // (declines further descent, falls back to mip-shading) when
        // tv_max*raySizeCoef + raySizeBias >= childScale*scale_exp2 (Inc3 M1's
        // generalized gate). With NO override (VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE
        // unset, the real RaySizeCoefNode value at 1920x1080/45deg FOV,
        // raySizeCoef ~= 7.272e-4), scale_exp2=0.5 (root's own 8 children), raySizeBias=0,
        // and the existing "1 local-t unit = kWorldGridSize*renderScale = 48 world units"
        // conversion (M5's own established derivation):
        //   worldDistance >= 48 * childScale * scale_exp2 / raySizeCoef
        // Hop 0 (T0->T1, childScale=2^-10): worldDistance >= ~32.23 -- this is the point,
        // sweeping OUT, where the ray STOPS crossing into T1 and instead mip-shades T0's
        // own leaf (predicted transition TICK ~372 of 400 at this log-spaced schedule).
        // Hop 1 (T1->T2, childScale=2^-10): the SAME gate re-applied inside T1's own local
        // traversal (Inc3 M3: "gates against that hop's own already-local scale_exp2"),
        // but T1's local units are compressed 1024x (cumulativeDirLen) relative to T0's
        // world units, so in TOP-LEVEL world-distance terms hop 1's decline threshold is
        // 1024x SMALLER: ~32.23/1024 ~= 0.0315 (predicted transition TICK ~200 of 400).
        // Both are well inside [0,400] and well-separated from each other and from the
        // schedule's own endpoints.
        if (renderGraph && std::getenv("VIXEN_TIER_EARTH_DEMO") &&
            (std::getenv("VIXEN_TIER_EARTH_ZOOM_DEMO") || std::getenv("VIXEN_TIER_EARTH_ZOOM_SCRIPT"))) {
            static long earthZoomTick = 0;
            ++earthZoomTick;
            constexpr long  kEarthResidencyFlipTick = 50;   // well before BOTH predicted transitions
            constexpr long  kEarthPhase1End         = 400;
            constexpr double kEarthNearDist         = 1e-5;  // T2/bedrock-scale detail
            constexpr double kEarthFarDist          = 100.0; // full T0-body orbit view

            if (auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
                const double t = std::min(1.0, static_cast<double>(earthZoomTick) / static_cast<double>(kEarthPhase1End));
                const double logNear = std::log10(kEarthNearDist);
                const double logFar  = std::log10(kEarthFarDist);
                const double dist    = std::pow(10.0, logNear + t * (logFar - logNear));
                camera->SetOrbitDistanceForTest(static_cast<float>(dist));

                // M6 finding (see Progress Log for the full derivation): yaw/pitch CANNOT fix
                // the crossing octant's framing here. CameraNode's orbit `forward` is hard-wired
                // to `normalize(orbitCenter - cameraPosition)` (CameraNode.cpp UpdateCameraData) --
                // it always re-targets orbitCenter regardless of yaw/pitch, which only choose
                // WHERE on the orbit sphere the camera sits, not what it looks at. The crossing
                // octant (root child 4) sits at world offset (-12,-12,+12) from
                // orbitCenter=(64,64,64); its angular offset from the camera's forward axis is
                // ~4 deg at the far end (D=100) but explodes past 60-125 deg at the near end
                // (D<20) as camera-to-orbitCenter distance shrinks toward the octant's own fixed
                // 12-17 unit offset. Both LOD-crossing thresholds (~14.92 and ~0.0146 world units,
                // see the derivation comment above) fall well inside that near-field blind zone --
                // the octant only re-enters the ~22.5 deg half-FOV cone around D~=20-25, by which
                // point hop 0's crossing has already declined and hop 1's is 1024x further out of
                // reach. This is a structural mismatch between the body's absolute scale (48 world
                // units) and childScale=2^-10 compounded over 2 hops, not a fixable camera aim;
                // see the M6 Progress Log for the full analysis and options going forward.
            }
            if (earthZoomTick == kEarthResidencyFlipTick && std::getenv("VIXEN_TIER_EARTH_ZOOM_DEMO")) {
                if (auto* bodyScene = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
                        renderGraph->GetInstance(bodyOctreeSceneNode_))) {
                    bodyScene->RequestBrickResidency(true);
                    if (mainLogger) {
                        mainLogger->Info("[TierEarthZoomDemo] tick " + std::to_string(earthZoomTick) +
                                          ": RequestBrickResidency(true) -- mid-flight residency grant");
                    }
                }
            }
            if (earthZoomTick % 20 == 0 && mainLogger) {
                mainLogger->Info("[TierEarthZoomDemo] tick " + std::to_string(earthZoomTick));
            }
        }

        // Tiered-ESVO Inc3 M7 Task 14 (the epic gate, finally): the live continuous
        // surface-to-orbit zoom on VIXEN_TIER_OBSERVABLE_DEMO's reconstructed body,
        // crossing BOTH real magnified tier boundaries mid-flight (childScale=0.25/hop,
        // per Task 13's proven construction). Run alongside VIXEN_TIER_OBSERVABLE_DEMO=1
        // (+ optionally VIXEN_TIER_OBSERVABLE_ZOOM_SCRIPT=1 for the residency grant).
        //
        // Task 13's hand-computed, validator-confirmed handoffs (see Progress Log):
        //   hop0 (T0->T1) ~= 79.58 world units
        //   hop1 (T1->T2) ~= 19.89 world units  (= hop0 * childScale)
        //   in-FOV floor (10*R, where the MARKED OCTANT enters the FOV cone) = 1.00 wu
        //   solid surface radius     =  0.5625 world units
        //   orbit ceiling            =  120.0 world units
        //
        // FIRST ATTEMPT at this schedule used kObsNearDist=5.0 (inside hop1, clear of
        // the in-FOV floor) reasoning that would show "close-in detail" -- captures
        // proved this wrong: the BODY's own visual angular size at distance d is
        // atan(bodyRadius/d) with bodyRadius~=0.56wu, giving only ~6.4deg half-angle
        // at d=5.0 (vs the 22.5deg half-FOV) -- a small dot, not bedrock-scale detail.
        // The in-FOV floor governs when the *crossing octant* enters frame, not when
        // the *body itself* is comfortably sized; those are different distances. The
        // corrected near end (kObsNearDist=1.2, just outside the 0.5625wu solid) gives
        // ~25deg half-angle -- the body genuinely fills the frame, true close-up
        // framing. The far end is now the actual orbit ceiling (120wu) for a true
        // "full-body orbit" endpoint, honestly accepting a documented finding: at the
        // body's own tiny 1.0wu diameter, hop0's distance (79.58wu) leaves the body at
        // only ~0.4deg angular half-size (a few-pixel speck at 500px height) -- the
        // hop0 crossing itself is NOT a dramatic zoomed-in event, it happens when the
        // body has already shrunk far below comfortable viewing size. This is a
        // genuine property of THIS reconstructed body's proportions (LOD-handoff
        // distances chosen for octant reachability/on-axis-ness, not for staying
        // visually close to the body), not a schedule defect -- surfaced honestly in
        // the Progress Log rather than silently reshaping the demo to hide it.
        //
        // Predicted transition ticks (log-interpolation inverted BEFORE running, near=
        // 1.2wu/far=120.0wu over kObsPhase1End=400 ticks):
        //   hop1 (T1->T2 decline, zooming out) -> tick ~244
        //   hop0 (T0->T1 decline, zooming out) -> tick ~364
        if (renderGraph && std::getenv("VIXEN_TIER_OBSERVABLE_DEMO") &&
            (std::getenv("VIXEN_TIER_OBSERVABLE_ZOOM_DEMO") || std::getenv("VIXEN_TIER_OBSERVABLE_ZOOM_SCRIPT"))) {
            static long obsZoomTick = 0;
            ++obsZoomTick;
            constexpr long  kObsResidencyFlipTick = 50;   // well before both predicted transitions (~244, ~364)
            constexpr long  kObsPhase1End         = 400;
            constexpr double kObsNearDist         = 1.2;    // just outside the 0.5625wu solid -- body fills the frame
            constexpr double kObsFarDist          = 120.0;  // full orbit ceiling -- true full-body-orbit endpoint

            if (auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
                const double t = std::min(1.0, static_cast<double>(obsZoomTick) / static_cast<double>(kObsPhase1End));
                const double logNear = std::log10(kObsNearDist);
                const double logFar  = std::log10(kObsFarDist);
                const double dist    = std::pow(10.0, logNear + t * (logFar - logNear));
                camera->SetOrbitDistanceForTest(static_cast<float>(dist));
            }
            if (obsZoomTick == kObsResidencyFlipTick && std::getenv("VIXEN_TIER_OBSERVABLE_ZOOM_DEMO")) {
                if (auto* bodyScene = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
                        renderGraph->GetInstance(bodyOctreeSceneNode_))) {
                    bodyScene->RequestBrickResidency(true);
                    if (mainLogger) {
                        mainLogger->Info("[TierObservableZoomDemo] tick " + std::to_string(obsZoomTick) +
                                          ": RequestBrickResidency(true) -- mid-flight residency grant");
                    }
                }
            }
            if (obsZoomTick % 20 == 0 && mainLogger) {
                mainLogger->Info("[TierObservableZoomDemo] tick " + std::to_string(obsZoomTick));
            }
        }

        // Same "input never rides the render graph's gates" hook, generalized to InputNode
        // (input-rework slice 1): drain its GLFW callback queue unconditionally too, right beside
        // WindowNode's own drain above. Same lookup pattern, same null-guard (a graph without an
        // InputNode -- e.g. the demo/benchmark graph builders -- leaves inputNode_ default-
        // constructed, GetInstance returns null, and this is skipped, matching windowNode_'s
        // existing convention just above).
        if (renderGraph) {
            if (auto* input = static_cast<InputNode*>(renderGraph->GetInstance(inputNode_))) {
                input->ProcessPendingInput();
            }
        }

        // Process events + deferred recompilation here (not in render) so updates run without rendering
        // (minimized windows), at a different rate, and event-driven invalidation is handled.
        if (renderGraph) {
            renderGraph->ProcessEvents();
            renderGraph->RecompileDirtyNodes();
        }

        // Sparse-Mip ESVO LOD Inc1 M4c: re-evaluate the brick-residency trigger + re-sort
        // instances front-to-back against the live camera state. Runs after RecompileDirtyNodes
        // (graph must be settled) and every tick regardless of render-pause state, same as the
        // WindowNode/InputNode drains above — camera state can still change (e.g. a queued
        // resize) while rendering is paused, and this call is cheap/no-op unless the camera
        // actually moved (change-detection lives inside the method itself).
        if (renderGraph) {
            UpdateBodySceneResidency();
        }

        // View Contract Inc-2 Task 5: dump a capture PNG if this tick is scripted for one. Placed
        // at the tail of Update() — Update() ticks BEFORE the render loop's first Render() call
        // (VulkanApplicationBase::Tick(): PreTick -> Update -> Render -> PostTick), so a capture
        // here reads main_swapchain's PREVIOUS frame result (the compute-blit + UI-composite HUD
        // draw already landed on it), same timing as EditorApplication's CaptureFrameToPng call
        // site; a capture at tick 0 would read the target before anything has ever been drawn into
        // it (an all-black PNG) — never schedule frame 0.
        for (const long captureFrame : hudCaptureFrames_) {
            if (captureFrame != hudUpdateTick_) continue;
            const std::string path = hudCaptureDir_ + "/hud_capture_" + std::to_string(hudUpdateTick_) + ".png";
            std::string captureErr;
            if (!CaptureHudFrameToPng(path, captureErr)) {
                if (mainLogger) mainLogger->Error("[VulkanGraphApplication] CaptureHudFrameToPng failed for " + path + ": " + captureErr);
            }
        }
        ++hudUpdateTick_;
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

void VulkanGraphApplication::SetBodyInstances(std::vector<Vixen::SVO::BodyInstanceGpu> instances) {
    // M-wire Task 8: forward the host-side instance list to BodyOctreeSceneNode, which will
    // re-upload the instance SSBO (binding 10) on the next compile tick.
    // Replaces the StarSystemGenerator::Register + MarkVoxelSceneDirty flow for body rendering.
    auto* node = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
        renderGraph->GetInstance(bodyOctreeSceneNode_));
    if (node) {
        node->SetInstances(std::move(instances));
    } else if (mainLogger) {
        mainLogger->Warning("[VulkanGraphApplication::SetBodyInstances] bodyOctreeSceneNode_ not found — bodies not updated");
    }
}

void VulkanGraphApplication::SetRecipePool(Vixen::SVO::ConcatenatedOctrees pool) {
    // Spec B I3/Task 6 (= main's I4.1 passthrough): forward the boot-baked recipe pool
    // (RecipeBootIngest -> BakeRegistryToPool) to BodyOctreeSceneNode, which then serves octree
    // slots 0..N-1 for every render_recipe blob id instead of the hardcoded 3-kind shell/SDF
    // archetypes. Mirrors SetBodyInstances above — same live GetInstance lookup, same null-guard.
    auto* node = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
        renderGraph->GetInstance(bodyOctreeSceneNode_));
    if (node) {
        node->SetRecipePool(std::move(pool));
    } else if (mainLogger) {
        mainLogger->Warning("[VulkanGraphApplication::SetRecipePool] bodyOctreeSceneNode_ not found — recipe pool not applied");
    }
}

void VulkanGraphApplication::RequestBodyBrickResidency(bool resident) {
    // Editor Brick-Residency Fix: forward to BodyOctreeSceneNode::RequestBrickResidency (stash-only
    // dirty-flag write; ExecuteImpl performs the actual upload next frame). Mirrors SetRecipePool
    // above — same live GetInstance lookup, same null-guard.
    auto* node = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
        renderGraph->GetInstance(bodyOctreeSceneNode_));
    if (node) {
        node->RequestBrickResidency(resident);
    } else if (mainLogger) {
        mainLogger->Warning("[VulkanGraphApplication::RequestBodyBrickResidency] bodyOctreeSceneNode_ not found — residency not requested");
    }
}

void VulkanGraphApplication::PushHudView(int tick, int bodyCount, int activeLens, int activeLensCount,
                                         std::span<const Vixen::App::HudFactionIn> factions,
                                         std::span<const Vixen::App::HudEventIn> events) {
    // Forwards through the HudViewBridge seam (never HudView.h directly — this TU sees gaia.h; see
    // the header's robin_hood/ODR rationale). hudView_ exists for the app's whole lifetime (ctor),
    // so this is safe to call any time after construction.
    if (!hudView_) return;
    Vixen::App::PushHudView(*hudView_, tick, bodyCount, activeLens, activeLensCount, factions, events);
}

namespace {
// Sparse-Mip ESVO LOD Inc1 M4c: conservative world-space bounding radius shared by every
// body placed in BuildRenderGraph.cpp's default scenes (kRadius/kHalf, both 24.0f — the
// Procedural sphere radius and the Stored shell's world half-extent are deliberately equal
// so the default camera frames both paths identically; see BuildRenderGraph.cpp's own
// comments at the instance-seeding block). Used for the frustum containment test's sphere
// radius; a generic per-instance radius does not exist in BodyInstanceGpu today (Stored
// uses renderScale, Procedural uses recipeParams[0], different units) and deriving one
// exactly is out of scope for this milestone's binary per-tree gate.
constexpr float kResidencyBoundingRadius = 24.0f;

// One pixel's worth of octree level detail must still matter at the default 1080p-ish
// render target; screenHeightPx is read live from the app's tracked window height each
// call (see UpdateBodySceneResidency) rather than hardcoded, so a resized window doesn't
// silently go stale.
constexpr float kResidencyPxThreshold = 1.0f;
constexpr float kResidencyLeafSizeM   = 0.01f;  // matches ResolvableLevel.h's 1cm-voxel convention
}  // namespace

void VulkanGraphApplication::UpdateBodySceneResidency() {
    // Tiered-ESVO Inc2 M5 Task 11: VIXEN_TIER_ZOOM_DEMO's scripted RequestBrickResidency(true) at
    // tick 24 (see the Update() block above) must be the ONLY residency driver for that run's
    // evidence to be attributable to the milestone's own scripted schedule rather than confounded
    // with this function's independent per-tick frustum/resolvability trigger (which runs every
    // tick regardless and would otherwise immediately re-decide -- and potentially override --
    // residency on the very same tick, since both run inside the same Update() call before
    // ExecuteImpl). This mirrors VIXEN_TIER_CROSSING_NONRESIDENT/VIXEN_RESIDENCY_GATE_DEMO's own
    // precedent of a demo env knob taking deliberate, exclusive control of one subsystem.
    if (std::getenv("VIXEN_TIER_ZOOM_DEMO") || std::getenv("VIXEN_TIER_EARTH_ZOOM_DEMO")) {
        return;
    }

    // Editor Brick-Residency Fix: a host whose body residency is under its own exclusive control
    // (vixen_editor — the one document body is unconditionally resident, see ApplyDocumentToScene)
    // opts out here, same shape as the VIXEN_TIER_ZOOM_DEMO early-return just above. Without this,
    // a static editor camera never satisfies the orbit-tuned frustum/resolvability heuristic below
    // and this call would silently stomp the grant back to false every tick.
    if (SkipResidencyHeuristic()) {
        return;
    }

    // Live lookups — both nodes persist across recompile; never cache (same discipline as
    // GetWindowHandle()/SetBodyInstances() above).
    auto* camera = static_cast<Vixen::RenderGraph::CameraNode*>(renderGraph->GetInstance(cameraNode_));
    auto* bodyScene = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
        renderGraph->GetInstance(bodyOctreeSceneNode_));
    if (!camera || !bodyScene) {
        return;  // no-op pre-Compile or on a graph without these nodes (e.g. demo graphs)
    }

    const Vixen::RenderGraph::CameraData& cam = camera->GetCurrentCameraData();

    // Change-detection only (not part of the trigger formula itself) — re-evaluating every
    // frame is cheap, but SortInstancesFrontToBack is a real (if small) per-frame sort, so
    // skip it on a genuinely static camera. Orientation matters independently of position
    // (M4b: it moves bodies in/out of frustum on its own), so cameraDir is compared too —
    // not just cameraPos/fov.
    constexpr float kPosEpsilon = 1e-4f;
    constexpr float kDirEpsilon = 1e-5f;
    constexpr float kFovEpsilon = 1e-4f;
    const bool changed = !residencyTriggerEverEvaluated_ ||
        glm::distance2(cam.cameraPos, lastResidencyCheckCameraPos_) > kPosEpsilon ||
        glm::distance2(cam.cameraDir, lastResidencyCheckCameraDir_) > kDirEpsilon ||
        std::abs(cam.fov - lastResidencyCheckFovDegrees_) > kFovEpsilon;
    if (!changed) {
        return;
    }
    residencyTriggerEverEvaluated_ = true;
    lastResidencyCheckCameraPos_   = cam.cameraPos;
    lastResidencyCheckCameraDir_   = cam.cameraDir;
    lastResidencyCheckFovDegrees_  = cam.fov;

    // near/far bound the same range CameraNode configures (BuildRenderGraph.cpp: 0.1/500.0).
    const float screenHeightPx = static_cast<float>(height > 0 ? height : 1080);
    const int brickTierLevel = Vixen::RenderGraph::BodyOctreeSceneNode::GetBrickTierLevel();

    // Sparse-Mip ESVO LOD Inc2 M3: the CPU-side residency occlusion gate Inc1 M4b deferred.
    // "Already brick-resident" here means resident as of the LAST re-check (lastResidencyGranted_),
    // not whatever this frame is about to decide — occlusion is tested against what's actually
    // uploaded right now, matching Inc1 M4b's "coarse depth estimate built from already
    // brick-resident trees only" spec. When the shared pool isn't resident yet, occluders is
    // empty and IsOccludedByResidentTrees degrades to always-false (frustum+resolvability-only),
    // the same graceful-degradation path ResidencyTrigger.h itself documents.
    //
    // Each occluder's id is its OWN index in GetInstances() — required so a candidate is never
    // tested against itself below (a tree/pool cannot occlude its own pending residency decision;
    // see OcclusionGate.h's own comment on why this is a correctness fix, not an edge case: a
    // naive full-instance-list occluder set trivially includes the candidate being evaluated
    // whenever residency is a whole-pool decision, and the ray toward a candidate's own centre
    // always "hits" its own bounding sphere well before reaching that centre).
    const auto& instances = bodyScene->GetInstances();
    std::vector<Vixen::SVO::ResidentOccluder> residentOccluders;
    if (lastResidencyGranted_) {
        residentOccluders.reserve(instances.size());
        for (size_t i = 0; i < instances.size(); ++i) {
            const auto& inst = instances[i];
            residentOccluders.push_back(Vixen::SVO::ResidentOccluder{
                glm::vec3(inst.worldPos[0], inst.worldPos[1], inst.worldPos[2]),
                kResidencyBoundingRadius,
                static_cast<int>(i)});
        }
    }

    // Per-tree binary residency (§0 scope): resident if ANY current instance, individually,
    // passes frustum+resolvability (ResidencyTrigger.h) AND is NOT occluded by an
    // already brick-resident tree (OcclusionGate.h, Inc2 M3) — the whole shared brick
    // pool must be populated the moment even one instance needs it and can see it.
    bool anyInstanceWantsBricks = false;
    for (size_t i = 0; i < instances.size(); ++i) {
        const auto& inst = instances[i];
        const glm::vec3 pos(inst.worldPos[0], inst.worldPos[1], inst.worldPos[2]);
        if (!Vixen::SVO::InstanceWantsBrickResidency(
                pos, kResidencyBoundingRadius,
                cam.cameraPos, cam.cameraDir, cam.cameraUp, cam.cameraRight,
                cam.fov, cam.aspect, screenHeightPx, /*nearDist=*/0.1f, /*farDist=*/500.0f,
                brickTierLevel, kResidencyLeafSizeM, kResidencyPxThreshold)) {
            continue;  // fails frustum+resolvability regardless of occlusion
        }
        const float distance = glm::distance(pos, cam.cameraPos);
        if (Vixen::SVO::IsOccludedByResidentTrees(
                cam.cameraPos, pos, distance, residentOccluders, static_cast<int>(i))) {
            continue;  // passes frustum+resolvability but a DIFFERENT already-resident tree blocks it
        }
        anyInstanceWantsBricks = true;
        break;
    }
    if (mainLogger && std::getenv("VIXEN_RESIDENCY_GATE_DEMO")) {
        static bool lastLoggedDecision = false;
        static bool everLogged = false;
        if (!everLogged || lastLoggedDecision != anyInstanceWantsBricks) {
            mainLogger->Info(std::string("[ResidencyGateDemo] RequestBrickResidency(") +
                              (anyInstanceWantsBricks ? "true" : "false") + ") | camDist=" +
                              std::to_string(glm::distance(cam.cameraPos, glm::vec3(0.0f))) +
                              " fov=" + std::to_string(cam.fov));
            lastLoggedDecision = anyInstanceWantsBricks;
            everLogged = true;
        }
    }
    bodyScene->RequestBrickResidency(anyInstanceWantsBricks);
    lastResidencyGranted_ = anyInstanceWantsBricks;  // Inc2 M3: next re-check's occlusion-gate input

    // M4b: re-sort front-to-back so the shader's per-ray gridT.x>bestT occlusion reject
    // (BodyInstanceRayMarch.comp) actually has closer instances visited first in the
    // instance loop — this is the live call site the M4b Progress Log flagged as missing
    // (SortInstancesFrontToBack existed and was tested in isolation, but had zero
    // production call sites until this milestone).
    bodyScene->SortInstancesFrontToBack(cam.cameraPos);
}

GLFWwindow* VulkanGraphApplication::GetWindowHandle() const {
    // Live lookup — the WindowNode owns the window (post-de-own refactor) and persists across
    // recompiles, so never cache the pointer (that was the dangling-window bug the refactor removed).
    auto* window = static_cast<WindowNode*>(renderGraph->GetInstance(windowNode_));
    return window != nullptr ? window->GetWindow() : nullptr;
}

Vixen::RenderGraph::UIRenderNode* VulkanGraphApplication::GetUiRenderNode() const {
    // Live lookup of the composite HUD node (set in BuildRenderGraph). Mirrors GetWindowHandle: never
    // cache the pointer (it persists across recompiles). nullptr when unset — e.g. the VIXEN_UI_DEMO
    // path builds BuildUIGraph (no composite node) and never assigns uiRenderNode_.
    if (!renderGraph) return nullptr;
    return static_cast<Vixen::RenderGraph::UIRenderNode*>(renderGraph->GetInstance(uiRenderNode_));
}

Vixen::RenderGraph::UISelectionProviderNode* VulkanGraphApplication::GetUiSelectionProviderNode() const {
    // Live lookup of the UI selection provider (set in BuildRenderGraph). Mirrors GetUiRenderNode: never
    // cache the pointer (it persists across recompiles). nullptr when unset — e.g. a graph built without
    // the selection provider node.
    if (!renderGraph) return nullptr;
    return static_cast<Vixen::RenderGraph::UISelectionProviderNode*>(
        renderGraph->GetInstance(uiSelectionProviderNode_));
}

bool VulkanGraphApplication::CaptureFrameToPng(const std::string& path) {
    // M4b: headless GPU-frame snapshot. Reuses Vixen::Profiler::FrameCapture (already ships the
    // barrier -> vkCmdCopyImageToBuffer -> map -> BGRA->RGBA swizzle -> stbi_write_png sequence,
    // proven in the BenchmarkRunner path) instead of re-deriving that readback here.
    if (!renderGraph) {
        if (mainLogger) mainLogger->Error("[CaptureFrameToPng] RenderGraph not initialized");
        return false;
    }
    // "main_swapchain" is registered by BuildRenderGraph.cpp. SwapChainNode::CompileImpl already
    // calls SetDevice() on itself from its VULKAN_DEVICE_IN connection, so the node IS the device
    // handle we need too — no separate "main_device" lookup.
    auto* swapChainNode = static_cast<Vixen::RenderGraph::SwapChainNode*>(
        renderGraph->GetNodeByName("main_swapchain"));
    if (!swapChainNode) {
        if (mainLogger) mainLogger->Error("[CaptureFrameToPng] 'main_swapchain' node not found");
        return false;
    }
    auto* device = swapChainNode->GetDevice();
    SwapChainPublicVariables* swapVars = swapChainNode->GetSwapchainPublic();
    if (!device || !swapVars) {
        if (mainLogger) mainLogger->Error("[CaptureFrameToPng] swapchain has no device/public vars yet");
        return false;
    }

    Vixen::Profiler::FrameCapture capture;
    if (!capture.Initialize(device->device, *device->gpu, device->queue, device->graphicsQueueIndex,
                             swapVars->Extent.width, swapVars->Extent.height)) {
        if (mainLogger) mainLogger->Error("[CaptureFrameToPng] FrameCapture::Initialize failed");
        return false;
    }

    // FrameCapture writes to <outputPath>/debug_images/<testName>_frame<N>.png (its test-harness
    // convention) rather than an exact path — capture into the caller's directory under that
    // convention, then rename to the exact path requested.
    std::filesystem::path want(path);
    Vixen::Profiler::CaptureConfig cfg;
    cfg.outputPath = want.parent_path().empty() ? std::filesystem::path(".") : want.parent_path();
    cfg.testName = want.stem().string();
    cfg.frameNumber = 0;
    Vixen::Profiler::CaptureResult result =
        capture.Capture(swapVars, swapChainNode->GetCurrentImageIndex(), cfg);
    if (!result.success) {
        if (mainLogger) mainLogger->Error("[CaptureFrameToPng] capture failed: " + result.errorMessage);
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(result.savedPath, want, ec);
    if (ec) {
        if (mainLogger) mainLogger->Error("[CaptureFrameToPng] rename to '" + path + "' failed: " + ec.message());
        return false;
    }
    if (mainLogger) mainLogger->Info("[CaptureFrameToPng] wrote " + path + " (" +
                                      std::to_string(result.capturedWidth) + "x" +
                                      std::to_string(result.capturedHeight) + ")");
    return true;
}

bool VulkanGraphApplication::CaptureHudFrameToPng(const std::string& path, std::string& err) {
    // Live lookups every call (mirrors EditorApplication::CaptureFrameToPng / GetWindowHandle's
    // rule) -- the target and device node persist across recompile, but re-resolving by name is
    // this codebase's established pattern for host-facing capture lookups.
    //
    // Reads "main_swapchain", NOT "compute_render_target": the compute dispatch blits its
    // offscreen compute_render_target INTO the swapchain, and UIRenderNode's composite pass then
    // LOADs and draws the HUD directly onto that SAME swapchain image (see BuildRenderGraph.cpp's
    // UI-composite-pass comment) -- compute_render_target is a physically separate VkImage that
    // never receives the HUD draw. CaptureSwapchainToPng (unlike the offscreen-target helper)
    // handles the PRESENT_SRC_KHR<->TRANSFER_SRC_OPTIMAL round-trip this capture needs.
    if (!renderGraph) {
        err = "CaptureHudFrameToPng: no render graph";
        return false;
    }
    static constexpr const char* kCaptureTargetName = "main_swapchain";
    auto* targetInst = renderGraph->GetInstanceByName(kCaptureTargetName);
    if (!targetInst) {
        err = std::string("CaptureHudFrameToPng: instance '") + kCaptureTargetName + "' not found";
        return false;
    }
    Resource* targetOutput = targetInst->GetOutput(1, 0);  // SWAPCHAIN_PUBLIC (slot 1)
    if (!targetOutput) {
        err = "CaptureHudFrameToPng: capture target has no SWAPCHAIN_PUBLIC output yet (graph not compiled?)";
        return false;
    }
    auto* renderTarget = targetOutput->GetHandle<Vixen::Vulkan::Resources::IRenderTarget*>();
    if (!renderTarget) {
        err = "CaptureHudFrameToPng: SWAPCHAIN_PUBLIC output handle is null";
        return false;
    }

    auto* deviceInst = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"));
    if (!deviceInst || !deviceInst->GetVulkanDevice()) {
        err = "CaptureHudFrameToPng: 'main_device' not found or has no VulkanDevice";
        return false;
    }
    auto* device = deviceInst->GetVulkanDevice();

    return Vixen::RenderGraph::Debug::CaptureSwapchainToPng(
        device, renderTarget, device->queue, device->graphicsQueueIndex, path, err);
}
