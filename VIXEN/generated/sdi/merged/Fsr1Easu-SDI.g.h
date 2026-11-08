// ============================================================================
// Compact merged SDI declarations (graph-facing ABI)
// ============================================================================
//
// Program: Fsr1Easu
// Generated from the merged shader reflection; do not edit.
// Layout mirrors live in Fsr1Easu-SDI.layout.g.h.
//
// ============================================================================

#pragma once

#include <cstdint>

namespace ShaderInterface {
namespace Fsr1Easu {

enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

namespace Set0 {

    struct Binding0 {
        static constexpr const char* NAME = "InputTexture";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding1 {
        static constexpr const char* NAME = "OutputTexture";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

namespace Bind {
using InputTexture = Set0::Binding0;
using OutputTexture = Set0::Binding1;
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
    {"InputTexture", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"OutputTexture", false, 0, 1, 0, Access::WriteOnly, 0, nullptr},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "Fsr1Easu";
    static constexpr uint32_t NUM_MEMBERS = 2;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace Fsr1Easu
} // namespace ShaderInterface
