#pragma once
// ResidencyTrigger.h — Sparse-Mip ESVO LOD Inc1, M4c (Task 10 part 3 + Task 11).
//
// Combines M4a's minResolvableLevel + M4b's frustum containment into the single per-instance
// residency decision RequestBrickResidency's live trigger evaluates every camera-driven
// re-check (VulkanGraphApplication::UpdateBodySceneResidency). Factored out as a pure,
// dependency-free function (no node/GPU/graph types) so the three-scenario test (distance/
// zoom/orientation-driven) can exercise it directly, the same way ResolvableLevel.h and
// FrustumCull.h are each independently unit-testable.
//
// occluded(idx) (M4b's CPU-side occlusion gate) is DEFERRED TO INC2 (see the plan's M4b
// Progress Log) — this function's signature has no occlusion parameter at all, matching the
// documented graceful-degradation path (frustum+resolvability-only), not a stubbed-out
// always-false placeholder waiting to be wired later.

#include "FrustumCull.h"
#include "ResolvableLevel.h"

#include <glm/glm.hpp>

namespace Vixen::SVO {

// True if a single instance at `instancePos` (with bounding sphere `instanceRadius`) should
// have its tree's brick tier resident, given the current camera/view state. `brickTierLevel`
// is the octree level the brick pool actually sits at (BodyOctreeSceneNode::GetBrickTierLevel()
// in the live app; a plain int in tests).
inline bool InstanceWantsBrickResidency(
    const glm::vec3& instancePos,
    float instanceRadius,
    const glm::vec3& cameraPos,
    const glm::vec3& cameraDir,
    const glm::vec3& cameraUp,
    const glm::vec3& cameraRight,
    float fovDegrees,
    float aspect,
    float screenHeightPx,
    float nearDist,
    float farDist,
    int brickTierLevel,
    float leafSize_m,
    float pxThreshold) {
    const Frustum frustum = BuildFrustum(
        cameraPos, cameraDir, cameraUp, cameraRight,
        fovDegrees, aspect, nearDist, farDist, kResidencyFrustumHysteresisDeg);

    if (!SphereIntersectsFrustum(frustum, instancePos, instanceRadius)) {
        return false;  // out of the (padded) frustum — never needs bricks regardless of distance
    }

    const float distance = glm::distance(instancePos, cameraPos);
    const float fovRadians = fovDegrees * (3.14159265358979323846f / 180.0f);
    const int resolvable = minResolvableLevel(distance, fovRadians, screenHeightPx, leafSize_m, pxThreshold);
    return resolvable <= brickTierLevel;
}

}  // namespace Vixen::SVO
