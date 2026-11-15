#include <gtest/gtest.h>

#include "TileBinningMirror.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using Vixen::Codegen::Gpu::InstanceBound;
using Vixen::Codegen::Gpu::TileBinView;
using Vixen::RenderGraph::Mirror::RunTileBinMirror;

constexpr int kWidth = 500;
constexpr int kHeight = 500;
constexpr int kTileSize = 8;
constexpr int kTilesX = (kWidth + kTileSize - 1) / kTileSize;
constexpr int kTilesY = (kHeight + kTileSize - 1) / kTileSize;
constexpr int kTierSide = 13;
constexpr int kBaseInstanceCount = 13;
constexpr int kInstanceCount = kTierSide * kTierSide * kBaseInstanceCount;
constexpr int kListCapacityLog2 = 9;

TileBinView LookdevTier3View() {
    // This is the load scenario's first overview pose: tier-3's 13x13 copies,
    // 45-degree camera, and the same 500x500 native render extent.
    constexpr float pi = 3.14159265358979323846f;
    constexpr float halfExtent = 78.0f;  // (13 - 1) * 12 / 2 + 6
    constexpr float yaw = 0.6f;
    constexpr float pitch = 0.62f;
    constexpr float radius = halfExtent * 2.3f;
    const glm::vec3 sceneCenter{5.0f, 5.0f, 5.0f};
    const glm::vec3 orbitOffset{std::cos(yaw) * std::cos(pitch), std::sin(pitch),
                                std::sin(yaw) * std::cos(pitch)};
    const glm::vec3 eye = sceneCenter + orbitOffset * radius;
    const glm::vec3 forward = glm::normalize(sceneCenter - eye);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));

    TileBinView view{};
    view.viewXx = right.x; view.viewXy = right.y; view.viewXz = right.z;
    view.viewXw = -glm::dot(right, eye);
    view.viewYx = up.x; view.viewYy = up.y; view.viewYz = up.z;
    view.viewYw = -glm::dot(up, eye);
    view.viewZx = forward.x; view.viewZy = forward.y; view.viewZz = forward.z;
    view.viewZw = -glm::dot(forward, eye);
    view.focalX = 0.5f * static_cast<float>(kHeight) /
                  std::tan(0.5f * (45.0f * pi / 180.0f));
    view.focalY = view.focalX;
    view.centerX = 0.5f * static_cast<float>(kWidth);
    view.centerY = 0.5f * static_cast<float>(kHeight);
    view.nearPlane = 0.001f;
    view.tileSize = 8.0f;
    view.tilesX = kTilesX;
    view.tilesY = kTilesY;
    view.listCapacityLog2 = kListCapacityLog2;
    view.instanceCount = kInstanceCount;
    return view;
}

std::vector<InstanceBound> LookdevTier3Bounds() {
    // BodyOctreeSceneNode's exact procedural bound upload is center=(0.5,0.5,0.5),
    // radius=0.5*sqrt(3).  The midday look-dev recipe set has 13 bodies; the
    // load scenario copies that set at 12-unit spacing in X/Z.
    std::vector<InstanceBound> bounds;
    bounds.reserve(kInstanceCount);
    const float radius = 0.5f * std::sqrt(3.0f);
    for (int copyX = 0; copyX < kTierSide; ++copyX) {
        for (int copyZ = 0; copyZ < kTierSide; ++copyZ) {
            const float offsetX = (static_cast<float>(copyX) - 6.0f) * 12.0f;
            const float offsetZ = (static_cast<float>(copyZ) - 6.0f) * 12.0f;
            for (int base = 0; base < kBaseInstanceCount; ++base) {
                // Midday keeps the lamp recipe in the 13-instance base set;
                // its authored emitter origin is the only non-zero transform.
                const bool lamp = base == kBaseInstanceCount - 1;
                const float originX = lamp ? 39.0f * (10.0f / 64.0f) : 0.0f;
                const float originY = lamp ? 36.0f * (10.0f / 64.0f) : 0.0f;
                const float originZ = lamp ? 25.0f * (10.0f / 64.0f) : 0.0f;
                bounds.push_back({originX + offsetX + 0.5f,
                                  originY + 0.5f,
                                  originZ + offsetZ + 0.5f,
                                  radius});
            }
        }
    }
    return bounds;
}

}  // namespace

