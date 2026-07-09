#pragma once
// MipBake.h — Sparse-Mip ESVO LOD Inc1, Task 2.
//
// Bottom-up bake-time fill: computes one MipSample per (node, channel) over
// an already-built Octree + its SerializeSdf() output. BakeAndAttachMipPool
// (below) serializes the result into SerializedOctree::mipPool (Task 3).
//
// ---------------------------------------------------------------------------
// Bottom-up bake hook — verified via codegraph_explore, NOT assumed
// ---------------------------------------------------------------------------
// The plan asked us to verify the actual bottom-up construction entry point
// before writing code. That hook is LaineKarrasOctree::rebuild()
// (libraries/SVO/src/SVORebuild.cpp:228), whose PHASE 2 (lines 449-522)
// builds parent levels bottom-up into `tempDescriptors`, THEN PHASE 3
// (lines 524-602) does a BFS REORDER into `finalDescriptors` — the array
// that becomes Octree::root->childDescriptors, and that SerializeSdf()
// copies verbatim into SerializedOctree::nodes. Because PHASE 3 renumbers
// everything, the bottom-up PHASE 2 order is NOT the final node ordinal
// order; hooking mip computation into PHASE 2 directly would require
// re-deriving PHASE 3's BFS remapping a second time inside SVORebuild.cpp,
// coupling this feature tightly to rebuild()'s internals.
//
// Instead, this file adds a SEPARATE bottom-up POST-PASS (per the Task 2
// text: "add a post-pass (or fold into the existing pass)") that walks the
// ALREADY-FINALIZED childDescriptors array (BFS order: every parent's index
// is strictly less than all of its children's indices — PHASE 3 pushes
// children after their parent's slot is claimed). Reverse index order over
// a BFS-ordered array visits every node AFTER all of its children — exactly
// bottom-up, root-ward — using only the same public Octree/OctreeBlock data
// SerializeSdf() already reads. This reuses the existing serialization
// order directly (Task 2's ordinal requirement) with zero changes to
// SVORebuild.cpp's construction algorithm.
//
// A brick-level LEAF node's mip sample is the filtered reduction of its own
// brick's 512 voxels (same per-channel filter rules as interior nodes); an
// INTERIOR node's mip sample is the filtered reduction of its (up to 8)
// children's already-computed mip samples (leaf children use their brick
// reduction; non-leaf children use their own already-computed interior mip
// sample — both are already MipSample values by the time an interior node's
// turn comes, since we go highest-index-first).
// ---------------------------------------------------------------------------

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "MipSample.h"
#include "ShellOctreeGpu.h"
#include "SVOBuilder.h"
#include "SVOTypes.h"

namespace Vixen::SVO {

// Per-node mip samples, SoA by channel: mipSamples[nodeIdx * channelCount + ch].
// nodeIdx matches the exact index into SerializedOctree::nodes (ChildDescriptor
// stride) — the same ordinal the shader will use to look up nodeArrayBase-
// relative node data, per the direction doc's "same ordinal in the other
// dataset" requirement.
struct MipPool {
    std::vector<MipSample> samples;  // size == nodeCount * channelCount
    uint32_t nodeCount = 0;
    uint32_t channelCount = 0;

