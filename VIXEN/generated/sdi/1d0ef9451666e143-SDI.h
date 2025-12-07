// ============================================================================
// SPIRV Descriptor Interface (SDI)
// ============================================================================
//
// UUID: 1d0ef9451666e143
// Generated: 2025-12-07 21:34:01
//
// This file provides compile-time type-safe access to shader resources.
// It is automatically generated from SPIRV reflection data.
//
// DO NOT MODIFY THIS FILE MANUALLY - it will be regenerated.
//
// ============================================================================

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace ShaderInterface {
namespace _1d0ef9451666e143 {

// ============================================================================
// Shader Struct Definitions
// ============================================================================

/**
 * @brief OctreeConfigUBO
 * Size: 192 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x4fee79a93d9682ba (for runtime discovery)
 */
struct OctreeConfigUBO {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x4fee79a93d9682baULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "int32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };
    struct pc_1 {
        static constexpr const char* TYPE = "int32_t";
        static constexpr uint32_t OFFSET = 4;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 1;
    };
    struct pc_2 {
        static constexpr const char* TYPE = "int32_t";
        static constexpr uint32_t OFFSET = 8;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 2;
    };
    struct pc_3 {
        static constexpr const char* TYPE = "int32_t";
        static constexpr uint32_t OFFSET = 12;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 3;
    };
    struct pc_4 {
        static constexpr const char* TYPE = "int32_t";
        static constexpr uint32_t OFFSET = 16;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 4;
    };
    struct pc_5 {
        static constexpr const char* TYPE = "int32_t";
        static constexpr uint32_t OFFSET = 20;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 5;
    };
    struct pc_6 {
        static constexpr const char* TYPE = "int32_t";
        static constexpr uint32_t OFFSET = 24;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 6;
    };
    struct pc_7 {
        static constexpr const char* TYPE = "int32_t";
        static constexpr uint32_t OFFSET = 28;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 7;
    };
    struct pc_8 {
        static constexpr const char* TYPE = "glm::dvec3";
        static constexpr uint32_t OFFSET = 32;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t BINDING = 8;
    };
    struct pc_9 {
        static constexpr const char* TYPE = "float";
        static constexpr uint32_t OFFSET = 44;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 9;
    };
    struct pc_10 {
        static constexpr const char* TYPE = "glm::dvec3";
        static constexpr uint32_t OFFSET = 48;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t BINDING = 10;
    };
    struct pc_11 {
        static constexpr const char* TYPE = "float";
        static constexpr uint32_t OFFSET = 60;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 11;
    };
    struct pc_12 {
        static constexpr const char* TYPE = "glm::mat4";
        static constexpr uint32_t OFFSET = 64;
        static constexpr uint32_t SIZE = 64;
        static constexpr uint32_t BINDING = 12;
    };
    struct pc_13 {
        static constexpr const char* TYPE = "glm::mat4";
        static constexpr uint32_t OFFSET = 128;
        static constexpr uint32_t SIZE = 64;
        static constexpr uint32_t BINDING = 13;
    };

};

/**
 * @brief MaterialIdBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x36671aadcdb33fe2 (for runtime discovery)
 */
struct MaterialIdBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x36671aadcdb33fe2ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief CompressedColorBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x12076e44dfa527aa (for runtime discovery)
 */
struct CompressedColorBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x12076e44dfa527aaULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief CompressedNormalBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x6b97afbc1e44419a (for runtime discovery)
 */
struct CompressedNormalBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x6b97afbc1e44419aULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief AABBBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xa8c3b1a1b95734c6 (for runtime discovery)
 */
struct AABBBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xa8c3b1a1b95734c6ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "AABB";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 24;
        static constexpr uint32_t BINDING = 0;
    };

};

// ============================================================================
// Descriptor Bindings
// ============================================================================

namespace Set0 {

    /**
     * @brief outputImage
     * Type: STORAGE_IMAGE
     * Stages: ALL
     * Count: 1
     */
    struct Binding0 {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_ALL;
    };

