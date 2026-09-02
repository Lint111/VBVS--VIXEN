#pragma once
// ============================================================================
// ShellDerive.h — Surface-Shell ESVO cache derivation (increment 1, CPU core).
//
// Derives, from an already-baked full-interior Stored-SDF octree
// (ConcatenatedOctrees produced by ConcatenateSdf), the minimal SOUND set of
// bricks the iso-surface ray march can ever reach, and compacts ONLY those
// bricks into a smaller channel pool + lookup table.
//
// This is the SINGLE SOURCE OF TRUTH for the shell-derivation algorithm. The
// planned GPU compute pass (ShellDerive.comp) mirrors this bit-for-bit; the CPU
// path here is what the double-buffer bootstrap and the correctness tests use.
//
// -------------------------------------------------------------------------
// Sound reachability invariant (why this is lossless for the render path):
//
//   Allocate a brick B iff
//       B contains a zero-iso crossing (SURFACE), OR
//       B is a 26-neighbour of a SURFACE brick (dilate26).
//
//   The render loop only ever visits a brick that _advanceToNextSdfLeaf hands
//   back for a ray tracking the iso-surface, and sampleSdfTrilinear's 8-corner
//   stencil reads up to one voxel OUTSIDE a brick. The 26-neighbour dilation of
//   the true surface set therefore guarantees no stencil reaches an unallocated
//   neighbour and no traversal hop lands on a dropped brick. Interior-solid and
//   far-exterior bricks are dropped — that is the bandwidth win.
//
//   SURFACE test (sign-and-magnitude crossing, NOT a |sd|<band proximity band):
//       over the 512 stored SDF distances of a brick,
//       min < +halfBrickDiag && max > -halfBrickDiag.
//   A fully-interior brick has all distances <= -halfBrickDiag (max fails);
//   a far-exterior brick has all distances >= +halfBrickDiag (min fails).
// ============================================================================
#include "ShellOctreeGpu.h"   // ConcatenatedOctrees, OctreeConfig, SEM_SDF, kVoxelsPerBrick
#include "KernelDispatch/Dispatcher.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Vixen::SVO {

// One oct8 normal is two quantized bytes (16 bits total), packed two normals
// per float word in the existing channelPool SSBO.  This is the conventional
// octahedral 16-bit representation: it has no per-brick decode frame and keeps
// interpolation continuous across the apron.
inline constexpr uint32_t kShellNormalStrideFloats =
    SerializedOctree::kVoxelsPerBrick / 2u;
inline constexpr float kShellNormalSentinel = 100.0f;

inline uint16_t EncodeShellNormalOct16(glm::vec3 normal) {
    const float length = glm::length(normal);
    if (!(length > 1.0e-6f) || !std::isfinite(length)) normal = glm::vec3(0.0f, 1.0f, 0.0f);
    else normal /= length;

    const float invL1 = 1.0f / (std::fabs(normal.x) + std::fabs(normal.y) + std::fabs(normal.z));
    glm::vec2 p(normal.x * invL1, normal.y * invL1);
    if (normal.z < 0.0f) {
        p = glm::vec2((1.0f - std::fabs(p.y)) * (p.x < 0.0f ? -1.0f : 1.0f),
                      (1.0f - std::fabs(p.x)) * (p.y < 0.0f ? -1.0f : 1.0f));
    }
    const auto quantize = [](float v) -> uint16_t {
        const float q = std::fmin(std::fmax(v * 0.5f + 0.5f, 0.0f), 1.0f) * 255.0f;
        return static_cast<uint16_t>(std::lround(q));
    };
    return static_cast<uint16_t>(quantize(p.x) | (static_cast<uint16_t>(quantize(p.y)) << 8u));
}

inline glm::vec3 DecodeShellNormalOct16(uint16_t packed) {
    glm::vec2 p(static_cast<float>(packed & 0xffu) / 255.0f * 2.0f - 1.0f,
                static_cast<float>((packed >> 8u) & 0xffu) / 255.0f * 2.0f - 1.0f);
    float z = 1.0f - std::fabs(p.x) - std::fabs(p.y);
    if (z < 0.0f) {
        p = glm::vec2((1.0f - std::fabs(p.y)) * (p.x < 0.0f ? -1.0f : 1.0f),
                      (1.0f - std::fabs(p.x)) * (p.y < 0.0f ? -1.0f : 1.0f));
    }
    const glm::vec3 normal(p.x, p.y, z);
    const float length = glm::length(normal);
    return length > 1.0e-6f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
}

// ---------------------------------------------------------------------------
// ShellProxyAabb — raster-proxy artifact element (hybrid slice A).
//
// One per SHELL brick, in the brick's TEMPLATE-LOCAL [0,1]^3 frame (the same
// convention OctreeConfig.traceBoundsMin/Max and TraceWorld's body-local space
// use): min = gridCoord/bpa, max = (gridCoord+1)/bpa. Instance transforms
// (worldPos/renderScale + localToWorld) are composed at draw time, so this
// artifact is instance-independent. std430-friendly 32-byte layout.
//
// CPU-side artifact generation only — ShellDerive.comp mirrors the surface/
// shell CLASSIFICATION bit-for-bit, not this emission.
// ---------------------------------------------------------------------------
struct ShellProxyAabb {
    float    minLocal[3];
    uint32_t brickId;       // source brick id (== shellLookup[slot])
    float    maxLocal[3];
    uint32_t octreeIndex;   // owning octree's index in the concatenation
};
static_assert(sizeof(ShellProxyAabb) == 32, "ShellProxyAabb must stay 32B (std430 pair of vec4-ish rows)");

// ---------------------------------------------------------------------------
// ShellDeriveResult — the compacted cache slot + provenance for verification.
// ---------------------------------------------------------------------------
struct ShellDeriveResult {
    // Compacted SoA pool holding ONLY shell bricks, restrided contiguously.
    // The source channels retain their original offsets; when normals are
    // enabled, an oct16 tail follows them and shellBrickStrideFloats expands.
    std::vector<uint8_t> shellData;      // shellBrickCount * shellBrickStrideFloats * 4 bytes

