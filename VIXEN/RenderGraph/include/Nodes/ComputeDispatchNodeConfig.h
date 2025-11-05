#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/ResourceVariant.h"

namespace Vixen::RenderGraph {

// ============================================================================
// SLOT COUNTS
// ============================================================================

namespace ComputeDispatchNodeCounts {
    static constexpr size_t INPUTS = 11;  // Added swapchain + sync inputs (descriptor sets moved to index 4)
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
     * @brief Descriptor sets (from DescriptorSetNode)
     */
    INPUT_SLOT(DESCRIPTOR_SETS, std::vector<VkDescriptorSet>, 4,
        SlotNullability::Required,
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
    INPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, std::vector<VkSemaphore>, 9,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Render complete semaphore array (indexed by IMAGE_INDEX)
     */
    INPUT_SLOT(RENDER_COMPLETE_SEMAPHORES_ARRAY, std::vector<VkSemaphore>, 10,
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

    // Constructor for runtime descriptor initialization
    ComputeDispatchNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device",
            ResourceLifetime::Persistent,
            vulkanDeviceDesc
        );

        INIT_INPUT_DESC(COMMAND_POOL, "command_pool",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_INPUT_DESC(COMPUTE_PIPELINE, "compute_pipeline",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_INPUT_DESC(PIPELINE_LAYOUT, "pipeline_layout",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_INPUT_DESC(DESCRIPTOR_SETS, "descriptor_sets",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handles
        );

        INIT_INPUT_DESC(SWAPCHAIN_INFO, "swapchain_info",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque struct pointer
        );

        INIT_INPUT_DESC(IMAGE_INDEX, "image_index",
            ResourceLifetime::Transient,
            BufferDescription{}  // Index value
        );

        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index",
            ResourceLifetime::Transient,
            BufferDescription{}  // Index value
        );

        INIT_INPUT_DESC(IN_FLIGHT_FENCE, "in_flight_fence",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        HandleDescriptor semaphoreArrayDesc{"VkSemaphoreArrayPtr"};
        INIT_INPUT_DESC(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, "image_available_semaphores_array",
            ResourceLifetime::Persistent,
            semaphoreArrayDesc
        );

        INIT_INPUT_DESC(RENDER_COMPLETE_SEMAPHORES_ARRAY, "render_complete_semaphores_array",
            ResourceLifetime::Persistent,
            semaphoreArrayDesc
        );

        // Initialize output descriptors
        INIT_OUTPUT_DESC(COMMAND_BUFFER, "command_buffer",
            ResourceLifetime::Transient,
            BufferDescription{}  // Opaque handle
        );

        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device_out",
            ResourceLifetime::Persistent,
            vulkanDeviceDesc
        );

        INIT_OUTPUT_DESC(RENDER_COMPLETE_SEMAPHORE, "render_complete_semaphore",
            ResourceLifetime::Transient,
            BufferDescription{}  // Opaque handle
        );
    }
    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<COMPUTE_PIPELINE_Slot::Type, VkPipeline>);
    static_assert(std::is_same_v<PIPELINE_LAYOUT_Slot::Type, VkPipelineLayout>);
    static_assert(std::is_same_v<DESCRIPTOR_SETS_Slot::Type, std::vector<VkDescriptorSet>>);
    static_assert(std::is_same_v<SWAPCHAIN_INFO_Slot::Type, SwapChainPublicVariablesPtr>);
    static_assert(std::is_same_v<IMAGE_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<IN_FLIGHT_FENCE_Slot::Type, VkFence>);
    static_assert(std::is_same_v<IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::Type, std::vector<VkSemaphore>>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::Type, std::vector<VkSemaphore>>);
    static_assert(std::is_same_v<COMMAND_BUFFER_Slot::Type, VkCommandBuffer>);
    static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORE_Slot::Type, VkSemaphore>);

    // Nullability validations
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE_IN is required");
    static_assert(!COMMAND_POOL_Slot::nullable, "COMMAND_POOL is required");
    static_assert(!COMPUTE_PIPELINE_Slot::nullable, "COMPUTE_PIPELINE is required");
    static_assert(!PIPELINE_LAYOUT_Slot::nullable, "PIPELINE_LAYOUT is required");
    static_assert(!DESCRIPTOR_SETS_Slot::nullable, "DESCRIPTOR_SETS is required");
    static_assert(!SWAPCHAIN_INFO_Slot::nullable, "SWAPCHAIN_INFO is required");
    static_assert(!IMAGE_INDEX_Slot::nullable, "IMAGE_INDEX is required");
    static_assert(!CURRENT_FRAME_INDEX_Slot::nullable, "CURRENT_FRAME_INDEX is required");
    static_assert(!IN_FLIGHT_FENCE_Slot::nullable, "IN_FLIGHT_FENCE is required");
    static_assert(!IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::nullable, "IMAGE_AVAILABLE_SEMAPHORES_ARRAY is required");
    static_assert(!RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::nullable, "RENDER_COMPLETE_SEMAPHORES_ARRAY is required");
};
// Compile-time validations
    static_assert(ComputeDispatchNodeConfig::INPUT_COUNT == ComputeDispatchNodeCounts::INPUTS, "Input count mismatch");
    static_assert(ComputeDispatchNodeConfig::OUTPUT_COUNT == ComputeDispatchNodeCounts::OUTPUTS, "Output count mismatch");

} // namespace Vixen::RenderGraph
