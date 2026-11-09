// ============================================================================
// Compact merged SDI declarations (graph-facing ABI)
// ============================================================================
//
// Program: ShadowRayTrace
// Generated from the merged shader reflection; do not edit.
// Layout mirrors live in ShadowRayTrace-SDI.layout.g.h.
//
// ============================================================================

#pragma once

#include <cstdint>

namespace ShaderInterface {
namespace ShadowRayTrace {

enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

namespace Set0 {

    struct Binding1 {
        static constexpr const char* NAME = "ESVOBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding2 {
        static constexpr const char* NAME = "BrickBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding3 {
        static constexpr const char* NAME = "MaterialBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding4 {
        static constexpr const char* NAME = "RayTraceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 4;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding5 {
        static constexpr const char* NAME = "OctreeConfigsSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 5;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding10 {
        static constexpr const char* NAME = "BodyInstanceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 10;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding11 {
        static constexpr const char* NAME = "ChannelPoolBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 11;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding12 {
        static constexpr const char* NAME = "BrickLookupBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 12;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding13 {
        static constexpr const char* NAME = "MipPoolBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 13;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding14 {
        static constexpr const char* NAME = "InstanceIterDebugBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 14;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_GPU_TRACE_HOOKS"};
    };

    struct Binding15 {
        static constexpr const char* NAME = "TierRefTableBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 15;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding35 {
        static constexpr const char* NAME = "InstanceSkipMaskBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 35;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding37 {
        static constexpr const char* NAME = "ShadowRayRequestBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 37;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding38 {
        static constexpr const char* NAME = "ShadowRayResultBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 38;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding47 {
        static constexpr const char* NAME = "BodyInstanceTransformBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 47;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding48 {
        static constexpr const char* NAME = "OccupancyGridBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 48;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding51 {
        static constexpr const char* NAME = "GlyphOutlineBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 51;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding52 {
        static constexpr const char* NAME = "GlyphOutlineFlatBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 52;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

namespace Bind {
using ESVOBuffer = Set0::Binding1;
using BrickBuffer = Set0::Binding2;
using MaterialBuffer = Set0::Binding3;
using RayTraceBuffer = Set0::Binding4;
using OctreeConfigsSSBO = Set0::Binding5;
using BodyInstanceBuffer = Set0::Binding10;
using ChannelPoolBuffer = Set0::Binding11;
using BrickLookupBuffer = Set0::Binding12;
using MipPoolBuffer = Set0::Binding13;
using InstanceIterDebugBuffer = Set0::Binding14;
using TierRefTableBuffer = Set0::Binding15;
using InstanceSkipMaskBuffer = Set0::Binding35;
using ShadowRayRequestBuffer = Set0::Binding37;
using ShadowRayResultBuffer = Set0::Binding38;
using BodyInstanceTransformBuffer = Set0::Binding47;
using OccupancyGridBuffer = Set0::Binding48;
using GlyphOutlineBuffer = Set0::Binding51;
using GlyphOutlineFlatBuffer = Set0::Binding52;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 96;

