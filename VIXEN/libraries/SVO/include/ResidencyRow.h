#pragma once
// ResidencyRow.h — out-of-core residency R0: the declared residency dimension.
//
// Source of truth: Vixen-Docs/01-Architecture/2026-09-08-out-of-core-residency-architecture.md
// §2.1 (the four-tier ladder), §2.4 (the per-kind constexpr row), §8 row R0 ("rows +
// ResidencyState, no behaviour change").
//
// R0 SCOPE — DECLARATION ONLY. Nothing on any load/unload/upload path reads kResidencyRows
// yet; the rows name the ladder each payload kind can occupy TODAY so R1+ (per-region SVO
// residency off-tick, budget admission, the eviction ladder) has a declared table to act
// on. Every row below describes what the engine already does (measured in the design's
// §1.1 table), not what it will do: no kind has a Cold or Virtual tier because no cold
// format / producer-reconstruct path exists in VIXEN yet, so declaring one would be a
// promise nothing can keep. Widening a row's tier mask is an R1+ change that lands with
// the code that can honour it.
//
// R4-corollary (design D9, ratified): a residency tier is data the runner boundary reads,
// NEVER a simulation input. Nothing in this header is reachable from a system signature,
// manifest or plan row; it is consumed by the render-side residency boundary only.

#include "ResidencyTier.h"          // ResidencyTier / ResidencyTierMask
#include "WholesaleAvailability.h"  // WholesalePayload — the payload bits the rows are keyed against

#include <cstddef>
#include <cstdint>

