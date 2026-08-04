#pragma once

#include "VulkanApplicationBase.h"
#include "Core/RenderGraph.h"
#include "Core/EngineContext.h"  // AR#7: instantiable engine aggregate
#include "Core/NodeTypeRegistry.h"
#include "Core/TypedConnection.h"
#include "Core/CalibrationStore.h"  // Sprint 6.3: Persistence
#include "error/VulkanError.h"
#include "Time/EngineTime.h"
#include "MessageBus.h"
#include "graph/HudViewBridge.h"  // HudFactionIn/HudEventIn (gaia-free) + Make/Wire/PushHudView seam
#include "graph/BuildingInspectorBridge.h"  // step-6 M-ui: Make/Mount/PushBuildingInspector seam (gaia-free)
#include "PerfCsvWriter.h"  // Inc1 M4 Task 6b: always-available perf-CSV recorder (no-op unless VIXEN_PERF_CSV set)
#include "Connection/SdiHazardCensus.h"  // Semantic-wiring S3: derived-hazard observer (VIXEN_SDI_HAZARD_REPORT)
#include "ShaderCacheManager.h"  // Baked-perf-pipeline M2b: persistent disk cache for the 4 live shader builders (BuildRenderGraph.cpp)
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>  // Inc4 M3: recipeSpecializedPipelineCache_
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Forward declarations
class VulkanRenderer;
class VulkanSwapChain;
struct GLFWwindow;  // cross-platform window handle (GLFW); real include only in the .cpp
namespace Vixen::RenderGraph { class UIRenderNode; }  // composite HUD node; real include only in the .cpp
namespace Vixen::RenderGraph { class UISelectionProviderNode; }  // UI hit-test provider; real include only in the .cpp
namespace Vixen::RenderGraph { class BodyOctreeSceneNode; }  // M-wire: sparse shell octree upload node; real include in .cpp
namespace Vixen::RenderGraph { class CameraNode; }  // Sparse-Mip ESVO LOD Inc1 M4c: live camera-state readback for the residency trigger
namespace Vixen::SVO { struct BodyInstanceGpu; }  // M-wire: per-body GPU instance record (64 bytes)
namespace Vixen::SVO { struct ConcatenatedOctrees; }  // Spec B I3: boot-baked recipe pool (SetRecipePool)
#include "Recipe/RecipeRegistry.h"  // Lazy-Procedural-Delta-Baseline Inc0 M5: zero-bake uber-shader recipes.
// Real include (not forward-declared like the two lines above) -- RecipeEntry is a NESTED type of
// RecipeRegistry, and RegisterProceduralRecipe below takes one by value; a forward-declared class
// cannot name its own nested type. Safe here: RecipeRegistry.h's own include chain (SdfInstruction.h,
// RecipeStack.h, glm) never touches gaia.h/robin_hood.h, so it doesn't trip this file's documented
// HudView ODR hazard (see the comment above HudView's own forward declaration).
// View Contract Inc-2: HudView.h's real include lives ONLY in HudViewBridge.cpp (gaia-free) and
// HudView.cpp itself -- NEVER in this header or in VulkanGraphApplication.cpp/BuildRenderGraph.cpp,
// both of which transitively include BodyOctreeSceneNode.h's gaia.h. Root cause (not a style
// preference): gaia vendors its OWN, DIFFERENT-VERSION copy of RmlUi's bundled robin_hood.h under
// the SAME include guard (ROBIN_HOOD_H_INCLUDED) -- whichever copy a TU sees first silently wins
// for that whole TU, so a TU seeing gaia's copy first compiles RmlUi's inline data-model template
// code (RegisterStruct/RegisterDefinition) against the WRONG struct layout, an ODR/ABI mismatch
// against the object RmlUi's own .cpp constructed (confirmed: 64 vs 56 bytes for the identical
// robin_hood::unordered_flat_map<FamilyId,...> instantiation) -- manifesting as a null-pointer
// access violation the instant that mismatched code touches the type registry. Held by a raw
// pointer (constructed via MakeHudView(), an opaque factory in HudViewBridge.h) so this header
// needs only the forward declaration -- see hudView_ below for why it isn't std::unique_ptr.
namespace Vixen::App { class HudView; }
// Building-inspector view (step-6 M-ui) — same forward-decl/opaque-factory pattern as HudView for the
// same robin_hood ODR reason; constructed via MakeBuildingInspectorView() (BuildingInspectorBridge.h).
namespace Vixen::App { class BuildingInspectorView; struct BuildingInspectorIn; }

using namespace Vixen::Vulkan::Resources;
using namespace Vixen::RenderGraph;

