// ============================================================================
// Feature-Tagged Merged SDI (Semantic Shader Wiring S0)
// ============================================================================
//
// Program: HitAccumResolve
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
namespace HitAccumResolve {

// Per-binding access mode, from SPIR-V decorations (storage kinds)
// or the descriptor kind's inherent read-only nature. Feeds the
// derived hazard/sync sets (semantic-wiring S3).
enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

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

namespace Set0 {

    /**
     * @brief outputImage
     * Type: STORAGE_IMAGE
     */
    struct Binding0 {
        static constexpr const char* NAME = "outputImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief HitRecordBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding1 {
        static constexpr const char* NAME = "HitRecordBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = HitRecordBuffer;
    };

    /**
     * @brief BodyInstanceBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding2 {
        static constexpr const char* NAME = "BodyInstanceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = BodyInstanceBuffer;
    };

    /**
     * @brief HitAccumTable
     * Type: STORAGE_BUFFER
     */
    struct Binding3 {
        static constexpr const char* NAME = "HitAccumTable";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = HitAccumTable;
    };

    /**
     * @brief HitAccumParamsSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding4 {
        static constexpr const char* NAME = "HitAccumParamsSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 4;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = HitAccumParamsSSBO;
    };

    /**
     * @brief HitAccumCellRadiance
     * Type: STORAGE_BUFFER
     */
    struct Binding5 {
        static constexpr const char* NAME = "HitAccumCellRadiance";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 5;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = HitAccumCellRadiance;
    };

} // namespace Set0

// Name-keyed binding aliases (duplicate names skipped)
namespace Bind {
using outputImage = Set0::Binding0;
using HitRecordBuffer = Set0::Binding1;
using BodyInstanceBuffer = Set0::Binding2;
using HitAccumTable = Set0::Binding3;
using HitAccumParamsSSBO = Set0::Binding4;
using HitAccumCellRadiance = Set0::Binding5;
} // namespace Bind

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
    {"outputImage", false, 0, 0, 0, Access::ReadWrite, 0, nullptr},
    {"HitRecordBuffer", false, 0, 1, 0, Access::ReadOnly, 0, nullptr},
    {"BodyInstanceBuffer", false, 0, 2, 0, Access::ReadOnly, 0, nullptr},
    {"HitAccumTable", false, 0, 3, 0, Access::ReadOnly, 0, nullptr},
    {"HitAccumParamsSSBO", false, 0, 4, 0, Access::ReadOnly, 0, nullptr},
    {"HitAccumCellRadiance", false, 0, 5, 0, Access::ReadOnly, 0, nullptr},
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
    static constexpr const char* PROGRAM_NAME = "HitAccumResolve";
    static constexpr uint32_t NUM_MEMBERS = 6;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace HitAccumResolve
} // namespace ShaderInterface
