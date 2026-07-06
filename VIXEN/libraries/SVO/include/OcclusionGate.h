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
struct ResidentOccluder {
    glm::vec3 centre{0.0f};
    float     radius = 0.0f;
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
// A candidate coincident with the camera (zero-length ray direction) is never treated
// as occluded -- there is no meaningful direction to test, and this can only happen in
// a degenerate scene, not a real occlusion case.
inline bool IsOccludedByResidentTrees(
    const glm::vec3& cameraPos,
    const glm::vec3& candidateCentre,
    float candidateDistance,
    const std::vector<ResidentOccluder>& residentOccluders) {
    if (residentOccluders.empty() || candidateDistance <= 0.0f) {
        return false;  // nothing resident yet, or a degenerate zero-distance candidate
    }
    const glm::vec3 toCandidate = candidateCentre - cameraPos;
    const float len = glm::length(toCandidate);
    if (len <= 0.0f) {
        return false;  // candidate sits exactly at the camera -- no direction to test
    }
    const glm::vec3 rayDir = toCandidate / len;

    // A tiny epsilon so a resident tree's OWN bounding sphere (the candidate itself, if
    // it happened to already be in residentOccluders) never self-occludes from a
    // hit landing at t ~= candidateDistance due to floating-point noise.
    constexpr float kSelfHitEpsilon = 1e-3f;

    for (const ResidentOccluder& occluder : residentOccluders) {
        float hitT = 0.0f;
        if (RaySphereNearestHit(cameraPos, rayDir, occluder.centre, occluder.radius, hitT)) {
            if (hitT < candidateDistance - kSelfHitEpsilon) {
                return true;  // something already-resident blocks this candidate first
            }
        }
    }
    return false;
}

}  // namespace Vixen::SVO
