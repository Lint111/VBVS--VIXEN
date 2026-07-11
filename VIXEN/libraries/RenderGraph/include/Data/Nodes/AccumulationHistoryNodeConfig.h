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
    static constexpr size_t OUTPUTS = 2;  // HISTORY_IMAGE_VIEW, HISTORY_IMAGE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for AccumulationHistoryNode
 * (Sampled Lighting Inc2 M1)
 *
 * Allocates the temporal-accumulation history target: a SINGLE persistent
 * STORAGE image (NOT a per-frame-in-flight ring like PickIdTargetNode/
 * RenderTargetNode -- history must survive ACROSS frames by design, so a
 * ring would defeat its whole purpose; see AccumulationHistoryNode.h's file
 * header for the full rationale), sized to the render target's extent and
 * format-matched to outputImage (VK_FORMAT_R8G8B8A8_UNORM, RenderTargetNode's
 * own default -- confirmed from RenderTargetNode.cpp, not rgba16f) so a
 * future blend can read/write it and the swapchain output through the same
 * format contract.
 *
 * Inputs: 4
 *   - VULKAN_DEVICE_IN  (VulkanDevice*)  Device for allocation + the one-shot transition queue
 *   - COMMAND_POOL      (VkCommandPool)  Pool for the one-shot transition command buffer
 *   - WIDTH             (uint32_t)       Image width  (RenderTargetNode WIDTH_OUT -- the RENDER
 *                                         extent, matching outputImage's own bounds, not the window)
 *   - HEIGHT            (uint32_t)       Image height (RenderTargetNode HEIGHT_OUT)
 * Outputs: 2
 *   - HISTORY_IMAGE_VIEW (VkImageView)  The persistent history image's view (constant across frames)
 *   - HISTORY_IMAGE      (VkImage)      The persistent history image (for a future copy/blit if needed)
 *
 * Layout: the compute shader will use the image as a STORAGE image (VK_IMAGE_LAYOUT_GENERAL). The
 * node performs a one-time UNDEFINED -> GENERAL transition at Compile (storage images stay GENERAL
 * thereafter), mirroring PickIdTargetNode's own transition pattern.
 *
 * Lifecycle: the image persists across graph recompile (same extent); released only on
 * FinalTeardown. A genuine resize recreates it at the new extent (M1: uninitialized content on
 * recreate is fine -- accumulation is disabled this milestone, so nothing reads it yet).
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

    // ----- Constructor: runtime descriptor initialization -----
    AccumulationHistoryNodeConfig() {
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        HandleDescriptor poolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, poolDesc);

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(WIDTH,  "width",  ResourceLifetime::Transient, uint32Desc);
        INIT_INPUT_DESC(HEIGHT, "height", ResourceLifetime::Transient, uint32Desc);

        // Output: the persistent history image's view/image (Persistent lifetime -- constant
        // across frames by design, unlike a ring's per-frame-rotating handle).
        HandleDescriptor viewDesc{"VkImageView"};
        INIT_OUTPUT_DESC(HISTORY_IMAGE_VIEW, "history_image_view", ResourceLifetime::Persistent, viewDesc);

        HandleDescriptor imageDesc{"VkImage"};
        INIT_OUTPUT_DESC(HISTORY_IMAGE, "history_image", ResourceLifetime::Persistent, imageDesc);
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

    VALIDATE_NODE_CONFIG(AccumulationHistoryNodeConfig, AccumulationHistoryNodeCounts);
};

} // namespace Vixen::RenderGraph