// Baked-perf-pipeline M2b: builds shaderCacheManager_'s config (VIXEN_SHADER_CACHE_DIR /
// VIXEN_SHADER_CACHE_DISABLE env parsing). Free function (not inline in the class body) so the
// env-parsing logic lives out-of-line in VulkanGraphApplication.cpp, matching the constructor's
// existing VIXEN_WINDOW_WIDTH/HEIGHT parsing style -- called from shaderCacheManager_'s
// default-member-initializer, so it must be declared (not just defined) before that point.
ShaderManagement::ShaderCacheConfig MakeShaderCacheConfig();

/**
 * @brief Graph-based Vulkan application using RenderGraph architecture
 * 
 * Uses RenderGraph for declarative, node-based rendering pipeline.
 * Supports advanced features like:
 * - Automatic resource management
 * - Frame graph optimization
 * - Multi-GPU rendering
 * - Dynamic pipeline reconfiguration
 */
class VulkanGraphApplication : public VulkanApplicationBase {
public:
    // Instantiable (AR#7): the former singleton (GetInstance + once_flag) is gone — a host
    // constructs and owns the application directly. Only main.cpp ever created it, and no
    // library code reached for the global instance, so there is nothing else to re-thread.
    VulkanGraphApplication();
    ~VulkanGraphApplication() override;

    // ====== Lifecycle Methods ======
    
    void Initialize() override;
    void DeInitialize() override;
    void Prepare() override;
    void Update() override;
    bool Render() override;
    // View Contract Inc-2 Task 5: parses VIXEN_HUD_SCRIPT/VIXEN_HUD_CAPTURE_FRAMES/
    // VIXEN_HUD_CAPTURE_DIR once and injects the scripted HUD payload due this tick (mirrors
    // EditorApplication::PreTick's scripted-action injector — this app has no subclass, so the
    // harness attaches here directly). Capture itself happens at the end of Update() (below the
    // dirty_-equivalent point where the frame's render target is guaranteed populated).
    void PreTick() override;
    // Inc1 M4 Task 6b: records this tick's perf-CSV row (CPU frame time, per-pass GPU
    // dispatch ms, cumulative bytes-uploaded). Runs AFTER Render() (VulkanApplicationBase::
    // Tick(): PreTick -> Update -> Render -> PostTick), so GetLastDispatchMs() reflects the
    // frame that just completed. No-op unless VIXEN_PERF_CSV is set.
    void PostTick() override;

    // ====== Graph Management ======

    /**
     * @brief Get the render graph
     */
    inline RenderGraph* GetRenderGraph() const { return renderGraph; }

    /**
     * @brief Get the node type registry
     */
    inline NodeTypeRegistry* GetNodeTypeRegistry() const { return nodeRegistry; }

    /**
     * @brief Enable logging for a specific node (by handle)
     * @param handle Node handle
     * @param enableTerminal If true, also prints logs to console in real-time
     */
    void EnableNodeLogger(NodeHandle handle, bool enableTerminal = true);

    /**
     * @brief Enable logging for a specific node (by instance name)
     * @param nodeName Name of the node instance
     * @param enableTerminal If true, also prints logs to console in real-time
     */
    void EnableNodeLogger(const std::string& nodeName, bool enableTerminal = true);

    /**
     * @brief Build the render graph
     *
     * Override this method to construct your specific render graph.
     * Called during Prepare() phase.
     */
    virtual void BuildRenderGraph();

    /**
     * @brief Build a UI-only RmlUi demo graph (S0). Gated by the VIXEN_UI_DEMO env var.
     */
    void BuildUIGraph();

    /**
     * @brief Build an isolated instanced-cube raster demo graph (AR#31). Gated by the
     *        VIXEN_INSTANCING_DEMO env var. Renders N = gridDim^2 cubes from one mesh via
     *        a per-instance model-matrix SSBO (InstanceBufferNode) indexed by gl_InstanceIndex.
     */
    void BuildInstancingDemoGraph();

    /**
     * @brief Build an isolated auto-sync FrameGraph demo graph (AR#21 P4). Gated by the
     *        VIXEN_AUTOSYNC_DEMO env var. Proves buffer-hazard auto-synchronization:
     *        compute(fill SSBO) -> compute(in-place RAW) -> render(fullscreen frag reads
     *        SSBO) -> present, all in ONE command buffer via PassGroupNode, with the
     *        intra-pass barriers auto-baked by BuildPassGroupSchedule.
     */
    void BuildAutoSyncDemoGraph();

