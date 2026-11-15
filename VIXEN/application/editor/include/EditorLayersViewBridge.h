#pragma once
// Inc-A2 (View-Model-Binding-Inc-A2-Plan-2026-07.md): keeps EditorLayersView.h (RmlUi's
// Generated/EditorLayers.g.h -> RmlUi's bundled robin_hood.h) OUT of EditorApplication.cpp, which
// also sees gaia.h transitively (Recipe/RecipeBaker.h -> ShellOctree -> LaineKarrasOctree ->
// ISVOStructure). Mirrors application/main/include/graph/HudViewBridge.h's ODR-isolation seam
// exactly -- same robin_hood v3.11.5-vs-v3.9.0 Table<> layout collision that seam's file header
// documents (confirmed here too: EditorApplication.cpp's own include-order comment, which orders
// the gaia headers before Nodes/UIRenderNode.h, is necessary for gaia's std::hash<> specialisations
// to be visible before robin_hood WRAPS them, but is NOT sufficient once RmlUi's inline data-model
// template code actually instantiates in that TU -- a real build in this TU hit the exact
// "std::hash<T> has no operator()" / ambiguous WrapHash errors HudViewBridge.h predicts. A second,
// gaia-free bridge TU is the only fix, same as HUD's).
//
// EditorLayersViewBridge.cpp (gaia-free) is where EditorLayersView.h / Ui/IView.h /
// Nodes/UIRenderNode.h actually get included and EditorLayersView's inline methods actually
// instantiate. EditorApplication.cpp reaches EditorLayersView ONLY through this bridge.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Vixen::App { class EditorLayersView; }
namespace Vixen::RenderGraph { class IView; }

