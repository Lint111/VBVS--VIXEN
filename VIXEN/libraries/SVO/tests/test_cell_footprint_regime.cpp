// test_cell_footprint_regime.cpp — E6-T1: shared CellFootprintRegime classifier.
//
// Pure math tests for ClassifyCellFootprintRegime, no octree/GPU needed. Covers the slice's
// required cases: all three regimes, zero/negative coefficients, exact thresholds, and
// float boundary behavior. Formula under test (must match shaders/SceneBindings.glsl's
// classifyCellFootprintRegime bit-for-bit):
//
//   footprint = worldDist * raySizeCoef + raySizeBias
//   raySizeCoef <= 0 || footprint < cellWorldSize/8  -> Surface (1)
//   footprint < cosmicK * cellWorldSize              -> MipHit  (2)
//   else                                             -> Cosmic  (3)

#include <gtest/gtest.h>

#include "CellFootprintRegime.h"

using namespace Vixen::SVO;

namespace {
constexpr float kCellWorldSize = 8.0f;  // chosen so cellWorldSize/8 == 1.0, easy thresholds
constexpr float kCosmicK = 4.0f;        // matches BuildRenderGraph.cpp's default cosmicK
}  // namespace

// ---------------------------------------------------------------------------
// All three regimes, ordinary inputs.
// ---------------------------------------------------------------------------

TEST(CellFootprintRegime, SurfaceWhenFootprintBelowCellDivBrickDivisor) {
    // footprint = 0.5*1 + 0 = 0.5 < 8/8=1.0 -> Surface
    const auto regime = ClassifyCellFootprintRegime(0.5f, kCellWorldSize, 1.0f, 0.0f, kCosmicK);
    EXPECT_EQ(regime, CellFootprintRegime::Surface);
}

TEST(CellFootprintRegime, MipHitWhenFootprintBetweenSurfaceAndCosmicThresholds) {
    // footprint = 2*1 + 0 = 2.0; surface bound 1.0, cosmic bound 4*8=32.0 -> MipHit
    const auto regime = ClassifyCellFootprintRegime(2.0f, kCellWorldSize, 1.0f, 0.0f, kCosmicK);
    EXPECT_EQ(regime, CellFootprintRegime::MipHit);
}

TEST(CellFootprintRegime, CosmicWhenFootprintAtOrAboveCosmicThreshold) {
    // footprint = 40*1 + 0 = 40.0 >= cosmicK*cellWorldSize = 32.0 -> Cosmic
    const auto regime = ClassifyCellFootprintRegime(40.0f, kCellWorldSize, 1.0f, 0.0f, kCosmicK);
    EXPECT_EQ(regime, CellFootprintRegime::Cosmic);
}

// ---------------------------------------------------------------------------
// Zero / negative raySizeCoef — the "LOD disabled" short-circuit.
// ---------------------------------------------------------------------------

TEST(CellFootprintRegime, ZeroRaySizeCoefAlwaysSurfaceRegardlessOfDistance) {
    // raySizeCoef == 0.0 disables LOD entirely -> always Surface, even at huge worldDist.
    EXPECT_EQ(ClassifyCellFootprintRegime(0.0f, kCellWorldSize, 0.0f, 0.0f, kCosmicK),
              CellFootprintRegime::Surface);
    EXPECT_EQ(ClassifyCellFootprintRegime(1'000'000.0f, kCellWorldSize, 0.0f, 100.0f, kCosmicK),
              CellFootprintRegime::Surface);
}

TEST(CellFootprintRegime, NegativeRaySizeCoefAlwaysSurface) {
    // The <= 0.0 branch of the gate also covers negative coefficients (never expected in
    // practice, but the shader's comparison operator does not special-case it either).
    EXPECT_EQ(ClassifyCellFootprintRegime(500.0f, kCellWorldSize, -1.0f, 0.0f, kCosmicK),
              CellFootprintRegime::Surface);
}

TEST(CellFootprintRegime, ZeroCosmicKCollapsesMipHitAwayFromSurface) {
    // cosmicK == 0 makes the cosmic threshold 0 * cellWorldSize == 0, so any footprint
    // that clears the Surface gate (footprint >= cellWorldSize/8, footprint > 0 required
    // to clear it when raySizeCoef>0) immediately fails "< 0" too -> Cosmic, never MipHit.
    const auto regime = ClassifyCellFootprintRegime(2.0f, kCellWorldSize, 1.0f, 0.0f, 0.0f);
    EXPECT_EQ(regime, CellFootprintRegime::Cosmic);
}

