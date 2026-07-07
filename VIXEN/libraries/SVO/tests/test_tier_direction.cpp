// test_tier_direction.cpp — Tiered ESVO Observer Addressing, Inc1 M2 (Task 3).
//
// Pure math tests for ComposeLocalDirection, no octree/GPU needed. Confirms
// shared-prefix composition behaves per design doc §4/§3.3: siblings compose
// in exactly one hop each, deep/root-level divergence composes through more
// hops but stays numerically well-behaved even across genuinely large
// tier-scale differences (using TierMath.h's own re-derived tier table,
// not hand-picked numbers) -- proving the "no accumulated world-space
// error" claim, not just a same-tier sanity check.

#include <gtest/gtest.h>

#include <cmath>

#include "TierDirection.h"
#include "TierMath.h"

using namespace Vixen::SVO;

namespace {

// Convenience: a TierHopFrame at a given signed offset (in [-0.5, 0.5]
// units of the tier's own [1,2) frame, i.e. relative to the frame's center
// at 1.5) and a given real-world scale in centimeters.
TierHopFrame MakeHop(glm::vec3 offsetFromCenter, double scaleCm) {
    TierHopFrame hop;
    hop.localPos = glm::vec3(1.5f, 1.5f, 1.5f) + offsetFromCenter;
    hop.scaleCm = scaleCm;
    return hop;
}

}  // namespace

// ---------------------------------------------------------------------------
// Sibling case: full shared prefix except the last hop -> exactly one
// divergent hop per address.
// ---------------------------------------------------------------------------

TEST(TierDirection, SiblingAddressesComposeInExactlyOneHopEach) {
    // Observer and object share hops [7, 2, 5] and diverge only at the
    // final (4th) hop -- siblings under the same tier-3 parent.
    TierAddress observer{7, 2, 5, 0};
    TierAddress object{7, 2, 5, 3};

    ASSERT_EQ(TierAddress::SharedPrefixLength(observer, object), 3u);
    ASSERT_EQ(observer.Depth() - 3u, 1u);
    ASSERT_EQ(object.Depth() - 3u, 1u);

    // Both are placed at the same tier's leaf scale (T2 bedrock span from
    // TierMath), offset from the shared parent's local-frame center in
    // opposite directions along X.
    const auto table = BuildTierScaleTable();
    const double leafScaleCm = table[static_cast<std::size_t>(TierIndex::T2Bedrock)].spanCm;

    std::vector<TierHopFrame> observerTail = {MakeHop(glm::vec3(-0.25f, 0.0f, 0.0f), leafScaleCm)};
    std::vector<TierHopFrame> objectTail = {MakeHop(glm::vec3(0.25f, 0.0f, 0.0f), leafScaleCm)};

    const ComposedDirection result =
        ComposeLocalDirection(observer, observerTail, object, objectTail);

    ASSERT_TRUE(result.valid);
    // Object is directly along +X from the observer (both offsets are pure
    // X, opposite sign).
    EXPECT_NEAR(result.direction.x, 1.0f, 1e-5f);
    EXPECT_NEAR(result.direction.y, 0.0f, 1e-5f);
    EXPECT_NEAR(result.direction.z, 0.0f, 1e-5f);
    // Distance = 0.5 units of the leaf scale (0.25 + 0.25).
    EXPECT_NEAR(result.distanceCm, 0.5 * leafScaleCm, leafScaleCm * 1e-9);
}

TEST(TierDirection, IdenticalAddressAndFramesYieldInvalidZeroDistance) {
    TierAddress observer{1, 1, 1};
    TierAddress object{1, 1, 1};
    // Depth equal, shared prefix == full depth -> zero-length tails on both
    // sides -> zero displacement -> not a well-defined direction.
    std::vector<TierHopFrame> emptyTail;

    const ComposedDirection result =
        ComposeLocalDirection(observer, emptyTail, object, emptyTail);

    EXPECT_FALSE(result.valid);
    EXPECT_NEAR(result.distanceCm, 0.0, 1e-12);
}

// ---------------------------------------------------------------------------
// Deep/root-level divergence: compose through more hops, across genuinely
// large tier-scale differences (galaxy-tier), and confirm no precision
// blowup.
// ---------------------------------------------------------------------------

