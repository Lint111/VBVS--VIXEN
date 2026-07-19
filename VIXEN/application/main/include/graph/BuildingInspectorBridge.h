#pragma once
// Gaia-free bridge for the building-inspector view (step-6 M-ui), mirroring HudViewBridge.h exactly.
// BuildingInspectorView.h pulls in RmlUi's vendored robin_hood.h; gaia vendors a DIFFERENT-version
// copy under the same include guard, so the two must never share a TU. This bridge is the ONLY seam
// the gaia-touching VulkanGraphApplication.cpp uses to reach BuildingInspectorView — it never
// #includes BuildingInspectorView.h itself. BuildingInspectorBridge.cpp (gaia-free) is where that
// header + Nodes/UIRenderNode.h actually get included. See HudViewBridge.h for the full rationale.
#include <cstdint>

namespace Vixen::App { class BuildingInspectorView; }
namespace Vixen::RenderGraph { class UIRenderNode; }

namespace Vixen::App {

// Plain-struct input the host pushes each dirty frame — the already-decoded, already-selected
// building's three-channel data. RmlUi-free (defined here, not in BuildingInspectorView.h) so the
// gaia-touching host TU can name it — same split as HudFactionIn/HudEventIn in HudFactionEventTypes.h.
// Every value is authoritative sim output read off the wire; the view only FORMATS it (spec §7.10).
struct BuildingInspectorIn {
    bool        present = false;   // false = no building selected/visible → the fragment shows a placeholder
    const char* defId = "";        // building definition id (e.g. "core:power_plant")
    const char* ownerName = "";    // owner faction display label
    bool        isOwnerViewer = false;
    // Power channel (BuildingPower row)
    float       powerDemand = 0.f;
    float       powerGenerated = 0.f;
    float       powerStored = 0.f;
    float       powerNet = 0.f;
    bool        powerConnected = false;
    int         powerImpact = 0;   // 0=Ok, 1=Throttled, 2=Halted (BuildingImpact)
    // Labor channel (BuildingLabor row)
    float       laborSupply = 0.f;
    float       laborNeed = 0.f;
    bool        laborNeedMet = false;
};

// Raw owning pointer (not unique_ptr) for the same incomplete-type-delete reason HudViewBridge gives:
// the owner (VulkanGraphApplication) forward-declares BuildingInspectorView.
BuildingInspectorView* MakeBuildingInspectorView();
void DestroyBuildingInspectorView(BuildingInspectorView* view);

// Mount `view` onto `node` via UIRenderNode::Mount (the IUiCompositionHost seam). Returns the mount
// handle (0 on failure — namespace collision / degenerate layout / load failure, all logged by the host).
uint32_t MountBuildingInspector(Vixen::RenderGraph::UIRenderNode& node, BuildingInspectorView& view);

// Forwards to BuildingInspectorView::SetBuilding (which dirties its own bound model, so the pushed
// values re-render — no host round-trip needed).
void PushBuildingInspector(BuildingInspectorView& view, const BuildingInspectorIn& in);

}  // namespace Vixen::App
