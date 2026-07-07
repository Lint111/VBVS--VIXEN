#include "graph/HudView.h"

namespace Vixen::App {

void HudView::SetHudView(int tick, int bodyCount, int activeLens, int activeLensCount,
                         std::span<const HudFactionIn> factions, std::span<const HudEventIn> events) {
    tick_      = tick;
    bodyCount_ = bodyCount;
    static const char* const kLensNames[] = { "None", "Intel", "Logistics", "Threat" };
    activeLensName_  = (activeLens >= 0 && activeLens < 4) ? kLensNames[activeLens] : "None";
    activeLensCount_ = activeLensCount;

    static constexpr uint8_t kJuiceK = 20;
    factions_.clear();
    factions_.reserve(factions.size());
    for (const HudFactionIn& f : factions)
        factions_.push_back({ f.name ? Rml::String(f.name) : Rml::String{},
                              f.grievance, f.focused, f.known, f.inLens,
                              f.recentEventAge < kJuiceK });

    events_.clear();
    events_.reserve(events.size());
    for (const HudEventIn& e : events)
        events_.push_back({ e.kind ? Rml::String(e.kind) : Rml::String{}, e.tick });

    if (model_) {
        model_.DirtyVariable("tick");
        model_.DirtyVariable("bodyCount");
        model_.DirtyVariable("activeLensName");
        model_.DirtyVariable("activeLensCount");
        model_.DirtyVariable("factions");
        model_.DirtyVariable("events");
    }
}

}  // namespace Vixen::App
