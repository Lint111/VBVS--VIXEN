#pragma once

#include "Core/TypedNodeInstance.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "ShaderDataBundle.h"

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Forward declaration for debug capture
namespace Vixen::RenderGraph::Debug {
    class IDebugCapture;
}

namespace Vixen::RenderGraph {

// Type alias for debug capture interface
using IDebugCapture = Debug::IDebugCapture;

// ============================================================================
// SLOT COUNTS
// ============================================================================

namespace ComputeDispatchNodeCounts {
    static constexpr size_t INPUTS = 15;  // Added DEBUG_CAPTURE input
    static constexpr size_t OUTPUTS = 4;  // Added DEBUG_CAPTURE_OUT output
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
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
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
    INPUT_SLOT(DESCRIPTOR_SETS, const std::vector<VkDescriptorSet>&, 4,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Swapchain info (image views, dimensions, format)
     * Execute-only: swapchain info only needed during dispatch, not during pipeline creation
     */
    INPUT_SLOT(SWAPCHAIN_INFO, SwapChainPublicVariables*, 5,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Current swapchain image index to render to
     */
    INPUT_SLOT(IMAGE_INDEX, uint32_t, 6,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Current frame-in-flight index for semaphore array indexing
     */
    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 7,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief In-flight fence for CPU-GPU synchronization
     */
    INPUT_SLOT(IN_FLIGHT_FENCE, VkFence, 8,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Image available semaphore array (indexed by CURRENT_FRAME_INDEX)
     */
    INPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 9,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Render complete semaphore array (indexed by IMAGE_INDEX)
     */
    INPUT_SLOT(RENDER_COMPLETE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 10,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Shader data bundle with reflection metadata (for push constant detection)
     */
    INPUT_SLOT(SHADER_DATA_BUNDLE, const std::shared_ptr<ShaderManagement::ShaderDataBundle>&, 11,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Push constant data buffer (from PushConstantGathererNode)
     * Contains raw bytes to be passed to vkCmdPushConstants
     */
    INPUT_SLOT(PUSH_CONSTANT_DATA, std::vector<uint8_t>, 12,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Push constant ranges from shader reflection
     * Contains size, offset, and stage flags
     */
    INPUT_SLOT(PUSH_CONSTANT_RANGES, std::vector<VkPushConstantRange>, 13,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Debug capture interface (optional)
     * If provided, the dispatch node will output it for debug reader nodes.
     * This allows automatic debug buffer passthrough without manual wiring.
     */
    INPUT_SLOT(DEBUG_CAPTURE, IDebugCapture*, 14,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (4) =====

    /**
     * @brief Recorded command buffer with vkCmdDispatch
     */
    OUTPUT_SLOT(COMMAND_BUFFER, VkCommandBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    /**
     * @brief Pass-through device for downstream nodes
     */
    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevice*, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    /**
     * @brief Render complete semaphore for Present to wait on
     */
    OUTPUT_SLOT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    /**
     * @brief Debug capture interface passthrough
     * Passes through any debug capture resource from input to output,
     * allowing downstream debug reader nodes to receive it.
     */
    OUTPUT_SLOT(DEBUG_CAPTURE_OUT, IDebugCapture*, 3,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // ===== CONSTRUCTOR (Runtime descriptor initialization) =====

    ComputeDispatchNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor commandPoolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        HandleDescriptor pipelineDesc{"VkPipeline"};
        INIT_INPUT_DESC(COMPUTE_PIPELINE, "compute_pipeline", ResourceLifetime::Persistent, pipelineDesc);

        HandleDescriptor layoutDesc{"VkPipelineLayout"};
        INIT_INPUT_DESC(PIPELINE_LAYOUT, "pipeline_layout", ResourceLifetime::Persistent, layoutDesc);

        HandleDescriptor descSetsDesc{"std::vector<VkDescriptorSet>"};
        INIT_INPUT_DESC(DESCRIPTOR_SETS, "descriptor_sets", ResourceLifetime::Persistent, descSetsDesc);

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

        HandleDescriptor shaderBundleDesc{"ShaderDataBundle"};
        INIT_INPUT_DESC(SHADER_DATA_BUNDLE, "shader_data_bundle", ResourceLifetime::Persistent, shaderBundleDesc);

        HandleDescriptor pushConstDataDesc{"std::vector<uint8_t>"};
        INIT_INPUT_DESC(PUSH_CONSTANT_DATA, "push_constant_data", ResourceLifetime::Transient, pushConstDataDesc);

        HandleDescriptor pushConstRangesDesc{"std::vector<VkPushConstantRange>"};
        INIT_INPUT_DESC(PUSH_CONSTANT_RANGES, "push_constant_ranges", ResourceLifetime::Transient, pushConstRangesDesc);

        HandleDescriptor debugCaptureDesc{"IDebugCapture*"};
        INIT_INPUT_DESC(DEBUG_CAPTURE, "debug_capture", ResourceLifetime::Transient, debugCaptureDesc);

        // Initialize output descriptors
        HandleDescriptor cmdBufferDesc{"VkCommandBuffer"};
        INIT_OUTPUT_DESC(COMMAND_BUFFER, "command_buffer", ResourceLifetime::Transient, cmdBufferDesc);

        HandleDescriptor deviceOutDesc{"VulkanDevice*"};
        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device_out", ResourceLifetime::Persistent, deviceOutDesc);

        HandleDescriptor semaphoreDesc{"VkSemaphore"};
        INIT_OUTPUT_DESC(RENDER_COMPLETE_SEMAPHORE, "render_complete_semaphore", ResourceLifetime::Transient, semaphoreDesc);

        HandleDescriptor debugCaptureOutDesc{"IDebugCapture*"};
        INIT_OUTPUT_DESC(DEBUG_CAPTURE_OUT, "debug_capture_out", ResourceLifetime::Transient, debugCaptureOutDesc);
    }

    // ===== COMPILE-TIME VALIDATIONS =====

    VALIDATE_NODE_CONFIG(ComputeDispatchNodeConfig, ComputeDispatchNodeCounts);

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
