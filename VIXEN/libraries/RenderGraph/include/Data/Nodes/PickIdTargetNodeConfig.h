#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"
#include <vulkan/vulkan.h>

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts (declared early for reuse)
namespace PickIdTargetNodeCounts {
    static constexpr size_t INPUTS  = 5;  // VULKAN_DEVICE_IN, COMMAND_POOL, WIDTH, HEIGHT, CURRENT_FRAME_INDEX
    static constexpr size_t OUTPUTS = 2;  // ID_IMAGE_VIEW, ID_IMAGE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for PickIdTargetNode (AR#35, GPU picking P1)
 *
 * Allocates an R32_UINT 2D STORAGE image (one per in-flight frame, a ring like the dynamic
 * instance buffer) sized to the swapchain extent, used by the voxel compute ray-march to write
 * a per-pixel hit identity (pickID) at descriptor binding 9. Usage is STORAGE | TRANSFER_SRC
 * (TRANSFER_SRC for the P2 click-readback copy). The compute shader writes the image as a
 * STORAGE image, which requires VK_IMAGE_LAYOUT_GENERAL; the node performs a one-time
 * UNDEFINED -> GENERAL transition of every ring image at Compile (storage images stay GENERAL
 * thereafter — the dispatch's storage writes do not change layout), so no per-frame barrier is
 * needed and the descriptor (always written with imageLayout = GENERAL) is always correct.
 *
 * Inputs: 5
 *   - VULKAN_DEVICE_IN     (VulkanDevice*)  Device for allocation + the one-shot transition queue
 *   - COMMAND_POOL         (VkCommandPool)  Pool for the one-shot transition command buffer
 *   - WIDTH                (uint32_t)       Image width  (M4: RENDER extent — RenderTargetNode WIDTH_OUT,
 *                                            matches the compute shader's idOutputImage bounds, not the window)
 *   - HEIGHT               (uint32_t)       Image height (M4: RenderTargetNode HEIGHT_OUT)
 *   - CURRENT_FRAME_INDEX  (uint32_t, Execute) Advances the ring; selects the exposed view
 * Outputs: 2
 *   - ID_IMAGE_VIEW (VkImageView)  Current in-flight frame's view (re-emitted each Execute, like a ring)
 *   - ID_IMAGE      (VkImage)      Current in-flight frame's image (for the P2 readback copy)
 * (Extent is intentionally not an output: the P2 readback already has WIDTH/HEIGHT; VkExtent2D is
 *  not a supported resource-slot type here, and adding a wrapper for it would be needless surface.)
 *
 * FR-7 lifecycle: images persist across graph recompile; released only on FinalTeardown.
 *
 * Type ID: 126
 */
CONSTEXPR_NODE_CONFIG(PickIdTargetNodeConfig,
                      PickIdTargetNodeCounts::INPUTS,
                      PickIdTargetNodeCounts::OUTPUTS,
                      PickIdTargetNodeCounts::ARRAY_MODE) {

    // ----- Input slots -----
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

    INPUT_SLOT(WIDTH, uint32_t, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(HEIGHT, uint32_t, 3,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // CURRENT_FRAME_INDEX advances the ring each frame (Execute role) and selects the exposed view.
    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 4,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ----- Output slots -----
    OUTPUT_SLOT(ID_IMAGE_VIEW, VkImageView, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(ID_IMAGE, VkImage, 1,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // ----- Constructor: runtime descriptor initialization -----
    PickIdTargetNodeConfig() {
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        HandleDescriptor poolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, poolDesc);

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(WIDTH,  "width",  ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(HEIGHT, "height", ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, uint32Desc);

        // Output: current frame's image view (persistent — same lifetime as the ring images, per FR-7)
        HandleDescriptor viewDesc{"VkImageView"};
        INIT_OUTPUT_DESC(ID_IMAGE_VIEW, "id_image_view", ResourceLifetime::Persistent, viewDesc);

        HandleDescriptor imageDesc{"VkImage"};
        INIT_OUTPUT_DESC(ID_IMAGE, "id_image", ResourceLifetime::Persistent, imageDesc);
    }

    // ----- Compile-time validation -----
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE_IN must not be nullable");
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>,
                  "VULKAN_DEVICE_IN must be VulkanDevice*");

    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(WIDTH_Slot::index  == 2, "WIDTH must be at index 2");
    static_assert(HEIGHT_Slot::index == 3, "HEIGHT must be at index 3");
    static_assert(CURRENT_FRAME_INDEX_Slot::index == 4, "CURRENT_FRAME_INDEX must be at index 4");

    static_assert(ID_IMAGE_VIEW_Slot::index == 0, "ID_IMAGE_VIEW must be at index 0");
    static_assert(!ID_IMAGE_VIEW_Slot::nullable, "ID_IMAGE_VIEW must not be nullable");
    static_assert(std::is_same_v<ID_IMAGE_VIEW_Slot::Type, VkImageView>,
                  "ID_IMAGE_VIEW must be VkImageView");
    static_assert(ID_IMAGE_Slot::index == 1, "ID_IMAGE must be at index 1");

    VALIDATE_NODE_CONFIG(PickIdTargetNodeConfig, PickIdTargetNodeCounts);
};

} // namespace Vixen::RenderGraph
