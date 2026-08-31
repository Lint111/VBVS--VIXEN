// ============================================================================
// Feature-Tagged Merged SDI (Semantic Shader Wiring S0)
// ============================================================================
//
// Program: SpatialReuseShade
// Feature axis: VIXEN_GPU_TRACE_HOOKS VIXEN_SRS_CELL_RESOLVE
//
// Merged across compiled feature variants: every member carries the
// feature conjunction under which it exists (empty = unconditional).
//
// DO NOT MODIFY THIS FILE MANUALLY - it will be regenerated.
//
// ============================================================================

#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace ShaderInterface {
namespace SpatialReuseShade {

// Per-binding access mode, from SPIR-V decorations (storage kinds)
// or the descriptor kind's inherent read-only nature. Feeds the
// derived hazard/sync sets (semantic-wiring S3).
enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

/**
 * @brief LightingConfigSSBO
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x6dc24fcf8fba6cee (for runtime discovery)
 */
struct LightingConfigSSBO {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x6dc24fcf8fba6ceeULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "LightingConfig";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 144;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief HitRecordBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x55fface242208434 (for runtime discovery)
 */
struct HitRecordBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x55fface242208434ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "HitRecord";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 60;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief ShadowConfigSSBO
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x3b53615a99b577f6 (for runtime discovery)
 */
struct ShadowConfigSSBO {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x3b53615a99b577f6ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "ShadowConfig";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 16;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief AccumulationConfigSSBO
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x5ce2d892be4b2b19 (for runtime discovery)
 */
struct AccumulationConfigSSBO {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x5ce2d892be4b2b19ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "AccumulationConfig";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 20;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief PrevCameraConfigSSBO
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x411234352776140c (for runtime discovery)
 */
struct PrevCameraConfigSSBO {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x411234352776140cULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "PrevCameraConfig";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 64;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief ReservoirConfigSSBO
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x2a45c4769623e86b (for runtime discovery)
 */
struct ReservoirConfigSSBO {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x2a45c4769623e86bULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "ReservoirConfig";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 32;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief LightTreeBufferSSBO
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x2b167e5b19a6a950 (for runtime discovery)
 */
struct LightTreeBufferSSBO {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x2b167e5b19a6a950ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "LightTreeBuffer";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 2064;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief SpatialReservoirDebugBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xb74da2ed75726bb4 (for runtime discovery)
 */
struct SpatialReservoirDebugBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xb74da2ed75726bb4ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "ReservoirRecord";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 16;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief DDGILeakGateDebugShadeSSBO
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x2976d0b82be0c4ec (for runtime discovery)
 */
struct DDGILeakGateDebugShadeSSBO {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x2976d0b82be0c4ecULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "DDGILeakGateDebugShade";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 56;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief ProbeGridConfigReadSSBO
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xb2dd7ab08f447af2 (for runtime discovery)
 */
struct ProbeGridConfigReadSSBO {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xb2dd7ab08f447af2ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "ProbeGridConfig";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 56;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief HitAccumTable
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x745341172a420820 (for runtime discovery)
 */
struct HitAccumTable {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x745341172a420820ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "HitAccumEntryGpu";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 56;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief HitAccumParamsSSBO
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x4e1444a4056b6d4a (for runtime discovery)
 */
struct HitAccumParamsSSBO {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x4e1444a4056b6d4aULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "HitAccumParams";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 48;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief HitAccumCellRadiance
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xbae52bb2aaed7efb (for runtime discovery)
 */
struct HitAccumCellRadiance {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xbae52bb2aaed7efbULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief BodyInstanceBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x3335dc522c336e07 (for runtime discovery)
 */
struct BodyInstanceBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x3335dc522c336e07ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "BodyInstance";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 64;
        static constexpr uint32_t BINDING = 0;
    };

};

namespace Set0 {

    /**
     * @brief sceneRadianceImage
     * Type: STORAGE_IMAGE
     */
    struct Binding0 {
        static constexpr const char* NAME = "sceneRadianceImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief LightingConfigSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding16 {
        static constexpr const char* NAME = "LightingConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 16;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = LightingConfigSSBO;
    };

