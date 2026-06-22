#pragma once

#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

/**
 * @brief Resource configuration for UIRenderNode (RmlUi → Vulkan).
 *
 * UIRenderNode mirrors GeometryRenderNode: it CONSUMES a color-only RENDER_PASS (from RenderPassNode)
 * and FRAMEBUFFERS (from FramebufferNode) built off the swapchain, so the swapchain-derived resource
 * lifecycle (recreate + cleanup on resize) lives in those nodes — never in the consumer. It owns only
 * its RmlUi pipeline/geometry/textures (via VixenRmlRenderInterface) and its per-image command buffers.
 *
 * Inputs (10): SWAPCHAIN_INFO, COMMAND_POOL, VULKAN_DEVICE, IMAGE_INDEX, CURRENT_FRAME_INDEX,
 *   IN_FLIGHT_FENCE, IMAGE_AVAILABLE_SEMAPHORES_ARRAY, RENDER_COMPLETE_SEMAPHORES_ARRAY,
 *   RENDER_PASS, FRAMEBUFFERS.
 * Outputs (2): COMMAND_BUFFERS, RENDER_COMPLETE_SEMAPHORE.
 * Parameters: rmlDocumentPath, fontPath.
 */
namespace UIRenderNodeCounts {
    static constexpr size_t INPUTS = 13;  // +COMPOSITE_WAIT_SEMAPHORE (compute→UI handoff) +TIMELINE_SEMAPHORE_IN +TIMELINE_FRAME_BASE_IN (P5b M1)
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Array;
}

