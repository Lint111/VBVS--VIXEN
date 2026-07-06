// test_occlusion_gate.cpp — Sparse-Mip ESVO LOD Inc2, M3.
//
// Pure-CPU test of the CPU-side residency occlusion gate (OcclusionGate.h's
// IsOccludedByResidentTrees) -- the mechanism Inc1 M4b explicitly deferred to Inc2 (see
// the Inc1 plan's M4b section, "DEFERRED TO INC2" bullet, and its own spec for this
// exact test shape). This is a companion, CPU-only test to
// test_body_instance_occlusion_reject.cpp (the GPU per-ray fix's own test, which proves
// the SHADER stops traversing an occluded instance that already has bricks); this file
// proves the earlier decision -- an occluded candidate never gets bricks requested for
// it in the first place -- entirely without a GPU/device/graph, mirroring
// test_residency_trigger.cpp's own no-node/no-GPU convention for testing
// ResidencyTrigger.h.
//
// Three-body line-up (camera -> occluder -> occluded target), occluder already
// brick-resident:
//   - The occluded target's residency is NOT requested despite passing frustum +
//     resolvability on its own (i.e. IsOccludedByResidentTrees is the ONLY reason it's
//     rejected -- proven by first showing InstanceWantsBrickResidency alone would grant
//     it).
//   - Moving the occluder aside (or letting the target emerge past it) DOES trigger a
//     residency request once unoccluded.

#include <gtest/gtest.h>

#include "OcclusionGate.h"
#include "ResidencyTrigger.h"

using namespace Vixen::SVO;

namespace {
constexpr float kScreenHeightPx = 1080.0f;
constexpr float kLeafSize_m     = 0.01f;
constexpr float kPxThreshold    = 1.0f;
constexpr float kNear           = 0.1f;
constexpr float kFar            = 5000.0f;  // wide enough that frustum far-plane never gates these tests
constexpr int   kBrickTierLevel = 6;        // matches BodyOctreeSceneNode::kShellDepth
constexpr float kBodyRadius     = 24.0f;    // matches BuildRenderGraph.cpp's shared body bounding radius

// Standard camera looking down +Z from the origin -- same convention as
// test_residency_trigger.cpp's own Camera fixture, so results are directly comparable.
struct Camera {
    glm::vec3 pos{0.0f, 0.0f, 0.0f};
    glm::vec3 dir{0.0f, 0.0f, 1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    float fovDeg = 60.0f;
    float aspect = 16.0f / 9.0f;
};

bool WantsIgnoringOcclusion(const Camera& cam, const glm::vec3& bodyPos) {
    return InstanceWantsBrickResidency(
        bodyPos, kBodyRadius,
        cam.pos, cam.dir, cam.up, cam.right,
        cam.fovDeg, cam.aspect, kScreenHeightPx, kNear, kFar,
        kBrickTierLevel, kLeafSize_m, kPxThreshold);
}

// The combined decision the live app's UpdateBodySceneResidency makes per-instance:
// frustum+resolvability AND not-occluded-by-an-already-resident-tree.
bool WantsResidency(const Camera& cam, const glm::vec3& bodyPos,
                     const std::vector<ResidentOccluder>& residentOccluders) {
    if (!WantsIgnoringOcclusion(cam, bodyPos)) {
        return false;
    }
    const float distance = glm::distance(bodyPos, cam.pos);
    return !IsOccludedByResidentTrees(cam.pos, bodyPos, distance, residentOccluders);
}
}  // namespace

// ---------------------------------------------------------------------------
// RaySphereNearestHit: isolated unit checks of the primitive the gate is built on,
// independent of the residency-decision plumbing above.
// ---------------------------------------------------------------------------
TEST(OcclusionGate, RaySphereNearestHit_HitsDirectlyAhead) {
    float t = -1.0f;
    const bool hit = RaySphereNearestHit(
        glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 10.0f), 2.0f, t);
    ASSERT_TRUE(hit);
    EXPECT_NEAR(t, 8.0f, 1e-3f) << "nearest hit on a sphere at z=10 radius=2 along +Z must be z=8";
}

TEST(OcclusionGate, RaySphereNearestHit_MissesWhenOffAxis) {
    float t = -1.0f;
    const bool hit = RaySphereNearestHit(
        glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(100.0f, 0.0f, 10.0f), 2.0f, t);
    EXPECT_FALSE(hit) << "a sphere 100m off the ray axis (radius 2) must not register a hit";
}

TEST(OcclusionGate, RaySphereNearestHit_IgnoresSphereBehindOrigin) {
    float t = -1.0f;
    const bool hit = RaySphereNearestHit(
        glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, -10.0f), 2.0f, t);
    EXPECT_FALSE(hit) << "a sphere entirely behind the ray origin must not register a hit";
}

// ---------------------------------------------------------------------------
// IsOccludedByResidentTrees: direct tests of the coarse per-candidate occlusion check.
// ---------------------------------------------------------------------------
TEST(OcclusionGate, NotOccludedWhenNoResidentTreesYet) {
    const glm::vec3 camPos(0.0f);
    const glm::vec3 candidate(0.0f, 0.0f, 100.0f);
    EXPECT_FALSE(IsOccludedByResidentTrees(camPos, candidate, 100.0f, {}))
        << "an empty resident set must never occlude -- graceful degradation to "
           "frustum+resolvability-only, matching ResidencyTrigger.h's own convention.";
}

TEST(OcclusionGate, OccludedWhenResidentTreeBlocksTheRay) {
    const glm::vec3 camPos(0.0f);
    const glm::vec3 candidate(0.0f, 0.0f, 100.0f);
    const std::vector<ResidentOccluder> occluders = {
        ResidentOccluder{glm::vec3(0.0f, 0.0f, 30.0f), 10.0f},  // closer, directly on the ray
    };
    EXPECT_TRUE(IsOccludedByResidentTrees(camPos, candidate, 100.0f, occluders));
}

