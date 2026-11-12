#pragma once
#include "EditorLayersViewBridge.h"
#include "Ui/IView.h"
#include "Generated/EditorLayers.g.h"   // Vixen::Views::{EditorLayerRow,EditorLayersBind,BindEditorLayersModel}
#include <RmlUi/Core/DataModelHandle.h>
#include <bit>
#include <cstdint>
#include <string>
#include <vector>

namespace Vixen::Views {

// Inc-Ovr (design §5b Override proof): the hand-written hook the generated
// BindEditorLayersModel calls for EditorLayers::activeLayerCount -- codegen emits only the
// forward declaration (Generated/EditorLayers.g.h: `int BindEditorLayersModel_
// activeLayerCountOverride(int* activeLayerCount);`, matching the same raw-storage-pointer shape
// every other Bind field gets), no body; this IS the whole implementation, and omitting it fails
// the link (proven negatively in Milestone 3's build gate -- LNK2019 unresolved external symbol,
// not a silent no-op or runtime crash). The bound storage (`activeLayerCountRaw_`, see
// EditorLayersView below) actually holds the raw mask, reused as an `int` bit pattern -- this
// hook's job is popcount(mask), a genuine aggregate computation over the whole bitset, not a
// per-field 1:1 transform of one value. That's precisely why it doesn't fit Projection's shape
// (design §5a maps ONE source value to ONE bound value via a named transform) and needs Override:
// the framework generates no wiring at all, only this hook point.
inline int BindEditorLayersModel_activeLayerCountOverride(int* maskAsInt) noexcept {
    return std::popcount(static_cast<uint32_t>(*maskAsInt));
}

}  // namespace Vixen::Views

namespace Vixen::App {

// The editor's layer-list view. Inc-A2 (View-Model-Binding-Inc-A2-Plan-2026-07.md): gives the
// editor layer view a real RmlUi data-model — the first model->view path anywhere in the editor
// — so editor_layers.rml's checkboxes reflect the document's enabled mask via a data binding instead of
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
    // R766: the editor's panels (outliner, layers, inspector, status) each mount their own document over
    // this one model; this is the layers panel's document.
    const char* DocumentPath() const override { return "assets/ui/editor_layers.rml"; }
    void Register(Rml::DataModelConstructor& c) override {
        Vixen::Views::BindEditorLayersModel(c,
            Vixen::Views::EditorLayersBind{ &layers_, &parameters_, &transform_, &selectedLayerName_,
                                            &selectedLayer_, &primitives_, &opChoices_, &shape_,
                                            &selectedShape_, &shapeNote_, &activeLayerCountRaw_, &recipes_,
                                            &statusDocument_, &statusMessage_, &flowState_,
                                            &undoDepth_, &redoDepth_ });
        model_ = c.GetModelHandle();
    }

