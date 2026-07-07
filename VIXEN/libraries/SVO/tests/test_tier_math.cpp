// test_tier_math.cpp — Tiered ESVO Observer Addressing, Inc1 M1 (Task 2).
//
// Pure math tests for BuildTierScaleTable, no octree/GPU needed. Confirms
// the tier-to-scale mapping is monotonically increasing across all 5 tiers,
// and that the top (galaxy) and bottom (T2 bedrock) tiers bracket the
// design docs' own cited figures: ~1cm bedrock voxel
// (Sparse-Mip-ESVO-LOD-Direction-2026-07.md) and ~9.46e22 cm galaxy
// diameter (Tiered-ESVO-Observer-Addressing-Design-2026-07.md §1).

#include <gtest/gtest.h>

#include <cmath>

#include "TierMath.h"

using namespace Vixen::SVO;

namespace {
constexpr double kGalaxyDiameterCm = 9.46e22;
constexpr double kBedrockLeafCm = 1.0;
}  // namespace

TEST(TierMath, TableHasFiveTiersInDeclaredOrder) {
    const auto table = BuildTierScaleTable();
    ASSERT_EQ(table.size(), static_cast<std::size_t>(TierIndex::Count));
    EXPECT_EQ(table[0].index, TierIndex::T2Bedrock);
    EXPECT_EQ(table[1].index, TierIndex::T1Region);
    EXPECT_EQ(table[2].index, TierIndex::T0Planet);
    EXPECT_EQ(table[3].index, TierIndex::System);
    EXPECT_EQ(table[4].index, TierIndex::Galaxy);
}

TEST(TierMath, BottomTierLeafMatchesCitedOneCmBedrockVoxel) {
    const auto table = BuildTierScaleTable();
    const auto& t2 = table[static_cast<std::size_t>(TierIndex::T2Bedrock)];
    EXPECT_NEAR(t2.leafCm, kBedrockLeafCm, 1e-9);
}

TEST(TierMath, TopTierSpanMatchesCitedGalaxyDiameter) {
    const auto table = BuildTierScaleTable();
    const auto& galaxy = table[static_cast<std::size_t>(TierIndex::Galaxy)];
    // Re-derived bottom-up, should land on the cited figure (by
    // construction of the derivation -- see TierMath.h header comment),
    // not just "close to it".
    EXPECT_NEAR(galaxy.spanCm, kGalaxyDiameterCm, kGalaxyDiameterCm * 1e-9);
}

TEST(TierMath, EachTiersLeafEqualsNextFinerTiersSpan) {
    // The nesting relationship the whole derivation rests on: a tier's leaf
    // cell is exactly the span of the next-finer tier (T1 leaf == T2 span,
    // T0 leaf == T1 span, System leaf == T0 span, Galaxy leaf == System
    // span).
    const auto table = BuildTierScaleTable();
    for (std::size_t i = 1; i < table.size(); ++i) {
        EXPECT_NEAR(table[i].leafCm, table[i - 1].spanCm, table[i - 1].spanCm * 1e-9)
            << "tier index " << i;
    }
}

TEST(TierMath, SpansAreMonotonicallyIncreasingAcrossAllTiers) {
    const auto table = BuildTierScaleTable();
    for (std::size_t i = 1; i < table.size(); ++i) {
        EXPECT_GT(table[i].spanCm, table[i - 1].spanCm)
            << "tier index " << i << " span did not exceed previous tier's span";
    }
}

TEST(TierMath, LeafSizesAreMonotonicallyIncreasingAcrossAllTiers) {
    const auto table = BuildTierScaleTable();
    for (std::size_t i = 1; i < table.size(); ++i) {
        EXPECT_GT(table[i].leafCm, table[i - 1].leafCm)
            << "tier index " << i << " leaf size did not exceed previous tier's leaf size";
    }
}

TEST(TierMath, LowerThreeTiersUseApproximatelyTenLevelsEach) {
    // T0/T1/T2 use the source doc's own "~10 effective levels each"
    // convention (Sparse-Mip-ESVO-LOD-Direction-2026-07.md's tier table).
    const auto table = BuildTierScaleTable();
    for (std::size_t i = 0; i <= static_cast<std::size_t>(TierIndex::T0Planet); ++i) {
        EXPECT_NEAR(table[i].levels, 10.0, 1e-6) << "tier index " << i;
    }
}

TEST(TierMath, UpperTwoTiersUseApproximatelyTwentyThreeLevelsEach) {
    // System + Galaxy split the remaining budget up to the cited galaxy
    // diameter across exactly 2 tiers, landing close to the design doc's
    // "23 levels per ESVO instance" (ESVO_MAX_SCALE=22) framing for those
    // tiers -- see TierMath.h header comment for the full derivation.
    const auto table = BuildTierScaleTable();
    const auto& system = table[static_cast<std::size_t>(TierIndex::System)];
    const auto& galaxy = table[static_cast<std::size_t>(TierIndex::Galaxy)];
    EXPECT_NEAR(system.levels, 23.0, 0.5);
    EXPECT_NEAR(galaxy.levels, 23.0, 0.5);
    // Symmetric split: both upper tiers cover the same level count.
    EXPECT_NEAR(system.levels, galaxy.levels, 1e-9);
}

TEST(TierMath, TotalLevelsAcrossAllTiersMatchesCitedSeventySixPointThree) {
    // log2(galaxy_diameter_cm / 1cm) ~= 76.3, per
    // Tiered-ESVO-Observer-Addressing-Design-2026-07.md §1.
    const auto table = BuildTierScaleTable();
    double totalLevels = 0.0;
    for (const auto& tier : table) {
        totalLevels += tier.levels;
    }
    const double citedTotal = std::log2(kGalaxyDiameterCm / kBedrockLeafCm);
    EXPECT_NEAR(totalLevels, citedTotal, 0.01);
    EXPECT_NEAR(totalLevels, 76.3, 0.05);
}
