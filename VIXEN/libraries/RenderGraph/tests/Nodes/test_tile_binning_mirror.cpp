#include <gtest/gtest.h>

#include "TileBinningMirror.h"
#include "Nodes/TileBinViewProjection.h"

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

TEST(TileBinningMirror, ViewRecordProjectsPrimaryRayPixelsIntoTheirTiles) {
    // The scatter must bin an instance into the tiles whose primary rays can hit
    // it. Build the record the node uploads, march a point along the exact
    // CameraRay.glsl getRayDir() ray of several pixels, and require the projected
    // rectangle to contain that pixel's tile. fov is in degrees and pixel rows
    // grow downward (the shader flips ndc.y); a radians-as-degrees or upward-Y
    // record bins instances into the wrong tiles and the march misses them.
    constexpr uint32_t width = 500;
    constexpr uint32_t height = 400;
    Vixen::RenderGraph::CameraData camera{};
    camera.cameraPos = glm::vec3(3.0f, 4.0f, -20.0f);
    camera.fov = 60.0f;
    camera.aspect = static_cast<float>(width) / static_cast<float>(height);
    const glm::vec3 forward = glm::normalize(glm::vec3(1.0f, 2.0f, 6.0f) - camera.cameraPos);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    camera.cameraDir = forward;
    camera.cameraRight = right;
    camera.cameraUp = up;

    const TileBinView view = Vixen::RenderGraph::MakeTileBinView(camera, width, height, 1);
    const float tanHalfFov = std::tan(glm::radians(camera.fov * 0.5f));
    const glm::ivec2 pixels[] = {{0, 0}, {499, 0}, {0, 399}, {499, 399},
                                 {250, 200}, {17, 333}, {123, 45}, {401, 288}};
    for (const glm::ivec2 pixel : pixels) {
        const glm::vec2 uv = (glm::vec2(pixel) + 0.5f) / glm::vec2(width, height);
        glm::vec2 ndc = uv * 2.0f - 1.0f;
        ndc.y = -ndc.y;
        const glm::vec3 dir = glm::normalize(forward + right * ndc.x * tanHalfFov * camera.aspect +
                                             up * ndc.y * tanHalfFov);
        const glm::vec3 point = camera.cameraPos + dir * 37.0f;
        const auto rect = Vixen::RenderGraph::Mirror::MirrorProjectBound(
            point.x, point.y, point.z, 1.0e-4f, view);
        const int tileX = pixel.x / 8;
        const int tileY = pixel.y / 8;
        EXPECT_EQ(rect.cull, 0) << "pixel " << pixel.x << "," << pixel.y;
        EXPECT_TRUE(rect.minX <= tileX && tileX <= rect.maxX && rect.minY <= tileY && tileY <= rect.maxY)
            << "pixel " << pixel.x << "," << pixel.y << " tile " << tileX << "," << tileY
            << " rect x[" << rect.minX << "," << rect.maxX << "] y[" << rect.minY << "," << rect.maxY << "]";
        EXPECT_LE(rect.maxX - rect.minX, 1);
        EXPECT_LE(rect.maxY - rect.minY, 1);
    }
}

