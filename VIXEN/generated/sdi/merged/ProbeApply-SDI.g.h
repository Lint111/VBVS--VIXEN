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

    struct Binding28 {
        static constexpr const char* NAME = "ProbeGridConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 28;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding29 {
        static constexpr const char* NAME = "probeIrradianceAtlas";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 29;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding30 {
        static constexpr const char* NAME = "probeVisibilityAtlas";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 30;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding31 {
        static constexpr const char* NAME = "DDGILeakGateDebugSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 31;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding38 {
        static constexpr const char* NAME = "ShadowRayResultBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 38;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding39 {
        static constexpr const char* NAME = "ProbeRayPayloadBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 39;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

namespace Bind {
using ProbeGridConfigSSBO = Set0::Binding28;
using probeIrradianceAtlas = Set0::Binding29;
using probeVisibilityAtlas = Set0::Binding30;
using DDGILeakGateDebugSSBO = Set0::Binding31;
using ShadowRayResultBuffer = Set0::Binding38;
using ProbeRayPayloadBuffer = Set0::Binding39;
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
    {"ProbeGridConfigSSBO", false, 0, 28, 0, Access::ReadOnly, 0, nullptr},
    {"probeIrradianceAtlas", false, 0, 29, 0, Access::ReadWrite, 0, nullptr},
    {"probeVisibilityAtlas", false, 0, 30, 0, Access::ReadWrite, 0, nullptr},
    {"DDGILeakGateDebugSSBO", false, 0, 31, 0, Access::ReadWrite, 0, nullptr},
    {"ShadowRayResultBuffer", false, 0, 38, 0, Access::ReadOnly, 0, nullptr},
    {"ProbeRayPayloadBuffer", false, 0, 39, 0, Access::ReadOnly, 0, nullptr},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "ProbeApply";
    static constexpr uint32_t NUM_MEMBERS = 6;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace ProbeApply
} // namespace ShaderInterface
