#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/ResourceVariant.h"

namespace Vixen::RenderGraph {

// ============================================================================
// SLOT COUNTS
// ============================================================================

namespace ComputeDispatchNodeCounts {
    static constexpr size_t INPUTS = 6;
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

// ============================================================================
// COMPUTE DISPATCH NODE CONFIG
// ============================================================================

/**
 * @brief Generic compute shader dispatch node
 *
 * Records command buffer with vkCmdDispatch for ANY compute shader.
 * Separates dispatch logic from pipeline creation (ComputePipelineNode).
 *
 * Phase G.3: Generic compute dispatcher for research flexibility
 *
 * Example usage:
 * ```
 * ShaderLibraryNode -> ComputePipelineNode -> ComputeDispatchNode -> Present
 * ```
 */
CONSTEXPR_NODE_CONFIG(ComputeDispatchNodeConfig,
                      ComputeDispatchNodeCounts::INPUTS,
                      ComputeDispatchNodeCounts::OUTPUTS,
                      ComputeDispatchNodeCounts::ARRAY_MODE) {

    // ===== PARAMETER NAMES =====
    static constexpr const char* DISPATCH_X = "dispatchX";
    static constexpr const char* DISPATCH_Y = "dispatchY";
    static constexpr const char* DISPATCH_Z = "dispatchZ";
    static constexpr const char* PUSH_CONSTANT_SIZE = "pushConstantSize";
    static constexpr const char* DESCRIPTOR_SET_COUNT = "descriptorSetCount";

    // ===== INPUTS (6) =====

    /**
     * @brief Vulkan device for command buffer allocation
     */
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevicePtr, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Command pool for command buffer allocation
     */
    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Compute pipeline to bind (from ComputePipelineNode)
     */
    INPUT_SLOT(COMPUTE_PIPELINE, VkPipeline, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Pipeline layout for descriptor sets and push constants
     */
    INPUT_SLOT(PIPELINE_LAYOUT, VkPipelineLayout, 3,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Descriptor sets (array input for multiple sets)
     */
    INPUT_SLOT(DESCRIPTOR_SETS, DescriptorSetVector, 4,
        SlotNullability::Optional,  // Not all shaders use descriptors
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Push constant data (raw byte array)
     */
    INPUT_SLOT(PUSH_CONSTANTS, VkBuffer, 5,
        SlotNullability::Optional,  // Not all shaders use push constants
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (2) =====

    /**
     * @brief Recorded command buffer with vkCmdDispatch
     */
    OUTPUT_SLOT(COMMAND_BUFFER, VkCommandBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    /**
     * @brief Pass-through device for downstream nodes
     */
    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevicePtr, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ===== COMPILE-TIME VALIDATIONS =====

    static constexpr bool ValidateDispatchDimensions(uint32_t x, uint32_t y, uint32_t z) {
        // Max dispatch size varies by GPU, but 65535 is safe minimum (Vulkan spec)
        return x > 0 && y > 0 && z > 0 && x <= 65535 && y <= 65535 && z <= 65535;
    }

    static constexpr bool ValidateDescriptorSetCount(uint32_t count) {
        // Vulkan spec guarantees at least 4 descriptor sets
        return count <= 4;
    }
};

} // namespace Vixen::RenderGraph
