#pragma once

#include "Core/ResourceConfig.h"
#include "Core/NodeInstance.h"  // For DepthFormat enum
#include "VulkanResources/VulkanDevice.h"
#include "Core/ResourceVariant.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;

/**
 * @brief Pure constexpr resource configuration for DepthBufferNode
 *
 * Inputs:
 * - WIDTH (uint32_t) - Depth buffer width (from SwapChainNode)
 * - HEIGHT (uint32_t) - Depth buffer height (from SwapChainNode)
 * - COMMAND_POOL (VkCommandPool) - Command pool for layout transition
 *
 * Outputs:
 * - DEPTH_IMAGE (VkImage) - Depth image handle
 * - DEPTH_IMAGE_VIEW (VkImageView) - Depth image view
 * - DEPTH_FORMAT (VkFormat) - Depth format used
 *
 * Parameters:
 * - FORMAT (DepthFormat enum) - Depth buffer format (D16, D24S8, D32)
 *
 * ALL type checking happens at compile time!
 */
// Compile-time slot counts (declared early for reuse)
namespace DepthBufferNodeCounts {
    static constexpr size_t INPUTS = 3;
    static constexpr size_t OUTPUTS = 3;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(DepthBufferNodeConfig,
                      DepthBufferNodeCounts::INPUTS,
                      DepthBufferNodeCounts::OUTPUTS,
                      DepthBufferNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* PARAM_FORMAT = "format";
    // ===== INPUTS (4) =====
    // VulkanDevice pointer (contains device, gpu, memory properties, etc.)
    CONSTEXPR_INPUT(VULKAN_DEVICE_IN, VulkanDevicePtr, 0, false);

    // Width and height from SwapChainNode
    CONSTEXPR_INPUT(SWAPCHAIN_PUBLIC_VARS, SwapChainPublicVariablesPtr, 1, false);

    // Command pool for layout transition
    CONSTEXPR_INPUT(COMMAND_POOL, VkCommandPool, 2, false);

    // ===== OUTPUTS (3) =====
    // Depth image
    CONSTEXPR_OUTPUT(DEPTH_IMAGE, VkImage, 0, false);

    // Depth image view (for framebuffer attachment)
    CONSTEXPR_OUTPUT(DEPTH_IMAGE_VIEW, VkImageView, 1, false);

    // Depth format (for render pass creation)
    CONSTEXPR_OUTPUT(DEPTH_FORMAT, VkFormat, 2, false);

    DepthBufferNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        INIT_INPUT_DESC(SWAPCHAIN_PUBLIC_VARS, "swapchain_public_vars",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(COMMAND_POOL, "command_pool",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        // Initialize output descriptors
        ImageDescription depthImgDesc{};
        depthImgDesc.width = 0;  // Set from input
        depthImgDesc.height = 0; // Set from input
        depthImgDesc.format = VK_FORMAT_D32_SFLOAT;
        depthImgDesc.usage = ResourceUsage::DepthStencilAttachment;
        depthImgDesc.tiling = VK_IMAGE_TILING_OPTIMAL;

        INIT_OUTPUT_DESC(DEPTH_IMAGE, "depth_image",
            ResourceLifetime::Transient,
            depthImgDesc
        );

        INIT_OUTPUT_DESC(DEPTH_IMAGE_VIEW, "depth_image_view",
            ResourceLifetime::Transient,
            BufferDescription{}  // Opaque handle
        );

        INIT_OUTPUT_DESC(DEPTH_FORMAT, "depth_format",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Format value
        );
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == DepthBufferNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == DepthBufferNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == DepthBufferNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE input must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE input is required");

    static_assert(SWAPCHAIN_PUBLIC_VARS_Slot::index == 1, "SWAPCHAIN_PUBLIC_VARS input must be at index 1");
    static_assert(!SWAPCHAIN_PUBLIC_VARS_Slot::nullable, "SWAPCHAIN_PUBLIC_VARS input is required");

    static_assert(COMMAND_POOL_Slot::index == 2, "COMMAND_POOL input must be at index 2");
    static_assert(!COMMAND_POOL_Slot::nullable, "COMMAND_POOL input is required");

    static_assert(DEPTH_IMAGE_Slot::index == 0, "DEPTH_IMAGE must be at index 0");
    static_assert(!DEPTH_IMAGE_Slot::nullable, "DEPTH_IMAGE is required");

    static_assert(DEPTH_IMAGE_VIEW_Slot::index == 1, "DEPTH_IMAGE_VIEW must be at index 1");
    static_assert(!DEPTH_IMAGE_VIEW_Slot::nullable, "DEPTH_IMAGE_VIEW is required");

    static_assert(DEPTH_FORMAT_Slot::index == 2, "DEPTH_FORMAT must be at index 2");
    static_assert(!DEPTH_FORMAT_Slot::nullable, "DEPTH_FORMAT is required");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<SWAPCHAIN_PUBLIC_VARS_Slot::Type, SwapChainPublicVariablesPtr>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);

    static_assert(std::is_same_v<DEPTH_IMAGE_Slot::Type, VkImage>);
    static_assert(std::is_same_v<DEPTH_IMAGE_VIEW_Slot::Type, VkImageView>);
    static_assert(std::is_same_v<DEPTH_FORMAT_Slot::Type, VkFormat>);
};

// Global compile-time validations
static_assert(DepthBufferNodeConfig::INPUT_COUNT == 3);
static_assert(DepthBufferNodeConfig::OUTPUT_COUNT == 3);

} // namespace Vixen::RenderGraph
