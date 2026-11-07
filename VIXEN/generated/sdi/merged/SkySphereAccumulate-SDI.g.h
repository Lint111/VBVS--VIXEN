// ============================================================================
// Compact merged SDI declarations (graph-facing ABI)
// ============================================================================
//
// Program: SkySphereAccumulate
// Generated from the merged shader reflection; do not edit.
// Layout mirrors live in SkySphereAccumulate-SDI.layout.g.h.
//
// ============================================================================

#pragma once

#include <cstdint>

namespace ShaderInterface {
namespace SkySphereAccumulate {

enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

namespace Set0 {

    struct Binding0 {
        static constexpr const char* NAME = "skySphereImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding1 {
        static constexpr const char* NAME = "MipPoolStandIn";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

namespace Bind {
using skySphereImage = Set0::Binding0;
using MipPoolStandIn = Set0::Binding1;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 16;

    struct outputWidth {
        static constexpr const char* NAME = "outputWidth";
        static constexpr uint32_t INDEX = 0;
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct outputHeight {
        static constexpr const char* NAME = "outputHeight";
        static constexpr uint32_t INDEX = 1;
        static constexpr uint32_t OFFSET = 4;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct cellCount {
        static constexpr const char* NAME = "cellCount";
        static constexpr uint32_t INDEX = 2;
        static constexpr uint32_t OFFSET = 8;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct transmittanceEpsilon {
        static constexpr const char* NAME = "transmittanceEpsilon";
        static constexpr uint32_t INDEX = 3;
        static constexpr uint32_t OFFSET = 12;
        static constexpr uint32_t SIZE = 4;
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
    {"skySphereImage", false, 0, 0, 0, Access::WriteOnly, 0, nullptr},
    {"MipPoolStandIn", false, 0, 1, 0, Access::ReadOnly, 0, nullptr},
    {"outputWidth", true, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"outputHeight", true, 0, 0, 4, Access::ReadOnly, 0, nullptr},
    {"cellCount", true, 0, 0, 8, Access::ReadOnly, 0, nullptr},
    {"transmittanceEpsilon", true, 0, 0, 12, Access::ReadOnly, 0, nullptr},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "SkySphereAccumulate";
    static constexpr uint32_t NUM_MEMBERS = 6;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace SkySphereAccumulate
} // namespace ShaderInterface
