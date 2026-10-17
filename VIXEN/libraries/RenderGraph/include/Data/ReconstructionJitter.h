#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace Vixen::RenderGraph {

enum class ReconstructionSamplingPath {
    InteractiveVisual,
    CanonicalCapture,
    Parity,
    AuthoritativePicking,
};

// Input-resolution pixel units, positive right/down. This finite Halton(2,3)
// sequence restarts with viewport history; the unjittered camera is unchanged.
inline glm::vec2 ReconstructionJitter(uint32_t frame, ReconstructionSamplingPath path) {
    if (path != ReconstructionSamplingPath::InteractiveVisual) return glm::vec2(0.0f);
    const auto radicalInverse = [](uint32_t value, uint32_t base) {
        float result = 0.0f;
        float fraction = 1.0f;
        while (value) {
            fraction /= static_cast<float>(base);
            result += fraction * static_cast<float>(value % base);
            value /= base;
        }
        return result;
    };
    const uint32_t sample = frame % 16u + 1u;
    return {radicalInverse(sample, 2u) - 0.5f, radicalInverse(sample, 3u) - 0.5f};
}

} // namespace Vixen::RenderGraph
