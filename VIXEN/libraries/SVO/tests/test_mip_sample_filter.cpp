// test_mip_sample_filter.cpp — Sparse-Mip ESVO LOD Inc1, M1 Task 1.
//
// Pure math tests for MipSample filtering, no octree needed:
//   - color/roughness: weighted mean by child coverage.
//   - SDF (FK_DISTANCE): conservative min-magnitude, NOT mean — includes the
//     adversarial regression case where a naive mean would misplace the
//     surface (a near-surface child averaged with a deep-interior child).

#include <gtest/gtest.h>
#include <array>
#include <cmath>

#include "MipSample.h"

using namespace Vixen::SVO;

namespace {

std::array<MipChildSample, 8> MakeChildren(std::initializer_list<float> values) {
    std::array<MipChildSample, 8> children{};
    size_t i = 0;
    for (float v : values) {
        children[i] = MipChildSample{v, true};
        ++i;
    }
    return children;
}

}  // namespace

// ---------------------------------------------------------------------------
// Weighted-mean filter (color/roughness)
// ---------------------------------------------------------------------------

TEST(MipSampleFilter, MeanUniformFill) {
    // All 8 children occupied with the same value -> mean == that value, full coverage.
    std::array<MipChildSample, 8> children;
    children.fill(MipChildSample{0.5f, true});

    MipSample out = FilterMipMean(children);
    EXPECT_NEAR(out.value, 0.5f, 1e-6f);
    EXPECT_NEAR(out.coverage, 1.0f, 1e-6f);
}

TEST(MipSampleFilter, MeanHalfEmpty) {
    // 4 occupied children (value=1.0), 4 empty -> mean of occupied only == 1.0,
    // coverage == 0.5 (occupied fraction, unoccupied children don't pull the mean down).
    std::array<MipChildSample, 8> children{};
    for (int i = 0; i < 4; ++i) children[i] = MipChildSample{1.0f, true};
    for (int i = 4; i < 8; ++i) children[i] = MipChildSample{0.0f, false};

    MipSample out = FilterMipMean(children);
    EXPECT_NEAR(out.value, 1.0f, 1e-6f);
    EXPECT_NEAR(out.coverage, 0.5f, 1e-6f);
}

TEST(MipSampleFilter, MeanWeightedByDistinctValues) {
    // Two distinct occupied values: 0.0 and 2.0 -> mean == 1.0.
    std::array<MipChildSample, 8> children = MakeChildren({0.0f, 2.0f});
    MipSample out = FilterMipMean(children);
    EXPECT_NEAR(out.value, 1.0f, 1e-6f);
    EXPECT_NEAR(out.coverage, 2.0f / 8.0f, 1e-6f);
}

TEST(MipSampleFilter, MeanAllEmptyYieldsZeroCoverage) {
    std::array<MipChildSample, 8> children{};  // all default: occupied=false
    MipSample out = FilterMipMean(children);
    EXPECT_NEAR(out.value, 0.0f, 1e-6f);
    EXPECT_NEAR(out.coverage, 0.0f, 1e-6f);
}

// ---------------------------------------------------------------------------
// Min-magnitude filter (SDF / FK_DISTANCE)
// ---------------------------------------------------------------------------

TEST(MipSampleFilter, MinMagnitudeUniformFill) {
    std::array<MipChildSample, 8> children;
    children.fill(MipChildSample{-3.0f, true});

    MipSample out = FilterMipMinMagnitude(children);
    EXPECT_NEAR(out.value, -3.0f, 1e-6f);
    EXPECT_NEAR(out.coverage, 1.0f, 1e-6f);
}

TEST(MipSampleFilter, MinMagnitudeHalfEmpty) {
    std::array<MipChildSample, 8> children{};
    children[0] = MipChildSample{4.0f, true};
    children[1] = MipChildSample{-1.0f, true};  // smallest |value| among occupied
    children[2] = MipChildSample{2.0f, true};
    // remaining 5 unoccupied

    MipSample out = FilterMipMinMagnitude(children);
    EXPECT_NEAR(out.value, -1.0f, 1e-6f);
    EXPECT_NEAR(out.coverage, 3.0f / 8.0f, 1e-6f);
}