    /**
     * @brief Build an isolated multi-submit fan-in demo graph (AR#21 P5b M2). Gated by
     *        the VIXEN_FANIN_DEMO env var. Proves TIMELINE-ONLY ordering across SEPARATE
     *        compute submits: compute(A) + compute(B) each write a distinct storage
     *        buffer (each its OWN submit/group), a consumer compute submit waits BOTH via
     *        2 baked timeline edges (NO binary handoff between them) and writes the
     *        swapchain → present. The genuine 2-wait fan-in the 1-edge composite can't
     *        isolate. Uses the generic ComputeStageNode.
     */
    void BuildFanInDemoGraph();

    /**
     * @brief Compile the render graph
     * 
     * Validates, optimizes, and prepares the graph for execution.
     */
    void CompileRenderGraph();

protected:
    /**
     * @brief Handle shutdown request from user (X button clicked)
     */
    void HandleShutdownRequest();

    /**
     * @brief Handle shutdown acknowledgment from a system
     */
    void HandleShutdownAck(const std::string& systemName);

    /**
     * @brief Complete shutdown after all systems acknowledged
     */
    void CompleteShutdown();

    // graph.Run() consolidation: expose the two facts the base Tick() classifies on.
    bool IsShutdownRequested() const override { return shutdownRequested; }
    bool IsDeviceLostState()   const override { return renderGraph && renderGraph->IsDeviceLost(); }

    // Editor Brick-Residency Fix (2026-07): lets a subclass opt its body OUT of the
    // per-frame frustum/resolvability heuristic below (VIXEN_TIER_ZOOM_DEMO's own env-knob
    // early-return is the established precedent for "one subsystem takes deliberate, exclusive
    // control of residency"; this generalizes that to a host, not a demo flag). EditorApplication
    // overrides this to true: its single document body is unconditionally resident (see
    // ApplyDocumentToScene), and a static editor camera would otherwise never satisfy the
    // orbit-tuned heuristic, silently stomping the grant back to false every tick.
    virtual bool SkipResidencyHeuristic() const { return false; }

    /**
     * @brief Sparse-Mip ESVO LOD Inc1 M4c: re-evaluate the brick-residency trigger against
     *        the live camera state and re-sort instances front-to-back for the GPU per-ray
     *        occlusion reject.
     *
     * Called every Update() tick (cheap: a live GetInstance lookup + a few dot products,
     * no GPU work unless the gate actually flips) — mirrors SetBodyInstances'/
     * SetRecipePool's live-uncached-pointer discipline. Combines M4a's minResolvableLevel
     * + M4b's frustum containment (with hysteresis) into RequestBrickResidency's single
     * trigger, re-checked whenever camera distance, FOV, OR orientation changes materially
     * since the last check (a fixed per-frame re-evaluation would also work — the
     * change-detection here only exists to avoid needless SortInstancesFrontToBack calls
     * on a static camera, not because re-checking is expensive).
     */
    void UpdateBodySceneResidency();

private:
    // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: per-frame orchestration for the opt-in
    // bucketed-dispatch path, called from PreTick() only when recipeBucketedDispatchEnabled_
    // is true. See its own definition (VulkanGraphApplication.cpp) for the full account.
    void RunRecipeBucketedDispatchPreTick();

    // Raster-proxy B1 M4: per-frame push-constant feed for the occlusion-cull pass, called
    // from PreTick() only when b1OcclusionCullEnabled_ is true. Feeds LAST frame's
    // viewProj/cameraPos (the cull reprojects against last frame's depth) + the live
    // instance count, then stashes this frame's values for next frame.
    void RunB1OcclusionCullPreTick();

    // ====== Engine (AR#7) ======
    // EngineContext OWNS the core graph subsystems (registry, bus, graph, and the autonomous
    // CalibrationStore). The app keeps non-owning views named as before so the existing call
    // sites are unchanged; they point into engine_ and are valid for its lifetime.
    std::unique_ptr<Vixen::RenderGraph::EngineContext> engine_;
    NodeTypeRegistry* nodeRegistry = nullptr;             // view: &engine_->Registry()
    RenderGraph* renderGraph = nullptr;                   // view: &engine_->Graph()
    Vixen::EventBus::MessageBus* messageBus = nullptr;    // view: &engine_->Bus()

    // ====== Application State ======
    uint32_t currentFrame;                           // Current frame index
    Vixen::Core::EngineTime time;                    // Time management
    bool graphCompiled;                              // Graph compilation state
    int width, height;                               // Window dimensions

    // ====== Shutdown Management ======
    bool shutdownRequested = false;                  // User requested shutdown
    std::unordered_set<std::string> shutdownAcksPending;  // Systems that need to acknowledge
    bool deinitialized = false;                      // Prevent double DeInitialize

