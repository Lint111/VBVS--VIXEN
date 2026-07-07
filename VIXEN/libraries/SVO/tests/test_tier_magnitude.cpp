// test_tier_magnitude.cpp — Tiered ESVO Observer Addressing, Inc1 M2 (Task 4).
//
// Pure math tests for ApparentMagnitude / ApparentMagnitudeWithStaleness, no
// octree/GPU needed. Confirms brightness falls off monotonically with
// composed distance, and that the light-delay/staleness term -- documented
// as a no-op stub (see TierMagnitude.h's header comment: VIXEN has no
// existing time-simulation/light-speed system) -- is genuinely inert: it
// never changes the computed magnitude, whether disabled (0) or given a
// nonzero delay, and simply echoes the requested delay back for a future
// increment's own bookkeeping.

#include <gtest/gtest.h>

#include <cmath>

#include "TierMagnitude.h"

using namespace Vixen::SVO;

// ---------------------------------------------------------------------------
// Monotonic falloff.
// ---------------------------------------------------------------------------

TEST(TierMagnitude, MagnitudeAtZeroDistanceEqualsIntrinsicBrightness) {
    const double m = ApparentMagnitude(0.0, 100.0);
    EXPECT_NEAR(m, 100.0, 1e-9);
}

TEST(TierMagnitude, MagnitudeFallsOffMonotonicallyWithDistance) {
    const double intrinsic = 50.0;
    const double d0 = ApparentMagnitude(0.0, intrinsic);
    const double d1 = ApparentMagnitude(1.0e8, intrinsic);
    const double d2 = ApparentMagnitude(1.0e9, intrinsic);
    const double d3 = ApparentMagnitude(1.0e12, intrinsic);
    const double d4 = ApparentMagnitude(1.0e20, intrinsic);

    EXPECT_GT(d0, d1);
    EXPECT_GT(d1, d2);
    EXPECT_GT(d2, d3);
    EXPECT_GT(d3, d4);
    // Never negative.
    EXPECT_GE(d4, 0.0);
}

TEST(TierMagnitude, MagnitudeAtReferenceDistanceIsHalfIntrinsic) {
    // By construction of the formula (1 / (1 + (d/ref)^2)), d == ref should
    // yield exactly half of intrinsicBrightness.
    const double intrinsic = 80.0;
    const double m = ApparentMagnitude(kDefaultReferenceDistanceCm, intrinsic);
    EXPECT_NEAR(m, intrinsic / 2.0, 1e-9);
}

TEST(TierMagnitude, MagnitudeApproachesZeroAtExtremeDistance) {
    const double m = ApparentMagnitude(1.0e30, 1000.0);
    EXPECT_GE(m, 0.0);
    EXPECT_LT(m, 1e-6);
}

TEST(TierMagnitude, NegativeDistanceIsClampedNotNegativeMagnitude) {
    // Defensive: a caller passing a negative distance (shouldn't happen
    // given ComposedDirection::distanceCm is always >= 0, but this
    // function has no dependency on that invariant holding) should not
    // produce a magnitude larger than the zero-distance case or negative.
    const double atZero = ApparentMagnitude(0.0, 42.0);
    const double atNegative = ApparentMagnitude(-500.0, 42.0);
    EXPECT_NEAR(atNegative, atZero, 1e-9);
}

TEST(TierMagnitude, ZeroIntrinsicBrightnessStaysZeroAtAnyDistance) {
    EXPECT_NEAR(ApparentMagnitude(0.0, 0.0), 0.0, 1e-12);
    EXPECT_NEAR(ApparentMagnitude(1.0e15, 0.0), 0.0, 1e-12);
}

// ---------------------------------------------------------------------------
// Light-delay/staleness: genuinely optional, inert stub.
// ---------------------------------------------------------------------------

TEST(TierMagnitude, StalenessDefaultsToDisabled) {
    LightDelayStaleness staleness;
    EXPECT_FALSE(staleness.IsEnabled());
    EXPECT_NEAR(staleness.delaySeconds, 0.0, 1e-12);
}

TEST(TierMagnitude, StalenessDisabledProducesIdenticalMagnitudeToPlainCall) {
    const double distance = 5.0e10;
    const double intrinsic = 33.0;

    LightDelayStaleness disabled;  // delaySeconds == 0.0
    const StaleApparentMagnitude withStale =
        ApparentMagnitudeWithStaleness(distance, intrinsic, disabled);
    const double plain = ApparentMagnitude(distance, intrinsic);

    EXPECT_NEAR(withStale.magnitude, plain, 1e-9);
    EXPECT_NEAR(withStale.appliedDelaySeconds, 0.0, 1e-12);
    EXPECT_FALSE(disabled.IsEnabled());
}

TEST(TierMagnitude, NonzeroStalenessIsStillInertOnMagnitudeButEchoedBack) {
    // A nonzero delay is accepted (IsEnabled() becomes true) but per the
    // documented no-op contract (TierMagnitude.h header: no time-simulation
    // system exists yet to compute a real light-travel-delayed state from),
    // it must NOT change the computed magnitude versus the same call with
    // staleness disabled -- only the echoed appliedDelaySeconds differs.
    const double distance = 2.0e12;
    const double intrinsic = 77.0;

    LightDelayStaleness enabled;
    enabled.delaySeconds = 3600.0 * 24.0 * 365.0;  // an arbitrary "1 year" delay
    EXPECT_TRUE(enabled.IsEnabled());

    const StaleApparentMagnitude withStale =
        ApparentMagnitudeWithStaleness(distance, intrinsic, enabled);
    const double plain = ApparentMagnitude(distance, intrinsic);

    // Inert: identical magnitude regardless of the requested delay.
    EXPECT_NEAR(withStale.magnitude, plain, 1e-9);
    // But the delay is echoed back verbatim for a future increment's own
    // bookkeeping/plumbing, not silently dropped.
    EXPECT_NEAR(withStale.appliedDelaySeconds, enabled.delaySeconds, 1e-9);
}

TEST(TierMagnitude, StalenessMagnitudeStillFallsOffMonotonicallyWithDistance) {
    // Sanity: wrapping in the staleness struct doesn't break the underlying
    // monotonic falloff property proven above for the plain function.
    LightDelayStaleness staleness;
    staleness.delaySeconds = 42.0;

    const double intrinsic = 10.0;
    const double m1 = ApparentMagnitudeWithStaleness(1.0e8, intrinsic, staleness).magnitude;
    const double m2 = ApparentMagnitudeWithStaleness(1.0e10, intrinsic, staleness).magnitude;
    const double m3 = ApparentMagnitudeWithStaleness(1.0e14, intrinsic, staleness).magnitude;

    EXPECT_GT(m1, m2);
    EXPECT_GT(m2, m3);
}
