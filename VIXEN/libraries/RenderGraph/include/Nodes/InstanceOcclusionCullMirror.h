#pragma once
// InstanceOcclusionCullMirror.h — Raster-proxy B1 M3: CPU mirror of
// shaders/InstanceOcclusionCull.comp.
//
// @shader shaders/InstanceOcclusionCull.comp
// 1:1 mirror per the gpu-shader-debug discipline. One shader thread owns one
// 32-bit skip-mask word (CullMaskWord); per instance (InstanceOccluded):
//   world AABB  = inst.worldPos + inst.renderScale * (localToWorld * corner)
//                 over the 8 traceBounds corners (TraceWorld.glsl's inverse)
//   reprojection: uv = ndc*0.5+0.5, texel = uv*dims, valid iff w > 1e-5
//                 (SpatialReuseShade.comp's convention)
//   verdict:     occluded iff EVERY tile of the one-tile-dilated, clamped
//                 pixel rect has max ray distance < the AABB's nearest
//                 euclidean distance from prevCamPos
// Conservatism escapes (→ visible): non-ESVO provider, invalid traceBounds
// (the all-zero convention fails max>min), any corner behind the near plane,
// undilated rect fully offscreen, > kCullTileCap covered tiles, camera inside
// the AABB, any covered miss tile (sentinel loses every compare).
// B1 writes camera-visibility SKIP bits in the high word region [6..11]. The
// bucketed-dispatch ownership words [0..5] are preserved independently; shadows
// read only those low words.
// Any fix here MUST be applied to the .comp and vice versa.

#include "Nodes/HiZDownsampleMirror.h"  // kDepthMissSentinel, kHiZTileSize, HiZTileCount

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>