    /**
     * @brief HitRecordBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding17 {
        static constexpr const char* NAME = "HitRecordBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 17;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = HitRecordBuffer;
    };

    /**
     * @brief ShadowConfigSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding18 {
        static constexpr const char* NAME = "ShadowConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 18;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = ShadowConfigSSBO;
    };

    /**
     * @brief AccumulationConfigSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding19 {
        static constexpr const char* NAME = "AccumulationConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 19;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = AccumulationConfigSSBO;
    };

    /**
     * @brief sceneRadianceHistory
     * Type: STORAGE_IMAGE
     */
    struct Binding20 {
        static constexpr const char* NAME = "sceneRadianceHistory";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 20;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief PrevCameraConfigSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding21 {
        static constexpr const char* NAME = "PrevCameraConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 21;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = PrevCameraConfigSSBO;
    };

    /**
     * @brief worldPosHistoryImage
     * Type: STORAGE_IMAGE
     */
    struct Binding22 {
        static constexpr const char* NAME = "worldPosHistoryImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 22;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief ReservoirConfigSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding23 {
        static constexpr const char* NAME = "ReservoirConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 23;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = ReservoirConfigSSBO;
    };

    /**
     * @brief LightTreeBufferSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding24 {
        static constexpr const char* NAME = "LightTreeBufferSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 24;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = LightTreeBufferSSBO;
    };

    /**
     * @brief SpatialReservoirDebugBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding27 {
        static constexpr const char* NAME = "SpatialReservoirDebugBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 27;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = SpatialReservoirDebugBuffer;
    };

    /**
     * @brief DDGILeakGateDebugShadeSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding31 {
        static constexpr const char* NAME = "DDGILeakGateDebugShadeSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 31;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = DDGILeakGateDebugShadeSSBO;
    };

    /**
     * @brief probeIrradianceAtlasRead
     * Type: STORAGE_IMAGE
     */
    struct Binding32 {
        static constexpr const char* NAME = "probeIrradianceAtlasRead";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 32;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief probeVisibilityAtlasRead
     * Type: STORAGE_IMAGE
     */
    struct Binding33 {
        static constexpr const char* NAME = "probeVisibilityAtlasRead";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 33;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief ProbeGridConfigReadSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding34 {
        static constexpr const char* NAME = "ProbeGridConfigReadSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 34;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = ProbeGridConfigReadSSBO;
    };

    /**
     * @brief HitAccumTable
     * Type: STORAGE_BUFFER
     * Requires: VIXEN_SRS_CELL_RESOLVE
     */
    struct Binding36 {
        static constexpr const char* NAME = "HitAccumTable";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 36;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_SRS_CELL_RESOLVE"};
        using DataType = HitAccumTable;
    };

    /**
     * @brief HitAccumParamsSSBO
     * Type: STORAGE_BUFFER
     * Requires: VIXEN_SRS_CELL_RESOLVE
     */
    struct Binding37 {
        static constexpr const char* NAME = "HitAccumParamsSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 37;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_SRS_CELL_RESOLVE"};
        using DataType = HitAccumParamsSSBO;
    };

    /**
     * @brief HitAccumCellRadiance
     * Type: STORAGE_BUFFER
     * Requires: VIXEN_SRS_CELL_RESOLVE
     */
    struct Binding38 {
        static constexpr const char* NAME = "HitAccumCellRadiance";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 38;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_SRS_CELL_RESOLVE"};
        using DataType = HitAccumCellRadiance;
    };

