#include <array>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

// HDR2 CPU mirror of ExposureMeter.comp's 16x16 tile clamp and frame reduction.
// Fixtures use the shader's log2-luminance domain; no GPU readback participates.
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

static float MirrorFrameEv100(const std::vector<float>& logLuma, uint32_t width, uint32_t height) {
    float logSum = 0.0f;
    uint32_t pixelCount = 0;
    for (uint32_t tileY = 0; tileY < height; tileY += 16) {
        for (uint32_t tileX = 0; tileX < width; tileX += 16) {
            std::array<float, 256> tile{};
            uint32_t validPixels = 0;
            for (uint32_t y = tileY; y < std::min(tileY + 16, height); ++y) {
                for (uint32_t x = tileX; x < std::min(tileX + 16, width); ++x) {
                    tile[validPixels++] = logLuma[y * width + x];
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

    // Two equal sized tiles have log luminance 0 and -4, so the frame mean is -2
    // and exposure must be +2 EV. The current one-group dispatch sees only tile 0.
    std::vector<float> twoTiles(32 * 16, 0.0f);
    for (uint32_t y = 0; y < 16; ++y)
        for (uint32_t x = 16; x < 32; ++x)
            twoTiles[y * 32 + x] = -4.0f;
    const float frameEv = MirrorFrameEv100(twoTiles, 32, 16);
    assert(std::fabs(frameEv - 2.0f) < 1e-4f);
    assert(std::fabs(std::exp2(frameEv) - 4.0f) < 1e-4f);

    // Partial 16x16 edge tiles contribute by their valid-pixel count, not by
    // counting padded out-of-bounds lanes as black pixels.
    std::vector<float> oddFrame(17 * 17, 0.0f);
    for (uint32_t y = 0; y < 17; ++y)
        for (uint32_t x = 0; x < 17; ++x)
            if (x == 16 || y == 16) oddFrame[y * 17 + x] = -4.0f;
    assert(std::fabs(MirrorFrameEv100(oddFrame, 17, 17) - (132.0f / 289.0f)) < 1e-4f);
    return 0;
}
