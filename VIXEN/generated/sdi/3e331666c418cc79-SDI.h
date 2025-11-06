// ============================================================================
// SPIRV Descriptor Interface (SDI)
// ============================================================================
//
// UUID: 3e331666c418cc79
// Generated: 2025-11-06 13:57:50
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
namespace _3e331666c418cc79 {

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

} // namespace Set0

// ============================================================================
// Push Constants
// ============================================================================

/**
 * @brief PushConstants
 * Size: 16 bytes
 * Alignment: 16 bytes
 * Layout Hash: 0xf87a55e2ef4e337 (for runtime discovery)
 */
struct PushConstants {
    // Phase H: Discovery system layout hash
    static constexpr uint64_t LAYOUT_HASH = 0xf87a55e2ef4e337ULL;

    // Offset: 0 bytes
    float time;
    // Offset: 4 bytes
    int32_t frame;
};

/**
 * @brief pc
 * Offset: 0 bytes
 * Size: 16 bytes
 * Stages: COMPUTE
 */
struct pc {
    static constexpr uint32_t OFFSET = 0;
    static constexpr uint32_t SIZE = 16;
    static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_ALL;
    using DataType = PushConstants;
};

// ============================================================================
// Shader Metadata
// ============================================================================

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "ComputeTest";
    static constexpr const char* INTERFACE_HASH = "3aeddafbb24aadd6";
    static constexpr uint32_t NUM_DESCRIPTOR_SETS = 1;
    static constexpr uint32_t NUM_PUSH_CONSTANTS = 1;
    static constexpr uint32_t NUM_VERTEX_INPUTS = 0;
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
