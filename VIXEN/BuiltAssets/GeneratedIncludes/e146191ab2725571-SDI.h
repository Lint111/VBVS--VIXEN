// ============================================================================
// SPIRV Descriptor Interface (SDI)
// ============================================================================
//
// UUID: e146191ab2725571
// Generated: 2025-11-09 17:10:44
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
namespace e146191ab2725571 {

// ============================================================================
// Shader Struct Definitions
// ============================================================================

/**
 * @brief CameraData
 * Size: 144 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xb42022690f7fc1dd (for runtime discovery)
 */
struct CameraData {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xb42022690f7fc1ddULL;

    // Offset: 0 bytes
    glm::mat4 invProjection;
    // Offset: 64 bytes
    glm::mat4 invView;
    // Offset: 128 bytes
    glm::dvec3 cameraPos;
    // Offset: 140 bytes
    int32_t gridResolution;
};

/**
 * @brief OctreeNodesBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x2db79363c03e9b68 (for runtime discovery)
 */
struct OctreeNodesBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x2db79363c03e9b68ULL;

    // Offset: 0 bytes
    uint32_t data[];
};

/**
 * @brief VoxelBricksBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0x31ac6aa8883c945 (for runtime discovery)
 */
struct VoxelBricksBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0x31ac6aa8883c945ULL;

    // Offset: 0 bytes
    uint32_t data[];
};

/**
 * @brief MaterialPaletteBuffer
 * Size: 0 bytes
 * Alignment: 16 bytes
 * Layout VixenHash: 0xbdb98922562d2e1b (for runtime discovery)
 */
struct MaterialPaletteBuffer {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xbdb98922562d2e1bULL;

    // Offset: 0 bytes
    uint32_t data[];
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
    struct outputImage {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr VkDescriptorType TYPE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_COMPUTE_BIT;
    };

    /**
     * @brief camera
     * Type: UNIFORM_BUFFER
     * Stages: COMPUTE
     * Count: 1
     */
    struct camera {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType TYPE = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_COMPUTE_BIT;
        using DataType = CameraData;
    };

    /**
     * @brief octreeNodes
     * Type: STORAGE_BUFFER
     * Stages: COMPUTE
     * Count: 1
     */
    struct octreeNodes {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr VkDescriptorType TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_COMPUTE_BIT;
        using DataType = OctreeNodesBuffer;
    };

    /**
     * @brief voxelBricks
     * Type: STORAGE_BUFFER
     * Stages: COMPUTE
     * Count: 1
     */
    struct voxelBricks {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr VkDescriptorType TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_COMPUTE_BIT;
        using DataType = VoxelBricksBuffer;
    };

    /**
     * @brief materialPalette
     * Type: STORAGE_BUFFER
     * Stages: COMPUTE
     * Count: 1
     */
    struct materialPalette {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 4;
        static constexpr VkDescriptorType TYPE = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_COMPUTE_BIT;
        using DataType = MaterialPaletteBuffer;
    };

} // namespace Set0

// ============================================================================
// Shader Metadata
// ============================================================================

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "VoxelRayMarch";
    static constexpr const char* INTERFACE_HASH = "ecbf513723484928";
    static constexpr uint32_t NUM_DESCRIPTOR_SETS = 1;
    static constexpr uint32_t NUM_PUSH_CONSTANTS = 0;
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
