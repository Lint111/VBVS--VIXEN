#pragma once
// EditorApplication — Inc1 vixen_editor: loads a VoxelDocument, live-renders it through
// the existing body-octree/recipe-pool render path, and supports layer enable/disable
// (UI click -> document -> flatten -> re-render) plus save. Reuses VulkanGraphApplication's
// whole graph (window, body-octree scene, UI composite HUD) unmodified; BuildRenderGraph
// calls the base implementation and then re-points the UI node at editor.rml, and the one
// default body instance is replaced by a single instance selecting the document's baked
// recipe-pool slot. Update() drains the UI selection provider's clicked-element-id each
// frame (S4 pattern, DrainClickedElementId) and re-flattens/re-uploads on the next tick
// after a toggle (mirrors BodyOctreeSceneNode::SetRecipePool's post-Compile dirty-flag/
// in-Execute re-materialize — no MarkNeedsRecompile).
#include "VulkanGraphApplication.h"
#include "EditorDocumentModel.h"
#include "AppFlowRuntime.h"
#include <Logger.h>

#include <memory>
#include <string>

class EditorApplication : public VulkanGraphApplication {
public:
    // documentPath: .vxd to load. Empty = caller must call LoadDocument() before Prepare().
    explicit EditorApplication(std::string documentPath);

    void BuildRenderGraph() override;
    void Update() override;

    // Loads (or reloads) the document. Must be called before Prepare()/BuildRenderGraph()
    // for the initial load; safe to no-op-check via LastError() on failure.
    bool LoadDocument(const std::string& path);

    // Toggles a layer's enabled override (UI checkbox click handler). Takes effect on the
    // next Update() tick via the dirty-flag re-flatten/re-upload path.
    void ToggleLayer(uint32_t layerIndex);

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

    // Inc-2b Task 3: reads the capture render target's CURRENT image back to host RGBA8 and
    // writes it as a PNG at `path`. Gated end-to-end on VIXEN_EDITOR_CAPTURE_FRAMES being set
    // (see BuildRenderGraph -- the capture target itself is not created otherwise). Looks the
    // target up LIVE by instance name every call (mirrors GetWindowHandle's live-lookup rule --
    // never cache a node pointer); if the target doesn't exist (capture not enabled, or a
    // recompile hasn't run yet) sets err and returns false without throwing. Public so the
    // scripted-harness call site (Update) and a future assertion test can both drive it.
    bool CaptureFrameToPng(const std::string& path, std::string& err);

private:
    std::string documentPath_;
    Vixen::Editor::EditorDocumentModel doc_;
    // Inc-2b: the editor owns an AppFlowRuntime (bus=nullptr — Publish no-ops; the editor
    // doesn't consume the events yet) so toggle/undo/redo route through the ActionStack
    // instead of mutating LayerController directly. Layers() exposes the same mask source
    // of truth Inc-2's raw layers_ member used.
    Vixen::AppFlow::AppFlowRuntime rt_{nullptr, /*sender*/0};
    bool dirty_ = false;  // set on toggle; drives the next-tick re-flatten (was doc_.ConsumeDirty())
    std::string lastEditorError_;
    std::string lastSavedPath_;
    bool sKeyWasDown_ = false;  // edge-detect for the Save keybinding
    bool ctrlZWasDown_ = false;  // edge-detect for the Undo keybinding
    bool ctrlYWasDown_ = false;  // edge-detect for the Redo keybinding

    // Inc-2b Task 3: capture-target decision (see BuildRenderGraph.cpp's file header comment for
    // the plan's option A/B language): rather than adding a NEW RenderTargetNode instance to the
    // editor graph, CaptureFrameToPng reads the standard graph's existing "compute_render_target"
    // instance (added by the base VulkanGraphApplication::BuildRenderGraph, see
    // application/main/source/graph/BuildRenderGraph.cpp) -- the offscreen target the compute
    // voxel-raymarch dispatch renders the scene into every frame, BEFORE the UI composite blits
    // it up to the swapchain. It already has VK_IMAGE_USAGE_TRANSFER_SRC_BIT set (for that same
    // blit), is sized to follow the swapchain 1:1 by default, and reliably holds the full
    // rendered body (toggle/undo/redo all show up there) with zero new node wiring. So there is
    // no separate "capture target name" member -- the literal instance name is used directly at
    // the one CaptureFrameToPng call site (kept as a local constant there, not duplicated here).

    std::shared_ptr<Vixen::Log::Logger> logger_ = std::make_shared<Vixen::Log::Logger>("editor", true);
};