    // ====== Phase 0.4: Loop System ======
    uint32_t physicsLoopID = 0;                      // Physics loop at 60Hz
    uint32_t simLoopID = 0;                          // Logic loop for the embedded sim (fixed cadence)
    NodeHandle voxelGridNode_{};                     // dense debug-buffer node (still in graph; no longer the render source)
    NodeHandle bodyOctreeSceneNode_{};               // M-wire: sparse shell octree node (bindings 1/2/3/5/10)
    NodeHandle cameraNode_{};                        // Sparse-Mip ESVO LOD Inc1 M4c: live camera-state lookup for the residency trigger
    NodeHandle skyProjectionNode_{};                 // Tiered ESVO Inc1 M3: address-derived sky-point composite pass (stored for potential live lookup)

    // Tiered-ESVO Inc3 M8 Task 17: world positions of the two tier-crossing octants in the
    // true Earth-scale (childScale=2^-10/hop) demo (VIXEN_TIER_M8_EARTH_DEMO), set once at
    // scene-build time in BuildRenderGraph.cpp. The scripted zoom (VulkanGraphApplication.cpp)
    // reads these to retarget CameraNode::SetLookTargetForTest at the currently-active
    // crossing, since at 1024x-per-hop the two crossings cannot both be framed by a single
    // fixed look-at-body-center camera (see M6/M7's R-invariance findings) -- retargeting the
    // look direction at the crossing itself is the M8 capability this demo exercises.
    glm::vec3 m8EarthHop0OctantWorld_{0.0f};         // T0's marked leaf world center (T0->T1 crossing)
    glm::vec3 m8EarthHop1OctantWorld_{0.0f};         // T1's marked leaf world center (T1->T2 crossing)

    // Sampled Lighting Inc4 M4: DDGI leak-test gate (VIXEN_DDGI_LEAK_GATE_DEMO) scene
    // parameters set once at scene-build time in BuildRenderGraph.cpp, read every tick by
    // VulkanGraphApplication.cpp's readback hook to populate the DDGILeakGateDebug SSBO
    // (ProbeUpdate.comp binding 31) before each dispatch and to interpret the GPU-written
    // gatheredLuma result after readback.
    uint32_t  ddgiLeakGateNearProbeIndex_ = 0;   // flat probe index of the lit/near probe (irradiance source)
    uint32_t  ddgiLeakGateFarProbeIndex_  = 0;   // flat probe index of the occluded/far probe (performs the gather)
    glm::vec3 ddgiLeakGateFarShadingPos_{0.0f};  // world-space point on the far/occluded side the gather shades

    // Sampled Lighting Inc4 M6: edit-loop responsiveness gate (VIXEN_DDGI_EDIT_LOOP_DEMO) --
    // one-shot flag so the light-tree cut is flipped from empty (source "off") to the real,
    // stashed cut (g_ddgiEditLoopWorldCut) exactly once, at the configured tick.
    bool      ddgiEditLoopContentAdded_ = false;

    // Sparse-Mip ESVO LOD Inc1 M4c: last camera state the residency trigger was evaluated
    // against — change-detection only (avoids re-sorting/re-requesting every single frame
    // on a static camera); NOT part of the trigger formula itself (that's stateless, per
    // M4a/M4b). Default-constructed (all zero) so the FIRST Update() tick always evaluates
    // (a zero cameraPos/cameraDir "changed" trivially from any real camera state).
    glm::vec3 lastResidencyCheckCameraPos_{0.0f};
    glm::vec3 lastResidencyCheckCameraDir_{0.0f};
    float     lastResidencyCheckFovDegrees_ = 0.0f;
    bool      residencyTriggerEverEvaluated_ = false;

