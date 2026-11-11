#include "VulkanGraphApplication.h"
#include "Logger.h"
#include "FrameTrace.h"
#include <map>
#include <algorithm>   // std::min for the Tiered-ESVO Inc2 M5 scripted zoom clamp
#include <cmath>       // std::tan for the LOD ray-cone (raySizeCoef) computation
#include <cstdio>      // std::sscanf for VIXEN_TIER_M8_FLIGHT_AIM_OFFSET (M8 Task 23)
#include <chrono>
#include <cstdlib>     // std::getenv/atoi for VIXEN_WINDOW_WIDTH/HEIGHT overrides
#include <cstring>     // std::memcpy for M5's shadeM5IndirectLumaBits float reinterpretation
#include <iostream>    // E11-T1: std::cout for the [PolicyStencilTiles] teardown readback print
#include <string_view>
#include <unordered_map>  // Sampled Lighting Inc4 M5: VIXEN_DUMP_SYNC_EDGES groupId->name lookup

#define GLFW_INCLUDE_NONE   // don't pull in <GL/gl.h> (absent on headless/WSL); Vulkan-only below
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "Core/LoopManager.h"  // Phase 0.4: Loop system
// M4: graph construction + its ~37 node includes moved to source/graph/Build*Graph.cpp.
// The lifecycle code here needs only WindowNode (live window lookup after the de-own refactor)
// plus the few concrete node types its host-feed seams downcast to: BodyOctreeSceneNode
// (SetBodyInstances), UIRenderNode (GetUiRenderNode), UISelectionProviderNode
// (GetUiSelectionProviderNode). Include-order is load-bearing: BodyOctreeSceneNode.h MUST precede
// UIRenderNode.h (and any RmlUi/robin_hood header) — BodyOctreeSceneNode.h pulls in gaia.h, whose
// std::hash<> specialisations must be visible before RmlUi's bundled robin_hood.h wraps them.
#include "Nodes/WindowNode.h"
#include "Nodes/InputNode.h"
#include "Nodes/ReconstructionHistoryStateNode.h"
#include "Nodes/BodyOctreeSceneNode.h"        // M-wire: SetBodyInstances() downcast target (gaia — before UIRenderNode.h)
#include "Nodes/ComputeDispatchNode.h"         // Inc1 M4 Task 6b: GetGPUPerformanceLogger() for the perf-CSV writer
#include "Nodes/ComputeStageNode.h"            // Task 0.1: GetGPUPerformanceLogger() for direct_lighting/spatial_reuse/probe_update columns
#include "Nodes/BlitNode.h"                    // Task 0.1: GetGPUPerformanceLogger() for the render_target_blit column
#include "Nodes/CameraNode.h"                 // Sparse-Mip ESVO LOD Inc1 M4c: GetCurrentCameraData() downcast target
#include "Nodes/UIRenderNode.h"               // GetUiRenderNode() downcast target (RmlUi — after BodyOctreeSceneNode.h)
#include "Nodes/UISelectionProviderNode.h"    // GetUiSelectionProviderNode() downcast target
#include "Nodes/DeviceNode.h"                 // View Contract Inc-2 Task 5: VulkanDevice* for CaptureHudFrameToPng
#include <sstream>                            // View Contract Inc-2 Task 5: VIXEN_HUD_SCRIPT/_CAPTURE_FRAMES parsing
#include "Nodes/StorageBufferNode.h"          // Sampled Lighting Inc3 M4: reservoirRecordsA/B readback for the ReSTIR gate
#include "Nodes/HitAccumParamsConfigNode.h"   // B2 (batch-23): ring-buffered hit_accum_params_buffer, MapCurrentForWrite
#include "Nodes/PhotonCellParamsConfigNode.h" // C0/C1: node-owned photon params/generation
#include "Nodes/PhotonCellTableNode.h"         // C0/C1: node-owned photon table/readback
#include "Nodes/FrameSyncNode.h"              // B2 (batch-23): GetCurrentFrameIndex() for the params ring's PreTick write
#include "graph/UpscalerModules.h"
#include "Nodes/ReservoirConfigNode.h"        // Sampled Lighting Inc3 M4: GetLastFrameParity() for the readback's buffer selector
#include "Nodes/LightTreeBufferNode.h"        // Sampled Lighting Inc4 M6: SetLightTreeCut() downcast target for the edit-loop demo's live content flip
#include "Generated/ReservoirRecord.g.h"      // Sampled Lighting Inc3 M4: Vixen::Gpu::ReservoirRecord layout for the readback
#include "LightTree.h"                        // Sampled Lighting Inc3 M4: Vixen::SVO::LightTreeNode for the DIAG readback recomputation
#include "Nodes/ConstantNode.h"                // Inc4 M3: refreshing recipe_bucketing_view_proj_constant every frame

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
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>   // glm::distance2 (change-detection epsilon compares)

// NOTE: "VoxelRayMarch_CompressedNames.g.h" was an orphaned include — that combined
// SDI header is no longer generated (the tool now emits per-stage headers such as
// VoxelRayMarch_Compressed_ComputeNames.g.h) and no symbol from it is referenced here.

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
        // A provider fault latched by the previous frame demotes through Prepare()'s fallback path:
        // the failed provider joins the failure list and the graph is rebuilt with the next one.
        if (reportedUpscalerInitialization_ && !reportedUpscalerInitialization_->initialized) {
            const RenderModuleProviderInitializationResult fault = *reportedUpscalerInitialization_;
            if (!RecordRenderModuleProviderInitializationFailure(upscalerInitializationFailures_,
                                                              fault.providerId)) {
                lastError_ = "Render failed: upscaler provider '" + fault.providerId +
                             "' faulted and no provider is left: " + fault.message;
                mainLogger->Error("[VulkanGraphApplication::Render] " + lastError_);
                return false;
            }
            mainLogger->Info("[VulkanGraphApplication::Render] Demoting upscaler provider '" +
                             fault.providerId + "' and rebuilding the graph");
            ResetEngineContextForRetry();
            PrepareWithUpscalerFallback();
            if (!isPrepared || !graphCompiled || !renderGraph) {
                if (lastError_.empty()) lastError_ = "Render failed: upscaler demotion did not produce a graph";
                return false;
            }
        }

        // Pump the OS event queue only when this graph owns a window. Offscreen graph frames have no
        // GLFW initialization, surface, or event source.
        if (presentationTarget_ == PresentationTarget::Window) glfwPollEvents();

        // Render a complete frame via the graph (it internally handles event processing + deferred
        // recompilation, image acquisition, command recording, queue submission with semaphores, present).
#if UNDERSET_NATIVE_BENCHMARK
        if (std::getenv("VIXEN_EDITOR_LATENCY_TRACE")) {
            const auto wallUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            std::fprintf(stderr, "[RECIPE/latency] phase=render edge=begin wall_us=%lld\n",
                static_cast<long long>(wallUs));
        }
#endif
        auto* historyWriter = dynamic_cast<Vixen::RenderGraph::ComputeStageNode*>(
            renderGraph->GetInstanceByName("render_path/reconstruction_history/seed"));
        const uint64_t historySerial = historyWriter ? historyWriter->GetSubmissionSerial() : 0;
        VkResult result = renderGraph->RenderFrame();
#if UNDERSET_NATIVE_BENCHMARK
        if (std::getenv("VIXEN_EDITOR_LATENCY_TRACE")) {
            const auto wallUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            std::fprintf(stderr, "[RECIPE/latency] phase=render edge=end wall_us=%lld\n",
                static_cast<long long>(wallUs));
        }
