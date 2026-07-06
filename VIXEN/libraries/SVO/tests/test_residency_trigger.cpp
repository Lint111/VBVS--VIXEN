// test_residency_trigger.cpp — Sparse-Mip ESVO LOD Inc1, M4c (Task 10 part 3 + Task 11).
//
// Pure-CPU test of the combined residency trigger (ResidencyTrigger.h's
// InstanceWantsBrickResidency — the same function VulkanGraphApplication::
// UpdateBodySceneResidency calls per-instance every frame). Covers the plan's explicit
// THREE scenarios (not two):
//   (a) camera moves toward a stationary body at fixed FOV/orientation (distance-driven)
//   (b) camera stays fixed while FOV narrows (telescope/zoom-driven)
//   (c) camera stays fixed distance/FOV but ROTATES so a body moves in/out of frustum
//       (orientation-driven — (a)/(b) alone can't cover this axis)
// plus a hysteresis/no-thrash check near the frustum boundary in scenario (c), and an
// eviction-symmetry check (moving away/zooming out/rotating out all correctly drop
// residency using the same combined gate).

#include <gtest/gtest.h>
#include <cmath>

#include "ResidencyTrigger.h"

using namespace Vixen::SVO;

namespace {
constexpr float kScreenHeightPx = 1080.0f;
constexpr float kLeafSize_m     = 0.01f;
constexpr float kPxThreshold    = 1.0f;
constexpr float kNear           = 0.1f;
// 5000, not the app's real 500.0f far plane (BuildRenderGraph.cpp) -- this test's Scenario B
// bodies sit at 2000m specifically so ONLY the FOV term crosses the brick-tier threshold
// (verified: level(2000,60deg)=8, level(2000,1deg)=2, both comfortably clear of the tier-6
// boundary in each direction) -- a 500m far plane would reject that same body on frustum
// containment ALONE regardless of FOV, silently passing the test for the wrong reason (a
// real bug caught here: EXPECT_FALSE(Wants(wide,body)) looked correct but SphereIntersectsFrustum
// was actually doing the rejecting, not minResolvableLevel, before this constant was widened).
constexpr float kFar            = 5000.0f;
constexpr int   kBrickTierLevel = 6;  // matches BodyOctreeSceneNode::kShellDepth (2^6 = 64 cells/axis)
constexpr float kBodyRadius     = 24.0f;  // matches BuildRenderGraph.cpp's shared body bounding radius

float DegToRad(float deg) { return deg * (3.14159265358979323846f / 180.0f); }

// Standard camera looking down +Z from the origin, standard basis — mirrors
// test_frustum_cull.cpp's MakeStandardFrustum convention so results are directly
// comparable to that file's isolated frustum tests.
struct Camera {
    glm::vec3 pos{0.0f, 0.0f, 0.0f};
    glm::vec3 dir{0.0f, 0.0f, 1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    float fovDeg = 60.0f;
    float aspect = 16.0f / 9.0f;
};

bool Wants(const Camera& cam, const glm::vec3& bodyPos) {
    return InstanceWantsBrickResidency(
        bodyPos, kBodyRadius,
        cam.pos, cam.dir, cam.up, cam.right,
        cam.fovDeg, cam.aspect, kScreenHeightPx, kNear, kFar,
        kBrickTierLevel, kLeafSize_m, kPxThreshold);
}

// Rotate `base`'s dir/right around the camera's up axis by `yawDeg`, ALWAYS measured from
// `base`'s own original orientation (an absolute-yaw model, not a composed/incremental one)
// — composing two calls by feeding an already-rotated Camera back in as `base` would rotate
// relative to the ALREADY-ROTATED right vector, silently doubling the angle instead of
// undoing it. Each call site below passes the same fixed `base` camera and a total yaw
// angle, matching how test_frustum_cull.cpp's own MakeStandardFrustum(pad) keeps one fixed
// reference and varies a single parameter.
Camera Yawed(const Camera& base, float yawDeg) {
    const float yawRad = DegToRad(yawDeg);
    const float c = std::cos(yawRad), s = std::sin(yawRad);
    Camera out = base;
    out.dir   = glm::normalize(base.dir * c + base.right * s);
    // cross(up, dir), NOT cross(dir, up) -- must match the Camera struct's own default
    // (dir=(0,0,1), up=(0,1,0), right=(1,0,0)): cross((0,1,0),(0,0,1))=(1,0,0) is
    // consistent with that default; cross(dir,up) gives (-1,0,0), the OPPOSITE sign,
    // which silently flipped the basis's handedness on every Yawed() call including
    // yawDeg=0 (a "no-op" rotation that should reproduce the input exactly) -- caught by
    // a debug dump showing Yawed(cam, 0.0f).right == (-1,0,0) instead of (1,0,0).
    out.right = glm::normalize(glm::cross(base.up, out.dir));
    return out;
}
}  // namespace

// ---------------------------------------------------------------------------
// Scenario (a): camera moves toward a stationary body at fixed FOV/orientation
// (distance-driven).
// ---------------------------------------------------------------------------
TEST(ResidencyTrigger, ScenarioA_DistanceDriven_FarNotResidentNearResident) {
    const Camera cam;  // fixed 60deg FOV, looking down +Z from origin
    const glm::vec3 body(0.0f, 0.0f, 2000.0f);  // directly ahead, in frustum regardless of distance

    // Far away: brick tier (level 6) not worth resolving under a normal 60deg FOV.
    EXPECT_FALSE(Wants(cam, body))
        << "A body 2000m away under 60deg FOV should not want brick residency yet.";

    // Camera "moves toward" the body — same fixed FOV/orientation, decreasing distance.
    // Simulate by testing progressively closer body placements (equivalent to moving the
    // camera the same net distance, since only relative distance matters to the formula).
    const glm::vec3 bodyNear(0.0f, 0.0f, 10.0f);
    EXPECT_TRUE(Wants(cam, bodyNear))
        << "A body 10m away under 60deg FOV should resolve the brick tier and want residency.";
}

TEST(ResidencyTrigger, ScenarioA_Eviction_MovingAwayDropsResidency) {
    // Symmetry check: the same combined gate evicts on the same axis it grants on.
    const Camera cam;
    const glm::vec3 bodyNear(0.0f, 0.0f, 10.0f);
    const glm::vec3 bodyFar(0.0f, 0.0f, 2000.0f);
    ASSERT_TRUE(Wants(cam, bodyNear));
    EXPECT_FALSE(Wants(cam, bodyFar))
        << "Moving the same body far away must drop residency via the identical formula.";
}

// ---------------------------------------------------------------------------
// Scenario (b): camera stays fixed while FOV narrows (telescope/zoom-driven).
// ---------------------------------------------------------------------------
TEST(ResidencyTrigger, ScenarioB_ZoomDriven_NarrowingFovGrantsResidencyAtFixedDistance) {
    // 2000m, directly ahead, in frustum regardless of FOV (a narrower FOV only shrinks the
    // frustum cone's angle, and this body sits on the forward axis at zero lateral offset).
    // At 2000m/60deg the resolvable level is 8 (verified: ceil(log2(2000*1*radians(60)/1080/0.01))
    // = 8), above the brick tier (6) -- not resolvable. At 2000m/1deg it drops to 2 (well
    // below 6) -- resolvable. Chosen so only the FOV term crosses the tier threshold, distance
    // held fixed throughout (unlike the tier-boundary-adjacent 500m/60deg case, which the
    // plan's own worked example places AT level 6 -- exactly resolvable -- too close to the
    // boundary to safely assert "not yet resident" here).
    const glm::vec3 body(0.0f, 0.0f, 2000.0f);

    Camera wide;
    wide.fovDeg = 60.0f;
    EXPECT_FALSE(Wants(wide, body))
        << "2000m under normal 60deg FOV should not resolve the brick tier.";

    Camera narrow = wide;
    narrow.fovDeg = 1.0f;  // telescope
    EXPECT_TRUE(Wants(narrow, body))
        << "The same 2000m body under a 1deg telescope FOV must want residency -- FOV "
           "alone (camera position unchanged) flips the trigger.";
}

TEST(ResidencyTrigger, ScenarioB_Eviction_WideningFovBackOutDropsResidency) {
    const glm::vec3 body(0.0f, 0.0f, 2000.0f);
    Camera narrow;
    narrow.fovDeg = 1.0f;
    ASSERT_TRUE(Wants(narrow, body));

    Camera wide = narrow;
    wide.fovDeg = 60.0f;
    EXPECT_FALSE(Wants(wide, body))
        << "Zooming back out (widening FOV) at the same distance must drop residency.";
}

// ---------------------------------------------------------------------------
// Scenario (c): camera stays fixed distance/FOV but ROTATES so a body moves in/out of
// frustum (orientation-driven -- the case (a)/(b) alone cannot cover).
// ---------------------------------------------------------------------------
TEST(ResidencyTrigger, ScenarioC_OrientationDriven_RotatingOutOfFrustumDropsResidency) {
    const Camera cam;  // looking down +Z
    // 100m: close enough to resolve the brick tier if in frustum (level 4 <= tier 6, verified),
    // but far enough that the body's 24m bounding radius doesn't itself reach past the near
    // plane once the camera yaws 90deg (a body within ~radius of the camera origin can satisfy
    // the frustum's near-plane/side-plane test from pure proximity regardless of facing,
    // independent of the orientation axis this test is isolating — 10m was too close and
    // produced a false "still in frustum after a 90deg yaw" result during test derivation).
    const glm::vec3 body(0.0f, 0.0f, 100.0f);

    ASSERT_TRUE(Wants(cam, body))
        << "Precondition: body must want residency while directly ahead.";

    // Rotate the camera 90deg in yaw -- the body (still at +Z) is now far outside the
    // FOV cone (to the "side"/behind relative to the new facing direction), same
    // distance, same FOV -- ONLY orientation changed.
    const Camera rotated = Yawed(cam, 90.0f);
    EXPECT_FALSE(Wants(rotated, body))
        << "Rotating the camera 90deg (same position/distance/FOV) must drop residency "
           "once the body leaves the view frustum -- distance/FOV alone could not "
           "detect this, proving orientation is an independent trigger axis.";
}

TEST(ResidencyTrigger, ScenarioC_OrientationDriven_RotatingBackIntoFrustumRestoresResidency) {
    // Eviction-symmetry check for the orientation axis specifically. Both Yawed() calls
    // rotate from the SAME fixed base `cam` (an absolute-yaw model) rather than chaining
    // (composing from an already-rotated camera would rotate relative to ITS right vector,
    // silently doubling the angle instead of undoing it — see Yawed()'s own comment).
    const Camera cam;
    const glm::vec3 body(0.0f, 0.0f, 100.0f);
    const Camera rotatedAway = Yawed(cam, 90.0f);
    ASSERT_FALSE(Wants(rotatedAway, body));

    const Camera rotatedBack = Yawed(cam, 0.0f);  // back to the original orientation
    EXPECT_TRUE(Wants(rotatedBack, body))
        << "Rotating back to face the body must restore residency via the identical gate.";
}

TEST(ResidencyTrigger, ScenarioC_HysteresisPreventsThrashNearFrustumBoundary) {
    // No-thrash requirement (plan's explicit ask): panning near the hysteresis margin must
    // not flip the decision on every tiny orientation delta. depth=400/mult=1.1 was found
    // (numerically, not guessed) to be where InstanceWantsBrickResidency's internal padded
    // frustum (BuildFrustum is always called with kResidencyFrustumHysteresisDeg -- see
    // ResidencyTrigger.h) actually diverges from a hypothetically-unpadded one for a
    // kBodyRadius=24 sphere: a body's own bounding radius dominates the geometry at short
    // depths (e.g. 10m), where even a body well past the TIGHT frustum's edge still overlaps
    // it purely from proximity to the camera/near-plane -- this is why the earlier scenario
    // tests above use 100m+, not 10m, bodies for orientation checks (same lesson, this test's
    // own derivation). Level@400m/60deg=6 <= tier 6, still resolvable at this depth.
    const float depth = 400.0f;
    const float halfHFovTight =
        std::atan(std::tan((60.0f * 0.5f) * (3.14159265358979323846f / 180.0f)) * (16.0f / 9.0f));
    const float edgeX = std::tan(halfHFovTight) * depth * 1.1f;  // past the TIGHT edge, within the padded one

    const Camera cam;  // 60deg FOV, looking down +Z
    const glm::vec3 bodyAtEdge(edgeX, 0.0f, depth);

    // Wants() always evaluates through InstanceWantsBrickResidency's padded (hysteresis)
    // frustum -- this body sits just past where an UNPADDED frustum would have rejected it
    // (verified numerically), so a TRUE result here demonstrates the pad is actually doing
    // something, not merely that the function returns true for an obviously-central body.
    EXPECT_TRUE(Wants(cam, bodyAtEdge))
        << "A body just past the tight (unpadded) frustum edge, but within the padded "
           "residency-check frustum, must still be granted residency -- this is the "
           "hysteresis margin actually preventing boundary thrash near the frustum edge.";
}

// ---------------------------------------------------------------------------
// Cross-axis sanity: distance/FOV/orientation are independent inputs to one formula,
// not three special-cased branches (mirrors ResolvableLevel.h's own "no telescope-mode
// branch" framing, extended to the full trigger).
// ---------------------------------------------------------------------------
TEST(ResidencyTrigger, CrossAxis_ClosingDistanceAndNarrowingFovBothIndependentlySufficient) {
    const glm::vec3 farBody(0.0f, 0.0f, 2000.0f);
    Camera cam;
    cam.fovDeg = 60.0f;
    ASSERT_FALSE(Wants(cam, farBody));

    // Path 1: close the distance, same FOV.
    const glm::vec3 nearBody(0.0f, 0.0f, 10.0f);
    EXPECT_TRUE(Wants(cam, nearBody));

    // Path 2: keep the far distance, narrow the FOV instead.
    Camera zoomed = cam;
    zoomed.fovDeg = 1.0f;
    EXPECT_TRUE(Wants(zoomed, farBody))
        << "Narrowing FOV alone (no distance change) must be independently sufficient "
           "to grant residency on a far body, same as closing distance alone is.";
}
