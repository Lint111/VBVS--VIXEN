#pragma once
#include "Data/Core/ResourceConfig.h"
#include "Data/Nodes/RayTracingPipelineNodeConfig.h"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;
// SwapChainPublicVariables is in global namespace (from VulkanSwapChain.h)

// ============================================================================
// NODE CONFIG
// ============================================================================

namespace TraceRaysNodeCounts {
    static constexpr size_t INPUTS = 12;  // Added DESCRIPTOR_SETS
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for TraceRaysNode
 *
 * Dispatches ray tracing using vkCmdTraceRaysKHR.
 * Follows same pattern as ComputeDispatchNode for frame synchronization.
 *
 * Inputs: 11
 * - VULKAN_DEVICE_IN, COMMAND_POOL: Device resources
 * - RT_PIPELINE_DATA, ACCELERATION_STRUCTURE_DATA: RT resources
 * - SWAPCHAIN_INFO, IMAGE_INDEX, CURRENT_FRAME_INDEX: Frame info
 * - IN_FLIGHT_FENCE: Synchronization
 * - IMAGE_AVAILABLE_SEMAPHORES_ARRAY, RENDER_COMPLETE_SEMAPHORES_ARRAY: Semaphores
 * - PUSH_CONSTANT_DATA: Camera data
 *
 * Outputs: 2
 * - COMMAND_BUFFER: Recorded command buffer
 * - RENDER_COMPLETE_SEMAPHORE: For present to wait on
 */
CONSTEXPR_NODE_CONFIG(TraceRaysNodeConfig,
                      TraceRaysNodeCounts::INPUTS,
                      TraceRaysNodeCounts::OUTPUTS,
                      TraceRaysNodeCounts::ARRAY_MODE) {

    // ===== PARAMETER NAMES =====
    static constexpr const char* PARAM_WIDTH = "width";
    static constexpr const char* PARAM_HEIGHT = "height";
    static constexpr const char* PARAM_DEPTH = "depth";

    // ===== INPUTS =====

    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(RT_PIPELINE_DATA, RayTracingPipelineData*, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(ACCELERATION_STRUCTURE_DATA, AccelerationStructureData*, 3,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Swapchain info for output image access
    INPUT_SLOT(SWAPCHAIN_INFO, SwapChainPublicVariables*, 4,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_INDEX, uint32_t, 5,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 6,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(IN_FLIGHT_FENCE, VkFence, 7,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 8,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(RENDER_COMPLETE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 9,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Push constants (camera data - 64 bytes)
    INPUT_SLOT(PUSH_CONSTANT_DATA, std::vector<uint8_t>, 10,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Descriptor sets from DescriptorSetNode
    INPUT_SLOT(DESCRIPTOR_SETS, const std::vector<VkDescriptorSet>&, 11,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS =====

    OUTPUT_SLOT(COMMAND_BUFFER, VkCommandBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ===== CONSTRUCTOR =====

    TraceRaysNodeConfig() {
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor commandPoolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        HandleDescriptor pipelineDesc{"RayTracingPipelineData*"};
        INIT_INPUT_DESC(RT_PIPELINE_DATA, "rt_pipeline", ResourceLifetime::Persistent, pipelineDesc);

        HandleDescriptor accelDesc{"AccelerationStructureData*"};
        INIT_INPUT_DESC(ACCELERATION_STRUCTURE_DATA, "acceleration_structure", ResourceLifetime::Persistent, accelDesc);

        HandleDescriptor swapchainDesc{"SwapChainPublicVariables*"};
        INIT_INPUT_DESC(SWAPCHAIN_INFO, "swapchain_info", ResourceLifetime::Persistent, swapchainDesc);

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(IMAGE_INDEX, "image_index", ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, uint32Desc);

        HandleDescriptor fenceDesc{"VkFence"};
        INIT_INPUT_DESC(IN_FLIGHT_FENCE, "in_flight_fence", ResourceLifetime::Transient, fenceDesc);

        HandleDescriptor semaphoreArrayDesc{"std::vector<VkSemaphore>"};
        INIT_INPUT_DESC(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, "image_available_semaphores", ResourceLifetime::Persistent, semaphoreArrayDesc);
        INIT_INPUT_DESC(RENDER_COMPLETE_SEMAPHORES_ARRAY, "render_complete_semaphores", ResourceLifetime::Persistent, semaphoreArrayDesc);

        HandleDescriptor pushConstDataDesc{"std::vector<uint8_t>"};
        INIT_INPUT_DESC(PUSH_CONSTANT_DATA, "push_constant_data", ResourceLifetime::Transient, pushConstDataDesc);

        HandleDescriptor descSetsDesc{"std::vector<VkDescriptorSet>"};
        INIT_INPUT_DESC(DESCRIPTOR_SETS, "descriptor_sets", ResourceLifetime::Persistent, descSetsDesc);

        // Outputs
        HandleDescriptor cmdBufferDesc{"VkCommandBuffer"};
        INIT_OUTPUT_DESC(COMMAND_BUFFER, "command_buffer", ResourceLifetime::Transient, cmdBufferDesc);

        HandleDescriptor semaphoreDesc{"VkSemaphore"};
        INIT_OUTPUT_DESC(RENDER_COMPLETE_SEMAPHORE, "render_complete_semaphore", ResourceLifetime::Transient, semaphoreDesc);
    }

    VALIDATE_NODE_CONFIG(TraceRaysNodeConfig, TraceRaysNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0);
    static_assert(COMMAND_POOL_Slot::index == 1);
    static_assert(RT_PIPELINE_DATA_Slot::index == 2);
    static_assert(ACCELERATION_STRUCTURE_DATA_Slot::index == 3);
    static_assert(SWAPCHAIN_INFO_Slot::index == 4);
    static_assert(IMAGE_INDEX_Slot::index == 5);
    static_assert(CURRENT_FRAME_INDEX_Slot::index == 6);
    static_assert(IN_FLIGHT_FENCE_Slot::index == 7);
    static_assert(IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::index == 8);
    static_assert(RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::index == 9);
    static_assert(PUSH_CONSTANT_DATA_Slot::index == 10);
    static_assert(DESCRIPTOR_SETS_Slot::index == 11);
    static_assert(COMMAND_BUFFER_Slot::index == 0);
    static_assert(RENDER_COMPLETE_SEMAPHORE_Slot::index == 1);
};

} // namespace Vixen::RenderGraph
