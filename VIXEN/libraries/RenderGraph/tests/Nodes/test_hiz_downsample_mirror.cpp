// test_hiz_downsample_mirror.cpp — Raster-proxy B1 M2 (CPU mirror, no device).
//
// 1:1 CPU mirror tests for shaders/HiZDownsample.comp per the gpu-shader-debug
// discipline: the mirror (HiZDownsampleMirror.h) carries the exact reduce the
// shader performs — one output texel = MAX over its 16x16 source block, blocks
// clipped at the source edge, miss pixels carry the 1e30 sentinel so sky never
// occludes. Any fix here MUST be applied to the .comp and vice versa.

#include <gtest/gtest.h>
#include "Nodes/HiZDownsampleMirror.h"

#include <cstdint>
#include <vector>

using namespace Vixen::RenderGraph::Mirror;

namespace {

std::vector<float> Uniform(uint32_t w, uint32_t h, float v) {
    return std::vector<float>(static_cast<size_t>(w) * h, v);
}

}  // namespace

TEST(HiZDownsampleMirror, OutputDimsAreCeilDiv16) {
    EXPECT_EQ(HiZTileCount(1920u), 120u);
    EXPECT_EQ(HiZTileCount(1080u), 68u);   // ceil(1080/16) = 67.5 -> 68
    EXPECT_EQ(HiZTileCount(16u), 1u);
    EXPECT_EQ(HiZTileCount(17u), 2u);
    EXPECT_EQ(HiZTileCount(1u), 1u);
}

TEST(HiZDownsampleMirror, UniformSourceReducesToUniformTiles) {
    const uint32_t w = 64, h = 48;               // 4x3 exact tiles
    auto src = Uniform(w, h, 7.5f);
    std::vector<float> dst(HiZTileCount(w) * HiZTileCount(h), -1.0f);
    HiZDownsampleAll(src.data(), w, h, dst.data());
    for (float v : dst) EXPECT_FLOAT_EQ(v, 7.5f);
}

TEST(HiZDownsampleMirror, SpikeLandsOnlyInItsTile) {
    const uint32_t w = 64, h = 64;               // 4x4 tiles
    auto src = Uniform(w, h, 2.0f);
    // Spike at pixel (35, 19) -> tile (2, 1).
    src[19u * w + 35u] = 99.0f;
    std::vector<float> dst(HiZTileCount(w) * HiZTileCount(h), -1.0f);
    HiZDownsampleAll(src.data(), w, h, dst.data());
    const uint32_t tilesX = HiZTileCount(w);
    for (uint32_t ty = 0; ty < HiZTileCount(h); ++ty) {
        for (uint32_t tx = 0; tx < tilesX; ++tx) {
            const float expect = (tx == 2u && ty == 1u) ? 99.0f : 2.0f;
            EXPECT_FLOAT_EQ(dst[ty * tilesX + tx], expect)
                << "tile (" << tx << "," << ty << ")";
        }
    }
}

TEST(HiZDownsampleMirror, EdgePartialTilesClipToSource) {
    // 20x20 source -> 2x2 tiles; right/bottom tiles cover only 4 source
    // pixels per axis. Fill the clipped remainder region with a HUGE value in
    // a LARGER backing store to prove the mirror never reads past (w,h): the
    // mirror receives w=h=20 and the poison lives outside that extent's rows,
    // so tile maxima must ignore it.
    const uint32_t w = 20, h = 20;
    auto src = Uniform(w, h, 3.0f);
    src[17u * w + 18u] = 5.0f;                    // inside bottom-right partial tile
    std::vector<float> dst(HiZTileCount(w) * HiZTileCount(h), -1.0f);
    HiZDownsampleAll(src.data(), w, h, dst.data());
    ASSERT_EQ(dst.size(), 4u);
    EXPECT_FLOAT_EQ(dst[0], 3.0f);                // (0,0) full-interior part
    EXPECT_FLOAT_EQ(dst[1], 3.0f);                // (1,0) partial right
    EXPECT_FLOAT_EQ(dst[2], 3.0f);                // (0,1) partial bottom
    EXPECT_FLOAT_EQ(dst[3], 5.0f);                // (1,1) partial corner holds spike
}

TEST(HiZDownsampleMirror, AllMissStaysSentinel) {
    const uint32_t w = 32, h = 32;
    auto src = Uniform(w, h, kDepthMissSentinel);
    std::vector<float> dst(HiZTileCount(w) * HiZTileCount(h), -1.0f);
    HiZDownsampleAll(src.data(), w, h, dst.data());
    for (float v : dst) EXPECT_FLOAT_EQ(v, kDepthMissSentinel);
}

TEST(HiZDownsampleMirror, MixedHitAndMissTileTakesSentinelMax) {
    // A tile containing ANY miss pixel must report the sentinel (max), so
    // instances behind sky are never culled — the conservative direction.
    const uint32_t w = 16, h = 16;                // single tile
    auto src = Uniform(w, h, 4.0f);
    src[5] = kDepthMissSentinel;
    std::vector<float> dst(1, -1.0f);
    HiZDownsampleAll(src.data(), w, h, dst.data());
    EXPECT_FLOAT_EQ(dst[0], kDepthMissSentinel);
}
