// Inc-A2: the ONLY editor TU (besides EditorLayersView.h's own inline instantiation site) that
// includes EditorLayersView.h / Ui/IView.h / Nodes/UIRenderNode.h (RmlUi's real headers).
// Deliberately does NOT include Recipe/RecipeBaker.h or anything else that transitively pulls
// gaia.h -- see EditorLayersViewBridge.h's file header for the ABI collision this isolation
// prevents (gaia's vendored robin_hood.h is a DIFFERENT VERSION from RmlUi's, sharing one include
// guard).
#include "EditorLayersViewBridge.h"
#include "EditorLayersView.h"

namespace Vixen::App {

EditorLayersView* MakeEditorLayersView() {
    return new EditorLayersView();
}

void DestroyEditorLayersView(EditorLayersView* view) {
    delete view;  // EditorLayersView is complete in this TU, so the real destructor runs here.
}

Vixen::RenderGraph::IView& EditorLayersAsView(EditorLayersView& view) {
    return view;
}

void RefreshEditorLayersView(EditorLayersView& view, uint32_t mask,
                             const std::vector<EditorLayerData>& layers,
                             const std::vector<EditorParameterData>& parameters,
                             const EditorPlacementData& placement,
                             const EditorShapeData& shape) {
    view.PopulateFromDocument(mask, layers, parameters, placement, shape);
}

void RefreshEditorPrimitivesView(EditorLayersView& view, const std::vector<EditorPrimitiveData>& primitives) {
    view.PopulatePrimitives(primitives);
}

std::string EditorPickerFilter(const EditorLayersView& view) { return view.PickerFilter(); }

void ClearEditorPickerFilter(EditorLayersView& view) { view.ClearPickerFilter(); }

void RefreshEditorRecipesView(EditorLayersView& view, const std::vector<EditorRecipeData>& recipes) {
    view.PopulateRecipes(recipes);
}

void RefreshEditorStatusView(EditorLayersView& view, const EditorStatusData& status) {
    view.PopulateStatus(status);
}

}  // namespace Vixen::App
