#include "VulkanGraphApplication.h"
#include "VulkanSwapChain.h"
#include "MeshData.h"
#include "Logger.h"
#include <algorithm>   // std::min for the Tiered-ESVO Inc2 M5 scripted zoom clamp
#include <cmath>       // std::tan for the LOD ray-cone (raySizeCoef) computation
#include <filesystem>  // CaptureFrameToPng: exact-path rename (M4b)
#include <cstdio>      // std::sscanf for VIXEN_TIER_M8_FLIGHT_AIM_OFFSET (M8 Task 23)
#include <cstdlib>     // std::getenv/atoi for VIXEN_WINDOW_WIDTH/HEIGHT overrides
#include <cstring>     // std::memcpy for M5's shadeM5IndirectLumaBits float reinterpretation
#include <unordered_map>  // Sampled Lighting Inc4 M5: VIXEN_DUMP_SYNC_EDGES groupId->name lookup
#include "Core/FrameSyncSchedule.h"  // Sampled Lighting Inc4 M5: VIXEN_DUMP_SYNC_EDGES

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
#include "FrameCapture.h"                      // CaptureFrameToPng(): reuse the existing readback->PNG path
#include "Nodes/DeviceNode.h"                 // View Contract Inc-2 Task 5: VulkanDevice* for CaptureHudFrameToPng
#include "Debug/RenderTargetReadback.h"       // View Contract Inc-2 Task 5: IRenderTarget -> PNG readback
#include <sstream>                            // View Contract Inc-2 Task 5: VIXEN_HUD_SCRIPT/_CAPTURE_FRAMES parsing
#include "Nodes/StorageBufferNode.h"          // Sampled Lighting Inc3 M4: reservoirRecordsA/B readback for the ReSTIR gate
#include "Nodes/ReservoirConfigNode.h"        // Sampled Lighting Inc3 M4: GetLastFrameParity() for the readback's buffer selector
#include "Nodes/LightTreeBufferNode.h"        // Sampled Lighting Inc4 M6: SetLightTreeCut() downcast target for the edit-loop demo's live content flip
#include "Generated/ReservoirRecord.g.h"      // Sampled Lighting Inc3 M4: Vixen::Gpu::ReservoirRecord layout for the readback
#include "LightTree.h"                        // Sampled Lighting Inc3 M4: Vixen::SVO::LightTreeNode for the DIAG readback recomputation

// Sampled Lighting Inc3 M4: the world-transformed light-tree cut for the ReSTIR gate demo
// (VIXEN_RESTIR_GATE_DEMO), stashed by BuildRenderGraph.cpp's demo-scene block and read by
// this file's Update() tick hook to compute a PER-PIXEL brute-force reference (evaluated at
// each pixel's own HitRecord.worldPos) once temporal reservoirs have converged. Plain global
// (process-lifetime-scoped, matching every other VIXEN_*_DEMO env-gated block's own static
// state in this file) -- not a general mechanism, a one-shot gate wire.
std::vector<Vixen::SVO::LightTreeNode>* g_restirGateWorldCut = nullptr;