    /**
     * @brief topLevelAS
     * Type: ACCELERATION_STRUCTURE_KHR
     * Stages: ALL
     * Count: 1
     */
    struct Binding1 {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_ALL;
    };

    /**
     * @brief octreeConfig
     * Type: UNIFORM_BUFFER
     * Stages: ALL
     * Count: 1
     */
    struct Binding5 {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 5;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_ALL;
        using DataType = OctreeConfigUBO;
    };

    /**
     * @brief materialIdBuffer
     * Type: STORAGE_BUFFER
     * Stages: ALL
     * Count: 1
     */
    struct Binding3 {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_ALL;
        using DataType = MaterialIdBuffer;
    };

    /**
     * @brief 
     * Type: STORAGE_BUFFER
     * Stages: ALL
     * Count: 1
     */
    struct Binding6 {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 6;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_ALL;
        using DataType = CompressedColorBuffer;
    };

    /**
     * @brief 
     * Type: STORAGE_BUFFER
     * Stages: ALL
     * Count: 1
     */
    struct Binding7 {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 7;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_ALL;
        using DataType = CompressedNormalBuffer;
    };

    /**
     * @brief aabbBuffer
     * Type: STORAGE_BUFFER
     * Stages: ALL
     * Count: 1
     */
    struct Binding2 {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_ALL;
        using DataType = AABBBuffer;
    };

} // namespace Set0

// ============================================================================
// Push Constants
// ============================================================================

/**
 * @brief PushConstants
 * Size: 64 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x9beba6e385f4b387 (for runtime discovery)
 */
struct PushConstants {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x9beba6e385f4b387ULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "glm::dvec3";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t BINDING = 0;
    };
    struct pc_1 {
        static constexpr const char* TYPE = "float";
        static constexpr uint32_t OFFSET = 12;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 1;
    };
    struct pc_2 {
        static constexpr const char* TYPE = "glm::dvec3";
        static constexpr uint32_t OFFSET = 16;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t BINDING = 2;
    };
    struct pc_3 {
        static constexpr const char* TYPE = "float";
        static constexpr uint32_t OFFSET = 28;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 3;
    };
    struct pc_4 {
        static constexpr const char* TYPE = "glm::dvec3";
        static constexpr uint32_t OFFSET = 32;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t BINDING = 4;
    };
    struct pc_5 {
        static constexpr const char* TYPE = "float";
        static constexpr uint32_t OFFSET = 44;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 5;
    };
    struct pc_6 {
        static constexpr const char* TYPE = "glm::dvec3";
        static constexpr uint32_t OFFSET = 48;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t BINDING = 6;
    };
    struct pc_7 {
        static constexpr const char* TYPE = "int32_t";
        static constexpr uint32_t OFFSET = 60;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 7;
    };

};

/**
 * @brief pc
 * Offset: 0 bytes
 * Size: 64 bytes
 * Stages: ALL
 */
struct pc {
    static constexpr uint32_t OFFSET = 0;
    static constexpr uint32_t SIZE = 64;
    static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_ALL;
    using DataType = PushConstants;
};

// ============================================================================
// Shader Metadata
// ============================================================================

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "VoxelRT";
    static constexpr const char* INTERFACE_HASH = "a77d8c23c42d8a65";
    static constexpr uint32_t NUM_DESCRIPTOR_SETS = 1;
    static constexpr uint32_t NUM_PUSH_CONSTANTS = 1;
    static constexpr uint32_t NUM_VERTEX_INPUTS = 0;
};

// ============================================================================
// Interface VixenHash Validation
// ============================================================================

/**
 * @brief Validate that runtime shader matches this interface
 *
 * @param runtimeHash VixenHash computed from runtime SPIRV bytecode
 * @return True if interface matches
 */
inline bool ValidateInterfaceHash(const char* runtimeHash) {
    return std::string(runtimeHash) == Metadata::INTERFACE_HASH;
}

} // namespace
} // namespace ShaderInterface
