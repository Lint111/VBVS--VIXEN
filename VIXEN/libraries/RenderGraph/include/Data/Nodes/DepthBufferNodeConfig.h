#pragma once

#include "Data/Core/ResourceConfig.h"
#include "Core/NodeInstance.h"  // For DepthFormat enum
#include "VulkanDeviceFwd.h"
#include "Data/Core/CompileTimeResourceSystem.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

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
    static constexpr size_t OUTPUTS = 4;  // Fixed: 4 outputs (DEPTH_IMAGE, DEPTH_IMAGE_VIEW, DEPTH_FORMAT, VULKAN_DEVICE_OUT)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(DepthBufferNodeConfig,
                      DepthBufferNodeCounts::INPUTS,
                      DepthBufferNodeCounts::OUTPUTS,
                      DepthBufferNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* PARAM_FORMAT = "format";
    // ===== INPUTS (3) =====
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(SWAPCHAIN_PUBLIC_VARS, SwapChainPublicVariables*, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (3) =====
    OUTPUT_SLOT(DEPTH_IMAGE, VkImage, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(DEPTH_IMAGE_VIEW, VkImageView, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(DEPTH_FORMAT, VkFormat, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevice*, 3,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

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
            ResourceLifetime::Transient,
            BufferDescription{}  // Format value
        );

        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device",
            ResourceLifetime::Persistent,
            vulkanDeviceDesc
        );
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(DepthBufferNodeConfig, DepthBufferNodeCounts);

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

    static_assert(VULKAN_DEVICE_OUT_Slot::index == 3, "VULKAN_DEVICE_OUT must be at index 3");
    static_assert(!VULKAN_DEVICE_OUT_Slot::nullable, "VULKAN_DEVICE_OUT must not be nullable");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<SWAPCHAIN_PUBLIC_VARS_Slot::Type, SwapChainPublicVariables*>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);

    static_assert(std::is_same_v<DEPTH_IMAGE_Slot::Type, VkImage>);
    static_assert(std::is_same_v<DEPTH_IMAGE_VIEW_Slot::Type, VkImageView>);
    static_assert(std::is_same_v<DEPTH_FORMAT_Slot::Type, VkFormat>);
};

} // namespace Vixen::RenderGraph


