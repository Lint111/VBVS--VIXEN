// ============================================================================
// Compact merged SDI declarations (graph-facing ABI)
// ============================================================================
//
// Program: ExposureMeter
// Generated from the merged shader reflection; do not edit.
// Layout mirrors live in ExposureMeter-SDI.layout.g.h.
//
// ============================================================================

#pragma once

#include <cstdint>

namespace ShaderInterface {
namespace ExposureMeter {

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
        static constexpr const char* NAME = "result";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

namespace Bind {
using sceneRadianceHistoryOutput = Set0::Binding0;
using result = Set0::Binding1;
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
    {"result", false, 0, 1, 0, Access::ReadWrite, 0, nullptr},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "ExposureMeter";
    static constexpr uint32_t NUM_MEMBERS = 2;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace ExposureMeter
} // namespace ShaderInterface