// Sampled Lighting Inc4 M6: the DDGI edit-loop demo's "real" (source-on) world-transformed
// light-tree cut, stashed by BuildRenderGraph.cpp's VIXEN_DDGI_EDIT_LOOP_DEMO scene block
// (same one-shot-global convention as g_restirGateWorldCut above). The scene starts with an
// EMPTY cut on LightTreeBufferNode (source "off"); this file's readback hook flips the real
// cut in at a chosen tick via LightTreeBufferNode::SetLightTreeCut, exercising a genuine live
// content edit and reading diagNearProbeAvgRadianceLuma's hysteresis convergence afterward.
std::vector<Vixen::SVO::LightTreeNode>* g_ddgiEditLoopWorldCut = nullptr;

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
        // Sampled Lighting Inc3 M4 live gate (VIXEN_RESTIR_GATE_DEMO=1): at a fixed tick (after
        // enough frames for the temporal reservoir to converge -- see kReadbackTick's own
        // rationale comment), map reservoirRecordsA/B (both host-visible/host-coherent, per
        // StorageBufferNode) and sum pixel(targetPdf * UnbiasedWeight) over every VALID
        // reservoir record -- exactly the same per-pixel RIS estimator identity 1's CPU-mirror
        // test already validates (test_reservoir_mirror.cpp's WeightNormalizationFormula*),
        // now read back from the REAL GPU-computed reservoirs instead of a synthetic candidate
        // set.
        //
        // PER-PIXEL brute-force reference (NOT a single hand-picked canonical point): an earlier
        // version of this gate compared the window-averaged GPU estimate against Sum(power_i/
        // dist_i^2) evaluated at ONE assumed camera-facing surface point -- a live-gate DIAG dump
        // (see git history) proved that assumption wrong: recomputing pHat at the assumed point
        // for the SAME node each reservoir actually chose gave ratios vs the shader's own
        // targetPdf that varied 28x-71x across different nodes/pixels -- the signature of a
        // GEOMETRIC mismatch (a uniform scale/unit bug would give a CONSTANT ratio), not a math
        // bug. Root cause: the assumed point doesn't equal the real per-pixel bestWorldPos. Fixed
        // by mapping hit_record_buffer (ALSO host-visible/host-coherent, per StorageBufferNode --
        // same read pattern) and reading each pixel's OWN HitRecord.worldPos (offset 32 in the
        // hand-written std430 layout, see HitRecord.glsl's own field-offset comment) to compute
        // the brute-force reference AT THE SAME POINT each pixel's own RIS estimate is for --
        // genuinely apples-to-apples, no assumed geometry.
        //
        // Read from ONLY the buffer frameParity says is CURRENT this frame (mirrors the
        // shader's own `parityEven` selector in DirectLighting.comp exactly) -- summing BOTH
        // buffers would double-count every pixel, since the buffer that was NOT written this
        // frame still holds its own last-written (stale but non-empty) reservoir content, not
        // zeros.
        if (renderGraph && std::getenv("VIXEN_RESTIR_GATE_DEMO")) {
            static long restirGateTick = 0;
            ++restirGateTick;
            // kReadbackTick: chosen well past the point where ReservoirConfig.temporalCap=32
            // (the default, see ReservoirConfigNode's MakeDefaultReservoirConfig) worth of
            // frames have accumulated into the reservoir AND the accumulation seam's own
            // converging-1/N alpha (Inc2) has had time to settle on a static camera -- 60
            // frames is generously 2x the temporal cap, giving the reservoir's own M-clamp
            // several full refresh cycles to reach its steady-state distribution before the
            // readback fires exactly once.
            constexpr long kReadbackTick = 60;
            if (restirGateTick == kReadbackTick) {
                auto* bufA = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("reservoir_buffer_a"));
                auto* bufB = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("reservoir_buffer_b"));
                auto* hitRecordBuf = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("hit_record_buffer"));
                auto* deviceInst = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"));
                auto* reservoirConfigInst = static_cast<ReservoirConfigNode*>(renderGraph->GetInstanceByName("reservoir_config"));
                auto* renderTargetInst = renderGraph->GetInstanceByName("compute_render_target");
                if (bufA && bufB && hitRecordBuf && deviceInst && deviceInst->GetVulkanDevice() &&
                    reservoirConfigInst && renderTargetInst && g_restirGateWorldCut) {
                    auto* device = deviceInst->GetVulkanDevice();
                    // Ensure every in-flight dispatch that could still be writing these buffers
                    // has fully retired before mapping -- mirrors WorldPosHistoryNode's own
                    // one-shot-transition vkQueueWaitIdle discipline (no partial/torn reads).
                    vkDeviceWaitIdle(device->device);

                    // parityEven mirrors DirectLighting.comp's own selector exactly: frameParity&1==0 -> A is current.
                    const bool parityEven = (reservoirConfigInst->GetLastFrameParity() & 1u) == 0u;
                    StorageBufferNode* currentBuf = parityEven ? bufA : bufB;

                    // RenderTargetNodeConfig::WIDTH_OUT/HEIGHT_OUT are slots 2/3 (see that config's
                    // own OUTPUT_SLOT declarations) -- needed to decode reservoirIdx = y*width+x
                    // (DirectLighting.comp's own flat row-major addressing) back into (x,y) so the
                    // center-window filter below can be applied.
                    Resource* widthRes  = renderTargetInst->GetOutput(2, 0);
                    Resource* heightRes = renderTargetInst->GetOutput(3, 0);
                    const uint32_t imgWidth  = widthRes  ? widthRes->GetHandle<uint32_t>()  : 0u;
                    const uint32_t imgHeight = heightRes ? heightRes->GetHandle<uint32_t>() : 0u;

                    double gpuEstimateSum = 0.0;
                    double bruteForceSum = 0.0;
                    uint64_t validPixels = 0;
                    if (imgWidth > 0 && imgHeight > 0) {
                        void* mapped = currentBuf->MapForReadback(device);
                        void* hitMapped = hitRecordBuf->MapForReadback(device);
                        if (mapped && hitMapped) {
                            const auto* records = reinterpret_cast<const Vixen::Gpu::ReservoirRecord*>(mapped);
                            const size_t count = static_cast<size_t>(currentBuf->GetSizeBytes()) / sizeof(Vixen::Gpu::ReservoirRecord);
                            const uint8_t* hitBytes = reinterpret_cast<const uint8_t*>(hitMapped);
                            constexpr size_t kHitRecordStride = 64;   // HitRecord.glsl's own documented struct size
                            constexpr size_t kHitRecordWorldPosOffset = 32;  // HitRecord.glsl's own documented worldPos offset
                            const uint32_t centerX = imgWidth / 2u, centerY = imgHeight / 2u;
                            constexpr uint32_t kWindowHalf = 8u;  // a 17x17 center window
                            for (size_t i = 0; i < count && i < static_cast<size_t>(imgWidth) * imgHeight; ++i) {
                                const uint32_t px = static_cast<uint32_t>(i % imgWidth);
                                const uint32_t py = static_cast<uint32_t>(i / imgWidth);
                                if (px + kWindowHalf < centerX || px > centerX + kWindowHalf ||
                                    py + kWindowHalf < centerY || py > centerY + kWindowHalf) continue;

                                const Vixen::Gpu::ReservoirRecord& r = records[i];
                                if (r.y == 0xFFFFFFFFu || r.sampleCount == 0u || r.targetPdf <= 0.0f) continue;
                                if (r.y >= g_restirGateWorldCut->size()) continue;

                                const double W = (1.0 / static_cast<double>(r.targetPdf)) *
                                                  (static_cast<double>(r.weightSum) / static_cast<double>(r.sampleCount));
                                gpuEstimateSum += static_cast<double>(r.targetPdf) * W;

                                // This pixel's OWN shading point, read directly from the same
                                // HitRecord the march wrote and DirectLighting.comp's RIS block
                                // itself shaded from -- genuinely the point THIS reservoir's pHat
                                // was evaluated at, not an assumption.
                                float hitWorldPos[3];
                                std::memcpy(hitWorldPos, hitBytes + i * kHitRecordStride + kHitRecordWorldPosOffset, sizeof(hitWorldPos));
                                const glm::vec3 shadingPos(hitWorldPos[0], hitWorldPos[1], hitWorldPos[2]);

                                double pixelBruteForce = 0.0;
                                for (const auto& node : *g_restirGateWorldCut) {
                                    const glm::vec3 toLight = node.worldPos - shadingPos;
                                    const double dist2 = std::max(static_cast<double>(glm::dot(toLight, toLight)), 1e-4);
                                    const double nodePower = static_cast<double>(node.intensity) * static_cast<double>(node.coverage) *
                                        std::pow(static_cast<double>(node.worldExtent), 3.0);
                                    pixelBruteForce += nodePower / dist2;
                                }
                                bruteForceSum += pixelBruteForce;

                                ++validPixels;
                            }
                            currentBuf->UnmapReadback(device);
                            hitRecordBuf->UnmapReadback(device);
                        } else {
                            if (mapped) currentBuf->UnmapReadback(device);
                            if (hitMapped) hitRecordBuf->UnmapReadback(device);
                        }
                    }
                    const double gpuEstimateAvg = validPixels > 0 ? gpuEstimateSum / static_cast<double>(validPixels) : 0.0;
                    const double bruteForceAvg = validPixels > 0 ? bruteForceSum / static_cast<double>(validPixels) : 0.0;

                    if (mainLogger) {
                        if (bruteForceAvg > 0.0 && validPixels > 0) {
                            const double relError = std::fabs(gpuEstimateAvg - bruteForceAvg) / bruteForceAvg;
                            mainLogger->Info("[RestirGateDemo] tick " + std::to_string(restirGateTick) +
                                              ": validPixels=" + std::to_string(validPixels) +
                                              " gpuEstimateAvg=" + std::to_string(gpuEstimateAvg) +
                                              " bruteForcePerPixelAvg=" + std::to_string(bruteForceAvg) +
                                              " relativeError=" + std::to_string(relError));
                        } else {
                            mainLogger->Warning("[RestirGateDemo] tick " + std::to_string(restirGateTick) +
                                                 ": no comparable data (bruteForceAvg=" + std::to_string(bruteForceAvg) +
                                                 ", validPixels=" + std::to_string(validPixels) + ")");
                        }
                    }
                } else if (mainLogger) {
                    mainLogger->Warning("[RestirGateDemo] tick " + std::to_string(restirGateTick) +
                                         ": reservoir/hit-record buffers, device, or the world-cut stash not found -- readback skipped");
                }

                // Sampled Lighting Inc3 M6: SAME equal-error identity, now read from the
                // POST-SPATIAL-COMBINE debug buffer (binding 27, SpatialReuseShade.comp's
                // `spatialReservoirDebug` -- see that shader's own header) instead of
                // DirectLightingNode's pre-spatial buffer above. This is the gap M5's own
                // Progress Log explicitly flagged: M4/M5's gate re-ran the SAME pre-spatial
                // estimator unchanged and deferred "validate the rest of the stack" to M6.
                // Independent block (does not touch/replace the M4/M5 measurement above) so
                // both numbers are visible side-by-side in one gate run.
                {
                    auto* spatialDebugBuf = static_cast<StorageBufferNode*>(
                        renderGraph->GetInstanceByName("spatial_reservoir_debug_buffer"));
                    auto* hitRecordBuf2 = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("hit_record_buffer"));
                    auto* deviceInst2 = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"));
                    auto* renderTargetInst2 = renderGraph->GetInstanceByName("compute_render_target");
                    if (spatialDebugBuf && hitRecordBuf2 && deviceInst2 && deviceInst2->GetVulkanDevice() &&
                        renderTargetInst2 && g_restirGateWorldCut) {
                        auto* device2 = deviceInst2->GetVulkanDevice();
                        vkDeviceWaitIdle(device2->device);

                        Resource* widthRes2  = renderTargetInst2->GetOutput(2, 0);
                        Resource* heightRes2 = renderTargetInst2->GetOutput(3, 0);
                        const uint32_t imgWidth2  = widthRes2  ? widthRes2->GetHandle<uint32_t>()  : 0u;
                        const uint32_t imgHeight2 = heightRes2 ? heightRes2->GetHandle<uint32_t>() : 0u;

                        double gpuEstimateSum2 = 0.0;
                        double bruteForceSum2 = 0.0;
                        uint64_t validPixels2 = 0;
                        if (imgWidth2 > 0 && imgHeight2 > 0) {
                            void* mapped2 = spatialDebugBuf->MapForReadback(device2);
                            void* hitMapped2 = hitRecordBuf2->MapForReadback(device2);
                            if (mapped2 && hitMapped2) {
                                const auto* records2 = reinterpret_cast<const Vixen::Gpu::ReservoirRecord*>(mapped2);
                                const size_t count2 = static_cast<size_t>(spatialDebugBuf->GetSizeBytes()) / sizeof(Vixen::Gpu::ReservoirRecord);
                                const uint8_t* hitBytes2 = reinterpret_cast<const uint8_t*>(hitMapped2);
                                constexpr size_t kHitRecordStride2 = 64;
                                constexpr size_t kHitRecordWorldPosOffset2 = 32;
                                const uint32_t centerX2 = imgWidth2 / 2u, centerY2 = imgHeight2 / 2u;
                                constexpr uint32_t kWindowHalf2 = 8u;
                                for (size_t i = 0; i < count2 && i < static_cast<size_t>(imgWidth2) * imgHeight2; ++i) {
                                    const uint32_t px = static_cast<uint32_t>(i % imgWidth2);
                                    const uint32_t py = static_cast<uint32_t>(i / imgWidth2);
                                    if (px + kWindowHalf2 < centerX2 || px > centerX2 + kWindowHalf2 ||
                                        py + kWindowHalf2 < centerY2 || py > centerY2 + kWindowHalf2) continue;

                                    const Vixen::Gpu::ReservoirRecord& r = records2[i];
                                    // A pixel this frame's SpatialReuseShade.comp never entered the
                                    // ReSTIR block for (reservoirEnabled==0 or !anyHitRT) never wrote
                                    // this buffer -- its content is whatever a PRIOR frame's dispatch
                                    // left there (or uninitialized on frame 1). Guard identically to
                                    // the pre-spatial block: skip anything that doesn't look like a
                                    // genuinely valid THIS-frame reservoir.
                                    if (r.y == 0xFFFFFFFFu || r.sampleCount == 0u || r.targetPdf <= 0.0f) continue;
                                    if (r.y >= g_restirGateWorldCut->size()) continue;

                                    const double W = (1.0 / static_cast<double>(r.targetPdf)) *
                                                      (static_cast<double>(r.weightSum) / static_cast<double>(r.sampleCount));
                                    gpuEstimateSum2 += static_cast<double>(r.targetPdf) * W;

                                    float hitWorldPos2[3];
                                    std::memcpy(hitWorldPos2, hitBytes2 + i * kHitRecordStride2 + kHitRecordWorldPosOffset2, sizeof(hitWorldPos2));
                                    const glm::vec3 shadingPos2(hitWorldPos2[0], hitWorldPos2[1], hitWorldPos2[2]);

                                    double pixelBruteForce2 = 0.0;
                                    for (const auto& node : *g_restirGateWorldCut) {
                                        const glm::vec3 toLight = node.worldPos - shadingPos2;
                                        const double dist2 = std::max(static_cast<double>(glm::dot(toLight, toLight)), 1e-4);
                                        const double nodePower = static_cast<double>(node.intensity) * static_cast<double>(node.coverage) *
                                            std::pow(static_cast<double>(node.worldExtent), 3.0);
                                        pixelBruteForce2 += nodePower / dist2;
                                    }
                                    bruteForceSum2 += pixelBruteForce2;

                                    ++validPixels2;
                                }
                                spatialDebugBuf->UnmapReadback(device2);
                                hitRecordBuf2->UnmapReadback(device2);
                            } else {
                                if (mapped2) spatialDebugBuf->UnmapReadback(device2);
                                if (hitMapped2) hitRecordBuf2->UnmapReadback(device2);
                            }
                        }
                        const double gpuEstimateAvg2 = validPixels2 > 0 ? gpuEstimateSum2 / static_cast<double>(validPixels2) : 0.0;
                        const double bruteForceAvg2 = validPixels2 > 0 ? bruteForceSum2 / static_cast<double>(validPixels2) : 0.0;

                        if (mainLogger) {
                            if (bruteForceAvg2 > 0.0 && validPixels2 > 0) {
                                const double relError2 = std::fabs(gpuEstimateAvg2 - bruteForceAvg2) / bruteForceAvg2;
                                mainLogger->Info("[RestirGateDemoM6PostSpatial] tick " + std::to_string(restirGateTick) +
                                                  ": validPixels=" + std::to_string(validPixels2) +
                                                  " gpuEstimateAvg=" + std::to_string(gpuEstimateAvg2) +
                                                  " bruteForcePerPixelAvg=" + std::to_string(bruteForceAvg2) +
                                                  " relativeError=" + std::to_string(relError2));
                            } else {
                                mainLogger->Warning("[RestirGateDemoM6PostSpatial] tick " + std::to_string(restirGateTick) +
                                                     ": no comparable data (bruteForceAvg=" + std::to_string(bruteForceAvg2) +
                                                     ", validPixels=" + std::to_string(validPixels2) + ")");
                            }
                        }
                    } else if (mainLogger) {
                        mainLogger->Warning("[RestirGateDemoM6PostSpatial] tick " + std::to_string(restirGateTick) +
                                             ": spatial-debug/hit-record buffers, device, or the world-cut stash not found -- readback skipped");
                    }
                }
            }
        }

        // Sampled Lighting Inc4 M4 live gate (VIXEN_DDGI_LEAK_GATE_DEMO=1): every tick, seed
        // ddgi_leak_gate_debug_buffer (ProbeUpdate.comp binding 31) with the near/far probe
        // indices + far shading point BuildRenderGraph.cpp's scene-build block computed
        // (ddgiLeakGateNearProbeIndex_/ddgiLeakGateFarProbeIndex_/ddgiLeakGateFarShadingPos_),
        // BEFORE this tick's Render() dispatches ProbeUpdate.comp (Update() runs before
        // Render() every loop iteration -- VulkanApplicationBase's own PreTick->Update->
        // Render->PostTick order, see that header's own Tick() doc comment), so the shader
        // reads THIS tick's values. chebyshevTestEnabled starts 1 (the real leak-test) for
        // enough ticks to let the irradiance/visibility atlases converge past
        // ProbeGridConfig's own hysteresisRate (default 0.02 -- mirrors AccumulationConfig's
        // own slow-EWMA convergence budget, needing dozens of ticks), reads back
        // gatheredLuma, THEN flips to 0 (the ablation negative control: force
        // visibility=1, bypassing the Chebyshev test) for the SAME re-convergence budget
        // again, and reads back a second time — proving the SAME scene leaks without the
        // mechanism under test (the recipe-epic's own "vary exactly one factor" ablation
        // discipline, per the plan's Task 4).
        // Sampled Lighting Inc4 M5 live-gate instrumentation (VIXEN_M5_SHADE_GATE_DEMO=1,
        // paired with VIXEN_DDGI_LEAK_GATE_DEMO=1): a live-gate finding this milestone made
        // trying two other combinations first -- (1) the DDGI scene alone: its geometry
        // (~world (5-9,8,8)) is inside the probe grid's [0,32) coverage but NOT framed by the
        // default camera orbit (center (64,64,64)), so anyHitRT never fires and the atomicMax
        // stays at its zero floor; (2) VIXEN_RESTIR_GATE_DEMO's own emissive scene: IS
        // camera-framed (its body sits at world ~(40,88), centered on the default orbit) but
        // OUTSIDE the probe grid's [0,32) coverage, so every gatherIndirectDiffuse() sample
        // clamps to boundary probes that never received irradiance -- also zero. The fix:
        // reuse the DDGI scene's OWN probe-grid-aligned geometry (already proven to receive
        // real irradiance via M4's own gatheredLuma=0.015836/0.135071 non-zero readings) and
        // SCRIPT the camera to actually look at it (SetPositionForTest, mirroring the
        // residency gate's own established live-camera-override pattern), rather than
        // reworking either scene's geometry or the shared default orbit other gates depend on.
        if (renderGraph && std::getenv("VIXEN_M5_SHADE_GATE_DEMO") && std::getenv("VIXEN_DDGI_LEAK_GATE_DEMO")) {
            if (auto* m5Camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
                // FIRST ATTEMPT (superseded): SetPositionForTest + SetLookTargetNoOrbitForTest
                // (the FIXED-mode combo CameraNode.h documents for Task 19's flight path) --
                // live-gate finding: this scene's own graph-build-time SetupImpl already
                // configures PARAM_ORBIT_CENTER_* (every scene does, BuildRenderGraph.cpp's
                // shared camera setup), which per SetPositionForTest's OWN doc comment latches
                // orbitActive_ -- UpdateCameraData's ORBIT MODE branch then recomputes
                // cameraPosition = orbitCenter + orbitOffset every frame, silently overriding a
                // FIXED-mode position write (shadeM5IndirectLumaMax stayed 0 through this
                // attempt, confirming the override). FIX: stay in orbit mode and solve for
                // yaw/pitch/orbitDistance such that orbitCenter(64,64,64) + orbitOffset lands at
                // the scripted position (-10,8,8) -- SetOrbitDistanceForTest/SetYawForTest/
                // SetPitchForTest are all read live every ExecuteImpl (unlike the FIXED-mode
                // setters, which conflict with an already-engaged orbit), and SetLookTargetForTest
                // decouples the AIM from orbitCenter while leaving orbitCenter itself as the
                // (irrelevant, since only used for position) pivot.
                // CORRECTED (3rd pass): this scene never configures PARAM_ORBIT_CENTER_* (only
                // VIXEN_TIER_ZOOM_DEMO-style scenes opt into that), so orbit is NEVER engaged by
                // scene setup for THIS demo -- orbitActive_ starts false and stays false unless
                // something calls EngageOrbit(). The 1st attempt's premise (orbit already
                // engaged, so use FIXED-mode setters and get silently overridden) was itself
                // wrong for this specific scene; the 2nd attempt's orbit-mode math still read 0
                // hits, unverifiable without a camera-position getter (none exists, and adding
                // one to this widely-used node for a one-off debug check was reconsidered as too
                // invasive). Reverting to attempt 1's FIXED-mode approach (SetPositionForTest +
                // SetLookTargetNoOrbitForTest, which does NOT call EngageOrbit -- see that
                // setter's own doc comment) is therefore actually correct for this scene: no
                // orbit engagement happens anywhere in this call chain, so FIXED mode's own
                // branch (UpdateCameraData's else-branch, honoring hasLookTarget_) applies
                // cleanly with no override risk.
                // CORRECTED (5th pass): every hand-derived grid->world transform attempt (5.1,8,8
                // then 5.125,13,13) was wrong. Used the AUTHORITATIVE value instead --
                // BuildRenderGraph.cpp's own "DIAG cutNode[0] worldPos=" log line (printed by the
                // VIXEN_DDGI_LEAK_GATE_DEMO scene-build block itself, from the ACTUAL LightTreeCut
                // node after its real grid->world transform) reads worldPos=(10.75,16.75,16.75) --
                // ground truth, not re-derived by hand. Camera placed a few units back along -X,
                // aimed directly at this logged position.
                m5Camera->SetPositionForTest(glm::vec3(0.0f, 16.75f, 16.75f));
                m5Camera->SetLookTargetNoOrbitForTest(glm::vec3(10.75f, 16.75f, 16.75f));

                static bool loggedOnce = false;
                if (!loggedOnce && mainLogger) {
                    glm::vec3 p = m5Camera->GetCameraPositionForTest();
                    glm::vec3 oc = m5Camera->GetOrbitCenterForTest();
                    mainLogger->Info("[M5ShadeGateDemo] DIAG camera pose after script: pos=(" +
                                      std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z) +
                                      ") orbitActive=" + std::to_string(m5Camera->GetOrbitActiveForTest()) +
                                      " orbitCenter=(" + std::to_string(oc.x) + "," + std::to_string(oc.y) + "," + std::to_string(oc.z) + ")");
                    loggedOnce = true;
                }
            }
        }

        if (renderGraph && std::getenv("VIXEN_M5_SHADE_GATE_DEMO")) {
            static long m5ShadeGateTick = 0;
            ++m5ShadeGateTick;
            constexpr long kM5ShadeReadTick = 150;  // well past hysteresis convergence (>>1/0.02=50)

            auto* shadeGateBuf = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("ddgi_leak_gate_debug_buffer"));
            auto* shadeGateDevInst = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"));
            if (shadeGateBuf && shadeGateDevInst && shadeGateDevInst->GetVulkanDevice()) {
                auto* shadeGateDev = shadeGateDevInst->GetVulkanDevice();

                // BUG FOUND live-gating this very check: resetting shadeM5IndirectLumaBits/
                // diagShadeAnyHitCount on tick N and then reading them back in the SAME
                // Update() call (before tick N's OWN Render() has run) captures this tick's
                // just-written zero, not the GPU's prior write -- every earlier attempt's
                // "shadeM5IndirectLumaMax=0.000000 diagShadeAnyHitCount=0" was this bug, not a
                // real camera/geometry problem (confirmed via the CPU-side HitRecord scan
                // below finding 99856/250000 real hits once camera framing was ALSO fixed).
                // Fix: skip the reset on the read tick itself -- read back what the PRIOR
                // tick's reset+dispatch produced, then reset for the tick after.
                if (m5ShadeGateTick != kM5ShadeReadTick) {
                    vkDeviceWaitIdle(shadeGateDev->device);
                    void* m = shadeGateBuf->MapForReadback(shadeGateDev);
                    if (m) {
                        // Only the two leading uint fields matter here (ddgiLeakGateEnabled,
                        // chebyshevTestEnabled) -- the rest of the record (probe indices/
                        // farShadingPos) is irrelevant to this flag-only check and left as
                        // whatever zero-init the buffer already has.
                        auto* flags = reinterpret_cast<uint32_t*>(m);
                        flags[0] = 1u;  // ddgiLeakGateEnabled
                        flags[12] = 0u;  // shadeM5IndirectLumaBits (index 12)
                        flags[13] = 0u;  // diagShadeAnyHitCount (index 13, DIAG temporary)
                        shadeGateBuf->UnmapReadback(shadeGateDev);
                    }
                } else {
                    // Read tick: still need ddgiLeakGateEnabled=1 seeded (harmless -- it was
                    // already 1 from the prior tick's write above), but do NOT touch
                    // flags[12]/[13] here -- that would clobber the very value being read below.
                    vkDeviceWaitIdle(shadeGateDev->device);
                    void* m = shadeGateBuf->MapForReadback(shadeGateDev);
                    if (m) {
                        auto* flags = reinterpret_cast<uint32_t*>(m);
                        flags[0] = 1u;
                        shadeGateBuf->UnmapReadback(shadeGateDev);
                    }
                }

                if (m5ShadeGateTick == kM5ShadeReadTick) {
                    vkDeviceWaitIdle(shadeGateDev->device);
                    void* rm = shadeGateBuf->MapForReadback(shadeGateDev);
                    if (rm) {
                        const auto* bits = reinterpret_cast<const uint32_t*>(rm);
                        float luma;
                        std::memcpy(&luma, &bits[12], sizeof(float));
                        if (mainLogger) {
                            mainLogger->Info("[M5ShadeGateDemo] tick " + std::to_string(m5ShadeGateTick) +
                                              ": shadeM5IndirectLumaMax=" + std::to_string(luma) +
                                              " diagShadeAnyHitCount=" + std::to_string(bits[13]));
                        }
                        shadeGateBuf->UnmapReadback(shadeGateDev);
                    }

                    // DIAG (temporary, M5 debugging): direct CPU-side HitRecord scan,
                    // bypassing SpatialReuseShade.comp's own atomic counter entirely --
                    // disambiguates "the shader's atomic never ran/never saw ddgiLeakGateEnabled"
                    // from "the march genuinely has zero HITRECORD_FLAG_HIT pixels this scene".
                    auto* hrBuf = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("hit_record_buffer"));
                    if (hrBuf) {
                        void* hrMapped = hrBuf->MapForReadback(shadeGateDev);
                        if (hrMapped) {
                            const size_t byteSize = static_cast<size_t>(hrBuf->GetSizeBytes());
                            constexpr size_t kHitRecordStride = 64;  // sizeof(HitRecord), see HitRecord.glsl
                            constexpr size_t kFlagsOffset = 44;      // HitRecord.glsl's own std430 layout comment
                            const auto* bytes = reinterpret_cast<const uint8_t*>(hrMapped);
                            size_t hitCount = 0;
                            const size_t recordCount = byteSize / kHitRecordStride;
                            for (size_t i = 0; i < recordCount; ++i) {
                                uint32_t flagsField;
                                std::memcpy(&flagsField, bytes + i * kHitRecordStride + kFlagsOffset, sizeof(uint32_t));
                                if (flagsField & 0x1u) ++hitCount;  // HITRECORD_FLAG_HIT
                            }
                            if (mainLogger) {
                                mainLogger->Info("[M5ShadeGateDemo] DIAG CPU-side HitRecord scan: " +
                                                  std::to_string(hitCount) + "/" + std::to_string(recordCount) + " pixels hit");
                            }
                            hrBuf->UnmapReadback(shadeGateDev);
                        }
                    }
                }
            }
        }

        if (renderGraph && std::getenv("VIXEN_DDGI_LEAK_GATE_DEMO")) {
            static long ddgiGateTick = 0;
            ++ddgiGateTick;

            constexpr long kConvergeTicks       = 120;  // >> 1/hysteresisRate(0.02)=50 ticks to steady-state
            constexpr long kChebyshevReadTick   = kConvergeTicks;
            constexpr long kAblationReadTick    = 2 * kConvergeTicks;

            auto* debugBuf = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("ddgi_leak_gate_debug_buffer"));
            auto* deviceInstDdgi = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"));

            if (debugBuf && deviceInstDdgi && deviceInstDdgi->GetVulkanDevice()) {
                auto* deviceDdgi = deviceInstDdgi->GetVulkanDevice();

                // Seed this tick's params EVERY tick (mirrors ProbeGridConfigNode::ExecuteImpl's
                // own per-frame re-upload — cheap, keeps the buffer ready with no separate
                // "only write once" bookkeeping). chebyshevTestEnabled: 1 through
                // kChebyshevReadTick (inclusive), then 0 (the ablation phase) thereafter.
                struct DDGILeakGateDebugHost {
                    uint32_t ddgiLeakGateEnabled;
                    uint32_t chebyshevTestEnabled;
                    uint32_t nearProbeIndex;
                    uint32_t farProbeIndex;
                    float    farShadingPosX, farShadingPosY, farShadingPosZ;
                    float    gatheredLuma;
                    // DIAG (temporary, M4 debugging -- remove before final commit): see
                    // ProbeUpdate.comp's own DDGILeakGateDebug struct for what these carry.
                    uint32_t diagNearProbeHitCount;
                    float    diagNearProbeAvgRadianceLuma;
                    float    diagNearProbeAvgDepth;
                    float    diagNearProbeAvgDepth2;
                    // M5 ADDITION: SpatialReuseShade.comp's own production 8-probe trilinear
                    // gather luma, atomicMax bit-cast (see ProbeUpdate.comp's DDGILeakGateDebug
                    // struct doc comment) -- CPU zeroes this each tick before readback.
                    uint32_t shadeM5IndirectLumaBits;
                    uint32_t diagShadeAnyHitCount;  // DIAG (temporary, M5 debugging)
                    // M6 ADDITION: post-hysteresis-blend atlas luma for nearProbeIndex -- see
                    // ProbeUpdate.comp's own DDGILeakGateDebug struct doc comment for why this
                    // is distinct from diagNearProbeAvgRadianceLuma above (that field is the
                    // RAW pre-blend per-tick value, not the EWMA-converging one).
                    float    diagNearProbeBlendedAtlasLuma;
                };
                static_assert(sizeof(DDGILeakGateDebugHost) == 60, "must match ProbeUpdate.comp's DDGILeakGateDebug std430 layout (60B, M6 added diagNearProbeBlendedAtlasLuma)");

                // vkDeviceWaitIdle before EVERY seed write, not just the two readback ticks:
                // this buffer is a single fixed allocation (no per-frame-in-flight ring, unlike
                // ProbeGridConfigNode's own upload ring), so a host write racing a still-in-
                // flight dispatch's read of the SAME memory would be a genuine data race under
                // Vulkan's external-synchronization model. Acceptable here (gate/demo-only path,
                // not a production hot loop -- same "blocking is fine for an unattended capture
                // harness" precedent RenderTargetReadback.h's own helpers already use).
                vkDeviceWaitIdle(deviceDdgi->device);
                void* seedMapped = debugBuf->MapForReadback(deviceDdgi);
                if (seedMapped) {
                    auto* rec = reinterpret_cast<DDGILeakGateDebugHost*>(seedMapped);
                    rec->ddgiLeakGateEnabled   = 1u;
                    rec->chebyshevTestEnabled  = (ddgiGateTick <= kChebyshevReadTick) ? 1u : 0u;
                    rec->nearProbeIndex        = ddgiLeakGateNearProbeIndex_;
                    rec->farProbeIndex         = ddgiLeakGateFarProbeIndex_;
                    rec->farShadingPosX        = ddgiLeakGateFarShadingPos_.x;
                    rec->farShadingPosY        = ddgiLeakGateFarShadingPos_.y;
                    rec->farShadingPosZ        = ddgiLeakGateFarShadingPos_.z;
                    // gatheredLuma left as whatever the GPU last wrote — it's an OUTPUT field,
                    // overwriting it here would just be clobbered by the shader's own write anyway.
                    // shadeM5IndirectLumaBits IS zeroed every tick: atomicMax needs a per-tick-
                    // fresh floor (0 == floatBitsToUint(0.0), the correct "no contribution yet"
                    // sentinel for a non-negative luma value), unlike gatheredLuma above which is
                    // a single deterministic (non-atomic) write, not an accumulating max.
                    rec->shadeM5IndirectLumaBits = 0u;
                    rec->diagShadeAnyHitCount = 0u;  // DIAG (temporary, M5 debugging): reset each tick too
                    debugBuf->UnmapReadback(deviceDdgi);
                }

                if (ddgiGateTick == kChebyshevReadTick || ddgiGateTick == kAblationReadTick) {
                    // Ensure this tick's dispatch (which wrote gatheredLuma using the params just
                    // seeded above) has fully retired before reading it back — same
                    // vkDeviceWaitIdle discipline the RESTIR gate's own readback uses.
                    vkDeviceWaitIdle(deviceDdgi->device);
                    void* readMapped = debugBuf->MapForReadback(deviceDdgi);
                    if (readMapped) {
                        const auto* rec = reinterpret_cast<const DDGILeakGateDebugHost*>(readMapped);
                        const char* label = (ddgiGateTick == kChebyshevReadTick) ? "ChebyshevEnabled" : "AblationNegativeControl";
                        float shadeM5IndirectLuma;
                        std::memcpy(&shadeM5IndirectLuma, &rec->shadeM5IndirectLumaBits, sizeof(float));
                        if (mainLogger) {
                            mainLogger->Info(std::string("[DdgiLeakGateDemo] tick ") + std::to_string(ddgiGateTick) +
                                              " [" + label + "]: nearProbeIndex=" + std::to_string(rec->nearProbeIndex) +
                                              " farProbeIndex=" + std::to_string(rec->farProbeIndex) +
                                              " gatheredLuma=" + std::to_string(rec->gatheredLuma) +
                                              " shadeM5IndirectLumaMax=" + std::to_string(shadeM5IndirectLuma) +
                                              " DIAG diagNearProbeHitCount=" + std::to_string(rec->diagNearProbeHitCount) +
                                              " diagNearProbeAvgRadianceLuma=" + std::to_string(rec->diagNearProbeAvgRadianceLuma) +
                                              " diagNearProbeAvgDepth=" + std::to_string(rec->diagNearProbeAvgDepth) +
                                              " diagNearProbeAvgDepth2=" + std::to_string(rec->diagNearProbeAvgDepth2) +
                                              " diagShadeAnyHitCount=" + std::to_string(rec->diagShadeAnyHitCount));
                        }
                        debugBuf->UnmapReadback(deviceDdgi);
                    }
                }
            } else if (mainLogger && ddgiGateTick == 1) {
                mainLogger->Warning("[DdgiLeakGateDemo] debug buffer or device not found -- gate readback skipped");
            }
        }

        // Sampled Lighting Inc4 M6: edit-loop responsiveness gate (VIXEN_DDGI_EDIT_LOOP_DEMO=1).
        // Reuses VIXEN_DDGI_LEAK_GATE_DEMO's own scene/near-probe/DDGILeakGateDebug plumbing
        // (BuildRenderGraph.cpp's isEditLoopMode branch built the SAME geometry but started
        // LightTreeBufferNode with an EMPTY cut -- source "off"). This hook flips in the real,
        // stashed cut (g_ddgiEditLoopWorldCut) at kContentAddTick -- a genuine live scene-
        // content edit, not a restart -- then samples diagNearProbeAvgRadianceLuma (the near
        // probe's own post-hysteresis-blend irradiance, already written every tick regardless
        // of ddgiLeakGateEnabled's near/far-gather fields) at several ticks before/after to
        // show BOUNDED convergence within the hysteresis window, per the plan's own explicit
        // "not instant, but bounded" gate requirement.
        if (renderGraph && std::getenv("VIXEN_DDGI_EDIT_LOOP_DEMO")) {
            static long editLoopTick = 0;
            ++editLoopTick;

            // kConvergeTicks mirrors the leak-gate's own >>1/hysteresisRate(0.02)=50-tick
            // derivation. kContentAddTick: content starts OFF, giving several ticks of a
            // genuine steady-zero baseline before the edit. Sample ticks span before the edit,
            // immediately after (still converging), and past kConvergeTicks ticks after
            // (expected converged) -- enough points to show the convergence CURVE, not just
            // two endpoints.
            constexpr long kConvergeTicks   = 120;
            constexpr long kContentAddTick  = 30;
            constexpr long kSampleTicks[]   = {20, 31, 50, 80, 110, 150, 30 + kConvergeTicks};

            auto* debugBuf = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("ddgi_leak_gate_debug_buffer"));
            auto* deviceInstEditLoop = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"));

            if (debugBuf && deviceInstEditLoop && deviceInstEditLoop->GetVulkanDevice()) {
                auto* deviceEditLoop = deviceInstEditLoop->GetVulkanDevice();

                // The live content edit itself: flip LightTreeBufferNode from empty to the
                // stashed real cut exactly once, at kContentAddTick. SetLightTreeCut only
                // stashes the vector (uploaded next ExecuteImpl) -- same "host calls a setter,
                // node picks it up next frame" seam SetInstances uses (see
                // BodyOctreeSceneNode.h's own doc comment), so this is a genuine mid-run
                // content mutation, not a scene rebuild.
                if (!ddgiEditLoopContentAdded_ && editLoopTick >= kContentAddTick && g_ddgiEditLoopWorldCut) {
                    if (auto* lightTreeInst = static_cast<LightTreeBufferNode*>(renderGraph->GetInstanceByName("light_tree_buffer"))) {
                        lightTreeInst->SetLightTreeCut(*g_ddgiEditLoopWorldCut);
                        ddgiEditLoopContentAdded_ = true;
                        if (mainLogger) {
                            mainLogger->Info("[DdgiEditLoopDemo] tick " + std::to_string(editLoopTick) +
                                              ": flipped light-tree cut from empty to real (" +
                                              std::to_string(g_ddgiEditLoopWorldCut->size()) +
                                              " nodes) -- source now ON");
                        }
                    }
                }

                // Same DDGILeakGateDebugHost layout the leak-gate hook above uses -- only
                // ddgiLeakGateEnabled + the near-probe DIAG fields matter here (near/far-
                // gather/Chebyshev fields are irrelevant to a single-probe convergence read,
                // left at their seeded defaults).
                struct DDGILeakGateDebugHost {
                    uint32_t ddgiLeakGateEnabled;
                    uint32_t chebyshevTestEnabled;
                    uint32_t nearProbeIndex;
                    uint32_t farProbeIndex;
                    float    farShadingPosX, farShadingPosY, farShadingPosZ;
                    float    gatheredLuma;
                    uint32_t diagNearProbeHitCount;
                    float    diagNearProbeAvgRadianceLuma;
                    float    diagNearProbeAvgDepth;
                    float    diagNearProbeAvgDepth2;
                    uint32_t shadeM5IndirectLumaBits;
                    uint32_t diagShadeAnyHitCount;
                    float    diagNearProbeBlendedAtlasLuma;  // M6: the field this gate actually needs
                };
                static_assert(sizeof(DDGILeakGateDebugHost) == 60, "must match ProbeUpdate.comp's DDGILeakGateDebug std430 layout");

                vkDeviceWaitIdle(deviceEditLoop->device);
                void* seedMapped = debugBuf->MapForReadback(deviceEditLoop);
                if (seedMapped) {
                    auto* rec = reinterpret_cast<DDGILeakGateDebugHost*>(seedMapped);
                    rec->ddgiLeakGateEnabled  = 1u;
                    rec->chebyshevTestEnabled = 1u;
                    rec->nearProbeIndex       = ddgiLeakGateNearProbeIndex_;
                    rec->farProbeIndex        = ddgiLeakGateFarProbeIndex_;
                    rec->farShadingPosX       = ddgiLeakGateFarShadingPos_.x;
                    rec->farShadingPosY       = ddgiLeakGateFarShadingPos_.y;
                    rec->farShadingPosZ       = ddgiLeakGateFarShadingPos_.z;
                    rec->shadeM5IndirectLumaBits = 0u;
                    rec->diagShadeAnyHitCount    = 0u;
                    debugBuf->UnmapReadback(deviceEditLoop);
                }

                for (long sampleTick : kSampleTicks) {
                    if (editLoopTick == sampleTick) {
                        vkDeviceWaitIdle(deviceEditLoop->device);
                        void* readMapped = debugBuf->MapForReadback(deviceEditLoop);
                        if (readMapped) {
                            const auto* rec = reinterpret_cast<const DDGILeakGateDebugHost*>(readMapped);
                            if (mainLogger) {
                                mainLogger->Info(std::string("[DdgiEditLoopDemo] tick ") + std::to_string(editLoopTick) +
                                                  (editLoopTick < kContentAddTick ? " [pre-edit]" : " [post-edit]") +
                                                  ": diagNearProbeBlendedAtlasLuma=" + std::to_string(rec->diagNearProbeBlendedAtlasLuma) +
                                                  " diagNearProbeAvgRadianceLuma(rawThisTick)=" + std::to_string(rec->diagNearProbeAvgRadianceLuma) +
                                                  " diagNearProbeHitCount=" + std::to_string(rec->diagNearProbeHitCount) +
                                                  " diagNearProbeAvgDepth=" + std::to_string(rec->diagNearProbeAvgDepth));
                            }
                            debugBuf->UnmapReadback(deviceEditLoop);
                        }
                        break;
                    }
                }
            } else if (mainLogger && editLoopTick == 1) {
                mainLogger->Warning("[DdgiEditLoopDemo] debug buffer or device not found -- gate readback skipped");
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

        // Sampled Lighting Inc3 M5 live gate: env-gated continuous yaw orbit
        // (VIXEN_RESTIR_ORBIT_DEMO=1), scene-agnostic so it composes with whatever
        // VIXEN_RESTIR_GATE_DEMO built. Mirrors VIXEN_RESIDENCY_GATE_DEMO's own yaw-sweep
        // phase directly above (SetYawForTest, no new InputNode injector) -- this demo
        // exists to exercise temporal reservoir reprojection + spatial reuse under
        // continuous camera motion (the no-ghosting/temporal-stability gate), a full
        // 0->2pi sweep held over the whole run rather than a phase within a larger
        // distance sweep.
        if (renderGraph && std::getenv("VIXEN_RESTIR_ORBIT_DEMO")) {
            static long orbitTick = 0;
            ++orbitTick;
            constexpr long kOrbitPeriodTicks = 300;
            if (auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
                const float t = static_cast<float>(orbitTick % kOrbitPeriodTicks) /
                                static_cast<float>(kOrbitPeriodTicks);
                camera->SetYawForTest(t * 2.0f * 3.14159265358979323846f);
            }
            if (orbitTick % 60 == 0 && mainLogger) {
                mainLogger->Info("[RestirOrbitDemo] tick " + std::to_string(orbitTick));
            }
        }

        // Tiered-ESVO Inc3 M8 Task 16 live gate: one-shot proof that a look-target genuinely
        // decouples `forward` from orbitCenter (CameraNode::SetLookTargetForTest). Deliberately
        // NOT part of any tier-crossing scene (VIXEN_LOOK_TARGET_DEMO runs standalone, alongside
        // whatever scene is otherwise active) -- this demo only exists to prove the CAPABILITY,
        // not to build the Task 17/18 Earth-scale demo. Aims the camera hard to one side of
        // orbitCenter at tick 1 (one-shot, mirrors the RequestBrickResidency one-shot pattern
        // above) so a later HUD capture shows a visibly different view than the same tick with
        // the env var unset.
        if (renderGraph && std::getenv("VIXEN_LOOK_TARGET_DEMO")) {
            static long lookTargetTick = 0;
            ++lookTargetTick;
            if (lookTargetTick == 1) {
                if (auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
                    // Orbit default is centered on (5,5,5); aim far off to one side instead.
                    camera->SetLookTargetForTest(glm::vec3(60.0f, 5.0f, 5.0f));
                    if (mainLogger) {
                        mainLogger->Info("[LookTargetDemo] tick 1: SetLookTargetForTest(60,5,5)");
                    }
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

        // Tiered-ESVO Inc3 M8 Task 17 (the epic's literal headline): the TRUE Earth-scale
        // (childScale=2^-10/hop) surface-to-orbit transition, using Task 16's CameraNode
        // look-target decoupling to keep the marked crossing octant framed through its
        // handoff -- the capability M6/M7 proved is REQUIRED for the off-axis problem (their
        // own R-invariance findings: a camera that always looks at body center cannot frame
        // an octant sitting far off-axis at 2^-10's near/mid-field distances). Run alongside
        // VIXEN_TIER_M8_EARTH_DEMO=1 (+ VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE=2.8935e-4, see
        // that demo's own comment) + VIXEN_TIER_M8_EARTH_ZOOM_DEMO=1 (or _ZOOM_SCRIPT=1 to
        // skip residency).
        //
        // TWO INDEPENDENT CONSTRAINTS AT TRUE 2^-10 (both hand-derived BEFORE writing this
        // schedule, the SECOND one found only after a first live-capture attempt failed --
        // see the M8 Progress Log for the full derivation and the failed attempt's own
        // captures):
        //  (1) OFF-AXIS ANGLE (M6/M7's finding): the marked octant sits at a FIXED world
        //      offset (-2.5R,-2.5R,+2.5R) from body center, independent of orbit distance --
        //      a camera that always looks at body center sees it far outside the 22.5deg
        //      half-FOV cone through most of a near/mid-field orbit. FIXED by look-target
        //      retargeting (this task's new capability): aim at the octant's own recorded
        //      world position (m8EarthHop0OctantWorld_) instead of body center.
        //  (2) SOLID-RADIUS/HOP-DISTANCE COUPLING (a SECOND, independent constraint this
        //      task discovered live, NOT solved by retargeting): the calibrated LOD-gate
        //      formula (hop0=20*R*childScale*scale_exp2/raySizeCoef, hop1=hop0*childScale)
        //      predicts hop0~=81.0wu (with the LOD-coef override) and hop1~=0.0791wu -- but
        //      hop1, as a camera-to-body-center WORLD distance, is far SMALLER than the
        //      body's own solid surface radius (~27wu at R=4.8). A body-center-orbit
        //      schedule literally cannot place the camera at hop1's distance without
        //      embedding it INSIDE T0's own solid volume (a degenerate ray-origin-inside-
        //      geometry case, independently confirmed live: a first schedule attempt with
        //      near=0.02wu produced T0/magenta fill at every near/mid tick and NEVER any
        //      green/cyan T1/T2 pixels anywhere in a 500-tick run). This holds for ANY
        //      raySizeCoef override that also keeps hop0 outside the solid (hop1=hop0*cs is
        //      always 1024x smaller than hop0), so it is NOT a schedule-tuning problem --
        //      it is the SAME 1024x-hop-gap structural fact M6/M7 found in FOV-angle terms,
        //      re-appearing as a raw-distance constraint that look-target retargeting does
        //      NOT solve (retargeting fixes WHERE the camera looks, not the fact that
        //      reaching hop1's threshold via a body-center orbit requires standing inside
        //      the body's own solid).
        //
        // CORRECTED SCHEDULE: sweeps ONLY from just outside the solid (near=30.0wu,
        // comfortably clear of the ~27wu solid radius) out through hop0 (T0->T1, ~81.0wu) to
        // the orbit ceiling (120.0wu) over kM8Phase1End=400 ticks -- this demonstrates the
        // ONE crossing (hop0) that IS genuinely reachable-and-outside-solid at true 2^-10,
        // with look-target retargeting fixing hop0's own off-axis angle (43deg at the near
        // end without retargeting, dropping under the 22.5deg half-FOV on its own by
        // ~tick 200 anyway -- retargeting keeps the octant framed through the WHOLE
        // near/mid range instead of only after the angle self-resolves). Predicted:
        //   hop0 tick ~= 287
        // hop1 (T1->T2) is NOT demonstrated by this schedule -- see the Progress Log for why
        // it is structurally unreachable via ANY body-center-orbit schedule at true 2^-10, a
        // genuine finding surfaced honestly rather than hidden or silently re-scoped.
        if (renderGraph && std::getenv("VIXEN_TIER_M8_EARTH_DEMO") &&
            (std::getenv("VIXEN_TIER_M8_EARTH_ZOOM_DEMO") || std::getenv("VIXEN_TIER_M8_EARTH_ZOOM_SCRIPT"))) {
            static long m8ZoomTick = 0;
            ++m8ZoomTick;
            // SECOND correction (found after the near=30wu attempt rendered near-total
            // mip-fallback grey with no T0/T1 attribution at all): near=30wu is too close
            // for the look-target retarget to keep the BODY itself in frame. At that
            // distance the body's own angular half-radius (atan-style, ~64deg) vastly
            // exceeds the 22.5deg half-FOV, so with `forward` aimed 43deg off the
            // body-center direction (toward the octant), the ray marcher's actual view cone
            // misses the body's silhouette entirely -- background/mip-placeholder, not a
            // crossing miss. A python sweep of (body's own angular half-radius, octant's
            // angular offset from the body-center direction) vs. orbit distance shows both
            // only drop under the 22.5deg half-FOV simultaneously around distance ~=70wu
            // (body half-angle ~=22.7deg, octant offset ~=16.3deg) -- i.e. the usable
            // "both body AND octant in frame while retargeted" band is much narrower than
            // assumed, and sits just BELOW hop0's own predicted distance (81wu). CORRECTED
            // AGAIN: near=70.0wu (just below hop0, where both fit), far=120.0wu (ceiling),
            // over a shorter kM8Phase1End=200-tick window -- predicts hop0 fires at
            // tick ~=54.
            constexpr long  kM8ResidencyFlipTick     = 10;   // well before hop0's own predicted tick (~54)
            constexpr long  kM8LookTargetReleaseTick = 150;  // well after hop0 (~54), comfortably stable
            constexpr long  kM8Phase1End             = 200;
            constexpr double kM8NearDist             = 70.0;   // where body+octant both fit in frame while retargeted
            constexpr double kM8FarDist              = 120.0;  // full orbit ceiling

            if (auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
                const double t = std::min(1.0, static_cast<double>(m8ZoomTick) / static_cast<double>(kM8Phase1End));
                const double logNear = std::log10(kM8NearDist);
                const double logFar  = std::log10(kM8FarDist);
                const double dist    = std::pow(10.0, logNear + t * (logFar - logNear));
                camera->SetOrbitDistanceForTest(static_cast<float>(dist));

                if (m8ZoomTick < kM8LookTargetReleaseTick) {
                    camera->SetLookTargetForTest(m8EarthHop0OctantWorld_);
                } else if (m8ZoomTick == kM8LookTargetReleaseTick) {
                    camera->ClearLookTargetForTest();
                    if (mainLogger) {
                        mainLogger->Info("[TierM8EarthZoomDemo] tick " + std::to_string(m8ZoomTick) +
                                          ": ClearLookTargetForTest() -- releasing to default body-center aim");
                    }
                }
            }
            if (m8ZoomTick == kM8ResidencyFlipTick && std::getenv("VIXEN_TIER_M8_EARTH_ZOOM_DEMO")) {
                if (auto* bodyScene = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
                        renderGraph->GetInstance(bodyOctreeSceneNode_))) {
                    bodyScene->RequestBrickResidency(true);
                    if (mainLogger) {
                        mainLogger->Info("[TierM8EarthZoomDemo] tick " + std::to_string(m8ZoomTick) +
                                          ": RequestBrickResidency(true) -- mid-flight residency grant");
                    }
                }
            }
            if (m8ZoomTick % 20 == 0 && mainLogger) {
                mainLogger->Info("[TierM8EarthZoomDemo] tick " + std::to_string(m8ZoomTick));
            }
        }

        // Tiered-ESVO Inc3 M8 Task 19 (primary demo, user-directed 2026-07-11): a genuinely
        // TRANSLATING flight path toward the true Earth-scale (childScale=2^-10/hop) crossing,
        // as opposed to Task 17's orbit-around-body-center + look-target-retarget schedule
        // (kept alive above as the SECONDARY "telescope" demo — camera stationary in its orbit
        // radius, only its aim sweeps). Task 17 found hop1 (T1->T2, ~0.079wu) structurally
        // unreachable while orbiting body center at ANY distance outside the ~27wu solid
        // radius, because hop1 is a camera-TO-BODY-CENTER distance threshold and the crossing
        // octant itself sits only ~20.8wu from center (deep inside that radius) — no orbit
        // radius can be simultaneously "outside the solid" (>27) and "at hop1's tiny distance"
        // (~0.079) from CENTER. The user's redirect: approach the crossing DIRECTLY (translate
        // camera position toward it) rather than orbiting center and re-aiming.
        //
        // THE GEOMETRIC FIX (derivation: Tiered-ESVO-Inc3-M8-Task19-flight-path-derivation.py):
        // measure distance-to-target from the CROSSING OCTANT'S OWN SPHERE-SURFACE INTERSECTION
        // POINT, not the octant's cell-center bookkeeping position and not body center. The
        // marked octant (camera-facing, direction (-1,-1,+1)/sqrt(3) from body center per
        // BuildRenderGraph.cpp's octantOffsetWorld) is a COARSE root-level leaf cell whose own
        // geometry (the baked sphere's SDF, radius 6.0*R=28.8wu) is exposed at the sphere's true
        // outer surface along that same direction — i.e. surfacePoint = bodyCenter + dir*R_sphere.
        // A camera flying along `dir`, aimed at surfacePoint, has "distance-to-crossing" shrink
        // from far (orbit view) down to a small margin just outside the sphere shell — the SAME
        // sense of "outside the solid" Task 13/17 required, but now measured relative to the
        // ACTUAL rendered surface point the crossing leaf occupies, not body center. This keeps
        // the camera outside the sphere (dist-from-center > R_sphere) at every tick while still
        // reaching hop1's ~0.079wu threshold, because hop1 is measured along the ray from the
        // camera to the point it's looking at (the crossing patch itself), not to body center.
        //
        // PREDICTED (derivation script, R_sphere=28.8, dir=(-1,-1,+1)/sqrt3, near=0.05wu past
        // the shell, far=91.2wu, log-spaced over kM8FlightPhaseEnd=400 ticks):
        //   hop1 (T1->T2) tick ~= 24.4     hop0 (T0->T1) tick ~= 393.7
        // Both inside [0,400], well-separated, and (unlike Task 17) BOTH hops are geometrically
        // reachable by this schedule — the flight path is the fix for Task 17's hop1 wall, not
        // just a different-shaped camera move.
        if (renderGraph && std::getenv("VIXEN_TIER_M8_EARTH_DEMO") && std::getenv("VIXEN_TIER_M8_FLIGHT_DEMO")) {
            static long m8FlightTick = 0;
            ++m8FlightTick;
            constexpr long   kM8FlightResidencyFlipTick = 10;    // well before both Task 23 handoffs
            constexpr long   kM8FlightPhaseEnd          = 400;
            constexpr double kM8SphereRadiusWorld       = 28.8;  // 6.0 (baked recipe radius) * renderScale 4.8
            // M8 Task 23: near end deepened 0.05 -> 2e-4, and the flight anchor is now the
            // recorded CHILD ANCHOR world position (BuildRenderGraph records the scene's own
            // entry-anchored childOriginLocal mapped to world, superseding the M7-era
            // octant-center assumption that pointed ~20wu away from the actual child
            // content). With the corrected camera-anchored crossing gate at the REAL
            // raySizeCoef (no override), hop1 (T1->T2) fires only below ~7.3e-3wu
            // camera-to-child, and T2's own content (childScale^2 of the body) needs a
            // ~2e-4wu approach to subtend more than a few pixels. The zoom-OUT schedule
            // shape is unchanged. (Near-clip narrowed to 1e-4 by BuildRenderGraph's
            // flight-demo block to match.)
            constexpr double kM8FlightNearDist          = 2e-4;
            constexpr double kM8FlightFarDist           = 91.2;  // far orbit-equivalent view (120wu radial - 28.8wu shell)

            if (auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
                const glm::vec3 bodyCenterWorld(64.0f, 64.0f, 64.0f);
                // The crossing octant's own recorded world position (m8EarthHop0OctantWorld_,
                // set once at scene-build time) gives the RADIAL DIRECTION from body center to
                // the camera-facing octant — a scale-invariant geometric fact of
                // RootLeafOctantCenterLocal's own convention (M7 Task 13), unaffected by which
                // hop's childScale is in play. The true sphere-surface point along that same
                // direction (not the octant's own coarse cell-center) is what the camera should
                // fly toward and aim at — see the derivation comment above.
                // M8 Task 19 own live-gate finding: extending the octant's radial direction OUT
                // to the sphere's nominal radius (the original approach) put the "surface point"
                // in genuinely empty/unresolved space -- own control test (VIXEN_TIER_M8_FLIGHT_DEBUG_ZDIR,
                // flying/aiming straight down +Z, Task 17's own known-good axis) rendered the real
                // T0 body correctly at every tick, isolating the bug to the octant-direction
                // extrapolation, not the SetPositionForTest/SetLookTargetNoOrbitForTest mechanism
                // itself (which is now proven correct). FIX: fly toward the octant's own RECORDED
                // world position (m8EarthHop0OctantWorld_) directly -- the exact point Task 17
                // already proved renders real geometry when aimed at from a +Z-anchored camera --
                // rather than an extrapolated point past it. The camera approaches ALONG the same
                // line from body center through the octant, but the near/far endpoints are placed
                // relative to the octant's own position, not a separately-derived sphere radius.
                const bool debugZDir = std::getenv("VIXEN_TIER_M8_FLIGHT_DEBUG_ZDIR") != nullptr;
                const bool debugZPosOctantAim = std::getenv("VIXEN_TIER_M8_FLIGHT_DEBUG_ZPOS_OCTANT_AIM") != nullptr;
                // M8 Task 23: optional rebuild-free aim calibration -- the child's VISIBLE
                // surface window sits a small, construction-specific offset inside the
                // marked leaf relative to the recorded child anchor (the anchor itself
                // straddles the leaf's corner boundary planes; only the in-leaf side is
                // reachable through the crossing). VIXEN_TIER_M8_FLIGHT_AIM_OFFSET="dx,dy,dz"
                // (world units) shifts the flight axis/aim point so the live session can
                // center the window without a rebuild per attempt.
                glm::vec3 aimOffset(0.0f);
                if (const char* aimOffsetEnv = std::getenv("VIXEN_TIER_M8_FLIGHT_AIM_OFFSET")) {
                    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
                    if (std::sscanf(aimOffsetEnv, "%f,%f,%f", &dx, &dy, &dz) == 3) {
                        aimOffset = glm::vec3(dx, dy, dz);
                    }
                }
                const glm::vec3 flightAnchor = m8EarthHop0OctantWorld_ + aimOffset;
                glm::vec3 dir;
                if (debugZDir || debugZPosOctantAim) {
                    dir = glm::vec3(0.0f, 0.0f, 1.0f);  // known-good POSITION direction, Task 17's static demo
                } else {
                    const glm::vec3 toOctant = flightAnchor - bodyCenterWorld;
                    const float toOctantLen = glm::length(toOctant);
                    dir = (toOctantLen > 1e-4f) ? (toOctant / toOctantLen) : glm::vec3(0.0f, 0.0f, 1.0f);
                }
                const glm::vec3 surfacePoint = debugZDir
                    ? (bodyCenterWorld + dir * static_cast<float>(kM8SphereRadiusWorld))
                    : flightAnchor;  // debugZPosOctantAim ALSO aims here (position +Z, aim at anchor)

                // The radial distance the flight anchors against: the sphere's nominal radius
                // for the +Z control path, but the octant's OWN recorded radial distance from
                // center for the real flight path (own live-gate finding above -- flying past
                // the octant to an extrapolated sphere-radius point lands in unresolved space;
                // anchoring at the octant's own position, which Task 17 already proved is real,
                // resolvable geometry, is the fix).
                const double anchorRadialDist = (debugZDir || debugZPosOctantAim)
                    ? kM8SphereRadiusWorld
                    : static_cast<double>(glm::length(flightAnchor - bodyCenterWorld));

                const double t = std::min(1.0, static_cast<double>(m8FlightTick) / static_cast<double>(kM8FlightPhaseEnd));
                const double logNear = std::log10(kM8FlightNearDist);
                const double logFar  = std::log10(kM8FlightFarDist);
                const double distToSurface = std::pow(10.0, logNear + t * (logFar - logNear));
                const double radialDistFromCenter = anchorRadialDist + distToSurface;

                const glm::vec3 flightPos = bodyCenterWorld + dir * static_cast<float>(radialDistFromCenter);
                camera->SetPositionForTest(flightPos);
                camera->SetLookTargetNoOrbitForTest(surfacePoint);

                if (m8FlightTick % 20 == 0 && mainLogger) {
                    mainLogger->Info("[TierM8FlightDemoDebug] tick " + std::to_string(m8FlightTick) +
                                      " distToSurface=" + std::to_string(distToSurface) +
                                      " flightPos=(" + std::to_string(flightPos.x) + "," + std::to_string(flightPos.y) + "," + std::to_string(flightPos.z) + ")" +
                                      " surfacePoint=(" + std::to_string(surfacePoint.x) + "," + std::to_string(surfacePoint.y) + "," + std::to_string(surfacePoint.z) + ")" +
                                      " dir=(" + std::to_string(dir.x) + "," + std::to_string(dir.y) + "," + std::to_string(dir.z) + ")");
                }
            }
            if (m8FlightTick == kM8FlightResidencyFlipTick) {
                if (auto* bodyScene = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
                        renderGraph->GetInstance(bodyOctreeSceneNode_))) {
                    bodyScene->RequestBrickResidency(true);
                    if (mainLogger) {
                        mainLogger->Info("[TierM8FlightDemo] tick " + std::to_string(m8FlightTick) +
                                          ": RequestBrickResidency(true) -- mid-flight residency grant");
                    }
                }
            }
            if (m8FlightTick % 20 == 0 && mainLogger) {
                mainLogger->Info("[TierM8FlightDemo] tick " + std::to_string(m8FlightTick));
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

    // Sampled Lighting Inc4 M5 live-gate instrumentation (VIXEN_DUMP_SYNC_EDGES=1): one-shot
    // dump of the actual baked FrameSyncSchedule edges touching probe_update, direct_lighting,
    // and spatial_reuse_shade — the plan's own instruction to inspect the scheduler's REAL
    // output (not infer it from "it renders correctly"), mirroring Inc3 M5's own fan-in-demo
    // edge-topology verification rigor. Reports, per named node, which OTHER named node (if
    // any) it has a direct SyncEdge to/from, so a barrier between probe_update and either
    // direct_lighting or spatial_reuse_shade is a concrete, grep-able finding rather than an
    // assumption.
    if (std::getenv("VIXEN_DUMP_SYNC_EDGES")) {
        const Vixen::RenderGraph::FrameSyncSchedule& sched = renderGraph->GetFrameSyncSchedule();
        std::vector<std::string> watchNames = {"probe_update", "direct_lighting", "spatial_reuse"};
        std::unordered_map<uint32_t, std::string> groupIdToName;
        for (const auto& group : sched.groups) {
            if (!group.node) continue;
            const std::string& name = group.node->GetInstanceName();
            for (const auto& watch : watchNames) {
                if (name == watch) { groupIdToName[group.groupId] = name; break; }
            }
        }
        mainLogger->Info("[SyncEdgeDump] " + std::to_string(sched.edges.size()) + " total baked edges; "
                         + std::to_string(groupIdToName.size()) + "/" + std::to_string(watchNames.size())
                         + " watched nodes found in schedule");
        bool foundAny = false;
        for (const auto& edge : sched.edges) {
            auto fromIt = groupIdToName.find(edge.fromGroup);
            auto toIt = groupIdToName.find(edge.toGroup);
            if (fromIt == groupIdToName.end() && toIt == groupIdToName.end()) continue;
            std::string fromName = fromIt != groupIdToName.end() ? fromIt->second : ("group" + std::to_string(edge.fromGroup));
            std::string toName = toIt != groupIdToName.end() ? toIt->second : ("group" + std::to_string(edge.toGroup));
            mainLogger->Info("[SyncEdgeDump] EDGE " + fromName + " -> " + toName);
            if (fromIt != groupIdToName.end() && toIt != groupIdToName.end()) foundAny = true;
        }
        mainLogger->Info(std::string("[SyncEdgeDump] direct edge between watched nodes: ") + (foundAny ? "YES" : "NO"));
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
