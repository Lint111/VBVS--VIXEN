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

namespace detail {
// Baked-Perf M5 Task 5.3: shared comparator core, keyed by a caller-supplied
// world-space CENTER accessor rather than hard-coding worldPos — see the two
// public overloads below for why both still need to exist.
template <typename InstanceT, typename CenterOfFn>
inline void SortFrontToBackByCenter(std::vector<InstanceT>& instances, const glm::vec3& cameraPos,
                                     CenterOfFn centerOf) {
    std::stable_sort(instances.begin(), instances.end(),
        [&cameraPos, &centerOf](const InstanceT& a, const InstanceT& b) {
            const glm::vec3 da = centerOf(a) - cameraPos;
            const glm::vec3 db = centerOf(b) - cameraPos;
            return glm::dot(da, da) < glm::dot(db, db);
        });
}
}  // namespace detail

// Reorders `instances` in place so the ones nearest `cameraPos` come first (by
// straight-line distance from worldPos — a cheap, order-only proxy; exact distance
// value is not otherwise used). Templated on the instance type so it works directly
// on Vixen::SVO::BodyInstanceGpu (ShellOctreeGpu.h) without a library-order include
// cycle (SVO's public headers do not need to depend on ShellOctreeGpu.h here) — the
// only requirement is a `float worldPos[3]` member.
//
// Baked-Perf M5 Task 5.3: worldPos is documented as "body centre" (BodyInstanceGpu's
// own field comment) but is ACTUALLY the min-CORNER of the body's full [0,1]^3->world
// cube (BuildRenderGraph.cpp's bodyWorldPos = bodyWorldCenter - halfExtent) — sorting
// by it is a full-cube-center proxy at best, and a genuinely wrong ordering once a
// body's TIGHT allocated-brick bounds (Task 5.1) sit off-center within that cube (e.g.
// a thin wall whose true content hugs one face of its bake cube). This plain overload
// is kept, unchanged, for callers with no per-octree bounds available (this file's own
// tests use a minimal FakeInstance with no octreeIndex/configs at all) — it now simply
// forwards to the bounds-aware overload below with a same-as-before worldPos-only
// center function, so existing behavior for those callers is untouched byte-for-byte.
template <typename InstanceT>
inline void SortInstancesFrontToBack(std::vector<InstanceT>& instances, const glm::vec3& cameraPos) {
    detail::SortFrontToBackByCenter(instances, cameraPos, [](const InstanceT& inst) {
        return glm::vec3(inst.worldPos[0], inst.worldPos[1], inst.worldPos[2]);
    });
}

// Baked-Perf M5 Task 5.3: bounds-aware overload. `centerOf(inst)` returns the TRUE
// world-space center the caller wants to sort by (e.g. worldPos + tight-bounds-center
// mapped through the instance's own renderScale/localToWorld) — see
// BodyOctreeSceneNode::SortInstancesFrontToBack for the real caller, which has the
// concatenated OctreeConfig array (traceBoundsMin/Max, Task 5.1) available to compute
// it per-instance. Kept as a separate overload (not a defaulted parameter) so the
// plain worldPos-only call above stays a simple, zero-lambda call for every existing
// caller/test that has no bounds data to offer.
template <typename InstanceT, typename CenterOfFn>
inline void SortInstancesFrontToBack(std::vector<InstanceT>& instances, const glm::vec3& cameraPos,
                                      CenterOfFn centerOf) {
    detail::SortFrontToBackByCenter(instances, cameraPos, centerOf);
}

}  // namespace Vixen::SVO
