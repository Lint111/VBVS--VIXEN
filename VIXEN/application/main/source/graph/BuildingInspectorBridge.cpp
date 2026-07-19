// The ONLY app TU (besides BuildingInspectorView.cpp) that includes BuildingInspectorView.h /
// Nodes/UIRenderNode.h (RmlUi's real headers). Deliberately gaia-free — see BuildingInspectorBridge.h
// / HudViewBridge.h for the robin_hood ODR-collision this isolation prevents.
#include "graph/BuildingInspectorBridge.h"
#include "graph/BuildingInspectorView.h"
#include "Nodes/UIRenderNode.h"

namespace Vixen::App {

BuildingInspectorView* MakeBuildingInspectorView() {
    return new BuildingInspectorView();
}

void DestroyBuildingInspectorView(BuildingInspectorView* view) {
    delete view;  // complete type in this TU
}

uint32_t MountBuildingInspector(Vixen::RenderGraph::UIRenderNode& node, BuildingInspectorView& view) {
    // Mount as a second document via the IUiCompositionHost seam. The view is owned by the caller
    // (VulkanGraphApplication's buildingInspectorView_), which outlives the graph — same non-owning
    // contract as WireHudView; Mount stores a borrowed pointer.
    return node.Mount(view);
}

void PushBuildingInspector(BuildingInspectorView& view, const BuildingInspectorIn& in) {
    view.SetBuilding(in);
}

}  // namespace Vixen::App
