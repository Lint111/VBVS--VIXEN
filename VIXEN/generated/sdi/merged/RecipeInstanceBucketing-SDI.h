// ============================================================================
// Feature-Tagged Merged SDI (Semantic Shader Wiring S0)
// ============================================================================
//
// Program: RecipeInstanceBucketing
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
namespace RecipeInstanceBucketing {

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
 * @brief RecipeBoundSphereBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xf545d1b65035b0c (for runtime discovery)
 */
struct RecipeBoundSphereBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xf545d1b65035b0cULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "RecipeBoundSphere";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 32;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief BucketCountBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x703a80e433882bf8 (for runtime discovery)
 */
struct BucketCountBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x703a80e433882bf8ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief BucketIndicesBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xd23e6a432d099a3b (for runtime discovery)
 */
struct BucketIndicesBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xd23e6a432d099a3bULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief BucketCoverageMinXBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x9ac7955a80189c35 (for runtime discovery)
 */
struct BucketCoverageMinXBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x9ac7955a80189c35ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief BucketCoverageMinYBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x2801dadacb20ed41 (for runtime discovery)
 */
struct BucketCoverageMinYBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x2801dadacb20ed41ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief BucketCoverageMaxXBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x69d5ab29f0b0bf9d (for runtime discovery)
 */
struct BucketCoverageMaxXBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x69d5ab29f0b0bf9dULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief BucketCoverageMaxYBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xe7ac016fc8190895 (for runtime discovery)
 */
struct BucketCoverageMaxYBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xe7ac016fc8190895ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief BucketIndirectCommandBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xc63b714aa73b6450 (for runtime discovery)
 */
struct BucketIndirectCommandBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xc63b714aa73b6450ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief PrecisionBucketCountBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xefb98332a91fc47e (for runtime discovery)
 */
struct PrecisionBucketCountBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xefb98332a91fc47eULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief PrecisionBucketIndicesBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xb49e2cddcd91b289 (for runtime discovery)
 */
struct PrecisionBucketIndicesBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xb49e2cddcd91b289ULL;

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
     * @brief RecipeBoundSphereBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding1 {
        static constexpr const char* NAME = "RecipeBoundSphereBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = RecipeBoundSphereBuffer;
    };

    /**
     * @brief BucketCountBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding2 {
        static constexpr const char* NAME = "BucketCountBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = BucketCountBuffer;
    };

    /**
     * @brief BucketIndicesBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding3 {
        static constexpr const char* NAME = "BucketIndicesBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = BucketIndicesBuffer;
    };

    /**
     * @brief BucketCoverageMinXBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding4 {
        static constexpr const char* NAME = "BucketCoverageMinXBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 4;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = BucketCoverageMinXBuffer;
    };

    /**
     * @brief BucketCoverageMinYBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding5 {
        static constexpr const char* NAME = "BucketCoverageMinYBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 5;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = BucketCoverageMinYBuffer;
    };

    /**
     * @brief BucketCoverageMaxXBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding6 {
        static constexpr const char* NAME = "BucketCoverageMaxXBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 6;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = BucketCoverageMaxXBuffer;
    };

    /**
     * @brief BucketCoverageMaxYBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding7 {
        static constexpr const char* NAME = "BucketCoverageMaxYBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 7;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = BucketCoverageMaxYBuffer;
    };

    /**
     * @brief BucketIndirectCommandBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding8 {
        static constexpr const char* NAME = "BucketIndirectCommandBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 8;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = BucketIndirectCommandBuffer;
    };

    /**
     * @brief PrecisionBucketCountBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding9 {
        static constexpr const char* NAME = "PrecisionBucketCountBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 9;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = PrecisionBucketCountBuffer;
    };

    /**
     * @brief PrecisionBucketIndicesBuffer
     * Type: STORAGE_BUFFER
     */
    struct Binding10 {
        static constexpr const char* NAME = "PrecisionBucketIndicesBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 10;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr uint32_t FEATURE_COUNT = 0;
        using DataType = PrecisionBucketIndicesBuffer;
    };

} // namespace Set0

