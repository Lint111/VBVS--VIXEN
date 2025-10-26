#pragma once

#include "Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"
// TEMPORARILY REMOVED - using VulkanShader directly for MVP
// #include <ShaderManagement/ShaderProgram.h>

// Forward declare VulkanShader
class VulkanShader;

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;
using VulkanShaderPtr = VulkanShader*;

/**
 * @brief Shader program descriptor with Vulkan objects (MVP STUB)
 *
/**
 * @brief Shader program descriptor with Vulkan objects (MVP STUB)
 *
 * Simplified version for MVP - will integrate ShaderManagement properly later.
 */
struct ShaderProgramDescriptor {
    uint32_t programId = 0;
    std::string name;
    // MVP: Minimal implementation - expand when ShaderManagement integrated
};

/**
 * @brief Pure constexpr resource configuration for ShaderLibraryNode
 *
 * Inputs: None (programs registered via API)
 *
 * Outputs:
 * - SHADER_PROGRAMS (ShaderProgramDescriptor*[]) - Array of program descriptors
 *
 * No parameters - programs registered via RegisterProgram() API
 */
// Compile-time slot counts (declared early for reuse)
namespace ShaderLibraryNodeCounts {
    static constexpr size_t INPUTS = 1;
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(ShaderLibraryNodeConfig, 
                      ShaderLibraryNodeCounts::INPUTS, 
                      ShaderLibraryNodeCounts::OUTPUTS, 
                      ShaderLibraryNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (1) =====
    // VulkanDevice pointer (contains device, gpu, memory properties, etc.)
    CONSTEXPR_INPUT(VULKAN_DEVICE_IN, VulkanDevicePtr, 0, false);
    
    // ===== OUTPUTS (2) =====
    // Array of shader program descriptors
    CONSTEXPR_OUTPUT(SHADER_PROGRAMS, ShaderProgramDescriptor*, 0, false);
    
    // Device output
    CONSTEXPR_OUTPUT(VULKAN_DEVICE_OUT, VulkanDevicePtr, 1, false);

    ShaderLibraryNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device_in", ResourceLifetime::Persistent, vulkanDeviceDesc);

        // Initialize output descriptors
        // Output: array of pointers to program descriptors
        INIT_OUTPUT_DESC(SHADER_PROGRAMS, "shader_programs",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handles
        );

        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device_out", ResourceLifetime::Persistent, vulkanDeviceDesc);
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == ShaderLibraryNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == ShaderLibraryNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == ShaderLibraryNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE input must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE input is required");

    static_assert(SHADER_PROGRAMS_Slot::index == 0, "SHADER_PROGRAMS must be at index 0");
    static_assert(!SHADER_PROGRAMS_Slot::nullable, "SHADER_PROGRAMS is required");

    static_assert(VULKAN_DEVICE_OUT_Slot::index == 1, "DEVICE_OUT must be at index 1");
    static_assert(!VULKAN_DEVICE_OUT_Slot::nullable, "DEVICE_OUT is required");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<SHADER_PROGRAMS_Slot::Type, ShaderProgramDescriptor*>);
    static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevicePtr>);
};

// Global compile-time validations
static_assert(ShaderLibraryNodeConfig::INPUT_COUNT == ShaderLibraryNodeCounts::INPUTS);
static_assert(ShaderLibraryNodeConfig::OUTPUT_COUNT == ShaderLibraryNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph