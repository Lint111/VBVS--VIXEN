#pragma once

// CPU parity for the generated GB1 tile-list stages.  This deliberately mirrors
// the authored TileBinningSystem/TileBinSortSystem bodies instead of exercising
// Vulkan.  Bounds and every array access are checked here so a bad generated
// loop cannot be hidden by a device-loss-only witness.

#include "Generated/InstanceBound.g.h"
#include "Generated/TileBinCounter.g.h"
#include "Generated/TileBinStats.g.h"
#include "Generated/TileBinView.g.h"
#include "Generated/TileCandidate.g.h"
#include "Generated/TileHeader.g.h"
#include "Generated/TileSortedCandidate.g.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Vixen::RenderGraph::Mirror {

using Vixen::Codegen::Gpu::InstanceBound;
using Vixen::Codegen::Gpu::TileBinCounter;
using Vixen::Codegen::Gpu::TileBinView;
using Vixen::Codegen::Gpu::TileCandidate;
using Vixen::Codegen::Gpu::TileHeader;
using Vixen::Codegen::Gpu::TileSortedCandidate;

struct TileRectMirror {
    int minX;
    int minY;
    int maxX;
    int maxY;
    float nearDepth;
    int cull;
};

struct TileBinMirrorMetrics {
    uint64_t scatterInstanceIterations = 0;
    uint64_t scatterTileIterations = 0;
    uint64_t sortTileIterations = 0;
    uint64_t sortCopyIterations = 0;
    uint64_t sortCompareIterations = 0;
    uint64_t consumerPixelIterations = 0;
    uint64_t consumerCandidateIterations = 0;
    uint32_t maximumOffered = 0;
    uint32_t overflowed = 0;
    uint64_t consumerFallbackPixels = 0;
    uint64_t legacyFullSlabCompareIterations = 0;
};

struct TileBinMirrorResult {
    std::vector<TileBinCounter> counters;
    std::vector<TileCandidate> candidates;
    std::vector<TileHeader> headers;
    std::vector<TileSortedCandidate> sortedCandidates;
    TileBinMirrorMetrics metrics;
};

// Records a failed mirror check on the running test. It neither throws nor
// relies on assert(), which the Release (-DNDEBUG) test build compiles out.
inline bool MirrorRequire(bool condition, const char* message) {
    if (!condition) ADD_FAILURE() << "GB1 CPU mirror: " << message;
    return condition;
}

inline int MirrorCapacity(int listCapacityLog2) {
    MirrorRequire(listCapacityLog2 >= 0 && listCapacityLog2 < 31,
                  "listCapacityLog2 outside the checked shift range");
    return 1 << listCapacityLog2;
}

inline int MirrorNextPowerOfTwo(int value) {
    MirrorRequire(value >= 0, "negative sort count");
    int result = 1;
    while (result < value) {
        if (!MirrorRequire(result <= (std::numeric_limits<int>::max() >> 1),
                           "sort bound overflow"))
            return result;
        result <<= 1;
    }
    return result;
}

