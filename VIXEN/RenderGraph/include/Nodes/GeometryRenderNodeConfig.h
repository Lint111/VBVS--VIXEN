#pragma once

#include "Core/ResourceConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Pure constexpr resource configuration for GeometryRenderNode
 *
 * Inputs:
 * - RENDER_PASS (VkRenderPass) - Render pass from RenderPassNode
 * - FRAMEBUFFERS (VkFramebuffer[]) - Framebuffers from FramebufferNode (array)
 * - PIPELINE (VkPipeline) - Graphics pipeline from GraphicsPipelineNode
 * - PIPELINE_LAYOUT (VkPipelineLayout) - Pipeline layout from GraphicsPipelineNode
 * - DESCRIPTOR_SETS (VkDescriptorSet[]) - Descriptor sets from DescriptorSetNode (array)
 * - VERTEX_BUFFER (VkBuffer) - Vertex buffer from VertexBufferNode
 * - INDEX_BUFFER (VkBuffer) - Index buffer from VertexBufferNode (nullable)
 * - VIEWPORT (VkViewport*) - Viewport configuration
 * - SCISSOR (VkRect2D*) - Scissor rectangle
 * - RENDER_WIDTH (uint32_t) - Render area width
 * - RENDER_HEIGHT (uint32_t) - Render area height
 *
 * Outputs:
 * - COMMAND_BUFFERS (VkCommandBuffer[]) - Recorded command buffers (array output)
 *
 * Parameters:
 * - VERTEX_COUNT (uint32_t) - Number of vertices to draw
 * - INSTANCE_COUNT (uint32_t) - Number of instances (default: 1)
 * - FIRST_VERTEX (uint32_t) - First vertex index (default: 0)
 * - FIRST_INSTANCE (uint32_t) - First instance index (default: 0)
 * - USE_INDEX_BUFFER (bool) - Whether to use indexed rendering (default: false)
 * - INDEX_COUNT (uint32_t) - Number of indices (if using index buffer)
 * - CLEAR_COLOR_R/G/B/A (float) - Clear color values (default: 0,0,0,1)
 * - CLEAR_DEPTH (float) - Clear depth value (default: 1.0)
 * - CLEAR_STENCIL (uint32_t) - Clear stencil value (default: 0)
 *
 * ALL type checking happens at compile time!
 */
// Compile-time slot counts (declared early for reuse)
namespace GeometryRenderNodeCounts {
    static constexpr size_t INPUTS = 11;
    static constexpr size_t OUTPUTS = 1;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Array; // Framebuffers + descriptor sets are arrays
}