namespace Vixen::App {

struct EditorLayerData {
    std::string name;
    std::string op;
    bool enabled = true;
    float blendRadius = 0.0f;
    // Stable identity and dependency affordances are appended after the original four fields so
    // existing headless panel fixtures can keep their compact aggregate initialization.
    uint32_t id = 0;
    bool provides = false;
    bool solo = false;
    std::string readsMark;
    std::string readByMark;
    std::string dependencyTitle;
    std::string warningMark;
    std::string warningTitle;
    std::string diagnostic;
    std::string materialName;
    std::string materialColor;
    std::string materialClearId;
};

// E3: the placement of the selected layer, in the order px py pz rx ry rz sx sy sz (rotation in
// degrees). The inspector shows it and its steppers; the field keys live with the view.
// R872.1: the catalogue-driven shape block of the selected layer. Everything here comes from the
// generated recipe-op catalogue through EditorDocumentModel; the view holds no op knowledge.
struct EditorPrimitiveData {
    std::string name;      // the exact opcode name (ids and action params)
    std::string display;   // presentation of the name (split at camel-case boundaries)
    std::string category;
};

struct EditorLayerOpData {
    std::string name;
    bool isCurrent = false;
};

struct EditorShapeFieldData {
    std::string key;     // "<argument>.<component>", the nudge selector's field key
    std::string label;
    float value = 0.0f;
    bool hasRange = false;
    float minimum = 0.0f;
    float maximum = 0.0f;
    float defaultValue = 0.0f;   // the catalogue default of this component
    bool hasDefault = false;
    bool isDefault = false;
};

struct EditorShapeData {
    std::vector<EditorLayerOpData> opChoices;
    std::vector<EditorShapeFieldData> fields;
    std::string primitive;   // empty when the selected layer is not shape-editable
    std::string note;
};

struct EditorNodeData {
    int nodeIndex = 0;
    std::string name;
    std::string category;
    std::string addOp;
    std::string summary;
    bool canRemove = false;
    bool isExpanded = false;
    std::string addReason;
    std::string removeReason;
    std::string removeTitle;
    int indent = 0;
    std::string warning;
    std::string materialName;
    std::string materialColor;
};

struct EditorNodeArgumentData {
    int nodeIndex = 0;
    std::string field;
    std::string component;
    std::string label;
    float value = 0.0f;
    bool hasRange = false;
    float minimum = 0.0f;
    float maximum = 0.0f;
    float defaultValue = 0.0f;
    bool hasDefault = false;
    bool isDefault = false;
    bool isPort = false;
    std::string portType;
    std::string portEmptyReason;
};

struct EditorNodePortChoiceData {
    int nodeIndex = -1;
    std::string argument;
    std::string label;
    std::string elementId;
    std::string removeId;
    bool isSelected = false;
};

struct EditorNodeWrapData {
    int firstNode = 0;
    int lastNode = 0;
    std::string op;
    std::string label;
};

struct EditorNodeChoiceData {
    std::string name;
    std::string display;
    std::string category;
    std::string elementId;
};

struct EditorGraphData {
    std::vector<EditorNodeData> nodes;
    std::vector<EditorNodeArgumentData> arguments;
    std::vector<EditorNodePortChoiceData> portChoices;
    std::vector<EditorNodePortChoiceData> readPortChoices;
    std::vector<EditorNodeWrapData> wrapChoices;
    std::vector<EditorNodeChoiceData> choices;
    bool nodePickerActive = false;
    std::string readPortEmptyReason;
    bool rawProgram = true;
    std::string rawProgramDiagnostic;
};

struct EditorPlacementData {
    int selectedLayer = 0;
    std::string selectedName;
    std::array<float, 9> values{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
};

struct EditorParameterData {
    std::string name;
    std::string unit;
    float value = 0.0f;
    float minimum = 0.0f;
    float maximum = 0.0f;
};

// Returns a raw, owning pointer (not std::unique_ptr<EditorLayersView>) -- same incomplete-type-
// delete rationale as HudViewBridge.h's MakeHudView(): EditorApplication.h forward-declares
// EditorLayersView only, so a unique_ptr's implicit destructor (instantiated at EditorApplication's
// own dtor, in this gaia-touching TU) would need the complete type there. A raw pointer + explicit
// DestroyEditorLayersView() sidesteps that.
// R765: one listed pack render-recipe document.
struct EditorRecipeData {
    std::string id;
    bool isOpen = false;
};

// R872.6: an open-pack recipe that can be inserted from the Add flyout.
struct EditorPackRecipeData {
    std::string id;
};

// R872.10: pack-owned material rows for the future material panel. The current
// lane only publishes data; it intentionally adds no editor control surface.
struct EditorMaterialData {
    std::string id;
    std::string name;
    std::string elementId;
    std::string defaultId;
    std::string color;
    std::string coverage;
    bool coverageStale = false;
};

// R766: the status-line panel's content.
struct EditorStatusData {
    std::string document;
    std::string message;
    std::string flowState;
    uint32_t undoDepth = 0;
    uint32_t redoDepth = 0;
};

EditorLayersView* MakeEditorLayersView();

// Destroys through a complete-type call site (EditorLayersViewBridge.cpp).
void DestroyEditorLayersView(EditorLayersView* view);

// R766: the view as the IView the editor's panel declarations bind (EditorLayersView is an IView;
// this TU-local upcast keeps EditorApplication.cpp from needing its complete type).
Vixen::RenderGraph::IView& EditorLayersAsView(EditorLayersView& view);

// Forwards the headless document's layer and parameter projection into the generated view model.
void RefreshEditorLayersView(EditorLayersView& view, uint32_t mask,
                             const std::vector<EditorLayerData>& layers,
                             const std::vector<EditorParameterData>& parameters,
                             const EditorPlacementData& placement = {},
                             const EditorShapeData& shape = {},
                             const EditorGraphData& graph = {});

// R872.1: the picker rows (static for a build of the catalogue).
void RefreshEditorPrimitivesView(EditorLayersView& view, const std::vector<EditorPrimitiveData>& primitives);

// sn-editorpicker1: the Add flyout's filter text as the picker's text field last wrote it, and clearing it.
std::string EditorPickerFilter(const EditorLayersView& view);
void ClearEditorPickerFilter(EditorLayersView& view);

void RefreshEditorNodeChoicesView(EditorLayersView& view,
                                  const std::vector<EditorNodeChoiceData>& choices,
                                  bool active);

void RefreshEditorRecipesView(EditorLayersView& view, const std::vector<EditorRecipeData>& recipes);
void RefreshEditorPackRecipesView(EditorLayersView& view,
                                  const std::vector<EditorPackRecipeData>& recipes);
void RefreshEditorMaterialsView(EditorLayersView& view,
                                const std::vector<EditorMaterialData>& materials);

void RefreshEditorStatusView(EditorLayersView& view, const EditorStatusData& status);

}  // namespace Vixen::App
