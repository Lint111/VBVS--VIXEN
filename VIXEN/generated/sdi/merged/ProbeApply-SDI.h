// ============================================================================
// Feature-Tagged Merged SDI (Semantic Shader Wiring S0)
// ============================================================================
//
// Program: ProbeApply
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
namespace ProbeApply {

// Per-binding access mode, from SPIR-V decorations (storage kinds)
// or the descriptor kind's inherent read-only nature. Feeds the
// derived hazard/sync sets (semantic-wiring S3).
enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

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
 * @brief ShadowRayResultBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x304773c7c2ba3140 (for runtime discovery)
 */
struct ShadowRayResultBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x304773c7c2ba3140ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief ProbeRayPayloadBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x340b03932f75e9d8 (for runtime discovery)
 */
struct ProbeRayPayloadBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x340b03932f75e9d8ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "ProbeRayPayload";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 16;
        static constexpr uint32_t BINDING = 0;
    };

};

namespace Set0 {

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
        static constexpr Access ACCESS = Access::ReadOnly;
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
        static constexpr Access ACCESS = Access::ReadWrite;
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
        static constexpr Access ACCESS = Access::ReadWrite;
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
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = DDGILeakGateDebugSSBO;
    };

    /**
     * @brief ShadowRayResultBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding38 {
        static constexpr const char* NAME = "ShadowRayResultBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 38;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = ShadowRayResultBuffer;
    };

    /**
     * @brief ProbeRayPayloadBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding39 {
        static constexpr const char* NAME = "ProbeRayPayloadBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 39;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = ProbeRayPayloadBuffer;
    };

} // namespace Set0

// Name-keyed binding aliases (duplicate names skipped)
namespace Bind {
using ProbeGridConfigSSBO = Set0::Binding28;
using probeIrradianceAtlas = Set0::Binding29;
using probeVisibilityAtlas = Set0::Binding30;
using DDGILeakGateDebugSSBO = Set0::Binding31;
using ShadowRayResultBuffer = Set0::Binding38;
using ProbeRayPayloadBuffer = Set0::Binding39;
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
    {"ProbeGridConfigSSBO", false, 0, 28, 0, Access::ReadOnly, 0, nullptr},
    {"probeIrradianceAtlas", false, 0, 29, 0, Access::ReadWrite, 0, nullptr},
    {"probeVisibilityAtlas", false, 0, 30, 0, Access::ReadWrite, 0, nullptr},
    {"DDGILeakGateDebugSSBO", false, 0, 31, 0, Access::ReadWrite, 0, nullptr},
    {"ShadowRayResultBuffer", false, 0, 38, 0, Access::ReadOnly, 0, nullptr},
    {"ProbeRayPayloadBuffer", false, 0, 39, 0, Access::ReadOnly, 0, nullptr},
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
    static constexpr const char* PROGRAM_NAME = "ProbeApply";
    static constexpr uint32_t NUM_MEMBERS = 6;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace ProbeApply
} // namespace ShaderInterface
