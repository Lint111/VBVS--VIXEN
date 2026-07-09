#pragma once
// InstanceSort.h — Sparse-Mip ESVO LOD Inc1, M4b (Task 10 part 2, occlusion piece).
//
// Front-to-back CPU-side instance ordering, computed once per frame (not per-ray/
// per-pixel) when building the per-frame instance list. This is what the shader's
// per-ray `gridT.x > bestT` occlusion reject (BodyInstanceRayMarch.comp) depends on:
// bestT only reflects every CLOSER instance's hit if closer instances are actually
// visited first in the instance loop, which requires the instance ARRAY ITSELF to be
// ordered near-to-far, not a per-ray sort (that would defeat the point — one CPU sort
// per frame is cheap; a per-pixel sort would not be).

#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace Vixen::SVO {

// Reorders `instances` in place so the ones nearest `cameraPos` come first (by
// straight-line distance from worldPos — a cheap, order-only proxy; exact distance
// value is not otherwise used). Templated on the instance type so it works directly
// on Vixen::SVO::BodyInstanceGpu (ShellOctreeGpu.h) without a library-order include
// cycle (SVO's public headers do not need to depend on ShellOctreeGpu.h here) — the
// only requirement is a `float worldPos[3]` member.
template <typename InstanceT>
inline void SortInstancesFrontToBack(std::vector<InstanceT>& instances, const glm::vec3& cameraPos) {
    std::stable_sort(instances.begin(), instances.end(),
        [&cameraPos](const InstanceT& a, const InstanceT& b) {
            const glm::vec3 pa(a.worldPos[0], a.worldPos[1], a.worldPos[2]);
            const glm::vec3 pb(b.worldPos[0], b.worldPos[1], b.worldPos[2]);
            const glm::vec3 da = pa - cameraPos;
            const glm::vec3 db = pb - cameraPos;
            return glm::dot(da, da) < glm::dot(db, db);
        });
}

}  // namespace Vixen::SVO