namespace Vixen::SVO {

// Which ResourceBudgetManager row a kind draws from (design §6.1).
enum class ResidencyBudgetClass : uint8_t { Host = 0, Device = 1, Staging = 2 };

// How eviction candidates of a kind are ranked (design §6.3, D10).
enum class ResidencyEvictPolicy : uint8_t { Age = 0, DistanceFromActive = 1, CostBenefit = 2 };

// What the kind's cold record is (design §5.2, D5).
enum class ResidencyColdFormat : uint8_t {
    SealedSnapshotPlusDeltaTail = 0,
    DeltaTailOnly = 1,
    ExternalArtifact = 2,
    None = 3,
};

// The current VIXEN payload kinds — the wholesale-admission payload table
// (Vixen-Docs/Deep-Field-Wholesale-Admission-2026-08.md, "Payload policy") plus the
// shell cache and the binary brick pool, i.e. everything CreateOctreeBuffers uploads.
enum class ResidencyKind : uint8_t {
    Nodes = 0,
    Materials,
    Config,
    ChannelPool,
    BrickLookup,
    MipPool,
    TierRefTable,
    OccupancyGrid,
    Bricks,
    ShellCache,
    Count,
};

inline constexpr size_t kResidencyKindCount = static_cast<size_t>(ResidencyKind::Count);

struct ResidencyRow {
    const char*          kind;             // the payload kind, spelled as the metrics/CSV columns spell it
    ResidencyTierMask    tiers;            // which of T0..T3 this kind supports today
    ResidencyTier        floorTier;        // never evicted below this (Hot = pinned)
    ResidencyBudgetClass budget;           // which budget row it draws from
    ResidencyEvictPolicy evict;            // candidate ranking when its class is over budget
    ResidencyColdFormat  cold;             // its cold record; None = no cold tier
    uint8_t              prefetchHorizon;  // frames of lookahead (0 = boundary only)
};

namespace detail {
inline constexpr ResidencyTierMask kHotOnly =
    ResidencyTierBit(ResidencyTier::Hot);
inline constexpr ResidencyTierMask kHotWarm =
    ResidencyTierBit(ResidencyTier::Hot) | ResidencyTierBit(ResidencyTier::Warm);
}  // namespace detail

// One row per kind, indexed by ResidencyKind. Today's ladder spans T0/T1 only:
// - Nodes/Materials/Config are the "minimum correctness root" (wholesale-admission doc:
//   "No in slice 1") — pinned Hot.
// - MipPool may not be absent for a mip-baked visible octree (the regime-2/3 fallback
//   samples it) — pinned Hot.
// - ShellCache uploads whole at Compile into two GPU slots and is never gated — Hot.
// - ChannelPool+BrickLookup (the atomic fine pair), TierRefTable, OccupancyGrid are the
//   wholesale-admitted payloads: demotion clears the ready bit and RETAINS bytes
//   (WholesaleAvailability.h, Advance* demote leg) — Hot|Warm, floor Warm.
// - Bricks is the binary residency gate: brickResident=0 renders from mips while the CPU
//   pool keeps the bytes ("stop requesting is sufficient; there is no brick data to
//   un-write") — Hot|Warm, floor Warm.
// Every VIXEN payload ranks eviction by DistanceFromActive (D10) and draws from the
// device budget; no kind has a cold record (D5: VIXEN persists authored render deltas only,
// and that path is unbuilt). prefetchHorizon is 0 everywhere: today's hysteresis is
// boundary-only, with no lookahead.
inline constexpr ResidencyRow kResidencyRows[kResidencyKindCount] = {
    {"nodes",         detail::kHotOnly, ResidencyTier::Hot,  ResidencyBudgetClass::Device, ResidencyEvictPolicy::DistanceFromActive, ResidencyColdFormat::None, 0u},
    {"materials",     detail::kHotOnly, ResidencyTier::Hot,  ResidencyBudgetClass::Device, ResidencyEvictPolicy::DistanceFromActive, ResidencyColdFormat::None, 0u},
    {"config",        detail::kHotOnly, ResidencyTier::Hot,  ResidencyBudgetClass::Device, ResidencyEvictPolicy::DistanceFromActive, ResidencyColdFormat::None, 0u},
    {"channelPool",   detail::kHotWarm, ResidencyTier::Warm, ResidencyBudgetClass::Device, ResidencyEvictPolicy::DistanceFromActive, ResidencyColdFormat::None, 0u},
    {"brickLookup",   detail::kHotWarm, ResidencyTier::Warm, ResidencyBudgetClass::Device, ResidencyEvictPolicy::DistanceFromActive, ResidencyColdFormat::None, 0u},
    {"mipPool",       detail::kHotOnly, ResidencyTier::Hot,  ResidencyBudgetClass::Device, ResidencyEvictPolicy::DistanceFromActive, ResidencyColdFormat::None, 0u},
    {"tierRefTable",  detail::kHotWarm, ResidencyTier::Warm, ResidencyBudgetClass::Device, ResidencyEvictPolicy::DistanceFromActive, ResidencyColdFormat::None, 0u},
    {"occupancyGrid", detail::kHotWarm, ResidencyTier::Warm, ResidencyBudgetClass::Device, ResidencyEvictPolicy::DistanceFromActive, ResidencyColdFormat::None, 0u},
    {"bricks",        detail::kHotWarm, ResidencyTier::Warm, ResidencyBudgetClass::Device, ResidencyEvictPolicy::DistanceFromActive, ResidencyColdFormat::None, 0u},
    {"shellCache",    detail::kHotOnly, ResidencyTier::Hot,  ResidencyBudgetClass::Device, ResidencyEvictPolicy::DistanceFromActive, ResidencyColdFormat::None, 0u},
};

inline constexpr const ResidencyRow& ResidencyRowOf(ResidencyKind kind) {
    return kResidencyRows[static_cast<size_t>(kind)];
}

// The wholesale-admission payload bits are a subset of the kinds; this is the bridge from
// a ResidencyState mask bit to its row. Kept as a switch (not a table) so adding a payload
// bit without a row fails to compile under -Wswitch.
inline constexpr ResidencyKind ResidencyKindOfPayload(WholesalePayload payload) {
    switch (payload) {
        case WholesalePayload::ChannelPool:   return ResidencyKind::ChannelPool;
        case WholesalePayload::BrickLookup:   return ResidencyKind::BrickLookup;
        case WholesalePayload::TierRefTable:  return ResidencyKind::TierRefTable;
        case WholesalePayload::OccupancyGrid: return ResidencyKind::OccupancyGrid;
    }
    return ResidencyKind::Count;
}

// Structural invariants the table must hold; compile-time so a bad row never links.
namespace detail {
inline constexpr bool ResidencyRowsWellFormed() {
    for (size_t i = 0; i < kResidencyKindCount; ++i) {
        const ResidencyRow& row = kResidencyRows[i];
        if (row.kind == nullptr || row.tiers == 0u) return false;
        // The floor must be a supported tier, and Hot must always be supported (a kind
        // that can never be bound has no reason to exist).
        if ((row.tiers & ResidencyTierBit(row.floorTier)) == 0u) return false;
        if ((row.tiers & ResidencyTierBit(ResidencyTier::Hot)) == 0u) return false;
        // A cold record without a cold tier (or vice versa) is a contradiction.
        const bool hasCold = (row.tiers & ResidencyTierBit(ResidencyTier::Cold)) != 0u;
        if (hasCold != (row.cold != ResidencyColdFormat::None)) return false;
    }
    return true;
}
}  // namespace detail

static_assert(detail::ResidencyRowsWellFormed(), "kResidencyRows violates a ladder invariant");
static_assert(ResidencyKindOfPayload(WholesalePayload::ChannelPool) == ResidencyKind::ChannelPool);
static_assert(ResidencyKindOfPayload(WholesalePayload::OccupancyGrid) == ResidencyKind::OccupancyGrid);

}  // namespace Vixen::SVO
