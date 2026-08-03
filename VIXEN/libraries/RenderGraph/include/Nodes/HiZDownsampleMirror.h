#pragma once
// HiZDownsampleMirror.h — Raster-proxy B1 M2: CPU mirror of shaders/HiZDownsample.comp.
//
// @shader shaders/HiZDownsample.comp
// 1:1 mirror per the gpu-shader-debug discipline: one output texel = MAX over
// its 16x16 source block of euclidean ray distances (BodyInstanceRayMarch's
// depthDistanceImage), blocks clipped at the source edge. Miss pixels carry
// kDepthMissSentinel so a tile containing any sky pixel reports "unoccluded"
// — the conservative direction for occlusion culling. Any fix here MUST be
// applied to the .comp and vice versa.

#include <algorithm>
#include <cstdint>

namespace Vixen::RenderGraph::Mirror {

// Matches the miss value BodyInstanceRayMarch.comp stores in depthDistanceImage.
inline constexpr float kDepthMissSentinel = 1.0e30f;

inline constexpr uint32_t kHiZTileSize = 16u;

inline uint32_t HiZTileCount(uint32_t pixels) {
    return (pixels + kHiZTileSize - 1u) / kHiZTileSize;
}

// Mirrors one shader invocation: the max ray distance inside tile (tileX, tileY).
// Distances are non-negative by construction, so 0 is a safe reduce identity.
inline float HiZDownsampleTexel(const float* src, uint32_t srcW, uint32_t srcH,
                                uint32_t tileX, uint32_t tileY) {
    const uint32_t x0 = tileX * kHiZTileSize;
    const uint32_t y0 = tileY * kHiZTileSize;
    const uint32_t x1 = std::min(x0 + kHiZTileSize, srcW);
    const uint32_t y1 = std::min(y0 + kHiZTileSize, srcH);
    float maxDist = 0.0f;
    for (uint32_t y = y0; y < y1; ++y) {
        for (uint32_t x = x0; x < x1; ++x) {
            maxDist = std::max(maxDist, src[static_cast<size_t>(y) * srcW + x]);
        }
    }
    return maxDist;
}

// Full-image reduce: dst is HiZTileCount(srcW) x HiZTileCount(srcH), row-major.
inline void HiZDownsampleAll(const float* src, uint32_t srcW, uint32_t srcH,
                             float* dst) {
    const uint32_t tilesX = HiZTileCount(srcW);
    const uint32_t tilesY = HiZTileCount(srcH);
    for (uint32_t ty = 0; ty < tilesY; ++ty) {
        for (uint32_t tx = 0; tx < tilesX; ++tx) {
            dst[static_cast<size_t>(ty) * tilesX + tx] =
                HiZDownsampleTexel(src, srcW, srcH, tx, ty);
        }
    }
}

}  // namespace Vixen::RenderGraph::Mirror
