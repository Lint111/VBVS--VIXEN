#pragma once

#include "Core/ResourceConfig.h"
#include "Core/NodeInstance.h"  // For enums

namespace Vixen::RenderGraph {

/**
 * @brief Pure constexpr resource configuration for RenderPassNode
 *
 * Inputs:
 * - COLOR_FORMAT (VkFormat) - Color attachment format (from SwapChainNode)
 * - DEPTH_FORMAT (VkFormat) - Depth attachment format (from DepthBufferNode, nullable)
 *
 * Outputs:
 * - RENDER_PASS (VkRenderPass) - Render pass handle
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
CONSTEXPR_NODE_CONFIG(RenderPassNodeConfig, 2, 1, false) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* PARAM_COLOR_LOAD_OP = "color_load_op";
    static constexpr const char* PARAM_COLOR_STORE_OP = "color_store_op";
    static constexpr const char* PARAM_DEPTH_LOAD_OP = "depth_load_op";
    static constexpr const char* PARAM_DEPTH_STORE_OP = "depth_store_op";
    static constexpr const char* PARAM_INITIAL_LAYOUT = "initial_layout";
    static constexpr const char* PARAM_FINAL_LAYOUT = "final_layout";
    static constexpr const char* PARAM_SAMPLES = "samples";

    // ===== INPUTS (2) =====
    // Color format from swapchain
    CONSTEXPR_INPUT(COLOR_FORMAT, VkFormat, 0, false);

    // Depth format from depth buffer (nullable - may not have depth)
    CONSTEXPR_INPUT(DEPTH_FORMAT, VkFormat, 1, true);

    // ===== OUTPUTS (1) =====
    // Render pass handle
    CONSTEXPR_OUTPUT(RENDER_PASS, VkRenderPass, 0, false);

    RenderPassNodeConfig() {
        // Initialize input descriptors
        INIT_INPUT_DESC(COLOR_FORMAT, "color_format",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(DEPTH_FORMAT, "depth_format",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        // Initialize output descriptors
        INIT_OUTPUT_DESC(RENDER_PASS, "render_pass",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );
    }

    // Compile-time validations
    static_assert(COLOR_FORMAT_Slot::index == 0, "COLOR_FORMAT must be at index 0");
    static_assert(!COLOR_FORMAT_Slot::nullable, "COLOR_FORMAT is required");

    static_assert(DEPTH_FORMAT_Slot::index == 1, "DEPTH_FORMAT must be at index 1");
    static_assert(DEPTH_FORMAT_Slot::nullable, "DEPTH_FORMAT is optional");

    static_assert(RENDER_PASS_Slot::index == 0, "RENDER_PASS must be at index 0");
    static_assert(!RENDER_PASS_Slot::nullable, "RENDER_PASS is required");

    // Type validations
    static_assert(std::is_same_v<COLOR_FORMAT_Slot::Type, VkFormat>);
    static_assert(std::is_same_v<DEPTH_FORMAT_Slot::Type, VkFormat>);
    static_assert(std::is_same_v<RENDER_PASS_Slot::Type, VkRenderPass>);
};

// Global compile-time validations
static_assert(RenderPassNodeConfig::INPUT_COUNT == 2);
static_assert(RenderPassNodeConfig::OUTPUT_COUNT == 1);

} // namespace Vixen::RenderGraph
