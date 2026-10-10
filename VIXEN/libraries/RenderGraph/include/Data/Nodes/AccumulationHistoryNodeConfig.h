#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"
#include <vulkan/vulkan.h>

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts (declared early for reuse)
namespace AccumulationHistoryNodeCounts {
    static constexpr size_t INPUTS  = 4;  // VULKAN_DEVICE_IN, COMMAND_POOL, WIDTH, HEIGHT
    static constexpr size_t OUTPUTS = 4;  // previous/current history views and images
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for AccumulationHistoryNode
 * (Sampled Lighting Inc2 M1)
 *
 * Allocates a pair of persistent STORAGE images for immutable previous-frame
 * history and a distinct current-frame output. The two image roles swap once
 * per graph execution; see AccumulationHistoryNode.h for the lifetime rationale.
 * Both images are sized to the render target's extent and use
 * VK_FORMAT_R16G16B16A16_SFLOAT so they preserve the scene-linear HDR radiance
 * that the display transform consumes.
 *
 * Inputs: 4
 *   - VULKAN_DEVICE_IN  (VulkanDevice*)  Device for allocation + the one-shot transition queue
 *   - COMMAND_POOL      (VkCommandPool)  Pool for the one-shot transition command buffer
 *   - WIDTH             (uint32_t)       Image width  (RenderTargetNode WIDTH_OUT -- the RENDER
 *                                         extent, matching outputImage's own bounds, not the window)
 *   - HEIGHT            (uint32_t)       Image height (RenderTargetNode HEIGHT_OUT)
 * Outputs: 4
 *   - HISTORY_IMAGE_VIEW         (VkImageView) Previous-frame image view
 *   - HISTORY_IMAGE              (VkImage)     Previous-frame image handle
 *   - CURRENT_HISTORY_IMAGE_VIEW (VkImageView) Current-frame output view
 *   - CURRENT_HISTORY_IMAGE      (VkImage)     Current-frame output handle
 *
 * Layout: the compute shader will use the image as a STORAGE image (VK_IMAGE_LAYOUT_GENERAL). The
 * node performs a one-time UNDEFINED -> GENERAL transition at Compile (storage images stay GENERAL
 * thereafter), mirroring PickIdTargetNode's own transition pattern.
 *
 * Lifecycle: both images persist across graph recompile (same extent); released only on
 * FinalTeardown. A genuine resize recreates the pair at the new extent. The accumulation config
 * resets the frame counter after recompile, so the first frame uses alpha >= 1 and skips history
 * reads before populating the new pair.
 */
CONSTEXPR_NODE_CONFIG(AccumulationHistoryNodeConfig,
                      AccumulationHistoryNodeCounts::INPUTS,
                      AccumulationHistoryNodeCounts::OUTPUTS,
                      AccumulationHistoryNodeCounts::ARRAY_MODE) {

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
    OUTPUT_SLOT(HISTORY_IMAGE_VIEW, VkImageView, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(HISTORY_IMAGE, VkImage, 1,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(CURRENT_HISTORY_IMAGE_VIEW, VkImageView, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(CURRENT_HISTORY_IMAGE, VkImage, 3,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // ----- Constructor: runtime descriptor initialization -----
    AccumulationHistoryNodeConfig() {
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        HandleDescriptor poolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, poolDesc);

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(WIDTH,  "width",  ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(HEIGHT, "height", ResourceLifetime::Transient, uint32Desc);

        // Each output role selects one of the pair's persistent views/images for this frame.
        HandleDescriptor viewDesc{"VkImageView"};
        INIT_OUTPUT_DESC(HISTORY_IMAGE_VIEW, "history_image_view", ResourceLifetime::Persistent, viewDesc);
        INIT_OUTPUT_DESC(CURRENT_HISTORY_IMAGE_VIEW, "current_history_image_view", ResourceLifetime::Persistent, viewDesc);

        HandleDescriptor imageDesc{"VkImage"};
        INIT_OUTPUT_DESC(HISTORY_IMAGE, "history_image", ResourceLifetime::Persistent, imageDesc);
        INIT_OUTPUT_DESC(CURRENT_HISTORY_IMAGE, "current_history_image", ResourceLifetime::Persistent, imageDesc);
    }

    // ----- Compile-time validation -----
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE_IN must not be nullable");
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>,
                  "VULKAN_DEVICE_IN must be VulkanDevice*");

    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(WIDTH_Slot::index  == 2, "WIDTH must be at index 2");
    static_assert(HEIGHT_Slot::index == 3, "HEIGHT must be at index 3");

    static_assert(HISTORY_IMAGE_VIEW_Slot::index == 0, "HISTORY_IMAGE_VIEW must be at index 0");
    static_assert(!HISTORY_IMAGE_VIEW_Slot::nullable, "HISTORY_IMAGE_VIEW must not be nullable");
    static_assert(std::is_same_v<HISTORY_IMAGE_VIEW_Slot::Type, VkImageView>,
                  "HISTORY_IMAGE_VIEW must be VkImageView");
    static_assert(HISTORY_IMAGE_Slot::index == 1, "HISTORY_IMAGE must be at index 1");
    static_assert(CURRENT_HISTORY_IMAGE_VIEW_Slot::index == 2,
                  "CURRENT_HISTORY_IMAGE_VIEW must be at index 2");
    static_assert(CURRENT_HISTORY_IMAGE_Slot::index == 3,
                  "CURRENT_HISTORY_IMAGE must be at index 3");

    VALIDATE_NODE_CONFIG(AccumulationHistoryNodeConfig, AccumulationHistoryNodeCounts);
};

} // namespace Vixen::RenderGraph
