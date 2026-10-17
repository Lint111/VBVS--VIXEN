// ============================================================================
// Feature-Tagged Merged SDI (Semantic Shader Wiring S0)
// ============================================================================
//
// Program: ReconstructionHistory
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
namespace ReconstructionHistory {

// Per-binding access mode, from SPIR-V decorations (storage kinds)
// or the descriptor kind's inherent read-only nature. Feeds the
// derived hazard/sync sets (semantic-wiring S3).
enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

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
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief historyA
     * Type: STORAGE_IMAGE
     */
    struct Binding1 {
        static constexpr const char* NAME = "historyA";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief historyB
     * Type: STORAGE_IMAGE
     */
    struct Binding2 {
        static constexpr const char* NAME = "historyB";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

// Name-keyed binding aliases (duplicate names skipped)
namespace Bind {
using sceneRadianceImage = Set0::Binding0;
using historyA = Set0::Binding1;
using historyB = Set0::Binding2;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 16;

    struct writeIndex {
        static constexpr const char* NAME = "writeIndex";
        static constexpr uint32_t INDEX = 0;
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Push

struct PushBlock {
    uint32_t writeIndex;
    uint8_t _tail[12];
};

static_assert(offsetof(PushBlock, writeIndex) == Push::writeIndex::OFFSET, "PushBlock.writeIndex is not at its SDI OFFSET");
static_assert(sizeof(PushBlock) == Push::SIZE, "PushBlock size differs from Push::SIZE");

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
    {"sceneRadianceImage", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"historyA", false, 0, 1, 0, Access::WriteOnly, 0, nullptr},
    {"historyB", false, 0, 2, 0, Access::WriteOnly, 0, nullptr},
    {"writeIndex", true, 0, 0, 0, Access::ReadOnly, 0, nullptr},
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
    static constexpr const char* PROGRAM_NAME = "ReconstructionHistory";
    static constexpr uint32_t NUM_MEMBERS = 4;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace ReconstructionHistory
} // namespace ShaderInterface
