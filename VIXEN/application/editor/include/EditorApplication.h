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

private:
    std::string documentPath_;
    Vixen::Editor::EditorDocumentModel doc_;
    std::string lastEditorError_;
    std::string lastSavedPath_;
    bool sKeyWasDown_ = false;  // edge-detect for the Save keybinding
    std::shared_ptr<Vixen::Log::Logger> logger_ = std::make_shared<Vixen::Log::Logger>("editor", true);
};