TEST(TierDirection, RootLevelDivergenceComposesThroughFullTailPerAddress) {
    // Diverge at the very first hop (root-adjacent) -- shared prefix is 0,
    // so the ENTIRE hop chain of each address is the "divergent tail".
    TierAddress observer{1, 4, 2};
    TierAddress object{6, 0, 7, 3};

    ASSERT_EQ(TierAddress::SharedPrefixLength(observer, object), 0u);
    ASSERT_EQ(observer.Depth(), 3u);
    ASSERT_EQ(object.Depth(), 4u);

    const auto table = BuildTierScaleTable();
    // Observer's tail: System-tier hop then two T0/T1-ish hops (values
    // don't need to be physically meaningful, just distinct, well-formed
    // TierHopFrames at real tier scales).
    const double systemScaleCm = table[static_cast<std::size_t>(TierIndex::System)].spanCm;
    const double t0ScaleCm = table[static_cast<std::size_t>(TierIndex::T0Planet)].spanCm;
    const double t1ScaleCm = table[static_cast<std::size_t>(TierIndex::T1Region)].spanCm;
    const double t2ScaleCm = table[static_cast<std::size_t>(TierIndex::T2Bedrock)].spanCm;

    std::vector<TierHopFrame> observerTail = {
        MakeHop(glm::vec3(0.1f, 0.0f, 0.0f), systemScaleCm),
        MakeHop(glm::vec3(0.0f, 0.1f, 0.0f), t0ScaleCm),
        MakeHop(glm::vec3(0.0f, 0.0f, 0.1f), t1ScaleCm),
    };
    std::vector<TierHopFrame> objectTail = {
        MakeHop(glm::vec3(-0.1f, 0.0f, 0.0f), systemScaleCm),
        MakeHop(glm::vec3(0.0f, -0.1f, 0.0f), t0ScaleCm),
        MakeHop(glm::vec3(0.0f, 0.0f, -0.1f), t1ScaleCm),
        MakeHop(glm::vec3(0.05f, 0.05f, 0.05f), t2ScaleCm),
    };

    const ComposedDirection result =
        ComposeLocalDirection(observer, observerTail, object, objectTail);

    ASSERT_TRUE(result.valid);
    // Direction must be a genuine unit vector: no NaN/Inf, length ~1.
    EXPECT_TRUE(std::isfinite(result.direction.x));
    EXPECT_TRUE(std::isfinite(result.direction.y));
    EXPECT_TRUE(std::isfinite(result.direction.z));
    const float len = glm::length(result.direction);
    EXPECT_NEAR(len, 1.0f, 1e-4f);

    // Distance must be finite, positive, and dominated by the System-tier
    // hop (by far the largest scale in this tail) -- i.e. on the same
    // order of magnitude as ~0.2 * systemScaleCm (0.1 offset each side),
    // not blown up or collapsed to zero/garbage by the much-smaller T0/T1
    // contributions.
    EXPECT_TRUE(std::isfinite(result.distanceCm));
    EXPECT_GT(result.distanceCm, 0.0);
    EXPECT_GT(result.distanceCm, 0.1 * systemScaleCm);
    EXPECT_LT(result.distanceCm, 10.0 * systemScaleCm);
}

TEST(TierDirection, GalaxyTierDivergenceStaysNumericallyWellBehaved) {
    // The strongest test of the "no accumulated world-space error" claim:
    // two addresses diverging at the root, one tail dominated by the
    // Galaxy tier's span (~9.46e22 cm) and the other by the much smaller
    // T2 bedrock span (~1024 cm) -- a ~10^19x scale ratio between the two
    // tails' dominant terms. If composition accumulated error through a
    // flattened world transform, a ratio this extreme would produce NaN,
    // Inf, or a direction vector that fails to normalize; per-hop local
    // composition (this function) must still produce a clean unit vector
    // and a finite, sane distance.
    TierAddress observer{3};
    TierAddress object{9, 1};

    ASSERT_EQ(TierAddress::SharedPrefixLength(observer, object), 0u);

    const auto table = BuildTierScaleTable();
    const double galaxyScaleCm = table[static_cast<std::size_t>(TierIndex::Galaxy)].spanCm;
    const double t2ScaleCm = table[static_cast<std::size_t>(TierIndex::T2Bedrock)].spanCm;

    std::vector<TierHopFrame> observerTail = {
        MakeHop(glm::vec3(0.3f, 0.0f, 0.0f), galaxyScaleCm),
    };
    std::vector<TierHopFrame> objectTail = {
        MakeHop(glm::vec3(-0.3f, 0.0f, 0.0f), galaxyScaleCm),
        MakeHop(glm::vec3(0.0f, 0.4f, 0.0f), t2ScaleCm),
    };

    const ComposedDirection result =
        ComposeLocalDirection(observer, observerTail, object, objectTail);

    ASSERT_TRUE(result.valid);
    EXPECT_TRUE(std::isfinite(result.direction.x));
    EXPECT_TRUE(std::isfinite(result.direction.y));
    EXPECT_TRUE(std::isfinite(result.direction.z));
    const float len = glm::length(result.direction);
    EXPECT_NEAR(len, 1.0f, 1e-4f);

    // Distance should be dominated by the Galaxy-tier term (0.6 * galaxyScaleCm
    // from the two opposite 0.3-unit offsets) -- the tiny T2 contribution
    // (0.4 * ~1024 cm) is utterly negligible against a ~9.46e22 cm term, so
    // the composed distance should sit extremely close to 0.6*galaxyScaleCm,
    // not be corrupted/dominated by the small-scale hop.
    EXPECT_TRUE(std::isfinite(result.distanceCm));
    const double expectedDominant = 0.6 * galaxyScaleCm;
    EXPECT_NEAR(result.distanceCm, expectedDominant, expectedDominant * 1e-6);
}

// ---------------------------------------------------------------------------
// Robustness: mismatched/oversized tail spans are clamped, not UB.
// ---------------------------------------------------------------------------

TEST(TierDirection, OversizedTailIsClampedToActualDivergentLength) {
    // Siblings (one divergent hop each) but the caller hands in a
    // deeper-than-needed tail (e.g. built from a full path and not sliced)
    // -- ComposeLocalDirection should use only the first (depth -
    // sharedPrefixLen) entries per side, not read past that.
    TierAddress observer{7, 2, 5, 0};
    TierAddress object{7, 2, 5, 3};

    const double leafScaleCm = 1024.0;
    std::vector<TierHopFrame> observerTail = {
        MakeHop(glm::vec3(-0.25f, 0.0f, 0.0f), leafScaleCm),
        MakeHop(glm::vec3(999.0f, 999.0f, 999.0f), 1e30),  // must be ignored
    };
    std::vector<TierHopFrame> objectTail = {
        MakeHop(glm::vec3(0.25f, 0.0f, 0.0f), leafScaleCm),
    };

    const ComposedDirection result =
        ComposeLocalDirection(observer, observerTail, object, objectTail);

    ASSERT_TRUE(result.valid);
    EXPECT_NEAR(result.direction.x, 1.0f, 1e-5f);
    EXPECT_NEAR(result.distanceCm, 0.5 * leafScaleCm, leafScaleCm * 1e-9);
}
