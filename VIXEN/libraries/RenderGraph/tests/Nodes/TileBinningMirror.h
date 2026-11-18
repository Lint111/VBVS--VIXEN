#pragma once

// CPU parity for the generated GB1 tile stages (z-bin form). This deliberately mirrors the
// authored TileBinPrepareSystem / TileBinScatterSystem bodies and the march's word walk instead of
// exercising Vulkan. Bounds and every array access are checked here so a bad generated loop
// cannot be hidden by a device-loss-only witness.
//
// Contract checked: every tile's rank set, walked word by word and lowest bit first, yields
// exactly the tile's candidates ordered by (near depth, instance), which is the list the former
// per-tile sort produced.

#include "Generated/InstanceBand.g.h"
#include "Generated/InstanceBound.g.h"
#include "Generated/LodBand.g.hpp"
#include "Generated/RecipeLodBand.g.h"
#include "Generated/InstanceOrder.g.h"
#include "Generated/TileBinStats.g.h"
#include "Generated/TileBinView.g.h"
#include "Generated/TileRankWord.g.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Vixen::RenderGraph::Mirror {

using Vixen::Codegen::Gpu::InstanceBound;
using Vixen::Codegen::Gpu::InstanceOrder;
using Vixen::Codegen::Gpu::TileBinView;
using Vixen::Codegen::Gpu::TileRankWord;

struct TileRectMirror {
    int minX;
    int minY;
    int maxX;
    int maxY;
    float nearDepth;
    int cull;
};

struct TileCandidateMirror {
    float nearDepth;
    uint32_t instance;
};

struct TileBinMirrorMetrics {
    uint64_t scatterInstanceIterations = 0;
    uint64_t scatterTileIterations = 0;
    // Longest tile walk of one scatter invocation: the serial chain the GPU pays.
    uint64_t scatterLongestLaneIterations = 0;
    uint64_t rankCompareIterations = 0;
    uint64_t consumerPixelIterations = 0;
    uint64_t consumerCandidateIterations = 0;
    uint64_t consumerWordIterations = 0;
    uint32_t maximumOffered = 0;
    uint32_t overflowed = 0;
    uint64_t consumerFallbackPixels = 0;
};

struct TileBinMirrorResult {
    std::vector<InstanceOrder> order;
    std::vector<TileRankWord> words;
    // Candidates the march visits per tile, in visit order (instance ids).
    std::vector<std::vector<uint32_t>> visited;
    TileBinMirrorMetrics metrics;
};

