// ============================================================================
// Feature-Tagged Merged SDI (Semantic Shader Wiring S0)
// ============================================================================
//
// Program: InstanceOcclusionCull
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
namespace InstanceOcclusionCull {

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
     * @brief BodyInstanceBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding0 {
        static constexpr const char* NAME = "BodyInstanceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = BodyInstanceBuffer;
    };

    /**
     * @brief OctreeConfigsSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding1 {
        static constexpr const char* NAME = "OctreeConfigsSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = OctreeConfigsSSBO;
    };

    /**
     * @brief tileMaxImage
     * Type: STORAGE_IMAGE
     */
    struct Binding2 {
        static constexpr const char* NAME = "tileMaxImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief InstanceSkipMaskBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding3 {
        static constexpr const char* NAME = "InstanceSkipMaskBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = InstanceSkipMaskBuffer;
    };

} // namespace Set0

namespace Push {

    static constexpr uint32_t SIZE = 96;

    struct prevViewProj {
        static constexpr const char* NAME = "prevViewProj";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 64;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct prevCamPos {
        static constexpr const char* NAME = "prevCamPos";
        static constexpr uint32_t OFFSET = 64;
        static constexpr uint32_t SIZE = 16;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct dims {
        static constexpr const char* NAME = "dims";
        static constexpr uint32_t OFFSET = 80;
        static constexpr uint32_t SIZE = 16;
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


inline constexpr MemberInfo MEMBERS[] = {
    {"BodyInstanceBuffer", false, 0, 0, 0, 0, nullptr},
    {"OctreeConfigsSSBO", false, 0, 1, 0, 0, nullptr},
    {"tileMaxImage", false, 0, 2, 0, 0, nullptr},
    {"InstanceSkipMaskBuffer", false, 0, 3, 0, 0, nullptr},
    {"prevViewProj", true, 0, 0, 0, 0, nullptr},
    {"prevCamPos", true, 0, 0, 64, 0, nullptr},
    {"dims", true, 0, 0, 80, 0, nullptr},
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
    static constexpr const char* PROGRAM_NAME = "InstanceOcclusionCull";
    static constexpr uint32_t NUM_MEMBERS = 7;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace InstanceOcclusionCull
} // namespace ShaderInterface
