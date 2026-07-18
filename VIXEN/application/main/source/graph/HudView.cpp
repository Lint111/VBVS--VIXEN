#include "graph/HudView.h"

#include <cstdio>   // std::snprintf for the "×N" speed formatting

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

void HudView::SetSpeed(double speed) {
    // Format the ladder multipliers cleanly: "×0.5", "×1", "×2", "×25" (no trailing ".0" / zeros).
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", speed);
    speed_ = Rml::String("\xC3\x97") + buf;   // U+00D7 MULTIPLICATION SIGN (UTF-8) + the number
    if (model_) model_.DirtyVariable("speed");
}

void HudView::SetHudInspect(bool selected, const char* name, const char* cause) {
    inspectSelected_ = selected ? 1 : 0;
    inspectName_  = name  ? Rml::String(name)  : Rml::String{};
    inspectCause_ = cause ? Rml::String(cause) : Rml::String{};
    if (model_) {
        model_.DirtyVariable("inspectSelected");
        model_.DirtyVariable("inspectName");
        model_.DirtyVariable("inspectCause");
    }
}

}  // namespace Vixen::App