CONSTEXPR_NODE_CONFIG(GeometryRenderNodeConfig, 
                      GeometryRenderNodeCounts::INPUTS, 
                      GeometryRenderNodeCounts::OUTPUTS, 
                      GeometryRenderNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* VERTEX_COUNT = "vertexCount";
    static constexpr const char* INSTANCE_COUNT = "instanceCount";
    static constexpr const char* FIRST_VERTEX = "firstVertex";
    static constexpr const char* FIRST_INSTANCE = "firstInstance";
    static constexpr const char* USE_INDEX_BUFFER = "useIndexBuffer";
    static constexpr const char* INDEX_COUNT = "indexCount";
    static constexpr const char* CLEAR_COLOR_R = "clearColorR";
    static constexpr const char* CLEAR_COLOR_G = "clearColorG";
    static constexpr const char* CLEAR_COLOR_B = "clearColorB";
    static constexpr const char* CLEAR_COLOR_A = "clearColorA";
    static constexpr const char* CLEAR_DEPTH = "clearDepth";
    static constexpr const char* CLEAR_STENCIL = "clearStencil";

    // ===== INPUTS (11) =====
    // Render pass from RenderPassNode
    CONSTEXPR_INPUT(RENDER_PASS, VkRenderPass, 0, false);

    // Framebuffers from FramebufferNode (array - one per swapchain image)
    CONSTEXPR_INPUT(FRAMEBUFFERS, VkFramebuffer, 1, false);

    // Graphics pipeline from GraphicsPipelineNode
    CONSTEXPR_INPUT(PIPELINE, VkPipeline, 2, false);

    // Pipeline layout from GraphicsPipelineNode
    CONSTEXPR_INPUT(PIPELINE_LAYOUT, VkPipelineLayout, 3, false);

    // Descriptor sets from DescriptorSetNode (array)
    CONSTEXPR_INPUT(DESCRIPTOR_SETS, VkDescriptorSet, 4, false);

    // Vertex buffer from VertexBufferNode
    CONSTEXPR_INPUT(VERTEX_BUFFER, VkBuffer, 5, false);

    // Index buffer from VertexBufferNode (nullable - may not use indexed rendering)
    CONSTEXPR_INPUT(INDEX_BUFFER, VkBuffer, 6, true);

    // Viewport configuration
    CONSTEXPR_INPUT(VIEWPORT, VkViewportPtr, 7, false);

    // Scissor rectangle
    CONSTEXPR_INPUT(SCISSOR, VkRect2DPtr, 8, false);

    // Render area dimensions
    CONSTEXPR_INPUT(RENDER_WIDTH, uint32_t, 9, false);
    CONSTEXPR_INPUT(RENDER_HEIGHT, uint32_t, 10, false);

    // ===== OUTPUTS (1) =====
    // Recorded command buffers (array - one per framebuffer)
    CONSTEXPR_OUTPUT(COMMAND_BUFFERS, VkCommandBuffer, 0, false);

    GeometryRenderNodeConfig() {
        // Initialize input descriptors
        INIT_INPUT_DESC(RENDER_PASS, "render_pass",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(FRAMEBUFFERS, "framebuffers",
            ResourceLifetime::Transient,
            BufferDescription{}
        );

        INIT_INPUT_DESC(PIPELINE, "pipeline",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(PIPELINE_LAYOUT, "pipeline_layout",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(DESCRIPTOR_SETS, "descriptor_sets",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(VERTEX_BUFFER, "vertex_buffer",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(INDEX_BUFFER, "index_buffer",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(VIEWPORT, "viewport",
            ResourceLifetime::Transient,
            BufferDescription{}
        );

        INIT_INPUT_DESC(SCISSOR, "scissor",
            ResourceLifetime::Transient,
            BufferDescription{}
        );

        INIT_INPUT_DESC(RENDER_WIDTH, "render_width",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_INPUT_DESC(RENDER_HEIGHT, "render_height",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        // Initialize output descriptors
        INIT_OUTPUT_DESC(COMMAND_BUFFERS, "command_buffers",
            ResourceLifetime::Transient,
            BufferDescription{}
        );
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == GeometryRenderNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == GeometryRenderNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == GeometryRenderNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(RENDER_PASS_Slot::index == 0, "RENDER_PASS must be at index 0");
    static_assert(!RENDER_PASS_Slot::nullable, "RENDER_PASS is required");

    static_assert(FRAMEBUFFERS_Slot::index == 1, "FRAMEBUFFERS must be at index 1");
    static_assert(!FRAMEBUFFERS_Slot::nullable, "FRAMEBUFFERS is required");

    static_assert(PIPELINE_Slot::index == 2, "PIPELINE must be at index 2");
    static_assert(!PIPELINE_Slot::nullable, "PIPELINE is required");

    static_assert(PIPELINE_LAYOUT_Slot::index == 3, "PIPELINE_LAYOUT must be at index 3");
    static_assert(!PIPELINE_LAYOUT_Slot::nullable, "PIPELINE_LAYOUT is required");

    static_assert(DESCRIPTOR_SETS_Slot::index == 4, "DESCRIPTOR_SETS must be at index 4");
    static_assert(!DESCRIPTOR_SETS_Slot::nullable, "DESCRIPTOR_SETS is required");

    static_assert(VERTEX_BUFFER_Slot::index == 5, "VERTEX_BUFFER must be at index 5");
    static_assert(!VERTEX_BUFFER_Slot::nullable, "VERTEX_BUFFER is required");

    static_assert(INDEX_BUFFER_Slot::index == 6, "INDEX_BUFFER must be at index 6");
    static_assert(INDEX_BUFFER_Slot::nullable, "INDEX_BUFFER is optional");

    static_assert(VIEWPORT_Slot::index == 7, "VIEWPORT must be at index 7");
    static_assert(!VIEWPORT_Slot::nullable, "VIEWPORT is required");

    static_assert(SCISSOR_Slot::index == 8, "SCISSOR must be at index 8");
    static_assert(!SCISSOR_Slot::nullable, "SCISSOR is required");

    static_assert(RENDER_WIDTH_Slot::index == 9, "RENDER_WIDTH must be at index 9");
    static_assert(!RENDER_WIDTH_Slot::nullable, "RENDER_WIDTH is required");

    static_assert(RENDER_HEIGHT_Slot::index == 10, "RENDER_HEIGHT must be at index 10");
    static_assert(!RENDER_HEIGHT_Slot::nullable, "RENDER_HEIGHT is required");

    static_assert(COMMAND_BUFFERS_Slot::index == 0, "COMMAND_BUFFERS must be at index 0");
    static_assert(!COMMAND_BUFFERS_Slot::nullable, "COMMAND_BUFFERS is required");

    // Type validations
    static_assert(std::is_same_v<RENDER_PASS_Slot::Type, VkRenderPass>);
    static_assert(std::is_same_v<FRAMEBUFFERS_Slot::Type, VkFramebuffer>);
    static_assert(std::is_same_v<PIPELINE_Slot::Type, VkPipeline>);
    static_assert(std::is_same_v<PIPELINE_LAYOUT_Slot::Type, VkPipelineLayout>);
    static_assert(std::is_same_v<DESCRIPTOR_SETS_Slot::Type, VkDescriptorSet>);
    static_assert(std::is_same_v<VERTEX_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<INDEX_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<VIEWPORT_Slot::Type, VkViewportPtr>);
    static_assert(std::is_same_v<SCISSOR_Slot::Type, VkRect2DPtr>);
    static_assert(std::is_same_v<RENDER_WIDTH_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<RENDER_HEIGHT_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<COMMAND_BUFFERS_Slot::Type, VkCommandBuffer>);
};

// Global compile-time validations
static_assert(GeometryRenderNodeConfig::INPUT_COUNT == 11);
static_assert(GeometryRenderNodeConfig::OUTPUT_COUNT == 1);
static_assert(GeometryRenderNodeConfig::ALLOW_INPUT_ARRAYS); // Array mode enabled

} // namespace Vixen::RenderGraph
