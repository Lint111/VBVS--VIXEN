// ============================================================================
// Compact merged SDI declarations (graph-facing ABI)
// ============================================================================
//
// Program: ReconstructionHistory
// Generated from the merged shader reflection; do not edit.
// Layout mirrors live in ReconstructionHistory-SDI.layout.g.h.
//
// ============================================================================

#pragma once

#include <cstdint>

namespace ShaderInterface {
namespace ReconstructionHistory {

enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

namespace Set0 {

    struct Binding0 {
        static constexpr const char* NAME = "sceneRadianceImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding1 {
        static constexpr const char* NAME = "historyA";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding2 {
        static constexpr const char* NAME = "historyB";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

namespace Bind {
using sceneRadianceImage = Set0::Binding0;
using historyA = Set0::Binding1;
using historyB = Set0::Binding2;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 16;

    struct writeIndex {
        static constexpr const char* NAME = "writeIndex";
        static constexpr uint32_t INDEX = 0;
        static constexpr uint32_t OFFSET = 0;
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
    {"sceneRadianceImage", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"historyA", false, 0, 1, 0, Access::WriteOnly, 0, nullptr},
    {"historyB", false, 0, 2, 0, Access::WriteOnly, 0, nullptr},
    {"writeIndex", true, 0, 0, 0, Access::ReadOnly, 0, nullptr},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "ReconstructionHistory";
    static constexpr uint32_t NUM_MEMBERS = 4;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace ReconstructionHistory
} // namespace ShaderInterface
