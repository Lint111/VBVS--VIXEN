// ============================================================================
// Compact merged SDI declarations (graph-facing ABI)
// ============================================================================
//
// Program: InstanceOcclusionCull
// Generated from the merged shader reflection; do not edit.
// Layout mirrors live in InstanceOcclusionCull-SDI.layout.g.h.
//
// ============================================================================

#pragma once

#include <cstdint>

namespace ShaderInterface {
namespace InstanceOcclusionCull {

enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

namespace Set0 {

    struct Binding0 {
        static constexpr const char* NAME = "BodyInstanceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding1 {
        static constexpr const char* NAME = "OctreeConfigsSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding2 {
        static constexpr const char* NAME = "tileMaxImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding3 {
        static constexpr const char* NAME = "InstanceSkipMaskBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding4 {
        static constexpr const char* NAME = "BodyInstanceTransformBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 4;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

namespace Bind {
using BodyInstanceBuffer = Set0::Binding0;
using OctreeConfigsSSBO = Set0::Binding1;
using tileMaxImage = Set0::Binding2;
using InstanceSkipMaskBuffer = Set0::Binding3;
using BodyInstanceTransformBuffer = Set0::Binding4;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 96;

    struct prevViewProj {
        static constexpr const char* NAME = "prevViewProj";
        static constexpr uint32_t INDEX = 0;
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 64;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct prevCamPos {
        static constexpr const char* NAME = "prevCamPos";
        static constexpr uint32_t INDEX = 1;
        static constexpr uint32_t OFFSET = 64;
        static constexpr uint32_t SIZE = 16;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct dims {
        static constexpr const char* NAME = "dims";
        static constexpr uint32_t INDEX = 2;
        static constexpr uint32_t OFFSET = 80;
        static constexpr uint32_t SIZE = 16;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Push

struct MemberInfo {
    const char* name;
    bool isPushMember;
    uint32_t set;
    uint32_t binding;
    uint32_t offset;
    Access access;
    uint32_t featureCount;
    const char* const* features;
};


inline constexpr MemberInfo MEMBERS[] = {
    {"BodyInstanceBuffer", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"OctreeConfigsSSBO", false, 0, 1, 0, Access::ReadOnly, 0, nullptr},
    {"tileMaxImage", false, 0, 2, 0, Access::ReadOnly, 0, nullptr},
    {"InstanceSkipMaskBuffer", false, 0, 3, 0, Access::ReadWrite, 0, nullptr},
    {"BodyInstanceTransformBuffer", false, 0, 4, 0, Access::ReadOnly, 0, nullptr},
    {"prevViewProj", true, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"prevCamPos", true, 0, 0, 64, Access::ReadOnly, 0, nullptr},
    {"dims", true, 0, 0, 80, Access::ReadOnly, 0, nullptr},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "InstanceOcclusionCull";
    static constexpr uint32_t NUM_MEMBERS = 8;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace InstanceOcclusionCull
} // namespace ShaderInterface
