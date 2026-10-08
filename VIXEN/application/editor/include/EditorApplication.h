#pragma once
// EditorApplication — Inc1 vixen_editor: loads a VoxelDocument, live-renders it through
// the shared body-octree/recipe and virtual-provider paths, and supports rule editing
// (UI selector -> headless document operation -> preview refresh) plus save/reopen. Reuses VulkanGraphApplication's
// whole graph (window, body-octree scene, UI composite HUD) unmodified; BuildRenderGraph
// calls the base implementation and then re-points the UI node at editor.rml, and the one
// default body instance is replaced by a procedural instance using the document's registered
// bytecode and the same six-value snapshot consumed by the CPU bake.
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
#include <array>
#include <filesystem>
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

    // One parsed VIXEN_EDITOR_SCRIPT entry (e.g. "parameter_up:0@30" or "undo@60").
    // Public (plain data, no invariants) so the free-function parser in EditorApplication.cpp's
    // anonymous namespace can build a std::vector<ScriptedAction> without befriending it.
    struct ScriptedAction {
        long frame = 0;
        enum class Kind {
            Toggle, CreateLayer, DeleteLayer, MoveLayerUp, MoveLayerDown,
            ProgramFieldUp, ProgramFieldDown, ParameterUp, ParameterDown,
            Undo, Redo, Save, Reopen, Settings, Back
        } kind = Kind::Undo;
        uint32_t index = 0;  // layer or parameter index, depending on Kind
    };

    void BuildRenderGraph() override;
    void PreTick() override;   // graph.Run(): scripted-action injector runs here, before Update()
    void Update() override;
    void PostTick() override;  // captures scripted frames after this tick's Render()

    // Loads (or reloads) the document. Must be called before Prepare()/BuildRenderGraph()
    // for the initial load; safe to no-op-check via LastError() on failure.
    bool LoadDocument(const std::string& path);

    // Reconstructs the document from the headless model's current enabled state and writes it to
    // "<input-path-without-extension>.edited.vxd". Returns false (see LastEditorError())
    // on failure.
    bool SaveDocument();
    bool ReopenSavedDocument();

    const Vixen::Editor::EditorDocumentModel& DocumentModel() const { return doc_; }
    const std::string& LastEditorError() const { return lastEditorError_; }

    // Captures one document revision and effective-parameter snapshot, bakes that entry into a
    // fresh CPU pool, and updates the shared virtual recipe instance. Returns false
    // (lastEditorError_ set) on flatten, registry, or bake failure.
    bool ApplyDocumentToScene(bool reframeCamera = false);

    const Vixen::Editor::DocumentBakeSnapshot& LastBakeSnapshot() const { return lastBakeSnapshot_; }
    const std::array<float, 6>& LastPreviewParameterValues() const { return lastPreviewParameterValues_; }

    // Editor Brick-Residency Fix (2026-07): the editor's one document body is the object being
    // directly edited and is always in view — it must render the fine SDF march (where the layer
    // mask lives), not the mask-invariant coarse mip-fallback the main app's camera-driven
    // heuristic would otherwise leave it on for a static session. Opts the body out of
    // VulkanGraphApplication::UpdateBodySceneResidency entirely (see ApplyDocumentToScene, which
    // grants residency unconditionally instead).
    bool SkipResidencyHeuristic() const override { return true; }

    // Reads the current completed compute_render_target image to PNG. Call after Render() so it
    // contains the scene output before the HUD covers part of the viewport. Scripted captures
    // are scheduled in Update() and performed in PostTick().
    bool CaptureFrameToPng(const std::string& path, std::string& err);

