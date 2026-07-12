#pragma once
#include "Ui/IView.h"
#include "Generated/EditorLayers.g.h"   // Vixen::Views::{EditorLayerRow,EditorLayersBind,BindEditorLayersModel}
#include <RmlUi/Core/DataModelHandle.h>
#include <cstdint>
#include <string>
#include <vector>

namespace Vixen::App {

// The editor's layer-list view. Inc-A2 (View-Model-Binding-Inc-A2-Plan-2026-07.md): gives the
// editor layer view a real RmlUi data-model — the first model->view path anywhere in the editor
// — so editor.rml's checkboxes reflect LayerController's mask via a data binding instead of
// static "checked" markup. Mirrors Vixen::App::HudView (graph/HudView.h): owns its storage,
// registers the model via the generated BindEditorLayersModel, and projects the mask into it.
//
// Unlike HudView, this TU does not need the HudViewBridge gaia/robin_hood isolation seam:
// EditorApplication.cpp already includes both the gaia-touching Recipe/RecipeBaker headers AND
// Nodes/UIRenderNode.h (which pulls Ui/IView.h + RmlUi/Core/DataModelHandle.h) in one TU, with
// gaia's std::hash<> specialisations ordered first — the exact fix that TU's own file-header
// comment documents for the robin_hood ODR hazard. This header is included from that same,
// already-proven-safe TU (EditorApplication.cpp), so no second bridge TU is needed.
class EditorLayersView final : public Vixen::RenderGraph::IView {
public:
    const char* ModelName() const override { return "editor_layers"; }
    const char* DocumentPath() const override { return "assets/ui/editor.rml"; }
    void Register(Rml::DataModelConstructor& c) override {
        Vixen::Views::BindEditorLayersModel(c, Vixen::Views::EditorLayersBind{ &layers_ });
        model_ = c.GetModelHandle();
    }

    // Rebuilds the bound row array from LayerController's mask. Inc-Ovr (View-Model-Binding-
    // Inc-Ovr-Plan-2026-07.md Task 3): isChecked's bit-decomposition is no longer a hand-written
    // shift here -- it is the schema-declared Projection on EditorLayerRow.isChecked
    // (codegen/view-schemas/EditorLayers.cs), generated as
    // Vixen::Views::ComputeEditorLayerRow_isChecked (Generated/EditorLayers.g.h), which itself
    // calls the transplanted [KernelCallable] Vixen::AppFlow::Generated::bitAt -- the same
    // transform EditorApplication's ToggleLayer handler's applyToggle is the write-side inverse
    // of. name/op/elementId come from the document model (LayerController itself carries no
    // names — see EditorDocumentModel). Dirties "layers" so an already-loaded model picks up the
    // change (initial population calls this before the document loads, so the dirty is a no-op
    // there; a later re-population, e.g. after the optional same-frame echo, needs it).
    void PopulateFromMask(uint32_t mask, uint32_t layerCount,
                          const std::vector<std::string>& names, const std::vector<std::string>& ops) {
        layers_.clear();
        layers_.reserve(layerCount);
        for (uint32_t i = 0; i < layerCount; ++i) {
            Vixen::Views::EditorLayerRow row;
            row.name      = i < names.size() ? Rml::String(names[i]) : Rml::String{};
            row.op        = i < ops.size()   ? Rml::String(ops[i])   : Rml::String{};
            row.isChecked = Vixen::Views::ComputeEditorLayerRow_isChecked(mask, i);
            row.elementId = "layer-" + std::to_string(i) + "-toggle";
            layers_.push_back(std::move(row));
        }
        if (model_) model_.DirtyVariable("layers");
    }

    // Debug accessor for tests.
    size_t DebugLayerCount() const { return layers_.size(); }
    const Vixen::Views::EditorLayerRow& DebugLayer(size_t i) const { return layers_.at(i); }

private:
    std::vector<Vixen::Views::EditorLayerRow> layers_;
    Rml::DataModelHandle model_;
};

}  // namespace Vixen::App
