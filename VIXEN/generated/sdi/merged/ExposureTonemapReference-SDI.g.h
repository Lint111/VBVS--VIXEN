// ============================================================================
// Compact merged SDI declarations (graph-facing ABI)
// ============================================================================
//
// Program: ExposureTonemapReference
// Generated from the merged shader reflection; do not edit.
// Layout mirrors live in ExposureTonemapReference-SDI.layout.g.h.
//
// ============================================================================

#pragma once

#include <cstdint>

namespace ShaderInterface {
namespace ExposureTonemapReference {

enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

namespace Set0 {

    struct Binding0 {
        static constexpr const char* NAME = "sceneRadianceHistoryOutput";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding1 {
        static constexpr const char* NAME = "outputImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding2 {
        static constexpr const char* NAME = "meter";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding16 {
        static constexpr const char* NAME = "LightingConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 16;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

namespace Bind {
using sceneRadianceHistoryOutput = Set0::Binding0;
using outputImage = Set0::Binding1;
using meter = Set0::Binding2;
using LightingConfigSSBO = Set0::Binding16;
} // namespace Bind

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
    {"sceneRadianceHistoryOutput", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"outputImage", false, 0, 1, 0, Access::WriteOnly, 0, nullptr},
    {"meter", false, 0, 2, 0, Access::ReadOnly, 0, nullptr},
    {"LightingConfigSSBO", false, 0, 16, 0, Access::ReadOnly, 0, nullptr},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "ExposureTonemapReference";
    static constexpr uint32_t NUM_MEMBERS = 4;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace ExposureTonemapReference
} // namespace ShaderInterface
