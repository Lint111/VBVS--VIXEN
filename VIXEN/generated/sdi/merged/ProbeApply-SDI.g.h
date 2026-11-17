// ============================================================================
// Compact merged SDI declarations (graph-facing ABI)
// ============================================================================
//
// Program: ProbeApply
// Generated from the merged shader reflection; do not edit.
// Layout mirrors live in ProbeApply-SDI.layout.g.h.
//
// ============================================================================

#pragma once

#include <cstdint>

namespace ShaderInterface {
namespace ProbeApply {

enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

namespace Set0 {

    struct Binding0 {
        static constexpr const char* NAME = "DDGILeakGateDebugSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding1 {
        static constexpr const char* NAME = "ProbeGridConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding2 {
        static constexpr const char* NAME = "ProbeRayPayloadBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding3 {
        static constexpr const char* NAME = "ShadowRayResultBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding4 {
        static constexpr const char* NAME = "probeIrradianceAtlas";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 4;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding5 {
        static constexpr const char* NAME = "probeVisibilityAtlas";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 5;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

namespace Bind {
using DDGILeakGateDebugSSBO = Set0::Binding0;
using ProbeGridConfigSSBO = Set0::Binding1;
using ProbeRayPayloadBuffer = Set0::Binding2;
using ShadowRayResultBuffer = Set0::Binding3;
using probeIrradianceAtlas = Set0::Binding4;
using probeVisibilityAtlas = Set0::Binding5;
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
    {"DDGILeakGateDebugSSBO", false, 0, 0, 0, Access::ReadWrite, 0, nullptr},
    {"ProbeGridConfigSSBO", false, 0, 1, 0, Access::ReadOnly, 0, nullptr},
    {"ProbeRayPayloadBuffer", false, 0, 2, 0, Access::ReadOnly, 0, nullptr},
    {"ShadowRayResultBuffer", false, 0, 3, 0, Access::ReadOnly, 0, nullptr},
    {"probeIrradianceAtlas", false, 0, 4, 0, Access::ReadWrite, 0, nullptr},
    {"probeVisibilityAtlas", false, 0, 5, 0, Access::ReadWrite, 0, nullptr},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "ProbeApply";
    static constexpr uint32_t NUM_MEMBERS = 6;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace ProbeApply
} // namespace ShaderInterface
