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
namespace SelectionCoordinatorNodeCounts {
    static constexpr size_t INPUTS = 8;   // INPUT_STATE, ID_IMAGE, VULKAN_DEVICE, COMMAND_POOL, CURRENT_FRAME_INDEX, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, CAMERA_DATA
    static constexpr size_t OUTPUTS = 1;   // SELECTION_COUNT (status)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for SelectionCoordinatorNode (SEL-P2).
 *
 * The engine-wide selection coordinator (generalizes the shipped PickingNode). On
 * a left-click down-edge it builds a SelectContext (crosshair screen point, viewport,
 * camera, input modifier), runs its registered ISelectionProviders in priority order
 * (highest first — UI occludes world), takes the first/topmost Hit, applies the
 * modifier to its owned SelectionSet, and broadcasts a SelectionChangedEvent on the
 * message bus. Today the only registered provider is VoxelSelectionProvider, which
 * performs the GPU ID-buffer readback the PickingNode used to do inline.
 *
 * The same graph inputs the PickingNode had are kept; CAMERA_DATA is added so the
 * SelectContext can carry the camera for future ray-based providers (the voxel
 * provider ignores it). The readback itself happens only on the click edge.
 *
 * Inputs (8):
 *   - INPUT_STATE         (InputState*, Execute)        cursor + mouse buttons + modifier keys
 *   - ID_IMAGE            (VkImage, Dependency)         current-frame pick-ID image (PickIdTargetNode.ID_IMAGE)
 *   - VULKAN_DEVICE       (VulkanDevice*, Dependency)   device for the one-shot copy + staging buffer
 *   - COMMAND_POOL        (VkCommandPool, Dependency)   pool for the one-shot readback command buffer
 *   - CURRENT_FRAME_INDEX (uint32_t, Execute)           frame-in-flight index (diagnostics / future ring use)
 *   - VIEWPORT_WIDTH      (uint32_t, Execute)           swapchain width  (WindowNode WIDTH_OUT)
 *   - VIEWPORT_HEIGHT     (uint32_t, Execute)           swapchain height (WindowNode HEIGHT_OUT)
 *   - CAMERA_DATA         (const CameraData&, Execute)  camera for the SelectContext (future ray providers)
 *
 * Outputs (1):
 *   - SELECTION_COUNT (uint32_t)   size of the current SelectionSet after the last pick.
 *
 * NOTE on output count: the graph's topological sort visits ALL added nodes, so a pure
 * 0-output sink WOULD still execute, and VALIDATE_NODE_CONFIG permits OUTPUTS == 0. We
 * nonetheless expose SELECTION_COUNT so the selection size is a first-class graph value a
 * future node (e.g. a highlight pass) can consume directly. The authoritative selection
 * still flows via SelectionChangedEvent on the bus (the coordinator owns the set).
 *
 * ID_IMAGE / VULKAN_DEVICE / COMMAND_POOL are consumed as Dependencies (stable for the
 * cached scene's lifetime); the coordinator caches them in CompileImpl and configures the
 * voxel provider with them. CURRENT_FRAME_INDEX is kept for diagnostics / previous-frame
 * fallback. CAMERA_DATA is Required (CameraNode writes it every Execute).
 */
CONSTEXPR_NODE_CONFIG(SelectionCoordinatorNodeConfig,
                      SelectionCoordinatorNodeCounts::INPUTS,
                      SelectionCoordinatorNodeCounts::OUTPUTS,
                      SelectionCoordinatorNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (8) =====
    // Per-frame inputs (InputState, frame index, viewport, camera) use SlotRole::Execute + ReadOnly
    // (mirrors how CameraNode declares its Execute inputs) so they are read during Execute.
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

    // Camera for the SelectContext (future ray-based providers unproject through it). Required —
    // CameraNode emits a CameraData every Execute. The voxel provider does not read it.
    INPUT_SLOT(CAMERA_DATA, const CameraData&, 7,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (1) =====
    OUTPUT_SLOT(SELECTION_COUNT, uint32_t, 0,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // Constructor for runtime descriptor initialization
    SelectionCoordinatorNodeConfig() {
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

        HandleDescriptor cameraDataDesc{"CameraDataPtr"};
        INIT_INPUT_DESC(CAMERA_DATA, "camera_data", ResourceLifetime::Persistent, cameraDataDesc);

        HandleDescriptor selectionCountDesc{"uint32_t"};
        INIT_OUTPUT_DESC(SELECTION_COUNT, "selection_count", ResourceLifetime::Transient, selectionCountDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(SelectionCoordinatorNodeConfig, SelectionCoordinatorNodeCounts);

    // Slot index validations
    static_assert(INPUT_STATE_Slot::index == 0, "INPUT_STATE must be at index 0");
    static_assert(ID_IMAGE_Slot::index == 1, "ID_IMAGE must be at index 1");
    static_assert(VULKAN_DEVICE_Slot::index == 2, "VULKAN_DEVICE must be at index 2");
    static_assert(COMMAND_POOL_Slot::index == 3, "COMMAND_POOL must be at index 3");
    static_assert(CURRENT_FRAME_INDEX_Slot::index == 4, "CURRENT_FRAME_INDEX must be at index 4");
    static_assert(VIEWPORT_WIDTH_Slot::index == 5, "VIEWPORT_WIDTH must be at index 5");
    static_assert(VIEWPORT_HEIGHT_Slot::index == 6, "VIEWPORT_HEIGHT must be at index 6");
    static_assert(CAMERA_DATA_Slot::index == 7, "CAMERA_DATA must be at index 7");
    static_assert(SELECTION_COUNT_Slot::index == 0, "SELECTION_COUNT must be at index 0");

    // Type validations
    static_assert(std::is_same_v<INPUT_STATE_Slot::Type, InputStatePtr>);
    static_assert(std::is_same_v<ID_IMAGE_Slot::Type, VkImage>);
    static_assert(std::is_same_v<VULKAN_DEVICE_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<VIEWPORT_WIDTH_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<VIEWPORT_HEIGHT_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<CAMERA_DATA_Slot::Type, const CameraData&>);
    static_assert(std::is_same_v<SELECTION_COUNT_Slot::Type, uint32_t>);
};

} // namespace Vixen::RenderGraph
