// ============================================================================
// Feature-Tagged Merged SDI (Semantic Shader Wiring S0)
// ============================================================================
//
// Program: ProbeUpdate
// Feature axis: VIXEN_GPU_TRACE_HOOKS
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
namespace ProbeUpdate {

/**
 * @brief ESVOBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xb9224cc8281c62e (for runtime discovery)
 */
struct ESVOBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xb9224cc8281c62eULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief BrickBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x541cbd85094c043f (for runtime discovery)
 */
struct BrickBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x541cbd85094c043fULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief MaterialBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xecdc8a9c9d1897d (for runtime discovery)
 */
struct MaterialBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xecdc8a9c9d1897dULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "Material";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 32;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief RayTraceBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x84cbfdc88582508 (for runtime discovery)
 */
struct RayTraceBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x84cbfdc88582508ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };
    struct pc_1 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 4;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 1;
    };
    struct pc_2 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 8;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 2;
    };
    struct pc_3 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 16;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 3;
    };

};

/**
 * @brief OctreeConfigsSSBO
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xaf0b4419476a6289 (for runtime discovery)
 */
struct OctreeConfigsSSBO {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xaf0b4419476a6289ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "OctreeConfig";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 432;
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

/**
 * @brief ChannelPoolBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xa5e12f5b7b9b9a83 (for runtime discovery)
 */
struct ChannelPoolBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xa5e12f5b7b9b9a83ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief BrickLookupBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xd38e5bfc557294ef (for runtime discovery)
 */
struct BrickLookupBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xd38e5bfc557294efULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief MipPoolBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x333d9a7d0bb610bd (for runtime discovery)
 */
struct MipPoolBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x333d9a7d0bb610bdULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief InstanceIterDebugBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x10a2936c7eb15ac5 (for runtime discovery)
 */
struct InstanceIterDebugBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x10a2936c7eb15ac5ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief TierRefTableBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xa0bfa51917a7609c (for runtime discovery)
 */
struct TierRefTableBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xa0bfa51917a7609cULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "TierRef";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 20;
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
 * @brief ProbeGridConfigSSBO
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xf9df306938bceb6 (for runtime discovery)
 */
struct ProbeGridConfigSSBO {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xf9df306938bceb6ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "ProbeGridConfig";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 56;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief DDGILeakGateDebugSSBO
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x4eab1e81643c3983 (for runtime discovery)
 */
struct DDGILeakGateDebugSSBO {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x4eab1e81643c3983ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "DDGILeakGateDebug";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 60;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief InstanceSkipMaskBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xc1cb627b00db3a9e (for runtime discovery)
 */
struct InstanceSkipMaskBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xc1cb627b00db3a9eULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

namespace Set0 {

    /**
     * @brief ESVOBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding1 {
        static constexpr const char* NAME = "ESVOBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = ESVOBuffer;
    };

    /**
     * @brief BrickBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding2 {
        static constexpr const char* NAME = "BrickBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = BrickBuffer;
    };

    /**
     * @brief MaterialBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding3 {
        static constexpr const char* NAME = "MaterialBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = MaterialBuffer;
    };

    /**
     * @brief RayTraceBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding4 {
        static constexpr const char* NAME = "RayTraceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 4;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = RayTraceBuffer;
    };

    /**
     * @brief OctreeConfigsSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding5 {
        static constexpr const char* NAME = "OctreeConfigsSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 5;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = OctreeConfigsSSBO;
    };

    /**
     * @brief BodyInstanceBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding10 {
        static constexpr const char* NAME = "BodyInstanceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 10;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = BodyInstanceBuffer;
    };

    /**
     * @brief ChannelPoolBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding11 {
        static constexpr const char* NAME = "ChannelPoolBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 11;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = ChannelPoolBuffer;
    };

    /**
     * @brief BrickLookupBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding12 {
        static constexpr const char* NAME = "BrickLookupBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 12;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = BrickLookupBuffer;
    };

    /**
     * @brief MipPoolBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding13 {
        static constexpr const char* NAME = "MipPoolBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 13;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = MipPoolBuffer;
    };

    /**
     * @brief InstanceIterDebugBuffer
     * Type: STORAGE_BUFFER
     * Requires: VIXEN_GPU_TRACE_HOOKS
     */
    struct Binding14 {
        static constexpr const char* NAME = "InstanceIterDebugBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 14;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 1;
        static constexpr const char* FEATURES[1] = {"VIXEN_GPU_TRACE_HOOKS"};
        using DataType = InstanceIterDebugBuffer;
    };