#endif


        // A provider record fault aborted this frame inside the graph, so nothing new was presented
        // and the display keeps the last good frame. The next Render() demotes the provider.
        if (reportedUpscalerInitialization_ && !reportedUpscalerInitialization_->initialized &&
            result != VK_ERROR_DEVICE_LOST) {
            mainLogger->Warning("[VulkanGraphApplication::Render] Upscaler provider '" +
                                reportedUpscalerInitialization_->providerId + "' faulted: " +
                                reportedUpscalerInitialization_->message +
                                "; this frame is dropped and the next provider is used from the next frame");
            currentFrame++;
            return true;
        }

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

        auto* submittedHistoryWriter = dynamic_cast<Vixen::RenderGraph::ComputeStageNode*>(
            renderGraph->GetInstanceByName("render_path/reconstruction_history/seed"));
        if (submittedHistoryWriter && submittedHistoryWriter->GetSubmissionSerial() > historySerial) {
            auto* state = dynamic_cast<Vixen::RenderGraph::ReconstructionHistoryStateNode*>(
                renderGraph->GetInstanceByName("render_path/reconstruction_history/state"));
            if (state) state->CommitHistoryFrame();
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

        // Recipe-Parameterization M3 Task 8: per-frame recipeParams[0] sweep for the ReadParam
        // demo body BuildRenderGraph.cpp registered above (VIXEN_PROCEDURAL_UBER_DEMO, gated on
        // the SAME env var so this block is a true no-op when the demo isn't active). Deliberately
        // NOT an engine-wide SDFImplicitParams-style sync (explicitly out of scope, plan §0) — a
        // plain per-frame SetInstances() call with ONE mutated float is the whole mechanism, per
        // the plan's own scope note. Reads the CURRENT instance list back from the node (mutating
        // one instance's recipeParams[0] and re-submitting the WHOLE list, since SetInstances
        // replaces wholesale) rather than caching a duplicate copy at the app level — the node is
        // already the single source of truth for "what's currently staged."
        if (std::getenv("VIXEN_PROCEDURAL_UBER_DEMO")) {
            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(
                    renderGraph ? renderGraph->GetInstance(bodyOctreeSceneNode_) : nullptr)) {
                std::vector<Vixen::SVO::BodyInstanceGpu> instances = bodyScene->GetInstances();
                // The ReadParam demo body is always appended LAST by BuildRenderGraph.cpp's
                // VIXEN_PROCEDURAL_UBER_DEMO block; a plain color-tag check (gold tint) confirms
                // we're mutating the right one without needing to plumb its recipeId/index through.
                if (!instances.empty()) {
                    Vixen::SVO::BodyInstanceGpu& rp = instances.back();
                    if (rp.material.providerKind == 1u && rp.material.color[0] == 1.0f &&
                        rp.material.color[1] == 0.85f && rp.material.color[2] == 0.2f) {
                        constexpr float kSweepAmplitude = 3.0f;   // matches BuildRenderGraph.cpp's kReadParamDemoSweepMax
                        constexpr float kSweepSpeed     = 0.05f;  // radians/frame
                        rp.material.recipeParams[0] = kSweepAmplitude *
                            std::sin(static_cast<float>(hudUpdateTick_) * kSweepSpeed);
                        // SAME instance COUNT every frame -> BodyOctreeSceneNode::SetInstances never
                        // calls MarkNeedsRecompile (see its own comment: "Do NOT call MarkNeedsRecompile
                        // for a steady same-size update"). This is the live proof Task 9's dedicated
                        // gtest also pins structurally.
                        bodyScene->SetInstances(std::move(instances));
                    }
                }
            }
        }

        // Recipe-Diversity-Stress-Scene Inc6 M3: per-frame parameter updates generalized from
        // M3 Task 8's single ReadParam body (above) to ALL instantiated bodies in the diversity
        // stress demo (min(N,192) per M2). SAME mechanism, same SetInstances() re-submission
        // path -- just applied to the whole instance list instead of one instance. Gated on the
        // SAME env var BuildRenderGraph.cpp used to build this scene, so this block is a true
        // no-op when the demo isn't active (mirrors the uber-demo sweep's own gating discipline).
        //
        // Two per-frame-updated parameters, per the M3 plan's own two requirements:
        //   1. EVERY instance: recipeParams[3] (a scalar shape parameter, ReadParam idx=3 --
        //      idx 0 is reserved by the float3 declared-position read, so idx=3 doesn't alias)
        //      subtracted from the resolve segment's own SDF value (BuildRenderGraph.cpp's
        //      program suffix: ReadParam(3), MathSub) -- a uniform "shrink/grow" sweep visible
        //      on every generated shape regardless of its specific CSG structure.
        //   2. A SUBSET of instances (every 4th, index % kAnimatedPositionStride == 0): the
        //      DECLARED POSITION ITSELF (recipeParams[0..2], ReadParamFloat3 idx=0) orbits
        //      around that instance's own resting grid slot -- exercising the spatial contract's
        //      own value proposition (a genuinely moving, ReadParam-sourced position) rather than
        //      only a shape parameter, per the plan's explicit "this is the actual point of the
        //      milestone" framing. Orbit is centered on a CACHED base position (captured once,
        //      the first time this block runs after a fresh N) rather than the previous frame's
        //      value, so per-frame writes never drift/accumulate -- each frame recomputes the
        //      absolute orbited position from the fixed base + current phase.
        if (std::getenv("VIXEN_RECIPE_DIVERSITY_STRESS_DEMO")) {
            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(
                    renderGraph ? renderGraph->GetInstance(bodyOctreeSceneNode_) : nullptr)) {
                std::vector<Vixen::SVO::BodyInstanceGpu> instances = bodyScene->GetInstances();

                // Base declared positions, cached once per instance COUNT (a fresh N re-seeds
                // the cache; the same N across frames reuses it) -- captured from the FIRST
                // frame's already-authored recipeParams[0..2] (BuildRenderGraph.cpp's own
                // static-per-run grid placement), not recomputed from grid math here, so this
                // block stays correct regardless of how the grid is laid out upstream.
                static std::vector<glm::vec3> diversityBasePositions;
                if (diversityBasePositions.size() != instances.size()) {
                    diversityBasePositions.clear();
                    diversityBasePositions.reserve(instances.size());
                    for (const auto& inst : instances) {
                        diversityBasePositions.emplace_back(
                            inst.material.recipeParams[0], inst.material.recipeParams[1], inst.material.recipeParams[2]);
                    }
                }

                constexpr float kShapeParamSweepMax   = 3.0f;   // matches BuildRenderGraph.cpp's kShapeParamSweepMax
                constexpr float kShapeSweepSpeed       = 0.05f;  // radians/frame, matches the uber-demo's own cadence
                constexpr int   kAnimatedPositionStride = 4;     // matches BuildRenderGraph.cpp's kAnimatedPositionStride
                constexpr float kOrbitRadius           = 6.0f;   // world units; covered by the boundRadius margin
                constexpr float kOrbitSpeed            = 0.03f;  // radians/frame

                const float t = static_cast<float>(hudUpdateTick_);
                for (size_t idx = 0; idx < instances.size(); ++idx) {
                    Vixen::SVO::BodyInstanceGpu& inst = instances[idx];
                    if (inst.material.providerKind != 1u) continue;  // only PROVIDER_PROCEDURAL bodies carry recipeParams

                    // (1) every instance: animated shape parameter.
                    inst.material.recipeParams[3] = kShapeParamSweepMax * std::sin(t * kShapeSweepSpeed + static_cast<float>(idx));

                    // (2) every Nth instance: animated declared position, orbiting its own base
                    // grid slot in the XZ plane (Y fixed, matching the grid's own flat layout).
                    if (idx % kAnimatedPositionStride == 0) {
                        const glm::vec3& base = diversityBasePositions[idx];
                        const float phase = t * kOrbitSpeed + static_cast<float>(idx);
                        const glm::vec3 orbited(
                            base.x + kOrbitRadius * std::cos(phase),
                            base.y,
                            base.z + kOrbitRadius * std::sin(phase));
                        inst.material.recipeParams[0] = orbited.x;
                        inst.material.recipeParams[1] = orbited.y;
                        inst.material.recipeParams[2] = orbited.z;
                    }
                }

                // SAME instance COUNT every frame (only recipeParams[] contents change) ->
                // BodyOctreeSceneNode::SetInstances never calls MarkNeedsRecompile, the exact
                // invariant ReadParamValueSweepNeverMarksNodeNeedsRecompile pins structurally and
                // this milestone's own scaled-up test extends to min(N,192) instances.
                bodyScene->SetInstances(std::move(instances));
            }
        }

        // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: live orchestration, gated behind
        // VIXEN_RECIPE_BUCKETED_DISPATCH (recipeBucketedDispatchEnabled_, set once in
        // BuildRenderGraph.cpp). A COMPLETE no-op when unset -- returns before touching any of
        // the M3 node handles (all default-constructed/invalid in that case anyway).
        if (recipeBucketedDispatchEnabled_) {
            RunRecipeBucketedDispatchPreTick();
        }

        // W-SPLIT: the accumulate pass's per-frame params — ONE host-written
        // 48-byte block (epoch, PRIMARY cone, detail, camera). NEVER push
        // constants (record-time bake per ring slot — the W3b finding).
        // primaryCoef replicates RaySizeCoefNode.cpp:31 (2·tan((fovYRad/height)·0.5);
        // lastComputed_ is private — SYNC with that line), bias = 0 (pinhole).
        if (hitAccumEnabled_) {
            if (auto* cameraInst = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
                if (auto* paramsNode = static_cast<HitAccumParamsConfigNode*>(renderGraph->GetInstance(hitAccumParamsBuffer_))) {
                    // B2 (batch-23): the buffer is now ring-buffered (was a single
                    // un-ringed StorageBufferNode — batch-22 root cause). PreTick
                    // runs BEFORE this frame's graph Execute pass, so it must write
                    // into the SAME ring slot ExecuteImpl will emit this frame.
                    // B2 ring-lag fix (batch-28): FrameSyncNode::ExecuteImpl
                    // advances currentFrameIndex at ITS OWN top, before this
                    // frame's Execute pass runs — GetCurrentFrameIndex() now
                    // predicts that advance so PreTick targets the same slot
                    // Execute/the descriptor set/the dispatch will read (the
                    // un-advanced value was one ring slot behind, every frame).
                    auto* frameSyncInst = static_cast<FrameSyncNode*>(renderGraph->GetInstanceByName("frame_sync"));
                    if (void* pm = frameSyncInst ? paramsNode->MapCurrentForWrite(frameSyncInst->GetCurrentFrameIndex()) : nullptr) {
                        const CameraData& haCam = cameraInst->GetCurrentCameraData();
                        struct HitAccumParamsCpu {
                            uint32_t frameEpoch;
                            float primaryCoef, primaryBias, detailSize0;
                            glm::vec4 camPos, camForward;
                        };
                        static_assert(sizeof(HitAccumParamsCpu) == 48, "must match the shader's HitAccumParams");
                        auto* p = reinterpret_cast<HitAccumParamsCpu*>(pm);
                        p->frameEpoch = ++hitAccumFrameEpoch_;
                        // B2 epoch pinning (batch-24): PreTick for frame N assigns
                        // epoch value to frame N (frameCounter_ is still N-1 here,
                        // pre-increment happens after Tick()'s PostTick -- so the
                        // target frame's PreTick is when frameCounter_+1 ==
                        // hitAccumDiagFrame_). Capture the epoch actually stamped
                        // on that exact frame, not whatever the counter races to
                        // later.
                        if (hitAccumDiagFrame_ > 0 && frameCounter_ + 1 == hitAccumDiagFrame_) {
                            diagSampledEpoch_ = hitAccumFrameEpoch_;
                        }
                        p->primaryCoef = 2.0f * std::tan((glm::radians(45.0f) / static_cast<float>(height)) * 0.5f);
                        p->primaryBias = 0.0f;
                        p->detailSize0 = hitAccumDetailSize0_;
                        // W3c-3: camPos.w carries the temporal-absorption EMA
                        // alpha (spare pad reused — the bandwidth ruling);
                        // 1.0 = no history.
                        p->camPos = glm::vec4(haCam.cameraPos, hitAccumTemporalAlpha_);
                        // W-LEAN L1: camForward.w carries the lean switch
                        // (the last spare pad; > 0 = skip per-pixel traces
                        // for fully cell-resolved pixels).
                        p->camForward = glm::vec4(haCam.cameraDir, hitAccumLeanEnabled_ ? 1.0f : 0.0f);
                        paramsNode->UnmapCurrentForWrite();
                    }
                }
            }
        }

        // C0/C1: the node owns generation/config emission; the application
        // supplies only the frame slot, camera-derived primary cone, and frame
        // number needed by the optional witness hook.
        if (photonCellParamsBuffer_.IsValid()) {
            auto* paramsNode = static_cast<PhotonCellParamsConfigNode*>(
                renderGraph->GetInstance(photonCellParamsBuffer_));
            if (paramsNode) {
                auto* frameSyncInst = static_cast<FrameSyncNode*>(
                    renderGraph->GetInstanceByName("frame_sync"));
                if (frameSyncInst) {
                    const float primaryCoef = 2.0f * std::tan(
                        (glm::radians(45.0f) / static_cast<float>(height)) * 0.5f);
                    paramsNode->PrepareFrame(frameSyncInst->GetCurrentFrameIndex(),
                                             primaryCoef, frameCounter_ + 1);
                }
            }
        }

        // Raster-proxy B1 M4: one-frame-delayed camera feed for the occlusion cull.
        // Ordering note: runs AFTER RunRecipeBucketedDispatchPreTick's LOW-region CPU fill —
        // the GPU cull pass writes/OR-composes only its separate HIGH camera-visibility region.
        if (b1OcclusionCullEnabled_) {
            RunB1OcclusionCullPreTick();
        }
    } catch (const std::exception& e) {
        lastError_ = std::string("PreTick failed: ") + e.what();
        if (mainLogger) mainLogger->Error("[VulkanGraphApplication::PreTick] " + lastError_);
    } catch (...) {
        lastError_ = "PreTick failed: unknown (non-std) exception";
        if (mainLogger) mainLogger->Error("[VulkanGraphApplication::PreTick] " + lastError_);
    }
}

// Recipe-Live-App-Bucketed-Dispatch Inc4 M3: per-frame orchestration for the opt-in bucketed-
// dispatch path. Pure-CPU hotness decision (see file header for why: the CPU already has full
// recipeId-per-instance knowledge via BodyOctreeSceneNode::GetInstances(), so grouping/counting
// needs no GPU readback), but the bucketing pre-pass itself still runs for real every frame
// (Task 3's own wording: wire the pre-pass in, not shortcut around it) -- its OWN output (the
// per-bucket indirect-dispatch command + screen-space coverage) is what the specialized
// indirect dispatch actually consumes below, so the mechanism this milestone gives a live home
// to is the real one, not a CPU-only stand-in.
// Raster-proxy B1 M4: feed the occlusion cull's push constants. The cull tests instance
// AABBs against LAST frame's depth tiles, so it must reproject with LAST frame's camera —
// feed the stashed previous-frame viewProj/cameraPos, then stash this frame's values.
// dims.z carries the live instance count (the shader's word loop clamps on it).
void VulkanGraphApplication::RunB1OcclusionCullPreTick() {
    if (!renderGraph) return;
    auto* cameraInst = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_));
    if (!cameraInst) return;

    if (auto* vpConst = static_cast<ConstantNode*>(renderGraph->GetInstance(b1CullPrevViewProjConstant_))) {
        vpConst->SetValue<glm::mat4>(b1PrevViewProj_);
    }
    if (auto* cpConst = static_cast<ConstantNode*>(renderGraph->GetInstance(b1CullPrevCamPosConstant_))) {
        cpConst->SetValue<glm::vec4>(glm::vec4(b1PrevCamPos_, 0.0f));
    }
    uint32_t instanceCount = 0;
    if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstanceByName("body_octree_scene"))) {
        instanceCount = static_cast<uint32_t>(bodyScene->GetInstances().size());
    }
    if (auto* dimsConst = static_cast<ConstantNode*>(renderGraph->GetInstance(b1CullDimsConstant_))) {
        dimsConst->SetValue<glm::uvec4>(glm::uvec4(b1SrcWidth_, b1SrcHeight_, instanceCount, 0u));
    }

    b1PrevViewProj_ = cameraInst->GetCurrentViewProj();
    b1PrevCamPos_   = cameraInst->GetCameraPositionForTest();  // documented read-only introspection getter
}

