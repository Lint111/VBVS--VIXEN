#pragma once
#include "Data/Core/ResourceConfig.h"
#include "Data/InputState.h"
#include "Data/CameraData.h"
#include "VulkanDeviceFwd.h"
#include <vulkan/vulkan.h>

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts
namespace PickingNodeCounts {
    static constexpr size_t INPUTS = 7;   // INPUT_STATE, ID_IMAGE, VULKAN_DEVICE, COMMAND_POOL, CURRENT_FRAME_INDEX, VIEWPORT_WIDTH, VIEWPORT_HEIGHT
    static constexpr size_t OUTPUTS = 1;   // LAST_PICK_HIT (status)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for PickingNode (AR#35 GPU picking, P2) — node type ID 125.
 *
 * GPU ID-buffer click-picking sink. On a left-click press edge it reads back the
 * CENTER pixel (crosshair; the cursor is locked to screen center) of the per-pixel
 * pick-ID image that the voxel compute ray-march wrote at binding 9 (PickIdTargetNode),
 * decodes brick/voxel from the packed pickID, logs the hit/miss and publishes a
 * PickResultEvent on the message bus.
 *
 * The readback is a single-texel vkCmdCopyImageToBuffer on a one-shot command buffer,
 * fenced and waited — only on the click edge, never per-frame. (The CPU ray-march path
 * via GaiaVoxelWorld was removed: the ECS world is null on cached-scene hits, so picking
 * must use the GPU octree the shader already traverses — see the design doc.)
 *
 * Inputs (7):
 *   - INPUT_STATE         (InputState*, Execute)        cursor + mouse buttons
 *   - ID_IMAGE            (VkImage, Dependency)         current-frame pick-ID image (PickIdTargetNode.ID_IMAGE)
 *   - VULKAN_DEVICE       (VulkanDevice*, Dependency)   device for the one-shot copy + staging buffer
 *   - COMMAND_POOL        (VkCommandPool, Dependency)   pool for the one-shot readback command buffer
 *   - CURRENT_FRAME_INDEX (uint32_t, Execute)           frame-in-flight index (diagnostics / future ring use)
 *   - VIEWPORT_WIDTH      (uint32_t, Execute)           swapchain width  (WindowNode WIDTH_OUT)
 *   - VIEWPORT_HEIGHT     (uint32_t, Execute)           swapchain height (WindowNode HEIGHT_OUT)
 *
 * Outputs (1):
 *   - LAST_PICK_HIT   (uint32_t)                 1 if the most recent pick hit a voxel, else 0.
 *
 * NOTE on output count: the graph's topological sort (GraphTopology::TopologicalSort)
 * visits ALL added nodes, so a pure 0-output sink WOULD still execute, and the config
 * macros (VALIDATE_NODE_CONFIG only checks count equality) permit OUTPUTS == 0. We
 * nonetheless expose LAST_PICK_HIT so the pick result is a first-class graph value a
 * future node (e.g. selection highlight) can consume directly — consistent with every
 * other node carrying at least one output. The authoritative result still flows via the
 * PickResultEvent on the bus.
 *
 * ID_IMAGE is consumed as a Dependency (the ID-image ring is stable across the cached
 * scene's lifetime, like VOXEL_WORLD was); the readback chooses the current frame's
 * image at Execute via the cached handle. CURRENT_FRAME_INDEX is kept as an Execute
 * input for diagnostics and to leave the door open for previous-frame fallback.
 */
CONSTEXPR_NODE_CONFIG(PickingNodeConfig,
                      PickingNodeCounts::INPUTS,
                      PickingNodeCounts::OUTPUTS,
                      PickingNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (7) =====
    // Per-frame inputs (InputState, frame index, viewport) use SlotRole::Execute + ReadOnly
    // (mirrors how CameraNode declares INPUT_STATE / IMAGE_INDEX) so they are read during Execute.
    INPUT_SLOT(INPUT_STATE, InputStatePtr, 0,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ID_IMAGE is the per-pixel pick-ID VkImage produced by PickIdTargetNode (binding 9 target).
    // The ID-image ring is stable for the cached scene's lifetime, so consume it as a Dependency
    // (cached in CompileImpl) — the actual single-texel copy happens only on a click edge.
    INPUT_SLOT(ID_IMAGE, VkImage, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Device for the one-shot copy submit (device->queue) + the host-visible staging buffer.
    INPUT_SLOT(VULKAN_DEVICE, VulkanDevice*, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Command pool for the one-shot readback command buffer (CommandPoolNode).
    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 3,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Frame-in-flight index (FrameSyncNode) — Execute role, advances each frame. Kept for
    // diagnostics and a possible previous-completed-frame fallback; the copy uses ID_IMAGE.
    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 4,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(VIEWPORT_WIDTH, uint32_t, 5,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(VIEWPORT_HEIGHT, uint32_t, 6,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (1) =====
    OUTPUT_SLOT(LAST_PICK_HIT, uint32_t, 0,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // Constructor for runtime descriptor initialization
    PickingNodeConfig() {
        HandleDescriptor inputStateDesc{"InputState*"};
        INIT_INPUT_DESC(INPUT_STATE, "input_state", ResourceLifetime::Persistent, inputStateDesc);

        HandleDescriptor idImageDesc{"VkImage"};
        INIT_INPUT_DESC(ID_IMAGE, "id_image", ResourceLifetime::Persistent, idImageDesc);

        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        HandleDescriptor commandPoolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        HandleDescriptor frameIndexDesc{"uint32_t"};
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, frameIndexDesc);

        HandleDescriptor viewportWidthDesc{"uint32_t"};
        INIT_INPUT_DESC(VIEWPORT_WIDTH, "viewport_width", ResourceLifetime::Transient, viewportWidthDesc);

        HandleDescriptor viewportHeightDesc{"uint32_t"};
        INIT_INPUT_DESC(VIEWPORT_HEIGHT, "viewport_height", ResourceLifetime::Transient, viewportHeightDesc);

        HandleDescriptor lastPickHitDesc{"uint32_t"};
        INIT_OUTPUT_DESC(LAST_PICK_HIT, "last_pick_hit", ResourceLifetime::Transient, lastPickHitDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(PickingNodeConfig, PickingNodeCounts);

    // Slot index validations
    static_assert(INPUT_STATE_Slot::index == 0, "INPUT_STATE must be at index 0");
    static_assert(ID_IMAGE_Slot::index == 1, "ID_IMAGE must be at index 1");
    static_assert(VULKAN_DEVICE_Slot::index == 2, "VULKAN_DEVICE must be at index 2");
    static_assert(COMMAND_POOL_Slot::index == 3, "COMMAND_POOL must be at index 3");
    static_assert(CURRENT_FRAME_INDEX_Slot::index == 4, "CURRENT_FRAME_INDEX must be at index 4");
    static_assert(VIEWPORT_WIDTH_Slot::index == 5, "VIEWPORT_WIDTH must be at index 5");
    static_assert(VIEWPORT_HEIGHT_Slot::index == 6, "VIEWPORT_HEIGHT must be at index 6");
    static_assert(LAST_PICK_HIT_Slot::index == 0, "LAST_PICK_HIT must be at index 0");

    // Type validations
    static_assert(std::is_same_v<INPUT_STATE_Slot::Type, InputStatePtr>);
    static_assert(std::is_same_v<ID_IMAGE_Slot::Type, VkImage>);
    static_assert(std::is_same_v<VULKAN_DEVICE_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<VIEWPORT_WIDTH_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<VIEWPORT_HEIGHT_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<LAST_PICK_HIT_Slot::Type, uint32_t>);
};

} // namespace Vixen::RenderGraph
