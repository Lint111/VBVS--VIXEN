// View Contract Inc-2 Task 5 fix: the ONLY app TU (besides HudView.cpp itself) that includes
// HudView.h / Ui/IView.h / Nodes/UIRenderNode.h (RmlUi's real headers). Deliberately does NOT
// include BodyOctreeSceneNode.h or anything else that transitively pulls gaia.h -- see
// HudViewBridge.h's file header for the ABI-collision this isolation prevents (gaia's vendored
// robin_hood.h is a DIFFERENT VERSION from RmlUi's, sharing one include guard).
#include "graph/HudViewBridge.h"
#include "graph/HudView.h"
#include "Nodes/UIRenderNode.h"

namespace Vixen::App {

HudView* MakeHudView() {
    return new HudView();
}

void DestroyHudView(HudView* view) {
    delete view;  // HudView is complete in this TU, so the real destructor runs here.
}

void WireHudView(Vixen::RenderGraph::UIRenderNode& node, HudView& view) {
    // Non-owning aliased shared_ptr -- view is owned by the caller (VulkanGraphApplication's
    // hudView_ member), which already outlives the graph/node it's wired into, so there is no
    // real ownership to transfer; the no-op deleter documents that SetView's shared_ptr contract
    // is satisfied without a second owner.
    node.SetView(std::shared_ptr<Vixen::RenderGraph::IView>(&view, [](Vixen::RenderGraph::IView*) {}));
}

void PushHudView(HudView& view, int tick, int bodyCount, int activeLens, int activeLensCount,
                 std::span<const HudFactionIn> factions, std::span<const HudEventIn> events) {
    view.SetHudView(tick, bodyCount, activeLens, activeLensCount, factions, events);
}

}  // namespace Vixen::App