inline TileRectMirror MirrorProjectBound(float centerX, float centerY, float centerZ,
                                         float radius, const TileBinView& view) {
    const float vx = view.viewXx * centerX + view.viewXy * centerY +
                     view.viewXz * centerZ + view.viewXw;
    const float vy = view.viewYx * centerX + view.viewYy * centerY +
                     view.viewYz * centerZ + view.viewYw;
    const float vz = view.viewZx * centerX + view.viewZy * centerY +
                     view.viewZz * centerZ + view.viewZw;
    const float nearDepth = vz - radius;
    const float farDepth = vz + radius;
    const int lastX = view.tilesX - 1;
    const int lastY = view.tilesY - 1;
    MirrorRequire(view.tilesX > 0 && view.tilesY > 0, "non-positive tile extent");
    if (farDepth <= view.nearPlane)
        return {0, 0, -1, -1, 0.0f, 1};
    if (nearDepth <= view.nearPlane)
        return {0, 0, lastX, lastY, view.nearPlane, 0};

    const float left = vx - radius;
    const float right = vx + radius;
    const float bottom = vy - radius;
    const float top = vy + radius;
    const float leftDepth = left < 0.0f ? nearDepth : farDepth;
    const float rightDepth = right > 0.0f ? nearDepth : farDepth;
    const float bottomDepth = bottom < 0.0f ? nearDepth : farDepth;
    const float topDepth = top > 0.0f ? nearDepth : farDepth;
    const float minPixelX = view.centerX + view.focalX * (left / leftDepth) - 0.5f;
    const float maxPixelX = view.centerX + view.focalX * (right / rightDepth) + 0.5f;
    const float minPixelY = view.centerY + view.focalY * (bottom / bottomDepth) - 0.5f;
    const float maxPixelY = view.centerY + view.focalY * (top / topDepth) + 0.5f;
    const float width = view.tileSize * static_cast<float>(view.tilesX);
    const float height = view.tileSize * static_cast<float>(view.tilesY);
    if (maxPixelX < 0.0f || maxPixelY < 0.0f || minPixelX >= width || minPixelY >= height)
        return {0, 0, -1, -1, nearDepth, 2};

    const int minX = std::max(static_cast<int>(std::floor(minPixelX / view.tileSize)), 0);
    const int minY = std::max(static_cast<int>(std::floor(minPixelY / view.tileSize)), 0);
    const int maxX = std::min(static_cast<int>(std::floor(maxPixelX / view.tileSize)), lastX);
    const int maxY = std::min(static_cast<int>(std::floor(maxPixelY / view.tileSize)), lastY);
    MirrorRequire(minX >= 0 && minY >= 0 && maxX < view.tilesX && maxY < view.tilesY,
                  "projected tile rectangle outside the tile grid");
    return {minX, minY, maxX, maxY, nearDepth, 0};
}

inline bool MirrorCandidateBefore(const TileSortedCandidate& a,
                                  const TileSortedCandidate& b) {
    return a.nearDepth < b.nearDepth ||
           (a.nearDepth == b.nearDepth && a.instance < b.instance);
}

inline uint64_t MirrorBitonicCompareCount(int capacity) {
    MirrorRequire(capacity > 0 && (capacity & (capacity - 1)) == 0,
                  "bitonic capacity is not a power of two");
    uint64_t levels = 0;
    for (int k = 2; k <= capacity; k <<= 1) {
        for (int j = k >> 1; j > 0; j >>= 1) ++levels;
    }
    return levels * static_cast<uint64_t>(capacity / 2);
}

