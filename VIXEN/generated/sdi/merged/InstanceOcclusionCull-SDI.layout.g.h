// Generated host-side push-layout mirror; do not edit.
#pragma once

#include "InstanceOcclusionCull-SDI.g.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

namespace ShaderInterface::InstanceOcclusionCull {

struct PushBlock {
    uint8_t prevViewProj[64];
    glm::vec4 prevCamPos;
    glm::uvec4 dims;
};

static_assert(offsetof(PushBlock, prevViewProj) == Push::prevViewProj::OFFSET, "PushBlock.prevViewProj is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, prevCamPos) == Push::prevCamPos::OFFSET, "PushBlock.prevCamPos is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, dims) == Push::dims::OFFSET, "PushBlock.dims is not at its SDI OFFSET");
static_assert(sizeof(PushBlock) == Push::SIZE, "PushBlock size differs from Push::SIZE");

} // namespace ShaderInterface::InstanceOcclusionCull
