// ============================================================================
// Feature-Tagged Merged SDI (Semantic Shader Wiring S0)
// ============================================================================
//
// Program: ReconstructionDepth
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
namespace ReconstructionDepth {

// Per-binding access mode, from SPIR-V decorations (storage kinds)
// or the descriptor kind's inherent read-only nature. Feeds the
// derived hazard/sync sets (semantic-wiring S3).
enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

/**
 * @brief HitRecordBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xd7b761d5431b4949 (for runtime discovery)
 */
struct HitRecordBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xd7b761d5431b4949ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "HitRecord";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 64;
        static constexpr uint32_t BINDING = 0;
    };

};

namespace Set0 {

    /**
     * @brief HitRecordBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding0 {
        static constexpr const char* NAME = "HitRecordBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = HitRecordBuffer;
    };

    /**
     * @brief viewDepthImage
     * Type: STORAGE_IMAGE
     */
    struct Binding1 {
        static constexpr const char* NAME = "viewDepthImage";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::WriteOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    /**
     * @brief normalizedDepthImage
     * Type: STORAGE_IMAGE
     */
    struct Binding2 {
        static constexpr const char* NAME = "normalizedDepthImage";
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
using HitRecordBuffer = Set0::Binding0;
using viewDepthImage = Set0::Binding1;
using normalizedDepthImage = Set0::Binding2;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 112;

    struct invProjection {
        static constexpr const char* NAME = "invProjection";
        static constexpr uint32_t INDEX = 0;
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 64;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct cameraPos {
        static constexpr const char* NAME = "cameraPos";
        static constexpr uint32_t INDEX = 1;
        static constexpr uint32_t OFFSET = 64;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct cameraDir {
        static constexpr const char* NAME = "cameraDir";
        static constexpr uint32_t INDEX = 2;
        static constexpr uint32_t OFFSET = 80;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct depthEncoding {
        static constexpr const char* NAME = "depthEncoding";
        static constexpr uint32_t INDEX = 3;
        static constexpr uint32_t OFFSET = 96;
        static constexpr uint32_t SIZE = 16;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Push

struct PushBlock {
    uint8_t invProjection[64];
    glm::vec3 cameraPos;
    uint8_t _pad76[4];
    glm::vec3 cameraDir;
    uint8_t _pad92[4];
    glm::vec4 depthEncoding;
};

static_assert(offsetof(PushBlock, invProjection) == Push::invProjection::OFFSET, "PushBlock.invProjection is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, cameraPos) == Push::cameraPos::OFFSET, "PushBlock.cameraPos is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, cameraDir) == Push::cameraDir::OFFSET, "PushBlock.cameraDir is not at its SDI OFFSET");
static_assert(offsetof(PushBlock, depthEncoding) == Push::depthEncoding::OFFSET, "PushBlock.depthEncoding is not at its SDI OFFSET");
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
    {"HitRecordBuffer", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"viewDepthImage", false, 0, 1, 0, Access::WriteOnly, 0, nullptr},
    {"normalizedDepthImage", false, 0, 2, 0, Access::WriteOnly, 0, nullptr},
    {"invProjection", true, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"cameraPos", true, 0, 0, 64, Access::ReadOnly, 0, nullptr},
    {"cameraDir", true, 0, 0, 80, Access::ReadOnly, 0, nullptr},
    {"depthEncoding", true, 0, 0, 96, Access::ReadOnly, 0, nullptr},
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
    static constexpr const char* PROGRAM_NAME = "ReconstructionDepth";
    static constexpr uint32_t NUM_MEMBERS = 7;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace ReconstructionDepth
} // namespace ShaderInterface
