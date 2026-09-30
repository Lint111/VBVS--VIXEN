// test_tier_address.cpp — Tiered ESVO Observer Addressing, Inc1 M1 (Task 1).
//
// Pure math/type tests for TierAddress, no octree/GPU needed. Covers
// construction at varying depths and the shared-prefix helper (the
// "shared-prefix = shared ancestor" primitive M2's direction/magnitude
// composition will depend on) for identical, partially-overlapping, and
// fully-divergent address pairs.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>

#include "TierAddress.h"
#include "TierAddressResolver.h"

using namespace Vixen::SVO;

// ---------------------------------------------------------------------------
// Construction at varying depths.
// ---------------------------------------------------------------------------

TEST(TierAddress, DefaultConstructedIsRootWithZeroDepth) {
    TierAddress root;
    EXPECT_EQ(root.Depth(), 0u);
}

TEST(TierAddress, PushHopIncreasesDepthAndStoresValue) {
    TierAddress addr;
    addr.PushHop(3);
    ASSERT_EQ(addr.Depth(), 1u);
    EXPECT_EQ(addr.Hop(0), 3u);

    addr.PushHop(5);
    ASSERT_EQ(addr.Depth(), 2u);
    EXPECT_EQ(addr.Hop(0), 3u);
    EXPECT_EQ(addr.Hop(1), 5u);
}

TEST(TierAddress, InitializerListConstructsExpectedDepthAndHops) {
    // A 5-hop address: galaxy-cell -> system -> orbit/planet-cell -> region
    // -> brick, matching the design doc §4's "4-5 entries typical" sketch.
    TierAddress addr{7, 2, 5, 0, 3};
    ASSERT_EQ(addr.Depth(), 5u);
    EXPECT_EQ(addr.Hop(0), 7u);
    EXPECT_EQ(addr.Hop(1), 2u);
    EXPECT_EQ(addr.Hop(2), 5u);
    EXPECT_EQ(addr.Hop(3), 0u);
    EXPECT_EQ(addr.Hop(4), 3u);
}

TEST(TierAddress, PushHopBeyondCapacityIsClampedNotUB) {
    TierAddress addr;
    for (std::size_t i = 0; i < kMaxTierAddressDepth + 4; ++i) {
        addr.PushHop(static_cast<uint32_t>(i));
    }
    // Defensive clamp: depth never exceeds the fixed capacity.
    EXPECT_EQ(addr.Depth(), kMaxTierAddressDepth);
}

// ---------------------------------------------------------------------------
// Equality.
// ---------------------------------------------------------------------------

TEST(TierAddress, EqualityHoldsForIdenticalHopSequences) {
    TierAddress a{1, 2, 3};
    TierAddress b{1, 2, 3};
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a != b);
}

TEST(TierAddress, EqualityFailsForDifferentDepthEvenWithSharedPrefix) {
    TierAddress a{1, 2, 3};
    TierAddress b{1, 2};
    EXPECT_NE(a, b);
}

TEST(TierAddress, EqualityFailsForSameDepthDifferentHop) {
    TierAddress a{1, 2, 3};
    TierAddress b{1, 2, 4};
    EXPECT_NE(a, b);
}

// ---------------------------------------------------------------------------
// Shared-prefix length ("shared-prefix = shared ancestor", design doc §4).
// ---------------------------------------------------------------------------

TEST(TierAddress, SharedPrefixLength_SelfComparisonReturnsFullDepth) {
    TierAddress addr{4, 1, 6, 2};
    EXPECT_EQ(TierAddress::SharedPrefixLength(addr, addr), addr.Depth());
}

TEST(TierAddress, SharedPrefixLength_IdenticalAddressesReturnFullDepth) {
    TierAddress a{4, 1, 6, 2};
    TierAddress b{4, 1, 6, 2};
    EXPECT_EQ(TierAddress::SharedPrefixLength(a, b), 4u);
}

TEST(TierAddress, SharedPrefixLength_FullyDivergentAtRootReturnsZero) {
    // Different galaxy-cell hop at tier 0 -- diverge immediately.
    TierAddress a{9, 1, 2};
    TierAddress b{0, 1, 2};
    EXPECT_EQ(TierAddress::SharedPrefixLength(a, b), 0u);
}

TEST(TierAddress, SharedPrefixLength_PartialOverlapMatchesCommonPrefixOnly) {
    // Share tier 0-1 (galaxy-cell, system), diverge at tier 2 (planet-cell).
    TierAddress a{7, 2, 5, 0};
    TierAddress b{7, 2, 9, 3};
    EXPECT_EQ(TierAddress::SharedPrefixLength(a, b), 2u);
}

TEST(TierAddress, SharedPrefixLength_SiblingsDivergeOnlyAtLastHop) {
    // Identical prefix except the final (finest) hop -- siblings under the
    // same parent at every tier above the leaf.
    TierAddress a{7, 2, 5, 0};
    TierAddress b{7, 2, 5, 1};
    EXPECT_EQ(TierAddress::SharedPrefixLength(a, b), 3u);
}

