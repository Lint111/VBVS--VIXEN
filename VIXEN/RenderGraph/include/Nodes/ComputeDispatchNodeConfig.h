#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/ResourceVariant.h"

namespace Vixen::RenderGraph {

// ============================================================================
// SLOT COUNTS
// ============================================================================

namespace ComputeDispatchNodeCounts {
    static constexpr size_t INPUTS = 12;  // Added swapchain + sync inputs
    static constexpr size_t OUTPUTS = 3;  // Added RENDER_COMPLETE_SEMAPHORE output
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
     * @brief Push constant data (raw byte array)
     */
    INPUT_SLOT(PUSH_CONSTANTS, VkBuffer, 4,
        SlotNullability::Optional,  // Not all shaders use push constants
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Swapchain info (image views, dimensions, format)
     */
    INPUT_SLOT(SWAPCHAIN_INFO, SwapChainPublicVariablesPtr, 5,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Current swapchain image index to render to
     */
    INPUT_SLOT(IMAGE_INDEX, uint32_t, 6,
        SlotNullability::Required,
        SlotRole::ExecuteOnly,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Current frame-in-flight index for semaphore array indexing
     */
    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 7,
        SlotNullability::Required,
        SlotRole::ExecuteOnly,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief In-flight fence for CPU-GPU synchronization
     */
    INPUT_SLOT(IN_FLIGHT_FENCE, VkFence, 8,
        SlotNullability::Required,
        SlotRole::ExecuteOnly,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Image available semaphore array (indexed by CURRENT_FRAME_INDEX)
     */
    INPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, VkSemaphoreArrayPtr, 9,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Descriptor sets from DescriptorSetNode (per-image sets for storage image output)
     */
    INPUT_SLOT(DESCRIPTOR_SETS, DescriptorSetVector, 10,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Render complete semaphore array (indexed by IMAGE_INDEX)
     */
    INPUT_SLOT(RENDER_COMPLETE_SEMAPHORES_ARRAY, VkSemaphoreArrayPtr, 11,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (3) =====

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

    /**
     * @brief Render complete semaphore for Present to wait on
     */
    OUTPUT_SLOT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 2,
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