    // Compacted lookup: shellSlot -> sourceBrickId. Length == shellBrickCount.
    // This is what a shell-aware brickGridLookup remaps to; increment 1 keeps
    // the mapping explicit so the double-buffer + dirty paths can rewrite it.
    std::vector<uint32_t> shellLookup;   // shellSlot -> sourceBrickId

    // Grid-space remap: uint32[bpa^3], flat = gx + gy*bpa + gz*bpa^2, value ==
    // the SHELL SLOT for that grid cell's brick (0xFFFFFFFF == empty/dropped).
    // This is a DROP-IN replacement for the source octree's brickGridLookup at
    // shader binding 12: the march reads `brickIdx = brickLookup[flat]` then
    // `channelPool[poolBrickBase + brickIdx*stride + ...]`. Binding shellData at
    // 11 and shellGridLookup at 12 (with the octree's poolBrickBase == 0) makes
    // the render read the COMPACT pool with the shader's EXISTING addressing —
    // no shader logic change. A cell whose brick was dropped maps to 0xFFFFFFFF,
    // which the march already treats as an unallocated (empty) brick.
    std::vector<uint8_t>  shellGridLookup;   // uint32[bpa^3] grid-coord -> shellSlot (as bytes)

    // Per-source-brick classification (length == source brickCount).
    std::vector<uint8_t> surface;        // 1 == contains a zero-iso crossing
    std::vector<uint8_t> shell;          // 1 == SURFACE U dilateN(SURFACE)

    // Inverse of shellLookup: sourceBrickId -> shellSlot, or 0xFFFFFFFF if the
    // brick was dropped. Length == source brickCount. Lets a dirty revalidate
    // find the shell slot to overwrite for a given source brick.
    std::vector<uint32_t> sourceToShellSlot;

    // Raster-proxy artifact: one local-space AABB per shell brick, in shellLookup
    // order. Invariant under RevalidateShellBricks (value edits never move a
    // brick's box); membership changes re-derive and re-emit the whole list.
    std::vector<ShellProxyAabb> proxyAabbs;

    uint32_t sourceBrickCount = 0;
    uint32_t surfaceBrickCount = 0;
    uint32_t shellBrickCount = 0;       // == shellLookup.size()
    uint32_t brickStrideFloats = 0;
    uint32_t shellBrickStrideFloats = 0; // source stride + optional normal tail
    uint32_t normalOffsetFloats = 0;
    uint32_t normalStrideFloats = 0;
    bool normalsBaked = false;

    // Bytes of the source vs the compacted pool — the measured bandwidth win.
    uint64_t sourcePoolBytes = 0;
    uint64_t shellPoolBytes  = 0;
    uint64_t normalPoolBytes = 0;
};

// ---------------------------------------------------------------------------
// ShellDeriveParams — configuration mirroring BodyOctreeSceneNode::shellDilation_.
// ---------------------------------------------------------------------------
struct ShellDeriveParams {
    // Brick-layer dilation of the SURFACE set along all 26 directions.
    //   1 = minimal sound invariant (26-neighbour of surface).  Default.
    //   2..3 = thicker shells for future effects; still sound (superset).
    uint32_t shellDilation = 1u;
    // 0 = the shared executor's default; positive values are the deterministic
    // worker-count gate used by the serial/2/N equivalence tests.
    uint32_t workerCount = 0u;
    // Flag-gated payload generation. The old channel pool is untouched when false.
    bool bakeNormals = false;
};

