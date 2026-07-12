#pragma once
// EditorApplication — Inc1 vixen_editor: loads a VoxelDocument, live-renders it through
// the existing body-octree/recipe-pool render path, and supports layer enable/disable
// (UI click -> document -> flatten -> re-render) plus save. Reuses VulkanGraphApplication's
// whole graph (window, body-octree scene, UI composite HUD) unmodified; BuildRenderGraph
// calls the base implementation and then re-points the UI node at editor.rml, and the one
// default body instance is replaced by a single instance selecting the document's baked
// recipe-pool slot.
//
// Inc-4 reframe (design D10/D15, R5): the editor is a PURE CONSUMER of the AppFlow registry
// -- it names zero triggers/actions in code. It registers one self-contained handler per
// action (each handler decides for itself whether to go through Stack() (undoable), a
// service, or a bare side effect -- the framework knows none of this) and dispatches
// exclusively via rt_.DispatchBySelector()/DispatchByKey(): UI clicks resolve through
// DispatchBySelector(clickedId); keys resolve through DispatchByKey via KeyMap.h's
// GLFW-keycode -> KeyId map. Update() drains the UI selection provider's clicked-element-id
// each frame (S4 pattern, DrainClickedElementId) and re-flattens/re-uploads on the next tick
// after a toggle (mirrors BodyOctreeSceneNode::SetRecipePool's post-Compile dirty-flag/
// in-Execute re-materialize — no MarkNeedsRecompile).
#include "VulkanGraphApplication.h"
#include "EditorDocumentModel.h"
#include "AppFlowRuntime.h"
#include "GaiaLayerViewDataProvider.h"  // Inc-B: Gaia-backed IViewDataProvider (was LayerControllerViewDataProvider)
#include "ViewReconcileNode.h"         // Inc-B: per-frame .changed<LayerMask>() reconcile
#include "EditorLayersViewBridge.h"  // Inc-A2: gaia/robin_hood ODR isolation seam -- see its file header
#include <Logger.h>

#include <memory>
#include <string>
#include <vector>

// Forward-declared only -- see EditorLayersViewBridge.h's file header for why this TU (which
// transitively sees gaia.h via Recipe/RecipeBaker.h below) never includes EditorLayersView.h itself.
namespace Vixen::App { class EditorLayersView; }

class EditorApplication : public VulkanGraphApplication {
public:
    // documentPath: .vxd to load. Empty = caller must call LoadDocument() before Prepare().
    explicit EditorApplication(std::string documentPath);
    // Explicit (not defaulted): layersView_ is a raw pointer to a forward-declared-only
    // EditorLayersView (see EditorLayersViewBridge.h) -- the destructor body must call
    // DestroyEditorLayersView() through a complete-type call site, defined in
    // EditorApplication.cpp, not implicitly generated here where the type is incomplete.
    ~EditorApplication() override;

    // Inc-2b Task 4: one parsed VIXEN_EDITOR_SCRIPT entry (e.g. "toggle:2@30" or "undo@60").
    // Public (plain data, no invariants) so the free-function parser in EditorApplication.cpp's
    // anonymous namespace can build a std::vector<ScriptedAction> without befriending it.
    struct ScriptedAction {
        long frame = 0;
        // R6a: Settings/Back added to exercise the back-button->Return edge in the RUNNING
        // editor (not just the FSM unit test) -- Settings drives NavTo(Settings) directly
        // (a real editor has no "open settings" UI yet, so there is no selector to route this
        // through; NavTo is a public service call, same primitive Return's own handler uses),
        // Back drives DispatchBySelector("back-button") -- the real dispatch path a back-button
        // click would take.
        enum class Kind { Toggle, Undo, Redo, Settings, Back } kind = Kind::Undo;
        uint32_t layerIndex = 0;  // only meaningful for Kind::Toggle
    };

    void BuildRenderGraph() override;
    void PreTick() override;   // graph.Run(): scripted-action injector runs here, before Update()
    void Update() override;

    // Loads (or reloads) the document. Must be called before Prepare()/BuildRenderGraph()
    // for the initial load; safe to no-op-check via LastError() on failure.
    bool LoadDocument(const std::string& path);

    // Reconstructs the document with the current enabled overrides and writes it to
    // "<input-path-without-extension>.edited.vxd". Returns false (see LastEditorError())
    // on failure.
    bool SaveDocument();

    const Vixen::Editor::EditorDocumentModel& DocumentModel() const { return doc_; }
    const std::string& LastEditorError() const { return lastEditorError_; }

