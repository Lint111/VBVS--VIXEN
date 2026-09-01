#pragma once
// ProxyIntervalPrepassMirror.h — Raster-proxy B2 CPU mirror of the compact
// per-pixel union interval + ordered candidate mask contract.
//
// @shader shaders/ProxyIntervalPrepass.frag
// @shader shaders/BodyInstanceRayMarch.comp
// Any fix here MUST be applied to both shader producer/consumer paths.

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace Vixen::RenderGraph::Mirror {

inline constexpr uint32_t kProxyCandidateWordCount = 6u;
inline constexpr uint32_t kProxyCandidateLimit = kProxyCandidateWordCount * 32u;
inline constexpr float kProxyParallelEpsilon = 1.0e-8f;

// std430 layout: two uint interval encodings followed by uint[6]. The record is
// deliberately clear-to-zero so one vkCmdFillBuffer initializes the full image.
struct ProxyIntervalPixel {
    uint32_t entryKey;
    uint32_t exitBits;
    uint32_t candidateMask[kProxyCandidateWordCount];
};
static_assert(sizeof(ProxyIntervalPixel) == 32,
              "B2 proxy interval pixel must stay 32 bytes");

struct ProxyRay {
    glm::vec3 origin;
    glm::vec3 direction;
};

inline uint32_t ProxyFloatBits(float value) {
    uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline float ProxyBitsFloat(uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// Non-negative finite IEEE-754 floats sort in the same order as uint32 bits.
// Reversing that order lets a zero-initialized atomicMax select the minimum.
inline uint32_t EncodeProxyEnter(float tEnter) {
    return std::numeric_limits<uint32_t>::max() - ProxyFloatBits(tEnter);
}

inline ProxyIntervalPixel ClearProxyIntervalPixel() {
    return ProxyIntervalPixel{};
}

inline bool HasProxyCandidates(const ProxyIntervalPixel& pixel) {
    for (uint32_t word : pixel.candidateMask) {
        if (word != 0u) return true;
    }
    return false;
}

inline bool IsProxyCandidate(const ProxyIntervalPixel& pixel, uint32_t instanceIndex) {
    if (instanceIndex >= kProxyCandidateLimit) return false;
    const uint32_t word = instanceIndex >> 5u;
    const uint32_t bit = instanceIndex & 31u;
    return (pixel.candidateMask[word] & (1u << bit)) != 0u;
}

inline float DecodeProxyEnter(const ProxyIntervalPixel& pixel) {
    const uint32_t bits = std::numeric_limits<uint32_t>::max() - pixel.entryKey;
    return ProxyBitsFloat(bits);
}

inline float DecodeProxyExit(const ProxyIntervalPixel& pixel) {
    return ProxyBitsFloat(pixel.exitBits);
}

inline bool AccumulateProxyInterval(ProxyIntervalPixel& pixel, uint32_t instanceIndex,
                                    float tEnter, float tExit) {
    if (instanceIndex >= kProxyCandidateLimit ||
        !std::isfinite(tEnter) || !std::isfinite(tExit) ||
        tExit < 0.0f || tEnter > tExit) {
        return false;
    }

    tEnter = std::max(tEnter, 0.0f);
    pixel.entryKey = std::max(pixel.entryKey, EncodeProxyEnter(tEnter));
    pixel.exitBits = std::max(pixel.exitBits, ProxyFloatBits(tExit));
    pixel.candidateMask[instanceIndex >> 5u] |=
        (1u << (instanceIndex & 31u));
    return true;
}

inline bool IntersectProxyAabb(const ProxyRay& ray,
                               const glm::vec3& aabbMin,
                               const glm::vec3& aabbMax,
                               float& outEnter,
                               float& outExit) {
    float tEnter = -std::numeric_limits<float>::infinity();
    float tExit = std::numeric_limits<float>::infinity();
    bool hasDirectionalAxis = false;

    for (uint32_t axis = 0u; axis < 3u; ++axis) {
        const float origin = ray.origin[axis];
        const float direction = ray.direction[axis];
        if (std::abs(direction) < kProxyParallelEpsilon) {
            if (origin < aabbMin[axis] || origin > aabbMax[axis]) return false;
            continue;
        }

        hasDirectionalAxis = true;
        float t0 = (aabbMin[axis] - origin) / direction;
        float t1 = (aabbMax[axis] - origin) / direction;
        if (t0 > t1) std::swap(t0, t1);
        tEnter = std::max(tEnter, t0);
        tExit = std::min(tExit, t1);
        if (tEnter > tExit) return false;
    }

    if (!hasDirectionalAxis || !std::isfinite(tEnter) || !std::isfinite(tExit) ||
        tExit < 0.0f) {
        return false;
    }

    outEnter = std::max(tEnter, 0.0f);
    outExit = tExit;
    return true;
}

inline bool AccumulateProxyAabb(ProxyIntervalPixel& pixel, uint32_t instanceIndex,
                                const ProxyRay& ray,
                                const glm::vec3& aabbMin,
                                const glm::vec3& aabbMax) {
    float tEnter = 0.0f;
    float tExit = 0.0f;
    if (!IntersectProxyAabb(ray, aabbMin, aabbMax, tEnter, tExit)) return false;
    return AccumulateProxyInterval(pixel, instanceIndex, tEnter, tExit);
}

}  // namespace Vixen::RenderGraph::Mirror