    // Sparse-Mip ESVO LOD Inc2 M3: whether the LAST residency re-check granted brick
    // residency to this BodyOctreeSceneNode's shared pool — the CPU-side occlusion
    // gate's "already brick-resident" input (Inc1 M4b's deferred spec: occlusion is
    // tested against trees resident BEFORE this frame's own re-decision, not against
    // whatever this frame is about to decide). Starts false (nothing resident pre-first-check).
    bool      lastResidencyGranted_ = false;
    // Lazy-Procedural-Delta-Baseline Inc0 M5: zero-bake procedural recipes, spliced into
    // BodyInstanceRayMarch.comp's evalRecipeField() switch by the compute-shader builder
    // lambda (BuildRenderGraph.cpp) every time it runs. Registration is independent of graph
    // compile state (v1: register-before-Compile, OR register-then-RecompileProceduralShader
    // for a live re-apply — see that method). NOT a NodeHandle-owned resource: the registry
    // itself has no GPU footprint; only its SPLICED TEXT (baked into the compute shader
    // source at build time) does. Kept as a real (not pointer) member for the same reason
    // perfCsvWriter_ above is -- always-valid, no null-check needed at any call site.
    Vixen::SVO::RecipeRegistry proceduralRecipes_;
    // Baked-perf-pipeline M2b (Task 2b.1): persistent disk cache for the 4 live shader
    // builders registered in BuildRenderGraph.cpp (BodyInstanceRayMarch/DirectLighting/
    // SpatialReuseShade/ProbeUpdate). Wired via .EnableCaching(&shaderCacheManager_) at each
    // builder site; ShaderBundleBuilder::Build() keys the cache off the FINAL effective
    // source (post-splice, post-VIXEN_GPU_TRACE_HOOKS-injection, post-#include-resolution --
    // see Build()'s cache-key comment in ShaderBundleBuilder.cpp), so a splice/define/include
    // change naturally busts the cache with no separate invalidation logic needed. Real
    // (not pointer) member -- always-valid, no null-check needed at any call site, same
    // rationale as perfCsvWriter_ above. Default-member-initialized (not ctor-init-list) so
    // construction order can't matter; cache dir is a relative "cache/shaders" (gitignored at
    // repo root via "cache/", same convention as the app's other relative "shaders/" search
    // paths). Override with VIXEN_SHADER_CACHE_DIR; VIXEN_SHADER_CACHE_DISABLE=1 disables
    // caching outright (e.g. for a clean-recompile A/B) without deleting the directory.
    ShaderManagement::ShaderCacheManager shaderCacheManager_{ MakeShaderCacheConfig() };
    NodeHandle computeShaderLibNode_{};              // stored so RecompileProceduralShader can MarkNodeNeedsRecompile

    // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: live orchestration state for the opt-in
    // VIXEN_RECIPE_BUCKETED_DISPATCH gate. Node handles are default-constructed/invalid when
    // the flag is unset (BuildRenderGraph.cpp never creates these nodes in that case) --
    // PreTick's orchestration hook (VulkanGraphApplication.cpp) checks
    // recipeBucketedDispatchEnabled_ FIRST and is a complete no-op when false, so touching an
    // invalid NodeHandle never happens on the default path.
    // Matches shaders/RecipeInstanceBucketing.comp's own push-constant caps exactly -- BOTH
    // BuildRenderGraph.cpp (SSBO sizing) and VulkanGraphApplication.cpp's PreTick (per-frame
    // orchestration/readback indexing) must agree on these, so they're declared once here
    // rather than duplicated as file-local constants in each .cpp.
    static constexpr uint32_t kRecipeBucketingMaxBuckets = 256;
    static constexpr uint32_t kRecipeBucketingMaxMembersPerBucket = 64;
    // Inc2 M3's own placeholder hotness threshold (test_recipe_multi_bucket_compositing.cpp) --
    // NOT tuned/benchmarked, explicitly a stand-in for a future real usage-history tracker (see
    // that test file's own comment) -- reused here verbatim rather than inventing a second
    // placeholder policy for the same not-yet-real decision.
    static constexpr uint32_t kRecipeBucketingHotnessThreshold = 4;

    bool recipeBucketedDispatchEnabled_ = false;
    NodeHandle recipeBucketCountBuffer_{};
    NodeHandle recipeBucketIndicesBuffer_{};
    NodeHandle recipeBucketCoverageMinXBuffer_{};
    NodeHandle recipeBucketCoverageMinYBuffer_{};
    NodeHandle recipeBucketCoverageMaxXBuffer_{};
    NodeHandle recipeBucketCoverageMaxYBuffer_{};
    NodeHandle recipeBucketIndirectCommandBuffer_{};
    // Load-Tier Contract M2 (precision tier): the precision sub-bucket pair, additive to the
    // recipe-only bucket above (bindings 9-10, RecipeInstanceBucketing.comp).
    NodeHandle recipePrecisionBucketCountBuffer_{};
    NodeHandle recipePrecisionBucketIndicesBuffer_{};
    NodeHandle recipeBoundSphereBuffer_{};
    NodeHandle recipeBucketMetaBuffer_{};
    NodeHandle recipeSpecializedDispatch_{};
    NodeHandle recipeBucketingViewProjConstant_{};  // ConstantNode PreTick refreshes every frame (see .cpp)
    NodeHandle instanceSkipMaskBuffer_{};             // Inc4 M1's placeholder buffer, populated live by PreTick when Inc4 M3's flag is set

    // Raster-proxy B1 M4 (VIXEN_B1_OCCLUSION_CULL): occlusion-probe chain state. PreTick feeds
    // the cull's push constants ONE-FRAME-DELAYED (the cull reprojects against last frame's
    // depth, so it needs last frame's camera) plus the live instance count; complete no-op when
    // the flag is unset (handles stay invalid, guarded like the bucketing path above).
    bool b1OcclusionCullEnabled_ = false;

