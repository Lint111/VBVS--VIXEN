// Generated host-side push-layout mirror; do not edit.
#pragma once

#include "ShadowVisibilityWave-SDI.g.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

namespace ShaderInterface::ShadowVisibilityWave {

struct PushBlock {
    glm::vec3 cameraPos;
    float time;
    glm::vec3 cameraDir;
    float fov;
    glm::vec3 cameraUp;
    float aspect;
    glm::vec3 cameraRight;
    int32_t debugMode;
    float raySizeCoef;
    float raySizeBias;
    int32_t instanceCount;
    uint8_t _pad76[4];
    glm::ivec2 debugTargetPixel;
    uint32_t accumFrameCount;
    float cosmicK;
};

static_assert(offsetof(PushBlock, cameraPos) == Push::cameraPos::OFFSET, "PushBlock.cameraPos is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, time) == Push::time::OFFSET, "PushBlock.time is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, cameraDir) == Push::cameraDir::OFFSET, "PushBlock.cameraDir is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, fov) == Push::fov::OFFSET, "PushBlock.fov is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, cameraUp) == Push::cameraUp::OFFSET, "PushBlock.cameraUp is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, aspect) == Push::aspect::OFFSET, "PushBlock.aspect is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, cameraRight) == Push::cameraRight::OFFSET, "PushBlock.cameraRight is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, debugMode) == Push::debugMode::OFFSET, "PushBlock.debugMode is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, raySizeCoef) == Push::raySizeCoef::OFFSET, "PushBlock.raySizeCoef is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, raySizeBias) == Push::raySizeBias::OFFSET, "PushBlock.raySizeBias is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, instanceCount) == Push::instanceCount::OFFSET, "PushBlock.instanceCount is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, debugTargetPixel) == Push::debugTargetPixel::OFFSET, "PushBlock.debugTargetPixel is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, accumFrameCount) == Push::accumFrameCount::OFFSET, "PushBlock.accumFrameCount is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, cosmicK) == Push::cosmicK::OFFSET, "PushBlock.cosmicK is not at its SDI OFFSET");
static_assert(sizeof(PushBlock) == Push::SIZE, "PushBlock size differs from Push::SIZE");

} // namespace ShaderInterface::ShadowVisibilityWave
