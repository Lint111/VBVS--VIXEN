// Generated host-side push-layout mirror; do not edit.
#pragma once

#include "ReconstructionDepth-SDI.g.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

namespace ShaderInterface::ReconstructionDepth {

struct PushBlock {
    uint8_t invProjection[64];
    glm::vec3 cameraPos;
    uint8_t _pad76[4];
    glm::vec3 cameraDir;
    uint8_t _pad92[4];
    glm::vec4 depthEncoding;
};

static_assert(offsetof(PushBlock, invProjection) == Push::invProjection::OFFSET, "PushBlock.invProjection is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, cameraPos) == Push::cameraPos::OFFSET, "PushBlock.cameraPos is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, cameraDir) == Push::cameraDir::OFFSET, "PushBlock.cameraDir is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, depthEncoding) == Push::depthEncoding::OFFSET, "PushBlock.depthEncoding is not at its SDI OFFSET");
static_assert(sizeof(PushBlock) == Push::SIZE, "PushBlock size differs from Push::SIZE");

} // namespace ShaderInterface::ReconstructionDepth