TEST(TileBinningMirror, Tier3InputsTerminateAndPreserveCandidateLists) {
    const TileBinView view = LookdevTier3View();
    const auto bounds = LookdevTier3Bounds();
    const std::vector<float> previousDepth(static_cast<size_t>(kWidth) * kHeight,
                                            std::numeric_limits<float>::max());

    Vixen::RenderGraph::Mirror::TileBinMirrorResult result;
    ASSERT_NO_THROW(result = RunTileBinMirror(view, bounds, previousDepth, kWidth, kHeight));

    EXPECT_EQ(bounds.size(), static_cast<size_t>(kInstanceCount));
    EXPECT_EQ(view.instanceCount, kInstanceCount);
    EXPECT_EQ(view.listCapacityLog2, kListCapacityLog2);
    EXPECT_EQ(result.metrics.overflowed, 0u);
    EXPECT_GT(result.metrics.scatterTileIterations, 0u);
    EXPECT_GT(result.metrics.consumerCandidateIterations, 0u);

    // Before the fix, every tile paid for all 512 entries.  The active-count
    // network must be materially smaller while retaining the exact sorted lists.
    EXPECT_GT(result.metrics.legacyFullSlabCompareIterations,
              result.metrics.sortCompareIterations * 4u);
    EXPECT_LT(result.metrics.sortCompareIterations,
              result.metrics.legacyFullSlabCompareIterations);
    std::cout << "GB1 CPU mirror: instances=" << view.instanceCount
              << " tiles=" << (view.tilesX * view.tilesY)
              << " maxOffered=" << result.metrics.maximumOffered
              << " activeSortComparisons=" << result.metrics.sortCompareIterations
              << " legacyFullSlabComparisons="
              << result.metrics.legacyFullSlabCompareIterations
              << " consumerCandidates=" << result.metrics.consumerCandidateIterations
              << " overflowed=" << result.metrics.overflowed << '\n';
}

TEST(TileBinningMirror, SortZeroesCountersSoNextFrameStartsEmpty) {
    // The counter buffer persists across frames and is zero only at allocation.
    // The sort must hand the next scatter zeroed counts: a tile that no instance
    // touches in frame 2 must not replay frame 1's list, and a repeated frame
    // must not accumulate.
    TileBinView view = LookdevTier3View();
    const auto bounds = LookdevTier3Bounds();
    const std::vector<float> previousDepth(static_cast<size_t>(kWidth) * kHeight,
                                            std::numeric_limits<float>::max());
    std::vector<Vixen::Codegen::Gpu::TileBinCounter> counters(
        static_cast<size_t>(kTilesX) * kTilesY);

    const auto first = RunTileBinMirror(view, bounds, previousDepth, kWidth, kHeight, &counters);
    const auto repeated = RunTileBinMirror(view, bounds, previousDepth, kWidth, kHeight, &counters);
    ASSERT_EQ(first.headers.size(), repeated.headers.size());
    for (size_t tile = 0; tile < first.headers.size(); ++tile)
        EXPECT_EQ(first.headers[tile].count, repeated.headers[tile].count) << "tile " << tile;
    EXPECT_GT(first.metrics.consumerCandidateIterations, 0u);

    // A ViewDepth history nobody has written (first frame, or no reconstruction
    // depth producer) is cleared to zero and must not cull: the lists match the
    // far-depth history exactly.
    const std::vector<float> clearedDepth(static_cast<size_t>(kWidth) * kHeight, 0.0f);
    const auto cleared = RunTileBinMirror(view, bounds, clearedDepth, kWidth, kHeight, &counters);
    EXPECT_EQ(cleared.metrics.consumerCandidateIterations,
              first.metrics.consumerCandidateIterations);

    view.instanceCount = 0;
    const auto empty = RunTileBinMirror(view, bounds, previousDepth, kWidth, kHeight, &counters);
    EXPECT_EQ(empty.metrics.consumerCandidateIterations, 0u);
    for (const auto& counter : counters) EXPECT_EQ(counter.count, 0);
}
