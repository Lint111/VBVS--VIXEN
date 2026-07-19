#include "graph/BuildingInspectorView.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <cstdio>   // std::snprintf for the number formatting

namespace Vixen::App {

namespace {
// Format one telemetry float with 1 decimal (the sim's flow values are kg/tick-scale; a fixed 1dp
// reads cleanly). Kept host-side so the fragment binds a pre-formatted string (no UI-side arithmetic).
Rml::String Fmt1(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    return Rml::String(buf);
}
}  // namespace

void BuildingInspectorView::Register(Rml::DataModelConstructor& c) {
    c.Bind("defId", &defId_);
    c.Bind("ownerName", &ownerName_);
    c.Bind("isOwnerViewer", &isOwnerViewer_);
    c.Bind("powerDemand", &powerDemand_);
    c.Bind("powerGenerated", &powerGenerated_);
    c.Bind("powerStored", &powerStored_);
    c.Bind("powerNet", &powerNet_);
    c.Bind("powerConnected", &powerConnected_);
    c.Bind("powerImpact", &powerImpact_);
    c.Bind("impactOk", &impactOk_);
    c.Bind("impactThrottled", &impactThrottled_);
    c.Bind("impactHalted", &impactHalted_);
    c.Bind("laborSupply", &laborSupply_);
    c.Bind("laborNeed", &laborNeed_);
    c.Bind("laborNeedMet", &laborNeedMet_);
    model_ = c.GetModelHandle();
}

void BuildingInspectorView::SetBuilding(const BuildingInspectorIn& in) {
    if (!in.present) {
        // No building selected/visible — placeholder. Keeps the fragment mounted (spec §6 gate 3:
        // absent ≠ unmount) while showing nothing meaningful.
        defId_ = "(no building)";
        ownerName_ = "";
        isOwnerViewer_ = 0;
        powerDemand_ = powerGenerated_ = powerStored_ = powerNet_ = "";
        powerConnected_ = "";
        powerImpact_ = "";
        impactOk_ = impactThrottled_ = impactHalted_ = 0;
        laborSupply_ = laborNeed_ = laborNeedMet_ = "";
    } else {
        defId_     = in.defId ? Rml::String(in.defId) : Rml::String{};
        ownerName_ = in.ownerName ? Rml::String(in.ownerName) : Rml::String{};
        isOwnerViewer_ = in.isOwnerViewer ? 1 : 0;

        powerDemand_    = Fmt1(in.powerDemand);
        powerGenerated_ = Fmt1(in.powerGenerated);
        powerStored_    = Fmt1(in.powerStored);
        powerNet_       = Fmt1(in.powerNet);
        powerConnected_ = in.powerConnected ? "yes" : "no";

        // Impact enum → label + the mutually-exclusive class flags the fragment colours by.
        static const char* const kImpact[] = { "Ok", "Throttled", "Halted" };
        const int imp = (in.powerImpact >= 0 && in.powerImpact <= 2) ? in.powerImpact : 0;
        powerImpact_     = kImpact[imp];
        impactOk_        = (imp == 0) ? 1 : 0;
        impactThrottled_ = (imp == 1) ? 1 : 0;
        impactHalted_    = (imp == 2) ? 1 : 0;

        laborSupply_  = Fmt1(in.laborSupply);
        laborNeed_    = Fmt1(in.laborNeed);
        laborNeedMet_ = in.laborNeedMet ? "yes" : "no";
    }

    if (model_) {
        model_.DirtyVariable("defId");
        model_.DirtyVariable("ownerName");
        model_.DirtyVariable("isOwnerViewer");
        model_.DirtyVariable("powerDemand");
        model_.DirtyVariable("powerGenerated");
        model_.DirtyVariable("powerStored");
        model_.DirtyVariable("powerNet");
        model_.DirtyVariable("powerConnected");
        model_.DirtyVariable("powerImpact");
        model_.DirtyVariable("impactOk");
        model_.DirtyVariable("impactThrottled");
        model_.DirtyVariable("impactHalted");
        model_.DirtyVariable("laborSupply");
        model_.DirtyVariable("laborNeed");
        model_.DirtyVariable("laborNeedMet");
    }
}

}  // namespace Vixen::App
