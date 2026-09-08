#pragma once
// ResidencyTier.h — out-of-core residency R0: the four-tier ladder (design
// Vixen-Docs/01-Architecture/2026-09-08-out-of-core-residency-architecture.md §2.1).
//
// Split out of ResidencyRow.h so the per-scope state (WholesaleAvailability.h's
// ResidencyState) and the per-kind row table can both name a tier without including each
// other.
//
// Naming: `ResidencyTier`, not `Tier` — in this library `Tier` already means the ESVO scale
// tier (TierAddress.h / TierRef / TierDirection), an unrelated axis.
//
// R4-corollary (design D9, ratified): a tier is data the runner boundary reads, NEVER a
// simulation input. This header is render-side residency bookkeeping only.

#include "CellFootprintRegime.h"

#include <cstdint>

namespace Vixen::SVO {

// Ordered by re-arm cost. Eviction walks down one step per boundary; a load may skip up
// but publishes only at the boundary.
enum class ResidencyTier : uint8_t {
    Hot = 0,      // T0: bound into the working set (GPU: ready bit set; brickResident=1)
    Warm = 1,     // T1: bytes retained in RAM/VRAM but not bound; re-arm is a flag flip
    Cold = 2,     // T2: persisted; re-arm is I/O + reconstruct
    Virtual = 3,  // T3: derivable from base + delta log; re-arm is compute
};

using ResidencyTierMask = uint8_t;

inline constexpr ResidencyTierMask ResidencyTierBit(ResidencyTier tier) {
    return static_cast<ResidencyTierMask>(1u << static_cast<uint8_t>(tier));
}

// The tier a wholesale-admission regime decision lands a payload on TODAY. The shipped
// ladder spans T0/T1 only: a Surface commit binds the payload (Hot); every non-Surface
// commit clears the ready bit and RETAINS the bytes (WholesaleAvailability.h's demote leg
// never reclaims), which is exactly T1. Cold/Virtual are unreachable until R1+ lands a
// reclaim/reconstruct path — this mapping is where that widening will happen.
inline constexpr ResidencyTier ResidencyTierOfRegime(CellFootprintRegime regime) {
    return regime == CellFootprintRegime::Surface ? ResidencyTier::Hot : ResidencyTier::Warm;
}

}  // namespace Vixen::SVO
