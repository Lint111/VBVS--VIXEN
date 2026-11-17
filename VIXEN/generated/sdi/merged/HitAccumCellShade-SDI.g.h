// ============================================================================
// Compact merged SDI declarations (graph-facing ABI)
// ============================================================================
//
// Program: HitAccumCellShade
// Generated from the merged shader reflection; do not edit.
// Layout mirrors live in HitAccumCellShade-SDI.layout.g.h.
//
// ============================================================================

#pragma once

#include <cstdint>

namespace ShaderInterface {
namespace HitAccumCellShade {

enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

namespace Set0 {

    struct Binding0 {
        static constexpr const char* NAME = "BodyInstanceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding1 {
        static constexpr const char* NAME = "BodyInstanceTransformBuffer";
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
        static constexpr const char* NAME = "BrickLookupBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding4 {
        static constexpr const char* NAME = "ChannelPoolBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 4;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding5 {
        static constexpr const char* NAME = "ESVOBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 5;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding6 {
        static constexpr const char* NAME = "GlyphOutlineBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 6;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding7 {
        static constexpr const char* NAME = "GlyphOutlineFlatBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 7;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding8 {
        static constexpr const char* NAME = "HitAccumCellRadiance";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 8;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding9 {
        static constexpr const char* NAME = "HitAccumParamsSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 9;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding10 {
        static constexpr const char* NAME = "HitAccumTable";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 10;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding11 {
        static constexpr const char* NAME = "InstanceIterDebugBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 11;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_GPU_TRACE_HOOKS"};
    };

    struct Binding13 {
        static constexpr const char* NAME = "InstanceSkipMaskBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 13;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding14 {
        static constexpr const char* NAME = "LightingConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 14;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding15 {
        static constexpr const char* NAME = "MaterialBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 15;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding16 {
        static constexpr const char* NAME = "MipPoolBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 16;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding17 {
        static constexpr const char* NAME = "OccupancyGridBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 17;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding18 {
        static constexpr const char* NAME = "OctreeConfigsSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 18;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding20 {
        static constexpr const char* NAME = "RayTraceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 20;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding22 {
        static constexpr const char* NAME = "RecipeTapeBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 22;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_UBER_RECIPE_MATERIAL"};
    };

    struct Binding26 {
        static constexpr const char* NAME = "ShadowConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 26;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding27 {
        static constexpr const char* NAME = "TierRefTableBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 27;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

namespace Bind {
using BodyInstanceBuffer = Set0::Binding0;
using BodyInstanceTransformBuffer = Set0::Binding1;
using BrickBuffer = Set0::Binding2;
using BrickLookupBuffer = Set0::Binding3;
using ChannelPoolBuffer = Set0::Binding4;
using ESVOBuffer = Set0::Binding5;
using GlyphOutlineBuffer = Set0::Binding6;
using GlyphOutlineFlatBuffer = Set0::Binding7;
using HitAccumCellRadiance = Set0::Binding8;
using HitAccumParamsSSBO = Set0::Binding9;
using HitAccumTable = Set0::Binding10;
using InstanceIterDebugBuffer = Set0::Binding11;
using InstanceSkipMaskBuffer = Set0::Binding13;
using LightingConfigSSBO = Set0::Binding14;
using MaterialBuffer = Set0::Binding15;
using MipPoolBuffer = Set0::Binding16;
using OccupancyGridBuffer = Set0::Binding17;
using OctreeConfigsSSBO = Set0::Binding18;
using RayTraceBuffer = Set0::Binding20;
using RecipeTapeBuffer = Set0::Binding22;
using ShadowConfigSSBO = Set0::Binding26;
using TierRefTableBuffer = Set0::Binding27;
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

inline constexpr const char* const kFeatures_Set0_Binding11[] = {"VIXEN_GPU_TRACE_HOOKS"};
inline constexpr const char* const kFeatures_Set0_Binding22[] = {"VIXEN_UBER_RECIPE_MATERIAL"};

inline constexpr MemberInfo MEMBERS[] = {
    {"BodyInstanceBuffer", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"BodyInstanceTransformBuffer", false, 0, 1, 0, Access::ReadOnly, 0, nullptr},
    {"BrickBuffer", false, 0, 2, 0, Access::ReadOnly, 0, nullptr},
    {"BrickLookupBuffer", false, 0, 3, 0, Access::ReadOnly, 0, nullptr},
    {"ChannelPoolBuffer", false, 0, 4, 0, Access::ReadOnly, 0, nullptr},
    {"ESVOBuffer", false, 0, 5, 0, Access::ReadOnly, 0, nullptr},
    {"GlyphOutlineBuffer", false, 0, 6, 0, Access::ReadOnly, 0, nullptr},
    {"GlyphOutlineFlatBuffer", false, 0, 7, 0, Access::ReadOnly, 0, nullptr},
    {"HitAccumCellRadiance", false, 0, 8, 0, Access::ReadWrite, 0, nullptr},
    {"HitAccumParamsSSBO", false, 0, 9, 0, Access::ReadOnly, 0, nullptr},
    {"HitAccumTable", false, 0, 10, 0, Access::ReadWrite, 0, nullptr},
    {"InstanceIterDebugBuffer", false, 0, 11, 0, Access::WriteOnly, 1, kFeatures_Set0_Binding11},
    {"InstanceSkipMaskBuffer", false, 0, 13, 0, Access::ReadOnly, 0, nullptr},
    {"LightingConfigSSBO", false, 0, 14, 0, Access::ReadOnly, 0, nullptr},
    {"MaterialBuffer", false, 0, 15, 0, Access::ReadOnly, 0, nullptr},
    {"MipPoolBuffer", false, 0, 16, 0, Access::ReadOnly, 0, nullptr},
    {"OccupancyGridBuffer", false, 0, 17, 0, Access::ReadOnly, 0, nullptr},
    {"OctreeConfigsSSBO", false, 0, 18, 0, Access::ReadOnly, 0, nullptr},
    {"RayTraceBuffer", false, 0, 20, 0, Access::ReadWrite, 0, nullptr},
    {"RecipeTapeBuffer", false, 0, 22, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding22},
    {"ShadowConfigSSBO", false, 0, 26, 0, Access::ReadOnly, 0, nullptr},
    {"TierRefTableBuffer", false, 0, 27, 0, Access::ReadOnly, 0, nullptr},
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
    static constexpr const char* PROGRAM_NAME = "HitAccumCellShade";
    static constexpr uint32_t NUM_MEMBERS = 36;
    static constexpr uint32_t NUM_FEATURES = 2;
};

} // namespace HitAccumCellShade
} // namespace ShaderInterface
