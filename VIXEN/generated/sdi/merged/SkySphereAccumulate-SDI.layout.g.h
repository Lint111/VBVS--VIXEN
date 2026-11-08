// Generated host-side push-layout mirror; do not edit.
#pragma once

#include "SkySphereAccumulate-SDI.g.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

namespace ShaderInterface::SkySphereAccumulate {

struct PushBlock {
    uint32_t outputWidth;
    uint32_t outputHeight;
    uint32_t cellCount;
    float transmittanceEpsilon;
};

static_assert(offsetof(PushBlock, outputWidth) == Push::outputWidth::OFFSET, "PushBlock.outputWidth is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, outputHeight) == Push::outputHeight::OFFSET, "PushBlock.outputHeight is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, cellCount) == Push::cellCount::OFFSET, "PushBlock.cellCount is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, transmittanceEpsilon) == Push::transmittanceEpsilon::OFFSET, "PushBlock.transmittanceEpsilon is not at its SDI OFFSET");
static_assert(sizeof(PushBlock) == Push::SIZE, "PushBlock size differs from Push::SIZE");

} // namespace ShaderInterface::SkySphereAccumulate