void VulkanGraphApplication::PostTick() {
    {
        VIXEN_FRAME_TRACE_SCOPE("post", "poll-captures");
        PollPendingCaptures();
    }
    if (std::getenv("VIXEN_HDR_METER_DEBUG")) {
        RunHdrMeterDebugReadback();
    }
    // B2 determinism slice (VIXEN_HIT_ACCUM_DIAG_FRAME=<n>): sample the hit-accum
    // table at a FIXED frame instead of shutdown, so repeat boots compare the
    // exact same accumulation window. Checked ahead of the perf-CSV early-return
    // below (unconditional on VIXEN_PERF_CSV) — frameCounter_ is post-increment
    // (Tick(): ...; PostTick(); ++frameCounter_;), so it still reads N-1 here;
    // ask for n+1 to land exactly on frame n's completed state.
    // B2 epoch pinning (batch-24, REVERTED batch-25): the MAX_FRAMES_IN_FLIGHT
    // deferral scanned epoch(sampled) AFTER several epochs of shader-side slot
    // reclaim had already overwritten it (HitAccumulate.comp's ClaimAccumSlot
    // reclaims slots across epochs by design) -- fire at the sampled frame
    // itself. B2 (batch-26, gated OFF by default batch-27): HitAccumClear.comp
    // (VIXEN_HIT_ACCUM_CLEAR, off by default) is an UNCONDITIONAL every-frame
    // zero of every table slot -- it carries no epoch filter, so with it on it
    // wipes the sampled frame's own rows too (a later in-flight frame's clear
    // can beat this deferred readback). The readback below does not depend on
    // any clear: it scans the table for rows stamped with diagSampledEpoch_
    // (captured at PreTick, the single epoch the shader actually stamped for
    // the sampled frame) and ignores every other epoch -- ClaimAccumSlot's
    // lazy per-slot reclaim is the only reclaim mechanism in the default path.
    if (hitAccumEnabled_ && hitAccumDiagFrame_ > 0 && !hitAccumDiagFrameFired_ &&
        frameCounter_ + 1 >= hitAccumDiagFrame_) {
        hitAccumDiagFrameFired_ = true;
        RunHitAccumDiagReadback(hitAccumDiagFrame_);
    }
    if (auto* photonParams = static_cast<PhotonCellParamsConfigNode*>(
            renderGraph ? renderGraph->GetInstance(photonCellParamsBuffer_) : nullptr);
        photonParams && photonParams->DiagnosticDue(frameCounter_ + 1)) {
        photonParams->MarkDiagnosticFired();
        RunPhotonCellDiagReadback(photonParams->Configuration().diagnosticFrame);
    }

#if UNDERSET_NATIVE_BENCHMARK
    if (!perfCsvWriter_.IsEnabled() || !renderGraph) return;

    const auto now = std::chrono::steady_clock::now();
    const double cpuFrameTimeMs = std::chrono::duration<double, std::milli>(now - lastPostTickTime_).count();
    lastPostTickTime_ = now;

    // Task 0.1 (Baked-Content Perf Audit, top action #9): the ESVO-traverse+shade march plus
    // every other GPU-timed pass in the graph, so a frame's whole GPU cost is attributable
    // instead of only 1 of 7 submits being visible. Node names are BuildRenderGraph.cpp's own
    // (see its Phase G / Sampled Lighting Inc3-4 node construction).
    std::vector<PerfCsvWriter::PassSource> passes;
    const auto addNamedComputeStagePasses = [&](std::initializer_list<const char*> names) {
        for (const char* name : names) {
            if (auto* stage = static_cast<ComputeStageNode*>(renderGraph->GetNodeByName(name))) {
                passes.push_back({name, stage->GetGPUPerformanceLogger()});
            }
        }
    };
    if (auto* dispatch = static_cast<ComputeDispatchNode*>(renderGraph->GetNodeByName("test_dispatch"))) {
        passes.push_back({"esvo_traverse_shade", dispatch->GetGPUPerformanceLogger()});
    }
    addNamedComputeStagePasses({"direct_lighting", "spatial_reuse"});
    for (const auto& metric : upscalerPerfPasses_) {
        auto* node = static_cast<ComputeStageNode*>(renderGraph->GetInstance(metric.node));
        passes.push_back({metric.columnName, node ? node->GetGPUPerformanceLogger() : nullptr});
    }
    // Each name below is a ComputeStageNode that exists only under its own feature flag (hit
    // accumulation, photon cells, the shadow wave, reservoir reuse, HDR exposure); absent nodes
    // add no column. A new graph-named timed pass is one more entry here, not one more block.
    // Order is the CSV column order.
    addNamedComputeStagePasses({
        "hit_accum_cell_shade", "probe_gather", "hit_accum_accumulate",
        "photon_cell_deposit", "photon_cell_fold", "shadow_visibility_wave",
        "spatial_reuse_gather", "shadow_visibility_wave_reservoir", "shadow_ray_trace",
        "probe_apply", "exposure_meter", "exposure_reduce", "exposure_tonemap"});
    for (const auto& generated : generatedSystems_.Stages()) {
        if (auto* stage = static_cast<ComputeStageNode*>(renderGraph->GetInstance(generated.stage))) {
            passes.push_back({generated.entry->className, stage->GetGPUPerformanceLogger()});
        }
    }
    if (auto* blit = static_cast<BlitNode*>(renderGraph->GetNodeByName("render_target_blit"))) {
        passes.push_back({"render_target_blit", blit->GetGPUPerformanceLogger()});
    }
    if (auto* uiComposite = static_cast<UIRenderNode*>(renderGraph->GetNodeByName("ui_composite_render"))) {
        passes.push_back({"ui_composite_render", uiComposite->GetGPUPerformanceLogger()});
    }

    // Whole-frame GPU span: earliest RecordDispatchStart -> latest RecordDispatchEnd across
    // every pass collected above. Valid because all these passes' GPUPerformanceLoggers share
    // ONE VulkanDevice's GPUQueryManager (one timestamp domain, one queue) — see
    // GPUPerformanceLogger::GetLastStart/EndTicks()'s own doc comment. 0.0 when no pass has a
    // valid reading yet (e.g. the first couple of frames before queries have completed a
    // reset->write->submit->read cycle).
    double wholeFrameGpuSpanMs = 0.0;
    {
        uint64_t minStartTicks = 0, maxEndTicks = 0;
        bool haveAny = false;
        float periodNs = 0.0f;
        for (const auto& pass : passes) {
            if (!pass.logger) continue;
            const uint64_t startTicks = pass.logger->GetLastStartTicks();
            const uint64_t endTicks = pass.logger->GetLastEndTicks();
            if (startTicks == 0 && endTicks == 0) continue;  // no valid reading this frame
            if (!haveAny) {
                minStartTicks = startTicks;
                maxEndTicks = endTicks;
                periodNs = pass.logger->GetTimestampPeriodNs();
                haveAny = true;
            } else {
                minStartTicks = std::min(minStartTicks, startTicks);
                maxEndTicks = std::max(maxEndTicks, endTicks);
            }
        }
        if (haveAny && maxEndTicks > minStartTicks && periodNs > 0.0f) {
            const uint64_t deltaTicks = maxEndTicks - minStartTicks;
            wholeFrameGpuSpanMs = static_cast<double>(deltaTicks) * static_cast<double>(periodNs) / 1'000'000.0;
        }
    }

    // Frame trace: each pass's latest GPU interval as raw device ticks, once per distinct reading;
    // the reader maps them to the host clock with the calibration pairs. Measurement only.
    if (Vixen::FrameTrace::Enabled()) {
        static std::map<std::string, uint64_t> lastEmittedStart;
        for (const auto& pass : passes) {
            if (!pass.logger) continue;
            const uint64_t startTicks = pass.logger->GetLastStartTicks();
            const uint64_t endTicks = pass.logger->GetLastEndTicks();
            if ((startTicks == 0 && endTicks == 0) || endTicks < startTicks) continue;
            auto& last = lastEmittedStart[pass.columnName];
            if (last == startTicks) continue;
            last = startTicks;
            Vixen::FrameTrace::AddGpuRaw(pass.columnName, startTicks, endTicks);
        }
    }

    uint64_t bootBytes = 0, steadyBytes = 0;
    PerfCsvWriter::WholesaleMetrics wholesale;
    if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode_))) {
        bootBytes = bodyScene->BootBytesUploaded();
        steadyBytes = bodyScene->SteadyStateBytesUploaded();
        const auto& admission = bodyScene->WholesaleState();
        wholesale.desiredMask = static_cast<uint32_t>(admission.desiredRegime == Vixen::SVO::CellFootprintRegime::Surface ? 3u : 0u);
        wholesale.readyMask = admission.readyMask;
        wholesale.generation = admission.generation;
        wholesale.reusablePopulatedBytes = bodyScene->LastWholesaleReusableBytes();
        const uint32_t finePayloadMask =
            static_cast<uint32_t>(Vixen::SVO::WholesalePayload::ChannelPool) |
            static_cast<uint32_t>(Vixen::SVO::WholesalePayload::BrickLookup);
        const bool finePayloadReady = (admission.readyMask & finePayloadMask) == finePayloadMask;
        wholesale.channelPoolPopulatedBytes = finePayloadReady ? bodyScene->ChannelPoolBytes() : 0u;
        wholesale.brickLookupPopulatedBytes = finePayloadReady ? bodyScene->BrickLookupBytes() : 0u;
        wholesale.mipPoolPopulatedBytes = bodyScene->MipPoolBytes();
        wholesale.tierRefPopulatedBytes = bodyScene->TierRefBytes();
        wholesale.occupancyGridPopulatedBytes = bodyScene->OccupancyGridBytes();
        wholesale.allocatedCapacityBytes = bodyScene->AllocatedCapacityBytes();
        wholesale.populatedShaderReadableBytes = wholesale.channelPoolPopulatedBytes + wholesale.brickLookupPopulatedBytes +
            wholesale.mipPoolPopulatedBytes + wholesale.tierRefPopulatedBytes + wholesale.occupancyGridPopulatedBytes;
        wholesale.wholeBufferUploadBytes = bootBytes + steadyBytes;
        // Canonical S1 signature seed: the admitted payload tuple is stable across
        // boots because it is derived from sorted, concatenated buffer sizes and the
        // published readiness state. Later slices extend this with content hashes.
        uint64_t signature = Vixen::SVO::WholesaleResidentSignatureFNV64(admission);
        const uint64_t signatureValues[] = {
            wholesale.desiredMask, wholesale.readyMask, wholesale.generation,
            wholesale.channelPoolPopulatedBytes, wholesale.brickLookupPopulatedBytes,
            wholesale.mipPoolPopulatedBytes, wholesale.tierRefPopulatedBytes,
            wholesale.occupancyGridPopulatedBytes};
        for (uint64_t value : signatureValues) {
            for (unsigned shift = 0; shift < 64; shift += 8) {
                signature ^= (value >> shift) & 0xffu;
                signature *= 1099511628211ull;
            }
        }
        wholesale.residentSignatureFnv64 = signature;
    }

    const auto& waitLedger = renderGraph->GetLastFrameWaitLedger();
    const double gpuWaitMs = static_cast<double>(waitLedger.gpuRetireWaitNs) / 1'000'000.0;
    const double paceWaitMs = static_cast<double>(waitLedger.framePaceWaitNs) / 1'000'000.0;
    const PerfCsvWriter::WindowMetrics windowMetrics{
        gpuWaitMs,
        paceWaitMs,
        cpuFrameTimeMs > 0.0 ? gpuWaitMs / cpuFrameTimeMs : 0.0,
        waitLedger.windowCount,
        static_cast<double>(waitLedger.windowPredictedNs) / 1'000'000.0,
        static_cast<double>(waitLedger.windowActualNs) / 1'000'000.0,
        waitLedger.ErrorP95Ms(),
        waitLedger.ErrorP50Relative(),
        waitLedger.ErrorP95Relative(),
        waitLedger.ErrorP50Ms(),
        waitLedger.violationCount,
        static_cast<double>(waitLedger.violationNs) / 1'000'000.0,
        static_cast<double>(waitLedger.unusedNs) / 1'000'000.0,
        waitLedger.Utilisation()};
    const auto taken = Vixen::FrameTrace::TakeFrameCounts();
    using Vixen::FrameTrace::Counter;
    const auto counted = [&taken](Counter c) { return taken.value[static_cast<uint32_t>(c)]; };
    const PerfCsvWriter::FrameCounters frameCounters{
        counted(Counter::MappedBufferAccesses), counted(Counter::MappedBufferBytes),
        counted(Counter::DescriptorUpdates),    counted(Counter::CommandRecordings),
        counted(Counter::QueueSubmits),         waitLedger.gpuRetireWaitCount,
        waitLedger.framePaceWaitNs > 0 ? 1u : 0u};
    {
        VIXEN_FRAME_TRACE_SCOPE("post", "perf-csv-frame");
        perfCsvWriter_.RecordFrame(cpuFrameTimeMs, passes, bootBytes, steadyBytes, wholeFrameGpuSpanMs,
                                   windowMetrics, wholesale, frameCounters);
    }
    std::vector<PerfCsvWriter::WindowSample> windowSamples;
    windowSamples.reserve(waitLedger.waitSampleCount);
    for (uint32_t i = 0; i < waitLedger.waitSampleCount; ++i) {
        const auto& w = waitLedger.waits[i];
        windowSamples.push_back(PerfCsvWriter::WindowSample{0,
            w.kind == Vixen::RenderGraph::IdleWindow::Kind::GpuRetire ? "gpu-retire" : "frame-pace",
            w.commandKey, w.cold, static_cast<double>(w.rawPredictedNs) / 1'000'000.0,
            static_cast<double>(w.predictedNs) / 1'000'000.0,
            static_cast<double>(w.actualNs) / 1'000'000.0, w.violation});
    }
    perfCsvWriter_.RecordWindowSamples(windowSamples);
