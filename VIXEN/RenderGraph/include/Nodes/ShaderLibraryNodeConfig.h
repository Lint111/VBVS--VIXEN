#pragma once

#include "Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"
// TEMPORARILY REMOVED - using VulkanShader directly for MVP
// #include <ShaderManagement/ShaderProgram.h>

// Forward declare VulkanShader
class VulkanShader;

// Forward declare ShaderDataBundle
namespace ShaderManagement {
    struct ShaderDataBundle;
}

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;
using VulkanShaderPtr = VulkanShader*;
using ShaderDataBundlePtr = std::shared_ptr<ShaderManagement::ShaderDataBundle>;

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
    static constexpr size_t OUTPUTS = 2;  // Phase 2: device_out, shader_data_bundle
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(ShaderLibraryNodeConfig, 
                      ShaderLibraryNodeCounts::INPUTS, 
                      ShaderLibraryNodeCounts::OUTPUTS, 
                      ShaderLibraryNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (1) =====
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevicePtr, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (2) =====
    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevicePtr, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(SHADER_DATA_BUNDLE, ShaderDataBundlePtr, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    ShaderLibraryNodeConfig() {
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device_in", ResourceLifetime::Persistent, vulkanDeviceDesc);

        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device_out", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor shaderDataBundleDesc{"ShaderDataBundle*"};
        INIT_OUTPUT_DESC(SHADER_DATA_BUNDLE, "shader_data_bundle", ResourceLifetime::Persistent, shaderDataBundleDesc);
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == ShaderLibraryNodeCounts::INPUTS);
    static_assert(OUTPUT_COUNT == ShaderLibraryNodeCounts::OUTPUTS);
    static_assert(ARRAY_MODE == ShaderLibraryNodeCounts::ARRAY_MODE);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0);
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable);

    static_assert(VULKAN_DEVICE_OUT_Slot::index == 0);
    static_assert(!VULKAN_DEVICE_OUT_Slot::nullable);

    static_assert(SHADER_DATA_BUNDLE_Slot::index == 1);
    static_assert(!SHADER_DATA_BUNDLE_Slot::nullable);

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<SHADER_DATA_BUNDLE_Slot::Type, ShaderDataBundlePtr>);
};

// Global compile-time validations
static_assert(ShaderLibraryNodeConfig::INPUT_COUNT == ShaderLibraryNodeCounts::INPUTS);
static_assert(ShaderLibraryNodeConfig::OUTPUT_COUNT == ShaderLibraryNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph