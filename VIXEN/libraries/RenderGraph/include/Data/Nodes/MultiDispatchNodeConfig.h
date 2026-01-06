#pragma once

#include "Core/TypedNodeInstance.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "Data/DispatchPass.h"  // Sprint 6.1: For GROUP_INPUTS accumulation slot

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

namespace Vixen::RenderGraph {

// ============================================================================
// SLOT COUNTS
// ============================================================================

namespace MultiDispatchNodeCounts {
    static constexpr size_t INPUTS = 6;  // Sprint 6.1: Added GROUP_INPUTS
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

// ============================================================================
// MULTI DISPATCH NODE CONFIG
// ============================================================================

/**
 * @brief Node that queues and executes multiple compute dispatches
 *
 * Records multiple vkCmdDispatch calls to a single command buffer with
 * automatic barrier insertion between passes. Useful for multi-pass
 * compute sequences like:
 * - Prefilter -> Main -> Postfilter
 * - Mipmap generation chains
 * - Iterative algorithms
 *
 * Sprint 6: Timeline Foundation - Task #312
 *
 * Example usage:
 * ```cpp
 * auto* multiDispatch = graph->GetNode<MultiDispatchNode>("myMultiDispatch");
 *
 * // Queue passes (before Execute)
 * multiDispatch->QueueDispatch(prefilterPass);
 * multiDispatch->QueueDispatch(mainPass);
 * multiDispatch->QueueDispatch(postfilterPass);
 *
 * // ExecuteImpl records all queued passes to command buffer
 * ```
 *
 * @see DispatchPass for pass descriptor
 * @see ComputeDispatchNode for single-dispatch equivalent
 */
CONSTEXPR_NODE_CONFIG(MultiDispatchNodeConfig,
                      MultiDispatchNodeCounts::INPUTS,
                      MultiDispatchNodeCounts::OUTPUTS,
                      MultiDispatchNodeCounts::ARRAY_MODE) {

    // ===== PARAMETER NAMES =====

    /// Enable automatic UAV barrier insertion between passes
    static constexpr const char* AUTO_BARRIERS = "autoBarriers";

    /// Enable per-pass timestamp queries for profiling
    static constexpr const char* ENABLE_TIMESTAMPS = "enableTimestamps";

    // ===== INPUTS (5) =====

    /**
     * @brief Vulkan device for command buffer operations
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
     * @brief Swapchain info for image count (command buffer sizing)
     */
    INPUT_SLOT(SWAPCHAIN_INFO, SwapChainPublicVariables*, 2,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Current swapchain image index
     */
    INPUT_SLOT(IMAGE_INDEX, uint32_t, 3,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Current frame-in-flight index
     */
    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 4,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Sprint 6.1: Accumulation slot for group-partitioned dispatch passes
     *
     * Collects DispatchPass elements and partitions them by group ID.
     * Each group gets its own dispatch execution with accumulated data.
     *
     * Usage with GroupKeyModifier:
     * @code
     * batch.Connect(passGenerator, PassGenConfig::DISPATCH_PASS,
     *               multiDispatch, MultiDispatchNodeConfig::GROUP_INPUTS,
     *               GroupKey(&DispatchPass::groupId));
     * @endcode
     *
     * Storage: Value strategy (copies passes - safe for cross-frame use)
     */
    ACCUMULATION_INPUT_SLOT_V2(GROUP_INPUTS, std::vector<DispatchPass>, DispatchPass, 5,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotStorageStrategy::Value);

    // ===== OUTPUTS (2) =====

    /**
     * @brief Recorded command buffer with all dispatches
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

    // ===== CONSTRUCTOR (Runtime descriptor initialization) =====

    MultiDispatchNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device",
            ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor commandPoolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool",
            ResourceLifetime::Persistent, commandPoolDesc);

        HandleDescriptor swapchainDesc{"SwapChainPublicVariables*"};
        INIT_INPUT_DESC(SWAPCHAIN_INFO, "swapchain_info",
            ResourceLifetime::Persistent, swapchainDesc);

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(IMAGE_INDEX, "image_index",
            ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index",
            ResourceLifetime::Transient, uint32Desc);

        // Sprint 6.1: Initialize GROUP_INPUTS accumulation slot
        HandleDescriptor dispatchPassVecDesc{"std::vector<DispatchPass>"};
        INIT_INPUT_DESC(GROUP_INPUTS, "group_inputs",
            ResourceLifetime::Transient, dispatchPassVecDesc);

        // Initialize output descriptors
        HandleDescriptor cmdBufferDesc{"VkCommandBuffer"};
        INIT_OUTPUT_DESC(COMMAND_BUFFER, "command_buffer",
            ResourceLifetime::Transient, cmdBufferDesc);

        HandleDescriptor deviceOutDesc{"VulkanDevice*"};
        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device_out",
            ResourceLifetime::Persistent, deviceOutDesc);
    }

    // ===== COMPILE-TIME VALIDATIONS =====

    VALIDATE_NODE_CONFIG(MultiDispatchNodeConfig, MultiDispatchNodeCounts);

    /**
     * @brief Validate dispatch dimensions against Vulkan spec limits
     */
    static constexpr bool ValidateWorkGroupCount(uint32_t x, uint32_t y, uint32_t z) {
        // Vulkan spec guarantees at least 65535 per dimension
        return x > 0 && y > 0 && z > 0 &&
               x <= 65535 && y <= 65535 && z <= 65535;
    }

    /**
     * @brief Maximum dispatches per frame (arbitrary limit for safety)
     */
    static constexpr uint32_t MAX_DISPATCHES_PER_FRAME = 256;
};

} // namespace Vixen::RenderGraph
