// ============================================================================
// Feature-Tagged Merged SDI (Semantic Shader Wiring S0)
// ============================================================================
//
// Program: SpatialReuseGather
// Feature axis: (none — single-variant interface)
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
namespace SpatialReuseGather {

// Per-binding access mode, from SPIR-V decorations (storage kinds)
// or the descriptor kind's inherent read-only nature. Feeds the
// derived hazard/sync sets (semantic-wiring S3).
enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

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
 * @brief ReservoirBufferA
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x34c38814902f6faf (for runtime discovery)
 */
struct ReservoirBufferA {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x34c38814902f6fafULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "ReservoirRecord";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 16;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief ReservoirBufferB
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xbbee7ca5b932c9a9 (for runtime discovery)
 */
struct ReservoirBufferB {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xbbee7ca5b932c9a9ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "ReservoirRecord";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 16;
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

namespace Set0 {

    /**
     * @brief ReservoirConfigSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding0 {
        static constexpr const char* NAME = "ReservoirConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
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
    struct Binding1 {
        static constexpr const char* NAME = "LightTreeBufferSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = LightTreeBufferSSBO;
    };

    /**
     * @brief ReservoirBufferA
     * Type: STORAGE_BUFFER
     */
    struct Binding2 {
        static constexpr const char* NAME = "ReservoirBufferA";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = ReservoirBufferA;
    };

    /**
     * @brief ReservoirBufferB
     * Type: STORAGE_BUFFER
     */
    struct Binding3 {
        static constexpr const char* NAME = "ReservoirBufferB";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = ReservoirBufferB;
    };

    /**
     * @brief SpatialReservoirDebugBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding4 {
        static constexpr const char* NAME = "SpatialReservoirDebugBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 4;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = SpatialReservoirDebugBuffer;
    };

    /**
     * @brief HitRecordBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding5 {
        static constexpr const char* NAME = "HitRecordBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 5;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = HitRecordBuffer;
    };

} // namespace Set0

// Name-keyed binding aliases (duplicate names skipped)
namespace Bind {
using ReservoirConfigSSBO = Set0::Binding0;
using LightTreeBufferSSBO = Set0::Binding1;
using ReservoirBufferA = Set0::Binding2;
using ReservoirBufferB = Set0::Binding3;
using SpatialReservoirDebugBuffer = Set0::Binding4;
using HitRecordBuffer = Set0::Binding5;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 16;

    struct imgWidth {
        static constexpr const char* NAME = "imgWidth";
        static constexpr uint32_t INDEX = 0;
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct imgHeight {
        static constexpr const char* NAME = "imgHeight";
        static constexpr uint32_t INDEX = 1;
        static constexpr uint32_t OFFSET = 4;
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


inline constexpr MemberInfo MEMBERS[] = {
    {"ReservoirConfigSSBO", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"LightTreeBufferSSBO", false, 0, 1, 0, Access::ReadOnly, 0, nullptr},
    {"ReservoirBufferA", false, 0, 2, 0, Access::ReadOnly, 0, nullptr},
    {"ReservoirBufferB", false, 0, 3, 0, Access::ReadOnly, 0, nullptr},
    {"SpatialReservoirDebugBuffer", false, 0, 4, 0, Access::WriteOnly, 0, nullptr},
    {"HitRecordBuffer", false, 0, 5, 0, Access::ReadOnly, 0, nullptr},
    {"imgWidth", true, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"imgHeight", true, 0, 0, 4, Access::ReadOnly, 0, nullptr},
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
    static constexpr const char* PROGRAM_NAME = "SpatialReuseGather";
    static constexpr uint32_t NUM_MEMBERS = 8;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace SpatialReuseGather
} // namespace ShaderInterface