    /**
     * @brief BodyInstanceBuffer
     * Type: STORAGE_BUFFER
     * Requires: VIXEN_SRS_CELL_RESOLVE
     */
    struct Binding39 {
        static constexpr const char* NAME = "BodyInstanceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 39;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_SRS_CELL_RESOLVE"};
        using DataType = BodyInstanceBuffer;
    };

} // namespace Set0

// Name-keyed binding aliases (duplicate names skipped)
namespace Bind {
using sceneRadianceImage = Set0::Binding0;
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
using HitAccumTable = Set0::Binding36;
using HitAccumParamsSSBO = Set0::Binding37;
using HitAccumCellRadiance = Set0::Binding38;
using BodyInstanceBuffer = Set0::Binding39;
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

// ============================================================================
// Member table (bindings + push members) for the semantic connect walk
// ============================================================================

struct MemberInfo {
    const char* name;
    bool isPushMember;
    uint32_t set;      // descriptor members only
    uint32_t binding;  // descriptor members only
    uint32_t offset;   // push members only
    Access access;     // push members: ReadOnly by nature
    uint32_t featureCount;
    const char* const* features;
};

inline constexpr const char* const kFeatures_Set0_Binding36[] = {"VIXEN_SRS_CELL_RESOLVE"};
inline constexpr const char* const kFeatures_Set0_Binding37[] = {"VIXEN_SRS_CELL_RESOLVE"};
inline constexpr const char* const kFeatures_Set0_Binding38[] = {"VIXEN_SRS_CELL_RESOLVE"};
inline constexpr const char* const kFeatures_Set0_Binding39[] = {"VIXEN_SRS_CELL_RESOLVE"};

inline constexpr MemberInfo MEMBERS[] = {
    {"sceneRadianceImage", false, 0, 0, 0, Access::WriteOnly, 0, nullptr},
    {"LightingConfigSSBO", false, 0, 16, 0, Access::ReadOnly, 0, nullptr},
    {"HitRecordBuffer", false, 0, 17, 0, Access::ReadOnly, 0, nullptr},
    {"ShadowConfigSSBO", false, 0, 18, 0, Access::ReadOnly, 0, nullptr},
    {"AccumulationConfigSSBO", false, 0, 19, 0, Access::ReadOnly, 0, nullptr},
    {"sceneRadianceHistory", false, 0, 20, 0, Access::ReadWrite, 0, nullptr},
    {"PrevCameraConfigSSBO", false, 0, 21, 0, Access::ReadOnly, 0, nullptr},
    {"worldPosHistoryImage", false, 0, 22, 0, Access::ReadWrite, 0, nullptr},
    {"ReservoirConfigSSBO", false, 0, 23, 0, Access::ReadOnly, 0, nullptr},
    {"LightTreeBufferSSBO", false, 0, 24, 0, Access::ReadOnly, 0, nullptr},
    {"SpatialReservoirDebugBuffer", false, 0, 27, 0, Access::ReadOnly, 0, nullptr},
    {"DDGILeakGateDebugShadeSSBO", false, 0, 31, 0, Access::ReadWrite, 0, nullptr},
    {"probeIrradianceAtlasRead", false, 0, 32, 0, Access::ReadOnly, 0, nullptr},
    {"probeVisibilityAtlasRead", false, 0, 33, 0, Access::ReadOnly, 0, nullptr},
    {"ProbeGridConfigReadSSBO", false, 0, 34, 0, Access::ReadOnly, 0, nullptr},
    {"HitAccumTable", false, 0, 36, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding36},
    {"HitAccumParamsSSBO", false, 0, 37, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding37},
    {"HitAccumCellRadiance", false, 0, 38, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding38},
    {"BodyInstanceBuffer", false, 0, 39, 0, Access::ReadOnly, 1, kFeatures_Set0_Binding39},
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

/**
 * @brief Members present under the given active feature set
 */
inline std::vector<MemberInfo> Members(
    const std::unordered_set<std::string>& activeFeatures
) {
    std::vector<MemberInfo> out;
    for (const auto& m : MEMBERS) {
        bool present = true;
        for (uint32_t i = 0; i < m.featureCount; ++i) {
            if (activeFeatures.count(m.features[i]) == 0) { present = false; break; }
        }
        if (present) out.push_back(m);
    }
    return out;
}

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "SpatialReuseShade";
    static constexpr uint32_t NUM_MEMBERS = 33;
    static constexpr uint32_t NUM_FEATURES = 2;
};

} // namespace SpatialReuseShade
} // namespace ShaderInterface
