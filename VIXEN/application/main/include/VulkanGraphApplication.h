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
#include <memory>
#include <string>
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

using namespace Vixen::Vulkan::Resources;
using namespace Vixen::RenderGraph;

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
};