CONSTEXPR_NODE_CONFIG(UIRenderNodeConfig,
                      UIRenderNodeCounts::INPUTS,
                      UIRenderNodeCounts::OUTPUTS,
                      UIRenderNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* RML_DOCUMENT_PATH = "rmlDocumentPath";
    static constexpr const char* FONT_PATH = "fontPath";
    // Composite mode (default false): when true this UI pass is layered OVER an upstream producer (the
    // voxel compute) that wrote the swapchain image and signalled the per-IMAGE semaphore wired into
    // IMAGE_AVAILABLE_SEMAPHORES_ARRAY. The node then waits on that array indexed by IMAGE (the
    // compute→UI handoff), signals its own per-image "ui complete" semaphore (output via
    // RENDER_COMPLETE_SEMAPHORE for present), and owns the frame fence. False = standalone UI graph
    // (S0 demo): wait imageAvailable[frame], signal renderComplete[image].
    static constexpr const char* PARAM_COMPOSITE = "composite";

    // ===== INPUTS (8) =====
    // Auto-sync P5b M3: the composite UI pass LOADs the compute output (initialLayout=General) and
    // blends the HUD over it, so it both reads and writes the swapchain image while it stays in
    // GENERAL. Declaring ColorAttachmentWriteGeneral makes the scheduler bake the compute(GENERAL)→
    // UI(GENERAL) timeline edge (compute writes ⇒ hazard) with NO layout transition — the timeline
    // semaphore alone carries the ordering + cross-submit memory visibility. ReadWrite mutability so
    // the tracker records this node as a writer for hazard detection (mirrors ComputeDispatchNode's
    // swapchain slot). UIRenderNode reads this handle in CompileImpl, so the slot stays Dependency.
    INPUT_SLOT_SYNC(SWAPCHAIN_INFO, Vixen::Vulkan::Resources::IRenderTarget*, 0,
        SlotNullability::Required, SlotRole::Dependency, SlotMutability::ReadWrite, SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ColorAttachmentWriteGeneral);

    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 1,
        SlotNullability::Required, SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(VULKAN_DEVICE, VulkanDevice*, 2,
        SlotNullability::Required, SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_INDEX, uint32_t, 3,
        SlotNullability::Required, SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 4,
        SlotNullability::Required, SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(IN_FLIGHT_FENCE, VkFence, 5,
        SlotNullability::Required, SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 6,
        SlotNullability::Required, SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(RENDER_COMPLETE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 7,
        SlotNullability::Required, SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    // Color-only render pass (from RenderPassNode) + per-image framebuffers (from FramebufferNode),
    // both built off the swapchain. UIRenderNode consumes them; it does not own their lifecycle.
    INPUT_SLOT(RENDER_PASS, VkRenderPass, 8,
        SlotNullability::Required, SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(FRAMEBUFFERS, std::vector<VkFramebuffer>, 9,
        SlotNullability::Required, SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    // Composite-only: the single per-frame semaphore the upstream producer (voxel compute) signals
    // after writing the swapchain image. The UI submit waits on it (COLOR_ATTACHMENT_OUTPUT) so the
    // load reads the finished voxel pixels. Optional ⇒ the standalone S0 UI graph omits it. This edge
    // also orders the UI's Execute after the compute's (the graph orders solely by explicit edges).
    INPUT_SLOT(COMPOSITE_WAIT_SEMAPHORE, VkSemaphore, 10,
        SlotNullability::Optional, SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    /**
     * @brief Timeline semaphore from FrameSyncNode (P5b M1).
     * Used in vkQueueSubmit2 to wait on timeline values for baked waitEdges.
     */
    INPUT_SLOT(TIMELINE_SEMAPHORE_IN, VkSemaphore, 11,
        SlotNullability::Optional, SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    /**
     * @brief Per-frame timeline base offset from FrameSyncNode (P5b M1).
     * Added to each SyncEdge::timelineOffset to compute the absolute wait value.
     */
    INPUT_SLOT(TIMELINE_FRAME_BASE_IN, uint64_t, 12,
        SlotNullability::Optional, SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    // ===== OUTPUTS (2) =====
    OUTPUT_SLOT(COMMAND_BUFFERS, VkCommandBuffer, 0,
        SlotNullability::Required, SlotMutability::WriteOnly);

    OUTPUT_SLOT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 1,
        SlotNullability::Required, SlotMutability::WriteOnly);

    UIRenderNodeConfig() {
        HandleDescriptor swapchainInfoDesc{"IRenderTarget*"};
        INIT_INPUT_DESC(SWAPCHAIN_INFO, "swapchain_info", ResourceLifetime::Persistent, swapchainInfoDesc);

        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, BufferDescription{});

        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        INIT_INPUT_DESC(IMAGE_INDEX, "image_index", ResourceLifetime::Transient, BufferDescription{});
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, BufferDescription{});
        INIT_INPUT_DESC(IN_FLIGHT_FENCE, "in_flight_fence", ResourceLifetime::Transient, BufferDescription{});

        HandleDescriptor semaphoreArrayDesc{"VkSemaphore*"};
        INIT_INPUT_DESC(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, "image_available_semaphores_array",
            ResourceLifetime::Persistent, semaphoreArrayDesc);
        INIT_INPUT_DESC(RENDER_COMPLETE_SEMAPHORES_ARRAY, "render_complete_semaphores_array",
            ResourceLifetime::Persistent, semaphoreArrayDesc);

        // RENDER_PASS is a handle (persistent-capable); FRAMEBUFFERS is a value vector, so it must be
        // Transient (a value type cannot be a Persistent slot). Mirrors GeometryRenderNodeConfig.
        INIT_INPUT_DESC(RENDER_PASS, "render_pass", ResourceLifetime::Persistent, BufferDescription{});
        INIT_INPUT_DESC(FRAMEBUFFERS, "framebuffers", ResourceLifetime::Transient, BufferDescription{});

        HandleDescriptor compositeWaitDesc{"VkSemaphore"};
        INIT_INPUT_DESC(COMPOSITE_WAIT_SEMAPHORE, "composite_wait_semaphore", ResourceLifetime::Transient, compositeWaitDesc);

        // P5b M1: timeline semaphore + per-frame base from FrameSyncNode
        HandleDescriptor timelineSemDesc{"VkSemaphore"};
        INIT_INPUT_DESC(TIMELINE_SEMAPHORE_IN, "timeline_semaphore_in", ResourceLifetime::Persistent, timelineSemDesc);

        HandleDescriptor frameBaseDesc{"uint64_t"};
        INIT_INPUT_DESC(TIMELINE_FRAME_BASE_IN, "timeline_frame_base_in", ResourceLifetime::Transient, frameBaseDesc);

        INIT_OUTPUT_DESC(COMMAND_BUFFERS, "command_buffers", ResourceLifetime::Transient, BufferDescription{});
        INIT_OUTPUT_DESC(RENDER_COMPLETE_SEMAPHORE, "render_complete_semaphore",
            ResourceLifetime::Transient, BufferDescription{});
    }

    VALIDATE_NODE_CONFIG(UIRenderNodeConfig, UIRenderNodeCounts);

    static_assert(SWAPCHAIN_INFO_Slot::index == 0, "SWAPCHAIN_INFO must be at index 0");
    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(VULKAN_DEVICE_Slot::index == 2, "VULKAN_DEVICE must be at index 2");
    static_assert(IMAGE_INDEX_Slot::index == 3, "IMAGE_INDEX must be at index 3");
    static_assert(CURRENT_FRAME_INDEX_Slot::index == 4, "CURRENT_FRAME_INDEX must be at index 4");
    static_assert(IN_FLIGHT_FENCE_Slot::index == 5, "IN_FLIGHT_FENCE must be at index 5");
    static_assert(IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::index == 6, "IMAGE_AVAILABLE_SEMAPHORES_ARRAY must be at index 6");
    static_assert(RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::index == 7, "RENDER_COMPLETE_SEMAPHORES_ARRAY must be at index 7");
    static_assert(RENDER_PASS_Slot::index == 8, "RENDER_PASS must be at index 8");
    static_assert(FRAMEBUFFERS_Slot::index == 9, "FRAMEBUFFERS must be at index 9");
    static_assert(COMPOSITE_WAIT_SEMAPHORE_Slot::index == 10, "COMPOSITE_WAIT_SEMAPHORE must be at index 10");
    static_assert(COMPOSITE_WAIT_SEMAPHORE_Slot::nullable, "COMPOSITE_WAIT_SEMAPHORE is optional");
    static_assert(TIMELINE_SEMAPHORE_IN_Slot::index == 11, "TIMELINE_SEMAPHORE_IN must be at index 11");
    static_assert(TIMELINE_SEMAPHORE_IN_Slot::nullable, "TIMELINE_SEMAPHORE_IN is optional");
    static_assert(TIMELINE_FRAME_BASE_IN_Slot::index == 12, "TIMELINE_FRAME_BASE_IN must be at index 12");
    static_assert(TIMELINE_FRAME_BASE_IN_Slot::nullable, "TIMELINE_FRAME_BASE_IN is optional");
    static_assert(COMMAND_BUFFERS_Slot::index == 0, "COMMAND_BUFFERS must be at index 0");
    static_assert(RENDER_COMPLETE_SEMAPHORE_Slot::index == 1, "RENDER_COMPLETE_SEMAPHORE must be at index 1");

    static_assert(std::is_same_v<SWAPCHAIN_INFO_Slot::Type, Vixen::Vulkan::Resources::IRenderTarget*>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<VULKAN_DEVICE_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<IMAGE_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<IN_FLIGHT_FENCE_Slot::Type, VkFence>);
    static_assert(std::is_same_v<IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::Type, const std::vector<VkSemaphore>&>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::Type, const std::vector<VkSemaphore>&>);
    static_assert(std::is_same_v<RENDER_PASS_Slot::Type, VkRenderPass>);
    static_assert(std::is_same_v<FRAMEBUFFERS_Slot::Type, std::vector<VkFramebuffer>>);
    static_assert(std::is_same_v<COMPOSITE_WAIT_SEMAPHORE_Slot::Type, VkSemaphore>);
    static_assert(std::is_same_v<TIMELINE_SEMAPHORE_IN_Slot::Type, VkSemaphore>);
    static_assert(std::is_same_v<TIMELINE_FRAME_BASE_IN_Slot::Type, uint64_t>);
    static_assert(std::is_same_v<COMMAND_BUFFERS_Slot::Type, VkCommandBuffer>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORE_Slot::Type, VkSemaphore>);
};

} // namespace Vixen::RenderGraph