private:
    // Replays the consumer-owned handler wiring after AppFlowRuntime replaces its primitives.
    void RegisterAppFlowHandlers();

    // Polls the external AppFlow artifact at a bounded cadence. The swap is called from Update()
    // before input dispatch, which is the editor's between-tick point.
    void PollAppFlowFile();

    // Inc-A2: re-derives layersView_'s bound "layers" array from doc_'s per-layer name/op and
    // enabled mask. Shared by the initial population (LoadDocument)
    // and the ToggleLayer handler's same-frame echo (the SAME ApplyFn body Undo()/Redo() re-run,
    // so one call site here covers toggle, undo, and redo alike).
    void RefreshLayersView();

    // Inc-B (View-Model-Binding-Inc-B-Plan-2026-07.md, design §4/§4a): runs the per-frame
    // .changed<LayerMask>() reconcile against the Gaia layer entity. Model->view for changes NOT
    // driven by the editor's own input (the ToggleLayer handler's same-frame echo above already
    // covers that case) -- e.g. a deterministic external write to gaiaLayerEntity_'s LayerMask
    // component, bypassing WriteU32 entirely. Called from Update(), AFTER input dispatch, so
    // cross-view/external propagation lands same-frame where possible (design §4a). Valid Gaia
    // writes are applied through EditorDocumentModel, then mirrored to the runtime projection.
    void ReconcileLayersView();
    void SyncAfterDocumentMutation();


    std::string documentPath_;
    Vixen::Editor::EditorDocumentModel doc_;
    // Inc-2b: the editor owns an AppFlowRuntime (bus=nullptr — Publish no-ops; the editor
    // doesn't consume the events yet) so toggle/undo/redo route through the ActionStack. The
    // runtime's LayerController is a projection for AppFlow snapshots; EditorDocumentModel owns
    // the accepted document mask and applies every layer mutation.
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
    // Accepted document state is projected through this provider; external writes are validated
    // and applied through EditorDocumentModel during ReconcileLayersView().
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
    // doc_ at LoadDocument time -- the first model->view path anywhere in the editor.
    Vixen::App::EditorLayersView* layersView_ = Vixen::App::MakeEditorLayersView();
    bool dirty_ = false;  // set on toggle; drives the next-tick re-flatten (was doc_.ConsumeDirty())
    std::string lastEditorError_;
    std::string lastSavedPath_;
    std::vector<Yeroket::Sdf::Generated::SdfInstruction> lastSavedProgram_;
    std::array<float, 6> lastSavedParameters_{};
    uint32_t lastSavedMask_ = 0;
    Vixen::Editor::DocumentBakeSnapshot lastBakeSnapshot_{};
    std::array<float, 6> lastPreviewParameterValues_{};
    std::vector<Yeroket::Sdf::Generated::SdfInstruction> lastProceduralProgram_;
    bool proceduralPreviewRegistered_ = false;
    bool sKeyWasDown_ = false;  // edge-detect for the Save keybinding
    bool ctrlZWasDown_ = false;  // edge-detect for the Undo keybinding
    bool ctrlYWasDown_ = false;  // edge-detect for the Redo keybinding
    bool escWasDown_ = false;  // edge-detect for the Return (Esc) keybinding
    std::string appFlowPath_;
    std::filesystem::file_time_type appFlowMtime_{};
    bool appFlowMtimeInitialized_ = false;

    // Inc-2b Task 3/4: capture + script harness state, inert when the two VIXEN_EDITOR_* env
    // knobs are unset (see BuildRenderGraph + Update). CaptureFrameToPng reads the existing
    // compute_render_target so UI overlays cannot satisfy a scene-render assertion.
    std::string pendingCapturePath_;  // one capture is scheduled by Update() for PostTick()
    long updateTick_ = 0;  // editor-local Update tick counter, independent of the base app's own counters

    std::vector<ScriptedAction> scriptedActions_;   // parsed once from VIXEN_EDITOR_SCRIPT
    std::vector<long> captureFrames_;               // parsed once from VIXEN_EDITOR_CAPTURE_FRAMES
    std::string captureDir_ = "temp";               // overridable via VIXEN_EDITOR_CAPTURE_DIR
    bool scriptParsed_ = false;                     // guards the one-time env parse in Update()

    std::shared_ptr<Vixen::Log::Logger> logger_ = std::make_shared<Vixen::Log::Logger>("editor", true);
};
