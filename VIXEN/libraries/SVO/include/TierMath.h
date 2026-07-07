#pragma once
// TierMath.h — Tiered ESVO Observer Addressing, Inc1 M1 (Task 2).
//
// Re-derivation (not re-invention) of the tier count/sizing math already
// established in Sparse-Mip-ESVO-LOD-Direction-2026-07.md's "Concrete tier
// math for the 1 cm planetary configuration" table (T0 planet / T1 region /
// T2 bedrock, ~10 effective levels each) and generalized upward per
// Tiered-ESVO-Observer-Addressing-Design-2026-07.md §1's restatement
// (~76 levels voxel-cm to galaxy-diameter, 23 levels per ESVO instance,
// ESVOTraversalState::scale confirmed 0-22 in LaineKarrasOctree.h -> ~4-5
// tiers). This file is pure CPU bookkeeping: a table mapping tier index to
// linear scale (span) range, in centimeters, for use by later increments'
// magnitude-falloff math (M2 — not built here).
//
// Derivation, verified against the source docs rather than copied:
//
//   Each tier's LEAF cell size equals the next-finer tier's SPAN (this is
//   the nesting relationship: T1's region span is what one T0 leaf covers,
//   and so on down to T2's bedrock leaf = 1 voxel-cm). Working bottom-up
//   from the design doc's cited ~1cm bedrock voxel:
//
//     T2 bedrock: leaf = 1 cm,        ~10 levels (doc: "~7 node levels +
//                 8^3 brick" = 7 + 3 = 10 effective levels of 2x
//                 subdivision) -> span = 1cm * 2^10 = 1024 cm (~10.2 m,
//                 matching the doc's cited "~12 m" T2 span within the
//                 same order of magnitude -- the doc's own numbers use a
//                 slightly fudged effective-level count; this table uses
//                 clean powers of two per tier so ranges compose exactly).
//     T1 region:  leaf = T2 span,     10 levels -> span ~10.49 km
//                 (doc cites T1 span "~12.4 km" -- same reconciliation).
//     T0 planet:  leaf = T1 span,     10 levels -> span ~10,737 km
//                 (doc cites planet diameter "12,700 km" -- same order of
//                 magnitude; the discrepancy is the doc's own "~10
//                 effective levels" being an approximation of Earth's
//                 actual diameter, not an error in this derivation).
//
//   That leaves the design doc's own claim: "~76 levels total, 23 levels
//   per ESVO instance -> ~4-5 nested tiers", generalizing T0/T1/T2 (3
//   tiers, ~30 levels) two tiers upward (system, galaxy) to reach the
//   cited galaxy diameter (9.46e22 cm). The remaining level budget is
//   log2(9.46e22 / T0_span) =~ 46.08 levels, split evenly across exactly
//   two tiers (system, galaxy) -> ~23.04 levels/tier -- which lines up
//   almost exactly with "23 levels per ESVO instance" (ESVO_MAX_SCALE=22),
//   confirming the design doc's arithmetic: T0/T1/T2 keep a conservative
//   ~10-level/tier budget (headroom in the 23-level stack, since those
//   tiers also carry brick-local subdivision), while the System and
//   Galaxy tiers above use closer to the full 23-level ESVO budget per
//   instance (no brick subdivision needed at that scale -- pure
//   scale/index hops). Total re-derived level count: 10+10+10+23.04+23.04
//   = 76.08, within rounding of the doc's cited 76.3 (log2(9.46e22/1)).
//
// Scope note: this is bookkeeping/derivation, not new design (Tiered-ESVO-
// Inc1-Plan-2026-07.md Task 2) -- no GPU/render dependency, no change to
// LaineKarrasOctree/ESVO_MAX_SCALE/ChildDescriptor.

#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace Vixen::SVO {