namespace detail {

// Half the space diagonal of an 8^3 brick, in voxel units:
//   0.5 * sqrt(3) * brickSide.  brickSide comes from the config's brickSize.
inline float HalfBrickDiag(uint32_t brickSide) {
    return 0.5f * 1.7320508075688772f * static_cast<float>(brickSide);
}

// Read the 512 SDF distances of source brick `bi` from a concatenated pool.
//   poolFloats  — reinterpreted channelPool as floats
//   poolBase    — this octree's poolBrickBase (float offset; 0 for octree 0)
//   stride      — brickStrideFloats
//   sdfBase     — channelBaseFloats for SEM_SDF (0 in the canonical layout)
// Fills out min/max over the brick's 512 stored distances.
inline void BrickSdfMinMax(const float* poolFloats, size_t poolFloatCount,
                           uint32_t poolBase, uint32_t stride, uint32_t sdfBase,
                           uint32_t bi, float& outMin, float& outMax) {
    outMin =  std::numeric_limits<float>::max();
    outMax = -std::numeric_limits<float>::max();
    const size_t brickStart =
        static_cast<size_t>(poolBase) + static_cast<size_t>(bi) * stride + sdfBase;
    for (uint32_t v = 0; v < SerializedOctree::kVoxelsPerBrick; ++v) {
        const size_t idx = brickStart + v;
        if (idx >= poolFloatCount) break;
        const float d = poolFloats[idx];
        if (d < outMin) outMin = d;
        if (d > outMax) outMax = d;
    }
}

inline uint32_t LookupTableOffset(const ConcatenatedOctrees& cat, uint32_t octreeIdx) {
    uint32_t offset = 0u;
    for (uint32_t k = 0; k < octreeIdx; ++k) {
        const uint32_t bpa = cat.configs[k].bricksPerAxis;
        offset += bpa * bpa * bpa;
    }
    return offset;
}

inline std::vector<uint32_t> BuildBrickToGrid(const ConcatenatedOctrees& cat,
                                              uint32_t octreeIdx,
                                              uint32_t brickCount) {
    const uint32_t bpa = static_cast<uint32_t>(cat.configs[octreeIdx].bricksPerAxis);
    const uint32_t tableSize = bpa * bpa * bpa;
    const uint32_t tableOffset = LookupTableOffset(cat, octreeIdx);
    const uint32_t* lookup = reinterpret_cast<const uint32_t*>(cat.brickGridLookup.data());
    const size_t lookupCount = cat.brickGridLookup.size() / sizeof(uint32_t);
    std::vector<uint32_t> brickToGrid(brickCount, 0xFFFFFFFFu);
    for (uint32_t flat = 0; flat < tableSize; ++flat) {
        const size_t idx = static_cast<size_t>(tableOffset) + flat;
        if (idx >= lookupCount) break;
        const uint32_t brick = lookup[idx];
        if (brick == 0xFFFFFFFFu || brick >= brickCount) continue;
        const uint32_t gx = flat % bpa;
        const uint32_t gy = (flat / bpa) % bpa;
        const uint32_t gz = flat / (bpa * bpa);
        brickToGrid[brick] = gx | (gy << 10u) | (gz << 20u);
    }
    return brickToGrid;
}

inline float SourceSdfVoxel(const float* poolFloats, size_t poolFloatCount,
                            uint32_t poolBase, uint32_t stride, uint32_t sdfBase,
                            uint32_t bpa, uint32_t brickSide, uint32_t tableOffset,
                            const uint32_t* lookup, size_t lookupCount,
                            int gx, int gy, int gz) {
    if (gx < 0 || gy < 0 || gz < 0 ||
        gx >= static_cast<int>(bpa * brickSide) ||
        gy >= static_cast<int>(bpa * brickSide) ||
        gz >= static_cast<int>(bpa * brickSide)) {
        return kShellNormalSentinel;
    }
    const uint32_t bx = static_cast<uint32_t>(gx) / brickSide;
    const uint32_t by = static_cast<uint32_t>(gy) / brickSide;
    const uint32_t bz = static_cast<uint32_t>(gz) / brickSide;
    const uint32_t flat = bx + by * bpa + bz * bpa * bpa;
    const size_t lookupIdx = static_cast<size_t>(tableOffset) + flat;
    if (lookupIdx >= lookupCount) return kShellNormalSentinel;
    const uint32_t brick = lookup[lookupIdx];
    if (brick == 0xFFFFFFFFu) return kShellNormalSentinel;
    const uint32_t lx = static_cast<uint32_t>(gx) % brickSide;
    const uint32_t ly = static_cast<uint32_t>(gy) % brickSide;
    const uint32_t lz = static_cast<uint32_t>(gz) % brickSide;
    const uint32_t voxel = lx + ly * brickSide + lz * brickSide * brickSide;
    if (voxel >= SerializedOctree::kVoxelsPerBrick) return kShellNormalSentinel;
    const size_t poolIdx = static_cast<size_t>(poolBase) +
                           static_cast<size_t>(brick) * stride + sdfBase + voxel;
    return poolIdx < poolFloatCount ? poolFloats[poolIdx] : kShellNormalSentinel;
}

inline float SourceSdfTrilinear(const float* poolFloats, size_t poolFloatCount,
                                uint32_t poolBase, uint32_t stride, uint32_t sdfBase,
                                uint32_t bpa, uint32_t brickSide, uint32_t tableOffset,
                                const uint32_t* lookup, size_t lookupCount,
                                glm::vec3 gridPos) {
    const glm::vec3 base = glm::floor(gridPos);
    const glm::vec3 f = gridPos - base;
    const int x = static_cast<int>(base.x);
    const int y = static_cast<int>(base.y);
    const int z = static_cast<int>(base.z);
    const float c000 = SourceSdfVoxel(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                      bpa, brickSide, tableOffset, lookup, lookupCount, x, y, z);
    const float c100 = SourceSdfVoxel(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                      bpa, brickSide, tableOffset, lookup, lookupCount, x + 1, y, z);
    const float c010 = SourceSdfVoxel(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                      bpa, brickSide, tableOffset, lookup, lookupCount, x, y + 1, z);
    const float c110 = SourceSdfVoxel(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                      bpa, brickSide, tableOffset, lookup, lookupCount, x + 1, y + 1, z);
    const float c001 = SourceSdfVoxel(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                      bpa, brickSide, tableOffset, lookup, lookupCount, x, y, z + 1);
    const float c101 = SourceSdfVoxel(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                      bpa, brickSide, tableOffset, lookup, lookupCount, x + 1, y, z + 1);
    const float c011 = SourceSdfVoxel(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                      bpa, brickSide, tableOffset, lookup, lookupCount, x, y + 1, z + 1);
    const float c111 = SourceSdfVoxel(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                      bpa, brickSide, tableOffset, lookup, lookupCount, x + 1, y + 1, z + 1);
    const float maxAbs = std::max({std::fabs(c000), std::fabs(c100), std::fabs(c010), std::fabs(c110),
                                   std::fabs(c001), std::fabs(c101), std::fabs(c011), std::fabs(c111)});
    if (maxAbs >= kShellNormalSentinel) return kShellNormalSentinel;
    const float z0 = std::fma(f.x, c100 - c000, c000);
    const float z1 = std::fma(f.x, c110 - c010, c010);
    const float z2 = std::fma(f.x, c101 - c001, c001);
    const float z3 = std::fma(f.x, c111 - c011, c011);
    return std::fma(f.z, std::fma(f.y, z3 - z2, z2) - std::fma(f.y, z1 - z0, z0),
                    std::fma(f.y, z1 - z0, z0));
}

inline glm::vec3 DeriveNormalAtVoxel(const float* poolFloats, size_t poolFloatCount,
                                     uint32_t poolBase, uint32_t stride, uint32_t sdfBase,
                                     uint32_t bpa, uint32_t brickSide, uint32_t tableOffset,
                                     const uint32_t* lookup, size_t lookupCount,
                                     glm::vec3 gridPos) {
    const float d0 = SourceSdfTrilinear(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                        bpa, brickSide, tableOffset, lookup, lookupCount, gridPos);
    if (std::fabs(d0) >= kShellNormalSentinel) return glm::vec3(0.0f, 1.0f, 0.0f);
    const float h = 0.5f;
    const float dxPlus = SourceSdfTrilinear(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                            bpa, brickSide, tableOffset, lookup, lookupCount,
                                            gridPos + glm::vec3(h, 0.0f, 0.0f));
    const float dxMinus = SourceSdfTrilinear(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                             bpa, brickSide, tableOffset, lookup, lookupCount,
                                             gridPos - glm::vec3(h, 0.0f, 0.0f));
    const float dyPlus = SourceSdfTrilinear(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                            bpa, brickSide, tableOffset, lookup, lookupCount,
                                            gridPos + glm::vec3(0.0f, h, 0.0f));
    const float dyMinus = SourceSdfTrilinear(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                             bpa, brickSide, tableOffset, lookup, lookupCount,
                                             gridPos - glm::vec3(0.0f, h, 0.0f));
    const float dzPlus = SourceSdfTrilinear(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                            bpa, brickSide, tableOffset, lookup, lookupCount,
                                            gridPos + glm::vec3(0.0f, 0.0f, h));
    const float dzMinus = SourceSdfTrilinear(poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                             bpa, brickSide, tableOffset, lookup, lookupCount,
                                             gridPos - glm::vec3(0.0f, 0.0f, h));
    const auto oneSided = [d0](float plus, float minus) {
        return std::fabs(plus) >= kShellNormalSentinel ? (d0 - minus) * 2.0f
             : std::fabs(minus) >= kShellNormalSentinel ? (plus - d0) * 2.0f
             : plus - minus;
    };
    const glm::vec3 gradient(oneSided(dxPlus, dxMinus), oneSided(dyPlus, dyMinus),
                             oneSided(dzPlus, dzMinus));
    const float length = glm::length(gradient);
    return length > 1.0e-6f ? gradient / length : glm::vec3(0.0f, 1.0f, 0.0f);
}

inline void BakeBrickNormals(std::vector<uint8_t>& shellData, uint32_t shellSlot,
                             uint32_t shellBrickStride, uint32_t normalOffset,
                             uint32_t brickSide, uint32_t packedGrid,
                             const float* poolFloats, size_t poolFloatCount,
                             uint32_t poolBase, uint32_t sourceStride, uint32_t sdfBase,
                             uint32_t bpa, uint32_t tableOffset, const uint32_t* lookup,
                             size_t lookupCount) {
    const uint32_t gx = packedGrid & 0x3ffu;
    const uint32_t gy = (packedGrid >> 10u) & 0x3ffu;
    const uint32_t gz = (packedGrid >> 20u) & 0x3ffu;
    const size_t wordBase = static_cast<size_t>(shellSlot) * shellBrickStride + normalOffset;
    for (uint32_t voxel = 0; voxel < SerializedOctree::kVoxelsPerBrick; voxel += 2u) {
        const uint32_t lx0 = voxel & 7u;
        const uint32_t ly0 = (voxel >> 3u) & 7u;
        const uint32_t lz0 = voxel >> 6u;
        const glm::vec3 p0(static_cast<float>(gx * brickSide + lx0),
                           static_cast<float>(gy * brickSide + ly0),
                           static_cast<float>(gz * brickSide + lz0));
        const glm::vec3 n0 = DeriveNormalAtVoxel(poolFloats, poolFloatCount, poolBase,
                                                  sourceStride, sdfBase, bpa, brickSide,
                                                  tableOffset, lookup, lookupCount, p0);
        const uint16_t a = EncodeShellNormalOct16(n0);
        const uint32_t voxel1 = voxel + 1u;
        const uint32_t lx1 = voxel1 & 7u;
        const uint32_t ly1 = (voxel1 >> 3u) & 7u;
        const uint32_t lz1 = voxel1 >> 6u;
        const glm::vec3 p1(static_cast<float>(gx * brickSide + lx1),
                           static_cast<float>(gy * brickSide + ly1),
                           static_cast<float>(gz * brickSide + lz1));
        const uint16_t b = EncodeShellNormalOct16(DeriveNormalAtVoxel(
            poolFloats, poolFloatCount, poolBase, sourceStride, sdfBase, bpa, brickSide,
            tableOffset, lookup, lookupCount, p1));
        const uint32_t packed = static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 16u);
        std::memcpy(shellData.data() + (wordBase + voxel / 2u) * sizeof(float), &packed,
                    sizeof(packed));
    }
}

