// ============================================================================
// MipFallback.glsl — Sparse-Mip ESVO LOD Inc1 M3: shader-side mip sample read.
// ============================================================================
// Included by BodyInstanceRayMarch.comp AFTER:
//   • OctreeConfig struct (binding 5), g_octreeIdx, octreeConfig macro
//   • VoxelChannelFormat.glsl:  SEM_* / FK_* defines
//   • MipPoolBuffer (binding 13): float mipPool[]
//
// mipPool layout (MipBake.h SerializeMipPool, MipSample.h):
//   mipPool[mipPoolBase + (nodeIdx*channelCount + channelIdx)*2 + {0,1}]
//     -> {value, coverage} — one packed MipSample (2 floats) per (node, channel).
// nodeIdx is the SAME global ESVO node ordinal used by esvoNodes[]/state.parentPtr/
// leafDescriptorIndex — no separate addressing scheme (direction doc "same
// ordinal in the other dataset").
//
// Binary/Procedural bodies have channelCount == 0 (ConcatenateSdf's plain,
// non-mip-aware sibling never fills channels[]), so mipChannelIndex() always
// reports "absent" for them and this fallback is a structural no-op — the
// existing LOD-cutoff grey-shade path (BodyInstanceRayMarch.comp's
// `#ifdef LOD_ENABLED` block) still shades those bodies exactly as before.
// A tree that was never mip-baked (mipPool empty, bound as a 1-byte
// placeholder) is guarded by the mipPool.length() bounds check below.
// ============================================================================
#ifndef MIP_FALLBACK_GLSL
#define MIP_FALLBACK_GLSL

void recordMipSampleRead(uint bodyId, uint level) {
    if (bodyId >= 192u || level >= 16u) {
        atomicExchange(mipReadCounterOverflow, 1u);
        return;
    }
    const uint kSampleBytes = 8u; // one serialized MipSample = {value, coverage}
    uint previousBytes = atomicAdd(mipReadBytes[bodyId][level], kSampleBytes);
    uint previousSamples = atomicAdd(mipReadSamples[bodyId][level], 1u);
    if (previousBytes > 0xFFFFFFFFu - kSampleBytes || previousSamples == 0xFFFFFFFFu) {
        atomicExchange(mipReadCounterOverflow, 1u);
    }
}

// ---------------------------------------------------------------------------
// mipChannelIndex: return the channel INDEX (0..channelCount-1) for a semantic
// in the active OctreeConfig, or 0xFFFFFFFFu if absent. Distinct from
// StoredSdf.glsl's channelBaseFloats() (which returns a float pool offset,
// not a channel index) — the mip pool is addressed by channel index, not by
// the per-voxel channel-pool's float-element base.
// ---------------------------------------------------------------------------
uint mipChannelIndex(uint sem) {
    for (uint i = 0u; i < octreeConfig.channelCount; ++i) {
        if (octreeConfig.channels[i].x == sem) return i;
    }
    return 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// readMipSample: fetch the packed {value, coverage} MipSample for (nodeIdx,
// semantic) in the CURRENT octree (g_octreeIdx). Returns `missingValue` with
// coverage=0.0 if the semantic is absent from this octree's channels, or if
// the computed index falls outside the bound mipPool buffer (never mip-baked
// — bound as a 1-byte placeholder).
// ---------------------------------------------------------------------------
vec2 readMipSample(uint nodeIdx, uint sem, float missingValue) {
    uint ch = mipChannelIndex(sem);
    if (ch == 0xFFFFFFFFu) return vec2(missingValue, 0.0);

    uint base = octreeConfig.mipPoolBase + (nodeIdx * octreeConfig.channelCount + ch) * 2u;
    if (base + 1u >= mipPool.length()) return vec2(missingValue, 0.0);

    recordMipSampleRead(uint(max(g_octreeIdx, 0)), g_mipSampleLevel);
    return vec2(mipPool[base], mipPool[base + 1u]);
}

// ---------------------------------------------------------------------------
// shadeFromMipSample: v1 hard-switch shading (direction doc point 4 — no
// lerp between levels). Reads SEM_SDF's mip sample to decide occupancy
// (coverage > 0 => something was here) and SEM_COLOR's mip mean as the flat
// shade color; falls back to a neutral grey (matching the existing LOD-cutoff
// placeholder shade) when no color channel is present (binary bodies, or an
// SDF octree with no color channel baked).
// ---------------------------------------------------------------------------
bool shadeFromMipSample(uint nodeIdx, out vec3 hitColor, out vec3 hitNormal) {
    vec2 sdfSample = readMipSample(nodeIdx, SEM_SDF, 0.0);
    if (sdfSample.y <= 0.0) return false;  // no coverage: nothing baked here to shade

    vec2 colorSample = readMipSample(nodeIdx, SEM_COLOR, 0.5);
    if (colorSample.y > 0.0) {
        hitColor = vec3(colorSample.x);
        incrFarFieldColorResolved();  // batch 10: real SEM_COLOR mip sample resolved
    } else {
        hitColor = vec3(0.5);
        incrFarFieldColorFallback();  // batch 10: no SEM_COLOR coverage, flat grey
    }
    hitNormal = vec3(0.0, 1.0, 0.0);  // v1: flat/placeholder normal, no coarse-normal derivation yet
    return true;
}

// ---------------------------------------------------------------------------
// mipHasCoverage (Baked-Perf M4 Task 4.2): occlusion-only counterpart of
// shadeFromMipSample -- reads ONLY the SEM_SDF coverage float (one readMipSample
// call, no SEM_COLOR fetch) to answer "is there an occluder baked at this
// mip-fallback node," the exact same coverage test shadeFromMipSample's first
// line already gates on. Any-hit callers (TraceWorldShadow's mip-fallback leaf
// case) never need the color sample shadeFromMipSample goes on to read.
// ---------------------------------------------------------------------------
bool mipHasCoverage(uint nodeIdx) {
    vec2 sdfSample = readMipSample(nodeIdx, SEM_SDF, 0.0);
    return sdfSample.y > 0.0;
}

#endif // MIP_FALLBACK_GLSL