TEST(TierAddress, SharedPrefixLength_OnePrefixOfTheOtherReturnsShorterDepth) {
    // b is exactly a's prefix (b is an ancestor cell of a).
    TierAddress a{7, 2, 5, 0};
    TierAddress b{7, 2};
    EXPECT_EQ(TierAddress::SharedPrefixLength(a, b), 2u);
    // Symmetric.
    EXPECT_EQ(TierAddress::SharedPrefixLength(b, a), 2u);
}

TEST(TierAddress, SharedPrefixLength_BothRootsReturnsZero) {
    TierAddress a;
    TierAddress b;
    EXPECT_EQ(TierAddress::SharedPrefixLength(a, b), 0u);
}

// ---------------------------------------------------------------------------
// Serialization form (stable, not the final wire format -- see header comment).
// ---------------------------------------------------------------------------

TEST(TierAddress, ToStringIsStableAndRoundTripDistinguishable) {
    TierAddress a{7, 2, 5, 0};
    TierAddress b{7, 2, 5, 1};
    EXPECT_NE(a.ToString(), b.ToString());
    EXPECT_EQ(a.ToString(), "4:7.2.5.0");
}

TEST(TierAddress, ToStringOfRootIsZeroDepthEmptyHops) {
    TierAddress root;
    EXPECT_EQ(root.ToString(), "0:");
}

TEST(TierAddressResolver, ThirtyAuAddressKeepsCloseupOffsetInTheActiveTier) {
    constexpr double kAuMeters = 149'597'870'700.0;
    constexpr double kSystemSpanMeters = 120.0 * kAuMeters;
    constexpr double kChildScale = 1.0 / 1024.0;
    constexpr double kBodyRadiusMeters = 4.0;
    constexpr double kCameraClearanceMeters = 1.0;
    constexpr std::size_t kTierDepth = 4;
    const double tierSpanMeters = kSystemSpanMeters * std::pow(kChildScale, kTierDepth);

    // Four concrete, per-parent TierRef slices: the first child's origin is
    // 30 AU from the system center; later children are centered in their own
    // parent's [1,2) frame. These are VIXEN in-process refs, not a wire shape.
    ConcatenatedOctrees octrees;
    octrees.count = static_cast<uint32_t>(kTierDepth + 1);
    octrees.configs.resize(kTierDepth + 1);
    octrees.tierRefCounts.assign(kTierDepth + 1, 0u);
    octrees.tierRefCounts[0] = 1;
    for (std::size_t i = 0; i < kTierDepth; ++i) {
        TierRef ref{};
        ref.childOctreeIndex = static_cast<uint32_t>(i + 1);
        ref.childOriginLocal[0] = (i == 0) ? 1.75f : 1.5f;
        ref.childOriginLocal[1] = 1.5f;
        ref.childOriginLocal[2] = 1.5f;
        ref.childScale = static_cast<float>(kChildScale);
        octrees.tierRefTable.push_back(ref);
        if (i + 1 < kTierDepth) octrees.tierRefCounts[i + 1] = 1;
        setTierRefTableBase(octrees.configs[i], static_cast<uint32_t>(i));
    }

    const TierAddress cameraAddress{0, 0, 0, 0};
    EXPECT_NEAR(tierSpanMeters, 16.326, 0.01);
    EXPECT_NEAR((static_cast<double>(octrees.tierRefTable[0].childOriginLocal[0]) - 1.5) * kSystemSpanMeters,
                30.0 * kAuMeters, 1e-6);

    // The body center sits at +30 AU in the system's X axis. Its camera point
    // is carried independently in the addressed tier: four 2^-10 hops make
    // that tier about 16.3m wide, so this local float retains sub-mm clearance.
    const double cameraOffsetFromBodyCenterMeters = kBodyRadiusMeters + kCameraClearanceMeters;
    const glm::dvec3 cameraTierLocalPosition{
        1.5,
        1.5,
        1.5 + cameraOffsetFromBodyCenterMeters / tierSpanMeters,
    };
    const auto resolved = ResolveTierAddressPosition(octrees, 0, cameraAddress, cameraTierLocalPosition);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->octreeIndex, 4u);

    EXPECT_DOUBLE_EQ(resolved->localPosition.z, cameraTierLocalPosition.z);
    const float cameraZForShader = static_cast<float>(resolved->localPosition.z);
    const float surfaceZForShader = static_cast<float>(1.5 + kBodyRadiusMeters / tierSpanMeters);
    const double measuredClearanceMeters =
        (static_cast<double>(cameraZForShader) - static_cast<double>(surfaceZForShader)) * tierSpanMeters;
    const double clearanceErrorMeters = std::abs(measuredClearanceMeters - kCameraClearanceMeters);
    EXPECT_LT(clearanceErrorMeters, 0.001)
        << "float tier-local camera/surface error at 30 AU = " << clearanceErrorMeters << " m";
    std::printf("[TIER ADDRESS 30 AU] tier span %.9f m, camera clearance %.9f m, float error %.12g m\n",
                tierSpanMeters, measuredClearanceMeters, clearanceErrorMeters);
}
