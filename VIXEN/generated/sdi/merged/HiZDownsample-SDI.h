// ============================================================================
// Feature-Tagged Merged SDI (Semantic Shader Wiring S0)
// ============================================================================
//
// Program: HiZDownsample
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
namespace HiZDownsample {

namespace Set0 {

    /**
     * @brief srcDepthImage
     * Type: STORAGE_IMAGE
     */
    struct Binding0 {
        static constexpr const char* NAME = "srcDepthImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief tileMaxImage
     * Type: STORAGE_IMAGE
     */
    struct Binding1 {
        static constexpr const char* NAME = "tileMaxImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

// Name-keyed binding aliases (duplicate names skipped)
namespace Bind {
using srcDepthImage = Set0::Binding0;
using tileMaxImage = Set0::Binding1;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 16;

    struct srcWidth {
        static constexpr const char* NAME = "srcWidth";
        static constexpr uint32_t INDEX = 0;
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct srcHeight {
        static constexpr const char* NAME = "srcHeight";
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
    uint32_t featureCount;
    const char* const* features;
};


inline constexpr MemberInfo MEMBERS[] = {
    {"srcDepthImage", false, 0, 0, 0, 0, nullptr},
    {"tileMaxImage", false, 0, 1, 0, 0, nullptr},
    {"srcWidth", true, 0, 0, 0, 0, nullptr},
    {"srcHeight", true, 0, 0, 4, 0, nullptr},
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
    static constexpr const char* PROGRAM_NAME = "HiZDownsample";
    static constexpr uint32_t NUM_MEMBERS = 4;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace HiZDownsample
} // namespace ShaderInterface
