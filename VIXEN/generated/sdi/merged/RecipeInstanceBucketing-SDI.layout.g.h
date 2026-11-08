// Generated host-side push-layout mirror; do not edit.
#pragma once

#include "RecipeInstanceBucketing-SDI.g.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

namespace ShaderInterface::RecipeInstanceBucketing {

struct PushBlock {
    uint8_t viewProj[64];
    int32_t instanceCount;
    uint32_t maxBuckets;
    uint32_t maxMembersPerBucket;
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t mode;
    float raySizeCoef;
    float raySizeBias;
    glm::vec3 cameraPos;
    uint8_t _tail[4];
};

static_assert(offsetof(PushBlock, viewProj) == Push::viewProj::OFFSET, "PushBlock.viewProj is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, instanceCount) == Push::instanceCount::OFFSET, "PushBlock.instanceCount is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, maxBuckets) == Push::maxBuckets::OFFSET, "PushBlock.maxBuckets is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, maxMembersPerBucket) == Push::maxMembersPerBucket::OFFSET, "PushBlock.maxMembersPerBucket is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, screenWidth) == Push::screenWidth::OFFSET, "PushBlock.screenWidth is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, screenHeight) == Push::screenHeight::OFFSET, "PushBlock.screenHeight is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, mode) == Push::mode::OFFSET, "PushBlock.mode is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, raySizeCoef) == Push::raySizeCoef::OFFSET, "PushBlock.raySizeCoef is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, raySizeBias) == Push::raySizeBias::OFFSET, "PushBlock.raySizeBias is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, cameraPos) == Push::cameraPos::OFFSET, "PushBlock.cameraPos is not at its SDI OFFSET");
static_assert(sizeof(PushBlock) == Push::SIZE, "PushBlock size differs from Push::SIZE");

} // namespace ShaderInterface::RecipeInstanceBucketing
