#pragma once
#include "EditorLayersViewBridge.h"
#include "Ui/IView.h"
#include "Generated/EditorLayers.g.h"   // Vixen::Views::{EditorLayerRow,EditorLayersBind,BindEditorLayersModel}
#include <RmlUi/Core/DataModelHandle.h>
#include <bit>
#include <charconv>
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
                                            &selectedShape_, &shapeNote_, &pickerFilter_, &activeLayerCountRaw_, &recipes_,
                                            &packRecipes_, &materials_, &nodes_, &nodeArguments_, &portChoices_,
                                            &readPortChoices_, &nodeWrapChoices_, &nodeChoices_, &nodePickerActive_,
                                            &readPortEmptyReason_,
                                            &rawProgram_, &rawProgramDiagnostic_,
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
                              const EditorShapeData& shape = {},
                              const EditorGraphData& graph = {}) {
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
            row.blendValueId = "layer-" + std::to_string(i) + "-blend-{radius}";
            row.isSelected = static_cast<int>(i) == placement.selectedLayer;
            row.providesMark = layerData[i].provides ? "P" : "";
            row.readsMark = Rml::String(layerData[i].readsMark);
            row.readByMark = Rml::String(layerData[i].readByMark);
            row.dependencyTitle = Rml::String(layerData[i].dependencyTitle);
            row.provideId = "layer-" + std::to_string(layerData[i].id) + "-provide-" +
                            (layerData[i].provides ? "0" : "1");
            row.soloId = "layer-" + std::to_string(layerData[i].id) + "-solo";
            row.isSolo = layerData[i].solo;
            row.warningMark = Rml::String(layerData[i].warningMark);
            row.warningTitle = Rml::String(layerData[i].warningTitle);
            row.diagnostic = Rml::String(layerData[i].diagnostic);
            row.materialName = Rml::String(layerData[i].materialName);
            row.materialColor = Rml::String(layerData[i].materialColor);
            row.materialClearId = Rml::String(layerData[i].materialClearId);
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
            row.setId = "parameter-" + std::to_string(i) + "-set-{value}";
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
                row.valueId = "layer-" + std::to_string(placement.selectedLayer) + "-placement-" +
                              kPlacementKeys[f] + "-{" + kPlacementKeys[f] + "}";
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
                // "<argument>.<component>" -> "layer-<i>-field-<argument>-<component>-{<component>}".
                const size_t dot = field.key.find('.');
                const std::string argument = dot == std::string::npos ? field.key : field.key.substr(0, dot);
                const std::string component = dot == std::string::npos ? std::string("x") : field.key.substr(dot + 1);
                row.valueId = layerStem + "-field-" + argument + "-" + component + "-{" + component + "}";
                row.defaultValue = field.defaultValue;
                row.hasDefault = field.hasDefault;
                row.isDefault = field.isDefault;
                // The reset button is the typed-value selector with the default filled in (one SetLayerPrimitiveField).
                row.resetId = field.hasDefault
                    ? layerStem + "-field-" + argument + "-" + component + "-" + FloatText(field.defaultValue)
                    : std::string();
                shape_.push_back(std::move(row));
            }
        }
        selectedShape_ = Rml::String(shape.primitive);
        shapeNote_ = Rml::String(shape.note);
        nodes_.clear();
        nodeArguments_.clear();
        portChoices_.clear();
        readPortChoices_.clear();
        nodeWrapChoices_.clear();
        if (!layerData.empty()) {
            const std::string layerStem = "layer-" + std::to_string(placement.selectedLayer);
            nodes_.reserve(graph.nodes.size());
            for (const auto& node : graph.nodes) {
                Vixen::Views::EditorNodeRow row;
                row.nodeIndex = node.nodeIndex;
                row.name = Rml::String(node.name);
                row.category = Rml::String(node.category);
                row.addOp = Rml::String(node.addOp);
                row.summary = Rml::String(node.summary);
                row.elementId = layerStem + "-node-" + std::to_string(node.nodeIndex);
                row.addId = "panel-node-picker-" + std::to_string(placement.selectedLayer) + "-" +
                            std::to_string(node.nodeIndex) + "-open";
                row.removeId = row.elementId + "-remove";
                row.toggleId = row.elementId + "-expand";
                row.canRemove = node.canRemove;
                row.isExpanded = node.isExpanded;
                row.addReason = Rml::String(node.addReason);
                row.removeReason = Rml::String(node.removeReason);
                row.removeTitle = Rml::String(node.removeTitle);
                row.indent = node.indent;
                row.warning = Rml::String(node.warning);
                row.materialName = Rml::String(node.materialName);
                row.materialColor = Rml::String(node.materialColor);
                nodes_.push_back(std::move(row));
            }
            nodeArguments_.reserve(graph.arguments.size());
            for (const auto& argument : graph.arguments) {
                Vixen::Views::EditorNodeArgumentRow row;
                row.nodeIndex = argument.nodeIndex;
                row.field = Rml::String(argument.field);
                row.component = Rml::String(argument.component);
                row.label = Rml::String(argument.label);
                row.value = argument.value;
                row.hasRange = argument.hasRange;
                row.minimum = argument.minimum;
                row.maximum = argument.maximum;
                row.defaultValue = argument.defaultValue;
                row.hasDefault = argument.hasDefault;
                row.isDefault = argument.isDefault;
                const std::string stem = layerStem + "-node-" + std::to_string(argument.nodeIndex) +
                                         "-field-" + argument.field;
                row.upId = stem + "-" + argument.component + "-up";
                row.downId = stem + "-" + argument.component + "-down";
                row.valueId = stem + "-" + argument.component + "-{" + argument.component + "}";
                row.resetId = argument.hasDefault
                    ? stem + "-" + argument.component + "-" + FloatText(argument.defaultValue)
                    : std::string();
                row.isPort = argument.isPort;
                row.portType = Rml::String(argument.portType);
                row.portEmptyReason = Rml::String(argument.portEmptyReason);
                nodeArguments_.push_back(std::move(row));
            }
            portChoices_.reserve(graph.portChoices.size());
            for (const auto& choice : graph.portChoices) {
                Vixen::Views::EditorNodePortChoiceRow row;
                row.nodeIndex = choice.nodeIndex;
                row.argument = Rml::String(choice.argument);
                row.label = Rml::String(choice.label);
                row.elementId = Rml::String(choice.elementId);
                row.removeId = Rml::String(choice.removeId);
                row.isSelected = choice.isSelected;
                portChoices_.push_back(std::move(row));
            }
            readPortChoices_.reserve(graph.readPortChoices.size());
            for (const auto& choice : graph.readPortChoices) {
                Vixen::Views::EditorReadPortChoiceRow row;
                row.nodeIndex = choice.nodeIndex;
                row.argument = Rml::String(choice.argument);
                row.label = Rml::String(choice.label);
                row.elementId = Rml::String(choice.elementId);
                row.removeId = Rml::String(choice.removeId);
                row.isSelected = choice.isSelected;
                readPortChoices_.push_back(std::move(row));
            }
            nodeWrapChoices_.reserve(graph.wrapChoices.size());
            for (const auto& choice : graph.wrapChoices) {
                Vixen::Views::EditorNodeWrapRow row;
                row.firstNode = choice.firstNode;
                row.lastNode = choice.lastNode;
                row.op = Rml::String(choice.op);
                row.label = Rml::String(choice.label);
                row.elementId = layerStem + "-nodes-" + std::to_string(choice.firstNode) + "-" +
                                std::to_string(choice.lastNode) + "-wrap-" + choice.op;
                nodeWrapChoices_.push_back(std::move(row));
            }
        }
        rawProgram_ = graph.rawProgram;
        rawProgramDiagnostic_ = Rml::String(graph.rawProgramDiagnostic);
        readPortEmptyReason_ = Rml::String(graph.readPortEmptyReason);
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
            model_.DirtyVariable("nodes");
            model_.DirtyVariable("nodeArguments");
            model_.DirtyVariable("portChoices");
            model_.DirtyVariable("readPortChoices");
            model_.DirtyVariable("nodeWrapChoices");
            model_.DirtyVariable("nodeChoices");
            model_.DirtyVariable("nodePickerActive");
            model_.DirtyVariable("readPortEmptyReason");
            model_.DirtyVariable("rawProgram");
            model_.DirtyVariable("rawProgramDiagnostic");
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
            row.display = Rml::String(primitive.display);
            row.category = Rml::String(primitive.category);
            row.elementId = "primitive-" + primitive.name + "-add";
            primitives_.push_back(std::move(row));
        }
        if (model_) model_.DirtyVariable("primitives");
    }

    void PopulateNodeChoices(const std::vector<EditorNodeChoiceData>& choices, bool active) {
        nodeChoices_.clear();
        nodeChoices_.reserve(choices.size());
        for (const auto& choice : choices) {
            Vixen::Views::EditorNodeChoiceRow row;
            row.name = Rml::String(choice.name);
            row.display = Rml::String(choice.display);
            row.category = Rml::String(choice.category);
            // InsertNodeAfter's generated trigger owns the full selector; the opcode is kept in the
            // choice only for presentation and filtering.
            row.elementId = choice.elementId;
            nodeChoices_.push_back(std::move(row));
        }
        nodePickerActive_ = active;
        if (model_) {
            model_.DirtyVariable("nodeChoices");
            model_.DirtyVariable("nodePickerActive");
        }
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

    // R872.6: the Add flyout's second section, using the same filter publication cadence as primitives.
    void PopulatePackRecipes(const std::vector<EditorPackRecipeData>& recipeData) {
        packRecipes_.clear();
        packRecipes_.reserve(recipeData.size());
        for (const auto& recipe : recipeData) {
            Vixen::Views::EditorPackRecipeRow row;
            row.id = Rml::String(recipe.id);
            row.elementId = "recipe-ref-" + recipe.id + "-add";
            packRecipes_.push_back(std::move(row));
        }
        if (model_) model_.DirtyVariable("packRecipes");
    }

    // R872.10: expose pack material rows as data for the later material-panel
    // lane. There is intentionally no panel/action wiring here.
    void PopulateMaterials(const std::vector<EditorMaterialData>& materialData) {
        materials_.clear();
        materials_.reserve(materialData.size());
        for (const auto& material : materialData) {
            Vixen::Views::EditorMaterialRow row;
            row.id = Rml::String(material.id);
            row.name = Rml::String(material.name);
            row.elementId = Rml::String(material.elementId);
            row.defaultId = Rml::String(material.defaultId);
            row.color = Rml::String(material.color);
            row.coverage = Rml::String(material.coverage);
            materials_.push_back(std::move(row));
        }
        if (model_) model_.DirtyVariable("materials");
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

    // sn-editorpicker1: the Add flyout's filter text. The picker's text field writes `pickerFilter_` through the
    // data binding as the user types; the editor reads it each frame and republishes the primitive rows.
    std::string PickerFilter() const { return std::string(pickerFilter_); }
    void ClearPickerFilter() {
        if (pickerFilter_.empty()) return;
        pickerFilter_.clear();
        if (model_) model_.DirtyVariable("pickerFilter");
    }
    // Test hook: what the document's text field does on a keystroke.
    void DebugSetPickerFilter(const std::string& text) { pickerFilter_ = Rml::String(text); }

    // Debug accessor for tests.
    size_t DebugLayerCount() const { return layers_.size(); }
    const Vixen::Views::EditorLayerRow& DebugLayer(size_t i) const { return layers_.at(i); }
    size_t DebugParameterCount() const { return parameters_.size(); }
    size_t DebugMaterialCount() const { return materials_.size(); }
    const Vixen::Views::EditorMaterialRow& DebugMaterial(size_t i) const { return materials_.at(i); }
    size_t DebugPrimitiveCount() const { return primitives_.size(); }
    const Vixen::Views::EditorPrimitiveRow& DebugPrimitive(size_t i) const { return primitives_.at(i); }
    size_t DebugOpChoiceCount() const { return opChoices_.size(); }
    const Vixen::Views::EditorLayerOpRow& DebugOpChoice(size_t i) const { return opChoices_.at(i); }
    size_t DebugShapeFieldCount() const { return shape_.size(); }
    const Vixen::Views::EditorShapeFieldRow& DebugShapeField(size_t i) const { return shape_.at(i); }
    size_t DebugNodeCount() const { return nodes_.size(); }
    const Vixen::Views::EditorNodeRow& DebugNode(size_t i) const { return nodes_.at(i); }
    size_t DebugNodeChoiceCount() const { return nodeChoices_.size(); }
    int DebugActiveLayerCount() const {
        return Vixen::Views::BindEditorLayersModel_activeLayerCountOverride(
            const_cast<int*>(&activeLayerCountRaw_));
    }

private:
    // Shortest text that parses back to the same float (the reset selector's value slot).
    static std::string FloatText(float value) {
        char buffer[32];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, result.ptr);
    }

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
    Rml::String pickerFilter_;
    int activeLayerCountRaw_ = 0;
    std::vector<Vixen::Views::EditorRecipeRow> recipes_;
    std::vector<Vixen::Views::EditorPackRecipeRow> packRecipes_;
    std::vector<Vixen::Views::EditorMaterialRow> materials_;
    std::vector<Vixen::Views::EditorNodeRow> nodes_;
    std::vector<Vixen::Views::EditorNodeArgumentRow> nodeArguments_;
    std::vector<Vixen::Views::EditorNodePortChoiceRow> portChoices_;
    std::vector<Vixen::Views::EditorReadPortChoiceRow> readPortChoices_;
    std::vector<Vixen::Views::EditorNodeWrapRow> nodeWrapChoices_;
    std::vector<Vixen::Views::EditorNodeChoiceRow> nodeChoices_;
    bool nodePickerActive_ = false;
    Rml::String readPortEmptyReason_;
    bool rawProgram_ = true;
    Rml::String rawProgramDiagnostic_;
    Rml::String statusDocument_;
    Rml::String statusMessage_;
    Rml::String flowState_;
    int undoDepth_ = 0;
    int redoDepth_ = 0;
    Rml::DataModelHandle model_;
};

}  // namespace Vixen::App
