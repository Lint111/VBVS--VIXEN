// ============================================================================
// Feature-Tagged Merged SDI (Semantic Shader Wiring S0)
// ============================================================================
//
// Program: ExposureTonemapSystem
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
namespace ExposureTonemapSystem {

// Per-binding access mode, from SPIR-V decorations (storage kinds)
// or the descriptor kind's inherent read-only nature. Feeds the
// derived hazard/sync sets (semantic-wiring S3).
enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

/**
 * @brief meterBlock
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xb0361b56ddae4a93 (for runtime discovery)
 */
struct meterBlock {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xb0361b56ddae4a93ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief LightingConfigSSBOBlock
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xd881302cd7c6504 (for runtime discovery)
 */
struct LightingConfigSSBOBlock {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xd881302cd7c6504ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "LightingConfig";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 272;
        static constexpr uint32_t BINDING = 0;
    };

};

namespace Set0 {

    /**
     * @brief sceneRadianceHistoryOutput_current
     * Type: STORAGE_IMAGE
     */
    struct Binding0 {
        static constexpr const char* NAME = "sceneRadianceHistoryOutput_current";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief outputImage_current
     * Type: STORAGE_IMAGE
     */
    struct Binding1 {
        static constexpr const char* NAME = "outputImage_current";
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
        using DataType = meterBlock;
    };

    /**
     * @brief LightingConfigSSBO
     * Type: STORAGE_BUFFER
     */
    struct Binding3 {
        static constexpr const char* NAME = "LightingConfigSSBO";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = LightingConfigSSBOBlock;
    };

} // namespace Set0

// Name-keyed binding aliases (duplicate names skipped)
namespace Bind {
using sceneRadianceHistoryOutput_current = Set0::Binding0;
using outputImage_current = Set0::Binding1;
using meter = Set0::Binding2;
using LightingConfigSSBO = Set0::Binding3;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 16;

    struct _ExtentX {
        static constexpr const char* NAME = "_ExtentX";
        static constexpr uint32_t INDEX = 0;
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct _ExtentY {
        static constexpr const char* NAME = "_ExtentY";
        static constexpr uint32_t INDEX = 1;
        static constexpr uint32_t OFFSET = 4;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Push

struct PushBlock {
    uint32_t _ExtentX;
    uint32_t _ExtentY;
    uint8_t _tail[8];
};

static_assert(offsetof(PushBlock, _ExtentX) == Push::_ExtentX::OFFSET, "PushBlock._ExtentX is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, _ExtentY) == Push::_ExtentY::OFFSET, "PushBlock._ExtentY is not at its SDI OFFSET");
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
    {"sceneRadianceHistoryOutput_current", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"outputImage_current", false, 0, 1, 0, Access::WriteOnly, 0, nullptr},
    {"meter", false, 0, 2, 0, Access::ReadOnly, 0, nullptr},
    {"LightingConfigSSBO", false, 0, 3, 0, Access::ReadOnly, 0, nullptr},
    {"_ExtentX", true, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"_ExtentY", true, 0, 0, 4, Access::ReadOnly, 0, nullptr},
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
    static constexpr const char* PROGRAM_NAME = "ExposureTonemapSystem";
    static constexpr uint32_t NUM_MEMBERS = 6;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace ExposureTonemapSystem
} // namespace ShaderInterface
