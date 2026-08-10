#pragma once

#include "FootprintRegime.h"

#include <cstdint>
#include <array>

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
    // Retained payload content survives demotion but is never readable until the
    // matching pair is re-published. Entries are indexed by payload bit order.
    std::array<uint64_t, 2> payloadBytes{};
    std::array<uint64_t, 2> payloadContentHash{};
    uint32_t retainedMask = 0;
    uint64_t reusablePopulatedBytes = 0;
};

inline uint32_t WholesalePayloadMask() {
    return static_cast<uint32_t>(WholesalePayload::ChannelPool) |
           static_cast<uint32_t>(WholesalePayload::BrickLookup);
}

inline void RetainWholesalePayload(WholesaleAvailability& state, uint32_t payloadMask,
                                   uint64_t channelPoolBytes, uint64_t brickLookupBytes,
                                   uint64_t channelPoolHash, uint64_t brickLookupHash) {
    if ((payloadMask & WholesalePayloadMask()) != WholesalePayloadMask()) return;
    state.payloadBytes = {channelPoolBytes, brickLookupBytes};
    state.payloadContentHash = {channelPoolHash, brickLookupHash};
    state.retainedMask |= WholesalePayloadMask();
}

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
        if (payloadMask != WholesalePayloadMask()) return false;
        state.committedRegime = FootprintRegime::Surface;
        state.pendingMask = WholesalePayloadMask();
        state.readyMask = 0u;
        if ((state.retainedMask & payloadMask) == payloadMask) {
            state.reusablePopulatedBytes = state.payloadBytes[0] + state.payloadBytes[1];
            state.pendingMask = 0u;
        } else {
            state.reusablePopulatedBytes = 0u;
        }
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
    state.readyMask = state.pendingMask != 0u ? state.pendingMask :
        (state.reusablePopulatedBytes != 0u ? WholesalePayloadMask() : 0u);
    state.pendingMask = 0u;
    state.reusablePopulatedBytes = 0u;
}

inline uint64_t WholesaleResidentSignatureFNV64(const WholesaleAvailability& state,
                                                uint32_t octreeIndex = 0u) {
    uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](uint64_t value) {
        for (unsigned i = 0; i < 8; ++i) {
            hash ^= (value >> (i * 8u)) & 0xffu;
            hash *= 1099511628211ull;
        }
    };
    mix(octreeIndex);
    mix(static_cast<uint32_t>(WholesalePayload::ChannelPool));
    mix(state.generation); mix((state.readyMask & 1u) != 0u); mix(state.payloadBytes[0]); mix(state.payloadContentHash[0]);
    mix(octreeIndex);
    mix(static_cast<uint32_t>(WholesalePayload::BrickLookup));
    mix(state.generation); mix((state.readyMask & 2u) != 0u); mix(state.payloadBytes[1]); mix(state.payloadContentHash[1]);
    return hash;
}

} // namespace Vixen::SVO
