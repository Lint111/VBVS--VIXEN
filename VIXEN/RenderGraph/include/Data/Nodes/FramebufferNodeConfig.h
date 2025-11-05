#pragma once

#include "Data/Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;

/**
 * @brief Pure constexpr resource configuration for FramebufferNode
 *
 * Inputs:
 * - VULKAN_DEVICE (VulkanDevice*) - Vulkan device pointer
 * - RENDER_PASS (VkRenderPass) - Render pass from RenderPassNode
 * - COLOR_ATTACHMENTS (VkImageView[]) - Array of color image views from SwapChainNode
 * - DEPTH_ATTACHMENT (VkImageView) - Depth image view from DepthBufferNode (nullable)
 * - WIDTH (uint32_t) - Framebuffer width (from SwapChainNode)
 * - HEIGHT (uint32_t) - Framebuffer height (from SwapChainNode)
 *
 * Outputs:
 * - FRAMEBUFFERS (VkFramebuffer[]) - Array of created framebuffer handles
 *
 * Parameters:
 * - LAYERS (uint32_t) - Number of layers (default: 1)
 *
 * ALL type checking happens at compile time!
 */
// Compile-time slot counts (declared early for reuse)
namespace FramebufferNodeCounts {
    static constexpr size_t INPUTS = 4;
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Array;
}

CONSTEXPR_NODE_CONFIG(FramebufferNodeConfig, 
                      FramebufferNodeCounts::INPUTS, 
                      FramebufferNodeCounts::OUTPUTS, 
                      FramebufferNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* PARAM_LAYERS = "layers";

    // ===== INPUTS (4) =====
    // VulkanDevice pointer (contains device, gpu, memory properties, etc.)
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevicePtr, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Render pass from RenderPassNode
    INPUT_SLOT(RENDER_PASS, VkRenderPass, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Swapchain public variables bundle (contains colorBuffers array)
    INPUT_SLOT(SWAPCHAIN_INFO, SwapChainPublicVariablesPtr, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Depth attachment from DepthBufferNode (nullable - may not use depth)
    INPUT_SLOT(DEPTH_ATTACHMENT, VkImageView, 3,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);


    // ===== OUTPUTS (2) =====
    // Framebuffer handles (vector containing all swapchain framebuffers)
    OUTPUT_SLOT(FRAMEBUFFERS, FramebufferVector, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

	OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevicePtr, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    FramebufferNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);
        
        INIT_INPUT_DESC(RENDER_PASS, "render_pass",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        HandleDescriptor swapchainInfoDesc{"SwapChainPublicVariables*"};
        INIT_INPUT_DESC(SWAPCHAIN_INFO, "swapchain_info",
            ResourceLifetime::Persistent,
            swapchainInfoDesc
        );

        INIT_INPUT_DESC(DEPTH_ATTACHMENT, "depth_attachment",
            ResourceLifetime::Transient,
            BufferDescription{}
        );

        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "VulkanDevice",
            ResourceLifetime::Persistent,
            vulkanDeviceDesc
		);
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == FramebufferNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == FramebufferNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == FramebufferNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE input must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE input is required");

    static_assert(RENDER_PASS_Slot::index == 1, "RENDER_PASS must be at index 1");
    static_assert(!RENDER_PASS_Slot::nullable, "RENDER_PASS is required");

    static_assert(SWAPCHAIN_INFO_Slot::index == 2, "SWAPCHAIN_INFO must be at index 2");
    static_assert(!SWAPCHAIN_INFO_Slot::nullable, "SWAPCHAIN_INFO is required");

    static_assert(DEPTH_ATTACHMENT_Slot::index == 3, "DEPTH_ATTACHMENT must be at index 3");
    static_assert(DEPTH_ATTACHMENT_Slot::nullable, "DEPTH_ATTACHMENT is optional");    

    static_assert(FRAMEBUFFERS_Slot::index == 0, "FRAMEBUFFERS must be at index 0");
    static_assert(!FRAMEBUFFERS_Slot::nullable, "FRAMEBUFFERS is required");

	static_assert(VULKAN_DEVICE_OUT_Slot::index == 1, "VULKAN_DEVICE_OUT must be at index 1");
	static_assert(!VULKAN_DEVICE_OUT_Slot::nullable, "VULKAN_DEVICE_OUT must not be nullable");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<RENDER_PASS_Slot::Type, VkRenderPass>);
    static_assert(std::is_same_v<SWAPCHAIN_INFO_Slot::Type, SwapChainPublicVariablesPtr>);
    static_assert(std::is_same_v<DEPTH_ATTACHMENT_Slot::Type, VkImageView>);
    static_assert(std::is_same_v<FRAMEBUFFERS_Slot::Type, FramebufferVector>);
	static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevicePtr>);
};

// Global compile-time validations
static_assert(FramebufferNodeConfig::INPUT_COUNT == FramebufferNodeCounts::INPUTS);
static_assert(FramebufferNodeConfig::OUTPUT_COUNT == FramebufferNodeCounts::OUTPUTS);
static_assert(FramebufferNodeConfig::ALLOW_INPUT_ARRAYS);

} // namespace Vixen::RenderGraph