    /**
     * @brief TierRefTableBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding15 {
        static constexpr const char* NAME = "TierRefTableBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 15;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = TierRefTableBuffer;
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
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = LightTreeBufferSSBO;
    };

    /**
     * @brief ProbeGridConfigSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding28 {
        static constexpr const char* NAME = "ProbeGridConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 28;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = ProbeGridConfigSSBO;
    };

    /**
     * @brief probeIrradianceAtlas
     * Type: STORAGE_IMAGE
     */
    struct Binding29 {
        static constexpr const char* NAME = "probeIrradianceAtlas";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 29;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief probeVisibilityAtlas
     * Type: STORAGE_IMAGE
     */
    struct Binding30 {
        static constexpr const char* NAME = "probeVisibilityAtlas";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 30;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief DDGILeakGateDebugSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding31 {
        static constexpr const char* NAME = "DDGILeakGateDebugSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 31;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = DDGILeakGateDebugSSBO;
    };

    /**
     * @brief InstanceSkipMaskBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding35 {
        static constexpr const char* NAME = "InstanceSkipMaskBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 35;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = InstanceSkipMaskBuffer;
    };

} // namespace Set0

// Name-keyed binding aliases (duplicate names skipped)
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
using LightTreeBufferSSBO = Set0::Binding24;
using ProbeGridConfigSSBO = Set0::Binding28;
using probeIrradianceAtlas = Set0::Binding29;
using probeVisibilityAtlas = Set0::Binding30;
using DDGILeakGateDebugSSBO = Set0::Binding31;
using InstanceSkipMaskBuffer = Set0::Binding35;
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
    uint32_t featureCount;
    const char* const* features;
};

inline constexpr const char* const kFeatures_Set0_Binding14[] = {"VIXEN_GPU_TRACE_HOOKS"};

inline constexpr MemberInfo MEMBERS[] = {
    {"ESVOBuffer", false, 0, 1, 0, 0, nullptr},
    {"BrickBuffer", false, 0, 2, 0, 0, nullptr},
    {"MaterialBuffer", false, 0, 3, 0, 0, nullptr},
    {"RayTraceBuffer", false, 0, 4, 0, 0, nullptr},
    {"OctreeConfigsSSBO", false, 0, 5, 0, 0, nullptr},
    {"BodyInstanceBuffer", false, 0, 10, 0, 0, nullptr},
    {"ChannelPoolBuffer", false, 0, 11, 0, 0, nullptr},
    {"BrickLookupBuffer", false, 0, 12, 0, 0, nullptr},
    {"MipPoolBuffer", false, 0, 13, 0, 0, nullptr},
    {"InstanceIterDebugBuffer", false, 0, 14, 0, 1, kFeatures_Set0_Binding14},
    {"TierRefTableBuffer", false, 0, 15, 0, 0, nullptr},
    {"LightTreeBufferSSBO", false, 0, 24, 0, 0, nullptr},
    {"ProbeGridConfigSSBO", false, 0, 28, 0, 0, nullptr},
    {"probeIrradianceAtlas", false, 0, 29, 0, 0, nullptr},
    {"probeVisibilityAtlas", false, 0, 30, 0, 0, nullptr},
    {"DDGILeakGateDebugSSBO", false, 0, 31, 0, 0, nullptr},
    {"InstanceSkipMaskBuffer", false, 0, 35, 0, 0, nullptr},
    {"cameraPos", true, 0, 0, 0, 0, nullptr},
    {"time", true, 0, 0, 12, 0, nullptr},
    {"cameraDir", true, 0, 0, 16, 0, nullptr},
    {"fov", true, 0, 0, 28, 0, nullptr},
    {"cameraUp", true, 0, 0, 32, 0, nullptr},
    {"aspect", true, 0, 0, 44, 0, nullptr},
    {"cameraRight", true, 0, 0, 48, 0, nullptr},
    {"debugMode", true, 0, 0, 60, 0, nullptr},
    {"raySizeCoef", true, 0, 0, 64, 0, nullptr},
    {"raySizeBias", true, 0, 0, 68, 0, nullptr},
    {"instanceCount", true, 0, 0, 72, 0, nullptr},
    {"debugTargetPixel", true, 0, 0, 80, 0, nullptr},
    {"accumFrameCount", true, 0, 0, 88, 0, nullptr},
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
    static constexpr const char* PROGRAM_NAME = "ProbeUpdate";
    static constexpr uint32_t NUM_MEMBERS = 30;
    static constexpr uint32_t NUM_FEATURES = 1;
};

} // namespace ProbeUpdate
} // namespace ShaderInterface