    // Flattens the current document state into a RecipeEntry, bakes a fresh single-recipe
    // pool, and pushes it into BodyOctreeSceneNode via SetRecipePool + one instance
    // (providerKind=0/Stored default, octreeIndex=0 selects the pool's only baked slot —
    // see test_recipe_pool_render.cpp for the identical pattern). Returns false
    // (lastEditorError_ set) on flatten/bake failure. Public so the headless live-gate test
    // can drive the exact same path the app uses without booting a window.
    bool ApplyDocumentToScene();

    // Editor Brick-Residency Fix (2026-07): the editor's one document body is the object being
    // directly edited and is always in view — it must render the fine SDF march (where the layer
    // mask lives), not the mask-invariant coarse mip-fallback the main app's camera-driven
    // heuristic would otherwise leave it on for a static session. Opts the body out of
    // VulkanGraphApplication::UpdateBodySceneResidency entirely (see ApplyDocumentToScene, which
    // grants residency unconditionally instead).
    bool SkipResidencyHeuristic() const override { return true; }

    // Inc-2b Task 3: reads the capture render target's CURRENT image back to host RGBA8 and
    // writes it as a PNG at `path`. Gated end-to-end on VIXEN_EDITOR_CAPTURE_FRAMES being set
    // (see BuildRenderGraph -- the capture target itself is not created otherwise). Looks the
    // target up LIVE by instance name every call (mirrors GetWindowHandle's live-lookup rule --
    // never cache a node pointer); if the target doesn't exist (capture not enabled, or a
    // recompile hasn't run yet) sets err and returns false without throwing. Public so the
    // scripted-harness call site (Update) and a future assertion test can both drive it.
    bool CaptureFrameToPng(const std::string& path, std::string& err);

private:
    // Inc-A2: re-derives layersView_'s bound "layers" array from doc_'s per-layer name/op plus
    // rt_.Layers().Mask()'s current bit state. Shared by the initial population (LoadDocument)
    // and the ToggleLayer handler's same-frame echo (the SAME ApplyFn body Undo()/Redo() re-run,
    // so one call site here covers toggle, undo, and redo alike).
    void RefreshLayersView();

    // Inc-B (View-Model-Binding-Inc-B-Plan-2026-07.md, design §4/§4a): runs the per-frame
    // .changed<LayerMask>() reconcile against the Gaia layer entity. Model->view for changes NOT
    // driven by the editor's own input (the ToggleLayer handler's same-frame echo above already
    // covers that case) -- e.g. a deterministic external write to gaiaLayerEntity_'s LayerMask
    // component, bypassing WriteU32 entirely. Called from Update(), AFTER input dispatch, so
    // cross-view/external propagation lands same-frame where possible (design §4a). Re-syncs
    // rt_.Layers() (the ToggleLayer handler's own mask source, used for undo snapshots + the
    // scripted-action state dump) so the Gaia component stays the single source of truth an
    // external write mutates, not a second copy that would silently diverge.
    void ReconcileLayersView();


