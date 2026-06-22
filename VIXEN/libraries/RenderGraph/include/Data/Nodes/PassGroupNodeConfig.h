// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "Core/TypedNodeInstance.h"
#include "Data/Core/CompileTimeResourceSystem.h"

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

namespace Vixen::Vulkan::Resources {
    struct IRenderTarget; // abstract render target interface
}

namespace Vixen::RenderGraph {

// ============================================================================
// SLOT COUNTS
// ============================================================================

namespace PassGroupNodeCounts {
    // 8 fixed FrameSync/WSI slots. The generic compile-ordering dependency is a
    // VARIADIC input (dynamic, beyond the fixed count): the host wires one edge per
    // handle-source node so TopologicalSort guarantees this node compiles last.
    static constexpr size_t INPUTS  = 8;
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

// ============================================================================
// PASS GROUP NODE CONFIG
// ============================================================================

/**
 * @brief Config for PassGroupNode — generic multi-pass node (compute + graphics)
 *
 * Assembles an ordered list of heterogeneous passes into ONE command buffer + ONE
 * submit, with intra-pass barriers auto-baked by BuildPassGroupSchedule (P4 core).
 *
 * Per-pass pipeline/render-pass/framebuffer handles are supplied via the host
 * assembly API (SetPasses / AddComputePass / AddRenderPass) — a fixed slot set
 * cannot express an arbitrary pass count.
 *
 * FrameSync / WSI wiring mirrors ComputeDispatchNode:
 *   - imageAvailable[frame]   wait binary semaphore
 *   - renderComplete[image]   signal binary semaphore
 *   - inFlightFence
 *
 * auto-sync P4 M3
 */
CONSTEXPR_NODE_CONFIG(PassGroupNodeConfig,
                      PassGroupNodeCounts::INPUTS,
                      PassGroupNodeCounts::OUTPUTS,
                      PassGroupNodeCounts::ARRAY_MODE) {

    // ===== INPUTS =====

    /** @brief Vulkan device for command buffer allocation (compile-time dependency) */
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Command pool for command buffer allocation */
    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Swapchain info (image views, dimensions, format) */
    INPUT_SLOT(SWAPCHAIN_INFO, Vixen::Vulkan::Resources::IRenderTarget*, 2,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Current swapchain image index to render to */
    INPUT_SLOT(IMAGE_INDEX, uint32_t, 3,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Image available semaphore array (indexed by CURRENT_FRAME_INDEX) */
    INPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 4,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Current frame-in-flight index for semaphore array indexing */
    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 5,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief In-flight fence for CPU-GPU synchronization */
    INPUT_SLOT(IN_FLIGHT_FENCE, VkFence, 6,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Render complete semaphore array (indexed by IMAGE_INDEX) */
    INPUT_SLOT(RENDER_COMPLETE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 7,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== VARIADIC INPUT (compile-ordering dependency) =====
    //
    // Beyond the 8 fixed slots, PassGroupNode accepts an ARBITRARY number of
    // VARIADIC inputs used SOLELY for compile ordering (their values are never
    // read). The host assembly API (SetPasses / AddComputePass / AddRenderPass)
    // supplies the concrete per-pass pipeline/render-pass/framebuffer handles,
    // which only exist AFTER their producing nodes compile. By wiring one variadic
    // edge per handle-source node (each Dependency-role, so it adds a topology
    // edge — VariadicConnectionRule), TopologicalSort GUARANTEES PassGroupNode
    // compiles after EVERY source, so the post-compile callback has populated the
    // pass list before CompileImpl bakes the schedule. Pass-count-agnostic.
    //
    // PassGroupNode subclasses VariadicTypedNode for this. With min=0 variadic
    // inputs, an unwired node (M3 smoke test) still constructs/validates.

    // ===== OUTPUTS =====

    /** @brief Render complete semaphore for Present to wait on */
    OUTPUT_SLOT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    /** @brief Recorded command buffer (debug / optional passthrough) */
    OUTPUT_SLOT(COMMAND_BUFFER, VkCommandBuffer, 1,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // ===== CONSTRUCTOR =====

    PassGroupNodeConfig() {
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor commandPoolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        HandleDescriptor swapchainDesc{"IRenderTarget*"};
        INIT_INPUT_DESC(SWAPCHAIN_INFO, "swapchain_info", ResourceLifetime::Persistent, swapchainDesc);

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(IMAGE_INDEX, "image_index", ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, uint32Desc);

        HandleDescriptor semaphoreArrayDesc{"std::vector<VkSemaphore>"};
        INIT_INPUT_DESC(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, "image_available_semaphores",
                        ResourceLifetime::Persistent, semaphoreArrayDesc);
        INIT_INPUT_DESC(RENDER_COMPLETE_SEMAPHORES_ARRAY, "render_complete_semaphores",
                        ResourceLifetime::Persistent, semaphoreArrayDesc);

        HandleDescriptor fenceDesc{"VkFence"};
        INIT_INPUT_DESC(IN_FLIGHT_FENCE, "in_flight_fence", ResourceLifetime::Transient, fenceDesc);

        HandleDescriptor semaphoreDesc{"VkSemaphore"};
        INIT_OUTPUT_DESC(RENDER_COMPLETE_SEMAPHORE, "render_complete_semaphore",
                         ResourceLifetime::Transient, semaphoreDesc);

        HandleDescriptor cmdBufferDesc{"VkCommandBuffer"};
        INIT_OUTPUT_DESC(COMMAND_BUFFER, "command_buffer", ResourceLifetime::Transient, cmdBufferDesc);
    }

    // ===== COMPILE-TIME VALIDATIONS =====

    VALIDATE_NODE_CONFIG(PassGroupNodeConfig, PassGroupNodeCounts);
};

} // namespace Vixen::RenderGraph