    struct cameraPos {
        static constexpr const char* NAME = "cameraPos";
        static constexpr uint32_t INDEX = 0;
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct time {
        static constexpr const char* NAME = "time";
        static constexpr uint32_t INDEX = 1;
        static constexpr uint32_t OFFSET = 12;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct cameraDir {
        static constexpr const char* NAME = "cameraDir";
        static constexpr uint32_t INDEX = 2;
        static constexpr uint32_t OFFSET = 16;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct fov {
        static constexpr const char* NAME = "fov";
        static constexpr uint32_t INDEX = 3;
        static constexpr uint32_t OFFSET = 28;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct cameraUp {
        static constexpr const char* NAME = "cameraUp";
        static constexpr uint32_t INDEX = 4;
        static constexpr uint32_t OFFSET = 32;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct aspect {
        static constexpr const char* NAME = "aspect";
        static constexpr uint32_t INDEX = 5;
        static constexpr uint32_t OFFSET = 44;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct cameraRight {
        static constexpr const char* NAME = "cameraRight";
        static constexpr uint32_t INDEX = 6;
        static constexpr uint32_t OFFSET = 48;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct debugMode {
        static constexpr const char* NAME = "debugMode";
        static constexpr uint32_t INDEX = 7;
        static constexpr uint32_t OFFSET = 60;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct raySizeCoef {
        static constexpr const char* NAME = "raySizeCoef";
        static constexpr uint32_t INDEX = 8;
        static constexpr uint32_t OFFSET = 64;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct raySizeBias {
        static constexpr const char* NAME = "raySizeBias";
        static constexpr uint32_t INDEX = 9;
        static constexpr uint32_t OFFSET = 68;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct instanceCount {
        static constexpr const char* NAME = "instanceCount";
        static constexpr uint32_t INDEX = 10;
        static constexpr uint32_t OFFSET = 72;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct debugTargetPixel {
        static constexpr const char* NAME = "debugTargetPixel";
        static constexpr uint32_t INDEX = 11;
        static constexpr uint32_t OFFSET = 80;
        static constexpr uint32_t SIZE = 8;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct accumFrameCount {
        static constexpr const char* NAME = "accumFrameCount";
        static constexpr uint32_t INDEX = 12;
        static constexpr uint32_t OFFSET = 88;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct cosmicK {
        static constexpr const char* NAME = "cosmicK";
        static constexpr uint32_t INDEX = 13;
        static constexpr uint32_t OFFSET = 92;
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

inline constexpr const char* const kFeatures_Set0_Binding14[] = {"VIXEN_GPU_TRACE_HOOKS"};

inline constexpr MemberInfo MEMBERS[] = {
    {"ESVOBuffer", false, 0, 1, 0, Access::ReadOnly, 0, nullptr},
    {"BrickBuffer", false, 0, 2, 0, Access::ReadOnly, 0, nullptr},
    {"MaterialBuffer", false, 0, 3, 0, Access::ReadOnly, 0, nullptr},
    {"RayTraceBuffer", false, 0, 4, 0, Access::ReadWrite, 0, nullptr},
    {"OctreeConfigsSSBO", false, 0, 5, 0, Access::ReadOnly, 0, nullptr},
    {"BodyInstanceBuffer", false, 0, 10, 0, Access::ReadOnly, 0, nullptr},
    {"ChannelPoolBuffer", false, 0, 11, 0, Access::ReadOnly, 0, nullptr},
    {"BrickLookupBuffer", false, 0, 12, 0, Access::ReadOnly, 0, nullptr},
    {"MipPoolBuffer", false, 0, 13, 0, Access::ReadOnly, 0, nullptr},
    {"InstanceIterDebugBuffer", false, 0, 14, 0, Access::WriteOnly, 1, kFeatures_Set0_Binding14},
    {"TierRefTableBuffer", false, 0, 15, 0, Access::ReadOnly, 0, nullptr},
    {"InstanceSkipMaskBuffer", false, 0, 35, 0, Access::ReadOnly, 0, nullptr},
    {"ShadowRayRequestBuffer", false, 0, 37, 0, Access::ReadOnly, 0, nullptr},
    {"ShadowRayResultBuffer", false, 0, 38, 0, Access::WriteOnly, 0, nullptr},
    {"BodyInstanceTransformBuffer", false, 0, 47, 0, Access::ReadOnly, 0, nullptr},
    {"OccupancyGridBuffer", false, 0, 48, 0, Access::ReadOnly, 0, nullptr},
    {"GlyphOutlineBuffer", false, 0, 51, 0, Access::ReadOnly, 0, nullptr},
    {"GlyphOutlineFlatBuffer", false, 0, 52, 0, Access::ReadOnly, 0, nullptr},
    {"cameraPos", true, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"time", true, 0, 0, 12, Access::ReadOnly, 0, nullptr},
    {"cameraDir", true, 0, 0, 16, Access::ReadOnly, 0, nullptr},
    {"fov", true, 0, 0, 28, Access::ReadOnly, 0, nullptr},
    {"cameraUp", true, 0, 0, 32, Access::ReadOnly, 0, nullptr},
    {"aspect", true, 0, 0, 44, Access::ReadOnly, 0, nullptr},
    {"cameraRight", true, 0, 0, 48, Access::ReadOnly, 0, nullptr},
    {"debugMode", true, 0, 0, 60, Access::ReadOnly, 0, nullptr},
    {"raySizeCoef", true, 0, 0, 64, Access::ReadOnly, 0, nullptr},
    {"raySizeBias", true, 0, 0, 68, Access::ReadOnly, 0, nullptr},
    {"instanceCount", true, 0, 0, 72, Access::ReadOnly, 0, nullptr},
    {"debugTargetPixel", true, 0, 0, 80, Access::ReadOnly, 0, nullptr},
    {"accumFrameCount", true, 0, 0, 88, Access::ReadOnly, 0, nullptr},
    {"cosmicK", true, 0, 0, 92, Access::ReadOnly, 0, nullptr},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "ShadowRayTrace";
    static constexpr uint32_t NUM_MEMBERS = 32;
    static constexpr uint32_t NUM_FEATURES = 1;
};

} // namespace ShadowRayTrace
} // namespace ShaderInterface