    // Rebuilds the bound row arrays from EditorDocumentModel's accepted state. Inc-Ovr (View-Model-Binding-
    // Inc-Ovr-Plan-2026-07.md Task 3): isChecked's bit-decomposition is no longer a hand-written
    // shift here -- it is the schema-declared Projection on EditorLayerRow.isChecked
    // (codegen/view-schemas/EditorLayers.cs), generated as
    // Vixen::Views::ComputeEditorLayerRow_isChecked (Generated/EditorLayers.g.h), which itself
    // calls the transplanted [KernelCallable] Vixen::AppFlow::Generated::bitAt -- the same
    // transform the EditorDocumentModel operation used by EditorApplication's ToggleLayer handler.
    // names, operations, and parameter metadata come from the document model. Dirties the bound
    // arrays so an already-loaded model picks up the
    // change (initial population calls this before the document loads, so the dirty is a no-op
    // there; a later re-population, e.g. after the optional same-frame echo, needs it).
    void PopulateFromDocument(uint32_t mask,
                              const std::vector<EditorLayerData>& layerData,
                              const std::vector<EditorParameterData>& parameterData,
                              const EditorPlacementData& placement = {},
                              const EditorShapeData& shape = {}) {
        layers_.clear();
        layers_.reserve(layerData.size());
        for (uint32_t i = 0; i < layerData.size(); ++i) {
            Vixen::Views::EditorLayerRow row;
            row.name      = Rml::String(layerData[i].name);
            row.op        = Rml::String(layerData[i].op);
            row.isChecked = Vixen::Views::ComputeEditorLayerRow_isChecked(mask, i);
            row.elementId = "layer-" + std::to_string(i) + "-toggle";
            row.moveUpId = "layer-" + std::to_string(i) + "-up";
            row.moveDownId = "layer-" + std::to_string(i) + "-down";
            row.deleteId = "layer-" + std::to_string(i) + "-delete";
            row.blendRadius = layerData[i].blendRadius;
            row.blendUpId = "layer-" + std::to_string(i) + "-nudge-blend-up";
            row.blendDownId = "layer-" + std::to_string(i) + "-nudge-blend-down";
            row.selectId = "layer-" + std::to_string(i) + "-select";
            row.isSelected = static_cast<int>(i) == placement.selectedLayer;
            layers_.push_back(std::move(row));
        }
        parameters_.clear();
        parameters_.reserve(parameterData.size());
        for (uint32_t i = 0; i < parameterData.size(); ++i) {
            Vixen::Views::EditorParameterRow row;
            row.name = Rml::String(parameterData[i].name);
            row.unit = Rml::String(parameterData[i].unit);
            row.upId = "parameter-" + std::to_string(i) + "-up";
            row.downId = "parameter-" + std::to_string(i) + "-down";
            row.value = parameterData[i].value;
            row.minimum = parameterData[i].minimum;
            row.maximum = parameterData[i].maximum;
            parameters_.push_back(std::move(row));
        }
        // E3: the selected layer's placement. Field keys are the SetLayerTransform param names, so a
        // nudge id reads "layer-<selected>-nudge-<key>-<up|down>".
        static constexpr const char* kPlacementLabels[9] = {
            "position x", "position y", "position z", "rotation x", "rotation y", "rotation z",
            "scale x", "scale y", "scale z"};
        static constexpr const char* kPlacementKeys[9] = {
            "px", "py", "pz", "rx", "ry", "rz", "sx", "sy", "sz"};
        selectedLayer_ = placement.selectedLayer;
        selectedLayerName_ = Rml::String(placement.selectedName);
        transform_.clear();
        if (!layerData.empty()) {
            transform_.reserve(9);
            for (size_t f = 0; f < 9; ++f) {
                Vixen::Views::EditorTransformRow row;
                const std::string stem = "layer-" + std::to_string(placement.selectedLayer) +
                                         "-nudge-" + kPlacementKeys[f];
                row.label = kPlacementLabels[f];
                row.value = placement.values[f];
                row.upId = stem + "-up";
                row.downId = stem + "-down";
                transform_.push_back(std::move(row));
            }
        }
        // R872.1: the selected layer's combine-op choices and typed primitive fields.
        opChoices_.clear();
        shape_.clear();
        if (!layerData.empty()) {
            const std::string layerStem = "layer-" + std::to_string(placement.selectedLayer);
            for (const auto& choice : shape.opChoices) {
                Vixen::Views::EditorLayerOpRow row;
                row.name = Rml::String(choice.name);
                row.elementId = layerStem + "-op-" + choice.name;
                row.isCurrent = choice.isCurrent;
                opChoices_.push_back(std::move(row));
            }
            for (const auto& field : shape.fields) {
                Vixen::Views::EditorShapeFieldRow row;
                row.label = Rml::String(field.label);
                row.value = field.value;
                row.hasRange = field.hasRange;
                row.minimum = field.minimum;
                row.maximum = field.maximum;
                row.upId = layerStem + "-nudge-" + field.key + "-up";
                row.downId = layerStem + "-nudge-" + field.key + "-down";
                shape_.push_back(std::move(row));
            }
        }
        selectedShape_ = Rml::String(shape.primitive);
        shapeNote_ = Rml::String(shape.note);
        // activeLayerCount (Inc-Ovr Override proof): storage holds the raw mask reinterpreted as
        // int; BindEditorLayersModel_activeLayerCountOverride (this file, above) popcounts it at
        // bind time. Only needs a dirty when the mask itself changes, same as "layers".
        activeLayerCountRaw_ = static_cast<int>(mask);
        if (model_) {
            model_.DirtyVariable("layers");
            model_.DirtyVariable("parameters");
            model_.DirtyVariable("transform");
            model_.DirtyVariable("selectedLayerName");
            model_.DirtyVariable("selectedLayer");
            model_.DirtyVariable("opChoices");
            model_.DirtyVariable("shape");
            model_.DirtyVariable("selectedShape");
            model_.DirtyVariable("shapeNote");
            model_.DirtyVariable("activeLayerCount");
        }
    }

