// ============================================================================
// Compact merged SDI declarations (graph-facing ABI)
// ============================================================================
//
// Program: BodyInstanceRayMarch
// Generated from the merged shader reflection; do not edit.
// Layout mirrors live in BodyInstanceRayMarch-SDI.layout.g.h.
//
// ============================================================================

#pragma once

#include <cstdint>

namespace ShaderInterface {
namespace BodyInstanceRayMarch {

enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

namespace Set0 {

    struct Binding0 {
        static constexpr const char* NAME = "AccumulationConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding1 {
        static constexpr const char* NAME = "BodyInstanceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding2 {
        static constexpr const char* NAME = "BodyInstanceMotionBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_RECONSTRUCTION_INPUTS"};
    };

    struct Binding3 {
        static constexpr const char* NAME = "BodyInstanceTransformBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding4 {
        static constexpr const char* NAME = "BrickBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 4;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding5 {
        static constexpr const char* NAME = "BrickLookupBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 5;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding6 {
        static constexpr const char* NAME = "ChannelPoolBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 6;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding7 {
        static constexpr const char* NAME = "ESVOBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 7;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding8 {
        static constexpr const char* NAME = "GlyphOutlineBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 8;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding9 {
        static constexpr const char* NAME = "GlyphOutlineFlatBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 9;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding10 {
        static constexpr const char* NAME = "HitRecordBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 10;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding11 {
        static constexpr const char* NAME = "InstanceBandBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 11;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding12 {
        static constexpr const char* NAME = "InstanceIterDebugBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 12;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_GPU_TRACE_HOOKS"};
    };

    struct Binding13 {
        static constexpr const char* NAME = "InstanceOrderBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 13;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding14 {
        static constexpr const char* NAME = "InstanceSkipMaskBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 14;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding15 {
        static constexpr const char* NAME = "LightingConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 15;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding16 {
        static constexpr const char* NAME = "MaterialBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 16;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding17 {
        static constexpr const char* NAME = "MipPoolBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 17;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding18 {
        static constexpr const char* NAME = "OccupancyGridBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 18;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding19 {
        static constexpr const char* NAME = "OctreeConfigsSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 19;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding20 {
        static constexpr const char* NAME = "PolicyStencilTileBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 20;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 2;
        static constexpr const char* FEATURES[2] = {"VIXEN_POLICY_STENCIL", "VIXEN_POLICY_STENCIL_TILES"};
    };

    struct Binding21 {
        static constexpr const char* NAME = "PrevCameraConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 21;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding22 {
        static constexpr const char* NAME = "ProxyIntervalBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 22;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_B2_PROXY_PREPASS"};
    };

    struct Binding23 {
        static constexpr const char* NAME = "RayTraceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 23;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding24 {
        static constexpr const char* NAME = "RecipeChunkAabbBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 24;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_RECIPE_CHUNK_CULL"};
    };

    struct Binding25 {
        static constexpr const char* NAME = "RecipeTapeBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 25;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_UBER_RECIPE_MATERIAL"};
    };

    struct Binding26 {
        static constexpr const char* NAME = "RtQueryProxyAabbBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 26;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_RTQUERY_TRAVERSAL"};
    };

    struct Binding29 {
        static constexpr const char* NAME = "ShadowConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 29;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding30 {
        static constexpr const char* NAME = "TierRefTableBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 30;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding31 {
        static constexpr const char* NAME = "TileBinViewBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 31;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding32 {
        static constexpr const char* NAME = "TileRankWordBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 32;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding33 {
        static constexpr const char* NAME = "depthDistanceImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 33;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_B1_OCCLUSION_CULL"};
    };

    struct Binding34 {
        static constexpr const char* NAME = "historyImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 34;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding35 {
        static constexpr const char* NAME = "idOutputImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 35;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding36 {
        static constexpr const char* NAME = "motionValidityImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 36;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_RECONSTRUCTION_INPUTS"};
    };

    struct Binding37 {
        static constexpr const char* NAME = "motionVectorImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 37;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_RECONSTRUCTION_INPUTS"};
    };

    struct Binding38 {
        static constexpr const char* NAME = "outputImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 38;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding39 {
        static constexpr const char* NAME = "rtQueryTlas";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 39;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_RTQUERY_TRAVERSAL"};
    };

} // namespace Set0

