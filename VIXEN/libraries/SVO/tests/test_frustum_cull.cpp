// test_frustum_cull.cpp — Sparse-Mip ESVO LOD Inc1, M4b (Task 10 part 2, frustum piece).
//
// Pure math tests for BuildFrustum/SphereIntersectsFrustum, no octree/GPU needed.
// Covers: in-frustum containment, behind-camera rejection, off-to-the-side (beyond
// FOV cone) rejection, and the hysteresis pad actually widening the accepted cone
// (a body just outside the tight frustum is accepted once the pad is applied).

#include <gtest/gtest.h>
#include <cmath>

#include "FrustumCull.h"

using namespace Vixen::SVO;

namespace {
constexpr float kFovDeg = 60.0f;
constexpr float kAspect = 16.0f / 9.0f;
constexpr float kNear = 0.1f;
constexpr float kFar = 10000.0f;

// A frustum looking down +Z, standard basis, positioned at the origin.
Frustum MakeStandardFrustum(float extraPadDeg = 0.0f) {
    return BuildFrustum(
        glm::vec3(0.0f, 0.0f, 0.0f),   // cameraPos
        glm::vec3(0.0f, 0.0f, 1.0f),   // cameraDir (+Z)
        glm::vec3(0.0f, 1.0f, 0.0f),   // cameraUp
        glm::vec3(1.0f, 0.0f, 0.0f),   // cameraRight
        kFovDeg, kAspect, kNear, kFar, extraPadDeg);
}
}  // namespace

TEST(FrustumCull, BodyDirectlyAheadIsContained) {
    const Frustum f = MakeStandardFrustum();
    EXPECT_TRUE(SphereIntersectsFrustum(f, glm::vec3(0.0f, 0.0f, 500.0f), 1.0f));
}

TEST(FrustumCull, BodyBehindCameraIsRejected) {
    // Same distance as the contained case above, but behind the camera along -Z.
    const Frustum f = MakeStandardFrustum();
    EXPECT_FALSE(SphereIntersectsFrustum(f, glm::vec3(0.0f, 0.0f, -500.0f), 1.0f));
}

TEST(FrustumCull, BodyCloseButBehindCameraIsRejected) {
    // The plan's explicit example: "a body 5m away but directly behind the camera
    // should be exactly as brick-empty as one 10km away" — distance alone must not
    // save it; only the frustum test catches this.
    const Frustum f = MakeStandardFrustum();
    EXPECT_FALSE(SphereIntersectsFrustum(f, glm::vec3(0.0f, 0.0f, -5.0f), 1.0f));
}

TEST(FrustumCull, BodyFarOffToTheSideBeyondFovConeIsRejected) {
    const Frustum f = MakeStandardFrustum();
    // Far enough along +Z that a small lateral offset is still within FOV, but a
    // huge lateral offset at the same depth is well outside the FOV cone.
    EXPECT_FALSE(SphereIntersectsFrustum(f, glm::vec3(100000.0f, 0.0f, 500.0f), 1.0f));
}

TEST(FrustumCull, BodyWithinNarrowAngleOfCenterIsContained) {
    const Frustum f = MakeStandardFrustum();
    // Small lateral offset at a reasonable depth should stay inside a 60deg FOV cone.
    EXPECT_TRUE(SphereIntersectsFrustum(f, glm::vec3(50.0f, 0.0f, 500.0f), 1.0f));
}

TEST(FrustumCull, HysteresisPadAcceptsBodyJustOutsideTightFrustum) {
    // Pick a lateral offset that is just outside the tight (no-pad) frustum's cone,
    // then confirm the padded (residency) frustum accepts it while the tight one
    // does not — this is the actual boundary-thrash-avoidance mechanism under test.
    const Frustum tight  = MakeStandardFrustum(0.0f);
    const Frustum padded = MakeStandardFrustum(kResidencyFrustumHysteresisDeg);

    const float depth = 500.0f;
    const float halfHFovTight =
        std::atan(std::tan((kFovDeg * 0.5f) * (3.14159265358979323846f / 180.0f)) * kAspect);
    // Lateral offset placing the sphere just past the tight horizontal FOV edge at
    // this depth (a small epsilon beyond tan(halfHFovTight)*depth).
    const float edgeX = std::tan(halfHFovTight) * depth * 1.02f;

    EXPECT_FALSE(SphereIntersectsFrustum(tight, glm::vec3(edgeX, 0.0f, depth), 1.0f))
        << "Test setup invariant broken: point should be just outside the tight frustum.";
    EXPECT_TRUE(SphereIntersectsFrustum(padded, glm::vec3(edgeX, 0.0f, depth), 1.0f))
        << "Hysteresis pad should accept a body just outside the tight (unpadded) frustum.";
}

TEST(FrustumCull, BeyondFarPlaneIsRejected) {
    const Frustum f = MakeStandardFrustum();
    EXPECT_FALSE(SphereIntersectsFrustum(f, glm::vec3(0.0f, 0.0f, kFar + 1000.0f), 1.0f));
}

TEST(FrustumCull, InFrontOfNearPlaneIsRejected) {
    const Frustum f = MakeStandardFrustum();
    // Well behind the camera relative to the near plane along +fwd (i.e. between
    // camera and near plane) — use a tiny positive Z inside [0, kNear).
    EXPECT_FALSE(SphereIntersectsFrustum(f, glm::vec3(0.0f, 0.0f, kNear * 0.1f), 0.001f));
}