TEST(TileBinningMirror, ScreenSizedInstanceIsWalkedByManyLanes) {
    // RTL: a plaque that covers the whole 1080p frame is 240 x 135 = 32,400 tiles. One
    // invocation per instance walked them alone (8 to 17 ms measured on the 8060S); the
    // lanes bound the serial chain to ceil(32,400 / 256) tiles and the lists stay exact.
    constexpr int width = 1920;
    constexpr int height = 1080;
    Vixen::RenderGraph::CameraData camera{};
    camera.cameraPos = glm::vec3(0.0f, 0.0f, -10.0f);
    camera.fov = 45.0f;
    camera.aspect = static_cast<float>(width) / static_cast<float>(height);
    camera.cameraDir = glm::vec3(0.0f, 0.0f, 1.0f);
    camera.cameraRight = glm::vec3(1.0f, 0.0f, 0.0f);
    camera.cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    TileBinView view = Vixen::RenderGraph::MakeTileBinView(camera, width, height, 3);
    view.listCapacityLog2 = 4;
    const std::vector<InstanceBound> bounds = {
        {0.0f, 0.0f, 0.0f, 24.0f},   // covers the frame
        {2.0f, 1.0f, 3.0f, 1.0f},    // a small body in front of it
        {-3.0f, -1.0f, 1.0f, 0.5f},
    };
    const std::vector<float> previousDepth(static_cast<size_t>(width) * height, 0.0f);
    const auto result = RunTileBinMirror(view, bounds, previousDepth, width, height);
    EXPECT_EQ(Vixen::RenderGraph::Mirror::MirrorScatterLanes(view), 256);
    EXPECT_EQ(result.metrics.overflowed, 0u);
    EXPECT_GE(result.metrics.scatterTileIterations, 32400u);
    EXPECT_LE(result.metrics.scatterLongestLaneIterations, (32400u + 255u) / 256u);
    std::cout << "GB1 CPU mirror 1080p: scatterTiles=" << result.metrics.scatterTileIterations
              << " longestLane=" << result.metrics.scatterLongestLaneIterations << '\n';
}

TEST(TileBinningMirror, LanesShrinkWhenTheDispatchCannotHoldThem) {
    TileBinView view{};
    view.tilesX = 63;
    view.tilesY = 63;
    view.instanceCount = 2197;  // the load scenario's tier-3 field at 500x500
    EXPECT_EQ(Vixen::RenderGraph::Mirror::MirrorScatterLanes(view), (62 * 62 * 64) / 2197);
    view.instanceCount = 1;
    EXPECT_EQ(Vixen::RenderGraph::Mirror::MirrorScatterLanes(view), 256);
    view.instanceCount = 1000000;
    EXPECT_EQ(Vixen::RenderGraph::Mirror::MirrorScatterLanes(view), 1);
}

TEST(TileBinningMirror, OverflowingTilesFallBackInsteadOfSortingUnbounded) {
    // base-reds #119: the 2,197-instance field offered hundreds of candidates to dense tiles
    // and the per-tile serial sort lost the device. The list capacity bounds the sort; a tile
    // offered more keeps an incomplete list that the march must not read (R861.1).
    TileBinView view = LookdevTier3View();
    view.listCapacityLog2 = 2;  // four entries: the dense overview tiles overflow
    const auto bounds = LookdevTier3Bounds();
    const std::vector<float> previousDepth(static_cast<size_t>(kWidth) * kHeight, 0.0f);
    const auto result = RunTileBinMirror(view, bounds, previousDepth, kWidth, kHeight);
    EXPECT_GT(result.metrics.overflowed, 0u);
    EXPECT_GT(result.metrics.consumerFallbackPixels, 0u);
    for (const auto& header : result.headers) {
        EXPECT_LE(header.count, 4);
        if (header.demand <= 4) EXPECT_EQ(header.count, header.demand);
    }
}

TEST(TileBinningMirror, ProductionCapacityKeepsTheLookdevListsComplete) {
    // The capacity the node uploads (kTileListCapacityLog2) holds every look-dev tile list, so
    // the RTL and look-dev captures keep their exact lists and no tile falls back.
    TileBinView view = LookdevTier3View();
    view.instanceCount = 13;
    view.listCapacityLog2 = static_cast<int>(Vixen::RenderGraph::kTileListCapacityLog2);
    const auto bounds = LookdevTier3Bounds();
    const std::vector<float> previousDepth(static_cast<size_t>(kWidth) * kHeight, 0.0f);
    const auto result = RunTileBinMirror(view, bounds, previousDepth, kWidth, kHeight);
    EXPECT_EQ(result.metrics.overflowed, 0u);
    EXPECT_EQ(result.metrics.consumerFallbackPixels, 0u);
}