// `previousDepth` is the image fed to imageLoad(viewDepth_previous).  The
// generated scatter shader samples it with the tile number as a linear pixel
// coordinate, so this mirror preserves that slightly unusual contract exactly.
// `persistentCounters` models the counter buffer that survives across frames
// (zero-initialised on allocation); null runs one frame on a fresh buffer.
inline TileBinMirrorResult RunTileBinMirror(
    const TileBinView& view,
    const std::vector<InstanceBound>& bounds,
    const std::vector<float>& previousDepth,
    int imageWidth,
    int imageHeight,
    std::vector<TileBinCounter>* persistentCounters = nullptr) {
    MirrorRequire(imageWidth > 0 && imageHeight > 0, "invalid depth image extent");
    MirrorRequire(view.instanceCount >= 0 &&
                      static_cast<size_t>(view.instanceCount) <= bounds.size(),
                  "instanceCount exceeds the bound stream");
    MirrorRequire(previousDepth.size() == static_cast<size_t>(imageWidth) * imageHeight,
                  "depth image size mismatch");
    MirrorRequire(view.tilesX == (imageWidth + 7) / 8 &&
                      view.tilesY == (imageHeight + 7) / 8,
                  "tile extent does not match the 8x8 dispatch contract");

    const int capacity = MirrorCapacity(view.listCapacityLog2);
    const int tileCount = view.tilesX * view.tilesY;
    MirrorRequire(tileCount > 0, "empty tile grid");
    const size_t slabSize = static_cast<size_t>(tileCount) * capacity;

    TileBinMirrorResult result;
    if (persistentCounters && persistentCounters->size() == static_cast<size_t>(tileCount))
        result.counters = *persistentCounters;
    else
        result.counters.assign(static_cast<size_t>(tileCount), {});
    result.candidates.assign(slabSize,
                             {std::numeric_limits<float>::max(), UINT32_MAX});
    result.headers.assign(static_cast<size_t>(tileCount), {});
    result.sortedCandidates.assign(slabSize,
                                   {std::numeric_limits<float>::max(), UINT32_MAX});
    result.metrics.legacyFullSlabCompareIterations =
        static_cast<uint64_t>(tileCount) * MirrorBitonicCompareCount(capacity);

    std::vector<std::vector<TileCandidate>> expected(static_cast<size_t>(tileCount));
    for (int i = 0; i < view.instanceCount; ++i) {
        ++result.metrics.scatterInstanceIterations;
        const InstanceBound& bound = bounds[static_cast<size_t>(i)];
        const TileRectMirror rect = MirrorProjectBound(bound.centerX, bound.centerY,
                                                      bound.centerZ, bound.radius, view);
        if (rect.cull != 0) continue;

        for (int tileY = rect.minY; tileY <= rect.maxY; ++tileY) {
            for (int tileX = rect.minX; tileX <= rect.maxX; ++tileX) {
                ++result.metrics.scatterTileIterations;
                const int tile = tileY * view.tilesX + tileX;
                MirrorRequire(tile >= 0 && tile < tileCount, "scatter tile index outside grid");

                // The generated scatter appends with one atomic increment and never
                // claims, resets or waits on a tile; the sort below zeroes the count.
                TileBinCounter& counter = result.counters[static_cast<size_t>(tile)];

                const size_t depthX = static_cast<size_t>(tile % imageWidth);
                const size_t depthY = static_cast<size_t>(tile / imageWidth);
                MirrorRequire(depthY < static_cast<size_t>(imageHeight),
                              "depth sample index outside image");
                const float tileFarDepth = previousDepth[depthY * imageWidth + depthX];
                // TileBinOps.IsBehindTileDepth: a zero (cleared, never written)
                // history depth occludes nothing.
                if (tileFarDepth > 0.0f && rect.nearDepth > tileFarDepth) continue;

                std::vector<TileCandidate>& tileExpected = expected[static_cast<size_t>(tile)];
                tileExpected.push_back(TileCandidate{rect.nearDepth, static_cast<uint32_t>(i)});
                const int slot = counter.count++;
                MirrorRequire(slot >= 0, "negative scatter slot");
                if (slot >= capacity) {
                    ++result.metrics.overflowed;
                    continue;
                }
                const size_t entry = static_cast<size_t>(tile) * capacity +
                                     static_cast<size_t>(slot);
                MirrorRequire(entry < result.candidates.size(),
                              "scatter candidate index outside slab");
                result.candidates[entry] = TileCandidate{rect.nearDepth, static_cast<uint32_t>(i)};
            }
        }
    }

    for (int tile = 0; tile < tileCount; ++tile) {
        ++result.metrics.sortTileIterations;
        TileBinCounter& counter = result.counters[static_cast<size_t>(tile)];
        const int offered = counter.count;
        counter.count = 0;  // TileBinSortSystem consumes and zeroes the tile count.
        MirrorRequire(offered >= 0, "negative offered candidate count");
        result.metrics.maximumOffered = std::max(result.metrics.maximumOffered,
                                                  static_cast<uint32_t>(offered));
        // TileBinOps.StoredCount: an overflowing tile keeps its first `capacity` appends;
        // its header reports demand > count and the march falls back for it (R861.1).
        const int stored = std::min(offered, capacity);
        const size_t first = static_cast<size_t>(tile) * capacity;
        MirrorRequire(first + static_cast<size_t>(capacity) <= result.candidates.size(),
                      "sort slab outside candidate buffer");

        TileHeader& header = result.headers[static_cast<size_t>(tile)];
        header.index = tile;
        header.capacityLog2 = view.listCapacityLog2;
        header.demand = offered;
        header.count = stored;

        const int sortCapacity = stored == 0 ? 0 : MirrorNextPowerOfTwo(stored);
        MirrorRequire(sortCapacity <= capacity, "active sort bound exceeds tile slab");
        for (int slot = 0; slot < sortCapacity; ++slot) {
            ++result.metrics.sortCopyIterations;
            const size_t entry = first + static_cast<size_t>(slot);
            MirrorRequire(entry < result.sortedCandidates.size(),
                          "sort copy index outside sorted slab");
            result.sortedCandidates[entry] = slot < stored
                ? TileSortedCandidate{result.candidates[entry].nearDepth,
                                      result.candidates[entry].instance}
                : TileSortedCandidate{std::numeric_limits<float>::max(), UINT32_MAX};
        }

        for (int k = 2; k <= sortCapacity; k <<= 1) {
            for (int j = k >> 1; j > 0; j >>= 1) {
                for (int slot = 0; slot < sortCapacity; ++slot) {
                    const int other = slot ^ j;
                    if (other <= slot) continue;
                    ++result.metrics.sortCompareIterations;
                    const size_t aIndex = first + static_cast<size_t>(slot);
                    const size_t bIndex = first + static_cast<size_t>(other);
                    MirrorRequire(bIndex < result.sortedCandidates.size(),
                                  "sort compare index outside sorted slab");
                    bool shouldSwap = false;
                    if ((slot & k) == 0)
                        shouldSwap = MirrorCandidateBefore(result.sortedCandidates[bIndex],
                                                           result.sortedCandidates[aIndex]);
                    else
                        shouldSwap = MirrorCandidateBefore(result.sortedCandidates[aIndex],
                                                           result.sortedCandidates[bIndex]);
                    if (shouldSwap) std::swap(result.sortedCandidates[aIndex],
                                              result.sortedCandidates[bIndex]);
                }
            }
        }

        auto expectedTile = expected[static_cast<size_t>(tile)];
        std::sort(expectedTile.begin(), expectedTile.end(), [](const TileCandidate& a,
                                                               const TileCandidate& b) {
            return a.nearDepth < b.nearDepth ||
                   (a.nearDepth == b.nearDepth && a.instance < b.instance);
        });
        if (offered > capacity) continue;  // incomplete by design; the march does not read it
        MirrorRequire(expectedTile.size() == static_cast<size_t>(stored),
                      "CPU candidate output was truncated");
        for (size_t slot = 0; slot < expectedTile.size(); ++slot) {
            const auto& actual = result.sortedCandidates[first + slot];
            MirrorRequire(actual.instance == expectedTile[slot].instance &&
                              actual.nearDepth == expectedTile[slot].nearDepth,
                          "sorted candidate list differs from expected front-to-back order");
        }
    }

    // TraceWorld's primary consumer visits each pixel's tile list.  Keep the
    // same count/base/index checks in the mirror, including its tile stride.
    for (int pixelY = 0; pixelY < imageHeight; ++pixelY) {
        for (int pixelX = 0; pixelX < imageWidth; ++pixelX) {
            ++result.metrics.consumerPixelIterations;
            const int tile = (pixelX / 8) + (pixelY / 8) * view.tilesX;
            MirrorRequire(tile >= 0 && tile < tileCount, "consumer tile index outside grid");
            const TileHeader& header = result.headers[static_cast<size_t>(tile)];
            MirrorRequire(header.count >= 0 && header.count <= capacity,
                          "consumer count outside tile capacity");
            if (header.demand > header.count) {
                // TraceWorld: an overflowing tile marches the bounded instance loop.
                ++result.metrics.consumerFallbackPixels;
                continue;
            }
            const size_t base = static_cast<size_t>(header.index) * capacity;
            MirrorRequire(base + static_cast<size_t>(header.count) <= result.sortedCandidates.size(),
                          "consumer candidate base/count outside sorted buffer");
            for (int offset = 0; offset < header.count; ++offset) {
                ++result.metrics.consumerCandidateIterations;
                const auto& candidate = result.sortedCandidates[base + static_cast<size_t>(offset)];
                MirrorRequire(candidate.instance < static_cast<uint32_t>(view.instanceCount),
                              "consumer instance index outside instance stream");
            }
        }
    }
    if (persistentCounters) *persistentCounters = result.counters;
    return result;
}

}  // namespace Vixen::RenderGraph::Mirror
