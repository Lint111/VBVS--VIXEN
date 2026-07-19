#pragma once
// The relational vertical slice's building-inspector view (step-6 M-ui). A SECOND IView (data model
// "building", document assets/ui/building_inspector.rml) mounted beside the HUD via the UIRenderNode's
// IUiCompositionHost. Owns its bound storage and projects the three building-relation View sections
// (facets / power / labor) for the SELECTED building into pre-formatted strings the fragment binds.
//
// RmlUi-touching (Rml::String storage), so — exactly like HudView — this header is only ever included
// by a gaia-FREE bridge TU (BuildingInspectorBridge.cpp), never by a TU that also sees gaia.h (the
// robin_hood ODR collision; see HudViewBridge.h's file header for the full rationale).
#include "Ui/IView.h"
#include "graph/BuildingInspectorBridge.h"  // BuildingInspectorIn (RmlUi-free input struct)
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>  // Rml::String
#include <cstdint>

namespace Vixen::App {

class BuildingInspectorView final : public Vixen::RenderGraph::IView {
public:
    const char* ModelName() const override { return "building"; }
    const char* DocumentPath() const override { return "assets/ui/building_inspector.rml"; }
    void Register(Rml::DataModelConstructor& c) override;

    // Copy the selected building's channels into the bound storage, apply the display transforms
    // (impact label, unit formatting, "you own this" gating), and dirty the vars. Called each frame
    // the building section is dirty (or the selection changed).
    void SetBuilding(const BuildingInspectorIn& in);

    // Debug accessors for the mount/unmount + binding unit test.
    const char* DebugDefId() const { return defId_.c_str(); }
    const char* DebugPowerImpact() const { return powerImpact_.c_str(); }
    bool DebugIsOwnerViewer() const { return isOwnerViewer_ != 0; }

private:
    // Bound storage — pre-formatted strings so the fragment carries zero logic.
    Rml::String defId_ = "(no building)";
    Rml::String ownerName_;
    int         isOwnerViewer_ = 0;
    Rml::String powerDemand_;
    Rml::String powerGenerated_;
    Rml::String powerStored_;
    Rml::String powerNet_;
    Rml::String powerConnected_;
    Rml::String powerImpact_;
    int         impactOk_ = 0;
    int         impactThrottled_ = 0;
    int         impactHalted_ = 0;
    Rml::String laborSupply_;
    Rml::String laborNeed_;
    Rml::String laborNeedMet_;
    Rml::DataModelHandle model_;
};

}  // namespace Vixen::App
