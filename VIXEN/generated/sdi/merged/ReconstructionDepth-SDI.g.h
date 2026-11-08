// ============================================================================
// Compact merged SDI declarations (graph-facing ABI)
// ============================================================================
//
// Program: ReconstructionDepth
// Generated from the merged shader reflection; do not edit.
// Layout mirrors live in ReconstructionDepth-SDI.layout.g.h.
//
// ============================================================================

#pragma once

#include <cstdint>

namespace ShaderInterface {
namespace ReconstructionDepth {

enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

namespace Set0 {

    struct Binding0 {
        static constexpr const char* NAME = "HitRecordBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding1 {
        static constexpr const char* NAME = "viewDepthImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding2 {
        static constexpr const char* NAME = "normalizedDepthImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

namespace Bind {
using HitRecordBuffer = Set0::Binding0;
using viewDepthImage = Set0::Binding1;
using normalizedDepthImage = Set0::Binding2;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 112;

    struct invProjection {
        static constexpr const char* NAME = "invProjection";
        static constexpr uint32_t INDEX = 0;
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 64;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct cameraPos {
        static constexpr const char* NAME = "cameraPos";
        static constexpr uint32_t INDEX = 1;
        static constexpr uint32_t OFFSET = 64;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct cameraDir {
        static constexpr const char* NAME = "cameraDir";
        static constexpr uint32_t INDEX = 2;
        static constexpr uint32_t OFFSET = 80;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct depthEncoding {
        static constexpr const char* NAME = "depthEncoding";
        static constexpr uint32_t INDEX = 3;
        static constexpr uint32_t OFFSET = 96;
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
    {"HitRecordBuffer", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"viewDepthImage", false, 0, 1, 0, Access::WriteOnly, 0, nullptr},
    {"normalizedDepthImage", false, 0, 2, 0, Access::WriteOnly, 0, nullptr},
    {"invProjection", true, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"cameraPos", true, 0, 0, 64, Access::ReadOnly, 0, nullptr},
    {"cameraDir", true, 0, 0, 80, Access::ReadOnly, 0, nullptr},
    {"depthEncoding", true, 0, 0, 96, Access::ReadOnly, 0, nullptr},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "ReconstructionDepth";
    static constexpr uint32_t NUM_MEMBERS = 7;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace ReconstructionDepth
} // namespace ShaderInterface
