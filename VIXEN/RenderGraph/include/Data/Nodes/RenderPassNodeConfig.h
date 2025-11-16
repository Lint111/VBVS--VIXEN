#pragma once

#include "Data/Core/ResourceConfig.h"
#include "Core/NodeInstance.h"
#include "VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

/**
 * @brief Pure constexpr resource configuration for RenderPassNode
 *
 * Inputs:
 * - VULKAN_DEVICE (VulkanDevice*) - VulkanDevice pointer
 * - COLOR_FORMAT (VkFormat) - Color attachment format (from SwapChainNode)
 * - DEPTH_FORMAT (VkFormat) - Depth attachment format (from DepthBufferNode, nullable)
 *
 * Outputs:
 * - RENDER_PASS (VkRenderPass) - Render pass handle
 * - DEVICE_OUT (VkDevice) - Device handle for passthrough
 *
 * Parameters:
 * - COLOR_LOAD_OP (AttachmentLoadOp enum) - Color load operation
 * - COLOR_STORE_OP (AttachmentStoreOp enum) - Color store operation
 * - DEPTH_LOAD_OP (AttachmentLoadOp enum) - Depth load operation
 * - DEPTH_STORE_OP (AttachmentStoreOp enum) - Depth store operation
 * - INITIAL_LAYOUT (ImageLayout enum) - Initial image layout
 * - FINAL_LAYOUT (ImageLayout enum) - Final image layout
 * - SAMPLES (uint32_t) - MSAA sample count
 *
 * ALL type checking happens at compile time!
 */
// Compile-time slot counts (declared early for reuse)
namespace RenderPassNodeCounts {
    static constexpr size_t INPUTS = 3;
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(RenderPassNodeConfig, 
                      RenderPassNodeCounts::INPUTS, 
                      RenderPassNodeCounts::OUTPUTS, 
                      RenderPassNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* PARAM_COLOR_LOAD_OP = "color_load_op";
    static constexpr const char* PARAM_COLOR_STORE_OP = "color_store_op";
    static constexpr const char* PARAM_DEPTH_LOAD_OP = "depth_load_op";
    static constexpr const char* PARAM_DEPTH_STORE_OP = "depth_store_op";
    static constexpr const char* PARAM_INITIAL_LAYOUT = "initial_layout";
    static constexpr const char* PARAM_FINAL_LAYOUT = "final_layout";
    static constexpr const char* PARAM_SAMPLES = "samples";

    // ===== INPUTS (3) =====
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(SWAPCHAIN_INFO, SwapChainPublicVariables*, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(DEPTH_FORMAT, VkFormat, 2,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (2) =====
    OUTPUT_SLOT(RENDER_PASS, VkRenderPass, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevice*, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    RenderPassNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor swapchainInfoDesc{"SwapChainPublicVariables*"};
        INIT_INPUT_DESC(SWAPCHAIN_INFO, "swapchain_info",
            ResourceLifetime::Persistent,
            swapchainInfoDesc
        );

        INIT_INPUT_DESC(DEPTH_FORMAT, "depth_format",
            ResourceLifetime::Transient,
            BufferDescription{}
        );

        // Initialize output descriptors
        INIT_OUTPUT_DESC(RENDER_PASS, "render_pass",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        HandleDescriptor deviceOutDesc{"VkDevice"};
        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device", ResourceLifetime::Persistent, deviceOutDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(RenderPassNodeConfig, RenderPassNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE is required");

    static_assert(SWAPCHAIN_INFO_Slot::index == 1, "SWAPCHAIN_INFO must be at index 1");
    static_assert(!SWAPCHAIN_INFO_Slot::nullable, "SWAPCHAIN_INFO is required");

    static_assert(DEPTH_FORMAT_Slot::index == 2, "DEPTH_FORMAT must be at index 2");
    static_assert(DEPTH_FORMAT_Slot::nullable, "DEPTH_FORMAT is optional");

    static_assert(RENDER_PASS_Slot::index == 0, "RENDER_PASS must be at index 0");
    static_assert(!RENDER_PASS_Slot::nullable, "RENDER_PASS is required");

    static_assert(VULKAN_DEVICE_OUT_Slot::index == 1, "DEVICE_OUT must be at index 1");
    static_assert(!VULKAN_DEVICE_OUT_Slot::nullable, "DEVICE_OUT is required");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<SWAPCHAIN_INFO_Slot::Type, SwapChainPublicVariables*>);
    static_assert(std::is_same_v<DEPTH_FORMAT_Slot::Type, VkFormat>);
    static_assert(std::is_same_v<RENDER_PASS_Slot::Type, VkRenderPass>);
    static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevice*>);
};

} // namespace Vixen::RenderGraph
