// ============================================================================
// Compact merged SDI declarations (graph-facing ABI)
// ============================================================================
//
// Program: SpatialReuseGather
// Generated from the merged shader reflection; do not edit.
// Layout mirrors live in SpatialReuseGather-SDI.layout.g.h.
//
// ============================================================================

#pragma once

#include <cstdint>

namespace ShaderInterface {
namespace SpatialReuseGather {

enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

namespace Set0 {

    struct Binding0 {
        static constexpr const char* NAME = "ReservoirConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding1 {
        static constexpr const char* NAME = "LightTreeBufferSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding2 {
        static constexpr const char* NAME = "ReservoirBufferA";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding3 {
        static constexpr const char* NAME = "ReservoirBufferB";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding4 {
        static constexpr const char* NAME = "SpatialReservoirDebugBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 4;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding5 {
        static constexpr const char* NAME = "HitRecordBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 5;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

namespace Bind {
using ReservoirConfigSSBO = Set0::Binding0;
using LightTreeBufferSSBO = Set0::Binding1;
using ReservoirBufferA = Set0::Binding2;
using ReservoirBufferB = Set0::Binding3;
using SpatialReservoirDebugBuffer = Set0::Binding4;
using HitRecordBuffer = Set0::Binding5;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 16;

    struct imgWidth {
        static constexpr const char* NAME = "imgWidth";
        static constexpr uint32_t INDEX = 0;
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct imgHeight {
        static constexpr const char* NAME = "imgHeight";
        static constexpr uint32_t INDEX = 1;
        static constexpr uint32_t OFFSET = 4;
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
    {"ReservoirConfigSSBO", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"LightTreeBufferSSBO", false, 0, 1, 0, Access::ReadOnly, 0, nullptr},
    {"ReservoirBufferA", false, 0, 2, 0, Access::ReadOnly, 0, nullptr},
    {"ReservoirBufferB", false, 0, 3, 0, Access::ReadOnly, 0, nullptr},
    {"SpatialReservoirDebugBuffer", false, 0, 4, 0, Access::WriteOnly, 0, nullptr},
    {"HitRecordBuffer", false, 0, 5, 0, Access::ReadOnly, 0, nullptr},
    {"imgWidth", true, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"imgHeight", true, 0, 0, 4, Access::ReadOnly, 0, nullptr},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "SpatialReuseGather";
    static constexpr uint32_t NUM_MEMBERS = 8;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace SpatialReuseGather
} // namespace ShaderInterface
