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
    static constexpr size_t OUTPUTS = 2;  // WORLDPOS_IMAGE_VIEW, WORLDPOS_IMAGE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for WorldPosHistoryNode
 * (Sampled Lighting Inc3 M2 -- KI-023 prerequisite)
 *
 * Allocates the companion worldPos/depth history target: a SINGLE persistent
 * STORAGE image (rgba32f: worldPos.xyz in .xyz, hitT/depth in .w), mirroring
 * AccumulationHistoryNode's own "one persistent resource, not a ring" shape
 * (see AccumulationHistoryNodeConfig.h's file header for the full rationale
 * -- identical here: this buffer must survive ACROSS frames since the
 * reproject branch reads what LAST frame wrote).
 *
 * Written each frame alongside historyImage (binding 20) at the same texel;
 * read back at the reprojected texel to replace the color-consistency reject
 * (KI-023) with a geometric one. Also the shared primitive Inc3's ReSTIR
 * reservoir-reprojection validity (M4/M5) will reuse -- one buffer, two
 * consumers (see the plan's design note).
 *
 * Inputs: 4
 *   - VULKAN_DEVICE_IN  (VulkanDevice*)  Device for allocation + the one-shot transition queue
 *   - COMMAND_POOL      (VkCommandPool)  Pool for the one-shot transition command buffer
 *   - WIDTH             (uint32_t)       Image width  (RenderTargetNode WIDTH_OUT)
 *   - HEIGHT            (uint32_t)       Image height (RenderTargetNode HEIGHT_OUT)
 * Outputs: 2
 *   - WORLDPOS_IMAGE_VIEW (VkImageView)  The persistent worldPos/depth image's view
 *   - WORLDPOS_IMAGE      (VkImage)      The persistent worldPos/depth image
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

    // ----- Constructor: runtime descriptor initialization -----
    WorldPosHistoryNodeConfig() {
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        HandleDescriptor poolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, poolDesc);

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(WIDTH,  "width",  ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(HEIGHT, "height", ResourceLifetime::Transient, uint32Desc);

        // Output: the persistent worldPos/depth image's view/image (Persistent lifetime --
        // constant across frames by design, mirroring AccumulationHistoryNodeConfig).
        HandleDescriptor viewDesc{"VkImageView"};
        INIT_OUTPUT_DESC(WORLDPOS_IMAGE_VIEW, "worldpos_image_view", ResourceLifetime::Persistent, viewDesc);

        HandleDescriptor imageDesc{"VkImage"};
        INIT_OUTPUT_DESC(WORLDPOS_IMAGE, "worldpos_image", ResourceLifetime::Persistent, imageDesc);
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

    VALIDATE_NODE_CONFIG(WorldPosHistoryNodeConfig, WorldPosHistoryNodeCounts);
};

} // namespace Vixen::RenderGraph