// Records a failed mirror check on the running test. It neither throws nor
// relies on assert(), which the Release (-DNDEBUG) test build compiles out.
inline bool MirrorRequire(bool condition, const char* message) {
    if (!condition) ADD_FAILURE() << "GB1 CPU mirror: " << message;
    return condition;
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

// TileBinOps.RankDepth.
inline float MirrorRankDepth(const InstanceBound& bound, const TileBinView& view) {
    const float vz = view.viewZx * bound.centerX + view.viewZy * bound.centerY +
                     view.viewZz * bound.centerZ + view.viewZw;
    const float nearDepth = vz - bound.radius;
    if (vz + bound.radius <= view.nearPlane) return 3.402823e38f;
    return nearDepth <= view.nearPlane ? view.nearPlane : nearDepth;
}

// TileBinOps.LodQ (R860 RL0): pixel footprint at the bound's nearest distance over its diameter.
inline float MirrorLodQ(const InstanceBound& bound, const TileBinView& view) {
    if (bound.radius <= 0.0f) return 0.0f;
    const float vx = view.viewXx * bound.centerX + view.viewXy * bound.centerY +
                     view.viewXz * bound.centerZ + view.viewXw;
    const float vy = view.viewYx * bound.centerX + view.viewYy * bound.centerY +
                     view.viewYz * bound.centerZ + view.viewYw;
    const float vz = view.viewZx * bound.centerX + view.viewZy * bound.centerY +
                     view.viewZz * bound.centerZ + view.viewZw;
    const float distance = std::sqrt(vx * vx + vy * vy + vz * vz) - bound.radius;
    if (distance <= 0.0f) return 0.0f;
    return distance / (2.0f * bound.radius * view.focalY);
}

// TileBinPrepareSystem's band step: four table rows per ladder slot, the generated band callable,
// and the SelfRecipe row variant resolved to the instance's own recipe.
inline Vixen::Codegen::Gpu::InstanceBand MirrorInstanceBand(
    float q, uint32_t recipeId, uint32_t lodSlot, const std::vector<Vixen::Codegen::Gpu::RecipeLodBand>& table) {
    constexpr uint32_t kRows = 4u;
    constexpr uint32_t kSelfRecipe = 0xFFFFFFFFu;
    const std::size_t row = static_cast<std::size_t>(lodSlot) * kRows;
    MirrorRequire(row + kRows <= table.size(), "band table does not hold the instance's ladder slot");
    const uint32_t band = Vixen::Lod::SelectLodBand(q, table[row].maxQ, table[row + 1].maxQ,
                                                    table[row + 2].maxQ, kRows);
    MirrorRequire(band < kRows, "band outside the ladder's rows");
    const uint32_t variant = table[row + band].variantRecipeId;
    return {variant == kSelfRecipe ? recipeId : variant, band};
}

// TileBinOps.ScatterLanes: invocations per instance in the generated scatter.
inline int MirrorScatterLanes(const TileBinView& view) {
    constexpr int kScatterLaneCount = 256;
    constexpr int kTilePixels = 64;
    const int available = (view.tilesX - 1) * (view.tilesY - 1) * kTilePixels;
    if (view.instanceCount <= 0) return kScatterLaneCount;
    const int lanes = available / view.instanceCount;
    if (lanes > kScatterLaneCount) return kScatterLaneCount;
    return lanes < 1 ? 1 : lanes;
}

// TileBinOps.TileWords.
inline int MirrorTileWords(const TileBinView& view) {
    const int words = (view.instanceCount + 31) / 32;
    return words < view.wordCapacity ? words : view.wordCapacity;
}

inline bool MirrorCandidateBefore(float depth, uint32_t instance, float otherDepth,
                                  uint32_t otherInstance) {
    return depth < otherDepth || (depth == otherDepth && instance < otherInstance);
}

// `previousDepth` is the image fed to imageLoad(viewDepth_previous).  The
// generated scatter shader samples it with the tile number as a linear pixel
// coordinate, so this mirror preserves that slightly unusual contract exactly.
// `persistentWords` models the rank-word buffer that survives across frames (garbage from
// the last frame); null runs one frame on a buffer filled with set bits, so a missing clear
// shows.
inline TileBinMirrorResult RunTileBinMirror(
    const TileBinView& view,
    const std::vector<InstanceBound>& bounds,
    const std::vector<float>& previousDepth,
    int imageWidth,
    int imageHeight,
    std::vector<TileRankWord>* persistentWords = nullptr) {
    MirrorRequire(imageWidth > 0 && imageHeight > 0, "invalid depth image extent");
    MirrorRequire(view.instanceCount >= 0 &&
                      static_cast<size_t>(view.instanceCount) <= bounds.size(),
                  "instanceCount exceeds the bound stream");
    MirrorRequire(previousDepth.size() == static_cast<size_t>(imageWidth) * imageHeight,
                  "depth image size mismatch");
    MirrorRequire(view.tilesX == (imageWidth + 7) / 8 &&
                      view.tilesY == (imageHeight + 7) / 8,
                  "tile extent does not match the 8x8 dispatch contract");
    MirrorRequire(view.wordCapacity > 0, "non-positive rank word capacity");

    const int tileCount = view.tilesX * view.tilesY;
    MirrorRequire(tileCount > 0, "empty tile grid");
    const size_t wordSlab = static_cast<size_t>(tileCount) * view.wordCapacity;
    const int instanceCount = view.instanceCount;

    TileBinMirrorResult result;
    if (persistentWords && persistentWords->size() == wordSlab)
        result.words = *persistentWords;
    else
        result.words.assign(wordSlab, {0xFFFFFFFFu});
    result.order.assign(static_cast<size_t>(view.wordCapacity) * 32u, {UINT32_MAX, UINT32_MAX});

    // --- TileBinPrepareSystem: clear the used words, rank every instance.
    const int tileWords = MirrorTileWords(view);
    const size_t usedWords = static_cast<size_t>(tileCount) * tileWords;
    MirrorRequire(usedWords <= result.words.size(), "clear outside the rank-word slab");
    for (size_t word = 0; word < usedWords; ++word) result.words[word].bits = 0u;
    std::vector<float> rankDepth(static_cast<size_t>(instanceCount));
    for (int i = 0; i < instanceCount; ++i)
        rankDepth[static_cast<size_t>(i)] = MirrorRankDepth(bounds[static_cast<size_t>(i)], view);
    const bool rankable = instanceCount <= view.wordCapacity * 32;
    for (int i = 0; rankable && i < instanceCount; ++i) {
        uint32_t rank = 0;
        for (int other = 0; other < instanceCount; ++other) {
            ++result.metrics.rankCompareIterations;
            if (MirrorCandidateBefore(rankDepth[static_cast<size_t>(other)], static_cast<uint32_t>(other),
                                      rankDepth[static_cast<size_t>(i)], static_cast<uint32_t>(i)))
                ++rank;
        }
        if (!MirrorRequire(rank < result.order.size(), "rank outside the order buffer")) continue;
        result.order[rank].instance = static_cast<uint32_t>(i);
        result.order[rank].depth = rankDepth[static_cast<size_t>(i)];
        result.order[static_cast<size_t>(i)].rank = rank;
    }
    for (int rank = 0; rankable && rank < std::min<int>(instanceCount, static_cast<int>(result.order.size())); ++rank)
        MirrorRequire(result.order[static_cast<size_t>(rank)].instance < static_cast<uint32_t>(instanceCount),
                      "ranks are not a permutation");

    // --- TileBinScatterSystem: lane-major across all instances (the GPU order is arbitrary
    // and the result must not depend on it: bits are order-free).
    std::vector<std::vector<TileCandidateMirror>> expected(static_cast<size_t>(tileCount));
    const int lanes = MirrorScatterLanes(view);
    std::vector<TileRectMirror> rects(static_cast<size_t>(instanceCount));
    for (int i = 0; i < instanceCount; ++i) {
        ++result.metrics.scatterInstanceIterations;
        const InstanceBound& bound = bounds[static_cast<size_t>(i)];
        rects[static_cast<size_t>(i)] = MirrorProjectBound(bound.centerX, bound.centerY,
                                                           bound.centerZ, bound.radius, view);
    }
    for (int lane = 0; lane < lanes; ++lane) {
        for (int i = 0; i < instanceCount; ++i) {
            const TileRectMirror& rect = rects[static_cast<size_t>(i)];
            if (rect.cull != 0) continue;
            if (!rankable) {
                if (lane == 0) ++result.metrics.overflowed;
                continue;
            }
            const uint32_t rank = result.order[static_cast<size_t>(i)].rank;
            const int rankWord = static_cast<int>(rank / 32u);
            if (rankWord >= tileWords) {
                if (lane == 0) ++result.metrics.overflowed;
                continue;
            }
            const uint32_t rankBit = 1u << (rank % 32u);
            const int width = rect.maxX - rect.minX + 1;
            const int area = width * (rect.maxY - rect.minY + 1);
            uint64_t laneIterations = 0;
            for (int offset = lane; offset < area; offset += lanes) {
                ++laneIterations;
                ++result.metrics.scatterTileIterations;
                const int row = offset / width;
                const int tileY = rect.minY + row;
                const int tileX = rect.minX + (offset - row * width);
                const int tile = tileY * view.tilesX + tileX;
                MirrorRequire(tile >= 0 && tile < tileCount, "scatter tile index outside grid");

                const size_t depthX = static_cast<size_t>(tile % imageWidth);
                const size_t depthY = static_cast<size_t>(tile / imageWidth);
                MirrorRequire(depthY < static_cast<size_t>(imageHeight),
                              "depth sample index outside image");
                const float tileFarDepth = previousDepth[depthY * imageWidth + depthX];
                // TileBinOps.IsBehindTileDepth: a zero (cleared, never written)
                // history depth occludes nothing.
                if (tileFarDepth > 0.0f && rect.nearDepth > tileFarDepth) continue;

                expected[static_cast<size_t>(tile)].push_back({rect.nearDepth, static_cast<uint32_t>(i)});
                const size_t entry = static_cast<size_t>(rankWord) * tileCount + static_cast<size_t>(tile);
                MirrorRequire(entry < usedWords, "scatter word outside the used rank words");
                result.words[entry].bits |= rankBit;
            }
            result.metrics.scatterLongestLaneIterations =
                std::max(result.metrics.scatterLongestLaneIterations, laneIterations);
        }
    }
    // Every (instance, tile) pair of every binned rectangle is visited exactly once.
    uint64_t rectangleTiles = 0;
    for (int i = 0; i < instanceCount; ++i) {
        const TileRectMirror& rect = rects[static_cast<size_t>(i)];
        if (rect.cull != 0 || !rankable) continue;
        if (static_cast<int>(result.order[static_cast<size_t>(i)].rank / 32u) >= tileWords) continue;
        rectangleTiles += static_cast<uint64_t>(rect.maxX - rect.minX + 1) *
                          static_cast<uint64_t>(rect.maxY - rect.minY + 1);
    }
    MirrorRequire(rectangleTiles == result.metrics.scatterTileIterations,
                  "scatter lanes did not visit every rectangle tile exactly once");

    const bool complete = instanceCount <= view.wordCapacity * 32;
    for (int tile = 0; tile < tileCount; ++tile)
        result.metrics.maximumOffered = std::max(
            result.metrics.maximumOffered, static_cast<uint32_t>(expected[static_cast<size_t>(tile)].size()));

    // --- TraceWorld's primary consumer: walk the tile's words and bits as the march does and
    // require the front-to-back list the per-tile sort used to produce.
    std::vector<std::vector<uint32_t>>& visited = result.visited;
    visited.assign(static_cast<size_t>(tileCount), {});
    for (int tile = 0; tile < tileCount; ++tile) {
        if (!complete) continue;
        for (int word = 0; word < tileWords; ++word) {
            uint32_t bits = result.words[static_cast<size_t>(word) * tileCount + tile].bits;
            while (bits != 0u) {
                const int bit = std::countr_zero(bits);
                bits &= bits - 1u;
                const size_t rank = static_cast<size_t>(word) * 32u + static_cast<size_t>(bit);
                MirrorRequire(rank < static_cast<size_t>(instanceCount), "rank bit beyond the instances");
                visited[static_cast<size_t>(tile)].push_back(result.order[rank].instance);
            }
        }
        auto expectedTile = expected[static_cast<size_t>(tile)];
        std::sort(expectedTile.begin(), expectedTile.end(),
                  [](const TileCandidateMirror& a, const TileCandidateMirror& b) {
                      return MirrorCandidateBefore(a.nearDepth, a.instance, b.nearDepth, b.instance);
                  });
        const auto& actual = visited[static_cast<size_t>(tile)];
        if (!MirrorRequire(actual.size() == expectedTile.size(),
                           "tile rank set differs from the tile's candidate set"))
            continue;
        for (size_t slot = 0; slot < actual.size(); ++slot)
            MirrorRequire(actual[slot] == expectedTile[slot].instance,
                          "rank order differs from the front-to-back (near depth, instance) order");
    }
    for (int pixelY = 0; pixelY < imageHeight; ++pixelY) {
        for (int pixelX = 0; pixelX < imageWidth; ++pixelX) {
            ++result.metrics.consumerPixelIterations;
            const int tile = (pixelX / 8) + (pixelY / 8) * view.tilesX;
            if (!complete) {
                ++result.metrics.consumerFallbackPixels;
                continue;
            }
            result.metrics.consumerWordIterations += static_cast<uint64_t>(tileWords);
            result.metrics.consumerCandidateIterations += visited[static_cast<size_t>(tile)].size();
        }
    }
    if (persistentWords) *persistentWords = result.words;
    return result;
}

}  // namespace Vixen::RenderGraph::Mirror
