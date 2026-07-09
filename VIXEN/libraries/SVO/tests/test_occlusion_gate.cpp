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
// frustum+resolvability AND not-occluded-by-an-already-resident-tree. candidateId
// mirrors the live wiring's own instance-index id (kNoOccluderId default is fine for
// tests whose occluder set never includes the candidate itself).
bool WantsResidency(const Camera& cam, const glm::vec3& bodyPos,
                     const std::vector<ResidentOccluder>& residentOccluders,
                     int candidateId = kNoOccluderId) {
    if (!WantsIgnoringOcclusion(cam, bodyPos)) {
        return false;
    }
    const float distance = glm::distance(bodyPos, cam.pos);
    return !IsOccludedByResidentTrees(cam.pos, bodyPos, distance, residentOccluders, candidateId);
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

// ---------------------------------------------------------------------------
// Self-occlusion regression (found by independent Opus validation of the live
// wiring, not by these tests originally -- see the Inc2 plan's M3 Progress Log
// "self-occlusion thrash" note). VulkanGraphApplication::UpdateBodySceneResidency
// builds `residentOccluders` from ALL current instances whenever the shared pool was
// last resident, then tests EACH candidate against that SAME full set -- which
// trivially includes the candidate's own bounding sphere. Without id-based exclusion,
// the ray from the camera toward a candidate's own centre always "hits" that same
// candidate's own sphere at `candidateDistance - radius`, comfortably closer than
// `candidateDistance` itself -- nowhere near float noise, so no epsilon tweak could
// fix it. Every earlier test above avoided this because its occluder set was always a
// genuinely DIFFERENT body from the candidate; these tests deliberately reproduce the
// live wiring's actual construction (full instance list, candidate included).
// ---------------------------------------------------------------------------
TEST(OcclusionGate, RaySphereNearestHit_ConfirmsTheSelfHitMechanismIsReal) {
    // Isolated confirmation of the root-cause mechanism itself (not yet the fix): a
    // ray toward a sphere's OWN centre always hits that sphere's own surface at
    // distance-radius, well short of the centre. This is what makes naive full-list
    // occluder construction dangerous without identity exclusion.
    const glm::vec3 camPos(0.0f);
    const glm::vec3 ownCentre(0.0f, 0.0f, 100.0f);
    constexpr float kRadius = 24.0f;
    float hitT = 0.0f;
    ASSERT_TRUE(RaySphereNearestHit(camPos, glm::normalize(ownCentre), ownCentre, kRadius, hitT));
    EXPECT_NEAR(hitT, 100.0f - kRadius, 1e-2f)
        << "a ray toward a sphere's own centre must hit its own surface at "
           "distance-radius -- confirming this is a real geometric self-hit, not a "
           "coincidental float-noise artifact an epsilon could paper over.";
}

TEST(OcclusionGate, CandidateDoesNotSelfOccludeWhenPresentInItsOwnOccluderSet) {
    // Direct reproduction of the validator's 3-body probe: a full occluder set built
    // from every instance (as the live wiring does), each candidate assigned an id
    // matching its own index -- exactly what UpdateBodySceneResidency constructs.
    // Lateral offsets (+-100, up to 60 vertical) are deliberately wide relative to the
    // 24-unit body radius so NONE of these three bodies' own camera-rays pass through
    // a DIFFERENT body's sphere (verified numerically) -- isolating this test to prove
    // ONLY the self-occlusion-exclusion behavior, not incidentally exercising genuine
    // cross-body occlusion (that's the next test's job).
    const Camera cam;
    const std::vector<glm::vec3> positions = {
        glm::vec3(0.0f, 0.0f, 50.0f),
        glm::vec3(100.0f, 0.0f, 60.0f),
        glm::vec3(-100.0f, 60.0f, 70.0f),
    };
    constexpr float kRadius = 24.0f;

    std::vector<ResidentOccluder> residentOccluders;
    for (size_t i = 0; i < positions.size(); ++i) {
        residentOccluders.push_back(ResidentOccluder{positions[i], kRadius, static_cast<int>(i)});
    }

    // Precondition: each body qualifies on frustum+resolvability alone (otherwise a
    // "not occluded" result would be meaningless -- it could just be failing the OTHER
    // gate). All three sit directly ahead at moderate range, so this should hold.
    for (size_t i = 0; i < positions.size(); ++i) {
        ASSERT_TRUE(WantsIgnoringOcclusion(cam, positions[i]))
            << "precondition failed for body " << i;
    }

    // THE bug the validator found: without id exclusion, ALL THREE would report
    // occluded=true (each self-hits its own sphere). With id exclusion, each body is
    // tested against the OTHER two only -- and since none of these three bodies sits
    // behind another along its own camera ray (they're spread laterally, not
    // collinear), none should be occluded by a genuinely different resident tree
    // either.
    for (size_t i = 0; i < positions.size(); ++i) {
        const float distance = glm::distance(positions[i], cam.pos);
        EXPECT_FALSE(IsOccludedByResidentTrees(
            cam.pos, positions[i], distance, residentOccluders, static_cast<int>(i)))
            << "body " << i << " must not self-occlude when its own bounding sphere "
               "is (correctly) present in residentOccluders under its own id -- a "
               "resident tree/pool can never occlude its own pending residency "
               "decision.";
    }
}

TEST(OcclusionGate, CandidateStillCorrectlyOccludedByADifferentResidentTreeWhenIdsDiffer) {
    // Guards against an over-broad fix: id exclusion must skip ONLY the matching id,
    // not accidentally suppress real occlusion by a genuinely different body.
    const Camera cam;
    const glm::vec3 occluderPos(0.0f, 0.0f, 50.0f);
    const glm::vec3 targetPos(0.0f, 0.0f, 200.0f);  // same ray, farther -- genuinely occluded
    constexpr float kRadius = 24.0f;
    ASSERT_TRUE(WantsIgnoringOcclusion(cam, targetPos));

    const std::vector<ResidentOccluder> residentOccluders = {
        ResidentOccluder{occluderPos, kRadius, /*id=*/0},
        ResidentOccluder{targetPos, kRadius, /*id=*/1},  // the target's own entry, id=1
    };

    // Target (id=1) tested against the full set: must skip its OWN entry (id=1) but
    // still be correctly occluded by the DIFFERENT resident tree at id=0.
    const float distance = glm::distance(targetPos, cam.pos);
    EXPECT_TRUE(IsOccludedByResidentTrees(cam.pos, targetPos, distance, residentOccluders, /*candidateId=*/1))
        << "excluding the candidate's own id must not also suppress genuine "
           "occlusion by a DIFFERENT resident tree in the same set.";
}
