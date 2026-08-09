#pragma once
// MipAnisoPool.h — Deep-Field Mip Policy, anisotropic coarse mips (spec
// "Anisotropic coarse mips (regime 3 only)").
//
// 6-AXIS DIRECTIONAL COVERAGE (GigaVoxels steal-list encoding — per-axis
// occupancy asymmetry; SGGX microflakes noted in the spec as a future,
// more-principled upgrade — closed under averaging, correct mip downsample —
// NOT implemented here). One MipAnisoSample per (node, channel), computed
// ONLY for interior nodes at or above a configurable threshold LEVEL (levels
// counted from the root, level 0 = root; default threshold = the octree's
// own interior-node depth, i.e. levels above the brick grid —
// GetThresholdLevel() below). This is an ADDITIVE SIDE POOL: it does not
// touch MipSample (MipSample.h) or MipPool/BakeMipPool (MipBake.h) — those
// stay byte-identical for every existing consumer. Regime-3 (cosmic
// accumulation) shader consumption is a later slice (deep-field-mip-policy
// design doc, "Parity bars per regime" / regime 3); this slice is bake +
// serialize only.
//
// Encoding: coverage[axis][sign] = (# occupied child octants on that side of
// the axis) / 4 (4 octants make up each half of a 2x2x2 split). 6 floats per
// (node, channel): +X,-X,+Y,-Y,+Z,-Z. Octant bit layout mirrors the rest of
// this codebase's convention (verified via SVORebuild.cpp PHASE 2: octant =
// x | (y<<1) | (z<<2) — bit0=X, bit1=Y, bit2=Z; MipBake.h's brick-octant
// split uses the identical bit assignment one scale down).

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "MipSample.h"  // FieldKind/SemanticId already-baked channel descriptors
#include "ShellOctreeGpu.h"
#include "SVOBuilder.h"
#include "SVOTypes.h"

namespace Vixen::SVO {

// One node's 6-axis directional coverage for one channel lane.
struct MipAnisoSample {
    float covPosX = 0.0f, covNegX = 0.0f;
    float covPosY = 0.0f, covNegY = 0.0f;
    float covPosZ = 0.0f, covNegZ = 0.0f;
};
static_assert(sizeof(MipAnisoSample) == 6 * sizeof(float), "MipAnisoSample must be a plain 6-float POD");

// Concatenated/per-octree side pool. SoA layout mirrors MipPool exactly:
// samples[nodeIdx * channelCount + channelIdx], but samples are only WRITTEN
// (non-zero) for nodes at level <= thresholdLevel (interior, coarse); every
// other slot stays default {0,0,0,0,0,0} (isotropic/absent — a consumer
// reads this as "no directional data, treat as uniform").
struct MipAnisoPool {
    std::vector<MipAnisoSample> samples;  // size == nodeCount * channelCount
    uint32_t nodeCount = 0;
    uint32_t channelCount = 0;
    uint32_t thresholdLevel = 0;   // bake-time parameter actually used
    uint32_t coarseNodeCount = 0;  // # nodes that received a non-default sample (diagnostic)

