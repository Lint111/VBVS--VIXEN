#pragma once
#include "Ui/IView.h"
#include "Generated/Hud.g.h"   // Vixen::Views::{HudFaction,HudEvent,HudBind,BindHudModel}
#include "graph/HudFactionEventTypes.h"  // HudFactionIn/HudEventIn (kept RmlUi-free)
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>  // Rml::String
#include <span>
#include <vector>
#include <cstdint>

namespace Vixen::App {

// The main app's HUD view. Owns its storage, registers the "hud" data model via the generated
// BindHudModel, and projects sim/host data into it. This is VIXEN's own native consumer of the
// renderer-agnostic view contract (Inc-2).
class HudView final : public Vixen::RenderGraph::IView {
public:
    const char* ModelName() const override { return "hud"; }
    const char* DocumentPath() const override { return "assets/ui/hud.rml"; }
    void Register(Rml::DataModelConstructor& c) override {
        Vixen::Views::BindHudModel(c, Vixen::Views::HudBind{
            &tick_, &bodyCount_, &activeLensName_, &activeLensCount_, &factions_, &events_,
            &inspectSelected_, &inspectName_, &inspectCause_ });
        // speed is HOST state (RealtimeSimDriver.Speed), not a sim/SimFrame field, so it is bound as
        // an EXTRA "hud"-model variable here rather than added to the generated Hud [View] schema —
        // keeping the pace out of the sim while still surfacing it on the clock line ({{speed}}).
        c.Bind("speed", &speed_);
        model_ = c.GetModelHandle();
    }
    // The relocated SetHudView projection (was UIRenderNode::SetHudView). Copies inputs into the
    // bound storage, applies the lens-name + juice projection, and dirties the vars.
    void SetHudView(int tick, int bodyCount, int activeLens, int activeLensCount,
                    std::span<const HudFactionIn> factions, std::span<const HudEventIn> events);
    // T1 inspect panel: set the selected-entity detail (gates the panel's data-if in hud.rml).
    // Kept separate from SetHudView so the engine's own demo HUD (which has no inspect source)
    // need not supply it — undertow pushes it each frame, the demo leaves it cleared.
    void SetHudInspect(bool selected, const char* name, const char* cause);

    // Host sim-speed readout: formats the multiplier as "×N" (e.g. "×1", "×0.5", "×25") into the
    // bound "speed" var and dirties it. Kept separate from SetHudView (like SetHudInspect) so the
    // engine demo HUD need not supply a pace; undertow pushes it each frame from ut_speed.
    void SetSpeed(double speed);

    // Debug accessors for the unit test.
    int DebugTick() const { return tick_; }
    const char* DebugLensName() const { return activeLensName_.c_str(); }
    bool DebugFactionRecentChanged(size_t i) const { return factions_.at(i).recentChanged; }

    // Additional read-only debug accessors (View Contract Inc-2b Task 9: proof-gate hash
    // equivalence vs the reflection-blob path) — mirror the accessors above, no behavior change.
    int DebugBodyCount() const { return bodyCount_; }
    int DebugActiveLensCount() const { return activeLensCount_; }
    size_t DebugFactionCount() const { return factions_.size(); }
    const Vixen::Views::HudFaction& DebugFaction(size_t i) const { return factions_.at(i); }
    size_t DebugEventCount() const { return events_.size(); }
    const Vixen::Views::HudEvent& DebugEvent(size_t i) const { return events_.at(i); }

private:
    int tick_ = 0;
    int bodyCount_ = 0;
    Rml::String activeLensName_ = "None";
    int activeLensCount_ = 0;
    std::vector<Vixen::Views::HudFaction> factions_;
    std::vector<Vixen::Views::HudEvent>   events_;
    int         inspectSelected_ = 0;
    Rml::String inspectName_;
    Rml::String inspectCause_;
    Rml::String speed_ = "\xC3\x97" "1";   // "×1" (UTF-8 multiply sign) — host pace; updated by SetSpeed.
    Rml::DataModelHandle model_;
};

}  // namespace Vixen::App
