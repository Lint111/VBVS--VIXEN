// View Contract Inc-2 Task 5 fix: the ONLY app TU (besides HudView.cpp itself) that includes
// HudView.h / Ui/IView.h / Nodes/UIRenderNode.h (RmlUi's real headers). Deliberately does NOT
// include BodyOctreeSceneNode.h or anything else that transitively pulls gaia.h -- see
// HudViewBridge.h's file header for the ABI-collision this isolation prevents (gaia's vendored
// robin_hood.h is a DIFFERENT VERSION from RmlUi's, sharing one include guard).
#include "graph/HudViewBridge.h"
#include "graph/HudView.h"

namespace Vixen::App {

HudView* MakeHudView() {
    return new HudView();
}

void DestroyHudView(HudView* view) {
    delete view;  // HudView is complete in this TU, so the real destructor runs here.
}

Vixen::RenderGraph::IView& HudAsView(HudView& view) {
    return view;
}

void PushHudView(HudView& view, int tick, int bodyCount, int activeLens, int activeLensCount,
                 std::span<const HudFactionIn> factions, std::span<const HudEventIn> events) {
    view.SetHudView(tick, bodyCount, activeLens, activeLensCount, factions, events);
}

void PushHudInspect(HudView& view, bool selected, const char* name, const char* cause,
                    float maxGrievance, float strength, const char* topRelName, float topRelSig) {
    view.SetHudInspect(selected, name, cause, maxGrievance, strength, topRelName, topRelSig);
}

void PushHudSpeed(HudView& view, double speed) {
    view.SetSpeed(speed);
}

}  // namespace Vixen::App
