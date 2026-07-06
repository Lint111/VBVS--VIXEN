// test_resolvable_level.cpp — Sparse-Mip ESVO LOD Inc1, M4a (Task 10 part 1).
//
// Pure math tests for minResolvableLevel, no octree/GPU needed. Covers both
// independent input axes per the plan's explicit requirement:
//   - fixed FOV, varying distance (the 500m-fleet-at-60deg-FOV worked example).
//   - fixed distance, varying FOV (the telescope/zoom scenario at the same
//     500m, showing narrowing FOV DECREASES the returned level).

#include <gtest/gtest.h>
#include <cmath>

#include "ResolvableLevel.h"

using namespace Vixen::SVO;

namespace {
constexpr float kLeafSize_m = 0.01f;   // 1cm-voxel configuration (level 0).
constexpr float kPxThreshold = 1.0f;
constexpr float kScreenHeightPx = 1080.0f;

float DegToRad(float deg) {
    return deg * (3.14159265358979323846f / 180.0f);
}
}  // namespace

// ---------------------------------------------------------------------------
// Axis 1: fixed FOV, varying distance.
// ---------------------------------------------------------------------------

TEST(ResolvableLevel, FixedFovVaryingDistance_500mFleetAt60DegFov) {
    // Plan's worked example: a 500m fleet under normal 60deg FOV needs
    // level >= 5.6 resolvable (raw log2, before ceil) -> ceil to 6.
    const float fovRad = DegToRad(60.0f);
    const int level = minResolvableLevel(500.0f, fovRad, kScreenHeightPx, kLeafSize_m, kPxThreshold);
    EXPECT_EQ(level, 6);

    // Raw (pre-ceil) value should be ~5.6, matching the plan's stated bound.
    const float thetaPx = fovRad / kScreenHeightPx;
    const float raw = std::log2(500.0f * kPxThreshold * thetaPx / kLeafSize_m);
    EXPECT_NEAR(raw, 5.6f, 0.05f);
}

TEST(ResolvableLevel, FixedFovVaryingDistance_MonotonicIncreaseWithDistance) {
    // Moving farther away should never make a finer level resolvable — the
    // returned level must be monotonically non-decreasing with distance.
    const float fovRad = DegToRad(60.0f);
    const int near = minResolvableLevel(100.0f, fovRad, kScreenHeightPx, kLeafSize_m, kPxThreshold);
    const int mid = minResolvableLevel(500.0f, fovRad, kScreenHeightPx, kLeafSize_m, kPxThreshold);
    const int far = minResolvableLevel(2000.0f, fovRad, kScreenHeightPx, kLeafSize_m, kPxThreshold);
    EXPECT_LE(near, mid);
    EXPECT_LE(mid, far);
}

// ---------------------------------------------------------------------------
// Axis 2: fixed distance, varying FOV (telescope/zoom scenario).
// ---------------------------------------------------------------------------

TEST(ResolvableLevel, FixedDistanceVaryingFov_500mFleetAt2DegTelescope) {
    // Plan's worked example: the same 500m fleet under a 2deg telescope FOV
    // needs only level >= 0.7 resolvable (raw log2) -> ceil to 1.
    const float fovRad = DegToRad(2.0f);
    const int level = minResolvableLevel(500.0f, fovRad, kScreenHeightPx, kLeafSize_m, kPxThreshold);
    EXPECT_EQ(level, 1);

    const float thetaPx = fovRad / kScreenHeightPx;
    const float raw = std::log2(500.0f * kPxThreshold * thetaPx / kLeafSize_m);
    EXPECT_NEAR(raw, 0.69f, 0.05f);
}

TEST(ResolvableLevel, FixedDistanceVaryingFov_NarrowingFovDecreasesLevel) {
    // The direction that matters most: zooming in (narrower fovRadians) must
    // DECREASE minResolvableLevel, meaning finer detail becomes resolvable.
    // Getting this backwards would mean zooming in requests LESS detail,
    // which is the inverted/wrong behavior the plan explicitly calls out.
    const float distance = 500.0f;
    const int wideFov = minResolvableLevel(distance, DegToRad(60.0f), kScreenHeightPx, kLeafSize_m, kPxThreshold);
    const int narrowFov = minResolvableLevel(distance, DegToRad(2.0f), kScreenHeightPx, kLeafSize_m, kPxThreshold);

    EXPECT_LT(narrowFov, wideFov);
    EXPECT_EQ(wideFov, 6);
    EXPECT_EQ(narrowFov, 1);
}

TEST(ResolvableLevel, FixedDistanceVaryingFov_MonotonicWithFov) {
    // Level should be monotonically non-decreasing as FOV widens (camera
    // position/distance held fixed) -- no special-casing for "zoomed" vs
    // "normal" view, just the same formula evaluated at different fovRadians.
    const float distance = 500.0f;
    const int narrow = minResolvableLevel(distance, DegToRad(2.0f), kScreenHeightPx, kLeafSize_m, kPxThreshold);
    const int normal = minResolvableLevel(distance, DegToRad(45.0f), kScreenHeightPx, kLeafSize_m, kPxThreshold);
    const int wide = minResolvableLevel(distance, DegToRad(90.0f), kScreenHeightPx, kLeafSize_m, kPxThreshold);
    EXPECT_LE(narrow, normal);
    EXPECT_LE(normal, wide);
}
