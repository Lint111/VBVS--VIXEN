#pragma once
// SdfTraceProbe.h - CPU decoder for the shader's per-pixel SDF ray trace probe (T-2895, R464).
// 1:1 MIRROR of shaders/SdfTraceProbe.glsl: keep the word layout and termination codes
// identical. Header-only, no Vulkan dependency, so the decode is unit-testable on any machine.
//
// A harness defines VIXEN_SDF_TRACE_PROBE in the shader source, binds a storage buffer of
// SdfTraceProbeBufferBytes(w, h) at kSdfTraceProbeBinding, and decodes the mapped words with
// DecodeSdfTraceProbe.

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Vixen::SVO {

constexpr uint32_t kSdfTraceProbeBinding = 48;
constexpr uint32_t kSdfTraceProbeWords = 16;

enum class SdfTraceTermination : uint32_t {
    BoundMiss = 0,  // ray misses the recipe bound
    Hit = 1,        // surface hit
    Budget = 2,     // step budget exhausted
    Exit = 3,       // marched past tFar
};

struct SdfTraceRecord {
    uint32_t steps = 0;
    uint32_t termination = 0;  // SdfTraceTermination
    uint32_t recipeId = 0;
    uint32_t gridDim = 0;      // occupancy acceleration grid dimension, 0 = none
    float t = 0.0f;
    float sdf = 0.0f;
    float position[3]{};
    float gradient[3]{};
    float gridBound = 0.0f;
    float tNear = 0.0f;
    float tFar = 0.0f;
    float lastStep = 0.0f;

    SdfTraceTermination Termination() const { return static_cast<SdfTraceTermination>(termination); }
    bool Hit() const { return termination == static_cast<uint32_t>(SdfTraceTermination::Hit); }
};
static_assert(sizeof(SdfTraceRecord) == kSdfTraceProbeWords * sizeof(uint32_t),
              "SdfTraceRecord must match the 16-word shader record");

struct SdfStepDistribution {
    double mean = 0.0;
    uint32_t p50 = 0;
    uint32_t p99 = 0;
    uint32_t maximum = 0;
    size_t activePixels = 0;
};

constexpr uint64_t SdfTraceProbeBufferBytes(uint32_t width, uint32_t height) {
    return uint64_t(width) * height * kSdfTraceProbeWords * sizeof(uint32_t);
}

inline SdfTraceRecord DecodeSdfTraceRecord(const uint32_t* record) {
    const auto f = [](uint32_t bits) { return std::bit_cast<float>(bits); };
    SdfTraceRecord r;
    r.steps = record[0];
    r.termination = record[1];
    r.recipeId = record[2];
    r.gridDim = record[3];
    r.t = f(record[4]);
    r.sdf = f(record[5]);
    for (size_t axis = 0; axis < 3; ++axis) {
        r.position[axis] = f(record[6 + axis]);
        r.gradient[axis] = f(record[9 + axis]);
    }
    r.gridBound = f(record[12]);
    r.tNear = f(record[13]);
    r.tFar = f(record[14]);
    r.lastStep = f(record[15]);
    return r;
}

// words: the mapped probe buffer, width*height records, row-major (pixel y*width + x).
inline std::vector<SdfTraceRecord> DecodeSdfTraceProbe(const uint32_t* words, size_t pixelCount) {
    std::vector<SdfTraceRecord> out(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i)
        out[i] = DecodeSdfTraceRecord(words + i * kSdfTraceProbeWords);
    return out;
}

inline SdfStepDistribution SummarizeSdfSteps(const std::vector<SdfTraceRecord>& traces) {
    SdfStepDistribution result;
    if (traces.empty()) return result;
    std::vector<uint32_t> sorted;
    sorted.reserve(traces.size());
    uint64_t total = 0;
    for (const SdfTraceRecord& trace : traces) {
        sorted.push_back(trace.steps);
        total += trace.steps;
        result.maximum = std::max(result.maximum, trace.steps);
        if (trace.steps != 0u) ++result.activePixels;
    }
    std::sort(sorted.begin(), sorted.end());
    result.mean = static_cast<double>(total) / static_cast<double>(sorted.size());
    result.p50 = sorted[((sorted.size() - 1u) * 50u) / 100u];
    result.p99 = sorted[((sorted.size() - 1u) * 99u) / 100u];
    return result;
}

}  // namespace Vixen::SVO