    std::string documentPath_;
    Vixen::Editor::EditorDocumentModel doc_;
    // Inc-2b: the editor owns an AppFlowRuntime (bus=nullptr — Publish no-ops; the editor
    // doesn't consume the events yet) so toggle/undo/redo route through the ActionStack
    // instead of mutating LayerController directly. Layers() exposes the same mask source
    // of truth Inc-2's raw layers_ member used.
    Vixen::AppFlow::AppFlowRuntime rt_{nullptr, /*sender*/0};
    // Inc-B: the editor's own Gaia world (Task 1 finding -- vixen_editor previously only pulled
    // gaia.h TRANSITIVELY via SVO's ShellOctree/LaineKarrasOctree; nothing instantiated a
    // GaiaVoxelWorld. A live world here is cheap -- GaiaVoxelWorld wraps one gaia::ecs::World
    // member plus caches, exactly what libraries/GaiaVoxelWorld/tests construct per-test -- so
    // Inc-B gives the editor a real one rather than reaching for the plan's headless-gtest
    // fallback (see the Inc-B report for the full Task-1 writeup)). Owns exactly one entity
    // (gaiaLayerEntity_) carrying the LayerMask component; nothing else in the editor touches
    // this world.
    Vixen::GaiaVoxel::GaiaVoxelWorld gaiaWorld_;
    // A bare entity (no MortonKey/spatial identity -- LayerMask is the only component it carries).
    // Constructed via a helper (MakeGaiaLayerEntity, EditorApplication.cpp) rather than a default
    // member initializer referencing gaiaWorld_ -- member initializers run in DECLARATION order
    // (gaiaWorld_ first, so this is technically safe), but the helper keeps the "what does this
    // entity look like" logic out of the header, same rationale as layersView_'s bridge factory.
    Vixen::GaiaVoxel::GaiaVoxelWorld::EntityID gaiaLayerEntity_ = Vixen::App::MakeGaiaLayerEntity(gaiaWorld_);
    // Inc-A: the view->model seam's provider (design View-Data-Provider-Seam-Design-2026-07.md).
    // ToggleLayer reads/writes LayerMask through this instead of touching a mask store directly.
    // Inc-B swaps the direct-field LayerControllerViewDataProvider for this Gaia-backed one --
    // same seam, same handler body, only this one construction changed (GaiaLayerViewDataProvider.h).
    // Binds gaiaWorld_/gaiaLayerEntity_, both declared above -- default member initializers run in
    // declaration order, so both are already constructed here.
    Vixen::App::GaiaLayerViewDataProvider layerProvider_{gaiaWorld_, gaiaLayerEntity_};
    // Inc-B: owns the persistent per-frame .changed<LayerMask>() query (design §4/§4b). Binds
    // gaiaWorld_ (declared above -- same declaration-order argument as layerProvider_).
    Vixen::App::ViewReconcileNode viewReconcile_{gaiaWorld_};
    // Inc-A2: the editor layer view's data-model host (design View-Model-Binding-Inc-A2-Plan-
    // 2026-07.md). Owned here (mirrors HudView's hudView_ ownership in VulkanGraphApplication --
    // same raw-pointer-via-bridge-factory pattern, same rationale: forward-declared-only type),
    // wired onto the UI node via WireEditorLayersView in BuildRenderGraph, and populated from
    // doc_/rt_.Layers() at LoadDocument time -- the first model->view path anywhere in the editor.
    Vixen::App::EditorLayersView* layersView_ = Vixen::App::MakeEditorLayersView();
    bool dirty_ = false;  // set on toggle; drives the next-tick re-flatten (was doc_.ConsumeDirty())
    std::string lastEditorError_;
    std::string lastSavedPath_;
    bool sKeyWasDown_ = false;  // edge-detect for the Save keybinding
    bool ctrlZWasDown_ = false;  // edge-detect for the Undo keybinding
    bool ctrlYWasDown_ = false;  // edge-detect for the Redo keybinding
    bool escWasDown_ = false;  // edge-detect for the Return (Esc) keybinding
    bool handlersRegistered_ = false;  // guards the one-time RegisterHandler calls in LoadDocument

    // Inc-2b Task 3/4: capture + script harness state, all zero-cost/inert when the two
    // VIXEN_EDITOR_* env knobs are unset (see BuildRenderGraph + Update).
    //
    // Capture-target decision (see BuildRenderGraph.cpp's file header comment for the plan's
    // option A/B language): rather than adding a NEW RenderTargetNode instance to the editor
    // graph, CaptureFrameToPng reads the standard graph's existing "compute_render_target"
    // instance (added by the base VulkanGraphApplication::BuildRenderGraph, see
    // application/main/source/graph/BuildRenderGraph.cpp) -- the offscreen target the compute
    // voxel-raymarch dispatch renders the scene into every frame, BEFORE the UI composite blits
    // it up to the swapchain. It already has VK_IMAGE_USAGE_TRANSFER_SRC_BIT set (for that same
    // blit), is sized to follow the swapchain 1:1 by default, and reliably holds the full
    // rendered body (toggle/undo/redo all show up there) with zero new node wiring. So there is
    // no separate "capture target name" member -- the literal instance name is used directly at
    // the one CaptureFrameToPng call site (kept as a local constant there, not duplicated here).
    long updateTick_ = 0;  // editor-local Update tick counter, independent of the base app's own counters

    std::vector<ScriptedAction> scriptedActions_;   // parsed once from VIXEN_EDITOR_SCRIPT
    std::vector<long> captureFrames_;               // parsed once from VIXEN_EDITOR_CAPTURE_FRAMES
    std::string captureDir_ = "temp";               // overridable via VIXEN_EDITOR_CAPTURE_DIR
    bool scriptParsed_ = false;                     // guards the one-time env parse in Update()

    std::shared_ptr<Vixen::Log::Logger> logger_ = std::make_shared<Vixen::Log::Logger>("editor", true);
};
