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

#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Vixen::SVO {

// ---------------------------------------------------------------------------
// ShellDeriveResult — the compacted cache slot + provenance for verification.
// ---------------------------------------------------------------------------
struct ShellDeriveResult {
    // Compacted SoA pool holding ONLY shell bricks, restrided contiguously.
    // Byte layout per shell slot is IDENTICAL to the source pool's per-brick
    // stride (brickStrideFloats floats/brick), so the render shader reads it
    // with the same addressing, just against a smaller buffer.
    std::vector<uint8_t> shellData;      // shellBrickCount * brickStrideFloats * 4 bytes

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

    uint32_t sourceBrickCount = 0;
    uint32_t surfaceBrickCount = 0;
    uint32_t shellBrickCount = 0;       // == shellLookup.size()
    uint32_t brickStrideFloats = 0;

    // Bytes of the source vs the compacted pool — the measured bandwidth win.
    uint64_t sourcePoolBytes = 0;
    uint64_t shellPoolBytes  = 0;
};

// ---------------------------------------------------------------------------
// ShellDeriveParams — configuration mirroring BodyOctreeSceneNode::shellDilation_.
// ---------------------------------------------------------------------------
struct ShellDeriveParams {
    // Brick-layer dilation of the SURFACE set along all 26 directions.
    //   1 = minimal sound invariant (26-neighbour of surface).  Default.
    //   2..3 = thicker shells for future effects; still sound (superset).
    uint32_t shellDilation = 1u;
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
    r.surface.assign(brickCount, 0u);
    r.shell.assign(brickCount, 0u);

    // ---- Pass 1: SURFACE = sign-and-magnitude crossing over each brick's 512 SDF.
    for (uint32_t bi = 0; bi < brickCount; ++bi) {
        float mn, mx;
        detail::BrickSdfMinMax(poolFloats, poolFloatCount, poolBase, stride,
                               sdfBase, bi, mn, mx);
        if (mn < halfDiag && mx > -halfDiag) {
            r.surface[bi] = 1u;
            ++r.surfaceBrickCount;
        }
    }

    // ---- Build brickIndex -> grid-coord from the dense grid lookup (invert it).
    // brickGridLookup for this octree is uint32[bpa^3], flat = gx + gy*bpa + gz*bpa^2,
    // value == source brick index, 0xFFFFFFFF == empty. The per-octree tables are
    // concatenated; slice out THIS octree's table by summing prior tables' sizes.
    const uint32_t tableSize = bpa * bpa * bpa;
    uint32_t tableFloatOffset = 0u;   // in uint32 units within brickGridLookup
    for (uint32_t k = 0; k < octreeIdx; ++k) {
        const uint32_t bpaK = cat.configs[k].bricksPerAxis;
        tableFloatOffset += bpaK * bpaK * bpaK;
    }
    const uint32_t* lookup =
        reinterpret_cast<const uint32_t*>(cat.brickGridLookup.data());
    const size_t lookupCount = cat.brickGridLookup.size() / sizeof(uint32_t);

    // brickToGrid[bi] = packed grid coord (gx | gy<<10 | gz<<20), or 0xFFFFFFFF.
    std::vector<uint32_t> brickToGrid(brickCount, 0xFFFFFFFFu);
    for (uint32_t flat = 0; flat < tableSize; ++flat) {
        const size_t idx = tableFloatOffset + flat;
        if (idx >= lookupCount) break;
        const uint32_t bview = lookup[idx];
        if (bview == 0xFFFFFFFFu || bview >= brickCount) continue;
        const uint32_t gx = flat % bpa;
        const uint32_t gy = (flat / bpa) % bpa;
        const uint32_t gz = flat / (bpa * bpa);
        brickToGrid[bview] = gx | (gy << 10) | (gz << 20);
    }

