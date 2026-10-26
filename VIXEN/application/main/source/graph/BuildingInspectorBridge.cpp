// The ONLY app TU (besides BuildingInspectorView.cpp) that includes BuildingInspectorView.h /
// Nodes/UIRenderNode.h (RmlUi's real headers). Deliberately gaia-free — see BuildingInspectorBridge.h
// / HudViewBridge.h for the robin_hood ODR-collision this isolation prevents.
#include "graph/BuildingInspectorBridge.h"
#include "graph/BuildingInspectorView.h"

namespace Vixen::App {

BuildingInspectorView* MakeBuildingInspectorView() {
    return new BuildingInspectorView();
}

void DestroyBuildingInspectorView(BuildingInspectorView* view) {
    delete view;  // complete type in this TU
}

Vixen::RenderGraph::IView& BuildingInspectorAsView(BuildingInspectorView& view) {
    return view;
}

void PushBuildingInspector(BuildingInspectorView& view, const BuildingInspectorIn& in) {
    view.SetBuilding(in);
}

}  // namespace Vixen::App
