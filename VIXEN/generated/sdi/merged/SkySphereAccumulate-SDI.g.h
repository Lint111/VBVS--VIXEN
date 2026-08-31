// ============================================================================
// Feature-Tagged Merged SDI (Semantic Shader Wiring S0)
// ============================================================================
//
// Program: SkySphereAccumulate
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
namespace SkySphereAccumulate {

// Per-binding access mode, from SPIR-V decorations (storage kinds)
// or the descriptor kind's inherent read-only nature. Feeds the
// derived hazard/sync sets (semantic-wiring S3).
enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

/**
 * @brief MipPoolStandIn
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x7decd23e56f90859 (for runtime discovery)
 */
struct MipPoolStandIn {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x7decd23e56f90859ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "MipPoolSample";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 8;
        static constexpr uint32_t BINDING = 0;
    };

};

namespace Set0 {

    /**
     * @brief skySphereImage
     * Type: STORAGE_IMAGE
     */
    struct Binding0 {
        static constexpr const char* NAME = "skySphereImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief MipPoolStandIn
     * Type: STORAGE_BUFFER
     */
    struct Binding1 {
        static constexpr const char* NAME = "MipPoolStandIn";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = MipPoolStandIn;
    };

} // namespace Set0

// Name-keyed binding aliases (duplicate names skipped)
namespace Bind {
using skySphereImage = Set0::Binding0;
using MipPoolStandIn = Set0::Binding1;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 16;

    struct outputWidth {
        static constexpr const char* NAME = "outputWidth";
        static constexpr uint32_t INDEX = 0;
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct outputHeight {
        static constexpr const char* NAME = "outputHeight";
        static constexpr uint32_t INDEX = 1;
        static constexpr uint32_t OFFSET = 4;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct cellCount {
        static constexpr const char* NAME = "cellCount";
        static constexpr uint32_t INDEX = 2;
        static constexpr uint32_t OFFSET = 8;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct transmittanceEpsilon {
        static constexpr const char* NAME = "transmittanceEpsilon";
        static constexpr uint32_t INDEX = 3;
        static constexpr uint32_t OFFSET = 12;
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
    {"skySphereImage", false, 0, 0, 0, Access::WriteOnly, 0, nullptr},
    {"MipPoolStandIn", false, 0, 1, 0, Access::ReadOnly, 0, nullptr},
    {"outputWidth", true, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"outputHeight", true, 0, 0, 4, Access::ReadOnly, 0, nullptr},
    {"cellCount", true, 0, 0, 8, Access::ReadOnly, 0, nullptr},
    {"transmittanceEpsilon", true, 0, 0, 12, Access::ReadOnly, 0, nullptr},
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
    static constexpr const char* PROGRAM_NAME = "SkySphereAccumulate";
    static constexpr uint32_t NUM_MEMBERS = 6;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace SkySphereAccumulate
} // namespace ShaderInterface
