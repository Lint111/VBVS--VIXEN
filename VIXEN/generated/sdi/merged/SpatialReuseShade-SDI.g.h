// ============================================================================
// Compact merged SDI declarations (graph-facing ABI)
// ============================================================================
//
// Program: SpatialReuseShade
// Generated from the merged shader reflection; do not edit.
// Layout mirrors live in SpatialReuseShade-SDI.layout.g.h.
//
// ============================================================================

#pragma once

#include <cstdint>

namespace ShaderInterface {
namespace SpatialReuseShade {

enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

namespace Set0 {

    struct Binding0 {
        static constexpr const char* NAME = "sceneRadianceImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding5 {
        static constexpr const char* NAME = "OctreeConfigsSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 5;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_MINING_BEAM"};
    };

    struct Binding16 {
        static constexpr const char* NAME = "LightingConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 16;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding17 {
        static constexpr const char* NAME = "HitRecordBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 17;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding18 {
        static constexpr const char* NAME = "ShadowConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 18;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding19 {
        static constexpr const char* NAME = "AccumulationConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 19;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding20 {
        static constexpr const char* NAME = "sceneRadianceHistory";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 20;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
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
        static constexpr const char* NAME = "worldPosHistoryImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 22;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding23 {
        static constexpr const char* NAME = "ReservoirConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 23;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding24 {
        static constexpr const char* NAME = "LightTreeBufferSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 24;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding27 {
        static constexpr const char* NAME = "SpatialReservoirDebugBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 27;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding31 {
        static constexpr const char* NAME = "DDGILeakGateDebugShadeSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 31;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding32 {
        static constexpr const char* NAME = "probeIrradianceAtlasRead";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 32;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding33 {
        static constexpr const char* NAME = "probeVisibilityAtlasRead";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 33;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding34 {
        static constexpr const char* NAME = "ProbeGridConfigReadSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 34;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding35 {
        static constexpr const char* NAME = "skySphereImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 35;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding36 {
        static constexpr const char* NAME = "HitAccumTable";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 36;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_SRS_CELL_RESOLVE"};
    };

    struct Binding37 {
        static constexpr const char* NAME = "HitAccumParamsSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 37;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_SRS_CELL_RESOLVE"};
    };

    struct Binding38 {
        static constexpr const char* NAME = "HitAccumCellRadiance";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 38;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_SRS_CELL_RESOLVE"};
    };

    struct Binding39 {
        static constexpr const char* NAME = "BodyInstanceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 39;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_SRS_CELL_RESOLVE"};
    };

    struct Binding44 {
        static constexpr const char* NAME = "MiningBeamBufferSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 44;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_MINING_BEAM"};
    };