template <typename Fn>
inline void RunPerBrickWave(const char* owner, uint32_t count, uint32_t requestedWorkers, Fn&& fn,
                            const char* readDescription, const char* writeDescription) {
    KernelDispatch::Stage stage;
    stage.owner = owner;
    stage.itemCount = count;
    stage.backend = KernelDispatch::Backend::CpuTbb;
    // Neighbor tables/frontiers are read-only inputs. Per-brick output is disjoint;
    // the declarations make that ownership visible to the shared dispatcher.
    stage.reads = {{1, static_cast<int32_t>(count), readDescription}};
    stage.writes = {{2, static_cast<int32_t>(count), writeDescription}};
    stage.perElement = [work = std::forward<Fn>(fn)](uint32_t i) { work(i); };
    const int workers = requestedWorkers == 0u
        ? KernelDispatch::DefaultWorkerCount()
        : static_cast<int>(requestedWorkers);
    const KernelDispatch::Handle handle = KernelDispatch::RunPerElementStage(stage, {}, workers);
    if (!handle.ok()) throw std::runtime_error(std::string(owner) + " dispatch failed");
}

}  // namespace detail

// ---------------------------------------------------------------------------
// DeriveShell — the algorithm.
//
// Operates on octree index `octreeIdx` of a ConcatenatedOctrees (increment 1
// touches octree 0 only). Requires the octree to be Stored-SDF (has a channel
// pool + brickGridLookup); throws otherwise.
// ---------------------------------------------------------------------------
inline ShellDeriveResult DeriveShell(const ConcatenatedOctrees& cat,
                                     uint32_t octreeIdx,
                                     const ShellDeriveParams& params = {}) {
    ShellDeriveResult r;
    if (octreeIdx >= cat.count) {
        throw std::out_of_range("DeriveShell: octreeIdx out of range");
    }

    const OctreeConfig& cfg = cat.configs[octreeIdx];
    const uint32_t brickCount  = cat.brickCounts[octreeIdx];
    const uint32_t stride      = cfg.brickStrideFloats;
    const uint32_t bpa         = static_cast<uint32_t>(cfg.bricksPerAxis);
    const uint32_t poolBase    = cfg.poolBrickBase;   // float offset (0 for octree 0)
    const uint32_t brickSide   = cfg.brickSize > 0 ? static_cast<uint32_t>(cfg.brickSize) : 8u;

    if (stride == 0u || brickCount == 0u) {
        // Non-Stored-SDF / empty octree — nothing to derive. Return empty result.
        r.sourceBrickCount  = brickCount;
        r.brickStrideFloats = stride;
        r.shellBrickStrideFloats = stride;
        return r;
    }

    // SDF channel base within a brick (0 in the canonical SDF->Color->Roughness order).
    uint32_t sdfBase = 0u;
    {
        bool found = false;
        for (uint32_t i = 0; i < cfg.channelCount && i < kMaxChannels; ++i) {
            if (cfg.channels[i].semanticId == static_cast<uint32_t>(SEM_SDF)) {
                sdfBase = cfg.channels[i].channelBaseFloats;
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::runtime_error("DeriveShell: octree has no SEM_SDF channel");
        }
    }

    const float* poolFloats =
        reinterpret_cast<const float*>(cat.channelPool.data());
    const size_t poolFloatCount = cat.channelPool.size() / sizeof(float);
    const float halfDiag = detail::HalfBrickDiag(brickSide);

    r.sourceBrickCount  = brickCount;
    r.brickStrideFloats = stride;
    r.shellBrickStrideFloats = stride;
    r.surface.assign(brickCount, 0u);
    r.shell.assign(brickCount, 0u);

    // ---- Pass 1: SURFACE = sign-and-magnitude crossing over each brick's 512 SDF.
    detail::RunPerBrickWave("SVO.ShellDerive.classify", brickCount, params.workerCount,
        [&](uint32_t bi) {
        float mn, mx;
        detail::BrickSdfMinMax(poolFloats, poolFloatCount, poolBase, stride,
                               sdfBase, bi, mn, mx);
        r.surface[bi] = (mn < halfDiag && mx > -halfDiag) ? 1u : 0u;
        }, "source SDF pool", "surface classification");
    for (uint8_t surface : r.surface) r.surfaceBrickCount += surface != 0u ? 1u : 0u;

    // ---- Build brickIndex -> grid-coord from the dense grid lookup (invert it).
    // brickGridLookup for this octree is uint32[bpa^3], flat = gx + gy*bpa + gz*bpa^2,
    // value == source brick index, 0xFFFFFFFF == empty. The per-octree tables are
    // concatenated; slice out THIS octree's table by summing prior tables' sizes.
    const uint32_t tableSize = bpa * bpa * bpa;
    const uint32_t tableFloatOffset = detail::LookupTableOffset(cat, octreeIdx);
    const uint32_t* lookup =
        reinterpret_cast<const uint32_t*>(cat.brickGridLookup.data());
    const size_t lookupCount = cat.brickGridLookup.size() / sizeof(uint32_t);

    // brickToGrid[bi] = packed grid coord (gx | gy<<10 | gz<<20), or 0xFFFFFFFF.
    const std::vector<uint32_t> brickToGrid = detail::BuildBrickToGrid(cat, octreeIdx, brickCount);

    // ---- Pass 2: SHELL = SURFACE U dilateN(SURFACE) via grid-space 26-neighbourhood.
    // Seed SHELL from SURFACE, then grow `shellDilation` brick-layers. Each layer
    // marks every 26-neighbour (via the grid lookup) of a currently-shell brick.
    r.shell = r.surface;
    const uint32_t dilation = params.shellDilation == 0u ? 1u : params.shellDilation;
    std::vector<uint8_t> frontier = r.surface;
    for (uint32_t layer = 0; layer < dilation; ++layer) {
        std::vector<uint8_t> nextFrontier(brickCount, 0u);
        detail::RunPerBrickWave("SVO.ShellDerive.dilate26", brickCount, params.workerCount,
            [&](uint32_t bi) {
            if (r.shell[bi]) return;
            const uint32_t packed = brickToGrid[bi];
            if (packed == 0xFFFFFFFFu) return;
            const int gx = static_cast<int>(packed & 0x3FFu);
            const int gy = static_cast<int>((packed >> 10) & 0x3FFu);
            const int gz = static_cast<int>((packed >> 20) & 0x3FFu);
            for (int dz = -1; dz <= 1; ++dz)
              for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0 && dz == 0) continue;
                    const int nx = gx + dx, ny = gy + dy, nz = gz + dz;
                    if (nx < 0 || ny < 0 || nz < 0 ||
                        nx >= static_cast<int>(bpa) ||
                        ny >= static_cast<int>(bpa) ||
                        nz >= static_cast<int>(bpa)) continue;
                    const uint32_t flat = static_cast<uint32_t>(nx)
                                        + static_cast<uint32_t>(ny) * bpa
                                        + static_cast<uint32_t>(nz) * bpa * bpa;
                    const size_t idx = static_cast<size_t>(tableFloatOffset) + flat;
                    if (idx >= lookupCount) continue;
                    const uint32_t nb = lookup[idx];
                    if (nb != 0xFFFFFFFFu && nb < brickCount && frontier[nb]) {
                        nextFrontier[bi] = 1u;
                        return;
                    }
                }
            }, "frontier, grid lookup, and neighbor reads", "next dilation frontier");
        for (uint32_t bi = 0; bi < brickCount; ++bi) r.shell[bi] |= nextFrontier[bi];
        frontier.swap(nextFrontier);
    }

    // ---- Compact: emit shellData + shellLookup for every SHELL brick, in
    //      ascending source-brick order (stable, so both cache slots agree).
    r.sourceToShellSlot.assign(brickCount, 0xFFFFFFFFu);
    uint32_t shellCount = 0;
    for (uint32_t bi = 0; bi < brickCount; ++bi) if (r.shell[bi]) ++shellCount;

    r.shellBrickCount = shellCount;
    r.normalsBaked = params.bakeNormals;
    r.normalOffsetFloats = params.bakeNormals ? stride : 0u;
    r.normalStrideFloats = params.bakeNormals ? kShellNormalStrideFloats : 0u;
    r.shellBrickStrideFloats = stride + r.normalStrideFloats;
    r.shellLookup.reserve(shellCount);
    r.proxyAabbs.reserve(shellCount);
    r.shellData.resize(static_cast<size_t>(shellCount) * r.shellBrickStrideFloats * sizeof(float));

    const float invBpa = 1.0f / static_cast<float>(bpa);
    uint32_t slot = 0;
    for (uint32_t bi = 0; bi < brickCount; ++bi) {
        if (!r.shell[bi]) continue;
        r.shellLookup.push_back(bi);
        r.sourceToShellSlot[bi] = slot;
        const uint32_t packed = brickToGrid[bi];
        if (packed != 0xFFFFFFFFu) {
            const uint32_t gx = packed & 0x3FFu;
            const uint32_t gy = (packed >> 10) & 0x3FFu;
            const uint32_t gz = (packed >> 20) & 0x3FFu;
            ShellProxyAabb p;
            p.minLocal[0] = static_cast<float>(gx) * invBpa;
            p.minLocal[1] = static_cast<float>(gy) * invBpa;
            p.minLocal[2] = static_cast<float>(gz) * invBpa;
            p.brickId     = bi;
            p.maxLocal[0] = static_cast<float>(gx + 1u) * invBpa;
            p.maxLocal[1] = static_cast<float>(gy + 1u) * invBpa;
            p.maxLocal[2] = static_cast<float>(gz + 1u) * invBpa;
            p.octreeIndex = octreeIdx;
            r.proxyAabbs.push_back(p);
        }
        ++slot;
    }

    detail::RunPerBrickWave("SVO.ShellDerive.compact", shellCount, params.workerCount,
        [&](uint32_t shellSlot) {
        const uint32_t bi = r.shellLookup[shellSlot];
        const size_t srcStart = static_cast<size_t>(poolBase) +
                                static_cast<size_t>(bi) * stride;
        const size_t dstStart = static_cast<size_t>(shellSlot) * r.shellBrickStrideFloats;
        float* shellFloats = reinterpret_cast<float*>(r.shellData.data());
        for (uint32_t f = 0; f < stride; ++f) {
            const size_t si = srcStart + f;
            shellFloats[dstStart + f] = (si < poolFloatCount) ? poolFloats[si] : 0.0f;
        }
        if (params.bakeNormals) {
            detail::BakeBrickNormals(r.shellData, shellSlot, r.shellBrickStrideFloats,
                                     r.normalOffsetFloats, brickSide, brickToGrid[bi],
                                     poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                     bpa, tableFloatOffset, lookup, lookupCount);
        }
        }, "source SDF/channel pool and read-only apron neighbors", "compact brick payload");

    r.sourcePoolBytes = static_cast<uint64_t>(brickCount) * stride * sizeof(float);
    r.shellPoolBytes  = static_cast<uint64_t>(shellCount) * r.shellBrickStrideFloats * sizeof(float);
    r.normalPoolBytes = static_cast<uint64_t>(shellCount) * r.normalStrideFloats * sizeof(float);

    // ---- Build the grid->shellSlot remap table (drop-in binding-12 replacement).
    // Walk THIS octree's source grid sub-table; for each occupied grid cell whose
    // source brick survived into the shell, write its shell slot; else 0xFFFFFFFF.
    // A cell mapping to 0xFFFFFFFF is treated by the march as an unallocated brick,
    // which for a DROPPED (interior-solid) brick is correct: the iso-surface march
    // never reaches deep interior, so it never samples there.
    r.shellGridLookup.assign(static_cast<size_t>(tableSize) * sizeof(uint32_t), 0u);
    {
        uint32_t* gridSlot = reinterpret_cast<uint32_t*>(r.shellGridLookup.data());
        for (uint32_t flat = 0; flat < tableSize; ++flat) {
            uint32_t out = 0xFFFFFFFFu;
            const size_t idx = tableFloatOffset + flat;
            if (idx < lookupCount) {
                const uint32_t srcBrick = lookup[idx];
                if (srcBrick != 0xFFFFFFFFu && srcBrick < brickCount)
                    out = r.sourceToShellSlot[srcBrick];   // 0xFFFFFFFF if dropped
            }
            gridSlot[flat] = out;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// ApplyBrickSdfEdit — THE value-edit entry point for the dirty path (§C).
//
// Writes one source brick's SDF lane in the source pool. Callers pair this
// with appending the brick to their dirty list (BodyOctreeSceneNode::
// EditSourceBrickSdf does both in one call) so RevalidateShellBricks consumes
// exactly what was written — the producer and the mark cannot drift apart.
// Membership-changing edits are legal here (this only writes values); whether
// the edit stayed value-only is the re-derive path's concern. Returns false,
// pool untouched, on an invalid octree/brick/channel or a short value span.
// ---------------------------------------------------------------------------
inline bool ApplyBrickSdfEdit(ConcatenatedOctrees& cat, uint32_t octreeIdx,
                              uint32_t brickId, const float* sdf, size_t count) {
    if (octreeIdx >= cat.count) return false;
    if (sdf == nullptr || count < SerializedOctree::kVoxelsPerBrick) return false;
    if (brickId >= cat.brickCounts[octreeIdx]) return false;
    const OctreeConfig& cfg = cat.configs[octreeIdx];
    const uint32_t stride = cfg.brickStrideFloats;
    if (stride == 0u) return false;
    uint32_t sdfBase = 0u;
    bool found = false;
    for (uint32_t i = 0; i < cfg.channelCount && i < kMaxChannels; ++i) {
        if (cfg.channels[i].semanticId == static_cast<uint32_t>(SEM_SDF)) {
            sdfBase = cfg.channels[i].channelBaseFloats;
            found = true;
            break;
        }
    }
    if (!found) return false;
    float* poolFloats = reinterpret_cast<float*>(cat.channelPool.data());
    const size_t poolFloatCount = cat.channelPool.size() / sizeof(float);
    const size_t start = static_cast<size_t>(cfg.poolBrickBase)
                       + static_cast<size_t>(brickId) * stride + sdfBase;
    if (start + SerializedOctree::kVoxelsPerBrick > poolFloatCount) return false;
    std::memcpy(poolFloats + start, sdf,
                SerializedOctree::kVoxelsPerBrick * sizeof(float));
    return true;
}

// ---------------------------------------------------------------------------
// RevalidateShellBricks — dirty-brick incremental update (increment 1, §C).
//
// Given a fresh source pool (post-edit) and a set of dirty SOURCE brick ids,
// overwrite only those bricks' data in an existing shell slot. Bricks whose
// shell membership itself changes are out of increment-1 scope (kind-range
// dirty re-derives the whole octree); this handles the common case where a
// value edit changes distances inside already-shell bricks.
//
// Returns the number of shell slots actually rewritten.
// ---------------------------------------------------------------------------
inline uint32_t RevalidateShellBricks(const ConcatenatedOctrees& freshCat,
                                      uint32_t octreeIdx,
                                      const ShellDeriveResult& baseline,
                                      const std::vector<uint32_t>& dirtyBricks,
                                      std::vector<uint8_t>& shellDataInOut) {
    if (octreeIdx >= freshCat.count) return 0u;
    const OctreeConfig& cfg = freshCat.configs[octreeIdx];
    const uint32_t stride   = cfg.brickStrideFloats;
    const uint32_t poolBase = cfg.poolBrickBase;
    if (stride == 0u || stride != baseline.brickStrideFloats) return 0u;
    const uint32_t shellStride = baseline.shellBrickStrideFloats == 0u
        ? stride : baseline.shellBrickStrideFloats;

    const float* poolFloats =
        reinterpret_cast<const float*>(freshCat.channelPool.data());
    const size_t poolFloatCount = freshCat.channelPool.size() / sizeof(float);
    float* shellFloats = reinterpret_cast<float*>(shellDataInOut.data());
    const size_t shellFloatCount = shellDataInOut.size() / sizeof(float);

    uint32_t sdfBase = 0u;
    bool hasSdf = false;
    for (uint32_t i = 0; i < cfg.channelCount && i < kMaxChannels; ++i) {
        if (cfg.channels[i].semanticId == static_cast<uint32_t>(SEM_SDF)) {
            sdfBase = cfg.channels[i].channelBaseFloats;
            hasSdf = true;
            break;
        }
    }
    const uint32_t bpa = static_cast<uint32_t>(cfg.bricksPerAxis);
    const uint32_t brickSide = cfg.brickSize > 0 ? static_cast<uint32_t>(cfg.brickSize) : 8u;
    const uint32_t tableOffset = detail::LookupTableOffset(freshCat, octreeIdx);
    const uint32_t* lookup = reinterpret_cast<const uint32_t*>(freshCat.brickGridLookup.data());
    const size_t lookupCount = freshCat.brickGridLookup.size() / sizeof(uint32_t);
    const std::vector<uint32_t> brickToGrid = detail::BuildBrickToGrid(
        freshCat, octreeIdx, freshCat.brickCounts[octreeIdx]);

    uint32_t rewritten = 0u;
    for (uint32_t bi : dirtyBricks) {
        if (bi >= baseline.sourceToShellSlot.size()) continue;
        const uint32_t slot = baseline.sourceToShellSlot[bi];
        if (slot == 0xFFFFFFFFu) continue;   // brick not in the shell; skip (out of scope)
        const size_t srcStart =
            static_cast<size_t>(poolBase) + static_cast<size_t>(bi) * stride;
        const size_t dstStart = static_cast<size_t>(slot) * shellStride;
        if (dstStart + stride > shellFloatCount) continue;
        for (uint32_t f = 0; f < stride; ++f) {
            const size_t si = srcStart + f;
            shellFloats[dstStart + f] =
                (si < poolFloatCount) ? poolFloats[si] : 0.0f;
        }
        if (baseline.normalsBaked && hasSdf && baseline.normalStrideFloats != 0u &&
            bi < brickToGrid.size() && brickToGrid[bi] != 0xFFFFFFFFu) {
            detail::BakeBrickNormals(shellDataInOut, slot, shellStride,
                                     baseline.normalOffsetFloats, brickSide, brickToGrid[bi],
                                     poolFloats, poolFloatCount, poolBase, stride, sdfBase,
                                     bpa, tableOffset, lookup, lookupCount);
        }
        ++rewritten;
    }
    return rewritten;
}

// ---------------------------------------------------------------------------
// ShellPool — a compact drop-in replacement for a source ConcatenatedOctrees.
//
// Holds a compact channelPool + a grid->shellSlot brickGridLookup + configs with
// rewritten poolBrickBase, so binding it at the render's SDF pool (binding 11) +
// brick lookup (binding 12) is byte-for-byte render-equivalent to the full pool
// (the march's existing addressing `channelPool[poolBrickBase + brickIdx*stride
// + ...]` reads the compact data). Also keeps the per-octree ShellDeriveResult
// for the dirty-revalidate path.
// ---------------------------------------------------------------------------
struct ShellPool {
    ConcatenatedOctrees           compact;   // drop-in: channelPool + brickGridLookup + configs
    std::vector<ShellDeriveResult> perOctree; // one per source octree (for revalidate)
    uint64_t sourcePoolBytes = 0;
    uint64_t shellPoolBytes  = 0;
};

// ---------------------------------------------------------------------------
// DeriveShellPool — derive the reachable shell for EVERY octree in `src` and
// assemble a compact, render-equivalent ConcatenatedOctrees. Non-Stored-SDF
// octrees (no SDF channel / zero stride) are PASSED THROUGH unchanged so mixed
// pools stay correct. Multi-octree safe: each octree's compact bricks are
// appended and its poolBrickBase rewritten to the running compact offset; its
// grid remap sub-table (grid->LOCAL shellSlot) is appended so the shader's
// per-octree lookupBase (octreeIdx*bpa^3) addressing is preserved.
// ---------------------------------------------------------------------------
inline ShellPool DeriveShellPool(const ConcatenatedOctrees& src,
                                 const ShellDeriveParams& params = {}) {
    ShellPool out;
    out.compact.count       = src.count;
    out.compact.nodes       = src.nodes;       // binary octree data unchanged
    out.compact.bricks      = src.bricks;
    out.compact.materials   = src.materials;
    out.compact.mipPool     = src.mipPool;
    out.compact.configs     = src.configs;     // rewrite poolBrickBase below
    out.compact.nodeCounts  = src.nodeCounts;
    out.compact.brickCounts = src.brickCounts; // rewrite to shell brick counts below
    out.perOctree.reserve(src.count);

    const float* srcPoolFloats =
        reinterpret_cast<const float*>(src.channelPool.data());
    const size_t srcPoolFloatCount = src.channelPool.size() / sizeof(float);

    uint32_t runningPoolBase = 0u;  // float offset into the compact pool
    for (uint32_t oi = 0; oi < src.count; ++oi) {
        const OctreeConfig& cfg = src.configs[oi];
        const uint32_t stride = cfg.brickStrideFloats;
        const uint32_t bpa    = static_cast<uint32_t>(cfg.bricksPerAxis);
        const uint32_t bcount = src.brickCounts[oi];
        const uint32_t tableSize = bpa * bpa * bpa;

        // Detect a Stored-SDF octree (has an SDF channel + non-zero stride).
        bool hasSdf = false;
        for (uint32_t i = 0; i < cfg.channelCount && i < kMaxChannels; ++i)
            if (cfg.channels[i].semanticId == static_cast<uint32_t>(SEM_SDF)) { hasSdf = true; break; }

        if (stride == 0u || bcount == 0u || !hasSdf) {
            // Pass through unchanged: copy this octree's full bricks + its raw grid sub-table.
            out.compact.configs[oi].poolBrickBase = runningPoolBase;
            setShellNormalDescriptor(out.compact.configs[oi], 0u, 0u, false);
            const size_t srcStart = static_cast<size_t>(cfg.poolBrickBase);
            const size_t floats   = static_cast<size_t>(bcount) * stride;
            for (size_t f = 0; f < floats; ++f) {
                const float v = (srcStart + f < srcPoolFloatCount) ? srcPoolFloats[srcStart + f] : 0.0f;
                const uint8_t* vb = reinterpret_cast<const uint8_t*>(&v);
                out.compact.channelPool.insert(out.compact.channelPool.end(), vb, vb + sizeof(float));
            }
            // Grid sub-table: copy the source octree's uint32[bpa^3] slice verbatim.
            uint32_t off = 0u;
            for (uint32_t k = 0; k < oi; ++k) { uint32_t b=src.configs[k].bricksPerAxis; off += b*b*b; }
            const uint32_t* srcLU = reinterpret_cast<const uint32_t*>(src.brickGridLookup.data());
            const size_t luCount = src.brickGridLookup.size() / sizeof(uint32_t);
            for (uint32_t flat = 0; flat < tableSize; ++flat) {
                uint32_t val = (off + flat < luCount) ? srcLU[off + flat] : 0xFFFFFFFFu;
                const uint8_t* vb = reinterpret_cast<const uint8_t*>(&val);
                out.compact.brickGridLookup.insert(out.compact.brickGridLookup.end(), vb, vb + sizeof(uint32_t));
            }
            out.perOctree.push_back(ShellDeriveResult{});
            runningPoolBase += bcount * stride;
            continue;
        }

        // Stored-SDF: derive the shell for this octree.
        ShellDeriveResult r = DeriveShell(src, oi, params);
        out.sourcePoolBytes += r.sourcePoolBytes;
        out.shellPoolBytes  += r.shellPoolBytes;

        // Append this octree's compact bricks; set its poolBrickBase.
        out.compact.configs[oi].poolBrickBase = runningPoolBase;
        out.compact.brickCounts[oi]           = r.shellBrickCount;
        out.compact.configs[oi].brickStrideFloats = r.shellBrickStrideFloats;
        setShellNormalDescriptor(out.compact.configs[oi], r.normalOffsetFloats,
                                 r.normalStrideFloats, r.normalsBaked);
        out.compact.channelPool.insert(out.compact.channelPool.end(),
                                       r.shellData.begin(), r.shellData.end());
        // Append this octree's grid remap sub-table (grid -> LOCAL shellSlot).
        out.compact.brickGridLookup.insert(out.compact.brickGridLookup.end(),
                                           r.shellGridLookup.begin(), r.shellGridLookup.end());

        runningPoolBase += r.shellBrickCount * r.shellBrickStrideFloats;
        out.perOctree.push_back(std::move(r));
    }

    return out;
}

}  // namespace Vixen::SVO
