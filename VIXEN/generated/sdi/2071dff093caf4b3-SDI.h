// ============================================================================
// SPIRV Descriptor Interface (SDI)
// ============================================================================
//
// UUID: 2071dff093caf4b3
// Generated: 2025-11-01 14:29:42
//
// This file provides compile-time type-safe access to shader resources.
// It is automatically generated from SPIRV reflection data.
//
// DO NOT MODIFY THIS FILE MANUALLY - it will be regenerated.
//
// ============================================================================

#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace ShaderInterface {
namespace _2071dff093caf4b3 {

// ============================================================================
// Shader Struct Definitions
// ============================================================================

/**
 * @brief bufferVals
 * Size: 64 bytes
 * Alignment: 16 bytes
 */
struct bufferVals {
    // Offset: 0 bytes
    glm::mat4 mvp;
};

// ============================================================================
// Descriptor Bindings
// ============================================================================

namespace Set0 {

    /**
     * @brief myBufferVals
     * Type: UNIFORM_BUFFER
     * Stages: VERTEX
     * Count: 1
     */
    struct myBufferVals {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr VkDescriptorType TYPE = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_VERTEX_BIT;
        using DataType = bufferVals;
    };

    /**
     * @brief tex
     * Type: COMBINED_IMAGE_SAMPLER
     * Stages: FRAGMENT
     * Count: 1
     */
    struct tex {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType TYPE = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_FRAGMENT_BIT;
    };

} // namespace Set0

// ============================================================================
// Push Constants
// ============================================================================

/**
 * @brief colorBlock
 * Size: 16 bytes
 * Alignment: 16 bytes
 */
struct colorBlock {
    // Offset: 0 bytes
    int32_t constColorIndex;
    // Offset: 4 bytes
    float mixerValue;
};

/**
 * @brief pushConstantsColorBlock
 * Offset: 0 bytes
 * Size: 16 bytes
 * Stages: FRAGMENT
 */
struct pushConstantsColorBlock {
    static constexpr uint32_t OFFSET = 0;
    static constexpr uint32_t SIZE = 16;
    static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_ALL;
    using DataType = colorBlock;
};

// ============================================================================
// Vertex Inputs
// ============================================================================

namespace VertexInput {

    /**
     * @brief inUV
     * Location: 1
     * Type: ivec2
     */
    struct inUV {
        static constexpr uint32_t LOCATION = 1;
        using DataType = glm::dvec2;
    };

    /**
     * @brief pos
     * Location: 0
     * Type: ivec4
     */
    struct pos {
        static constexpr uint32_t LOCATION = 0;
        using DataType = glm::dvec4;
    };

} // namespace VertexInput

// ============================================================================
// Shader Metadata
// ============================================================================

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "Draw_Shader";
    static constexpr const char* INTERFACE_HASH = "6fd8a4ccf2095fa8";
    static constexpr uint32_t NUM_DESCRIPTOR_SETS = 1;
    static constexpr uint32_t NUM_PUSH_CONSTANTS = 1;
    static constexpr uint32_t NUM_VERTEX_INPUTS = 2;
};

// ============================================================================
// Interface Hash Validation
// ============================================================================

/**
 * @brief Validate that runtime shader matches this interface
 *
 * @param runtimeHash Hash computed from runtime SPIRV bytecode
 * @return True if interface matches
 */
inline bool ValidateInterfaceHash(const char* runtimeHash) {
    return std::string(runtimeHash) == Metadata::INTERFACE_HASH;
}

} // namespace
} // namespace ShaderInterface