TEST(CellFootprintRegime, NegativeCosmicKAlsoCollapsesToCosmic) {
    const auto regime = ClassifyCellFootprintRegime(2.0f, kCellWorldSize, 1.0f, 0.0f, -4.0f);
    EXPECT_EQ(regime, CellFootprintRegime::Cosmic);
}

// ---------------------------------------------------------------------------
// Exact thresholds and float boundary behavior.
// ---------------------------------------------------------------------------

TEST(CellFootprintRegime, ExactlyAtSurfaceThresholdIsNotSurface) {
    // footprint == cellWorldSize/8 exactly: the gate is a strict "<", so equality does
    // NOT count as Surface — it falls through to the MipHit/Cosmic comparison.
    const float footprintEqualsThreshold = kCellWorldSize / kBrickDivisor;  // == 1.0
    const auto regime = ClassifyCellFootprintRegime(footprintEqualsThreshold, kCellWorldSize, 1.0f, 0.0f, kCosmicK);
    EXPECT_NE(regime, CellFootprintRegime::Surface);
    EXPECT_EQ(regime, CellFootprintRegime::MipHit);
}

TEST(CellFootprintRegime, JustBelowSurfaceThresholdIsSurface) {
    const float justBelow = kCellWorldSize / kBrickDivisor - 1e-4f;
    const auto regime = ClassifyCellFootprintRegime(justBelow, kCellWorldSize, 1.0f, 0.0f, kCosmicK);
    EXPECT_EQ(regime, CellFootprintRegime::Surface);
}

TEST(CellFootprintRegime, ExactlyAtCosmicThresholdIsNotMipHit) {
    // footprint == cosmicK*cellWorldSize exactly: strict "<" again, so equality falls
    // through to Cosmic, not MipHit.
    const float footprintEqualsCosmic = kCosmicK * kCellWorldSize;  // == 32.0
    const auto regime = ClassifyCellFootprintRegime(footprintEqualsCosmic, kCellWorldSize, 1.0f, 0.0f, kCosmicK);
    EXPECT_EQ(regime, CellFootprintRegime::Cosmic);
}

TEST(CellFootprintRegime, JustBelowCosmicThresholdIsMipHit) {
    const float justBelow = kCosmicK * kCellWorldSize - 1e-3f;
    const auto regime = ClassifyCellFootprintRegime(justBelow, kCellWorldSize, 1.0f, 0.0f, kCosmicK);
    EXPECT_EQ(regime, CellFootprintRegime::MipHit);
}

TEST(CellFootprintRegime, RaySizeBiasShiftsFootprintAcrossThresholds) {
    // Same worldDist/coef, only raySizeBias differs: a small distance with a large bias
    // should still be able to cross into MipHit, exercising the additive term, not just
    // the multiplicative one.
    const auto noBias = ClassifyCellFootprintRegime(0.1f, kCellWorldSize, 1.0f, 0.0f, kCosmicK);
    EXPECT_EQ(noBias, CellFootprintRegime::Surface);

    const auto withBias = ClassifyCellFootprintRegime(0.1f, kCellWorldSize, 1.0f, 5.0f, kCosmicK);
    EXPECT_EQ(withBias, CellFootprintRegime::MipHit);
}

TEST(CellFootprintRegime, ZeroWorldDistAndZeroBiasIsSurface) {
    // footprint == 0 < cellWorldSize/8 (assuming cellWorldSize > 0) -> Surface.
    const auto regime = ClassifyCellFootprintRegime(0.0f, kCellWorldSize, 1.0f, 0.0f, kCosmicK);
    EXPECT_EQ(regime, CellFootprintRegime::Surface);
}

TEST(CellFootprintRegime, MonotonicNonDecreasingRegimeWithIncreasingWorldDist) {
    // Regime ordinal must never DECREASE as worldDist grows (fixed coef/bias/cosmicK/cell):
    // farther/coarser footprints should never resolve to a "less coarse" regime.
    const float dists[] = {0.0f, 0.5f, 2.0f, 10.0f, 100.0f, 10000.0f};
    unsigned int prev = 0;
    for (float d : dists) {
        const auto regime = ClassifyCellFootprintRegime(d, kCellWorldSize, 1.0f, 0.0f, kCosmicK);
        const unsigned int cur = static_cast<unsigned int>(regime);
        EXPECT_GE(cur, prev) << "regime decreased at worldDist=" << d;
        prev = cur;
    }
}
