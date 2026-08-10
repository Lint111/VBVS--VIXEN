#pragma once

#include "FootprintRegime.h"

#include <cstdint>

namespace Vixen::SVO {

// CPU-owned S1 admission ledger. Payload bits are intentionally explicit: channelPool
// and brickLookup are one atomic fine-data admission; the remaining bits are reserved
// for later slices without changing the shader-visible mask contract.
enum class WholesalePayload : uint32_t {
    ChannelPool = 1u << 0,
    BrickLookup = 1u << 1,
};

struct WholesaleAvailability {
    FootprintRegime desiredRegime = FootprintRegime::MipHit;
    FootprintRegime committedRegime = FootprintRegime::MipHit;
    uint32_t generation = 0;
    uint32_t pendingMask = 0;
    uint32_t readyMask = 0;
    uint32_t surfaceFrames = 0;
    uint32_t nonSurfaceFrames = 0;
};

// Apply the frozen hysteresis: two consecutive Surface classifications promote, while
// four consecutive non-Surface classifications demote. A transition clears readiness
// before any payload is reused, so stale retained bytes are never shader-readable.
inline bool AdvanceWholesaleAvailability(WholesaleAvailability& state,
                                         FootprintRegime classified,
                                         uint32_t payloadMask) {
    const bool surface = classified == FootprintRegime::Surface;
    state.surfaceFrames = surface ? state.surfaceFrames + 1u : 0u;
    state.nonSurfaceFrames = surface ? 0u : state.nonSurfaceFrames + 1u;
    state.desiredRegime = classified;

    bool changed = false;
    if (state.committedRegime != FootprintRegime::Surface && state.surfaceFrames >= 2u) {
        state.committedRegime = FootprintRegime::Surface;
        state.pendingMask = payloadMask;
        state.readyMask = 0u;
        ++state.generation;
        changed = true;
    } else if (state.committedRegime == FootprintRegime::Surface && state.nonSurfaceFrames >= 4u) {
        state.committedRegime = classified;
        state.pendingMask = 0u;
        state.readyMask = 0u;
        ++state.generation;
        changed = true;
    }
    return changed;
}

inline void PublishWholesaleReady(WholesaleAvailability& state) {
    state.readyMask = state.pendingMask;
    state.pendingMask = 0u;
}

} // namespace Vixen::SVO