    // Semantic-wiring S3 sub-slice 2: hazard census over the synthesized
    // stages (filled at their synthesis sites in BuildRenderGraph). OBSERVER
    // only — VIXEN_SDI_HAZARD_REPORT=1 diffs derived edges against the baked
    // FrameSyncSchedule after compile; never feeds the graph.
    Vixen::RenderGraph::SdiHazardCensus sdiHazardCensus_;
    NodeHandle b1CullPrevViewProjConstant_{};
    NodeHandle b1CullPrevCamPosConstant_{};
    NodeHandle b1CullDimsConstant_{};
    glm::mat4 b1PrevViewProj_{1.0f};
    glm::vec3 b1PrevCamPos_{0.0f};
    uint32_t b1SrcWidth_ = 0;    // depth/tile extents fixed at graph build (render-scale applied)
    uint32_t b1SrcHeight_ = 0;

    // Per-recipeId compiled specialized pipeline cache (Inc4 M3): avoids recompiling the same
    // recipe's shader every frame it stays hot. Key = recipeId; value = the compiled
    // VkPipeline/VkPipelineLayout/VkDescriptorSet triple (owned here, outliving any single
    // frame) plus the VkShaderModule/VkDescriptorPool/VkDescriptorSetLayout needed to destroy
    // it cleanly at shutdown. Populated by PreTick's orchestration hook on first promotion of
    // a given recipeId; never evicted this milestone (GPU-LRU eviction is explicitly deferred
    // to a future increment per the plan doc's own scope boundary).
    struct SpecializedRecipePipeline {
        VkShaderModule        shaderModule = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool      descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet       descriptorSet = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayout = VK_NULL_HANDLE;
        VkPipeline            pipeline = VK_NULL_HANDLE;
    };
    std::unordered_map<uint32_t, SpecializedRecipePipeline> recipeSpecializedPipelineCache_;
    // W2c (wavefront epoch, VIXEN_BUCKETED_SHADE opt-in, requires the bucketed-dispatch
    // flag): per-recipe MATERIAL (bucket-shade) kernels — the W2b identity skeleton
    // grown into the real traversal/shade split. The march kernels compile LEAN (no
    // gradient taps) and each hot recipeId gets a specialized shade pipeline
    // (EmitSpecializedRecipeShadeShader), queued on the SAME
    // recipe_specialized_dispatch node AFTER the march passes (autoBarriers_
    // serializes same-HitRecord passes — ordering inherited, zero graph changes).
    bool bucketedShadeEnabled_ = false;
    std::unordered_map<uint32_t, SpecializedRecipePipeline> bucketShadePipelineCache_;
    // W3b (wavefront epoch, VIXEN_HIT_ACCUM opt-in): the (recipeId, cell@mip)
    // accumulation pass. Capacity MUST equal HitAccumulationCommon.glsl's
    // kHitAccumTableCapacity (sync comment there; the diag readback's occupancy
    // scan catches a mismatch loudly, not silently). Entry stride = 12 words.
    static constexpr uint32_t kHitAccumTableCapacity   = 65536u;
    static constexpr uint32_t kHitAccumEntryBytes      = 48u;
    static constexpr float    kHitAccumDetailSize0     = 0.5f;  // engagement threshold (world units at mip 0)
    bool hitAccumEnabled_ = false;
    float hitAccumDetailSize0_ = kHitAccumDetailSize0;  // resolved (env-overridable) engagement threshold
    NodeHandle hitAccumCamPosConstant_{};  // set every frame from PreTick (camera cell anchor)
    NodeHandle hitAccumTableBuffer_{};     // mapped by the VIXEN_HIT_ACCUM_PROBE_LOG diag readback
    NodeHandle windowNode_{};                        // stored so GetWindowHandle() can query the WindowNode live
    NodeHandle inputNode_{};                         // stored so Update() can drain InputNode's event queue live (input-rework slice 1)
    NodeHandle uiRenderNode_{};                      // stored so GetUiRenderNode() can query the composite UI node live
    NodeHandle uiSelectionProviderNode_{};           // stored so GetUiSelectionProviderNode() can drain HUD clicks live

    // View Contract Inc-2: the app's native IView, set on the UI node via SetView (BuildRenderGraph).
    // Owned here (not by the node) — the node only holds a non-owning aliased shared_ptr, since
    // hudView_ (a VulkanGraphApplication member) already outlives the graph it is wired into.
    // Raw pointer, NOT std::unique_ptr<HudView> -- this header only forward-declares HudView (see
    // above), and std::unique_ptr's implicit destructor needs the complete type at the point it is
    // itself instantiated (this class's own destructor, defined in the gaia-touching
    // VulkanGraphApplication.cpp) -- an incomplete-type-delete compile error. Constructed via
    // MakeHudView() (ctor) and destroyed via DestroyHudView() (dtor), both HudViewBridge.h seams
    // whose bodies live in HudViewBridge.cpp, the one gaia-free TU where HudView is complete.
    Vixen::App::HudView* hudView_ = nullptr;

