// Generated host-side push-layout mirror; do not edit.
#pragma once

#include "HiZDownsample-SDI.g.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

namespace ShaderInterface::HiZDownsample {

struct PushBlock {
    uint32_t srcWidth;
    uint32_t srcHeight;
    uint8_t _tail[8];
};

static_assert(offsetof(PushBlock, srcWidth) == Push::srcWidth::OFFSET, "PushBlock.srcWidth is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, srcHeight) == Push::srcHeight::OFFSET, "PushBlock.srcHeight is not at its SDI OFFSET");
static_assert(sizeof(PushBlock) == Push::SIZE, "PushBlock size differs from Push::SIZE");

} // namespace ShaderInterface::HiZDownsample
