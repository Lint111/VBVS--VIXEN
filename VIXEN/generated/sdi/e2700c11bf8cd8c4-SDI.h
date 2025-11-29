// ============================================================================
// SPIRV Descriptor Interface (SDI)
// ============================================================================
//
// UUID: e2700c11bf8cd8c4
// Generated: 2025-11-29 18:53:38
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
namespace e2700c11bf8cd8c4 {

// ============================================================================
// Shader Struct Definitions
// ============================================================================

/**
 * @brief ESVOBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xb9224cc8281c62e (for runtime discovery)
 */
struct ESVOBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xb9224cc8281c62eULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief BrickBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x541cbd85094c043f (for runtime discovery)
 */
struct BrickBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x541cbd85094c043fULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "uint32_t";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief MaterialBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xecdc8a9c9d1897d (for runtime discovery)
 */
struct MaterialBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xecdc8a9c9d1897dULL;

    // Member metadata structs
    struct pc_0 {
        static constexpr const char* TYPE = "Material";
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 32;
        static constexpr uint32_t BINDING = 0;
    };

};

/**
 * @brief DebugCaptureBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x959cbc17adac4206 (for runtime discovery)
 */
struct DebugCaptureBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x959cbc17adac4206ULL;

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
        static constexpr const char* TYPE = "DebugRaySample";
        static constexpr uint32_t OFFSET = 16;
        static constexpr uint32_t SIZE = 112;
        static constexpr uint32_t BINDING = 2;
    };

};

// ============================================================================
// Descriptor Bindings
// ============================================================================

namespace Set0 {

    /**
     * @brief outputImage
     * Type: STORAGE_IMAGE
     * Stages: COMPUTE
     * Count: 1
     */
    struct Binding0 {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_COMPUTE_BIT;
    };

    /**
     * @brief 
     * Type: STORAGE_BUFFER
     * Stages: COMPUTE
     * Count: 1
     */
    struct Binding1 {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_COMPUTE_BIT;
        using DataType = ESVOBuffer;
    };

    /**
     * @brief 
     * Type: STORAGE_BUFFER
     * Stages: COMPUTE
     * Count: 1
     */
    struct Binding2 {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_COMPUTE_BIT;
        using DataType = BrickBuffer;
    };

    /**
     * @brief 
     * Type: STORAGE_BUFFER
     * Stages: COMPUTE
     * Count: 1
     */
    struct Binding3 {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_COMPUTE_BIT;
        using DataType = MaterialBuffer;
    };

    /**
     * @brief 
     * Type: STORAGE_BUFFER
     * Stages: COMPUTE
     * Count: 1
     */
    struct Binding4 {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 4;
        static constexpr VkDescriptorType DESCRIPTOR_TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_COMPUTE_BIT;
        using DataType = DebugCaptureBuffer;
    };

} // namespace Set0

// ============================================================================
// Push Constants
// ============================================================================

/**
 * @brief PushConstants
 * Size: 64 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x796faee378fa3795 (for runtime discovery)
 */
struct PushConstants {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x796faee378fa3795ULL;

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
        static constexpr const char* TYPE = "float";
        static constexpr uint32_t OFFSET = 60;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t BINDING = 7;
    };

};

/**
 * @brief pc
 * Offset: 0 bytes
 * Size: 64 bytes
 * Stages: COMPUTE
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
    static constexpr const char* PROGRAM_NAME = "VoxelRayMarch";
    static constexpr const char* INTERFACE_HASH = "fa0251c4c067b647";
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
