#pragma once
// OcclusionGate.h — Sparse-Mip ESVO LOD Inc2, M3.
//
// CPU-side residency occlusion gate, deferred from Inc1 M4b (see the Inc1 plan's M4b
// section, "DEFERRED TO INC2" bullet, and the Inc2 plan's M3 milestone). Separate from
// (and upstream of) two things that already exist:
//   - FrustumCull.h / ResidencyTrigger.h: "is this candidate even worth resolving" —
//     answers frustum containment + distance/FOV resolvability, nothing about whether
//     something else already blocks it.
//   - BodyInstanceRayMarch.comp's per-ray `gridT.x > bestT` reject (InstanceSort.h's
//     SortInstancesFrontToBack feeds it): a GPU, per-PIXEL early-out that saves
//     traversal work for instances THAT ALREADY HAVE bricks resident. It does nothing
//     to stop a fully-occluded instance from being granted residency in the first
//     place — that's this file's job.
//
// Deliberately approximate, per the Inc1 note: for each frustum-passing, resolvable
// residency candidate, cast one representative ray from the camera toward the
// candidate's centre and test it against every ALREADY brick-resident tree's bounding
// sphere (not a full per-pixel occlusion query, not a new render pass -- a coarse,
// single ray-vs-sphere check per resident tree, run once per candidate per residency
// re-check). If any resident tree's sphere is hit strictly closer than the candidate's
// own distance along that ray, the candidate is occluded and should not have residency
// requested regardless of what frustum+resolvability decided.
//
// CPU is the right home at Inc1/Inc2's scale (Inc1 M4b's own note, re-confirmed here):
// this runs once per candidate tree per re-check (not per-pixel/per-ray in the render
// loop), and at current/near-term tree counts (tens to a few hundred bodies -- see
// BodyOctreeSceneNode.cpp's existing instance cap and the undertow 60-300-body target)
// an O(candidates * residentTrees) sweep of ray-vs-sphere tests is comfortably
// sub-millisecond, single-threaded. Revisit only if the nested-tree epic
// (Tiered-ESVO-Observer-Addressing-Design) pushes resident-tree counts up by orders of
// magnitude -- not a concern for this increment.

#include <glm/glm.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

namespace Vixen::SVO {

// A single already-brick-resident tree's coarse geometry, as input to the occlusion
// gate. Deliberately NOT Vixen::SVO::BodyInstanceGpu itself -- this keeps the gate a
// pure, dependency-free function (matching FrustumCull.h/ResolvableLevel.h's own
// no-node/no-GPU-type convention) testable without any node/device/graph plumbing.
// The live call site (VulkanGraphApplication::UpdateBodySceneResidency) builds this
// from BodyOctreeSceneNode::GetInstances() + the residency-bounding-radius constant
// it already uses for ResidencyTrigger.h, filtered to only currently-resident trees.
//
// `id` is an opaque caller-assigned identity (e.g. the instance's index in
// BodyOctreeSceneNode::GetInstances()) -- NOT part of the geometry test itself, used
// solely so IsOccludedByResidentTrees can skip an occluder that IS the candidate being
// tested. A resident tree/pool cannot occlude its own pending residency decision --
// that's not a coincidental self-hit to epsilon away, it is never a real occlusion,
// regardless of how close the ray-entry distance happens to land to the candidate's
// own distance. Pass kNoOccluderId (the default) when the caller has no meaningful
// identity to attach (e.g. isolated unit tests constructing occluders that are
// genuinely distinct bodies from the candidate).
constexpr int kNoOccluderId = -1;

struct ResidentOccluder {
    glm::vec3 centre{0.0f};
    float     radius = 0.0f;
    int       id     = kNoOccluderId;
};

// Ray-vs-sphere nearest-hit distance along `rayDir` (assumed normalized) from
// `rayOrigin`. Returns true and sets `outT` to the nearest non-negative hit distance
// if the ray intersects the sphere in front of the origin; false (outT untouched) if
// it misses entirely or the sphere is entirely behind the origin.
inline bool RaySphereNearestHit(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDir,
    const glm::vec3& sphereCentre,
    float sphereRadius,
    float& outT) {
    const glm::vec3 oc = rayOrigin - sphereCentre;
    const float b = glm::dot(oc, rayDir);              // rayDir assumed normalized (a=1)
    const float c = glm::dot(oc, oc) - sphereRadius * sphereRadius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0f) {
        return false;  // ray misses the sphere entirely
    }
    const float sqrtDisc = std::sqrt(discriminant);
    const float t0 = -b - sqrtDisc;  // nearer root
    const float t1 = -b + sqrtDisc;  // farther root
    const float tHit = (t0 >= 0.0f) ? t0 : t1;  // prefer the entry point; use exit if origin is inside
    if (tHit < 0.0f) {
        return false;  // sphere is entirely behind the ray origin
    }
    outT = tHit;
    return true;
}

