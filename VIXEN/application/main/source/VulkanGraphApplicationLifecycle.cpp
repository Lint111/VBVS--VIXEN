#include "VulkanGraphApplication.h"
#include "VulkanGlobalNames.h"
#include "graph/UpscalerModules.h"
#include "Core/FrameSyncSchedule.h"
#include "ShaderLogger.h"
#include "ComputePipelineCacher.h"
#include "Core/LoopManager.h"
#include "Core/NodeRegistration.h"
#include "FrameCapture.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/StorageBufferNode.h"
#include "Nodes/PhotonCellParamsConfigNode.h"
#include "Nodes/PhotonCellTableNode.h"
#include "RunDataPaths.h"

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

VulkanGraphApplication::VulkanGraphApplication()
    : VulkanApplicationBase(),
      currentFrame(0),
      graphCompiled(false),
      width(500),
      height(500),
      hudView_(Vixen::App::MakeHudView()),
      buildingInspectorView_(Vixen::App::MakeBuildingInspectorView()) {

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

    // The generic renderer host transports its one upscaler setting through this existing
    // process-configuration seam; provider eligibility remains owned by the render graph.
    if (const char* env = std::getenv("VIXEN_UPSCALER")) {
        const std::string_view requested(env);
        if (requested == "none" || requested == "native") {
            renderPathConfig_.upscaler = UpscalerKind::None;
        } else if (requested == "linear") {
            renderPathConfig_.upscaler = UpscalerKind::Linear;
        } else if (requested == "fsr1") {
            renderPathConfig_.upscaler = UpscalerKind::Fsr1;
        } else if (requested == "fsr3") {
            renderPathConfig_.upscaler = UpscalerKind::Fsr3;
        } else if (mainLogger) {
            mainLogger->Warning("Unknown VIXEN_UPSCALER='" + std::string(requested) +
                                "'; using the native render path");
        }
    }

    // The anti-aliasing request rides the same seam; the graph still decides eligibility (a
    // canonical capture never admits a provider).
    if (const char* env = std::getenv("VIXEN_ANTI_ALIASING")) {
        const std::string_view requested(env);
        if (requested == "none" || requested == "off") {
            renderPathConfig_.antiAliasing = "none";
        } else if (requested == "taa") {
            renderPathConfig_.antiAliasing = "taa";
        } else if (mainLogger) {
            mainLogger->Warning("Unknown VIXEN_ANTI_ALIASING='" + std::string(requested) +
                                "'; keeping the requested provider");
        }
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
    Vixen::App::DestroyBuildingInspectorView(buildingInspectorView_);  // complete type in the bridge TU
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

    CreateEngineContext();
    mainLogger->Debug("VulkanGraphApplication::Initialize() - COMPLETE");
}

void VulkanGraphApplication::CreateEngineContext() {
    // AR#7: the context owns registry/bus/graph + the autonomous CalibrationStore and wires
    // MainCacher. The same setup is used when a provider context failure requires a fresh graph.
    mainLogger->Debug("Creating EngineContext (registry + bus + graph + calibration)");
    Vixen::RenderGraph::EngineConfig engineCfg;
    engineCfg.logger = mainLogger.get();
    engineCfg.calibrationDir = Vixen::App::CalibrationDir(Vixen::App::RunDataDir());
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
    messageBus = &engine_->Bus();
    renderGraph = &engine_->Graph();
    // Graph recompiles drain the GPU on this thread; keep the window's message pump alive meanwhile.
    renderGraph->SetIdleWaitPump([this] {
        if (presentationTarget_ == PresentationTarget::Window) glfwPollEvents();
    });
    mainLogger->Info("EngineContext created (MainCacher initialized, autonomous CalibrationStore active)");

    messageBus->Subscribe(
        Vixen::EventBus::WindowCloseEvent::TYPE,
        [this](const Vixen::EventBus::BaseEventMessage&) -> bool {
            HandleShutdownRequest();
            return true;
        });
    messageBus->Subscribe(
        Vixen::EventBus::ShutdownAckEvent::TYPE,
        [this](const Vixen::EventBus::BaseEventMessage& msg) -> bool {
            try {
                const auto* ackMsg = dynamic_cast<const Vixen::EventBus::ShutdownAckEvent*>(&msg);
                if (ackMsg && shutdownRequested) HandleShutdownAck(ackMsg->systemName);
            } catch (...) {
                // Shutdown acknowledgements must not escape through the application event loop.
            }
            return true;
        });

    constexpr Vixen::KernelDispatch::LoopDomainMetadata kPhysicsDomain{
        0x5048595349435331ULL, 1, 0, Vixen::KernelDispatch::TaskLane::FrameCompute, true, true};
    physicsLoopID = renderGraph->RegisterLoop(LoopConfig{
        1.0 / 60.0, "PhysicsLoop", LoopCatchupMode::MultipleSteps, 0.25, kPhysicsDomain});
    constexpr Vixen::KernelDispatch::LoopDomainMetadata kSimulationDomain{
        0x53494D4C4F4F5031ULL, 2, 0, Vixen::KernelDispatch::TaskLane::FrameCompute, true, true};
    simLoopID = renderGraph->RegisterLoop(LoopConfig{
        1.0 / 30.0, "SimLoop", LoopCatchupMode::MultipleSteps, 0.25, kSimulationDomain});
    mainLogger->Info("RenderGraph created and recurring loops registered");
}

void VulkanGraphApplication::Prepare() {
    upscalerInitializationFailures_.clear();
    PrepareWithUpscalerFallback();
}

void VulkanGraphApplication::ResetEngineContextForRetry() {
    // BuildRenderGraph overrides re-register their recipes on every build, so the registry
    // must start empty or the retry fails with DuplicateId.
    engine_.reset();
    proceduralRecipes_.Clear();
    nodeRegistry = nullptr;
    messageBus = nullptr;
    renderGraph = nullptr;
    graphCompiled = false;
    CreateEngineContext();
}

void VulkanGraphApplication::PrepareWithUpscalerFallback() {
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[VulkanGraphApplication::Prepare] START");
    }
    isPrepared = false;
    graphCompiled = false;
    upscalerCandidatesForCompile_.clear();
    selectedUpscalerProviderId_.clear();
    selectedUpscalerHasRealContext_ = false;

    try {
        for (;;) {
            // PHASE 1: Nodes manage their own resources. Provider selection is provisional
            // until CompileRenderGraph has initialized the selected provider's real context.
            reportedUpscalerInitialization_.reset();
            if (mainLogger && mainLogger->IsEnabled()) {
                mainLogger->Info("[VulkanGraphApplication::Prepare] Calling BuildRenderGraph...");
            }
            BuildRenderGraph();
            if (mainLogger && mainLogger->IsEnabled()) {
                mainLogger->Info("[VulkanGraphApplication::Prepare] BuildRenderGraph complete");
            }

            if (mainLogger && mainLogger->IsEnabled()) {
                mainLogger->Info("[VulkanGraphApplication::Prepare] Calling CompileRenderGraph...");
            }
            CompileRenderGraph();
            if (!selectedUpscalerHasRealContext_ && !reportedUpscalerInitialization_ &&
                selectedUpscalerProviderId_ != "fsr3") {
                // FSR 1 and linear own graph-compiled pipelines rather than an external
                // SDK context; successful graph compilation is their initialization proof.
                selectedUpscalerHasRealContext_ = true;
            }
            const auto selectedCandidate = std::find_if(
                upscalerCandidatesForCompile_.begin(), upscalerCandidatesForCompile_.end(),
                [this](const auto& candidate) {
                    return candidate.id == selectedUpscalerProviderId_;
                });
            if (selectedCandidate == upscalerCandidatesForCompile_.end()) {
                throw std::logic_error("Compiled upscaler provider is absent from its candidate set");
            }
            RenderModuleProviderInitializationResult initialization{
                selectedUpscalerProviderId_, true, {}};
            if (reportedUpscalerInitialization_ && !reportedUpscalerInitialization_->initialized) {
                initialization = *reportedUpscalerInitialization_;
            } else if (!selectedUpscalerHasRealContext_) {
                initialization.initialized = false;
                initialization.message =
                    "provider compile completed without reporting an initialized context";
            }
            if (initialization.initialized) {
                selectedCandidate->eligibility.initialized = true;
                const auto initializedSelection =
                    SelectRenderModuleProvider(upscalerCandidatesForCompile_);
                if (!initializedSelection.selected ||
                    initializedSelection.selected->id != selectedUpscalerProviderId_) {
                    throw std::logic_error(
                        "Compiled upscaler provider failed its final initialized eligibility check");
                }
                if (mainLogger && mainLogger->IsEnabled()) {
                    mainLogger->Info("[VulkanGraphApplication::Prepare] CompileRenderGraph complete");
                }
                break;
            }

            if (upscalerInitializationFailures_.size() >= 3 ||
                !RecordRenderModuleProviderInitializationFailure(
                    upscalerInitializationFailures_, initialization.providerId)) {
                // No provider is left to try: report through the host-facing error state.
                lastError_ = "Prepare failed: " + initialization.message;
                if (mainLogger) {
                    mainLogger->Error("[VulkanGraphApplication::Prepare] " + lastError_);
                }
                isPrepared = false;
                return;
            }
            const auto* failedBackend = FindUpscalerBackend(initialization.providerId);
            const std::string variantCapability =
                failedBackend && !failedBackend->runtimeCapability.empty()
                    ? std::string(failedBackend->runtimeCapability)
                    : initialization.providerId;
            if (mainLogger) {
                mainLogger->Warning(
                    "[VulkanGraphApplication::Prepare] Upscaler provider '" +
                    initialization.providerId + "' failed context initialization: " +
                    initialization.message +
                    "; trying the next provider; suggest a device variant that includes '" +
                    variantCapability + "'");
            }

            // Compile has already created the graph device and may have compiled dependent
            // nodes. Destroy that EngineContext as a unit so the retry starts with fresh
            // Vulkan resources and a new graph topology.
            ResetEngineContextForRetry();
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
        // A presentable surface is optional for frame-loop witnesses and captures. A minimized,
        // disconnected, or locked Windows desktop reports a zero surface extent during the first
        // SwapChainNode compile; retry the same graph on the RenderTargetNode path so the caller
        // gets a running offscreen frame loop instead of a pre-loop failure. BuildRenderGraph's
        // explicit VIXEN_HEADLESS/capture selection normally avoids this retry; this is the last
        // recovery seam for a windowed caller that discovers the unavailable surface at compile.
        const std::string_view failure(e.what());
        if (presentationTarget_ == PresentationTarget::Window &&
            failure.find("Surface reported zero extent") != std::string_view::npos) {
            if (mainLogger) {
                mainLogger->Warning(
                    "[VulkanGraphApplication::Prepare] presentable surface unavailable; "
                    "retrying on the offscreen target (set VIXEN_HEADLESS=1 to select it before compile)");
            }
            SetPresentationTarget(PresentationTarget::Offscreen);
            ResetEngineContextForRetry();
            PrepareWithUpscalerFallback();
            return;
        }
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

    // Baked-perf-pipeline M2b (Task 2b.2): log the process-wide shader-cache telemetry right
    // after Compile() -- every ShaderBundleBuilder::Build() (cached by default through
    // ShaderManagement::DefaultShaderCacheManager()) funnels into this ONE ShaderLogger::
    // GetTelemetry() singleton's cacheHits/cacheMisses counters via ShaderBundleBuilder::Build().
    // Info level (not the cache-hit path's own Debug-level log line) so this is visible in a
    // default run without raising the whole app's log verbosity -- the evidence line the
    // cold-vs-warm-boot and poisoning-test benches grep for.
    {
        auto& shaderTelemetry = ShaderManagement::ShaderLogger::GetTelemetry();
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[CompileRenderGraph] Shader cache telemetry: hits=" +
                              std::to_string(shaderTelemetry.cacheHits.load()) + " misses=" +
                              std::to_string(shaderTelemetry.cacheMisses.load()));
            // E3-T1 probe (temporary, log-only): joins the aggregate SPIR-V control above with the
            // per-program Vulkan pipeline creation ordinals. create=0 means an in-process cache hit
            // (no VkPipeline created). vkcache is expected 0 at this revision (compute cache stub).
            const auto& pipeProbe = CashSystem::GetComputePipelineCreationProbe();
            mainLogger->Info("[BootPipelineProbe] shader_hits=" +
                              std::to_string(shaderTelemetry.cacheHits.load()) + " shader_misses=" +
                              std::to_string(shaderTelemetry.cacheMisses.load()) + " march_create=" +
                              std::to_string(pipeProbe.marchOrdinal) + " wave_create=" +
                              std::to_string(pipeProbe.waveOrdinal) + " march_vkcache=" +
                              std::to_string(pipeProbe.marchVkCacheNonNull) + " wave_vkcache=" +
                              std::to_string(pipeProbe.waveVkCacheNonNull));
        }
    }

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
    // dump of the actual baked FrameSyncSchedule edges touching the probe chain (W1a's
    // gather/wave/apply split — probe_update's successors), direct_lighting, and
    // spatial_reuse_shade — the plan's own instruction to inspect the scheduler's REAL
    // output (not infer it from "it renders correctly"), mirroring Inc3 M5's own fan-in-demo
    // edge-topology verification rigor. Reports, per named node, which OTHER named node (if
    // any) it has a direct SyncEdge to/from, so a barrier between the probe chain and either
    // direct_lighting or spatial_reuse_shade is a concrete, grep-able finding rather than an
    // assumption.
    if (std::getenv("VIXEN_DUMP_SYNC_EDGES")) {
        const Vixen::RenderGraph::FrameSyncSchedule& sched = renderGraph->GetFrameSyncSchedule();
        std::vector<std::string> watchNames = {"probe_gather", "shadow_ray_trace", "probe_apply",
                                               "direct_lighting", "spatial_reuse"};
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

    // Semantic-wiring S3 sub-slice 2 (VIXEN_SDI_HAZARD_REPORT=1): derived-vs-
    // baked hazard report — OBSERVER only, never feeds the graph. Derives
    // candidate sync edges from the synthesized stages' census (member ACCESS
    // × provider source, filled at their synthesis sites in BuildRenderGraph)
    // and classifies each against the baked FrameSyncSchedule; then the
    // reverse: baked edges between census'd stages the derivation missed.
    // This is the measured answer to "which hand sync sets are derivable"
    // (epoch doc, S3) — switching any stage to derived sync is a later
    // sub-slice gated on baked-SyncEdges identity.
    if (std::getenv("VIXEN_SDI_HAZARD_REPORT")) {
        using Vixen::RenderGraph::FrameSyncSchedule;
        using Vixen::RenderGraph::NodeHandle;
        const FrameSyncSchedule& sched = renderGraph->GetFrameSyncSchedule();
        const auto derived = Vixen::RenderGraph::DeriveHazardEdges(sdiHazardCensus_);

        std::unordered_map<const Vixen::RenderGraph::NodeInstance*, uint32_t> groupOf;
        std::unordered_map<uint32_t, std::string> groupName;
        for (const auto& g : sched.groups) {
            if (!g.node) continue;
            groupOf[g.node] = g.groupId;
            groupName[g.groupId] = g.node->GetInstanceName();
        }
        // W3c-2 diag: the schedule's group order IS the frame's execution order
        // (FrameSyncScheduler::Build copies executionOrder in). Dumping the
        // dispatch-bearing groups settles same-frame stage order questions with
        // evidence instead of topo-sort inference.
        {
            std::string orderLine = "[SdiHazard] EXEC-ORDER:";
            for (const auto& g : sched.groups) {
                if (!g.node) continue;
                const auto* type = g.node->GetNodeType();
                const std::string tn = type ? type->GetTypeName() : "";
                if (tn == "ComputeStage" || tn == "ComputeDispatch" || tn == "Blit" ||
                    tn == "MultiDispatch" || tn == "SkyProjection" || tn == "UiComposite") {
                    orderLine += " " + std::to_string(g.groupId) + ":" + g.node->GetInstanceName();
                }
            }
            mainLogger->Info(orderLine);
        }
        auto nameOf = [&](NodeHandle h) -> std::string {
            auto* inst = renderGraph->GetInstance(h);
            return inst ? inst->GetInstanceName()
                        : ("handle" + std::to_string(h.index));
        };

        // Pair -> smallest timelineOffset baked for it. Offset 0 = same-frame
        // edge; >0 = cross-frame (e.g. the WAR "next frame's writer waits this
        // frame's readers" that several hand designs bake as the REVERSE
        // direction of the naive same-frame RAW — sub-slice 2 finding (1).
        std::map<std::pair<uint32_t, uint32_t>, uint64_t> bakedPairs;
        for (const auto& e : sched.edges) {
            const auto key = std::make_pair(e.fromGroup, e.toGroup);
            auto it = bakedPairs.find(key);
            if (it == bakedPairs.end() || e.timelineOffset < it->second) {
                bakedPairs[key] = e.timelineOffset;
            }
        }
        // Canonical full baked-edge dump — the identity-gate surface: a
        // pre/post log diff of these lines IS the "baked SyncEdges identical"
        // check any derived-sync switch must pass.
        for (const auto& e : sched.edges) {
            const std::string fromName = groupName.count(e.fromGroup)
                ? groupName[e.fromGroup] : ("group" + std::to_string(e.fromGroup));
            const std::string toName = groupName.count(e.toGroup)
                ? groupName[e.toGroup] : ("group" + std::to_string(e.toGroup));
            mainLogger->Info("[SdiHazard] SCHED " + fromName + " -> " + toName +
                             " @offset " + std::to_string(e.timelineOffset));
        }

        std::set<uint32_t> censusGroups;
        for (const NodeHandle stage : sdiHazardCensus_.Stages()) {
            auto* inst = renderGraph->GetInstance(stage);
            if (inst && groupOf.count(inst)) censusGroups.insert(groupOf[inst]);
        }

        mainLogger->Info("[SdiHazard] derived=" + std::to_string(derived.size()) +
                         " candidate edges; baked total=" + std::to_string(sched.edges.size()) +
                         "; census stages=" + std::to_string(sdiHazardCensus_.Stages().size()) +
                         " (" + std::to_string(censusGroups.size()) + " scheduled); opaque members=" +
                         std::to_string(sdiHazardCensus_.OpaqueMembers().size()));

        std::set<std::pair<uint32_t, uint32_t>> derivedPairs;
        for (const auto& e : derived) {
            auto* fromInst = renderGraph->GetInstance(e.from);
            auto* toInst = renderGraph->GetInstance(e.to);
            // Timeline-aware classification (sub-slice 3): a naive same-frame
            // RAW candidate counts as covered when the scheduler baked either
            // the direct pair (any offset) or the REVERSE pair as a
            // cross-frame WAR (offset > 0).
            std::string verdict = "[NOT-baked]     ";
            if (fromInst && toInst && groupOf.count(fromInst) && groupOf.count(toInst)) {
                const auto pair = std::make_pair(groupOf[fromInst], groupOf[toInst]);
                derivedPairs.insert(pair);
                auto direct = bakedPairs.find(pair);
                if (direct != bakedPairs.end()) {
                    verdict = direct->second == 0 ? "[baked]         "
                                                  : "[baked+offset]  ";
                } else {
                    auto rev = bakedPairs.find({pair.second, pair.first});
                    if (rev != bakedPairs.end() && rev->second > 0) {
                        verdict = "[reverse-WAR]   ";
                    }
                }
            }
            mainLogger->Info("[SdiHazard] DERIVED " + verdict +
                             nameOf(e.from) + " -> " + nameOf(e.to) + " via " +
                             nameOf(e.resource.node) + ":" + std::to_string(e.resource.slot));
        }
        for (const auto& e : sched.edges) {
            if (!censusGroups.count(e.fromGroup) || !censusGroups.count(e.toGroup)) continue;
            if (derivedPairs.count({e.fromGroup, e.toGroup})) continue;
            mainLogger->Info("[SdiHazard] BAKED-not-derived " + groupName[e.fromGroup] +
                             " -> " + groupName[e.toGroup]);
        }
        for (const auto& om : sdiHazardCensus_.OpaqueMembers()) {
            mainLogger->Info("[SdiHazard] OPAQUE " + nameOf(om.stage) +
                             " member '" + om.memberName + "'");
        }
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
