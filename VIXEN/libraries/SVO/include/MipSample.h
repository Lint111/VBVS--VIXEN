#pragma once
// MipSample.h — Sparse-Mip ESVO LOD Inc1, Task 1.
//
// A per-level filtered value sample, one per octree interior node, one lane
// per live channel (read-by-semantic, mirrors VoxelChannelFormat.h's
// SemanticId/FieldKind — NOT a new parallel format). Filled bottom-up during
// bake (Task 2), read by the traversal shader as the fallback whenever a
// leaf's brick is not resident or the ray's LOD cutoff says "stop here"
// (Sparse-Mip-ESVO-LOD-Direction-2026-07.md).
//
// Filtering semantics per channel (direction doc, "Design decisions" #3):
//   - SEM_COLOR / SEM_ROUGHNESS / other non-SDF channels: weighted mean by
//     child coverage (coverage = fraction of the 8 child octants non-empty).
//   - SEM_SDF (FK_DISTANCE): conservative MIN-MAGNITUDE, not mean — the
//     child sample closest to the surface (smallest |distance|), sign
//     preserved. Mean-filtering an SDF across a level can average a
//     near-surface negative sample with a far-interior very-negative sample
//     into a value whose SIGN or zero-crossing no longer matches where the
//     true surface actually is at that level (the Inc3 "solid=Density>0"
//     lesson recurring one level up: don't blend a field whose ZERO is
//     load-bearing).

#include <array>
#include <cmath>
#include <cstdint>

#include "VoxelChannelFormat.h"

namespace Vixen::SVO {

// One child's contribution to a parent-level mip sample: a single scalar
// lane's value, plus whether this child slot is occupied at all (an empty
// child octant contributes no coverage and is excluded from the mean).
//
// KI-047 (weighted propagation): `coverage` carries the child's OWN fractional
// occupancy so the parent can sum fractions instead of counting booleans.
// Before this, a parent's coverage was (#children with ANY content)/8, so a
// child that was itself 1/8 covered contributed exactly as much as a fully
// solid one — sub-child sparsity was erased at every level and coverage
// saturated to 1.0 a level or two above the bricks (measured: covMin≡covMax≡1).
// A child with occupied=true and coverage left at 0 is treated as fully
// covered (1.0), so every pre-KI-047 construction site keeps its old meaning.
struct MipChildSample {
    float value = 0.0f;
    bool occupied = false;
    float coverage = 0.0f;

    // Effective fractional weight this child contributes to its parent.
    [[nodiscard]] float Weight() const {
        if (!occupied) return 0.0f;
        return coverage > 0.0f ? coverage : 1.0f;
    }
};

// A per-node, per-channel mip sample: one filtered scalar lane. Multi-
// component channels (e.g. SEM_COLOR) are filtered one component at a time —
// callers loop over elemCount and store one MipSample per component, exactly
// mirroring the channelPool's per-component lane layout
// (ShellOctreeGpu.h SerializeSdf: "comp*512 + voxel" addressing).
struct MipSample {
    float value = 0.0f;   // filtered value for this channel/component lane
    float coverage = 0.0f; // fraction of child octants (0..8) that were occupied
};

// Weighted-mean filter (color/roughness/etc): average the occupied children's
// values, weighted equally per occupied child (coverage-weighted mean —
// unoccupied children don't pull the average toward a default/zero value).
// Returns value=0, coverage=0 if no child is occupied (fully empty node).
inline MipSample FilterMipMean(const std::array<MipChildSample, 8>& children) {
    float sum = 0.0f;
    int occupiedCount = 0;
    float weightSum = 0.0f;
    for (const MipChildSample& c : children) {
        if (c.occupied) {
            sum += c.value;
            ++occupiedCount;
            weightSum += c.Weight();
        }
    }
    MipSample out;
    if (occupiedCount > 0) {
        out.value = sum / static_cast<float>(occupiedCount);
        // KI-047: sum of the children's OWN fractional coverage, not a count of
        // nonempty children. Identical to the old occupiedCount/8 whenever every
        // occupied child is fully covered (Weight()==1).
        out.coverage = weightSum / 8.0f;
    }
    return out;
}

// Conservative min-magnitude filter (SDF / FK_DISTANCE): pick the occupied
// child sample with the smallest |value| (closest to the zero-crossing /
// surface), preserving its sign. This is deliberately NOT a mean — averaging
// SDF values across a level can shift the apparent zero-crossing away from
// where the finer levels actually place the surface (see adversarial
// regression case in test_mip_sample_filter.cpp). Returns value=0, coverage=0
// if no child is occupied.
inline MipSample FilterMipMinMagnitude(const std::array<MipChildSample, 8>& children) {
    float best = 0.0f;
    float bestAbs = -1.0f;
    int occupiedCount = 0;
    float weightSum = 0.0f;
    for (const MipChildSample& c : children) {
        if (!c.occupied) continue;
        ++occupiedCount;
        weightSum += c.Weight();
        const float a = std::fabs(c.value);
        if (bestAbs < 0.0f || a < bestAbs) {
            bestAbs = a;
            best = c.value;
        }
    }
    MipSample out;
    if (occupiedCount > 0) {
        out.value = best;
        // KI-047: see FilterMipMean — fractional sum, not nonempty-child count.
        out.coverage = weightSum / 8.0f;
    }
    return out;
}

// Dispatch by channel semantics — the one place that decides mean vs
// min-magnitude, so callers (bake pass, tests) never have to re-derive the
// per-semantic rule. FK_DISTANCE (SDF) uses min-magnitude; everything else
// (FK_NONE/FK_DENSITY-tagged color, roughness, etc.) uses weighted mean.
inline MipSample FilterMipSample(FieldKind kind, const std::array<MipChildSample, 8>& children) {
    if (kind == FK_DISTANCE) {
        return FilterMipMinMagnitude(children);
    }
    return FilterMipMean(children);
}

}  // namespace Vixen::SVO
