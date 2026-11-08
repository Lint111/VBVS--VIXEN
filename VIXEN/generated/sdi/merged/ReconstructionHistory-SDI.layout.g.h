// Generated host-side push-layout mirror; do not edit.
#pragma once

#include "ReconstructionHistory-SDI.g.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

namespace ShaderInterface::ReconstructionHistory {

struct PushBlock {
    uint32_t writeIndex;
    uint8_t _tail[12];
};

static_assert(offsetof(PushBlock, writeIndex) == Push::writeIndex::OFFSET, "PushBlock.writeIndex is not at its SDI OFFSET");
static_assert(sizeof(PushBlock) == Push::SIZE, "PushBlock size differs from Push::SIZE");

} // namespace ShaderInterface::ReconstructionHistory