    MipSample Get(uint32_t nodeIdx, uint32_t channelIdx) const {
        const size_t idx = static_cast<size_t>(nodeIdx) * channelCount + channelIdx;
        if (idx >= samples.size()) return MipSample{};
        return samples[idx];
    }
};

namespace detail {

// Reduce one brick's 512 voxels for one channel/component lane down to a
// single MipSample, using the same per-semantic filter rule a parent node
// would apply to its children (min-magnitude for SDF, mean otherwise) — a
// brick's voxels ARE this leaf node's "children" for mip-filtering purposes.
inline MipSample ReduceBrickToMipSample(const SerializedOctree& serialized,
                                         uint32_t brickIndex,
                                         SemanticId sem,
                                         FieldKind kind,
                                         uint32_t comp) {
    // A brick has 512 voxels; MipChildSample only holds 8 slots, so reduce in
    // two stages: 512 voxels -> 8 octant groups (64 voxels each, matching the
    // z*64+y*8+x layout's top-level octant split) -> 1 MipSample. This keeps
    // FilterMipMean/FilterMipMinMagnitude as the single source of truth for
    // both the leaf-from-voxels reduction and the interior-from-children
    // reduction, rather than duplicating the filter math at brick scale.
    std::array<MipChildSample, 8> octantGroups{};
    std::array<float, 8> octantBestOrSum{};
    std::array<int, 8> octantOccupiedCount{};
    octantBestOrSum.fill(0.0f);
    octantOccupiedCount.fill(0);
    std::array<float, 8> octantBestAbs{};
    octantBestAbs.fill(-1.0f);

    // Occupancy comes from the material bricks array (out.bricks), NOT from
    // "channel value != 0" — a genuinely on-surface SDF voxel can legitimately
    // hold exactly 0.0, which a value-based occupancy check would wrongly
    // treat as unoccupied. materialId==0 means empty (SerializeSdf leaves
    // non-alive voxel slots at materialId 0; a real bake always writes
    // Material{1u} or higher for a live voxel — see SdfBake.h).
    const uint32_t* brickMaterials =
        reinterpret_cast<const uint32_t*>(serialized.bricks.data()) +
        static_cast<size_t>(brickIndex) * SerializedOctree::kVoxelsPerBrick;

    for (uint32_t voxel = 0; voxel < SerializedOctree::kVoxelsPerBrick; ++voxel) {
        // z*64+y*8+x layout: octant = which half of each axis (top bit of
        // each 3-bit coordinate within the 8-wide brick).
        const uint32_t x = voxel & 0x7u;
        const uint32_t y = (voxel >> 3) & 0x7u;
        const uint32_t z = (voxel >> 6) & 0x7u;
        const uint32_t octant = (x >> 2) | ((y >> 2) << 1) | ((z >> 2) << 2);

        const bool occupied = brickMaterials[voxel] != 0u;
        if (!occupied) continue;

        const float value = serialized.readPoolVoxel(sem, brickIndex, voxel, comp);

        if (kind == FK_DISTANCE) {
            const float a = std::fabs(value);
            if (octantBestAbs[octant] < 0.0f || a < octantBestAbs[octant]) {
                octantBestAbs[octant] = a;
                octantBestOrSum[octant] = value;
            }
        } else {
            octantBestOrSum[octant] += value;
        }
        ++octantOccupiedCount[octant];
    }

    for (int o = 0; o < 8; ++o) {
        if (octantOccupiedCount[o] == 0) {
            octantGroups[o] = MipChildSample{0.0f, false};
        } else if (kind == FK_DISTANCE) {
            octantGroups[o] = MipChildSample{octantBestOrSum[o], true};
        } else {
            octantGroups[o] = MipChildSample{
                octantBestOrSum[o] / static_cast<float>(octantOccupiedCount[o]), true};
        }
    }

    return FilterMipSample(kind, octantGroups);
}

}  // namespace detail

// Compute the full MipPool for one already-baked+serialized octree.
// `oct` must be the SAME octree body's `SdfBodyOctree::octree->getOctree()`
// used to produce `serialized` (SerializeSdf's output) — this function reads
// oct->root->childDescriptors (node hierarchy) and serialized.channelPool
// (leaf voxel data) together.
inline MipPool BakeMipPool(const Octree& oct, const SerializedOctree& serialized) {
    MipPool pool;
    pool.channelCount = serialized.channelCount;
    pool.nodeCount = serialized.nodeCount;
    pool.samples.assign(static_cast<size_t>(pool.nodeCount) * pool.channelCount, MipSample{});

    if (!oct.root || pool.nodeCount == 0 || pool.channelCount == 0) {
        return pool;
    }

    const std::vector<ChildDescriptor>& nodes = oct.root->childDescriptors;
    const std::unordered_map<uint64_t, uint32_t>& leafToBrickView = oct.root->leafToBrickView;

    // Reverse index order over the BFS-ordered array visits every node AFTER
    // all of its children (see file header) — bottom-up, root-ward.
    for (uint32_t nodeIdxPlusOne = pool.nodeCount; nodeIdxPlusOne > 0; --nodeIdxPlusOne) {
        const uint32_t nodeIdx = nodeIdxPlusOne - 1;
        const ChildDescriptor& desc = nodes[nodeIdx];

        for (uint32_t ch = 0; ch < serialized.channelCount; ++ch) {
            const SemanticId sem = static_cast<SemanticId>(serialized.channels[ch].semanticId);
            const FieldKind kind = static_cast<FieldKind>(serialized.channels[ch].fieldKind);
            const uint32_t elemCount = serialized.channels[ch].elemCount;
            // Mip samples are one scalar lane per component; component 0 is
            // sufficient for the filter-semantics contract this increment
            // proves (Task 1's dispatch operates per-lane already). Multi-
            // component channels (color) fill lane 0 here; later increments
            // may extend to per-component storage if a consumer needs it.
            const uint32_t comp = 0;
            (void)elemCount;

            std::array<MipChildSample, 8> children{};
            bool anyChild = false;

            for (int octant = 0; octant < 8; ++octant) {
                if (!desc.hasChild(octant)) continue;
                anyChild = true;

                if (desc.isLeaf(octant)) {
                    // Leaf child: look up its brick via (nodeIdx, octant).
                    const uint64_t key = (static_cast<uint64_t>(nodeIdx) << 3) |
                                          static_cast<uint64_t>(octant);
                    auto it = leafToBrickView.find(key);
                    if (it == leafToBrickView.end()) continue;
                    const uint32_t brickIndex = it->second;
                    MipSample leafSample = detail::ReduceBrickToMipSample(
                        serialized, brickIndex, sem, kind, comp);
                    children[octant] = MipChildSample{leafSample.value, leafSample.coverage > 0.0f};
                } else {
                    // Non-leaf child: its mip sample was already computed
                    // (higher index, processed earlier in this reverse walk).
                    // Children are stored contiguously from childPointer;
                    // BFS-order pushes non-leaf children first, then leaf
                    // children (SVORebuild.cpp PHASE 3) — so a non-leaf
                    // child's position within that contiguous run is NOT
                    // simply `octant`. Recompute the same nonleaf-first
                    // ordering rebuild() used: count how many valid children
                    // with LOWER octant index are also non-leaf, to get this
                    // child's position in the contiguous nonleaf run.
                    uint32_t nonLeafPosition = 0;
                    for (int prior = 0; prior < octant; ++prior) {
                        if (desc.hasChild(prior) && !desc.isLeaf(prior)) ++nonLeafPosition;
                    }
                    const uint32_t resolvedChildIdx = desc.childPointer + nonLeafPosition;
                    if (resolvedChildIdx >= pool.nodeCount) continue;

                    const MipSample childSample = pool.Get(resolvedChildIdx, ch);
                    children[octant] = MipChildSample{childSample.value, childSample.coverage > 0.0f};
                }
            }

            if (anyChild) {
                pool.samples[static_cast<size_t>(nodeIdx) * pool.channelCount + ch] =
                    FilterMipSample(kind, children);
            }
        }
    }

    // ---------------------------------------------------------------------
    // Leaf-zero-sample fix (M3 Task 7 callout, option (a)): the loop above
    // only ever writes pool.samples[nodeIdx] when nodeIdx is visited as a
    // PARENT (a node with children). A brick-level LEAF's own descriptor
    // slot (the nodeIdx the shader's leafDescriptorIndex addresses at
    // BodyInstanceRayMarch.comp's handleLeafHitInstanced/Sdf) is never a
    // parent — it has no children of its own — so without this pass its
    // mip sample stays at the zero-initialized default. That default reads
    // as "everything zero" (SDF value=0.0, i.e. ON the surface) whenever
    // the shader falls back to mip[level][ordinal] for a non-resident leaf,
    // which is a false surface crossing, not a graceful coarse shade.
    //
    // Fix: for every leaf child recorded in leafToBrickView, resolve its OWN
    // final nodeIdx (not just its brick index) and fill pool.samples[leafIdx]
    // with the same brick-reduction this leaf already contributes to its
    // parent's sample (detail::ReduceBrickToMipSample) — so a leaf queried
    // directly reads its own brick's filtered value, identical in kind to
    // what an ancestor would show one level up.
    for (uint32_t nodeIdx = 0; nodeIdx < pool.nodeCount; ++nodeIdx) {
        const ChildDescriptor& desc = nodes[nodeIdx];
        // Leaf children are appended contiguously after all non-leaf children
        // in the same run starting at desc.childPointer (SVORebuild.cpp PHASE 3:
        // "allChildren = nonLeafChildren then leafChildren"). Resolve each leaf
        // child's own final index the same way the shader's
        // handleLeafHitInstanced does (childPointer + totalInternalChildren +
        // leafChildrenBeforeMe), rather than assuming octant order.
        const uint32_t totalNonLeafChildren =
            static_cast<uint32_t>(std::popcount(static_cast<uint8_t>(desc.validMask & ~desc.leafMask)));

        uint32_t leafPosition = 0;
        for (int octant = 0; octant < 8; ++octant) {
            if (!desc.hasChild(octant) || !desc.isLeaf(octant)) continue;

            const uint64_t key = (static_cast<uint64_t>(nodeIdx) << 3) | static_cast<uint64_t>(octant);
            auto it = leafToBrickView.find(key);
            if (it == leafToBrickView.end()) { ++leafPosition; continue; }
            const uint32_t brickIndex = it->second;

            const uint32_t leafNodeIdx = desc.childPointer + totalNonLeafChildren + leafPosition;
            ++leafPosition;
            if (leafNodeIdx >= pool.nodeCount) continue;

            for (uint32_t ch = 0; ch < serialized.channelCount; ++ch) {
                const SemanticId sem = static_cast<SemanticId>(serialized.channels[ch].semanticId);
                const FieldKind kind = static_cast<FieldKind>(serialized.channels[ch].fieldKind);
                const MipSample leafSample = detail::ReduceBrickToMipSample(
                    serialized, brickIndex, sem, kind, /*comp=*/0);
                pool.samples[static_cast<size_t>(leafNodeIdx) * pool.channelCount + ch] = leafSample;
            }
        }
    }

    return pool;
}

// Serialize a MipPool's samples into raw bytes for SerializedOctree::mipPool
// (Task 3) — stride sizeof(MipSample), SoA order [nodeIdx * channelCount + ch],
// matching MipPool::Get's own addressing exactly (a straight memcpy of the
// samples vector, since MipSample is a plain 2-float POD).
inline std::vector<uint8_t> SerializeMipPool(const MipPool& pool) {
    static_assert(sizeof(MipSample) == 2 * sizeof(float), "MipSample must be a plain 2-float POD");
    std::vector<uint8_t> bytes(pool.samples.size() * sizeof(MipSample));
    if (!pool.samples.empty()) {
        std::memcpy(bytes.data(), pool.samples.data(), bytes.size());
    }
    return bytes;
}

// Bake + serialize in one call: compute the MipPool for `body`'s octree
// against its own `serialized` (already produced by SerializeSdf), write the
// resulting bytes into `serialized.mipPool`. Convenience wrapper — Task 3's
// test may also call BakeMipPool/SerializeMipPool directly for finer control.
inline void BakeAndAttachMipPool(const Octree& oct, SerializedOctree& serialized) {
    MipPool pool = BakeMipPool(oct, serialized);
    serialized.mipPool = SerializeMipPool(pool);
}

// ===========================================================================
// ConcatenateSdfWithMips — ConcatenateSdf (ShellOctreeGpu.h) + mip-pool bake
// ===========================================================================
// ConcatenateSdf lives in ShellOctreeGpu.h, which MipBake.h already depends
// on; baking mips FROM ShellOctreeGpu.h would be a circular include, so the
// mip-aware concatenation entry point lives here instead. This mirrors
// ConcatenateSdf's own per-octree loop exactly (same nodeBase/brickBase/
// poolBase bookkeeping, same append order) and additionally bakes+attaches
// each octree's mip pool before appending it — "ConcatenateSdf appends each
// octree's mipPool... same pattern as channelPool/brickGridLookup" (Task 3),
// realized as this sibling entry point so ConcatenateSdf itself (and its
// existing non-mip callers) stays unchanged.
inline ConcatenatedOctrees ConcatenateSdfWithMips(const std::vector<const SdfBodyOctree*>& octrees) {
    ConcatenatedOctrees cat;
    cat.count = static_cast<uint32_t>(octrees.size());
    cat.configs.resize(octrees.size());
    cat.nodeCounts.resize(octrees.size());
    cat.brickCounts.resize(octrees.size());

    uint32_t nodeBase    = 0;
    uint32_t brickBase   = 0;
    uint32_t poolBase    = 0;
    uint32_t mipPoolBase = 0;

    for (size_t k = 0; k < octrees.size(); ++k) {
        if (octrees[k] == nullptr) {
            throw std::invalid_argument("MipBake::ConcatenateSdfWithMips: null octree pointer");
        }
        SerializedOctree s = SerializeSdf(*octrees[k]);
        const Octree* oct = octrees[k]->octree->getOctree();
        if (oct != nullptr) {
            BakeAndAttachMipPool(*oct, s);
        }

        s.config.nodeArrayBase  = static_cast<int32_t>(nodeBase);
        s.config.brickArrayBase = static_cast<int32_t>(brickBase);
        setSdfBrickArrayBase(s.config, poolBase);
        setMipPoolBase(s.config, mipPoolBase);

        cat.configs[k]     = s.config;
        cat.nodeCounts[k]  = s.nodeCount;
        cat.brickCounts[k] = s.brickCount;

        cat.nodes.insert(cat.nodes.end(),   s.nodes.begin(),   s.nodes.end());
        cat.bricks.insert(cat.bricks.end(), s.bricks.begin(),  s.bricks.end());
        cat.channelPool.insert(cat.channelPool.end(),
                               s.channelPool.begin(), s.channelPool.end());
        cat.brickGridLookup.insert(cat.brickGridLookup.end(),
                                   s.brickGridLookup.begin(), s.brickGridLookup.end());
        cat.mipPool.insert(cat.mipPool.end(), s.mipPool.begin(), s.mipPool.end());

        if (cat.materials.empty()) {
            cat.materials = std::move(s.materials);
        }

        nodeBase  += s.nodeCount;
        brickBase += s.brickCount;
        poolBase  += s.brickCount * s.brickStrideFloats;
        mipPoolBase += s.nodeCount * s.channelCount;
    }

    return cat;
}

}  // namespace Vixen::SVO