TEST(MipSampleFilter, MinMagnitudeAllEmptyYieldsZeroCoverage) {
    std::array<MipChildSample, 8> children{};
    MipSample out = FilterMipMinMagnitude(children);
    EXPECT_NEAR(out.value, 0.0f, 1e-6f);
    EXPECT_NEAR(out.coverage, 0.0f, 1e-6f);
}

// Adversarial regression case (Inc1 M1 Task 1's required test): a naive mean
// filter on SDF would misplace the surface. One child is JUST outside the
// surface (small positive distance, +0.1 — surface is very close here); its
// sibling is DEEP inside the solid (very negative, -8.0). A mean would report
// (-8.0 + 0.1) / 2 = -3.95 — a strongly negative "deep interior" value at a
// node that actually straddles the surface within 0.1 units. Any traversal
// logic gating on sign or magnitude (e.g. "is this level's mip sample near
// zero, meaning the surface passes through here") would be fooled by the mean
// into believing this node is far from the surface, when in fact the correct
// (min-magnitude) answer is +0.1 — small, positive, correctly signaling
// "surface is right here, just outside."
TEST(MipSampleFilter, SdfMinMagnitudeAvoidsFalseSurfaceCrossing_AdversarialCase) {
    std::array<MipChildSample, 8> children{};
    children[0] = MipChildSample{0.1f, true};   // just outside the surface
    children[1] = MipChildSample{-8.0f, true};  // deep interior

    MipSample meanOut = FilterMipMean(children);
    MipSample minMagOut = FilterMipMinMagnitude(children);

    // The naive mean IS strongly negative (deep-interior-looking) — this is
    // exactly the false signal min-magnitude must avoid.
    EXPECT_NEAR(meanOut.value, -3.95f, 1e-6f);
    EXPECT_LT(meanOut.value, -1.0f)
        << "sanity: the naive mean really would misrepresent this node as deep-interior";

    // Min-magnitude correctly reports the near-surface child, sign preserved.
    EXPECT_NEAR(minMagOut.value, 0.1f, 1e-6f)
        << "min-magnitude must pick the child closest to the surface (smallest |value|)";
    EXPECT_GT(minMagOut.value, 0.0f)
        << "sign must be preserved: this node is (just) outside the surface, not inside";
}

// A second adversarial flavor: both children negative but very different
// magnitudes (a shallow near-surface interior sample vs a deep interior
// sample). Mean shifts the reported depth well past the shallow child's true
// distance; min-magnitude anchors to the shallow (near-surface) one.
TEST(MipSampleFilter, SdfMinMagnitudeShallowVsDeepInterior) {
    std::array<MipChildSample, 8> children{};
    children[0] = MipChildSample{-0.2f, true};  // shallow interior, near surface
    children[1] = MipChildSample{-9.0f, true};  // deep interior

    MipSample meanOut = FilterMipMean(children);
    MipSample minMagOut = FilterMipMinMagnitude(children);

    EXPECT_NEAR(meanOut.value, -4.6f, 1e-6f);
    EXPECT_NEAR(minMagOut.value, -0.2f, 1e-6f)
        << "min-magnitude must anchor to the shallow near-surface child, not the deep one";
}

// ---------------------------------------------------------------------------
// Dispatch by channel FieldKind
// ---------------------------------------------------------------------------

TEST(MipSampleFilter, DispatchPicksMinMagnitudeForDistanceKind) {
    std::array<MipChildSample, 8> children{};
    children[0] = MipChildSample{0.1f, true};
    children[1] = MipChildSample{-8.0f, true};

    MipSample out = FilterMipSample(FK_DISTANCE, children);
    EXPECT_NEAR(out.value, 0.1f, 1e-6f);
}

TEST(MipSampleFilter, DispatchPicksMeanForNonDistanceKind) {
    std::array<MipChildSample, 8> children{};
    children[0] = MipChildSample{0.1f, true};
    children[1] = MipChildSample{-8.0f, true};

    MipSample out = FilterMipSample(FK_NONE, children);
    EXPECT_NEAR(out.value, -3.95f, 1e-6f);
}

TEST(MipSampleFilter, DispatchPicksMeanForDensityKind) {
    std::array<MipChildSample, 8> children{};
    children.fill(MipChildSample{0.5f, true});

    MipSample out = FilterMipSample(FK_DENSITY, children);
    EXPECT_NEAR(out.value, 0.5f, 1e-6f);
}
