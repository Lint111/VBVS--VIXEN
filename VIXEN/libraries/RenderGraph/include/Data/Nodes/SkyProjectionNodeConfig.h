#pragma once

#include "Data/Core/ResourceConfig.h"
#include "Data/CameraData.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts
namespace SkyProjectionNodeCounts {
    // VULKAN_DEVICE_IN, COMMAND_POOL, SWAPCHAIN_INFO, CAMERA_DATA, RENDER_PASS, FRAMEBUFFERS,
    // IMAGE_INDEX, CURRENT_FRAME_INDEX, IN_FLIGHT_FENCE, IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
    // TIMELINE_SEMAPHORE_IN, TIMELINE_FRAME_BASE_IN, COMPOSITE_WAIT_SEMAPHORE
    static constexpr size_t INPUTS  = 13;
    static constexpr size_t OUTPUTS = 3;  // SKY_POINTS_BUFFER, SKY_POINT_COUNT, RENDER_COMPLETE_SEMAPHORE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Array;
}

/**
 * @brief Configuration for SkyProjectionNode (Tiered-ESVO Inc1 M3, Tasks 5-7).
 *
 * Two roles in one node/config, exactly as the design doc frames it ("a small, bolt-on node
 * consuming TierAddress data"):
 *
 *   DATA role (Task 5): builds a small CPU-side "sky point" fixture — (direction, magnitude)
 *   tuples derived from M1/M2's TierAddress/TierDirection/TierMagnitude math — and uploads it
 *   as a host-visible SSBO. Mirrors InstanceBufferNode's "small CPU-side per-instance dataset
 *   -> SSBO" idiom: created once in CompileImpl, filled directly via vkMapMemory/memcpy (no
 *   staging buffer — appropriate for a handful of points), persists across recompile
 *   (destroyed only at FinalTeardown). No per-frame re-upload/ring index (like
 *   BodyOctreeSceneNodeConfig::CURRENT_FRAME_INDEX): this fixture never changes after Compile,
 *   so a ring buffer would only guard against a race that cannot happen here.
 *
 *   DRAW role (Tasks 6-7): composites the SSBO's points as point sprites over the existing
 *   swapchain image via a point-list graphics pipeline this node owns directly (raw Vulkan,
 *   not the ShaderLibraryNode/DescriptorResourceGathererNode/DescriptorSetNode reflection
 *   chain — unnecessary for a single-SSBO-binding pipeline). This node sits BETWEEN the voxel
 *   compute dispatch and the UI/HUD composite pass in the graph (compute -> sky-projection ->
 *   UI), so its slot shape mirrors ComputeDispatchNodeConfig's "middle pass" shape (NOT
 *   UIRenderNodeConfig's "last pass, owns the frame fence + present semaphore" shape): it reads
 *   IN_FLIGHT_FENCE/semaphore-array inputs but submits with VK_NULL_HANDLE as its own fence
 *   parameter (mirrors ComputeDispatchNode's `leaveImageInGeneral ? VK_NULL_HANDLE :
 *   inFlightFence` — the fence is owned by whichever pass is LAST, which stays UIRenderNode),
 *   and its RENDER_COMPLETE_SEMAPHORE output is wired to the downstream UI composite pass'
 *   ordering input (not to PresentNode — present keeps waiting on UI, unchanged), while actual
 *   compute->sky-projection->UI ordering is carried by the FrameSyncScheduler's baked timeline
 *   edges (TIMELINE_SEMAPHORE_IN/TIMELINE_FRAME_BASE_IN), exactly like the existing compute->UI
 *   edge already works (see BuildRenderGraph.cpp's long comment on COMPOSITE_WAIT_SEMAPHORE).
 *
 * CAMERA_DATA (from CameraNode) is consumed as a single `const CameraData&` slot rather than
 * routed through a PushConstantGathererNode's per-field ExtractField wiring: this node reads
 * the 5 basis fields (cameraDir/cameraUp/cameraRight/fov/aspect — cameraPos is intentionally
 * unused, see SkyProjectionNode.h/shaders/SkyProjection.vert) directly in ExecuteImpl and
 * hand-packs its own small push-constant block, which is simpler than standing up a dedicated
 * gatherer node for 5 fields feeding one small graphics pass.
 *
 * Inputs: 13 (VULKAN_DEVICE_IN, COMMAND_POOL, SWAPCHAIN_INFO, CAMERA_DATA, RENDER_PASS,
 *             FRAMEBUFFERS, IMAGE_INDEX, CURRENT_FRAME_INDEX, IN_FLIGHT_FENCE,
 *             IMAGE_AVAILABLE_SEMAPHORES_ARRAY, TIMELINE_SEMAPHORE_IN, TIMELINE_FRAME_BASE_IN,
 *             COMPOSITE_WAIT_SEMAPHORE)
 * Outputs: 3 (SKY_POINTS_BUFFER, SKY_POINT_COUNT, RENDER_COMPLETE_SEMAPHORE)
 */