namespace Vixen::RenderGraph::Mirror {

inline constexpr uint32_t kCullTileCap = 64u;
inline constexpr float kCullMinClipW = 1e-5f;
inline constexpr uint32_t kInstanceMaskWordCount = 6u;
inline constexpr uint32_t kCameraVisibilityMaskWordBase = kInstanceMaskWordCount;

// The BodyInstance fields the cull reads (std430 record, SceneBindings.glsl).
struct CullInstance {
    glm::vec3 worldPos;
    float renderScale;
    uint32_t octreeIndex;
    uint32_t providerKind;  // 0 = Stored/ESVO; anything else is never culled
};

// The OctreeConfig fields the cull reads (Generated/OctreeConfig.glsl).
struct CullOctreeConfig {
    glm::vec3 traceBoundsMin;   // template-local [0,1]^3
    glm::vec3 traceBoundsMax;
    glm::mat4 localToWorld;
};

// Push-constant block (shader: {prevViewProj, prevCamPos, dims, count}).
struct CullParams {
    glm::mat4 prevViewProj;
    glm::vec3 prevCamPos;
    uint32_t srcWidth;    // depth image extent — the HiZ tile grid derives from it
    uint32_t srcHeight;
    uint32_t instanceCount;
};

inline void CullWorldAabb(const CullInstance& inst, const CullOctreeConfig& cfg,
                          glm::vec3& outMin, glm::vec3& outMax) {
    outMin = glm::vec3(3.4e38f);
    outMax = glm::vec3(-3.4e38f);
    for (uint32_t i = 0; i < 8u; ++i) {
        const glm::vec3 l((i & 1u) ? cfg.traceBoundsMax.x : cfg.traceBoundsMin.x,
                          (i & 2u) ? cfg.traceBoundsMax.y : cfg.traceBoundsMin.y,
                          (i & 4u) ? cfg.traceBoundsMax.z : cfg.traceBoundsMin.z);
        const glm::vec3 base = glm::vec3(cfg.localToWorld * glm::vec4(l, 1.0f));
        const glm::vec3 w = inst.worldPos + inst.renderScale * base;
        outMin = glm::min(outMin, w);
        outMax = glm::max(outMax, w);
    }
}

inline bool InstanceOccluded(const CullInstance& inst, const CullOctreeConfig& cfg,
                             const float* tileMax, const CullParams& p) {
    if (inst.providerKind != 0u) return false;

    // getOctreeTraceBounds validity (TraceWorld.glsl): max > min, within [0,1].
    const bool validBounds =
        glm::all(glm::greaterThan(cfg.traceBoundsMax, cfg.traceBoundsMin)) &&
        glm::all(glm::greaterThanEqual(cfg.traceBoundsMin, glm::vec3(0.0f))) &&
        glm::all(glm::lessThanEqual(cfg.traceBoundsMax, glm::vec3(1.0f)));
    if (!validBounds) return false;

    glm::vec3 mn, mx;
    CullWorldAabb(inst, cfg, mn, mx);

    // Nearest euclidean distance from the previous camera position to the AABB
    // — the same distance metric the march's hitT/tile maxes carry.
    const glm::vec3 d =
        glm::max(glm::max(mn - p.prevCamPos, glm::vec3(0.0f)), p.prevCamPos - mx);
    const float dNear = glm::length(d);
    if (dNear <= 0.0f) return false;  // camera inside the box

    // Project the 8 AABB corners into the previous frame.
    glm::vec2 uvMin(3.4e38f), uvMax(-3.4e38f);
    for (uint32_t i = 0; i < 8u; ++i) {
        const glm::vec3 c((i & 1u) ? mx.x : mn.x,
                          (i & 2u) ? mx.y : mn.y,
                          (i & 4u) ? mx.z : mn.z);
        const glm::vec4 clip = p.prevViewProj * glm::vec4(c, 1.0f);
        if (clip.w <= kCullMinClipW) return false;  // behind/at the near plane
        const glm::vec2 uv = glm::vec2(clip.x, clip.y) / clip.w * 0.5f + 0.5f;
        uvMin = glm::min(uvMin, uv);
        uvMax = glm::max(uvMax, uv);
    }

    const glm::vec2 dims(static_cast<float>(p.srcWidth),
                         static_cast<float>(p.srcHeight));
    const glm::vec2 pxMin = uvMin * dims;
    const glm::vec2 pxMax = uvMax * dims;
    if (pxMax.x < 0.0f || pxMax.y < 0.0f || pxMin.x >= dims.x || pxMin.y >= dims.y) {
        return false;  // undilated rect fully offscreen — no occlusion evidence
    }

    // Tile rect: floor-divide (NOT int-truncate — negative px must round down),
    // dilate one tile each side, clamp to the grid.
    const int tilesX = static_cast<int>(HiZTileCount(p.srcWidth));
    const int tilesY = static_cast<int>(HiZTileCount(p.srcHeight));
    const float ts = static_cast<float>(kHiZTileSize);
    const int t0x = glm::clamp(static_cast<int>(std::floor(pxMin.x / ts)) - 1, 0, tilesX - 1);
    const int t0y = glm::clamp(static_cast<int>(std::floor(pxMin.y / ts)) - 1, 0, tilesY - 1);
    const int t1x = glm::clamp(static_cast<int>(std::floor(pxMax.x / ts)) + 1, 0, tilesX - 1);
    const int t1y = glm::clamp(static_cast<int>(std::floor(pxMax.y / ts)) + 1, 0, tilesY - 1);

    const uint32_t covered =
        static_cast<uint32_t>(t1x - t0x + 1) * static_cast<uint32_t>(t1y - t0y + 1);
    if (covered > kCullTileCap) return false;  // big things are visible anyway

    for (int ty = t0y; ty <= t1y; ++ty) {
        for (int tx = t0x; tx <= t1x; ++tx) {
            // A miss tile's sentinel (1e30) loses this compare — sky never occludes.
            if (!(dNear > tileMax[ty * tilesX + tx])) return false;
        }
    }
    return true;
}

// One shader thread's work: compute one B1 camera-visibility word. The caller
// supplies only the existing camera-region word; bucket ownership lives in the
// separate low region and is never mixed into this result.
inline uint32_t CullMaskWord(uint32_t wordIdx, const CullInstance* instances,
                             const CullOctreeConfig* configs, const float* tileMax,
                             const CullParams& p, uint32_t existingWord) {
    uint32_t word = existingWord;
    for (uint32_t lane = 0; lane < 32u; ++lane) {
        const uint32_t instIdx = wordIdx * 32u + lane;
        if (instIdx >= p.instanceCount) break;
        const CullInstance& inst = instances[instIdx];
        if (InstanceOccluded(inst, configs[inst.octreeIndex], tileMax, p)) {
            word |= (1u << lane);
        }
    }
    return word;
}

}  // namespace Vixen::RenderGraph::Mirror