#endif  // UNDERSET_NATIVE_BENCHMARK
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

        // T1.0: poll the opt-in HUD .viewblob watcher at the top of the host update tick. This is
        // before ProcessEvents/RecompileDirtyNodes and before Render(), so a successful model swap
        // cannot occur while UIRenderNode is recording the current frame.
        if (renderGraph) {
            if (auto* ui = static_cast<UIRenderNode*>(renderGraph->GetInstance(uiRenderNode_))) {
                ui->PollHudHotReload();
                // R766: re-place the declared panels when the layout or the window size changed
                // (a revision/extent compare otherwise; nothing is touched on a steady frame).
                panelDock_->Update();
            }
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
                    // GATE INFRASTRUCTURE (M4, permanent): see ProbeUpdate.comp's own
                    // DDGILeakGateDebug struct for what these carry. Must stay byte-identical
                    // to that shader struct (see the static_assert below); not scaffolding to
                    // remove.
                    uint32_t diagNearProbeHitCount;
                    float    diagNearProbeAvgRadianceLuma;
                    float    diagNearProbeAvgDepth;
                    float    diagNearProbeAvgDepth2;
                    // M5 ADDITION: SpatialReuseShade.comp's own production 8-probe trilinear
                    // gather luma, atomicMax bit-cast (see ProbeUpdate.comp's DDGILeakGateDebug
                    // struct doc comment) -- CPU zeroes this each tick before readback.
                    uint32_t shadeM5IndirectLumaBits;
                    uint32_t diagShadeAnyHitCount;  // gate infrastructure (M5, permanent) -- see SpatialReuseShade.comp
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

                // Sampled Lighting Inc5 M1 live-gate lever (opt-in, VIXEN_DDGI_AMORTIZATION_
                // PROBE_LOG=1): read back BEFORE re-seeding, at the START of this Update() call --
                // this function runs in Update(), which executes BEFORE this tick's Render()
                // dispatches ProbeUpdate.comp (see this block's own header comment above), so at
                // this point in the loop the buffer still holds whatever the PREVIOUS tick's
                // dispatch (or this tick's amortization early-out) left behind. Reading here (tick
                // N observes tick N-1's dispatch result) is the correct place to distinguish "the
                // probe's own workgroup ran last tick" (diagNearProbeHitCount overwritten, != the
                // sentinel below) from "amortized away last tick" (still the sentinel from N-1's
                // own seed) -- reading AFTER the seed-reset below would only ever see the sentinel
                // THIS call just wrote, never the prior dispatch's result (a bug caught before
                // trusting this diagnostic's output).
                if (std::getenv("VIXEN_DDGI_AMORTIZATION_PROBE_LOG") && ddgiGateTick >= 2 && ddgiGateTick <= 17) {
                    void* probeLogMapped = debugBuf->MapForReadback(deviceDdgi);
                    if (probeLogMapped) {
                        const auto* rec = reinterpret_cast<const DDGILeakGateDebugHost*>(probeLogMapped);
                        bool updatedLastTick = (rec->diagNearProbeHitCount != 0xFFFFFFFFu);
                        if (mainLogger) {
                            mainLogger->Info(std::string("[DdgiAmortizationProbeLog] tick ") + std::to_string(ddgiGateTick - 1) +
                                              " nearProbeIndex=" + std::to_string(ddgiLeakGateNearProbeIndex_) +
                                              " updatedThisTick=" + (updatedLastTick ? std::string("1") : std::string("0")) +
                                              " diagNearProbeHitCount=" + std::to_string(rec->diagNearProbeHitCount));
                        }
                        debugBuf->UnmapReadback(deviceDdgi);
                    }
                }

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
                    // Reset diagNearProbeHitCount to a sentinel BEFORE this tick's dispatch runs
                    // (see the read block above this one for the full amortization-proof rationale;
                    // this write is what NEXT tick's read call will check). Only fires when the new
                    // env var is set; ordinary diagNearProbeHitCount reads (the existing
                    // kChebyshevReadTick/kAblationReadTick gate below) are unaffected -- by then the
                    // field has long since been overwritten by real dispatches either way.
                    if (std::getenv("VIXEN_DDGI_AMORTIZATION_PROBE_LOG")) {
                        rec->diagNearProbeHitCount = 0xFFFFFFFFu;  // sentinel: "not touched this tick"
                    }
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

        // Cornell demo M1 root-cause hunt (VIXEN_DDGI_CORNELL_BAKED_DEMO=1): decisive GPU
        // readback of the actual traced world-space hit positions, reusing hit_record_buffer
        // (already populated by BodyInstanceRayMarch.comp -- see HitRecord.glsl, worldPos at
        // byte offset 32) exactly like the M5 CPU-side HitRecord scan above -- no new shader
        // binding needed, since HitRecord already carries rayOrigin + rayDir*hitT for every
        // pixel, which is precisely "the actual ray-march transform result" this investigation
        // needs: if the walls are baked at the wrong world scale (renderScale/localToWorld
        // mismatch), the hit positions read back here will disagree with
        // CornellBoxSceneDefinition.h's own world-unit box bounds ([kBoxCenter +/- kBoxHalfExtent]).
        //
        // Baked-Perf-Fix-Pipeline M2d: also fires under VIXEN_DDGI_CORNELL_VIRTUAL_DEMO, same OR
        // idiom BuildRenderGraph.cpp's shared camera-preset block already uses (both demo
        // variants read hit_record_buffer identically post-KI-018 -- see this milestone's
        // compare_parity.py, which needs a [CornellDiag] instIdx map from BOTH providers to
        // quantify the virtual<->baked lighting divergence the cross_path mode reports).
        //
        // M6b Task 6b.1/6b.2: also fires under VIXEN_DDGI_CORNELL_HYBRID_DEMO and
        // VIXEN_DDGI_CORNELL_MIXED_DEMO (mixed-provider variants) -- same hit_record_buffer
        // readback mechanism applies unchanged regardless of which bodies are STORED vs
        // PROCEDURAL, so these variants get the same instIdx-map/OOB gate evidence for free.
        if (renderGraph && (std::getenv("VIXEN_DDGI_CORNELL_BAKED_DEMO") ||
                            std::getenv("VIXEN_DDGI_CORNELL_VIRTUAL_DEMO") ||
                            std::getenv("VIXEN_DDGI_CORNELL_HYBRID_DEMO") ||
                            std::getenv("VIXEN_DDGI_CORNELL_MIXED_DEMO"))) {
            static long cornellDiagTick = 0;
            ++cornellDiagTick;
            constexpr long kCornellDiagReadTick = 150;  // matches the camera-pose diagnostic's own settle tick

            if (cornellDiagTick == kCornellDiagReadTick) {
                if (auto* bodySceneDiag = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstanceByName("body_octree_scene"))) {
                    mainLogger->Info(std::string("[CornellDiag] residency at tick 150: requested=") +
                                      (bodySceneDiag->IsResidencyRequested() ? "true" : "false") +
                                      " brickPoolUploaded=" + (bodySceneDiag->IsBrickPoolUploaded() ? "true" : "false"));
                }
                auto* hrBuf = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("hit_record_buffer"));
                auto* cornellDevInst = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"));
                if (hrBuf && cornellDevInst && cornellDevInst->GetVulkanDevice()) {
                    auto* cornellDev = cornellDevInst->GetVulkanDevice();
                    vkDeviceWaitIdle(cornellDev->device);
                    void* hrMapped = hrBuf->MapForReadback(cornellDev);
                    if (hrMapped) {
                        constexpr size_t kHitRecordStride = 64;   // sizeof(HitRecord), HitRecord.glsl
                        constexpr size_t kWorldPosOffset   = 32;  // HitRecord.glsl's own layout comment
                        constexpr size_t kHitTOffset        = 28;
                        constexpr size_t kFlagsOffset       = 44;
                        constexpr size_t kPad0Offset        = 48;  // _pad0[0] = winning instIdx (M3 round 3, TraceWorld.glsl's WorldHit.instIdx)
                        const auto* bytes = reinterpret_cast<const uint8_t*>(hrMapped);
                        const size_t byteSize = static_cast<size_t>(hrBuf->GetSizeBytes());
                        const size_t recordCount = byteSize / kHitRecordStride;
                        // Screen is 500x500 (this demo's offscreen render target) -- sample the
                        // center pixel (camera forward, should look straight down -Z into the box
                        // per the already-confirmed camera pose) plus a horizontal sweep across the
                        // middle row to catch a wall even if the center pixel lands on the gap
                        // between geometry and the open +Z face.
                        constexpr int kImgSize = 500;
                        // Round 4 follow-up: the per-body AABB fix (BuildRenderGraph.cpp, tight
                        // per-body grids) made hitT vary sensibly across the middle-row sweep, but
                        // EVERY sampled pixel's worldPos still lands at Z~=44 -- well past every
                        // body's own AABB (max Z=32, ceiling). Sample corners + top/bottom-center
                        // too, and track min/max worldPos across the WHOLE buffer, to tell a real
                        // (if unexpectedly-placed) hit apart from a systematic miss/garbage read.
                        const int samplePixelsX[] = {50, 150, 250, 350, 450};
                        const int centerY = kImgSize / 2;
                        size_t hitCount = 0;
                        float minWorld[3] = {1e30f, 1e30f, 1e30f};
                        float maxWorld[3] = {-1e30f, -1e30f, -1e30f};
                        // M3 round 3 (2026-07-14): localize the out-of-bounds stray hit(s) found
                        // in rounds 1/2 (worldPos.z ~= -48, impossible for any real Cornell body
                        // -- max legitimate Z is 4, the back wall's outer face) by scanning every
                        // pixel for an OUT-OF-BOUNDS worldPos (any axis outside a generous
                        // [-20,60] box, well beyond the true scene's [~-2,~30] extent) and
                        // reporting exact pixel coordinates + hitT, not just the aggregate
                        // min/max -- pinpoints WHERE on screen the anomaly lands (which wall's
                        // region, or a lone isolated pixel) instead of just confirming it exists.
                        constexpr float kBoundsLo = -20.0f, kBoundsHi = 60.0f;
                        size_t oobCount = 0;
                        int oobFirstPx = -1, oobFirstPy = -1;
                        for (size_t i = 0; i < recordCount; ++i) {
                            uint32_t flagsField;
                            std::memcpy(&flagsField, bytes + i * kHitRecordStride + kFlagsOffset, sizeof(uint32_t));
                            if (flagsField & 0x1u) {
                                ++hitCount;
                                float wp[3];
                                std::memcpy(wp, bytes + i * kHitRecordStride + kWorldPosOffset, sizeof(wp));
                                for (int a = 0; a < 3; ++a) {
                                    minWorld[a] = std::min(minWorld[a], wp[a]);
                                    maxWorld[a] = std::max(maxWorld[a], wp[a]);
                                }
                                const bool oob = wp[0] < kBoundsLo || wp[0] > kBoundsHi ||
                                                  wp[1] < kBoundsLo || wp[1] > kBoundsHi ||
                                                  wp[2] < kBoundsLo || wp[2] > kBoundsHi;
                                if (oob) {
                                    const int px = static_cast<int>(i % static_cast<size_t>(kImgSize));
                                    const int py = static_cast<int>(i / static_cast<size_t>(kImgSize));
                                    if (oobCount == 0) { oobFirstPx = px; oobFirstPy = py; }
                                    ++oobCount;
                                    if (oobCount <= 20 && mainLogger) {
                                        float hitT;
                                        std::memcpy(&hitT, bytes + i * kHitRecordStride + kHitTOffset, sizeof(float));
                                        uint32_t winnerInstIdx;
                                        std::memcpy(&winnerInstIdx, bytes + i * kHitRecordStride + kPad0Offset, sizeof(uint32_t));
                                        float nrm[3];
                                        std::memcpy(nrm, bytes + i * kHitRecordStride + 16, sizeof(nrm));  // worldNormal @16
                                        mainLogger->Info("[CornellDiag] OOB pixel(" + std::to_string(px) + "," + std::to_string(py) +
                                                          ") hitT=" + std::to_string(hitT) +
                                                          " instIdx=" + std::to_string(winnerInstIdx) +
                                                          " worldPos=(" + std::to_string(wp[0]) + "," + std::to_string(wp[1]) + "," + std::to_string(wp[2]) + ")" +
                                                          " normal=(" + std::to_string(nrm[0]) + "," + std::to_string(nrm[1]) + "," + std::to_string(nrm[2]) + ")");
                                    }
                                }
                            }
                        }
                        if (mainLogger) {
                            mainLogger->Info("[CornellDiag] tick " + std::to_string(cornellDiagTick) +
                                              ": HitRecord scan " + std::to_string(hitCount) + "/" +
                                              std::to_string(recordCount) + " pixels hit");
                            mainLogger->Info("[CornellDiag] worldPos range across all hits: min=(" +
                                              std::to_string(minWorld[0]) + "," + std::to_string(minWorld[1]) + "," + std::to_string(minWorld[2]) +
                                              ") max=(" + std::to_string(maxWorld[0]) + "," + std::to_string(maxWorld[1]) + "," + std::to_string(maxWorld[2]) + ")");
                            mainLogger->Info("[CornellDiag] out-of-bounds hits (any axis outside [" +
                                              std::to_string(kBoundsLo) + "," + std::to_string(kBoundsHi) + "]): " +
                                              std::to_string(oobCount) + "/" + std::to_string(hitCount) +
                                              (oobCount > 0 ? (" first at pixel(" + std::to_string(oobFirstPx) + "," + std::to_string(oobFirstPy) + ")") : std::string()));
                        }
                        auto dumpPixel = [&](int px, int py) {
                            const size_t idx = static_cast<size_t>(py) * kImgSize + static_cast<size_t>(px);
                            if (idx >= recordCount) return;
                            uint32_t flagsField;
                            std::memcpy(&flagsField, bytes + idx * kHitRecordStride + kFlagsOffset, sizeof(uint32_t));
                            float worldPos[3];
                            std::memcpy(worldPos, bytes + idx * kHitRecordStride + kWorldPosOffset, sizeof(worldPos));
                            float hitT;
                            std::memcpy(&hitT, bytes + idx * kHitRecordStride + kHitTOffset, sizeof(float));
                            uint32_t winnerInstIdx;
                            std::memcpy(&winnerInstIdx, bytes + idx * kHitRecordStride + kPad0Offset, sizeof(uint32_t));
                            if (mainLogger) {
                                mainLogger->Info("[CornellDiag] pixel(" + std::to_string(px) + "," + std::to_string(py) +
                                                  ") hit=" + std::to_string(flagsField & 0x1u) +
                                                  " hitT=" + std::to_string(hitT) +
                                                  " instIdx=" + std::to_string(winnerInstIdx) +
                                                  " worldPos=(" + std::to_string(worldPos[0]) + "," +
                                                  std::to_string(worldPos[1]) + "," + std::to_string(worldPos[2]) + ")");
                            }
                        };
                        for (int px : samplePixelsX) dumpPixel(px, centerY);
                        dumpPixel(10, 10);
                        dumpPixel(490, 10);
                        dumpPixel(10, 490);
                        dumpPixel(490, 490);
                        dumpPixel(250, 10);
                        dumpPixel(250, 490);

                        // COARSE-GRID VISUAL RECONSTRUCTION (2026-07-15, combined-verify round):
                        // prior rounds relied on a handful of sample pixels + aggregate min/max,
                        // which was insufficient to catch the deterministic per-region breakage.
                        // Emit a 25x25 ASCII map (every 20th pixel over the 500x500 frame) of the
                        // WINNING instIdx per cell -- so the rendered screen->body mapping can be
                        // reconstructed and compared against where each wall SHOULD project, from
                        // the log alone, without needing the PNG. Legend: 0=leftWall 1=rightWall
                        // 2=backWall 3=floor 4=ceiling 5=light 6=sphere 7=boxObj '.'=miss
                        // '?'=out-of-range instIdx. Also dumps a coarse worldPos/hitT grid so a
                        // region's depth can be checked against its body's true world extent.
                        {
                            constexpr int kGrid = 25;
                            constexpr int kStep = kImgSize / kGrid;  // 20
                            std::string legend =
                                "[CornellDiag] instIdx map (25x25, cell=20px; 0=Lwall 1=Rwall 2=back 3=floor 4=ceil 5=light 6=sph 7=box .=miss):";
                            mainLogger->Info(legend);
                            for (int gy = 0; gy < kGrid; ++gy) {
                                std::string row;
                                const int py = gy * kStep + kStep / 2;
                                for (int gx = 0; gx < kGrid; ++gx) {
                                    const int px = gx * kStep + kStep / 2;
                                    const size_t idx = static_cast<size_t>(py) * kImgSize + static_cast<size_t>(px);
                                    if (idx >= recordCount) { row += ' '; continue; }
                                    uint32_t f;
                                    std::memcpy(&f, bytes + idx * kHitRecordStride + kFlagsOffset, sizeof(uint32_t));
                                    if ((f & 0x1u) == 0u) { row += '.'; continue; }
                                    uint32_t wi;
                                    std::memcpy(&wi, bytes + idx * kHitRecordStride + kPad0Offset, sizeof(uint32_t));
                                    row += (wi <= 7u) ? static_cast<char>('0' + wi) : '?';
                                }
                                mainLogger->Info("[CornellDiag] |" + row + "|");
                            }
                            // Coarse worldPos.z depth map (same grid) -- one char per cell bucketing
                            // the hit's world Z into a symbol, to see whether a region's depth is
                            // sane for the body it claims to be (walls at known Z, floor/ceiling
                            // spanning Z etc). '-'=z<6, '='=6..16, '#'=16..26, '+'=z>26, '.'=miss.
                            mainLogger->Info("[CornellDiag] worldPos.z depth map (-:<6 =:6-16 #:16-26 +:>26 .=miss):");
                            for (int gy = 0; gy < kGrid; ++gy) {
                                std::string row;
                                const int py = gy * kStep + kStep / 2;
                                for (int gx = 0; gx < kGrid; ++gx) {
                                    const int px = gx * kStep + kStep / 2;
                                    const size_t idx = static_cast<size_t>(py) * kImgSize + static_cast<size_t>(px);
                                    if (idx >= recordCount) { row += ' '; continue; }
                                    uint32_t f;
                                    std::memcpy(&f, bytes + idx * kHitRecordStride + kFlagsOffset, sizeof(uint32_t));
                                    if ((f & 0x1u) == 0u) { row += '.'; continue; }
                                    float wp[3];
                                    std::memcpy(wp, bytes + idx * kHitRecordStride + kWorldPosOffset, sizeof(wp));
                                    const float z = wp[2];
                                    row += (z < 6.0f) ? '-' : (z < 16.0f) ? '=' : (z < 26.0f) ? '#' : '+';
                                }
                                mainLogger->Info("[CornellDiag] |" + row + "|");
                            }
                        }

                        // SEAM-INVESTIGATION (2026-07-17): 1px-resolution instIdx window over
                        // the box/floor seam (coarse 25x25 grid disagreed with virtual at exactly
                        // one cell -- pixel(290,390) baked=floor(3) virtual=boxObj(7)). Dump the
                        // true per-pixel winner over a window centered on that seam so the real
                        // extent of the disagreement (single-pixel aliasing vs a contiguous wrong
                        // wedge) is visible from the log alone. Legend identical to the 25x25 map.
                        if (std::getenv("VIXEN_CORNELL_SEAM_WINDOW")) {
                            constexpr int wx0 = 250, wx1 = 340, wy0 = 355, wy1 = 405;
                            mainLogger->Info("[CornellDiag] SEAM window instIdx (x=" +
                                             std::to_string(wx0) + ".." + std::to_string(wx1) +
                                             " y=" + std::to_string(wy0) + ".." + std::to_string(wy1) +
                                             "; 3=floor 7=box .=miss):");
                            for (int py = wy0; py <= wy1; ++py) {
                                std::string row;
                                for (int px = wx0; px <= wx1; ++px) {
                                    const size_t idx = static_cast<size_t>(py) * kImgSize + static_cast<size_t>(px);
                                    if (idx >= recordCount) { row += ' '; continue; }
                                    uint32_t f;
                                    std::memcpy(&f, bytes + idx * kHitRecordStride + kFlagsOffset, sizeof(uint32_t));
                                    if ((f & 0x1u) == 0u) { row += '.'; continue; }
                                    uint32_t wi;
                                    std::memcpy(&wi, bytes + idx * kHitRecordStride + kPad0Offset, sizeof(uint32_t));
                                    row += (wi <= 7u) ? static_cast<char>('0' + wi) : '?';
                                }
                                mainLogger->Info("[CornellDiag] SW y=" + std::to_string(py) + " |" + row + "|");
                            }
                            // Per-pixel hitT + worldPos along the seam's key column (x=290, the
                            // disagreeing cell's center) so box-vs-floor depth ordering is visible.
                            for (int py = wy0; py <= wy1; ++py) {
                                const size_t idx = static_cast<size_t>(py) * kImgSize + 290;
                                if (idx >= recordCount) continue;
                                uint32_t f; std::memcpy(&f, bytes + idx * kHitRecordStride + kFlagsOffset, sizeof(uint32_t));
                                if ((f & 0x1u) == 0u) continue;
                                uint32_t wi; std::memcpy(&wi, bytes + idx * kHitRecordStride + kPad0Offset, sizeof(uint32_t));
                                float ht; std::memcpy(&ht, bytes + idx * kHitRecordStride + kHitTOffset, sizeof(float));
                                float wp[3]; std::memcpy(wp, bytes + idx * kHitRecordStride + kWorldPosOffset, sizeof(wp));
                                mainLogger->Info("[CornellDiag] SWcol x=290 y=" + std::to_string(py) +
                                                 " instIdx=" + std::to_string(wi) +
                                                 " hitT=" + std::to_string(ht) +
                                                 " wp=(" + std::to_string(wp[0]) + "," + std::to_string(wp[1]) + "," + std::to_string(wp[2]) + ")");
                            }
                        }
                        // NORMAL/ALBEDO ORACLE-DIFF DUMP (diagnosis-only, 2026-07-18):
                        // For each winning instIdx, aggregate the HitRecord's albedo,
                        // worldNormal, and roughness across every pixel that body won, so the
                        // baked run's per-body shading inputs can be diffed field-for-field
                        // against the virtual run's. Reports, per instance: hit count, a
                        // representative (first-hit) albedo/normal/roughness, and the MEAN
                        // per-pixel normal length -- a normalized analytic gradient has |n|~=1
                        // at every pixel, so a mean length well below 1 (or a low variance of a
                        // faceted field) is the direct numeric fingerprint of a coarse/quantized
                        // baked normal vs a crisp analytic one. Env-gated, off by default.
                        if (std::getenv("VIXEN_CORNELL_NORMAL_DUMP")) {
                            constexpr int kMaxInst = 9;  // 0..7 bodies + one ?-bucket
                            size_t nCount[kMaxInst] = {0};
                            double nLenSum[kMaxInst] = {0};
                            double nLenMin[kMaxInst]; double nLenMax[kMaxInst];
                            double albSum[kMaxInst][3] = {{0}};
                            double nrmSum[kMaxInst][3] = {{0}};
                            double rghSum[kMaxInst] = {0};
                            float firstAlb[kMaxInst][3] = {{0}}; float firstNrm[kMaxInst][3] = {{0}};
                            float firstRgh[kMaxInst] = {0};
                            for (int k = 0; k < kMaxInst; ++k) { nLenMin[k] = 1e30; nLenMax[k] = -1e30; }
                            for (size_t i = 0; i < recordCount; ++i) {
                                uint32_t f; std::memcpy(&f, bytes + i * kHitRecordStride + kFlagsOffset, sizeof(uint32_t));
                                if ((f & 0x1u) == 0u) continue;
                                uint32_t wi; std::memcpy(&wi, bytes + i * kHitRecordStride + kPad0Offset, sizeof(uint32_t));
                                const int slot = (wi < 8u) ? static_cast<int>(wi) : 8;
                                float alb[3]; std::memcpy(alb, bytes + i * kHitRecordStride + 0, sizeof(alb));   // albedo @0
                                float rgh;    std::memcpy(&rgh, bytes + i * kHitRecordStride + 12, sizeof(float)); // roughness @12
                                float nrm[3]; std::memcpy(nrm, bytes + i * kHitRecordStride + 16, sizeof(nrm));  // worldNormal @16
                                const double len = std::sqrt((double)nrm[0]*nrm[0] + (double)nrm[1]*nrm[1] + (double)nrm[2]*nrm[2]);
                                if (nCount[slot] == 0) {
                                    for (int a = 0; a < 3; ++a) { firstAlb[slot][a] = alb[a]; firstNrm[slot][a] = nrm[a]; }
                                    firstRgh[slot] = rgh;
                                }
                                for (int a = 0; a < 3; ++a) { albSum[slot][a] += alb[a]; nrmSum[slot][a] += nrm[a]; }
                                rghSum[slot] += rgh; nLenSum[slot] += len;
                                nLenMin[slot] = std::min(nLenMin[slot], len);
                                nLenMax[slot] = std::max(nLenMax[slot], len);
                                ++nCount[slot];
                            }
                            const char* names[kMaxInst] = {"Lwall","Rwall","back","floor","ceil","light","sphere","box","?"};
                            for (int k = 0; k < kMaxInst; ++k) {
                                if (nCount[k] == 0) continue;
                                const double inv = 1.0 / (double)nCount[k];
                                mainLogger->Info(std::string("[CornellDiag] NRMDUMP ") + names[k] + " n=" + std::to_string(nCount[k]) +
                                    " |n|mean=" + std::to_string(nLenSum[k]*inv) +
                                    " |n|min=" + std::to_string(nLenMin[k]) + " |n|max=" + std::to_string(nLenMax[k]) +
                                    " meanNrm=(" + std::to_string(nrmSum[k][0]*inv) + "," + std::to_string(nrmSum[k][1]*inv) + "," + std::to_string(nrmSum[k][2]*inv) + ")" +
                                    " meanAlb=(" + std::to_string(albSum[k][0]*inv) + "," + std::to_string(albSum[k][1]*inv) + "," + std::to_string(albSum[k][2]*inv) + ")" +
                                    " meanRgh=" + std::to_string(rghSum[k]*inv) +
                                    " first[alb=(" + std::to_string(firstAlb[k][0]) + "," + std::to_string(firstAlb[k][1]) + "," + std::to_string(firstAlb[k][2]) +
                                    ") nrm=(" + std::to_string(firstNrm[k][0]) + "," + std::to_string(firstNrm[k][1]) + "," + std::to_string(firstNrm[k][2]) +
                                    ") rgh=" + std::to_string(firstRgh[k]) + "]");
                            }
                        }
                        // M10 SHADOW DIAGNOSTIC decode (env-gated, off by default): when
                        // VIXEN_SHADOW_DBG_PX/_PY are set, SpatialReuseShade.comp re-ran the
                        // first light's shadow trace for that ONE pixel with capture armed and
                        // stashed the result into that pixel's HitRecord._pad0[0..2]. Decode it:
                        //   pad0[0]: bit0=occluded, bit1=NdotL>0, [2..9]=step+1, [10..21]=hop+1,
                        //            [22..]=occluderInstIdx+1 (0=none)
                        //   pad0[1]: float d at the crossing sample
                        //   pad0[2]: float grid-arc-length to crossing (from brick entry)
                        // This is the LIVE d-distribution / occluder-identity probe -- no CPU
                        // mirror, the actual value marchBrickSdfAnyHit crossed on.
                        if (std::getenv("VIXEN_SHADOW_DBG_PX") && std::getenv("VIXEN_SHADOW_DBG_PY") && mainLogger) {
                            const int tpx = std::atoi(std::getenv("VIXEN_SHADOW_DBG_PX"));
                            const int tpy = std::atoi(std::getenv("VIXEN_SHADOW_DBG_PY"));
                            const size_t ti = static_cast<size_t>(tpy) * static_cast<size_t>(kImgSize) + static_cast<size_t>(tpx);
                            if (ti < recordCount) {
                                uint32_t packed0, dBits, sBits, flagsField;
                                std::memcpy(&packed0, bytes + ti * kHitRecordStride + kPad0Offset + 0, sizeof(uint32_t));
                                std::memcpy(&dBits,   bytes + ti * kHitRecordStride + kPad0Offset + 4, sizeof(uint32_t));
                                std::memcpy(&sBits,   bytes + ti * kHitRecordStride + kPad0Offset + 8, sizeof(uint32_t));
                                std::memcpy(&flagsField, bytes + ti * kHitRecordStride + kFlagsOffset, sizeof(uint32_t));
                                float dVal, sVal; std::memcpy(&dVal, &dBits, 4); std::memcpy(&sVal, &sBits, 4);
                                float wp[3]; std::memcpy(wp, bytes + ti * kHitRecordStride + kWorldPosOffset, sizeof(wp));
                                float nrm[3]; std::memcpy(nrm, bytes + ti * kHitRecordStride + 16, sizeof(nrm));
                                const bool occluded = (packed0 & 0x1u) != 0u;
                                const bool ndotlPos = (packed0 & 0x2u) != 0u;
                                const int  step     = static_cast<int>((packed0 >> 2)  & 0xFFu) - 1;
                                const int  hop      = static_cast<int>((packed0 >> 10) & 0xFFFu) - 1;
                                const int  occInst  = static_cast<int>((packed0 >> 22) & 0x3Fu) - 1;
                                const int  leafKind = static_cast<int>((packed0 >> 28) & 0x7u) - 1;
                                const char* leafName = (leafKind==0)?"SDF-march":(leafKind==1)?"binaryDDA":(leafKind==2)?"procedural":
                                                       (leafKind==3)?"MIP-tierCross":(leafKind==4)?"MIP-brickNonResident":(leafKind==5)?"MIP-lodSubpixel":"none";
                                mainLogger->Info("[CornellDiag] SHADOWDBG pixel(" + std::to_string(tpx) + "," + std::to_string(tpy) +
                                    ") primaryHit=" + std::string((flagsField & 0x1u) ? "1" : "0") +
                                    " NdotL>0=" + std::string(ndotlPos ? "1" : "0") +
                                    " OCCLUDED=" + std::string(occluded ? "1" : "0") +
                                    " occluderInst=" + std::to_string(occInst) +
                                    " leafKind=" + std::string(leafName) +
                                    " d@cross=" + std::to_string(dVal) +
                                    " sHitGrid=" + std::to_string(sVal) +
                                    " brickHop=" + std::to_string(hop) +
                                    " step=" + std::to_string(step) +
                                    " shadePos=(" + std::to_string(wp[0]) + "," + std::to_string(wp[1]) + "," + std::to_string(wp[2]) + ")" +
                                    " normal=(" + std::to_string(nrm[0]) + "," + std::to_string(nrm[1]) + "," + std::to_string(nrm[2]) + ")");
                            } else {
                                mainLogger->Warning("[CornellDiag] SHADOWDBG target pixel out of range");
                            }
                        }
                        hrBuf->UnmapReadback(cornellDev);
                    }
                } else if (mainLogger) {
                    mainLogger->Warning("[CornellDiag] hit_record_buffer or device not found -- readback skipped");
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
        // E6-T2 measurement mode keeps the existing PolicyDisagreement rows but
        // suppresses this demo's far->near camera animation, allowing a fixed
        // VIXEN_TIER_OBSERVABLE_DISTANCE leg to be measured without changing the
        // default residency-gate sweep when VIXEN_RESIDENCY_GATE_STATIC is unset.
        if (renderGraph && std::getenv("VIXEN_RESIDENCY_GATE_DEMO") &&
            !std::getenv("VIXEN_RESIDENCY_GATE_STATIC")) {
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

        // Lazy-Procedural-Delta-Baseline Inc0 M2 Task 5 live gate: env-gated scripted
        // boot-away/fly-in schedule for the VIXEN_STORED_SDF_DEMO scene (mirrors
        // VIXEN_RESIDENCY_GATE_DEMO/VIXEN_TIER_ZOOM_DEMO's shape directly above — same
        // SetOrbitDistanceForTest/SetYawForTest live-write pattern, no InputNode injector).
        //
        // Feasibility (see the milestone's own math, kept here so a future reader doesn't
        // have to re-derive it): CameraNode::kOrbitDistanceMax=120 is a hard clamp well
        // under the residency trigger's farDist=500, so a distance-only sweep (like
        // VIXEN_RESIDENCY_GATE_DEMO's) can NEVER push this scene's bodies (~236 units out
        // at the default pose) far enough to deny — the trigger's resolvability term alone
        // never denies inside a >=614px-tall window either (0.815*screenHeightPx exceeds
        // farDist first). Denial here can ONLY come from the frustum CONTAINMENT term, so
        // this schedule boots the camera facing AWAY (yaw=pi, bodies behind the camera —
        // SphereIntersectsFrustum fails) instead of far away, then sweeps yaw back to 0 to
        // bring the body cluster into frustum and let UpdateBodySceneResidency's per-tick
        // trigger (below, unconditionally) grant residency itself — this demo scripts ONLY
        // the camera; it deliberately does NOT call RequestBrickResidency directly (unlike
        // VIXEN_TIER_ZOOM_DEMO), so the live grant is genuinely trigger-driven, the same
        // path a real flythrough exercises.
        if (renderGraph && std::getenv("VIXEN_STORED_SDF_DEMO") && std::getenv("VIXEN_BOOT_LAZY_GATE_DEMO")) {
            static long bootLazyTick = 0;
            ++bootLazyTick;
            constexpr long  kAwayHoldEnd     = 60;    // 1) hold facing away — proves boot-lazy (mip-shaded, no brick upload)
            constexpr long  kApproachEnd     = 180;   // 2) sweep yaw pi -> 0 — brings bodies into frustum, triggers the grant
            constexpr long  kGrantSettleEnd  = 240;   // 3) hold facing the bodies — lets the async upload land
            constexpr long  kEditTick        = 260;   // 4) edit-after-approach: Rematerialize while close + resident
            constexpr float kPi              = 3.14159265358979323846f;

            if (auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
                if (bootLazyTick == 1) {
                    // orbitCenter must match the Stored-SDF demo's body cluster centre
                    // (64,64,64) BEFORE the first SetYawForTest engages orbit — mirrors
                    // VIXEN_TIER_ZOOM_DEMO's own fix for the exact same stale-(5,5,5)-
                    // orbitCenter trap (see that block's comment above).
                    camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 64.0f);
                    camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 64.0f);
                    camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 64.0f);
                    // Engage orbit at yaw=pi (facing +Z, away from the bodies at -Z from the
                    // boot pose (64,64,300)) — EngageOrbit's own re-seed keeps orbitDistance
                    // at the current ~236 (matching the boot fixed pose), only yaw changes.
                    camera->SetYawForTest(kPi);
                    if (mainLogger) {
                        mainLogger->Info("[BootLazyGateDemo] tick 1: camera engaged orbit, "
                                          "yaw=pi (facing away) — expect mip-shaded boot, no brick upload");
                    }
                } else if (bootLazyTick <= kAwayHoldEnd) {
                    // Hold — no-op tick, gives the boot-frame evidence capture a stable window.
                } else if (bootLazyTick <= kApproachEnd) {
                    const float t = static_cast<float>(bootLazyTick - kAwayHoldEnd) /
                                    static_cast<float>(kApproachEnd - kAwayHoldEnd);
                    camera->SetYawForTest(kPi * (1.0f - t));  // pi -> 0
                } else if (bootLazyTick == kApproachEnd + 1) {
                    camera->SetYawForTest(0.0f);  // land exactly on-axis, facing the bodies
                    if (mainLogger) {
                        mainLogger->Info("[BootLazyGateDemo] tick " + std::to_string(bootLazyTick) +
                                          ": yaw=0 (facing bodies) — expect the frustum trigger to grant residency");
                    }
                }
                // kGrantSettleEnd..kEditTick: hold yaw=0, no further camera writes — lets
                // UpdateBodySceneResidency's per-tick trigger (unconditional, runs every
                // tick regardless of this block) observe the now-in-frustum bodies and
                // grant residency on its own, and lets the async brick upload settle.
            }

            if (bootLazyTick == kEditTick) {
                // Task 4's Rematerialize hazard, exercised LIVE: while close + brick-resident
                // (settled since kGrantSettleEnd, kEditTick-kGrantSettleEnd=20 ticks earlier),
                // trigger a genuine Rematerialize via SetBakeRecipe — an EMPTY program is a
                // no-op edit for the analytic bake path (EnsureOctreesBuilt only takes the
                // recipe branch when bakeRecipe_ is non-empty), so the geometry is byte-
                // identical before/after; only the rebuild-triggered code path runs. Per M2's
                // fix, residencyRequested_ must NOT be reset to lazy by this — bricks must
                // stay resident with NO further camera motion (assert this on the
                // real-GPU checklist: no re-fade to mip-shaded, no brick re-upload log line).
                if (auto* bodyScene = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
                        renderGraph->GetInstance(bodyOctreeSceneNode_))) {
                    bodyScene->SetBakeRecipe({});
                    if (mainLogger) {
                        mainLogger->Info("[BootLazyGateDemo] tick " + std::to_string(bootLazyTick) +
                                          ": SetBakeRecipe({}) -- no-op edit triggering Rematerialize "
                                          "while close + resident (Task 4 Rematerialize-hazard live exercise)");
                    }
                }
            }

            if (bootLazyTick % 30 == 0 && mainLogger) {
                mainLogger->Info("[BootLazyGateDemo] tick " + std::to_string(bootLazyTick));
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

        // E7-T2/E8-T1: multi-rung control fixture -- a PLATEAU variant of the
        // VIXEN_TIER_OBSERVABLE_ZOOM_DEMO block directly above. That block continuously
        // log-interpolates distance every tick; this one holds at fixed distances for
        // fixed windows so residency/mip-level counters can be attributed to a specific,
        // named hold rather than smeared across a continuously-moving camera. Same body
        // (VIXEN_TIER_OBSERVABLE_DEMO's T0/T1/T2 tree-of-trees, childScale=0.25,
        // renderScale=0.1), same predicted tier handoffs (hop1 T1->T2 ~=19.89wu, hop0
        // T0->T1 ~=79.58wu, perf/e7-t2-multirung-fixture-design.md Sec.1). Deterministic,
        // tick-indexed, no input injection -- gated on VIXEN_TIER_OBSERVABLE_PLATEAU_DEMO.
        if (renderGraph && std::getenv("VIXEN_TIER_OBSERVABLE_DEMO") &&
            std::getenv("VIXEN_TIER_OBSERVABLE_PLATEAU_DEMO")) {
            static long obsPlateauTick = 0;
            ++obsPlateauTick;
            // Design Sec.2 hold list (near-to-far-to-near omitted -- far ceiling is the
            // last hold, no return leg needed for the level-spread signature) with the
            // design's own six named distances, 60-frame holds, in ascending order so the
            // trip is monotonic (close detail -> below hop1 -> hop1 band -> mid band ->
            // hop0 band -> far ceiling):
            static constexpr double kObsPlateauDistances[] = {
                1.2,    // close detail: just outside the 0.5625wu solid surface
                10.0,   // below hop1 (~19.89wu): T2/child path visible
                19.89,  // hop1 band: T1 parent mip fallback should engage
                40.0,   // mid band: between hop1 and hop0
                79.58,  // hop0 band: T0 parent mip fallback should engage
                120.0,  // far ceiling: coarsest available levels should dominate
            };
            static constexpr const char* kObsPlateauNames[] = {
                "close_1.2wu", "below_hop1_10wu", "hop1_19.89wu",
                "mid_40wu", "hop0_79.58wu", "far_120wu",
            };
            constexpr long kObsPlateauHoldFrames = 60;
            constexpr size_t kObsPlateauHoldCount =
                sizeof(kObsPlateauDistances) / sizeof(kObsPlateauDistances[0]);

            const size_t holdIndex = static_cast<size_t>(
                std::min<long>(obsPlateauTick - 1, kObsPlateauHoldCount * kObsPlateauHoldFrames - 1) /
                kObsPlateauHoldFrames);
            const size_t clampedHoldIndex = std::min(holdIndex, kObsPlateauHoldCount - 1);

            if (auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
                camera->SetOrbitDistanceForTest(static_cast<float>(kObsPlateauDistances[clampedHoldIndex]));
            }
            if ((obsPlateauTick - 1) % kObsPlateauHoldFrames == 0 && mainLogger) {
                mainLogger->Info(std::string("[TierObservablePlateauDemo] tick ") +
                                  std::to_string(obsPlateauTick) + " hold=" +
                                  kObsPlateauNames[clampedHoldIndex] + " dist=" +
                                  std::to_string(kObsPlateauDistances[clampedHoldIndex]));
            }
        }

        // E19-T1: orbital showcase capture -- a monotonic approach from the far
        // orbital hold to near-field detail. The explicit gate keeps the shipped
        // plateau fixture byte-identical and gives the capture a stable, named
        // hold for each frame requested by VIXEN_HUD_CAPTURE_FRAMES.
        if (renderGraph && std::getenv("VIXEN_TIER_OBSERVABLE_DEMO") &&
            std::getenv("VIXEN_TIER_OBSERVABLE_SHOWCASE_DEMO")) {
            static long obsShowcaseTick = 0;
            ++obsShowcaseTick;
            static constexpr double kObsShowcaseDistances[] = {
                120.0, 100.0, 79.58, 60.0, 40.0, 19.89, 10.0, 4.0, 1.2,
            };
            static constexpr const char* kObsShowcaseNames[] = {
                "far_orbit_120wu", "approach_100wu", "silhouette_79.58wu",
                "approach_60wu", "windows_40wu", "windows_19.89wu",
                "detail_10wu", "near_4wu", "near_1.2wu",
            };
            constexpr long kObsShowcaseHoldFrames = 60;
            constexpr size_t kObsShowcaseHoldCount =
                sizeof(kObsShowcaseDistances) / sizeof(kObsShowcaseDistances[0]);
            const size_t holdIndex = static_cast<size_t>(
                std::min<long>(obsShowcaseTick - 1,
                               kObsShowcaseHoldCount * kObsShowcaseHoldFrames - 1) /
                kObsShowcaseHoldFrames);
            const size_t clampedHoldIndex = std::min(holdIndex, kObsShowcaseHoldCount - 1);
            if (auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
                camera->SetOrbitDistanceForTest(
                    static_cast<float>(kObsShowcaseDistances[clampedHoldIndex]));
            }
            if ((obsShowcaseTick - 1) % kObsShowcaseHoldFrames == 0 && mainLogger) {
                mainLogger->Info(std::string("[TierObservableShowcaseDemo] tick ") +
                                 std::to_string(obsShowcaseTick) + " hold=" +
                                 kObsShowcaseNames[clampedHoldIndex] + " dist=" +
                                 std::to_string(kObsShowcaseDistances[clampedHoldIndex]));
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
            VIXEN_FRAME_TRACE_SCOPE("update", "input");
            if (auto* input = static_cast<InputNode*>(renderGraph->GetInstance(inputNode_))) {
                input->ProcessPendingInput();
            }
        }

        // Process events + deferred recompilation here (not in render) so updates run without rendering
        // (minimized windows), at a different rate, and event-driven invalidation is handled.
        if (renderGraph) {
            {
                VIXEN_FRAME_TRACE_SCOPE("update", "process-events");
                renderGraph->ProcessEvents();
            }
            {
                VIXEN_FRAME_TRACE_SCOPE("update", "recompile-dirty");
                renderGraph->RecompileDirtyNodes();
            }
        }

        // Sparse-Mip ESVO LOD Inc1 M4c: re-evaluate the brick-residency trigger + re-sort
        // instances front-to-back against the live camera state. Runs after RecompileDirtyNodes
        // (graph must be settled) and every tick regardless of render-pause state, same as the
        // WindowNode/InputNode drains above — camera state can still change (e.g. a queued
        // resize) while rendering is paused, and this call is cheap/no-op unless the camera
        // actually moved (change-detection lives inside the method itself).
        if (renderGraph) {
            VIXEN_FRAME_TRACE_SCOPE("update", "body-residency");
            UpdateBodySceneResidency();
        }

        // View Contract Inc-2 Task 5: dump a capture PNG if this tick is scripted for one. Placed
        // at the tail of Update() — Update() ticks BEFORE the render loop's first Render() call
        // (VulkanApplicationBase::Tick(): PreTick -> Update -> Render -> PostTick), so a capture
        // here reads the selected presentation target's PREVIOUS frame result (the compute-blit +
        // UI-composite HUD draw already landed on it), same timing as the editor capture call site;
        // a capture at tick 0 would read the target before anything has been drawn (an all-black
        // PNG) — never schedule frame 0.
        for (const long captureFrame : hudCaptureFrames_) {
            if (captureFrame != hudUpdateTick_) continue;
            const std::string path = hudCaptureDir_ + "/hud_capture_" + std::to_string(hudUpdateTick_) + ".png";
            std::string captureErr;
            if (!CaptureHudFrameToPng(path, captureErr) &&
                !pendingHudCapture_ && pendingOffscreenCaptures_.empty()) {
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

void VulkanGraphApplication::SetSessionCamera(glm::vec3 eye, glm::vec3 target, glm::vec3 up,
                                              float verticalFov) {
    auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_));
    if (camera) camera->SetExternalCamera(eye, target, up, verticalFov);
    else if (mainLogger)
        mainLogger->Warning("[VulkanGraphApplication::SetSessionCamera] camera node not found");
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

void VulkanGraphApplication::SetRecipePoolProducer(
    std::function<Vixen::SVO::ConcatenatedOctrees()> producer) {
    auto* node = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
        renderGraph->GetInstance(bodyOctreeSceneNode_));
    if (node) {
        node->SetRecipePoolProducer(std::move(producer));
    } else if (mainLogger) {
        mainLogger->Warning("[VulkanGraphApplication::SetRecipePoolProducer] bodyOctreeSceneNode_ not found — producer not applied");
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

void VulkanGraphApplication::PushHudInspect(bool selected, const char* name, const char* cause,
                                            float maxGrievance, float strength,
                                            const char* topRelName, float topRelSig) {
    if (!hudView_) return;
    Vixen::App::PushHudInspect(*hudView_, selected, name, cause,
                               maxGrievance, strength, topRelName, topRelSig);
}

void VulkanGraphApplication::DeclarePanels(Vixen::RenderGraph::PanelDock& dock) {
    using Vixen::RenderGraph::DockRegion;
    using Vixen::RenderGraph::PanelDecl;
    using Vixen::RenderGraph::PanelMount;
    // The game's panels draw over the viewport (Overlay) exactly as before R766: the HUD positions its
    // own widgets inside the full viewport (hud.rcss), and the building inspector anchors bottom-left
    // inside its document. Both can be moved, docked or closed through the dock like any panel.
    PanelDecl hud;
    hud.id = "hud";
    hud.title = "HUD";
    hud.view = &Vixen::App::HudAsView(*hudView_);
    hud.document = "assets/ui/hud.rml";
    hud.region = DockRegion::Overlay;
    hud.header = false;
    hud.mount = PanelMount::Primary;
    dock.Declare(std::move(hud));

    PanelDecl building;
    building.id = "building";
    building.title = "Building";
    building.view = &Vixen::App::BuildingInspectorAsView(*buildingInspectorView_);
    building.document = "assets/ui/building_inspector.rml";
    building.region = DockRegion::Overlay;
    building.header = false;
    building.size = 264.0f;
    dock.Declare(std::move(building));
}

void VulkanGraphApplication::PushBuildingInspector(const Vixen::App::BuildingInspectorIn& in) {
    // Forwards through the gaia-free bridge (never BuildingInspectorView.h directly — this TU sees
    // gaia.h). The view dirties its own bound model, so no host round-trip is needed. Safe any time
    // after construction; a no-op visually when the app declares no "building" panel.
    if (!buildingInspectorView_) return;
    Vixen::App::PushBuildingInspector(*buildingInspectorView_, in);
}

void VulkanGraphApplication::PushHudSpeed(double speed) {
    if (!hudView_) return;
    Vixen::App::PushHudSpeed(*hudView_, speed);
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
constexpr float kResidencyBricksPerAxis = 8.0f;
constexpr float kResidencyDefaultCosmicK = 4.0f;
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

    // Cornell Box GI reference demo (baked variant): its BuildRenderGraph block calls
    // RequestBrickResidency(true) once, unconditionally, because the whole point is a
    // fully-resident brick-marched box to visually compare against the virtual variant.
    // This per-frame frustum/resolvability heuristic — tuned for the orbit-scale tiered-ESVO
    // demos, not this fixed close box — otherwise re-decides residency to FALSE every frame
    // (the walls fail its pixel-resolvability threshold at this camera distance), stomping the
    // explicit grant back off and dropping the primary visible hit onto the coarse mip-fallback
    // path (grey/flat-normal, geometrically-incoherent leaf-node hits — the "fragmented Cornell
    // box" this demo was reported broken with). Take exclusive control here, exactly like
    // VIXEN_TIER_ZOOM_DEMO above (a demo env knob owning one subsystem's schedule).
    if (std::getenv("VIXEN_DDGI_CORNELL_BAKED_DEMO")) {
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
    const float fovRadians = cam.fov * (3.14159265358979323846f / 180.0f);
    float residencyRaySizeCoef = 2.0f * std::tan((fovRadians / screenHeightPx) * 0.5f);
    if (const char* overrideEnv = std::getenv("VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE")) {
        residencyRaySizeCoef = std::strtof(overrideEnv, nullptr);
    }
    float residencyCosmicK = kResidencyDefaultCosmicK;
    if (const char* cosmicEnv = std::getenv("VIXEN_REGIME3_K")) {
        residencyCosmicK = std::strtof(cosmicEnv, nullptr);
    }

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
                Vixen::SVO::TransformPoint(inst.transform.localToWorld, glm::vec3(0.0f)),
                kResidencyBoundingRadius,
                static_cast<int>(i)});
        }
    }

    // Per-tree binary residency (§0 scope): resident if ANY current instance, individually,
    // passes frustum+resolvability (ResidencyTrigger.h) AND is NOT occluded by an
    // already brick-resident tree (OcclusionGate.h, Inc2 M3) — the whole shared brick
    // pool must be populated the moment even one instance needs it and can see it.
    bool anyInstanceWantsBricks = false;
    bool oldAnyInstanceWantsBricks = false;
    std::vector<size_t> policyDisagreements;
    for (size_t i = 0; i < instances.size(); ++i) {
        const auto& inst = instances[i];
        const glm::vec3 pos = Vixen::SVO::TransformPoint(
            inst.transform.localToWorld, glm::vec3(0.0f));
        const bool oldPolicy = Vixen::SVO::InstanceWantsBrickResidency(
                pos, kResidencyBoundingRadius,
                cam.cameraPos, cam.cameraDir, cam.cameraUp, cam.cameraRight,
                cam.fov, cam.aspect, screenHeightPx, /*nearDist=*/0.1f, /*farDist=*/500.0f,
                brickTierLevel, kResidencyLeafSizeM, kResidencyPxThreshold);
        oldAnyInstanceWantsBricks = oldAnyInstanceWantsBricks || oldPolicy;
        const float cellWorldSize = std::max(
                                        Vixen::SVO::MinimumAxisScale(inst.transform.localToWorld),
                                        1e-6f) /
                                    kResidencyBricksPerAxis;
        const bool footprintPolicy = Vixen::SVO::InstanceWantsBrickResidencyByFootprint(
            pos, kResidencyBoundingRadius,
            cam.cameraPos, cam.cameraDir, cam.cameraUp, cam.cameraRight,
            cam.fov, cam.aspect, /*nearDist=*/0.1f, /*farDist=*/500.0f,
            cellWorldSize, residencyRaySizeCoef, /*raySizeBias=*/0.0f, residencyCosmicK);
        if (oldPolicy != footprintPolicy) {
            policyDisagreements.push_back(i);
        }
        if (!footprintPolicy) {
            continue;  // fails frustum or remains above the Surface regime
        }
        const float distance = glm::distance(pos, cam.cameraPos);
        if (Vixen::SVO::IsOccludedByResidentTrees(
                cam.cameraPos, pos, distance, residentOccluders, static_cast<int>(i))) {
            continue;  // passes frustum+resolvability but a DIFFERENT already-resident tree blocks it
        }
        anyInstanceWantsBricks = true;
    }
    if (mainLogger && std::getenv("VIXEN_RESIDENCY_GATE_DEMO")) {
        std::string disagreementBodies;
        for (size_t n = 0; n < policyDisagreements.size(); ++n) {
            if (n != 0) disagreementBodies += ",";
            disagreementBodies += std::to_string(policyDisagreements[n]);
        }
        mainLogger->Info("[ResidencyGateDemo] PolicyDisagreement old=" +
                         std::string(oldAnyInstanceWantsBricks ? "true" : "false") +
                         " new=" + std::string(anyInstanceWantsBricks ? "true" : "false") +
                         " bodies=" + (disagreementBodies.empty() ? "-" : disagreementBodies));
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

void VulkanGraphApplication::NotifyReconstructionCameraCut() {
    if (!renderGraph) return;
    auto* camera = dynamic_cast<Vixen::RenderGraph::CameraNode*>(
        renderGraph->GetInstanceByName("raymarch_camera"));
    if (camera) camera->NotifyCameraCut();
}

void VulkanGraphApplication::SetReconstructionViewportActive(bool active) {
    if (!renderGraph) return;
    auto* state = dynamic_cast<Vixen::RenderGraph::ReconstructionHistoryStateNode*>(
        renderGraph->GetInstanceByName("render_path/reconstruction_history/state"));
    if (state) state->SetViewportActive(active);
}
