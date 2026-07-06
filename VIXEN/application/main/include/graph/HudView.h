#pragma once
#include "Ui/IView.h"
#include "Generated/Hud.g.h"   // Vixen::Views::{HudFaction,HudEvent,HudBind,BindHudModel}
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>  // Rml::String
#include <span>
#include <vector>
#include <cstdint>

namespace Vixen::App {

// Host-facing input types for the HUD projection (relocated from UIRenderNode — these are the
// CONSUMER's vocabulary, not the engine's). recentEventAge: ticks since this faction's most recent
// world event (255 = none within K); < kJuiceK -> recentChanged (drives the .changed CSS pulse).
struct HudFactionIn { const char* name; float grievance; bool focused; bool known; bool inLens; uint8_t recentEventAge; };
struct HudEventIn   { const char* kind; int tick; };

// The main app's HUD view. Owns its storage, registers the "hud" data model via the generated
// BindHudModel, and projects sim/host data into it. This is VIXEN's own native consumer of the
// renderer-agnostic view contract (Inc-2).
class HudView final : public Vixen::RenderGraph::IView {
public:
    const char* ModelName() const override { return "hud"; }
    const char* DocumentPath() const override { return "assets/ui/hud.rml"; }
    void Register(Rml::DataModelConstructor& c) override {
        Vixen::Views::BindHudModel(c, Vixen::Views::HudBind{
            &tick_, &bodyCount_, &activeLensName_, &activeLensCount_, &factions_, &events_ });
        model_ = c.GetModelHandle();
    }
    // The relocated SetHudView projection (was UIRenderNode::SetHudView). Copies inputs into the
    // bound storage, applies the lens-name + juice projection, and dirties the vars.
    void SetHudView(int tick, int bodyCount, int activeLens, int activeLensCount,
                    std::span<const HudFactionIn> factions, std::span<const HudEventIn> events);

    // Debug accessors for the unit test.
    int DebugTick() const { return tick_; }
    const char* DebugLensName() const { return activeLensName_.c_str(); }
    bool DebugFactionRecentChanged(size_t i) const { return factions_.at(i).recentChanged; }

private:
    int tick_ = 0;
    int bodyCount_ = 0;
    Rml::String activeLensName_ = "None";
    int activeLensCount_ = 0;
    std::vector<Vixen::Views::HudFaction> factions_;
    std::vector<Vixen::Views::HudEvent>   events_;
    Rml::DataModelHandle model_;
};

}  // namespace Vixen::App
