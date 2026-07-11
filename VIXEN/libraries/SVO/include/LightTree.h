#pragma once
// LightTree.h — Sampled Lighting Inc3 M3: mip-cut light-tree.
//
// A BOUNDED CUT through the emissive mip pyramid (MipBake.h's MipPool), turning
// a scene's emissive voxels ("millions of glowing voxels") into a handful of
// coarse aggregate emitter NODES a future ReSTIR candidate-generation pass
// (M4) samples from — the "light-tree for free" the plan describes.
//
// Traversal: descend the octree TOP-DOWN from the root (mirroring the same
// octant/child-resolution convention MipBake.h's bottom-up walk uses), reading
// each node's emissive MipSample. Stop descending (CUT) at a node when either:
//   (a) its aggregate emissive power (intensity * coverage, a proxy for total
//       radiant power over the node's footprint) is below `powerThreshold`, or
//   (b) it's a genuine leaf (brick) — can't descend further, or
//   (c) its world-space extent is below `minExtentThreshold` (very fine detail
//       isn't worth its own light-tree node; folds into the parent's cut).
// A node with ZERO emissive coverage (no emitter beneath it) is pruned
// entirely — it contributes no cut node.
//
// This is SCALAR/RGB-phenomenological (Sampled Lighting Inc3 M3 scope) — the
// mip pyramid this walks stores scalar intensity (VoxelChannelFormat.h's
// SEM_EMISSION), not spectral/temperature data. Forward-compatible: a future
// spectral fast-follow (Inc3b/M8+) would average a temperature channel up the
// SAME mip pyramid and this cut logic would carry over unchanged.
//
// World-space center/extent: the bake grid is [0,n)^3 in grid space (SdfBake.h),
// n a power of two; a node at BFS depth `d` covers a cube of side n/2^d. This
// module computes depth via ITS OWN top-down walk (independent of MipBake.h's
// bottom-up walk) since the finalized childDescriptors array carries no
// explicit per-node depth field.

#include "MipBake.h"
#include "MipSample.h"
#include "ShellOctreeGpu.h"
#include "SVOBuilder.h"
#include "SVOTypes.h"
#include "VoxelChannelFormat.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace Vixen::SVO {

// One cut node: an aggregate emitter approximating every emissive voxel
// beneath it in the mip pyramid as a single point/area light.
struct LightTreeNode {
    glm::vec3 worldPos{0.0f};   // node's cube center, in the SAME grid space SdfBake.h bakes into
    float     worldExtent = 0.0f;  // cube side length (grid units)
    float     intensity   = 0.0f;  // mean emissive intensity beneath this node (MipSample::value)
    float     coverage    = 0.0f;  // fraction of the node's footprint that is actually emissive (0..1]
};

// Cut parameters (mirrors the eventual ReservoirConfig.lightTreeCutThreshold —
// this CPU-side struct is the pre-GPU-config scaffolding M3 needs to validate
// the cut against the brute-force reference; ReservoirConfig wires the same
// knob for the GPU path in a later milestone).
struct LightTreeCutParams {
    // Stop descending when intensity*coverage falls below this (a node whose
    // aggregate power is negligible isn't worth its own light-tree node).
    float powerThreshold = 0.01f;
    // Stop descending when a node's world-space cube extent falls below this
    // (very fine detail folds into its parent's cut rather than fragmenting
    // the light-tree into near-leaf-level nodes).
    float minExtentThreshold = 0.0f;
};

