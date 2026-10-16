#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>

namespace Vixen::RenderGraph {

// R608.1: consumers select a convention row; the conversion contains no SDK names.
enum class ReconstructionDepthConvention : uint32_t {
    ForwardZeroToOne,
    ReversedZeroToOne,
    ForwardMinusOneToOne,
    ReversedMinusOneToOne,
};

inline glm::vec4 ReconstructionDepthEncoding(ReconstructionDepthConvention convention) {
    static constexpr std::array<glm::vec4, 4> encodings{{
        { 1.0f,  0.0f,  1.0f, 1.0f},
        {-1.0f,  1.0f,  0.0f, 1.0f},
        { 2.0f, -1.0f,  1.0f, 1.0f},
        {-2.0f,  1.0f, -1.0f, 1.0f},
    }};
    return encodings.at(static_cast<size_t>(convention));
}

} // namespace Vixen::RenderGraph