TEST(OcclusionGate, NotOccludedWhenResidentTreeIsOffToTheSide) {
    const glm::vec3 camPos(0.0f);
    const glm::vec3 candidate(0.0f, 0.0f, 100.0f);
    const std::vector<ResidentOccluder> occluders = {
        ResidentOccluder{glm::vec3(50.0f, 0.0f, 30.0f), 10.0f},  // well off the ray axis
    };
    EXPECT_FALSE(IsOccludedByResidentTrees(camPos, candidate, 100.0f, occluders));
}

TEST(OcclusionGate, NotOccludedWhenResidentTreeIsFartherThanCandidate) {
    const glm::vec3 camPos(0.0f);
    const glm::vec3 candidate(0.0f, 0.0f, 30.0f);
    const std::vector<ResidentOccluder> occluders = {
        ResidentOccluder{glm::vec3(0.0f, 0.0f, 100.0f), 10.0f},  // farther than the candidate
    };
    EXPECT_FALSE(IsOccludedByResidentTrees(camPos, candidate, 30.0f, occluders))
        << "a resident tree BEHIND the candidate (along the same ray) must not occlude it.";
}

// ---------------------------------------------------------------------------
// THE decisive test (Inc2 M3 / Inc1 M4b's own spec): three-body line-up, camera ->
// occluder -> occluded target, occluder already brick-resident.
// ---------------------------------------------------------------------------
TEST(OcclusionGate, OccludedTargetResidencyNotRequestedDespitePassingFrustumAndResolvability) {
    const Camera cam;  // 60deg FOV, looking down +Z from origin
    // Occluder: close, directly ahead, large enough to fully cover the target behind it.
    const glm::vec3 occluderPos(0.0f, 0.0f, 50.0f);
    constexpr float kOccluderRadius = 24.0f;  // == kBodyRadius, matches the shared-body convention
    // Target: farther down the SAME ray, well within frustum+resolvability on its own.
    const glm::vec3 targetPos(0.0f, 0.0f, 200.0f);

    // Precondition: absent the occlusion gate, the target WOULD be granted residency --
    // frustum+resolvability alone says yes (this is what proves the rejection below is
    // actually the occlusion gate's doing, not some other reason).
    ASSERT_TRUE(WantsIgnoringOcclusion(cam, targetPos))
        << "Precondition failed: the target must pass frustum+resolvability on its own "
           "for the occlusion-gate rejection below to be a meaningful proof.";

    const std::vector<ResidentOccluder> residentOccluders = {
        ResidentOccluder{occluderPos, kOccluderRadius},
    };

    EXPECT_FALSE(WantsResidency(cam, targetPos, residentOccluders))
        << "The occluded target's residency must NOT be requested: it passes "
           "frustum+resolvability but the already-resident occluder blocks the "
           "camera's view of it entirely.";
}

TEST(OcclusionGate, TargetResidencyRequestedOnceOccluderMovesAside) {
    const Camera cam;
    const glm::vec3 targetPos(0.0f, 0.0f, 200.0f);
    ASSERT_TRUE(WantsIgnoringOcclusion(cam, targetPos));

    // Occluder moves off to the side -- no longer on the camera->target ray at all.
    const glm::vec3 occluderMovedAside(80.0f, 0.0f, 50.0f);
    constexpr float kOccluderRadius = 24.0f;
    const std::vector<ResidentOccluder> residentOccluders = {
        ResidentOccluder{occluderMovedAside, kOccluderRadius},
    };

    EXPECT_TRUE(WantsResidency(cam, targetPos, residentOccluders))
        << "Once the occluder moves aside (off the camera->target ray), the target must "
           "be granted residency again via the identical gate -- eviction/grant symmetry.";
}

TEST(OcclusionGate, TargetResidencyRequestedOnceItEmergesPastTheOccluder) {
    // Same line-up as the decisive test, but the target now sits BEHIND (closer to the
    // camera than) the occluder along the ray -- i.e. it has "emerged past" the occluder
    // from the camera's point of view, rather than the occluder moving.
    const Camera cam;
    const glm::vec3 occluderPos(0.0f, 0.0f, 200.0f);  // now the farther body
    constexpr float kOccluderRadius = 24.0f;
    const glm::vec3 targetPos(0.0f, 0.0f, 50.0f);     // now the nearer body -- "emerged"
    ASSERT_TRUE(WantsIgnoringOcclusion(cam, targetPos));

    const std::vector<ResidentOccluder> residentOccluders = {
        ResidentOccluder{occluderPos, kOccluderRadius},
    };

    EXPECT_TRUE(WantsResidency(cam, targetPos, residentOccluders))
        << "A target nearer than every resident occluder along the same ray has "
           "emerged past them and must be granted residency.";
}

// ---------------------------------------------------------------------------
// Sanity: a lone candidate (nothing resident yet) is granted residency normally --
// the occlusion gate must degrade gracefully, not accidentally reject everything.
// ---------------------------------------------------------------------------
TEST(OcclusionGate, LoneCandidateGrantedWhenNothingResidentYet) {
    const Camera cam;
    const glm::vec3 bodyPos(0.0f, 0.0f, 50.0f);
    ASSERT_TRUE(WantsIgnoringOcclusion(cam, bodyPos));
    EXPECT_TRUE(WantsResidency(cam, bodyPos, /*residentOccluders=*/{}))
        << "With no already-resident trees, the occlusion gate must never itself "
           "reject an otherwise-qualifying candidate.";
}