    struct Binding45 {
        static constexpr const char* NAME = "MiningBeamBodyInstanceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 45;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_MINING_BEAM"};
    };

    struct Binding46 {
        static constexpr const char* NAME = "MiningBeamInstanceTransformBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 46;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_MINING_BEAM"};
    };

    struct Binding48 {
        static constexpr const char* NAME = "sceneRadianceHistoryOutput";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 48;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding49 {
        static constexpr const char* NAME = "worldPosHistoryOutput";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 49;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
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
using sceneRadianceImage = Set0::Binding0;
using OctreeConfigsSSBO = Set0::Binding5;
using LightingConfigSSBO = Set0::Binding16;
using HitRecordBuffer = Set0::Binding17;
using ShadowConfigSSBO = Set0::Binding18;
using AccumulationConfigSSBO = Set0::Binding19;
using sceneRadianceHistory = Set0::Binding20;
using PrevCameraConfigSSBO = Set0::Binding21;
using worldPosHistoryImage = Set0::Binding22;
using ReservoirConfigSSBO = Set0::Binding23;
using LightTreeBufferSSBO = Set0::Binding24;
using SpatialReservoirDebugBuffer = Set0::Binding27;
using DDGILeakGateDebugShadeSSBO = Set0::Binding31;
using probeIrradianceAtlasRead = Set0::Binding32;
using probeVisibilityAtlasRead = Set0::Binding33;
using ProbeGridConfigReadSSBO = Set0::Binding34;
using skySphereImage = Set0::Binding35;
using HitAccumTable = Set0::Binding36;
using HitAccumParamsSSBO = Set0::Binding37;
using HitAccumCellRadiance = Set0::Binding38;
using BodyInstanceBuffer = Set0::Binding39;
using MiningBeamBufferSSBO = Set0::Binding44;
using MiningBeamBodyInstanceBuffer = Set0::Binding45;
using MiningBeamInstanceTransformBuffer = Set0::Binding46;
using sceneRadianceHistoryOutput = Set0::Binding48;
using worldPosHistoryOutput = Set0::Binding49;
using GlyphOutlineBuffer = Set0::Binding51;
using GlyphOutlineFlatBuffer = Set0::Binding52;
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

    struct moduleIndirectIrradiance {
        static constexpr const char* NAME = "moduleIndirectIrradiance";
        static constexpr uint32_t INDEX = 14;
        static constexpr uint32_t OFFSET = 96;
        static constexpr uint32_t SIZE = 12;
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

inline constexpr const char* const kFeatures_Set0_Binding5[] = {"VIXEN_MINING_BEAM"};
inline constexpr const char* const kFeatures_Set0_Binding36[] = {"VIXEN_SRS_CELL_RESOLVE"};
inline constexpr const char* const kFeatures_Set0_Binding37[] = {"VIXEN_SRS_CELL_RESOLVE"};
inline constexpr const char* const kFeatures_Set0_Binding38[] = {"VIXEN_SRS_CELL_RESOLVE"};
inline constexpr const char* const kFeatures_Set0_Binding39[] = {"VIXEN_SRS_CELL_RESOLVE"};
inline constexpr const char* const kFeatures_Set0_Binding44[] = {"VIXEN_MINING_BEAM"};
inline constexpr const char* const kFeatures_Set0_Binding45[] = {"VIXEN_MINING_BEAM"};
inline constexpr const char* const kFeatures_Set0_Binding46[] = {"VIXEN_MINING_BEAM"};

inline constexpr MemberInfo MEMBERS[] = {
    {"sceneRadianceImage", false, 0, 0, 0, Access::WriteOnly, 0, nullptr},
    {"OctreeConfigsSSBO", false, 0, 5, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding5},
    {"LightingConfigSSBO", false, 0, 16, 0, Access::ReadOnly, 0, nullptr},
    {"HitRecordBuffer", false, 0, 17, 0, Access::ReadOnly, 0, nullptr},
    {"ShadowConfigSSBO", false, 0, 18, 0, Access::ReadOnly, 0, nullptr},
    {"AccumulationConfigSSBO", false, 0, 19, 0, Access::ReadOnly, 0, nullptr},
    {"sceneRadianceHistory", false, 0, 20, 0, Access::ReadOnly, 0, nullptr},
    {"PrevCameraConfigSSBO", false, 0, 21, 0, Access::ReadOnly, 0, nullptr},
    {"worldPosHistoryImage", false, 0, 22, 0, Access::ReadOnly, 0, nullptr},
    {"ReservoirConfigSSBO", false, 0, 23, 0, Access::ReadOnly, 0, nullptr},
    {"LightTreeBufferSSBO", false, 0, 24, 0, Access::ReadOnly, 0, nullptr},
    {"SpatialReservoirDebugBuffer", false, 0, 27, 0, Access::ReadOnly, 0, nullptr},
    {"DDGILeakGateDebugShadeSSBO", false, 0, 31, 0, Access::ReadWrite, 0, nullptr},
    {"probeIrradianceAtlasRead", false, 0, 32, 0, Access::ReadOnly, 0, nullptr},
    {"probeVisibilityAtlasRead", false, 0, 33, 0, Access::ReadOnly, 0, nullptr},
    {"ProbeGridConfigReadSSBO", false, 0, 34, 0, Access::ReadOnly, 0, nullptr},
    {"skySphereImage", false, 0, 35, 0, Access::ReadOnly, 0, nullptr},
    {"HitAccumTable", false, 0, 36, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding36},
    {"HitAccumParamsSSBO", false, 0, 37, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding37},
    {"HitAccumCellRadiance", false, 0, 38, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding38},
    {"BodyInstanceBuffer", false, 0, 39, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding39},
    {"MiningBeamBufferSSBO", false, 0, 44, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding44},
    {"MiningBeamBodyInstanceBuffer", false, 0, 45, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding45},
    {"MiningBeamInstanceTransformBuffer", false, 0, 46, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding46},
    {"sceneRadianceHistoryOutput", false, 0, 48, 0, Access::WriteOnly, 0, nullptr},
    {"worldPosHistoryOutput", false, 0, 49, 0, Access::WriteOnly, 0, nullptr},
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
    {"moduleIndirectIrradiance", true, 0, 0, 96, Access::ReadOnly, 0, nullptr},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "SpatialReuseShade";
    static constexpr uint32_t NUM_MEMBERS = 43;
    static constexpr uint32_t NUM_FEATURES = 3;
};

} // namespace SpatialReuseShade
} // namespace ShaderInterface
