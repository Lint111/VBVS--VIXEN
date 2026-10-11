#pragma once
// EmitterPower.h - R502.2 sparse emitter power lane (count-agnostic lighting, S0).
//
// A per-octree power lane appended to the mip stream (see SerializedOctree::emitterPowerPool).
// Word layout, little-endian 32-bit words, padded to an even word count so the lane is a
// whole number of MipSample (8 B) units:
//   [0]                     emissiveLeafCount (uint)
//   [1]                     root power, float   (== sum over leaves)
//   [2]                     nodeCount N (uint)
//   [3 .. 3+N)              phi per node, float (BFS node ordinal)
//   [3+N .. 3+N+L)          emissive leaf node ordinals, uint, ascending (L = emissiveLeafCount)
//   [3+N+L .. 3+N+9L)       8 octant sums per emissive leaf, float, same order (32 B per brick)
// Power is in octree-normalized units: phi_voxel = emission * (1 / (bricksPerAxis * 8))^2,
// i.e. emission-weighted area in the octree's own [0,1]^3 frame. A consumer scales by
// (kWorldGridSize * instanceScale)^2 and applies pi. Non-emissive trees bake an EMPTY lane,
// so far-field mip bytes and tree sizes stay unchanged for them.
//
// Far-field shading bytes are untouched: the lane is a separate pool, never mixed into
// mipPool's value/coverage samples.

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "ShellOctreeGpu.h"
#include "SVOBuilder.h"
#include "SVOTypes.h"

namespace Vixen::SVO {

inline constexpr uint32_t kEmitterPowerHeaderWords = 3u;
inline constexpr uint32_t kEmitterPowerOctantWords = 8u;

struct EmitterPowerLaneView {
    const uint32_t* words = nullptr;
    uint32_t wordCount = 0u;
    uint32_t nodeCount = 0u;

