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
#include <cstdint>
#include <string>
#include <vector>

namespace Vixen::App { class EditorLayersView; }
namespace Vixen::RenderGraph { class UIRenderNode; }

namespace Vixen::App {

// Returns a raw, owning pointer (not std::unique_ptr<EditorLayersView>) -- same incomplete-type-
// delete rationale as HudViewBridge.h's MakeHudView(): EditorApplication.h forward-declares
// EditorLayersView only, so a unique_ptr's implicit destructor (instantiated at EditorApplication's
// own dtor, in this gaia-touching TU) would need the complete type there. A raw pointer + explicit
// DestroyEditorLayersView() sidesteps that.
EditorLayersView* MakeEditorLayersView();

// Destroys through a complete-type call site (EditorLayersViewBridge.cpp).
void DestroyEditorLayersView(EditorLayersView* view);

// Wires view onto node via UIRenderNode::SetView (non-owning aliased shared_ptr -- view is owned
// by the caller, e.g. EditorApplication::layersView_, which outlives the graph/node it's wired into).
void WireEditorLayersView(Vixen::RenderGraph::UIRenderNode& node, EditorLayersView& view);

// Forwards to EditorLayersView::PopulateFromMask (the mask/names/ops -> bound "layers" projection).
void RefreshEditorLayersView(EditorLayersView& view, uint32_t mask, uint32_t layerCount,
                             const std::vector<std::string>& names, const std::vector<std::string>& ops);

}  // namespace Vixen::App
