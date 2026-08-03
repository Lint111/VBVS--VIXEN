#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"
#include <vulkan/vulkan.h>

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

namespace DepthTargetNodeCounts {
    static constexpr size_t INPUTS  = 5;  // VULKAN_DEVICE_IN, COMMAND_POOL, WIDTH, HEIGHT, CURRENT_FRAME_INDEX
    static constexpr size_t OUTPUTS = 2;  // DEPTH_WRITE_VIEW, DEPTH_READ_VIEW
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for DepthTargetNode (Raster-proxy B1 M4).
 *
 * TWO persistent R32_SFLOAT storage images ping-ponged by frame parity: the
 * march writes euclidean ray distance into slot [frame&1] (binding 36,
 * VIXEN_B1_OCCLUSION_CULL) while the HiZ reduce reads LAST frame's slot
 * [(frame+1)&1]. Distinct VkImage/Resource* per slot means the FrameSyncScheduler
 * needs NO march↔reduce sync edge at all — the shell double-buffer precedent
 * (BodyOctreeSceneNode's SHELL_DATA read-slot re-emission), applied to images.
 *
 * Both slots are cleared to the 1e30 miss sentinel ONCE at creation, so frame 0's
 * reduce sees "everything sky" and the cull conservatively skips nothing.
 *
 * Inputs: 5
 *   - VULKAN_DEVICE_IN     (VulkanDevice*)  Device for allocation + one-shot transition/clear
 *   - COMMAND_POOL         (VkCommandPool)  Pool for the one-shot command buffer
 *   - WIDTH                (uint32_t)       Image width  (RenderTargetNode WIDTH_OUT)
 *   - HEIGHT               (uint32_t)       Image height (RenderTargetNode HEIGHT_OUT)
 *   - CURRENT_FRAME_INDEX  (uint32_t, Execute) Selects which slot is write vs read
 * Outputs: 2 (re-emitted each Execute by parity, like PickIdTargetNode's ring view)
 *   - DEPTH_WRITE_VIEW (VkImageView)  This frame's march-write slot [frame&1]
 *   - DEPTH_READ_VIEW  (VkImageView)  Last frame's slot [(frame+1)&1] for the HiZ reduce
 */
CONSTEXPR_NODE_CONFIG(DepthTargetNodeConfig,
                      DepthTargetNodeCounts::INPUTS,
                      DepthTargetNodeCounts::OUTPUTS,
                      DepthTargetNodeCounts::ARRAY_MODE) {

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

    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 4,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ----- Output slots -----
    OUTPUT_SLOT(DEPTH_WRITE_VIEW, VkImageView, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(DEPTH_READ_VIEW, VkImageView, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ----- Constructor: runtime descriptor initialization -----
    DepthTargetNodeConfig() {
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        HandleDescriptor poolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, poolDesc);

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(WIDTH,  "width",  ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(HEIGHT, "height", ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, uint32Desc);

        HandleDescriptor viewDesc{"VkImageView"};
        INIT_OUTPUT_DESC(DEPTH_WRITE_VIEW, "depth_write_view", ResourceLifetime::Transient, viewDesc);
        INIT_OUTPUT_DESC(DEPTH_READ_VIEW,  "depth_read_view",  ResourceLifetime::Transient, viewDesc);
    }

    // ----- Compile-time validation -----
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(WIDTH_Slot::index  == 2, "WIDTH must be at index 2");
    static_assert(HEIGHT_Slot::index == 3, "HEIGHT must be at index 3");
    static_assert(CURRENT_FRAME_INDEX_Slot::index == 4, "CURRENT_FRAME_INDEX must be at index 4");
    static_assert(DEPTH_WRITE_VIEW_Slot::index == 0, "DEPTH_WRITE_VIEW must be at index 0");
    static_assert(DEPTH_READ_VIEW_Slot::index == 1, "DEPTH_READ_VIEW must be at index 1");
    static_assert(std::is_same_v<DEPTH_WRITE_VIEW_Slot::Type, VkImageView>,
                  "DEPTH_WRITE_VIEW must be VkImageView");

    VALIDATE_NODE_CONFIG(DepthTargetNodeConfig, DepthTargetNodeCounts);
};

} // namespace Vixen::RenderGraph
