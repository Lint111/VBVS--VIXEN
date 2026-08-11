#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

// HDR2 CPU mirror of ExposureMeter.comp's fixed 16x16 reduction. The fixture is
// represented in the same log2-luminance domain consumed by the shader; no GPU
// readback participates in the metering path.
static float MirrorEv100(const std::array<float, 256>& logLuma) {
    float tree[256];
    for (uint32_t i = 0; i < 256; ++i) tree[i] = logLuma[i];
    for (uint32_t stride = 128; stride != 0; stride >>= 1)
        for (uint32_t i = 0; i < stride; ++i) tree[i] += tree[i + stride];
    return -tree[0] / 256.0f;
}

int main() {
    std::array<float, 256> first{};
    std::array<float, 256> steady{};
    steady.fill(-5.45907f);
    assert(std::fabs(MirrorEv100(first) - 0.0f) < 1e-6f);
    assert(std::fabs(MirrorEv100(steady) - 5.45907f) < 1e-4f);
    return 0;
}
