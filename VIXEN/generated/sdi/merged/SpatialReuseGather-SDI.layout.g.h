// Generated host-side push-layout mirror; do not edit.
#pragma once

#include "SpatialReuseGather-SDI.g.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

namespace ShaderInterface::SpatialReuseGather {

struct PushBlock {
    uint32_t imgWidth;
    uint32_t imgHeight;
    uint8_t _tail[8];
};

static_assert(offsetof(PushBlock, imgWidth) == Push::imgWidth::OFFSET, "PushBlock.imgWidth is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, imgHeight) == Push::imgHeight::OFFSET, "PushBlock.imgHeight is not at its SDI OFFSET");
static_assert(sizeof(PushBlock) == Push::SIZE, "PushBlock size differs from Push::SIZE");

} // namespace ShaderInterface::SpatialReuseGather