    bool Valid() const { return words != nullptr && wordCount >= kEmitterPowerHeaderWords; }
    uint32_t LeafCount() const { return Valid() ? words[0] : 0u; }
    float RootPower() const {
        float v = 0.0f;
        if (Valid()) std::memcpy(&v, &words[1], sizeof(float));
        return v;
    }
    float NodePower(uint32_t nodeIdx) const {
        float v = 0.0f;
        if (Valid() && nodeIdx < nodeCount)
            std::memcpy(&v, &words[kEmitterPowerHeaderWords + nodeIdx], sizeof(float));
        return v;
    }
    uint32_t LeafNode(uint32_t leafSlot) const {
        return words[kEmitterPowerHeaderWords + nodeCount + leafSlot];
    }
    float LeafOctantPower(uint32_t leafSlot, uint32_t octant) const {
        float v = 0.0f;
        std::memcpy(&v,
                    &words[kEmitterPowerHeaderWords + nodeCount + LeafCount() +
                           leafSlot * kEmitterPowerOctantWords + octant],
                    sizeof(float));
        return v;
    }
    float LeafPower(uint32_t leafSlot) const {
        double s = 0.0;
        for (uint32_t o = 0; o < kEmitterPowerOctantWords; ++o) s += LeafOctantPower(leafSlot, o);
        return static_cast<float>(s);
    }
    // Binary search by node ordinal; returns leafSlot or 0xFFFFFFFF.
    uint32_t FindLeaf(uint32_t nodeIdx) const {
        uint32_t lo = 0u, hi = LeafCount();
        while (lo < hi) {
            const uint32_t mid = (lo + hi) >> 1u;
            const uint32_t v = LeafNode(mid);
            if (v < nodeIdx) lo = mid + 1u; else hi = mid;
        }
        return (lo < LeafCount() && LeafNode(lo) == nodeIdx) ? lo : 0xFFFFFFFFu;
    }
};

inline EmitterPowerLaneView ViewEmitterPowerLane(const uint8_t* bytes, size_t size) {
    EmitterPowerLaneView v;
    if (bytes == nullptr || size < kEmitterPowerHeaderWords * sizeof(uint32_t)) return v;
    v.words = reinterpret_cast<const uint32_t*>(bytes);
    v.wordCount = static_cast<uint32_t>(size / sizeof(uint32_t));
    v.nodeCount = v.words[2];
    return v;
}

inline EmitterPowerLaneView ViewEmitterPowerLane(const std::vector<uint8_t>& bytes) {
    return ViewEmitterPowerLane(bytes.data(), bytes.size());
}

// The lane of octree `octreeIdx` inside a concatenated mip stream; invalid view when the
// octree was not lane-baked (non-emissive, or a stream assembled without the lane).
template <typename ConcatenatedT>
inline EmitterPowerLaneView ViewEmitterPowerLaneOf(const ConcatenatedT& cat, uint32_t octreeIdx) {
    if (octreeIdx >= cat.configs.size() || !emitterPowerPoolBakedOf(cat.configs[octreeIdx])) return {};
    const size_t byteOffset = static_cast<size_t>(emitterPowerPoolBaseOf(cat.configs[octreeIdx])) * 8u;
    if (byteOffset >= cat.mipPool.size()) return {};
    return ViewEmitterPowerLane(cat.mipPool.data() + byteOffset, cat.mipPool.size() - byteOffset);
}

// Root power (octree-normalized units) of every octree in a concatenated stream; 0 where the
// octree carries no lane.
template <typename ConcatenatedT>
inline std::vector<float> EmitterRootPowers(const ConcatenatedT& cat) {
    std::vector<float> roots(cat.configs.size(), 0.0f);
    for (uint32_t i = 0; i < roots.size(); ++i) roots[i] = ViewEmitterPowerLaneOf(cat, i).RootPower();
    return roots;
}

namespace detail {

// Per-brick octant emission-weighted area sums (octant = (x>>2)|((y>>2)<<1)|((z>>2)<<2)).
inline std::array<double, 8> BrickOctantEmission(const SerializedOctree& serialized,
                                                 uint32_t brickIndex) {
    std::array<double, 8> sums{};
    const uint32_t* mats = reinterpret_cast<const uint32_t*>(serialized.bricks.data()) +
                           static_cast<size_t>(brickIndex) * SerializedOctree::kVoxelsPerBrick;
    for (uint32_t voxel = 0; voxel < SerializedOctree::kVoxelsPerBrick; ++voxel) {
        if (mats[voxel] == 0u) continue;
        const float e = serialized.readPoolVoxel(SEM_EMISSION, brickIndex, voxel, 0);
        if (!(e > 0.0f)) continue;
        const uint32_t x = voxel & 7u, y = (voxel >> 3) & 7u, z = (voxel >> 6) & 7u;
        sums[(x >> 2) | ((y >> 2) << 1) | ((z >> 2) << 2)] += static_cast<double>(e);
    }
    return sums;
}

}  // namespace detail

// Bake the power lane for one serialized octree. Returns empty bytes when the tree has no
// SEM_EMISSION channel or no emissive voxel.
inline std::vector<uint8_t> BakeEmitterPowerLane(const Octree& oct, const SerializedOctree& serialized) {
    if (!oct.root || serialized.nodeCount == 0u || serialized.brickCount == 0u) return {};
    if (serialized.channelBaseFloats(SEM_EMISSION) == 0xFFFFFFFFu) return {};
    const int bpa = serialized.config.bricksPerAxis;
    if (bpa <= 0) return {};

    const auto& nodes = oct.root->childDescriptors;
    const auto& leafToBrickView = oct.root->leafToBrickView;
    const uint32_t nodeCount = serialized.nodeCount;
    const double voxelEdge = 1.0 / (static_cast<double>(bpa) * 8.0);
    const double voxelArea = voxelEdge * voxelEdge;

    struct LeafEntry { uint32_t node; std::array<double, 8> oct; };
    std::vector<LeafEntry> leaves;
    std::vector<double> nodePhi(nodeCount, 0.0);
    std::vector<uint8_t> isLeafSlot(nodeCount, 0u);

    // Leaf nodes first (their own slots): brick octant sums, area-weighted.
    for (uint32_t nodeIdx = 0; nodeIdx < nodeCount && nodeIdx < nodes.size(); ++nodeIdx) {
        const ChildDescriptor& desc = nodes[nodeIdx];
        const uint32_t totalNonLeaf =
            static_cast<uint32_t>(std::popcount(static_cast<uint8_t>(desc.validMask & ~desc.leafMask)));
        uint32_t leafPosition = 0u;
        for (int octant = 0; octant < 8; ++octant) {
            if (!desc.hasChild(octant) || !desc.isLeaf(octant)) continue;
            const uint64_t key = (static_cast<uint64_t>(nodeIdx) << 3) | static_cast<uint64_t>(octant);
            const auto it = leafToBrickView.find(key);
            const uint32_t leafIdx = desc.childPointer + totalNonLeaf + leafPosition++;
            if (it == leafToBrickView.end() || leafIdx >= nodeCount) continue;
            // A leaf slot's own descriptor is not a parent (its validMask/childPointer are
            // not meaningful), so mark every real brick-backed leaf, emissive or not.
            isLeafSlot[leafIdx] = 1u;
            LeafEntry e{leafIdx, detail::BrickOctantEmission(serialized, it->second)};
            double total = 0.0;
            for (double& s : e.oct) { s *= voxelArea; total += s; }
            if (total <= 0.0) continue;
            nodePhi[leafIdx] = total;
            leaves.push_back(e);
        }
    }
    if (leaves.empty()) return {};

    // Bottom-up: reverse BFS order visits every node after its children (MipBake.h).
    for (uint32_t nodePlusOne = nodeCount; nodePlusOne > 0u; --nodePlusOne) {
        const uint32_t nodeIdx = nodePlusOne - 1u;
        if (nodeIdx >= nodes.size() || isLeafSlot[nodeIdx]) continue;
        const ChildDescriptor& desc = nodes[nodeIdx];
        const uint32_t totalNonLeaf =
            static_cast<uint32_t>(std::popcount(static_cast<uint8_t>(desc.validMask & ~desc.leafMask)));
        double sum = 0.0;
        uint32_t nonLeafPosition = 0u, leafPosition = 0u;
        for (int octant = 0; octant < 8; ++octant) {
            if (!desc.hasChild(octant)) continue;
            uint32_t childIdx;
            if (desc.isLeaf(octant)) childIdx = desc.childPointer + totalNonLeaf + leafPosition++;
            else childIdx = desc.childPointer + nonLeafPosition++;
            if (childIdx < nodeCount) sum += nodePhi[childIdx];
        }
        if (sum > 0.0) nodePhi[nodeIdx] = sum;
    }

    std::sort(leaves.begin(), leaves.end(),
              [](const LeafEntry& a, const LeafEntry& b) { return a.node < b.node; });

    const uint32_t L = static_cast<uint32_t>(leaves.size());
    uint32_t words = kEmitterPowerHeaderWords + nodeCount + L + L * kEmitterPowerOctantWords;
    words += words & 1u;  // whole MipSample units
    std::vector<uint8_t> bytes(static_cast<size_t>(words) * sizeof(uint32_t), 0u);
    auto* w = reinterpret_cast<uint32_t*>(bytes.data());
    auto putF = [&](uint32_t i, double v) { const float f = static_cast<float>(v); std::memcpy(&w[i], &f, 4); };
    w[0] = L;
    putF(1u, nodePhi[0]);
    w[2] = nodeCount;
    for (uint32_t n = 0; n < nodeCount; ++n) putF(kEmitterPowerHeaderWords + n, nodePhi[n]);
    const uint32_t idxBase = kEmitterPowerHeaderWords + nodeCount;
    const uint32_t octBase = idxBase + L;
    for (uint32_t i = 0; i < L; ++i) {
        w[idxBase + i] = leaves[i].node;
        for (uint32_t o = 0; o < 8; ++o) putF(octBase + i * 8u + o, leaves[i].oct[o]);
    }
    return bytes;
}

}  // namespace Vixen::SVO