// Name-keyed binding aliases (duplicate names skipped)
namespace Bind {
using BodyInstanceBuffer = Set0::Binding0;
using RecipeBoundSphereBuffer = Set0::Binding1;
using BucketCountBuffer = Set0::Binding2;
using BucketIndicesBuffer = Set0::Binding3;
using BucketCoverageMinXBuffer = Set0::Binding4;
using BucketCoverageMinYBuffer = Set0::Binding5;
using BucketCoverageMaxXBuffer = Set0::Binding6;
using BucketCoverageMaxYBuffer = Set0::Binding7;
using BucketIndirectCommandBuffer = Set0::Binding8;
using PrecisionBucketCountBuffer = Set0::Binding9;
using PrecisionBucketIndicesBuffer = Set0::Binding10;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 112;

    struct viewProj {
        static constexpr const char* NAME = "viewProj";
        static constexpr uint32_t INDEX = 0;
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 64;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct instanceCount {
        static constexpr const char* NAME = "instanceCount";
        static constexpr uint32_t INDEX = 1;
        static constexpr uint32_t OFFSET = 64;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct maxBuckets {
        static constexpr const char* NAME = "maxBuckets";
        static constexpr uint32_t INDEX = 2;
        static constexpr uint32_t OFFSET = 68;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct maxMembersPerBucket {
        static constexpr const char* NAME = "maxMembersPerBucket";
        static constexpr uint32_t INDEX = 3;
        static constexpr uint32_t OFFSET = 72;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct screenWidth {
        static constexpr const char* NAME = "screenWidth";
        static constexpr uint32_t INDEX = 4;
        static constexpr uint32_t OFFSET = 76;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct screenHeight {
        static constexpr const char* NAME = "screenHeight";
        static constexpr uint32_t INDEX = 5;
        static constexpr uint32_t OFFSET = 80;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct mode {
        static constexpr const char* NAME = "mode";
        static constexpr uint32_t INDEX = 6;
        static constexpr uint32_t OFFSET = 84;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct raySizeCoef {
        static constexpr const char* NAME = "raySizeCoef";
        static constexpr uint32_t INDEX = 7;
        static constexpr uint32_t OFFSET = 88;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct raySizeBias {
        static constexpr const char* NAME = "raySizeBias";
        static constexpr uint32_t INDEX = 8;
        static constexpr uint32_t OFFSET = 92;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct cameraPos {
        static constexpr const char* NAME = "cameraPos";
        static constexpr uint32_t INDEX = 9;
        static constexpr uint32_t OFFSET = 96;
        static constexpr uint32_t SIZE = 12;
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
    {"RecipeBoundSphereBuffer", false, 0, 1, 0, 0, nullptr},
    {"BucketCountBuffer", false, 0, 2, 0, 0, nullptr},
    {"BucketIndicesBuffer", false, 0, 3, 0, 0, nullptr},
    {"BucketCoverageMinXBuffer", false, 0, 4, 0, 0, nullptr},
    {"BucketCoverageMinYBuffer", false, 0, 5, 0, 0, nullptr},
    {"BucketCoverageMaxXBuffer", false, 0, 6, 0, 0, nullptr},
    {"BucketCoverageMaxYBuffer", false, 0, 7, 0, 0, nullptr},
    {"BucketIndirectCommandBuffer", false, 0, 8, 0, 0, nullptr},
    {"PrecisionBucketCountBuffer", false, 0, 9, 0, 0, nullptr},
    {"PrecisionBucketIndicesBuffer", false, 0, 10, 0, 0, nullptr},
    {"viewProj", true, 0, 0, 0, 0, nullptr},
    {"instanceCount", true, 0, 0, 64, 0, nullptr},
    {"maxBuckets", true, 0, 0, 68, 0, nullptr},
    {"maxMembersPerBucket", true, 0, 0, 72, 0, nullptr},
    {"screenWidth", true, 0, 0, 76, 0, nullptr},
    {"screenHeight", true, 0, 0, 80, 0, nullptr},
    {"mode", true, 0, 0, 84, 0, nullptr},
    {"raySizeCoef", true, 0, 0, 88, 0, nullptr},
    {"raySizeBias", true, 0, 0, 92, 0, nullptr},
    {"cameraPos", true, 0, 0, 96, 0, nullptr},
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
    static constexpr const char* PROGRAM_NAME = "RecipeInstanceBucketing";
    static constexpr uint32_t NUM_MEMBERS = 21;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace RecipeInstanceBucketing
} // namespace ShaderInterface
