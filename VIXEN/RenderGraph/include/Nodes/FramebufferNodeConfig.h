#pragma once

#include "Core/ResourceConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Pure constexpr resource configuration for FramebufferNode
 *
 * Inputs:
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
CONSTEXPR_NODE_CONFIG(FramebufferNodeConfig, 5, 1, true) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* PARAM_LAYERS = "layers";

    // ===== INPUTS (5) =====
    // Render pass from RenderPassNode
    CONSTEXPR_INPUT(RENDER_PASS, VkRenderPass, 0, false);

    // Color attachments array from SwapChainNode (one per swapchain image)
    CONSTEXPR_INPUT(COLOR_ATTACHMENTS, VkImageView, 1, false);

    // Depth attachment from DepthBufferNode (nullable - may not use depth)
    CONSTEXPR_INPUT(DEPTH_ATTACHMENT, VkImageView, 2, true);

    // Width and height from SwapChainNode
    CONSTEXPR_INPUT(WIDTH, uint32_t, 3, false);
    CONSTEXPR_INPUT(HEIGHT, uint32_t, 4, false);

    // ===== OUTPUTS (1) =====
    // Framebuffer handles (one per swapchain image)
    CONSTEXPR_OUTPUT(FRAMEBUFFERS, VkFramebuffer, 0, false);

    FramebufferNodeConfig() {
        // Initialize input descriptors
        INIT_INPUT_DESC(RENDER_PASS, "render_pass",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(COLOR_ATTACHMENTS, "color_attachments",
            ResourceLifetime::Transient,
            BufferDescription{}
        );

        INIT_INPUT_DESC(DEPTH_ATTACHMENT, "depth_attachment",
            ResourceLifetime::Transient,
            BufferDescription{}
        );

        INIT_INPUT_DESC(WIDTH, "width",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(HEIGHT, "height",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        // Initialize output descriptors
        INIT_OUTPUT_DESC(FRAMEBUFFERS, "framebuffers",
            ResourceLifetime::Transient,
            BufferDescription{}  // Opaque handles
        );
    }

    // Compile-time validations
    static_assert(RENDER_PASS_Slot::index == 0, "RENDER_PASS must be at index 0");
    static_assert(!RENDER_PASS_Slot::nullable, "RENDER_PASS is required");

    static_assert(COLOR_ATTACHMENTS_Slot::index == 1, "COLOR_ATTACHMENTS must be at index 1");
    static_assert(!COLOR_ATTACHMENTS_Slot::nullable, "COLOR_ATTACHMENTS is required");

    static_assert(DEPTH_ATTACHMENT_Slot::index == 2, "DEPTH_ATTACHMENT must be at index 2");
    static_assert(DEPTH_ATTACHMENT_Slot::nullable, "DEPTH_ATTACHMENT is optional");

    static_assert(WIDTH_Slot::index == 3, "WIDTH must be at index 3");
    static_assert(!WIDTH_Slot::nullable, "WIDTH is required");

    static_assert(HEIGHT_Slot::index == 4, "HEIGHT must be at index 4");
    static_assert(!HEIGHT_Slot::nullable, "HEIGHT is required");

    static_assert(FRAMEBUFFERS_Slot::index == 0, "FRAMEBUFFERS must be at index 0");
    static_assert(!FRAMEBUFFERS_Slot::nullable, "FRAMEBUFFERS is required");

    // Type validations
    static_assert(std::is_same_v<RENDER_PASS_Slot::Type, VkRenderPass>);
    static_assert(std::is_same_v<COLOR_ATTACHMENTS_Slot::Type, VkImageView>);
    static_assert(std::is_same_v<DEPTH_ATTACHMENT_Slot::Type, VkImageView>);
    static_assert(std::is_same_v<WIDTH_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<HEIGHT_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<FRAMEBUFFERS_Slot::Type, VkFramebuffer>);
};

// Global compile-time validations
static_assert(FramebufferNodeConfig::INPUT_COUNT == 5);
static_assert(FramebufferNodeConfig::OUTPUT_COUNT == 1);
static_assert(FramebufferNodeConfig::ALLOW_INPUT_ARRAYS);

} // namespace Vixen::RenderGraph