    // Step-6 M-ui: the building-inspector view, mounted as a SECOND document on the composite UI node
    // via IUiCompositionHost (BuildRenderGraph → MountBuildingInspector). Owned here (raw pointer, same
    // incomplete-type/ODR reason as hudView_); the mount stores a borrowed pointer. buildingMount_ is
    // the handle returned by Mount (0 if there's no composite UI node / the mount was rejected).
    Vixen::App::BuildingInspectorView* buildingInspectorView_ = nullptr;
    uint32_t buildingMount_ = 0;

    // Inc1 M4 Task 6b: perf-CSV recorder + the steady_clock timestamp of the previous
    // PostTick call (for measuring this tick's CPU frame time). No-op object when
    // VIXEN_PERF_CSV is unset (IsEnabled()==false), so construction/RecordFrame/Flush are
    // all cheap no-ops on every run that doesn't opt in.
    PerfCsvWriter perfCsvWriter_;
    std::chrono::steady_clock::time_point lastPostTickTime_ = std::chrono::steady_clock::now();

    // Task 5: scripted HUD-inject + byte-exact capture harness (mirrors EditorApplication's
    // VIXEN_EDITOR_SCRIPT/_CAPTURE_FRAMES/_CAPTURE_DIR harness — this app has no subclass, so it
    // attaches directly here). All zero-cost/inert when VIXEN_HUD_* env vars are unset.
    long hudUpdateTick_ = 0;               // local tick counter, independent of other counters in Update()
    bool hudScriptParsed_ = false;         // guards the one-time env parse in PreTick()
    std::vector<std::pair<long, char>> hudScript_;  // (frame, 'A'|'B') parsed from VIXEN_HUD_SCRIPT
    std::vector<long> hudCaptureFrames_;   // parsed from VIXEN_HUD_CAPTURE_FRAMES
    std::string hudCaptureDir_ = "temp";   // overridable via VIXEN_HUD_CAPTURE_DIR

    // Reads main_swapchain's CURRENT image back to host RGBA8 and writes it as a PNG at `path`
    // (mirrors EditorApplication::CaptureFrameToPng's device lookup, but reads the swapchain, not
    // compute_render_target — see the .cpp definition's comment for why).
    bool CaptureHudFrameToPng(const std::string& path, std::string& err);

