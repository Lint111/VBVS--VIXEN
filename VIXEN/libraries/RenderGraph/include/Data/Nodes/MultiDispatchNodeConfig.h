#pragma once

#include "Core/TypedNodeInstance.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "Data/DispatchPass.h"  // Sprint 6.1: For GROUP_INPUTS accumulation slot

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

namespace Vixen::Vulkan::Resources {
    struct IRenderTarget; // AR#28: abstract render target interface
}

namespace Vixen::RenderGraph {

// ============================================================================
// SLOT COUNTS
// ============================================================================

namespace MultiDispatchNodeCounts {
    // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: +5 (IN_FLIGHT_FENCE,
    // IMAGE_AVAILABLE_SEMAPHORES_ARRAY, RENDER_COMPLETE_SEMAPHORES_ARRAY,
    // TIMELINE_SEMAPHORE_IN, TIMELINE_FRAME_BASE_IN) so this node can submit its own
    // recorded command buffer (vkQueueSubmit2) instead of only recording it — pre-M3,
    // MultiDispatchNode had no fence/semaphore inputs at all and every existing
    // consumer (test_group_dispatch, test_multidispatch_integration, the Inc2/3
    // bucketing GTest harnesses) either never submitted or hand-built its own
    // VkSubmitInfo OUTSIDE the node. All 5 are Optional (default VK_NULL_HANDLE/0) —
    // unlike ComputeStageNode's own Required IN_FLIGHT_FENCE, this stays Optional so
    // the 3 existing test suites above, none of which wire these, keep compiling and
    // behaving byte-identically: ExecuteImpl only calls vkQueueSubmit2 when the fence
    // input is actually connected (see that function's own guard).
    static constexpr size_t INPUTS = 11;  // Sprint 6.1: GROUP_INPUTS; Inc4 M3: +5 submit inputs
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

    /// Sprint 6.2: Frame budget for TaskQueue (nanoseconds, default 16.67ms for 60 FPS)
    static constexpr const char* FRAME_BUDGET_NS = "frameBudgetNs";

    /// Sprint 6.2: Budget overflow mode ("strict" or "lenient")
    static constexpr const char* BUDGET_OVERFLOW_MODE = "budgetOverflowMode";

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
    INPUT_SLOT(SWAPCHAIN_INFO, Vixen::Vulkan::Resources::IRenderTarget*, 2,
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
     * **Semantics:** Compile-time-only. Data is read and partitioned during
     * CompileImpl and cached in groupedDispatches_ map. This matches the
     * lifecycle of other compile-time resources (command buffers, etc.).
     * Use QueueDispatch() API for per-frame dynamic dispatch queuing.
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
        SlotStorageStrategy::Value);

    // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: submit-capability inputs (indices 6-10),
    // mirroring ComputeStageNodeConfig's own IN_FLIGHT_FENCE/semaphore-array/timeline
    // shape exactly (see that config's own doc comments for the full auto-sync
    // rationale) so ExecuteImpl can vkQueueSubmit2 this node's own recorded command
    // buffer instead of only recording it. All Optional — see MultiDispatchNodeCounts'
    // own comment above for why (byte-identical no-op for every pre-M3 consumer).

    /** @brief In-flight fence this node resets+signals on submit, when connected. */
    INPUT_SLOT(IN_FLIGHT_FENCE, VkFence, 6,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief imageAvailable (acquire) binary semaphore array, indexed by frame. */
    INPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 7,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief renderComplete binary semaphore array, indexed by image. */
    INPUT_SLOT(RENDER_COMPLETE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 8,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Timeline semaphore from FrameSyncNode (P5b), when this node participates
     *  in the graph's auto-sync schedule. */
    INPUT_SLOT(TIMELINE_SEMAPHORE_IN, VkSemaphore, 9,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Per-frame timeline base offset from FrameSyncNode (P5b). */
    INPUT_SLOT(TIMELINE_FRAME_BASE_IN, uint64_t, 10,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

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

        HandleDescriptor swapchainDesc{"IRenderTarget*"};
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

        // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: submit-capability input descriptors.
        HandleDescriptor fenceDesc{"VkFence"};
        INIT_INPUT_DESC(IN_FLIGHT_FENCE, "in_flight_fence",
            ResourceLifetime::Transient, fenceDesc);

        HandleDescriptor semArrayDesc{"std::vector<VkSemaphore>"};
        INIT_INPUT_DESC(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, "image_available_semaphores_array",
            ResourceLifetime::Persistent, semArrayDesc);
        INIT_INPUT_DESC(RENDER_COMPLETE_SEMAPHORES_ARRAY, "render_complete_semaphores_array",
            ResourceLifetime::Persistent, semArrayDesc);

        HandleDescriptor timelineSemDesc{"VkSemaphore"};
        INIT_INPUT_DESC(TIMELINE_SEMAPHORE_IN, "timeline_semaphore_in",
            ResourceLifetime::Persistent, timelineSemDesc);

        HandleDescriptor timelineBaseDesc{"uint64_t"};
        INIT_INPUT_DESC(TIMELINE_FRAME_BASE_IN, "timeline_frame_base_in",
            ResourceLifetime::Transient, timelineBaseDesc);

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