namespace Bind {
using AccumulationConfigSSBO = Set0::Binding0;
using BodyInstanceBuffer = Set0::Binding1;
using BodyInstanceMotionBuffer = Set0::Binding2;
using BodyInstanceTransformBuffer = Set0::Binding3;
using BrickBuffer = Set0::Binding4;
using BrickLookupBuffer = Set0::Binding5;
using ChannelPoolBuffer = Set0::Binding6;
using ESVOBuffer = Set0::Binding7;
using GlyphOutlineBuffer = Set0::Binding8;
using GlyphOutlineFlatBuffer = Set0::Binding9;
using HitRecordBuffer = Set0::Binding10;
using InstanceBandBuffer = Set0::Binding11;
using InstanceIterDebugBuffer = Set0::Binding12;
using InstanceOrderBuffer = Set0::Binding13;
using InstanceSkipMaskBuffer = Set0::Binding14;
using LightingConfigSSBO = Set0::Binding15;
using MaterialBuffer = Set0::Binding16;
using MipPoolBuffer = Set0::Binding17;
using OccupancyGridBuffer = Set0::Binding18;
using OctreeConfigsSSBO = Set0::Binding19;
using PolicyStencilTileBuffer = Set0::Binding20;
using PrevCameraConfigSSBO = Set0::Binding21;
using ProxyIntervalBuffer = Set0::Binding22;
using RayTraceBuffer = Set0::Binding23;
using RecipeChunkAabbBuffer = Set0::Binding24;
using RecipeTapeBuffer = Set0::Binding25;
using RtQueryProxyAabbBuffer = Set0::Binding26;
using ShadowConfigSSBO = Set0::Binding29;
using TierRefTableBuffer = Set0::Binding30;
using TileBinViewBuffer = Set0::Binding31;
using TileRankWordBuffer = Set0::Binding32;
using depthDistanceImage = Set0::Binding33;
using historyImage = Set0::Binding34;
using idOutputImage = Set0::Binding35;
using motionValidityImage = Set0::Binding36;
using motionVectorImage = Set0::Binding37;
using outputImage = Set0::Binding38;
using rtQueryTlas = Set0::Binding39;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 112;

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

