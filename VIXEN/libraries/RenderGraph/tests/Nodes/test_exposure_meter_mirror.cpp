#include <array>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

// HDR2 CPU mirror of ExposureMeter.comp's 16x16 tile clamp and frame reduction.
// Fixtures contain single-channel radiance; no GPU readback participates.
static float MirrorClampedLogSum(std::array<float, 256> tile, uint32_t validPixels) {
    if (validPixels == 0) return 0.0f;
    std::sort(tile.begin(), tile.begin() + validPixels);
    const uint32_t cutoffIndex = (validPixels * 95u + 99u) / 100u - 1u;
    const float cutoff = tile[cutoffIndex];
    float sum = 0.0f;
    for (uint32_t i = 0; i < validPixels; ++i) sum += std::min(tile[i], cutoff);
    return sum;
}

static float MirrorEv100(const std::array<float, 256>& logLuma) {
    return -MirrorClampedLogSum(logLuma, 256) / 256.0f;
}

static float MirrorLogLuminance(float luminance) {
    if (!std::isfinite(luminance) || luminance <= 1.0e-6f) return 0.0f;
    return std::log2(luminance);
}

static float MirrorFrameEv100(const std::vector<float>& radiance, uint32_t width, uint32_t height) {
    float logSum = 0.0f;
    uint32_t pixelCount = 0;
    for (uint32_t tileY = 0; tileY < height; tileY += 16) {
        for (uint32_t tileX = 0; tileX < width; tileX += 16) {
            std::array<float, 256> tile{};
            uint32_t validPixels = 0;
            for (uint32_t y = tileY; y < std::min(tileY + 16, height); ++y) {
                for (uint32_t x = tileX; x < std::min(tileX + 16, width); ++x) {
                    tile[validPixels++] = MirrorLogLuminance(radiance[y * width + x]);
                }
            }
            logSum += MirrorClampedLogSum(tile, validPixels);
            pixelCount += validPixels;
        }
    }
    return pixelCount == 0 ? 0.0f : -logSum / static_cast<float>(pixelCount);
}

int main() {
    std::array<float, 256> first{};
    std::array<float, 256> steady{};
    steady.fill(-5.45907f);
    assert(std::fabs(MirrorEv100(first) - 0.0f) < 1e-6f);
    assert(std::fabs(MirrorEv100(steady) - 5.45907f) < 1e-4f);
    std::array<float, 256> solar = steady;
    solar[0] = 20.0f;
    const float ev = MirrorEv100(solar);
    assert(std::fabs(ev - 5.45907f) < 1e-4f);
    assert(MirrorEv100(solar) == ev);

    // The first 16x16 tile is black while the other three are lit at 0.25.
    // The shader's safe-black path contributes log luminance 0 for the first
    // tile; the full-frame result is +1.5 EV, while a top-left-only meter reads 0.
    std::vector<float> blackCorner(32 * 32, 0.25f);
    for (uint32_t y = 0; y < 16; ++y)
        for (uint32_t x = 0; x < 16; ++x)
            blackCorner[y * 32 + x] = 0.0f;
    std::array<float, 256> topLeftOnly{};
    for (float& sample : topLeftOnly) sample = MirrorLogLuminance(0.0f);
    const float topLeftOnlyEv = MirrorEv100(topLeftOnly);
    const float frameEv = MirrorFrameEv100(blackCorner, 32, 32);
    assert(std::fabs(topLeftOnlyEv - 0.0f) < 1e-6f);
    assert(std::fabs(frameEv - 1.5f) < 1e-4f);
    assert(frameEv > topLeftOnlyEv + 1.0f);
    assert(MirrorFrameEv100(blackCorner, 32, 32) == frameEv);

    // Partial 16x16 edge tiles contribute by their valid-pixel count, not by
    // counting padded out-of-bounds lanes as black pixels.
    std::vector<float> oddFrame(17 * 17, 1.0f);
    for (uint32_t y = 0; y < 17; ++y)
        for (uint32_t x = 0; x < 17; ++x)
            if (x == 16 || y == 16) oddFrame[y * 17 + x] = 0.25f;
    assert(std::fabs(MirrorFrameEv100(oddFrame, 17, 17) - (66.0f / 289.0f)) < 1e-4f);
    return 0;
}
