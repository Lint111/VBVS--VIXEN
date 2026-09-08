#pragma once

#include "CellFootprintRegime.h"
#include "ResidencyTier.h"

#include <cstdint>
#include <array>

namespace Vixen::SVO {

// CPU-owned wholesale admission ledger. Payload bits are explicit and independently
// admissible; channelPool and brickLookup remain the atomic fine-data pair.
enum class WholesalePayload : uint32_t {
    ChannelPool = 1u << 0,
    BrickLookup = 1u << 1,
    TierRefTable = 1u << 2,
    OccupancyGrid = 1u << 3,
};

// Per-scope residency state — the out-of-core design's `ResidencyState`
// (Vixen-Docs/01-Architecture/2026-09-08-out-of-core-residency-architecture.md §2.3),
// i.e. the former `WholesaleAvailability` struct with the ladder position added beside
// the regime it is derived from. R0 (design §8) adds the dimension WITHOUT changing the
// protocol: the regime fields, masks, counters, retained-payload ledger and the FNV
// signature below are byte-for-byte what the wholesale gate ran on before, and every
// caller still keys its decisions on `committedRegime`. `desiredTier`/`committedTier`
// are kept in lockstep by the same functions that move the regimes
// (ResidencyTierOfRegime) — they are observers of today's T0/T1 ladder, read by nothing
// on the upload path yet. Re-keying one state per (kind, scope) is R1 (per-region
// requests); today there is exactly one instance per BodyOctreeSceneNode, whose scope is
// the whole concatenated pool and whose kind set is the payload mask.
//
// R4-corollary (design D9): this state is render-side residency bookkeeping; it is never a
// simulation input.
struct ResidencyState {
    CellFootprintRegime desiredRegime = CellFootprintRegime::MipHit;
    CellFootprintRegime committedRegime = CellFootprintRegime::MipHit;
    ResidencyTier desiredTier = ResidencyTierOfRegime(CellFootprintRegime::MipHit);
    ResidencyTier committedTier = ResidencyTierOfRegime(CellFootprintRegime::MipHit);
    uint32_t generation = 0;
    uint32_t pendingMask = 0;
    uint32_t readyMask = 0;
    uint32_t surfaceFrames = 0;
    uint32_t nonSurfaceFrames = 0;
    // Retained payload content survives demotion but is never readable until the
    // matching pair is re-published. Entries are indexed by payload bit order.
    std::array<uint64_t, 4> payloadBytes{};
    std::array<uint64_t, 4> payloadContentHash{};
    uint32_t retainedMask = 0;
    uint64_t reusablePopulatedBytes = 0;
};

inline uint32_t WholesalePayloadMask() {
    return static_cast<uint32_t>(WholesalePayload::ChannelPool) |
           static_cast<uint32_t>(WholesalePayload::BrickLookup);
}

inline uint32_t WholesaleFinePayloadMask() {
    return static_cast<uint32_t>(WholesalePayload::ChannelPool) |
           static_cast<uint32_t>(WholesalePayload::BrickLookup);
}

inline uint32_t WholesaleS4PayloadMask() {
    return static_cast<uint32_t>(WholesalePayload::TierRefTable) |
           static_cast<uint32_t>(WholesalePayload::OccupancyGrid);
}

// The only writers of the regime fields: every regime move carries its tier with it, so
// the tier can never drift from the regime it is derived from.
inline void SetDesiredRegime(ResidencyState& state, CellFootprintRegime regime) {
    state.desiredRegime = regime;
    state.desiredTier = ResidencyTierOfRegime(regime);
}

inline void SetCommittedRegime(ResidencyState& state, CellFootprintRegime regime) {
    state.committedRegime = regime;
    state.committedTier = ResidencyTierOfRegime(regime);
}

inline void RetainWholesalePayload(ResidencyState& state, uint32_t payloadMask,
                                   uint64_t channelPoolBytes, uint64_t brickLookupBytes,
                                   uint64_t channelPoolHash, uint64_t brickLookupHash) {
    if ((payloadMask & WholesaleFinePayloadMask()) == WholesaleFinePayloadMask()) {
        state.payloadBytes[0] = channelPoolBytes; state.payloadBytes[1] = brickLookupBytes;
        state.payloadContentHash[0] = channelPoolHash; state.payloadContentHash[1] = brickLookupHash;
        state.retainedMask |= WholesaleFinePayloadMask();
    }
}

// Apply the frozen hysteresis: two consecutive Surface classifications promote, while
// four consecutive non-Surface classifications demote. A transition clears readiness
// before any payload is reused, so stale retained bytes are never shader-readable.
inline bool AdvanceWholesaleAvailability(ResidencyState& state,
                                         CellFootprintRegime classified,
                                         uint32_t payloadMask) {
    const bool surface = classified == CellFootprintRegime::Surface;
    state.surfaceFrames = surface ? state.surfaceFrames + 1u : 0u;
    state.nonSurfaceFrames = surface ? 0u : state.nonSurfaceFrames + 1u;
    SetDesiredRegime(state, classified);

    bool changed = false;
    if (state.committedRegime != CellFootprintRegime::Surface && state.surfaceFrames >= 2u) {
        if ((payloadMask & WholesaleFinePayloadMask()) != WholesaleFinePayloadMask()) return false;
        SetCommittedRegime(state, CellFootprintRegime::Surface);
        state.pendingMask = payloadMask;
        state.readyMask = 0u;
        if ((state.retainedMask & payloadMask) == payloadMask) {
            state.reusablePopulatedBytes = state.payloadBytes[0] + state.payloadBytes[1];
            state.pendingMask = 0u;
        } else {
            state.reusablePopulatedBytes = 0u;
        }
        ++state.generation;
        changed = true;
    } else if (state.committedRegime == CellFootprintRegime::Surface && state.nonSurfaceFrames >= 4u) {
        SetCommittedRegime(state, classified);
        state.pendingMask = 0u;
        state.readyMask = 0u;
        ++state.generation;
        changed = true;
    }
    return changed;
}

inline void PublishWholesaleReady(ResidencyState& state) {
    state.readyMask = state.pendingMask != 0u ? state.pendingMask :
        (state.reusablePopulatedBytes != 0u ? WholesaleFinePayloadMask() : 0u);
    state.pendingMask = 0u;
    state.reusablePopulatedBytes = 0u;
}

// Per-octree parity witness (design §2.3). Deliberately does NOT mix the tier fields:
// they are derived from the regime, so the signature is unchanged by R0 by construction.
inline uint64_t WholesaleResidentSignatureFNV64(const ResidencyState& state,
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
