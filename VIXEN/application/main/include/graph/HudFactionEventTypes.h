#pragma once
#include <cstdint>

namespace Vixen::App {

// Host-facing input types for the HUD projection (relocated from UIRenderNode — these are the
// CONSUMER's vocabulary, not the engine's). recentEventAge: ticks since this faction's most recent
// world event (255 = none within K); < kJuiceK -> recentChanged (drives the .changed CSS pulse).
// Split into their own header (no RmlUi dependency) so callers that must NOT see RmlUi's bundled
// robin_hood.h in the same TU as gaia.h's OWN vendored robin_hood.h (an ODR/ABI collision between
// the two DIFFERENT vendored versions — see HudViewBridge.h) can still name these plain structs.
struct HudFactionIn { const char* name; float grievance; bool focused; bool known; bool inLens; uint8_t recentEventAge; };
struct HudEventIn   { const char* kind; int tick; };

}  // namespace Vixen::App