namespace detail {

// Resolve a child's final nodeIdx from its parent's descriptor + octant —
// EXACT copy of MipBake.h's own resolution rule (BFS non-leaf-children-first,
// then leaf-children, per SVORebuild.cpp PHASE 3) so both walks agree on which
// physical node a given (parentIdx, octant) pair addresses.
inline uint32_t ResolveChildNodeIdx(const ChildDescriptor& desc, int octant) {
    uint32_t nonLeafPosition = 0;
    for (int prior = 0; prior < octant; ++prior) {
        if (desc.hasChild(prior) && !desc.isLeaf(prior)) ++nonLeafPosition;
    }
    if (desc.isLeaf(octant)) {
        const uint32_t totalNonLeafChildren = static_cast<uint32_t>(
            std::popcount(static_cast<uint8_t>(desc.validMask & ~desc.leafMask)));
        uint32_t leafPosition = 0;
        for (int prior = 0; prior < octant; ++prior) {
            if (desc.hasChild(prior) && desc.isLeaf(prior)) ++leafPosition;
        }
        return desc.childPointer + totalNonLeafChildren + leafPosition;
    }
    return desc.childPointer + nonLeafPosition;
}

// Octant bit convention: bit0=x-half, bit1=y-half, bit2=z-half (matches
// MipBake.h's brick-level octant = (x>>2)|((y>>2)<<1)|((z>>2)<<2) and the
// same low-to-high axis order used throughout SVO octant addressing).
inline glm::vec3 OctantOffset(int octant) {
    return glm::vec3(
        static_cast<float>(octant & 1),
        static_cast<float>((octant >> 1) & 1),
        static_cast<float>((octant >> 2) & 1));
}

}  // namespace detail

// Build the mip-cut light-tree for one already-baked+mip-baked octree.
// `serialized.mipPool` must already be populated (BakeAndAttachMipPool /
// ConcatenateSdfWithMips) — this function reads it, it does not bake it.
// `gridN` is the bake grid's side length (SdfBakeResult::n) — the root node's
// worldExtent; `gridOrigin` is the bake grid's own origin (grid space is
// [gridOrigin, gridOrigin+gridN) — SdfBake.h always bakes into [0,n), so the
// default (0,0,0) is correct for every existing caller).
inline std::vector<LightTreeNode> BuildLightTreeCut(
    const Octree& oct, const SerializedOctree& serialized,
    const MipPool& mipPool, int gridN,
    const LightTreeCutParams& params = LightTreeCutParams{},
    const glm::vec3& gridOrigin = glm::vec3(0.0f)) {
    std::vector<LightTreeNode> cut;

    if (!oct.root || serialized.nodeCount == 0 || serialized.channelCount == 0) {
        return cut;
    }

    // Locate the emissive channel; a channel table without SEM_EMISSION means
    // this content never bakes emission (byte-identity default) -> empty cut.
    uint32_t emissionChannel = 0xFFFFFFFFu;
    for (uint32_t ci = 0; ci < serialized.channelCount; ++ci) {
        if (serialized.channels[ci].semanticId == static_cast<uint32_t>(SEM_EMISSION)) {
            emissionChannel = ci;
            break;
        }
    }
    if (emissionChannel == 0xFFFFFFFFu) {
        return cut;
    }

    const std::vector<ChildDescriptor>& nodes = oct.root->childDescriptors;

    struct StackEntry {
        uint32_t nodeIdx;
        glm::vec3 center;
        float extent;
        bool isLeaf;   // true if THIS node is a brick-level leaf (no children
                       // of its own — known from the PARENT's descriptor at
                       // push time; the finalized childDescriptors array does
                       // not otherwise distinguish leaf vs interior at a given
                       // nodeIdx without a reverse lookup, see file header).
    };
    // Root: BFS order places it at index 0 (SVORebuild.cpp pushes the root
    // first — see MipBake.h's identical assumption, verified by its own tests).
    // The root itself is never a brick-level leaf (SdfBake.h's fixture always
    // produces at least one interior level).
    std::vector<StackEntry> stack;
    stack.push_back(StackEntry{
        0u,
        gridOrigin + glm::vec3(static_cast<float>(gridN) * 0.5f),
        static_cast<float>(gridN),
        false});

    while (!stack.empty()) {
        const StackEntry entry = stack.back();
        stack.pop_back();
        if (entry.nodeIdx >= mipPool.nodeCount) continue;

        const MipSample sample = mipPool.Get(entry.nodeIdx, emissionChannel);
        const float power = sample.value * sample.coverage;

        // Prune: nothing emissive beneath this node.
        if (sample.coverage <= 0.0f || power <= 0.0f) continue;

        const bool belowPowerThreshold  = power < params.powerThreshold;
        const bool belowExtentThreshold = entry.extent <= params.minExtentThreshold;

        if (entry.isLeaf || belowPowerThreshold || belowExtentThreshold ||
            entry.nodeIdx >= nodes.size()) {
            cut.push_back(LightTreeNode{entry.center, entry.extent, sample.value, sample.coverage});
            continue;
        }

        // Descend: push every child (leaf or interior) as its own stack
        // entry — a leaf child carries isLeaf=true so it cuts unconditionally
        // on its own turn next iteration, uniformly with the threshold rules.
        const ChildDescriptor& desc = nodes[entry.nodeIdx];
        const float childExtent = entry.extent * 0.5f;
        for (int octant = 0; octant < 8; ++octant) {
            if (!desc.hasChild(octant)) continue;
            const uint32_t childIdx = detail::ResolveChildNodeIdx(desc, octant);
            const glm::vec3 childCenter =
                entry.center - glm::vec3(childExtent * 0.5f) +
                detail::OctantOffset(octant) * childExtent;
            stack.push_back(StackEntry{childIdx, childCenter, childExtent, desc.isLeaf(octant)});
        }
    }

    return cut;
}