    // NOTE: Command buffers, semaphores, and all Vulkan resources
    // are managed by the render graph nodes, not the application

public:
    // --- Embedded-sim driver seams (host-driven; VIXEN-agnostic) -----------------------------------
    // True when the SimLoop's fixed timestep is due this frame; outDt = that fixed timestep (seconds).
    bool ShouldStepLogic(double& outDt);
    // Mark the dense voxel scene for regeneration (kept for legacy/demo callers; not called by the
    // body render path post M-wire — bodies are pushed via SetBodyInstances instead).
    void MarkVoxelSceneDirty();
    // M-wire: push the current per-body instance list into BodyOctreeSceneNode so it re-uploads the
    // SSBO on the next compile tick. Replaces the StarSystemGenerator + MarkVoxelSceneDirty flow.
    void SetBodyInstances(std::vector<Vixen::SVO::BodyInstanceGpu> instances);
    // Spec B I3/Task 6 (= main's I4.1 passthrough — both lines converged on this API): push a
    // boot-baked recipe pool (RecipeBootIngest -> BakeRegistryToPool) into BodyOctreeSceneNode,
    // which then serves octree slots 0..N-1 for the render_recipe blob ids the bridge resolves in
    // ToBodyInstanceGpu. Mirrors SetBodyInstances above — same live GetInstance lookup, same
    // null-guard; a host that owns document/recipe authoring (e.g. vixen_editor) uses this to
    // swap the render source without hand-rolling a NodeTypeRegistry lookup.
    void SetRecipePool(Vixen::SVO::ConcatenatedOctrees pool);
    // Lazy-Procedural-Delta-Baseline Inc0 M5 Task 11: register a zero-bake procedural recipe
    // by id. Fills any unset bounds-metadata field via Recipe::ApplyRecipeBoundsDefaults
    // (Recipe/RecipeBounds.h) before registering. Safe to call BEFORE the graph's first
    // Compile() (the common case -- the shader builder lambda reads proceduralRecipes_ live
    // when it runs) OR AFTER (a live scene addition) -- in the latter case the caller MUST
    // also call RecompileProceduralShader() to force the spliced source to regenerate and the
    // pipeline to rebuild; registering alone does not retroactively touch an already-built
    // pipeline. Returns the registry's own RegisterResult (Ok / DuplicateId / BadOpCode / ...).
    Vixen::SVO::RecipeRegistry::RegisterResult RegisterProceduralRecipe(
        uint32_t recipeId, Vixen::SVO::RecipeRegistry::RecipeEntry entry);
    // Forces BodyInstanceRayMarch.comp to be re-spliced from the CURRENT proceduralRecipes_
    // contents and recompiled (MarkNodeNeedsRecompile on the stored compute_shader_lib node
    // handle). No-op (logs a warning) if the graph hasn't been compiled yet -- there is
    // nothing to mark dirty before the first Compile(), which will pick up whatever is
    // registered at that point anyway.
    void RecompileProceduralShader();
    // Editor Brick-Residency Fix: forwards to BodyOctreeSceneNode::RequestBrickResidency (stash-only,
    // dirty-flag pattern -- safe to call any time, including right after SetRecipePool/SetBodyInstances;
    // ExecuteImpl performs the actual upload next frame). Mirrors SetRecipePool's passthrough above --
    // same live GetInstance lookup, same null-guard. A host that needs its body unconditionally
    // resident (e.g. vixen_editor's single always-in-view document body) calls this instead of
    // relying on UpdateBodySceneResidency's camera-driven heuristic.
    void RequestBodyBrickResidency(bool resident);
    // Host-facing HUD push: forwards into the app-owned HudView (hudView_, wired onto the composite
    // UI node in BuildRenderGraph via WireHudView). Replaces the pre-Inc-2 host call
    // UIRenderNode::SetHudView — the projection now lives on HudView, which this app owns, so hosts
    // push through the app instead of the node. No-op before Prepare() (hudView_ wired but the UI
    // node absent on no-composite-UI graphs is fine — the view just holds the latest push).
    void PushHudView(int tick, int bodyCount, int activeLens, int activeLensCount,
                     std::span<const Vixen::App::HudFactionIn> factions,
                     std::span<const Vixen::App::HudEventIn> events);
    // T1 inspect: forward the selected-entity detail to the HUD (see PushHudView's bridge rationale).
    void PushHudInspect(bool selected, const char* name, const char* cause);
    // Step-6 M-ui: push the selected building's three-channel data into the mounted building-inspector
    // fragment (same bridge rationale as PushHudView). No-op before Prepare() / when nothing is mounted.
    void PushBuildingInspector(const Vixen::App::BuildingInspectorIn& in);
    // Host sim-speed readout: forward the current speed multiplier to the HUD clock line (same bridge
    // rationale as PushHudView). The host reads it from ut_speed each frame.
    void PushHudSpeed(double speed);
    // Expose the GLFW window handle so the host can poll input (e.g. Space/period for pause/step).
    // Queries the WindowNode LIVE each call (the node owns the window post-de-own refactor + persists
    // across recompiles) — no cached handle, so no dangling-pointer window-capture bug.
    GLFWwindow* GetWindowHandle() const;
    // Expose the composite HUD node so the host can push live sim data (SetHudData) each frame. LIVE
    // lookup (like GetWindowHandle) — the node persists across recompiles; returns nullptr if unset
    // (e.g. the VIXEN_UI_DEMO path, which has no composite UI node).
    Vixen::RenderGraph::UIRenderNode* GetUiRenderNode() const;
    // Expose the UI selection provider node so the host can drain HUD clicks (DrainClickedElementId) each
    // frame and forward the clicked element id into the feedback slice. LIVE lookup (like GetUiRenderNode);
    // returns nullptr if unset (e.g. a graph without the selection provider).
    Vixen::RenderGraph::UISelectionProviderNode* GetUiSelectionProviderNode() const;

    // M4b: read back the swapchain image just presented and write it as a PNG. Call AFTER a
    // Render() call (so a real presented frame exists). One-shot, synchronous (waits on the
    // copy internally) — fine for a capture-then-exit tool, not for per-frame use. Returns false
    // (and logs) if the swapchain/device node isn't found or the write fails.
    bool CaptureFrameToPng(const std::string& path);

    // M4b-fix (capture-on-aborted-frame): true only when main_swapchain currently holds a VALID
    // acquired image (currentImageIndex in range). A frame that was aborted mid-flight — e.g. a
    // swapchain recreation from the WSL launch focus toggle — leaves currentImageIndex == UINT32_MAX,
    // and CaptureFrameToPng on that frame fails deep inside FrameCapture ("Image index out of range").
    // The host polls this so it captures on a good frame instead of blindly at frame N. Returns false
    // if the swapchain node/vars aren't ready yet.
    bool SwapchainImageIsValid() const;
};