    struct proxyAabbCount {
        static constexpr const char* NAME = "proxyAabbCount";
        static constexpr uint32_t INDEX = 14;
        static constexpr uint32_t OFFSET = 96;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_B2_PROXY_PREPASS"};
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

inline constexpr const char* const kFeatures_Set0_Binding2[] = {"VIXEN_RECONSTRUCTION_INPUTS"};
inline constexpr const char* const kFeatures_Set0_Binding12[] = {"VIXEN_GPU_TRACE_HOOKS"};
inline constexpr const char* const kFeatures_Set0_Binding20[] = {"VIXEN_POLICY_STENCIL", "VIXEN_POLICY_STENCIL_TILES"};
inline constexpr const char* const kFeatures_Set0_Binding22[] = {"VIXEN_B2_PROXY_PREPASS"};
inline constexpr const char* const kFeatures_Set0_Binding24[] = {"VIXEN_RECIPE_CHUNK_CULL"};
inline constexpr const char* const kFeatures_Set0_Binding25[] = {"VIXEN_UBER_RECIPE_MATERIAL"};
inline constexpr const char* const kFeatures_Set0_Binding26[] = {"VIXEN_RTQUERY_TRAVERSAL"};
inline constexpr const char* const kFeatures_Set0_Binding33[] = {"VIXEN_B1_OCCLUSION_CULL"};
inline constexpr const char* const kFeatures_Set0_Binding36[] = {"VIXEN_RECONSTRUCTION_INPUTS"};
inline constexpr const char* const kFeatures_Set0_Binding37[] = {"VIXEN_RECONSTRUCTION_INPUTS"};
inline constexpr const char* const kFeatures_Set0_Binding39[] = {"VIXEN_RTQUERY_TRAVERSAL"};
inline constexpr const char* const kFeatures_Push_proxyAabbCount[] = {"VIXEN_B2_PROXY_PREPASS"};

inline constexpr MemberInfo MEMBERS[] = {
    {"AccumulationConfigSSBO", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"BodyInstanceBuffer", false, 0, 1, 0, Access::ReadOnly, 0, nullptr},
    {"BodyInstanceMotionBuffer", false, 0, 2, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding2},
    {"BodyInstanceTransformBuffer", false, 0, 3, 0, Access::ReadOnly, 0, nullptr},
    {"BrickBuffer", false, 0, 4, 0, Access::ReadOnly, 0, nullptr},
    {"BrickLookupBuffer", false, 0, 5, 0, Access::ReadOnly, 0, nullptr},
    {"ChannelPoolBuffer", false, 0, 6, 0, Access::ReadOnly, 0, nullptr},
    {"ESVOBuffer", false, 0, 7, 0, Access::ReadOnly, 0, nullptr},
    {"GlyphOutlineBuffer", false, 0, 8, 0, Access::ReadOnly, 0, nullptr},
    {"GlyphOutlineFlatBuffer", false, 0, 9, 0, Access::ReadOnly, 0, nullptr},
    {"HitRecordBuffer", false, 0, 10, 0, Access::ReadWrite, 0, nullptr},
    {"InstanceBandBuffer", false, 0, 11, 0, Access::ReadOnly, 0, nullptr},
    {"InstanceIterDebugBuffer", false, 0, 12, 0, Access::WriteOnly, 1, kFeatures_Set0_Binding12},
    {"InstanceOrderBuffer", false, 0, 13, 0, Access::ReadOnly, 0, nullptr},
    {"InstanceSkipMaskBuffer", false, 0, 14, 0, Access::ReadOnly, 0, nullptr},
    {"LightingConfigSSBO", false, 0, 15, 0, Access::ReadOnly, 0, nullptr},
    {"MaterialBuffer", false, 0, 16, 0, Access::ReadOnly, 0, nullptr},
    {"MipPoolBuffer", false, 0, 17, 0, Access::ReadOnly, 0, nullptr},
    {"OccupancyGridBuffer", false, 0, 18, 0, Access::ReadOnly, 0, nullptr},
    {"OctreeConfigsSSBO", false, 0, 19, 0, Access::ReadOnly, 0, nullptr},
    {"PolicyStencilTileBuffer", false, 0, 20, 0, Access::ReadWrite, 2, kFeatures_Set0_Binding20},
    {"PrevCameraConfigSSBO", false, 0, 21, 0, Access::ReadOnly, 0, nullptr},
    {"ProxyIntervalBuffer", false, 0, 22, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding22},
    {"RayTraceBuffer", false, 0, 23, 0, Access::ReadWrite, 0, nullptr},
    {"RecipeChunkAabbBuffer", false, 0, 24, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding24},
    {"RecipeTapeBuffer", false, 0, 25, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding25},
    {"RtQueryProxyAabbBuffer", false, 0, 26, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding26},
    {"ShadowConfigSSBO", false, 0, 29, 0, Access::ReadOnly, 0, nullptr},
    {"TierRefTableBuffer", false, 0, 30, 0, Access::ReadOnly, 0, nullptr},
    {"TileBinViewBuffer", false, 0, 31, 0, Access::ReadOnly, 0, nullptr},
    {"TileRankWordBuffer", false, 0, 32, 0, Access::ReadOnly, 0, nullptr},
    {"depthDistanceImage", false, 0, 33, 0, Access::WriteOnly, 1, kFeatures_Set0_Binding33},
    {"historyImage", false, 0, 34, 0, Access::ReadWrite, 0, nullptr},
    {"idOutputImage", false, 0, 35, 0, Access::WriteOnly, 0, nullptr},
    {"motionValidityImage", false, 0, 36, 0, Access::WriteOnly, 1, kFeatures_Set0_Binding36},
    {"motionVectorImage", false, 0, 37, 0, Access::WriteOnly, 1, kFeatures_Set0_Binding37},
    {"outputImage", false, 0, 38, 0, Access::WriteOnly, 0, nullptr},
    {"rtQueryTlas", false, 0, 39, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding39},
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
    {"proxyAabbCount", true, 0, 0, 96, Access::ReadOnly, 1, kFeatures_Push_proxyAabbCount},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "BodyInstanceRayMarch";
    static constexpr uint32_t NUM_MEMBERS = 53;
    static constexpr uint32_t NUM_FEATURES = 9;
};

} // namespace BodyInstanceRayMarch
} // namespace ShaderInterface