CONSTEXPR_NODE_CONFIG(SkyProjectionNodeConfig,
                      SkyProjectionNodeCounts::INPUTS,
                      SkyProjectionNodeCounts::OUTPUTS,
                      SkyProjectionNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (13) =====
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

    // Composite color-attachment write over an image kept in GENERAL (the voxel compute wrote
    // it, and this pass's own render pass LOADs it) — same AccessKind UIRenderNode declares on
    // its own SWAPCHAIN_INFO slot, so the scheduler bakes the compute(GENERAL)->sky-projection
    // (GENERAL) timeline edge with no layout transition. ReadWrite: the tracker records this
    // node as a writer for hazard detection (this node also needs the extent -> GetExtent()).
    INPUT_SLOT_SYNC(SWAPCHAIN_INFO, Vixen::Vulkan::Resources::IRenderTarget*, 2,
        SlotNullability::Required, SlotRole::Dependency, SlotMutability::ReadWrite, SlotScope::NodeLevel,
        ::Vixen::RenderGraph::AccessKind::ColorAttachmentWriteGeneral);

    // Live camera basis (cameraDir/cameraUp/cameraRight/fov/aspect) for the direction->screen
    // push constant — see file header for why this is one CameraData slot, not per-field.
    INPUT_SLOT(CAMERA_DATA, const CameraData&, 3,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Color-only render pass (Load-op, built by a dedicated RenderPassNode mirroring
    // ui_composite_render_pass) + per-image framebuffers (FramebufferNode). Consumed, not
    // owned — mirrors GeometryRenderNode/UIRenderNode's own RENDER_PASS/FRAMEBUFFERS slots.
    INPUT_SLOT(RENDER_PASS, VkRenderPass, 4,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(FRAMEBUFFERS, std::vector<VkFramebuffer>, 5,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_INDEX, uint32_t, 6,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 7,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Read but NOT used as this node's own vkQueueSubmit2 fence parameter (this is a middle
    // pass — see class doc comment); kept as a real slot (not omitted) so a future change that
    // needs it does not require a signature/wiring change, mirroring ComputeDispatchNodeConfig's
    // own IN_FLIGHT_FENCE slot, which is likewise unused-as-submit-fence on the composite path.
    INPUT_SLOT(IN_FLIGHT_FENCE, VkFence, 8,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Optional: only needed in a hypothetical standalone graph (no upstream compute producer)
    // where THIS pass would be the first submit and must wait the WSI acquire directly. In the
    // live composite pipeline this is left unconnected — ordering vs. the upstream compute is
    // carried solely by the timeline waitEdge (mirrors UIRenderNode's composite_ convention).
    INPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 9,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(TIMELINE_SEMAPHORE_IN, VkSemaphore, 10,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(TIMELINE_FRAME_BASE_IN, uint64_t, 11,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // TOPOLOGY-ONLY ordering edge from the upstream compute dispatch (connected to
    // ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORE) — mirrors UIRenderNodeConfig's own
    // COMPOSITE_WAIT_SEMAPHORE exactly, including its documented vestigial status: the binary
    // semaphore this carries is NOT waited on by this node's own vkQueueSubmit2 (ordering is
    // carried solely by the baked timeline waitEdge, see TIMELINE_SEMAPHORE_IN above). This slot
    // exists ONLY so the graph's topological sort places this node after the compute dispatch
    // (FrameSyncScheduler derives edge direction from execution/groupId order — see
    // BuildRenderGraph.cpp's long comment on the equivalent compute->UI edge for the full
    // rationale, which applies identically here now that this node sits in between).
    INPUT_SLOT(COMPOSITE_WAIT_SEMAPHORE, VkSemaphore, 12,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (3) =====
    OUTPUT_SLOT(SKY_POINTS_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // uint32_t (not int32_t): this count is consumed directly by SkyProjectionNode's own
    // vkCmdDraw(vertexCount=count, ...) — it never crosses a PushConstantGathererNode
    // reflection boundary (unlike BodyOctreeSceneNodeConfig::INSTANCE_COUNT, which must be
    // int32_t to match that shader's reflected `int instanceCount` field). No such
    // constraint applies here, so the natural unsigned "how many points" type is used.
    OUTPUT_SLOT(SKY_POINT_COUNT, uint32_t, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // The ordering handoff to the downstream UI composite pass (connected to
    // UIRenderNodeConfig::COMPOSITE_WAIT_SEMAPHORE, replacing the compute->UI direct edge —
    // compute->sky-projection->UI). This is a binary semaphore signal alongside the timeline
    // signal, kept for the same reason UIRenderNode's own COMPOSITE_WAIT_SEMAPHORE input slot
    // is kept even though the timeline edge is what actually orders execution: preserving the
    // topology edge so the scheduler's topological sort places this node before UI.
    OUTPUT_SLOT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    SkyProjectionNodeConfig() {
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        CommandPoolDescriptor commandPoolDesc{};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        HandleDescriptor swapchainInfoDesc{"IRenderTarget*"};
        INIT_INPUT_DESC(SWAPCHAIN_INFO, "swapchain_info", ResourceLifetime::Persistent, swapchainInfoDesc);

        HandleDescriptor cameraDataDesc{"CameraData"};
        INIT_INPUT_DESC(CAMERA_DATA, "camera_data", ResourceLifetime::Persistent, cameraDataDesc);

        INIT_INPUT_DESC(RENDER_PASS, "render_pass", ResourceLifetime::Persistent, BufferDescription{});
        INIT_INPUT_DESC(FRAMEBUFFERS, "framebuffers", ResourceLifetime::Transient, BufferDescription{});

        INIT_INPUT_DESC(IMAGE_INDEX, "image_index", ResourceLifetime::Transient, BufferDescription{});
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, BufferDescription{});

        HandleDescriptor fenceDesc{"VkFence"};
        INIT_INPUT_DESC(IN_FLIGHT_FENCE, "in_flight_fence", ResourceLifetime::Transient, fenceDesc);

        HandleDescriptor semaphoreArrayDesc{"VkSemaphore*"};
        INIT_INPUT_DESC(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, "image_available_semaphores_array",
            ResourceLifetime::Persistent, semaphoreArrayDesc);

        HandleDescriptor timelineSemDesc{"VkSemaphore"};
        INIT_INPUT_DESC(TIMELINE_SEMAPHORE_IN, "timeline_semaphore_in", ResourceLifetime::Persistent, timelineSemDesc);

        HandleDescriptor frameBaseDesc{"uint64_t"};
        INIT_INPUT_DESC(TIMELINE_FRAME_BASE_IN, "timeline_frame_base_in", ResourceLifetime::Transient, frameBaseDesc);

        HandleDescriptor compositeWaitDesc{"VkSemaphore"};
        INIT_INPUT_DESC(COMPOSITE_WAIT_SEMAPHORE, "composite_wait_semaphore", ResourceLifetime::Transient, compositeWaitDesc);

        BufferDescriptor skyPointsDesc{};
        skyPointsDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(SKY_POINTS_BUFFER, "sky_points_buffer", ResourceLifetime::Persistent, skyPointsDesc);

        INIT_OUTPUT_DESC(SKY_POINT_COUNT, "sky_point_count", ResourceLifetime::Transient, BufferDescription{});

        HandleDescriptor renderCompleteDesc{"VkSemaphore"};
        INIT_OUTPUT_DESC(RENDER_COMPLETE_SEMAPHORE, "render_complete_semaphore", ResourceLifetime::Transient, renderCompleteDesc);
    }

    VALIDATE_NODE_CONFIG(SkyProjectionNodeConfig, SkyProjectionNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(SWAPCHAIN_INFO_Slot::index == 2, "SWAPCHAIN_INFO must be at index 2");
    static_assert(CAMERA_DATA_Slot::index == 3, "CAMERA_DATA must be at index 3");
    static_assert(RENDER_PASS_Slot::index == 4, "RENDER_PASS must be at index 4");
    static_assert(FRAMEBUFFERS_Slot::index == 5, "FRAMEBUFFERS must be at index 5");
    static_assert(IMAGE_INDEX_Slot::index == 6, "IMAGE_INDEX must be at index 6");
    static_assert(CURRENT_FRAME_INDEX_Slot::index == 7, "CURRENT_FRAME_INDEX must be at index 7");
    static_assert(IN_FLIGHT_FENCE_Slot::index == 8, "IN_FLIGHT_FENCE must be at index 8");
    static_assert(IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::index == 9, "IMAGE_AVAILABLE_SEMAPHORES_ARRAY must be at index 9");
    static_assert(IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::nullable, "IMAGE_AVAILABLE_SEMAPHORES_ARRAY is optional");
    static_assert(TIMELINE_SEMAPHORE_IN_Slot::index == 10, "TIMELINE_SEMAPHORE_IN must be at index 10");
    static_assert(TIMELINE_SEMAPHORE_IN_Slot::nullable, "TIMELINE_SEMAPHORE_IN is optional");
    static_assert(TIMELINE_FRAME_BASE_IN_Slot::index == 11, "TIMELINE_FRAME_BASE_IN must be at index 11");
    static_assert(TIMELINE_FRAME_BASE_IN_Slot::nullable, "TIMELINE_FRAME_BASE_IN is optional");
    static_assert(COMPOSITE_WAIT_SEMAPHORE_Slot::index == 12, "COMPOSITE_WAIT_SEMAPHORE must be at index 12");
    static_assert(COMPOSITE_WAIT_SEMAPHORE_Slot::nullable, "COMPOSITE_WAIT_SEMAPHORE is optional");

    static_assert(SKY_POINTS_BUFFER_Slot::index == 0, "SKY_POINTS_BUFFER must be at index 0");
    static_assert(SKY_POINT_COUNT_Slot::index == 1, "SKY_POINT_COUNT must be at index 1");
    static_assert(RENDER_COMPLETE_SEMAPHORE_Slot::index == 2, "RENDER_COMPLETE_SEMAPHORE must be at index 2");

    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<SWAPCHAIN_INFO_Slot::Type, Vixen::Vulkan::Resources::IRenderTarget*>);
    static_assert(std::is_same_v<CAMERA_DATA_Slot::Type, const CameraData&>);
    static_assert(std::is_same_v<RENDER_PASS_Slot::Type, VkRenderPass>);
    static_assert(std::is_same_v<FRAMEBUFFERS_Slot::Type, std::vector<VkFramebuffer>>);
    static_assert(std::is_same_v<IMAGE_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<IN_FLIGHT_FENCE_Slot::Type, VkFence>);
    static_assert(std::is_same_v<IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::Type, const std::vector<VkSemaphore>&>);
    static_assert(std::is_same_v<TIMELINE_SEMAPHORE_IN_Slot::Type, VkSemaphore>);
    static_assert(std::is_same_v<TIMELINE_FRAME_BASE_IN_Slot::Type, uint64_t>);
    static_assert(std::is_same_v<COMPOSITE_WAIT_SEMAPHORE_Slot::Type, VkSemaphore>);
    static_assert(std::is_same_v<SKY_POINTS_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<SKY_POINT_COUNT_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORE_Slot::Type, VkSemaphore>);
};

} // namespace Vixen::RenderGraph