    // R872.1: the primitive picker; each row carries its CreatePrimitiveLayer selector.
    void PopulatePrimitives(const std::vector<EditorPrimitiveData>& primitiveData) {
        primitives_.clear();
        primitives_.reserve(primitiveData.size());
        for (const auto& primitive : primitiveData) {
            Vixen::Views::EditorPrimitiveRow row;
            row.name = Rml::String(primitive.name);
            row.category = Rml::String(primitive.category);
            row.elementId = "primitive-" + primitive.name + "-add";
            primitives_.push_back(std::move(row));
        }
        if (model_) model_.DirtyVariable("primitives");
    }

    // R765: the open pack's render-recipe documents; each row carries its OpenRecipe selector.
    void PopulateRecipes(const std::vector<EditorRecipeData>& recipeData) {
        recipes_.clear();
        recipes_.reserve(recipeData.size());
        for (const auto& recipe : recipeData) {
            Vixen::Views::EditorRecipeRow row;
            row.id = Rml::String(recipe.id);
            row.elementId = "recipe-" + recipe.id + "-open";
            row.isOpen = recipe.isOpen;
            recipes_.push_back(std::move(row));
        }
        if (model_) model_.DirtyVariable("recipes");
    }

    // R766: the status-line panel's fields.
    void PopulateStatus(const EditorStatusData& status) {
        statusDocument_ = Rml::String(status.document);
        statusMessage_ = Rml::String(status.message);
        flowState_ = Rml::String(status.flowState);
        undoDepth_ = static_cast<int>(status.undoDepth);
        redoDepth_ = static_cast<int>(status.redoDepth);
        if (model_) {
            model_.DirtyVariable("statusDocument");
            model_.DirtyVariable("statusMessage");
            model_.DirtyVariable("flowState");
            model_.DirtyVariable("undoDepth");
            model_.DirtyVariable("redoDepth");
        }
    }

    // Debug accessor for tests.
    size_t DebugLayerCount() const { return layers_.size(); }
    const Vixen::Views::EditorLayerRow& DebugLayer(size_t i) const { return layers_.at(i); }
    size_t DebugParameterCount() const { return parameters_.size(); }
    size_t DebugPrimitiveCount() const { return primitives_.size(); }
    const Vixen::Views::EditorPrimitiveRow& DebugPrimitive(size_t i) const { return primitives_.at(i); }
    size_t DebugOpChoiceCount() const { return opChoices_.size(); }
    const Vixen::Views::EditorLayerOpRow& DebugOpChoice(size_t i) const { return opChoices_.at(i); }
    size_t DebugShapeFieldCount() const { return shape_.size(); }
    const Vixen::Views::EditorShapeFieldRow& DebugShapeField(size_t i) const { return shape_.at(i); }
    int DebugActiveLayerCount() const {
        return Vixen::Views::BindEditorLayersModel_activeLayerCountOverride(
            const_cast<int*>(&activeLayerCountRaw_));
    }

private:
    std::vector<Vixen::Views::EditorLayerRow> layers_;
    std::vector<Vixen::Views::EditorParameterRow> parameters_;
    std::vector<Vixen::Views::EditorTransformRow> transform_;
    Rml::String selectedLayerName_;
    int selectedLayer_ = 0;
    std::vector<Vixen::Views::EditorPrimitiveRow> primitives_;
    std::vector<Vixen::Views::EditorLayerOpRow> opChoices_;
    std::vector<Vixen::Views::EditorShapeFieldRow> shape_;
    Rml::String selectedShape_;
    Rml::String shapeNote_;
    int activeLayerCountRaw_ = 0;
    std::vector<Vixen::Views::EditorRecipeRow> recipes_;
    Rml::String statusDocument_;
    Rml::String statusMessage_;
    Rml::String flowState_;
    int undoDepth_ = 0;
    int redoDepth_ = 0;
    Rml::DataModelHandle model_;
};

}  // namespace Vixen::App
