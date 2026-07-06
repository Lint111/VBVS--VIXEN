#pragma once
// ResolvableLevel.h — Sparse-Mip ESVO LOD Inc1, M4a (Task 10 part 1).
//
// Closed-form finest-resolvable-octree-level formula, taking distance AND the
// active camera's FOV as independent inputs (Sparse-Mip-ESVO-LOD-Inc1-Plan-2026-07.md,
// "M4a — Resolvable-level formula"). Called once per tree per residency
// re-check, NOT per-ray — a coarser, separate calculation from the existing
// per-ray RaySizeCoefNode/shader LOD cutoff (RaySizeCoefNode.cpp), which
// applies the same screen-space-footprint reasoning per-pixel to gate
// traversal depth rather than per-tree to gate brick residency.
//
// Level convention matches LaineKarrasOctree.h's ESVOTraversalState::scale:
// level 0 = finest/leaf, increasing level = coarser, root at ESVO_MAX_SCALE.

#include <cmath>

namespace Vixen::SVO {

// Finest (smallest, lowest-L) octree level still resolvable (subtends >=
// pxThreshold pixels) at distance `distance` under the given FOV/screen
// geometry. A node at level L has linear size leafSize_m * 2^L; one pixel
// subtends fovRadians/screenHeightPx radians. Narrowing fovRadians (zoom/
// telescope) shrinks that per-pixel angle, which DECREASES the returned
// level — correctly meaning finer detail becomes worth resolving, with no
// separate telescope-mode branch: FOV is just another term in the same
// formula distance is.
inline int minResolvableLevel(
    float distance,
    float fovRadians,
    float screenHeightPx,
    float leafSize_m,
    float pxThreshold) {
    const float thetaPx = fovRadians / screenHeightPx;
    const float ratio = distance * pxThreshold * thetaPx / leafSize_m;
    return static_cast<int>(std::ceil(std::log2(ratio)));
}

}  // namespace Vixen::SVO
