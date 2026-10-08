// ============================================================================
// Feature-Tagged Merged SDI (Semantic Shader Wiring S0)
// ============================================================================
//
// Program: ExposureTonemap
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
namespace ExposureTonemap {

// Per-binding access mode, from SPIR-V decorations (storage kinds)
// or the descriptor kind's inherent read-only nature. Feeds the
// derived hazard/sync sets (semantic-wiring S3).
enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

/**
 * @brief ExposureMeterResult
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x7a82e00a809949e8 (for runtime discovery)
 */
struct ExposureMeterResult {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x7a82e00a809949e8ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "float";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief LightingConfigSSBO
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x21ad6abbc3ea8eae (for runtime discovery)
 */
struct LightingConfigSSBO {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x21ad6abbc3ea8eaeULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "LightingConfig";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 208;
        static constexpr uint32_t BINDING = 0;
    };

};

namespace Set0 {

    /**
     * @brief sceneRadianceHistory
     * Type: STORAGE_IMAGE
     */
    struct Binding0 {
        static constexpr const char* NAME = "sceneRadianceHistory";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief outputImage
     * Type: STORAGE_IMAGE
     */
    struct Binding1 {
        static constexpr const char* NAME = "outputImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief meter
     * Type: STORAGE_BUFFER
     */
    struct Binding2 {
        static constexpr const char* NAME = "meter";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = ExposureMeterResult;
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

} // namespace Set0

// Name-keyed binding aliases (duplicate names skipped)
namespace Bind {
using sceneRadianceHistory = Set0::Binding0;
using outputImage = Set0::Binding1;
using meter = Set0::Binding2;
using LightingConfigSSBO = Set0::Binding16;
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
    {"sceneRadianceHistory", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"outputImage", false, 0, 1, 0, Access::WriteOnly, 0, nullptr},
    {"meter", false, 0, 2, 0, Access::ReadOnly, 0, nullptr},
    {"LightingConfigSSBO", false, 0, 16, 0, Access::ReadOnly, 0, nullptr},
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
    static constexpr const char* PROGRAM_NAME = "ExposureTonemap";
    static constexpr uint32_t NUM_MEMBERS = 4;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace ExposureTonemap
} // namespace ShaderInterface
