#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"
#include <vulkan/vulkan.h>

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts (declared early for reuse)
namespace WorldPosHistoryNodeCounts {
    static constexpr size_t INPUTS  = 4;  // VULKAN_DEVICE_IN, COMMAND_POOL, WIDTH, HEIGHT
    static constexpr size_t OUTPUTS = 4;  // previous/current world-position views and images
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for WorldPosHistoryNode
 * (Sampled Lighting Inc3 M2 -- KI-023 prerequisite)
 *
 * Allocates paired persistent STORAGE images (rgba32f: worldPos.xyz in .xyz,
 * hitT/depth in .w). One is the immutable previous-frame image and the other
 * is the current-frame output; their roles swap once per graph execution.
 *
 * Written each frame by SpatialReuseShade.comp alongside the current radiance output;
 * read back from the immutable previous image at the reprojected texel to replace the color-consistency reject
 * (KI-023) with a geometric one. Also the shared primitive Inc3's ReSTIR
 * reservoir-reprojection validity (M4/M5) will reuse -- one buffer, two
 * consumers (see the plan's design note).
 *
 * Inputs: 4
 *   - VULKAN_DEVICE_IN  (VulkanDevice*)  Device for allocation + the one-shot transition queue
 *   - COMMAND_POOL      (VkCommandPool)  Pool for the one-shot transition command buffer
 *   - WIDTH             (uint32_t)       Image width  (RenderTargetNode WIDTH_OUT)
 *   - HEIGHT            (uint32_t)       Image height (RenderTargetNode HEIGHT_OUT)
 * Outputs: 4
 *   - WORLDPOS_IMAGE_VIEW         (VkImageView) Previous-frame image view
 *   - WORLDPOS_IMAGE              (VkImage)     Previous-frame image handle
 *   - CURRENT_WORLDPOS_IMAGE_VIEW (VkImageView) Current-frame output view
 *   - CURRENT_WORLDPOS_IMAGE      (VkImage)     Current-frame output handle
 *
 * Layout: same one-time UNDEFINED -> GENERAL transition at Compile as
 * AccumulationHistoryNode; storage images stay GENERAL thereafter.
 *
 * Lifecycle: persists across graph recompile (same extent); released only on
 * FinalTeardown. A genuine resize recreates the image at the new extent.
 */
CONSTEXPR_NODE_CONFIG(WorldPosHistoryNodeConfig,
                      WorldPosHistoryNodeCounts::INPUTS,
                      WorldPosHistoryNodeCounts::OUTPUTS,
                      WorldPosHistoryNodeCounts::ARRAY_MODE) {

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

    // ----- Output slots -----
    OUTPUT_SLOT(WORLDPOS_IMAGE_VIEW, VkImageView, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(WORLDPOS_IMAGE, VkImage, 1,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(CURRENT_WORLDPOS_IMAGE_VIEW, VkImageView, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(CURRENT_WORLDPOS_IMAGE, VkImage, 3,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // ----- Constructor: runtime descriptor initialization -----
    WorldPosHistoryNodeConfig() {
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        HandleDescriptor poolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, poolDesc);

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(WIDTH,  "width",  ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(HEIGHT, "height", ResourceLifetime::Transient, uint32Desc);

        // Each output role selects one of the pair's persistent views/images for this frame.
        HandleDescriptor viewDesc{"VkImageView"};
        INIT_OUTPUT_DESC(WORLDPOS_IMAGE_VIEW, "worldpos_image_view", ResourceLifetime::Persistent, viewDesc);
        INIT_OUTPUT_DESC(CURRENT_WORLDPOS_IMAGE_VIEW, "current_worldpos_image_view", ResourceLifetime::Persistent, viewDesc);

        HandleDescriptor imageDesc{"VkImage"};
        INIT_OUTPUT_DESC(WORLDPOS_IMAGE, "worldpos_image", ResourceLifetime::Persistent, imageDesc);
        INIT_OUTPUT_DESC(CURRENT_WORLDPOS_IMAGE, "current_worldpos_image", ResourceLifetime::Persistent, imageDesc);
    }

    // ----- Compile-time validation -----
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE_IN must not be nullable");
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>,
                  "VULKAN_DEVICE_IN must be VulkanDevice*");

    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(WIDTH_Slot::index  == 2, "WIDTH must be at index 2");
    static_assert(HEIGHT_Slot::index == 3, "HEIGHT must be at index 3");

    static_assert(WORLDPOS_IMAGE_VIEW_Slot::index == 0, "WORLDPOS_IMAGE_VIEW must be at index 0");
    static_assert(!WORLDPOS_IMAGE_VIEW_Slot::nullable, "WORLDPOS_IMAGE_VIEW must not be nullable");
    static_assert(std::is_same_v<WORLDPOS_IMAGE_VIEW_Slot::Type, VkImageView>,
                  "WORLDPOS_IMAGE_VIEW must be VkImageView");
    static_assert(WORLDPOS_IMAGE_Slot::index == 1, "WORLDPOS_IMAGE must be at index 1");
    static_assert(CURRENT_WORLDPOS_IMAGE_VIEW_Slot::index == 2,
                  "CURRENT_WORLDPOS_IMAGE_VIEW must be at index 2");
    static_assert(CURRENT_WORLDPOS_IMAGE_Slot::index == 3,
                  "CURRENT_WORLDPOS_IMAGE must be at index 3");

    VALIDATE_NODE_CONFIG(WorldPosHistoryNodeConfig, WorldPosHistoryNodeCounts);
};

} // namespace Vixen::RenderGraph
