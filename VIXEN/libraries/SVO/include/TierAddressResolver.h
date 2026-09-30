#pragma once
// TierAddressResolver.h — Tiered ESVO CPU address-to-local-frame bridge.
//
// TierAddress is a path identity; TierRefTable supplies the per-hop links to
// the active octree. The camera's local point is already expressed in that
// addressed tree's frame and must stay separate from the path. Combining a
// meter-scale offset with a 30-AU root coordinate would discard millimeters
// even in double precision before the shader ever sees it. This helper
// resolves the active octree without flattening the chain or changing that
// tier-local point; GPU traversal still uses the existing per-hop remap.

#include "ShellOctreeGpu.h"
#include "TierAddress.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <glm/glm.hpp>

namespace Vixen::SVO {

struct ResolvedTierAddress {
    uint32_t octreeIndex = 0;
    glm::dvec3 localPosition{1.5, 1.5, 1.5};
};

// Resolve the addressed tree and pair it with a point already expressed in
// that tree's [1,2) frame. Each hop is an index local to the current
// OctreeConfig's TierRef slice. The point is carried unchanged: callers must
// keep its small local offset separate from ancestor-tier origins.
// Invalid roots, missing refs, invalid child indices, or non-positive scales
// return nullopt so callers can keep the camera at its previous valid tier.
inline std::optional<ResolvedTierAddress> ResolveTierAddressPosition(
    const ConcatenatedOctrees& octrees,
    uint32_t rootOctreeIndex,
    const TierAddress& address,
    glm::dvec3 tierLocalPosition) {
    if (rootOctreeIndex >= octrees.configs.size() ||
        octrees.count != octrees.configs.size() ||
        octrees.tierRefCounts.size() != octrees.configs.size()) {
        return std::nullopt;
    }
    if (!std::isfinite(tierLocalPosition.x) || !std::isfinite(tierLocalPosition.y) ||
        !std::isfinite(tierLocalPosition.z)) {
        return std::nullopt;
    }

    uint32_t currentOctree = rootOctreeIndex;
    for (std::size_t depth = 0; depth < address.Depth(); ++depth) {
        const uint32_t hop = address.Hop(depth);
        const uint32_t count = octrees.tierRefCounts[currentOctree];
        if (hop >= count) return std::nullopt;

        const std::size_t tableIndex =
            static_cast<std::size_t>(tierRefTableBaseOf(octrees.configs[currentOctree])) + hop;
        if (tableIndex >= octrees.tierRefTable.size()) return std::nullopt;

        const TierRef& ref = octrees.tierRefTable[tableIndex];
        if (ref.childOctreeIndex >= octrees.configs.size() ||
            !std::isfinite(ref.childScale) || ref.childScale <= 0.0f ||
            !std::isfinite(ref.childOriginLocal[0]) ||
            !std::isfinite(ref.childOriginLocal[1]) ||
            !std::isfinite(ref.childOriginLocal[2])) {
            return std::nullopt;
        }
        currentOctree = ref.childOctreeIndex;
    }

    return ResolvedTierAddress{currentOctree, tierLocalPosition};
}

}  // namespace Vixen::SVO