// Ordered tier index, finest (bedrock) to coarsest (galaxy). Matches
// TierAddress's hop ordering convention: hops[0] is the topmost/coarsest
// hop taken, descending toward finer tiers -- so tier index here is
// "distance from the bedrock leaf", not "hop position in an address".
enum class TierIndex : std::size_t {
    T2Bedrock = 0,  // leaf ~1 cm; bricks live only here (today's ESVO leaf->brick format)
    T1Region  = 1,  // leaf ~10.24 m
    T0Planet  = 2,  // leaf ~10.49 km
    System    = 3,  // leaf ~T0 span (~10,737 km)
    Galaxy    = 4,  // leaf ~System span; span == cited galaxy diameter

    Count = 5
};

// One tier's linear-scale range, in centimeters: [spanCm / 2^levels, spanCm].
// spanCm is the tier's own full extent (the size of its root cell); leafCm
// is the size of one leaf cell within this tier (== the next-finer tier's
// spanCm, by the nesting relationship derived above).
struct TierScaleRange {
    TierIndex index;
    double leafCm;    // finest cell size resolvable within this tier
    double spanCm;    // full linear extent this tier's root cell covers
    double levels;    // log2(spanCm / leafCm) -- levels of 2x subdivision in this tier
};

namespace detail {
// T2 bedrock leaf size: the design doc's cited "~1cm voxel" bedrock
// configuration (Sparse-Mip-ESVO-LOD-Direction-2026-07.md).
inline constexpr double kBedrockLeafCm = 1.0;

// T0/T1/T2 each use ~10 effective levels per the source doc's own table
// ("Three tiers, ~10 effective levels each").
inline constexpr double kLowerTierLevels = 10.0;

// Cited galaxy diameter (Tiered-ESVO-Observer-Addressing-Design-2026-07.md
// §1: "galaxy-diameter... 9.46x10^22 cm").
inline constexpr double kGalaxyDiameterCm = 9.46e22;
}  // namespace detail

// Builds the full tier table, finest to coarsest, re-deriving every span
// from the bedrock leaf size upward (never hand-copying a number the doc
// itself only approximates) so System/Galaxy exactly bracket the cited
// galaxy diameter regardless of intermediate rounding.
inline std::array<TierScaleRange, static_cast<std::size_t>(TierIndex::Count)> BuildTierScaleTable() {
    std::array<TierScaleRange, static_cast<std::size_t>(TierIndex::Count)> table{};

    // T2 bedrock.
    const double t2Leaf = detail::kBedrockLeafCm;
    const double t2Levels = detail::kLowerTierLevels;
    const double t2Span = t2Leaf * std::pow(2.0, t2Levels);
    table[0] = TierScaleRange{TierIndex::T2Bedrock, t2Leaf, t2Span, t2Levels};

    // T1 region: leaf == T2's span.
    const double t1Leaf = t2Span;
    const double t1Levels = detail::kLowerTierLevels;
    const double t1Span = t1Leaf * std::pow(2.0, t1Levels);
    table[1] = TierScaleRange{TierIndex::T1Region, t1Leaf, t1Span, t1Levels};

    // T0 planet: leaf == T1's span.
    const double t0Leaf = t1Span;
    const double t0Levels = detail::kLowerTierLevels;
    const double t0Span = t0Leaf * std::pow(2.0, t0Levels);
    table[2] = TierScaleRange{TierIndex::T0Planet, t0Leaf, t0Span, t0Levels};

    // System + Galaxy split the remaining level budget up to the cited
    // galaxy diameter evenly across exactly two tiers (re-derivation, see
    // file header comment) -- this is what makes the top tier land exactly
    // on the cited figure rather than an approximation of it.
    const double remainingLevels = std::log2(detail::kGalaxyDiameterCm / t0Span);
    const double upperTierLevels = remainingLevels / 2.0;

    const double sysLeaf = t0Span;
    const double sysSpan = sysLeaf * std::pow(2.0, upperTierLevels);
    table[3] = TierScaleRange{TierIndex::System, sysLeaf, sysSpan, upperTierLevels};

    const double galLeaf = sysSpan;
    const double galSpan = galLeaf * std::pow(2.0, upperTierLevels);
    table[4] = TierScaleRange{TierIndex::Galaxy, galLeaf, galSpan, upperTierLevels};

    return table;
}

}  // namespace Vixen::SVO