// ===========================================================================
// Brute-force reference: sum the TRUE emissive power over every occupied
// voxel with EmissionIntensity > 0, no cut, no reservoirs — the ground-truth
// total this whole increment's cut approximation is validated against
// (Sampled Lighting Inc3 M3 Task 3's "brute-force reference" deliverable).
// Returns Sum(intensity) over every occupied voxel across every brick, i.e.
// the SAME aggregate quantity a light-tree cut approximates as
// Sum(node.intensity * node.coverage * (voxels-per-node)) — see
// LightTreeCutTotalPower below for the cut-side equivalent, so the two totals
// are directly comparable.
// ===========================================================================
inline double BruteForceTotalEmissivePower(const SerializedOctree& serialized) {
    double total = 0.0;
    for (uint32_t bi = 0; bi < serialized.brickCount; ++bi) {
        const uint32_t* mats = reinterpret_cast<const uint32_t*>(serialized.bricks.data()) +
                                static_cast<size_t>(bi) * SerializedOctree::kVoxelsPerBrick;
        for (uint32_t voxel = 0; voxel < SerializedOctree::kVoxelsPerBrick; ++voxel) {
            if (mats[voxel] == 0u) continue;  // unoccupied
            total += static_cast<double>(serialized.readPoolVoxel(SEM_EMISSION, bi, voxel, 0));
        }
    }
    return total;
}

// Cut-side total power, in the SAME units as BruteForceTotalEmissivePower:
// each cut node approximates `coverage * (voxel count under this node)`
// occupied voxels, each contributing `intensity`. `leafVoxelCount(extent)` is
// the number of physical voxels a node of this world-space extent spans —
// callers pass extent^3 for a dense grid (SdfBake.h's convention: one grid
// unit == one voxel), matching BruteForceTotalEmissivePower's per-voxel sum.
inline double LightTreeCutTotalPower(const std::vector<LightTreeNode>& cut) {
    double total = 0.0;
    for (const LightTreeNode& n : cut) {
        const double voxelsUnderNode = static_cast<double>(n.worldExtent) *
                                        static_cast<double>(n.worldExtent) *
                                        static_cast<double>(n.worldExtent);
        total += static_cast<double>(n.intensity) * static_cast<double>(n.coverage) * voxelsUnderNode;
    }
    return total;
}

}  // namespace Vixen::SVO