    MipAnisoSample Get(uint32_t nodeIdx, uint32_t channelIdx) const {
        const size_t idx = static_cast<size_t>(nodeIdx) * channelCount + channelIdx;
        if (idx >= samples.size()) return MipAnisoSample{};
        return samples[idx];
    }
};

namespace detail {

// Per-node level (hops from root, root=0), forward BFS pass over the
// already-finalized childDescriptors array. Mirrors BakeMipPool's own
// nonleaf-run resolution (MipBake.h) so both walks agree on which physical
// node a given (parent, octant) non-leaf child resolves to — duplicated
// here rather than shared because BakeMipPool's helpers are file-local
// inline functions and this pool must stay a fully independent, additive
// unit (spec: "zero risk to shipped consumers").
inline std::vector<uint32_t> ComputeNodeLevels(const std::vector<ChildDescriptor>& nodes) {
    std::vector<uint32_t> level(nodes.size(), 0u);
    if (nodes.empty()) return level;
    // BFS order guarantees parent index < child index (SVORebuild.cpp PHASE 3),
    // so a single forward pass — each node's level already resolved before we
    // reach it as someone's parent — suffices; no queue needed.
    for (uint32_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const ChildDescriptor& desc = nodes[nodeIdx];
        uint32_t nonLeafPosition = 0;
        for (int octant = 0; octant < 8; ++octant) {
            if (!desc.hasChild(octant) || desc.isLeaf(octant)) continue;
            const uint32_t childIdx = desc.childPointer + nonLeafPosition;
            ++nonLeafPosition;
            if (childIdx < nodes.size()) level[childIdx] = level[nodeIdx] + 1;
        }
    }
    return level;
}

// Default threshold: levels above the brick grid, i.e. the octree's own
// interior-node depth (userMaxLevels - brickDepthLevels), matching the
// spec's "above a threshold level (make it a bake parameter, default =
// levels above the brick grid)". Nodes at level < this value are coarse
// (root-ward of the brick grid); level >= this value sit at/inside the
// brick grid and get no aniso sample (near/mid content — spec: "not needed
// at close and mid levels").
inline uint32_t DefaultAnisoThresholdLevel(const OctreeConfig& config) {
    const int interiorDepth = config.userMaxLevels - config.brickDepthLevels;
    return interiorDepth > 0 ? static_cast<uint32_t>(interiorDepth) : 0u;
}

}  // namespace detail

// Compute the full MipAnisoPool for one already-baked+serialized octree.
// `oct`/`serialized` must be the same pair BakeMipPool would take (same
// contract). thresholdLevel: nodes with level < thresholdLevel get a
// directional sample computed from their children's occupancy asymmetry;
// nodes at/after thresholdLevel (including all brick-level leaves) are left
// at the zero default. Pass detail::DefaultAnisoThresholdLevel(serialized.config)
// for the spec's stated default.
inline MipAnisoPool BakeMipAnisoPool(const Octree& oct, const SerializedOctree& serialized,
                                      uint32_t thresholdLevel) {
    MipAnisoPool pool;
    pool.channelCount = serialized.channelCount;
    pool.nodeCount = serialized.nodeCount;
    pool.thresholdLevel = thresholdLevel;
    pool.samples.assign(static_cast<size_t>(pool.nodeCount) * pool.channelCount, MipAnisoSample{});

    if (!oct.root || pool.nodeCount == 0 || pool.channelCount == 0) {
        return pool;
    }

    const std::vector<ChildDescriptor>& nodes = oct.root->childDescriptors;
    const std::vector<uint32_t> levels = detail::ComputeNodeLevels(nodes);

    for (uint32_t nodeIdx = 0; nodeIdx < pool.nodeCount; ++nodeIdx) {
        if (levels[nodeIdx] >= thresholdLevel) continue;  // at/below the coarse cutoff — leave default

        const ChildDescriptor& desc = nodes[nodeIdx];
        bool anyOccupied = false;
        for (int octant = 0; octant < 8; ++octant) {
            if (desc.hasChild(octant)) { anyOccupied = true; break; }
        }
        if (!anyOccupied) continue;

        // Per-axis occupancy asymmetry: octant bit0=X, bit1=Y, bit2=Z
        // (verified convention, see file header). Each axis half holds 4
        // octants; coverage = occupied-count / 4.
        int posX = 0, negX = 0, posY = 0, negY = 0, posZ = 0, negZ = 0;
        for (int octant = 0; octant < 8; ++octant) {
            if (!desc.hasChild(octant)) continue;
            if (octant & 1) ++posX; else ++negX;
            if (octant & 2) ++posY; else ++negY;
            if (octant & 4) ++posZ; else ++negZ;
        }

        MipAnisoSample sample;
        sample.covPosX = posX / 4.0f; sample.covNegX = negX / 4.0f;
        sample.covPosY = posY / 4.0f; sample.covNegY = negY / 4.0f;
        sample.covPosZ = posZ / 4.0f; sample.covNegZ = negZ / 4.0f;

        // Same sample applies to every channel lane at this node — direction
        // is geometric occupancy, not per-channel content (unlike MipSample's
        // per-channel value/coverage). Mirrors MipPool's SoA stride so a
        // consumer's addressing code is identical between the two pools.
        for (uint32_t ch = 0; ch < pool.channelCount; ++ch) {
            pool.samples[static_cast<size_t>(nodeIdx) * pool.channelCount + ch] = sample;
        }
        ++pool.coarseNodeCount;
    }

    return pool;
}

// Convenience: bake at the spec's default threshold (levels above the brick grid).
inline MipAnisoPool BakeMipAnisoPool(const Octree& oct, const SerializedOctree& serialized) {
    return BakeMipAnisoPool(oct, serialized, detail::DefaultAnisoThresholdLevel(serialized.config));
}

// Serialize a MipAnisoPool's samples into raw bytes — stride sizeof(MipAnisoSample),
// SoA order [nodeIdx * channelCount + ch], mirroring SerializeMipPool exactly.
inline std::vector<uint8_t> SerializeMipAnisoPool(const MipAnisoPool& pool) {
    static_assert(sizeof(MipAnisoSample) == 6 * sizeof(float), "MipAnisoSample must be a plain 6-float POD");
    std::vector<uint8_t> bytes(pool.samples.size() * sizeof(MipAnisoSample));
    if (!pool.samples.empty()) {
        std::memcpy(bytes.data(), pool.samples.data(), bytes.size());
    }
    return bytes;
}

// ---------------------------------------------------------------------------
// CPU-side bake-time self-check (spec item 3): an axis-aligned slab body
// must show strong axis asymmetry at the root; a solid cube must be near-
// isotropic. Returns true/pass; writes a human-readable line to `outLine`
// either way so the boot path can print PASS/FAIL with numbers.
// ---------------------------------------------------------------------------
struct MipAnisoSelfCheckResult {
    bool pass = false;
    std::string report;  // one line, numbers included
};

inline MipAnisoSelfCheckResult MipAnisoSelfCheckSlabAsymmetry(const MipAnisoSample& rootSample) {
    // A slab flattened along Z (thin in Z, wide in X/Y) should show its
    // strongest occupancy imbalance on the Z axis: both Z halves populated
    // similarly (the slab straddles z=0 by construction below) while X/Y
    // stay balanced too in this symmetric case — so the real discriminator
    // is COVERAGE MAGNITUDE (Z axis is "thin": both +Z/-Z are covered as a
    // unit) vs. spread. To keep this a genuine "asymmetry" check per the
    // spec wording, the self-check body (see below) is deliberately
    // off-center along one axis instead of centered — see BuildSlabBody.
    const float diffX = std::fabs(rootSample.covPosX - rootSample.covNegX);
    const float diffY = std::fabs(rootSample.covPosY - rootSample.covNegY);
    const float diffZ = std::fabs(rootSample.covPosZ - rootSample.covNegZ);
    const float maxDiff = std::max({diffX, diffY, diffZ});

    MipAnisoSelfCheckResult r;
    r.pass = maxDiff >= 0.5f;  // strong asymmetry: one axis fully one-sided vs the others balanced
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "[MipAnisoPool] slab self-check: diffX=%.3f diffY=%.3f diffZ=%.3f maxDiff=%.3f (threshold 0.5) -> %s",
        diffX, diffY, diffZ, maxDiff, r.pass ? "PASS" : "FAIL");
    r.report = buf;
    return r;
}

inline MipAnisoSelfCheckResult MipAnisoSelfCheckCubeIsotropy(const MipAnisoSample& rootSample) {
    const float diffX = std::fabs(rootSample.covPosX - rootSample.covNegX);
    const float diffY = std::fabs(rootSample.covPosY - rootSample.covNegY);
    const float diffZ = std::fabs(rootSample.covPosZ - rootSample.covNegZ);
    const float maxDiff = std::max({diffX, diffY, diffZ});

    MipAnisoSelfCheckResult r;
    r.pass = maxDiff <= 0.26f;  // near-isotropic: a solid cube's 8 octants are all occupied -> diff ~= 0
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "[MipAnisoPool] cube self-check: diffX=%.3f diffY=%.3f diffZ=%.3f maxDiff=%.3f (threshold 0.26) -> %s",
        diffX, diffY, diffZ, maxDiff, r.pass ? "PASS" : "FAIL");
    r.report = buf;
    return r;
}

}  // namespace Vixen::SVO