// True if `candidateCentre` (at distance `candidateDistance` from `cameraPos`, along
// the ray `cameraPos -> candidateCentre`) is occluded by any tree in `residentOccluders`
// -- i.e. some already-resident tree's bounding sphere is hit by that same
// representative ray strictly closer than the candidate itself. `candidateDistance` is
// passed explicitly (rather than recomputed) so callers that already have it (the
// residency trigger's own distance term) don't redundantly recompute glm::distance.
//
// `candidateId` identifies the candidate being tested (e.g. its own instance index) and
// is compared against each occluder's `id`: an occluder whose id matches is skipped
// entirely, REGARDLESS of geometry. This is a correctness requirement, not a tie-break --
// a resident tree/pool can never occlude its own pending residency decision, and a naively
// built occluder set (e.g. "every currently-resident instance," which trivially includes
// the very candidate being evaluated when residency is a whole-pool decision) would
// otherwise self-occlude on every call: the ray from the camera toward the candidate's
// OWN centre always enters the candidate's OWN bounding sphere at
// `candidateDistance - radius`, which is comfortably closer than `candidateDistance`
// itself -- nowhere near float noise, so no epsilon could paper over it. Pass
// kNoOccluderId for both if the caller has no meaningful identity to compare (matches
// kNoOccluderId's own default, so two "no id" occluders/candidates never spuriously
// exclude each other by accident... except when they legitimately should not: callers
// with real distinct-body semantics should always assign real, distinct ids).
//
// A candidate coincident with the camera (zero-length ray direction) is never treated
// as occluded -- there is no meaningful direction to test, and this can only happen in
// a degenerate scene, not a real occlusion case.
inline bool IsOccludedByResidentTrees(
    const glm::vec3& cameraPos,
    const glm::vec3& candidateCentre,
    float candidateDistance,
    const std::vector<ResidentOccluder>& residentOccluders,
    int candidateId = kNoOccluderId) {
    if (residentOccluders.empty() || candidateDistance <= 0.0f) {
        return false;  // nothing resident yet, or a degenerate zero-distance candidate
    }
    const glm::vec3 toCandidate = candidateCentre - cameraPos;
    const float len = glm::length(toCandidate);
    if (len <= 0.0f) {
        return false;  // candidate sits exactly at the camera -- no direction to test
    }
    const glm::vec3 rayDir = toCandidate / len;

    // Pure floating-point-noise guard only (NOT the self-occlusion fix -- that's the
    // id comparison below). Kept tiny and purely defensive against a hit landing
    // exactly at candidateDistance due to rounding on two genuinely distinct,
    // touching/coincident bodies.
    constexpr float kNumericNoiseEpsilon = 1e-4f;

    for (const ResidentOccluder& occluder : residentOccluders) {
        if (candidateId != kNoOccluderId && occluder.id == candidateId) {
            continue;  // a tree can never occlude its own pending residency decision
        }
        float hitT = 0.0f;
        if (RaySphereNearestHit(cameraPos, rayDir, occluder.centre, occluder.radius, hitT)) {
            if (hitT < candidateDistance - kNumericNoiseEpsilon) {
                return true;  // something already-resident blocks this candidate first
            }
        }
    }
    return false;
}

}  // namespace Vixen::SVO
