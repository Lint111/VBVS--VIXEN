// Inc-A2: the ONLY editor TU (besides EditorLayersView.h's own inline instantiation site) that
// includes EditorLayersView.h / Ui/IView.h / Nodes/UIRenderNode.h (RmlUi's real headers).
// Deliberately does NOT include Recipe/RecipeBaker.h or anything else that transitively pulls
// gaia.h -- see EditorLayersViewBridge.h's file header for the ABI collision this isolation
// prevents (gaia's vendored robin_hood.h is a DIFFERENT VERSION from RmlUi's, sharing one include
// guard).
#include "EditorLayersViewBridge.h"
#include "EditorLayersView.h"
#include "Nodes/UIRenderNode.h"

namespace Vixen::App {

EditorLayersView* MakeEditorLayersView() {
    return new EditorLayersView();
}

void DestroyEditorLayersView(EditorLayersView* view) {
    delete view;  // EditorLayersView is complete in this TU, so the real destructor runs here.
}

void WireEditorLayersView(Vixen::RenderGraph::UIRenderNode& node, EditorLayersView& view) {
    // Non-owning aliased shared_ptr -- view is owned by the caller (EditorApplication's
    // layersView_ member), which already outlives the graph/node it is wired into, so there is no
    // real ownership to transfer; the no-op deleter documents that SetView's shared_ptr contract
    // is satisfied without a second owner. Mirrors HudViewBridge.cpp's WireHudView.
    node.SetView(std::shared_ptr<Vixen::RenderGraph::IView>(&view, [](Vixen::RenderGraph::IView*) {}));
}

void RefreshEditorLayersView(EditorLayersView& view, uint32_t mask, uint32_t layerCount,
                             const std::vector<std::string>& names, const std::vector<std::string>& ops) {
    view.PopulateFromMask(mask, layerCount, names, ops);
}

}  // namespace Vixen::App
