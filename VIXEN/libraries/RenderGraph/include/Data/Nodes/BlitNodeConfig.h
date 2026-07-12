// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc3 M1 (KI-018): presentation-only render-target->swapchain blit node config.
#pragma once

#include "Core/TypedNodeInstance.h"
#include "Data/Core/CompileTimeResourceSystem.h"

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

namespace Vixen::Vulkan::Resources {
    struct IRenderTarget;  // AR#28: abstract render target interface
}

namespace Vixen::RenderGraph {

// ============================================================================
// SLOT COUNTS
// ============================================================================

namespace BlitNodeCounts {
    static constexpr size_t INPUTS  = 12;
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

// ============================================================================
// BLIT NODE CONFIG
// ============================================================================

/**
 * @brief Presentation-only render-target->swapchain blit node (Sampled Lighting Inc3 M1, KI-018).
 *
 * The architectural gap this node closes: ComputeDispatchNode bundles "dispatch its own
 * shader" + "blit its render target to swapchain" in one ExecuteImpl, and ComputeStageNode
 * (the node type an auto-sync-chained middle pass, e.g. DirectLightingNode, uses) has no
 * blit capability at all. Neither node can correctly sequence a THREE-stage chain — an
 * image-producing pass (march, HitRecord only) -> a second image-producing pass
 * (DirectLighting, shades + writes the render target) -> blit-to-swapchain — without either
 * blitting stale content or losing the auto-sync hazard declaration. BlitNode is a THIRD,
 * deliberately minimal node whose only job is the blit: it reads the render target an
 * upstream ComputeStageNode wrote (via that node's IMAGE_WRITE sync slot) and blits it to
 * the swapchain, exactly reusing SwapchainBarriers::BlitRenderTargetToSwapchain (the SAME
 * logic ComputeDispatchNode's own render-scale blit uses — extracted, not reimplemented).
 *
 * Architectural principle (per design review): this is a PRESENTATION node, kept separate
 * from the compute-stage nodes on purpose — the separation IS the point. It owns the WSI
 * contract (binary imageAvailable wait is NOT needed here — the upstream pass chain already
 * consumed the acquire; BlitNode signals renderComplete for Present, owns the in-flight
 * fence, and ends the swapchain image at the same leaveImageInGeneral-gated layout contract
 * ComputeDispatchNode's blit path already established) so that IMAGE_WRITE producers
 * upstream (ComputeStageNode) never need to touch WSI at all.
 *
 * IMAGE_READ pairs with an upstream ComputeStageNode's IMAGE_WRITE (ComputeStorageWrite) on
 * the SAME wired Resource* — the scheduler bakes a real SyncEdge (write->read hazard) off
 * that shared Resource*, giving BlitNode a genuine timeline wait on the shading pass's
 * completion, mirroring the BUFFER_WRITE/BUFFER_READ_A fan-in pattern exactly, just for an
 * image instead of a buffer.
 */
CONSTEXPR_NODE_CONFIG(BlitNodeConfig,
                      BlitNodeCounts::INPUTS,
                      BlitNodeCounts::OUTPUTS,
                      BlitNodeCounts::ARRAY_MODE) {

    // ===== PARAMETER NAMES =====
    // Mirrors ComputeDispatchNodeConfig::PARAM_LEAVE_IMAGE_IN_GENERAL exactly: when true, the
    // swapchain image is left in GENERAL after the blit (a downstream graphics pass — sky
    // projection / UI composite — owns the final ->PRESENT_SRC transition and the frame
    // fence). When false, this node hands the image straight to Present.
    static constexpr const char* PARAM_LEAVE_IMAGE_IN_GENERAL = "leaveImageInGeneral";

    // ===== INPUTS (12) =====

    /** @brief Vulkan device for command-buffer allocation. */
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Command pool for command-buffer allocation. */
    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Render target this node blits FROM (paired with an upstream ComputeStageNode's
     * IMAGE_WRITE output — SAME wired Resource*).
     * Auto-sync: TransferRead — pairs with IMAGE_WRITE's ComputeStorageWrite on the shared
     * Resource*, baking the real write->read SyncEdge (the fan-in wait this node needs before
     * its blit is safe to read).
     */
    INPUT_SLOT_SYNC(IMAGE_READ, Vixen::Vulkan::Resources::IRenderTarget*, 2,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::TransferRead);

    /** @brief The true swapchain — blit destination + WSI plumbing (image index, present). */
    INPUT_SLOT(SWAPCHAIN_INFO, Vixen::Vulkan::Resources::IRenderTarget*, 3,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Current swapchain image index. */
    INPUT_SLOT(IMAGE_INDEX, uint32_t, 4,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Current frame-in-flight index (semaphore array indexing). */
    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 5,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief In-flight fence — this node owns + resets it (the frame's last compute-queue submit). */
    INPUT_SLOT(IN_FLIGHT_FENCE, VkFence, 6,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief renderComplete binary semaphore array, indexed by image (this node signals it -> Present). */
    INPUT_SLOT(RENDER_COMPLETE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 7,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Timeline semaphore from FrameSyncNode (P5b). vkQueueSubmit2 WAITS the absolute
     * timeline value(s) for the baked IMAGE_READ<-IMAGE_WRITE edge(s) — the genuine fan-in
     * wait proving the shading pass finished before this node reads its output.
     */
    INPUT_SLOT(TIMELINE_SEMAPHORE_IN, VkSemaphore, 8,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief Per-frame timeline base offset from FrameSyncNode (P5b). */
    INPUT_SLOT(TIMELINE_FRAME_BASE_IN, uint64_t, 9,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /**
     * @brief Ordering-only edge INTO this node (mirrors ComputeStageNodeConfig's own
     * ordering-edge convention — see BuildRenderGraph.cpp's COMPOSITE_WAIT_SEMAPHORE usage):
     * wiring the upstream shading pass's RENDER_COMPLETE_SEMAPHORE here establishes the
     * TOPOLOGICAL execution order (shading-before-blit) the FrameSyncScheduler needs to bake
     * the IMAGE_READ/IMAGE_WRITE edge in the right direction — the binary semaphore VALUE
     * itself is inert (this node does not wait it; see the class doc comment on why an
     * explicit WSI acquire-wait is not needed here).
     */
    INPUT_SLOT(ORDERING_WAIT_SEMAPHORE, VkSemaphore, 10,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief imageAvailable (acquire) binary semaphore array — only consulted if this node
     * turns out to be the FIRST submit in some future graph shape; the default composite
     * chain's acquire is already consumed upstream, so this is Optional and typically
     * unconnected (mirrors ComputeStageNode's own imageAvailable contract for a non-first
     * consumer, kept for shape-completeness rather than active use in M1's wiring). */
    INPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 11,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (2) =====

    /** @brief renderComplete semaphore for Present to wait on. */
    OUTPUT_SLOT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    /** @brief Pass-through device for downstream nodes. */
    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevice*, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ===== CONSTRUCTOR (runtime descriptor initialization) =====

    BlitNodeConfig() {
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor commandPoolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        HandleDescriptor renderTargetDesc{"IRenderTarget*"};
        INIT_INPUT_DESC(IMAGE_READ, "image_read", ResourceLifetime::Persistent, renderTargetDesc);
        INIT_INPUT_DESC(SWAPCHAIN_INFO, "swapchain_info", ResourceLifetime::Persistent, renderTargetDesc);

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(IMAGE_INDEX, "image_index", ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, uint32Desc);

        HandleDescriptor fenceDesc{"VkFence"};
        INIT_INPUT_DESC(IN_FLIGHT_FENCE, "in_flight_fence", ResourceLifetime::Transient, fenceDesc);

        HandleDescriptor semaphoreArrayDesc{"std::vector<VkSemaphore>"};
        INIT_INPUT_DESC(RENDER_COMPLETE_SEMAPHORES_ARRAY, "render_complete_semaphores", ResourceLifetime::Persistent, semaphoreArrayDesc);
        INIT_INPUT_DESC(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, "image_available_semaphores", ResourceLifetime::Persistent, semaphoreArrayDesc);

        HandleDescriptor timelineSemDesc{"VkSemaphore"};
        INIT_INPUT_DESC(TIMELINE_SEMAPHORE_IN, "timeline_semaphore_in", ResourceLifetime::Persistent, timelineSemDesc);

        HandleDescriptor frameBaseDesc{"uint64_t"};
        INIT_INPUT_DESC(TIMELINE_FRAME_BASE_IN, "timeline_frame_base_in", ResourceLifetime::Transient, frameBaseDesc);

        HandleDescriptor orderingSemDesc{"VkSemaphore"};
        INIT_INPUT_DESC(ORDERING_WAIT_SEMAPHORE, "ordering_wait_semaphore", ResourceLifetime::Transient, orderingSemDesc);

        // Outputs.
        HandleDescriptor semaphoreDesc{"VkSemaphore"};
        INIT_OUTPUT_DESC(RENDER_COMPLETE_SEMAPHORE, "render_complete_semaphore", ResourceLifetime::Transient, semaphoreDesc);

        HandleDescriptor deviceOutDesc{"VulkanDevice*"};
        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device_out", ResourceLifetime::Persistent, deviceOutDesc);
    }

    // ===== COMPILE-TIME VALIDATIONS =====

    VALIDATE_NODE_CONFIG(BlitNodeConfig, BlitNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE_IN must not be nullable");
    static_assert(std::is_same_v<IMAGE_READ_Slot::Type, Vixen::Vulkan::Resources::IRenderTarget*>);
    static_assert(std::is_same_v<SWAPCHAIN_INFO_Slot::Type, Vixen::Vulkan::Resources::IRenderTarget*>);
    static_assert(IMAGE_READ_Slot::accessKind == ::Vixen::RenderGraph::AccessKind::TransferRead);
    static_assert(!IMAGE_READ_Slot::nullable, "IMAGE_READ must be required — a blit with nothing to read is a config error");
};

} // namespace Vixen::RenderGraph