    // ---- Pass 2: SHELL = SURFACE U dilateN(SURFACE) via grid-space 26-neighbourhood.
    // Seed SHELL from SURFACE, then grow `shellDilation` brick-layers. Each layer
    // marks every 26-neighbour (via the grid lookup) of a currently-shell brick.
    r.shell = r.surface;
    const uint32_t dilation = params.shellDilation == 0u ? 1u : params.shellDilation;
    std::vector<uint8_t> frontier = r.surface;
    for (uint32_t layer = 0; layer < dilation; ++layer) {
        std::vector<uint8_t> nextFrontier(brickCount, 0u);
        for (uint32_t bi = 0; bi < brickCount; ++bi) {
            if (!frontier[bi]) continue;
            const uint32_t packed = brickToGrid[bi];
            if (packed == 0xFFFFFFFFu) continue;
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
                    const size_t idx = tableFloatOffset + flat;
                    if (idx >= lookupCount) continue;
                    const uint32_t nb = lookup[idx];
                    if (nb == 0xFFFFFFFFu || nb >= brickCount) continue;
                    if (!r.shell[nb]) {
                        r.shell[nb] = 1u;
                        nextFrontier[nb] = 1u;
                    }
                }
        }
        frontier.swap(nextFrontier);
    }

    // ---- Compact: emit shellData + shellLookup for every SHELL brick, in
    //      ascending source-brick order (stable, so both cache slots agree).
    r.sourceToShellSlot.assign(brickCount, 0xFFFFFFFFu);
    uint32_t shellCount = 0;
    for (uint32_t bi = 0; bi < brickCount; ++bi) if (r.shell[bi]) ++shellCount;

    r.shellBrickCount = shellCount;
    r.shellLookup.reserve(shellCount);
    r.shellData.resize(static_cast<size_t>(shellCount) * stride * sizeof(float));

    float* shellFloats = reinterpret_cast<float*>(r.shellData.data());
    uint32_t slot = 0;
    for (uint32_t bi = 0; bi < brickCount; ++bi) {
        if (!r.shell[bi]) continue;
        r.shellLookup.push_back(bi);
        r.sourceToShellSlot[bi] = slot;
        // Copy the whole brick stride (all channels) from source pool to shell pool.
        const size_t srcStart =
            static_cast<size_t>(poolBase) + static_cast<size_t>(bi) * stride;
        const size_t dstStart = static_cast<size_t>(slot) * stride;
        for (uint32_t f = 0; f < stride; ++f) {
            const size_t si = srcStart + f;
            shellFloats[dstStart + f] =
                (si < poolFloatCount) ? poolFloats[si] : 0.0f;
        }
        ++slot;
    }

    r.sourcePoolBytes = static_cast<uint64_t>(brickCount) * stride * sizeof(float);
    r.shellPoolBytes  = static_cast<uint64_t>(shellCount)  * stride * sizeof(float);

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

    const float* poolFloats =
        reinterpret_cast<const float*>(freshCat.channelPool.data());
    const size_t poolFloatCount = freshCat.channelPool.size() / sizeof(float);
    float* shellFloats = reinterpret_cast<float*>(shellDataInOut.data());
    const size_t shellFloatCount = shellDataInOut.size() / sizeof(float);

    uint32_t rewritten = 0u;
    for (uint32_t bi : dirtyBricks) {
        if (bi >= baseline.sourceToShellSlot.size()) continue;
        const uint32_t slot = baseline.sourceToShellSlot[bi];
        if (slot == 0xFFFFFFFFu) continue;   // brick not in the shell; skip (out of scope)
        const size_t srcStart =
            static_cast<size_t>(poolBase) + static_cast<size_t>(bi) * stride;
        const size_t dstStart = static_cast<size_t>(slot) * stride;
        if (dstStart + stride > shellFloatCount) continue;
        for (uint32_t f = 0; f < stride; ++f) {
            const size_t si = srcStart + f;
            shellFloats[dstStart + f] =
                (si < poolFloatCount) ? poolFloats[si] : 0.0f;
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
        out.compact.channelPool.insert(out.compact.channelPool.end(),
                                       r.shellData.begin(), r.shellData.end());
        // Append this octree's grid remap sub-table (grid -> LOCAL shellSlot).
        out.compact.brickGridLookup.insert(out.compact.brickGridLookup.end(),
                                           r.shellGridLookup.begin(), r.shellGridLookup.end());

        runningPoolBase += r.shellBrickCount * stride;
        out.perOctree.push_back(std::move(r));
    }

    return out;
}

}  // namespace Vixen::SVO
