#pragma once

#include "Core/ResourceConfig.h"
#include "ShaderManagement/ShaderProgram.h"

namespace Vixen::RenderGraph {

/**
 * @brief Pure constexpr resource configuration for GraphicsPipelineNode
 *
 * Inputs:
 * - SHADER_PROGRAM (ShaderProgramDescriptor*) - Compiled shader program from ShaderLibraryNode
 * - RENDER_PASS (VkRenderPass) - Render pass from RenderPassNode
 * - DESCRIPTOR_SET_LAYOUT (VkDescriptorSetLayout) - Descriptor layout from DescriptorSetNode
 * - VIEWPORT (VkViewport*) - Viewport configuration (from SwapChainNode dimensions)
 * - SCISSOR (VkRect2D*) - Scissor rectangle (from SwapChainNode dimensions)
 *
 * Outputs:
 * - PIPELINE (VkPipeline) - Graphics pipeline handle
 * - PIPELINE_LAYOUT (VkPipelineLayout) - Pipeline layout handle
 * - PIPELINE_CACHE (VkPipelineCache) - Pipeline cache for optimization
 *
 * Parameters:
 * - ENABLE_DEPTH_TEST (bool) - Enable depth testing (default: true)
 * - ENABLE_DEPTH_WRITE (bool) - Enable depth writes (default: true)
 * - ENABLE_VERTEX_INPUT (bool) - Enable vertex input (default: true)
 * - CULL_MODE (string) - Cull mode: "None", "Front", "Back", "FrontAndBack" (default: "Back")
 * - POLYGON_MODE (string) - Polygon mode: "Fill", "Line", "Point" (default: "Fill")
 * - TOPOLOGY (string) - Primitive topology: "TriangleList", "TriangleStrip", etc. (default: "TriangleList")
 * - FRONT_FACE (string) - Front face: "Clockwise", "CounterClockwise" (default: "CounterClockwise")
 *
 * ALL type checking happens at compile time!
 */
// Compile-time slot counts (declared early for reuse)
namespace GraphicsPipelineNodeCounts {
    static constexpr size_t INPUTS = 5;
    static constexpr size_t OUTPUTS = 3;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(GraphicsPipelineNodeConfig, 
                      GraphicsPipelineNodeCounts::INPUTS, 
                      GraphicsPipelineNodeCounts::OUTPUTS, 
                      GraphicsPipelineNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* ENABLE_DEPTH_TEST = "enableDepthTest";
    static constexpr const char* ENABLE_DEPTH_WRITE = "enableDepthWrite";
    static constexpr const char* ENABLE_VERTEX_INPUT = "enableVertexInput";
    static constexpr const char* CULL_MODE = "cullMode";
    static constexpr const char* POLYGON_MODE = "polygonMode";
    static constexpr const char* TOPOLOGY = "topology";
    static constexpr const char* FRONT_FACE = "frontFace";

    // ===== INPUTS (5) =====
    // Shader program from ShaderLibraryNode
    CONSTEXPR_INPUT(SHADER_PROGRAM, ShaderProgramDescriptorPtr, 0, false);

    // Render pass from RenderPassNode
    CONSTEXPR_INPUT(RENDER_PASS, VkRenderPass, 1, false);

    // Descriptor set layout from DescriptorSetNode
    CONSTEXPR_INPUT(DESCRIPTOR_SET_LAYOUT, VkDescriptorSetLayout, 2, false);

    // Viewport configuration (pointer to VkViewport struct)
    CONSTEXPR_INPUT(VIEWPORT, VkViewportPtr, 3, false);

    // Scissor rectangle (pointer to VkRect2D struct)
    CONSTEXPR_INPUT(SCISSOR, VkRect2DPtr, 4, false);

    // ===== OUTPUTS (3) =====
    // Graphics pipeline handle
    CONSTEXPR_OUTPUT(PIPELINE, VkPipeline, 0, false);

    // Pipeline layout handle
    CONSTEXPR_OUTPUT(PIPELINE_LAYOUT, VkPipelineLayout, 1, false);

    // Pipeline cache for optimization
    CONSTEXPR_OUTPUT(PIPELINE_CACHE, VkPipelineCache, 2, false);

    GraphicsPipelineNodeConfig() {
        // Initialize input descriptors
        INIT_INPUT_DESC(SHADER_PROGRAM, "shader_program",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque pointer
        );

        INIT_INPUT_DESC(RENDER_PASS, "render_pass",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_INPUT_DESC(DESCRIPTOR_SET_LAYOUT, "descriptor_set_layout",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_INPUT_DESC(VIEWPORT, "viewport",
            ResourceLifetime::Transient,
            BufferDescription{}  // Pointer to viewport struct
        );

        INIT_INPUT_DESC(SCISSOR, "scissor",
            ResourceLifetime::Transient,
            BufferDescription{}  // Pointer to scissor struct
        );

        // Initialize output descriptors
        INIT_OUTPUT_DESC(PIPELINE, "pipeline",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_OUTPUT_DESC(PIPELINE_LAYOUT, "pipeline_layout",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_OUTPUT_DESC(PIPELINE_CACHE, "pipeline_cache",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == GraphicsPipelineNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == GraphicsPipelineNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == GraphicsPipelineNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(SHADER_PROGRAM_Slot::index == 0, "SHADER_PROGRAM must be at index 0");
    static_assert(!SHADER_PROGRAM_Slot::nullable, "SHADER_PROGRAM is required");

    static_assert(RENDER_PASS_Slot::index == 1, "RENDER_PASS must be at index 1");
    static_assert(!RENDER_PASS_Slot::nullable, "RENDER_PASS is required");

    static_assert(DESCRIPTOR_SET_LAYOUT_Slot::index == 2, "DESCRIPTOR_SET_LAYOUT must be at index 2");
    static_assert(!DESCRIPTOR_SET_LAYOUT_Slot::nullable, "DESCRIPTOR_SET_LAYOUT is required");

    static_assert(VIEWPORT_Slot::index == 3, "VIEWPORT must be at index 3");
    static_assert(!VIEWPORT_Slot::nullable, "VIEWPORT is required");

    static_assert(SCISSOR_Slot::index == 4, "SCISSOR must be at index 4");
    static_assert(!SCISSOR_Slot::nullable, "SCISSOR is required");

    static_assert(PIPELINE_Slot::index == 0, "PIPELINE must be at index 0");
    static_assert(!PIPELINE_Slot::nullable, "PIPELINE is required");

    static_assert(PIPELINE_LAYOUT_Slot::index == 1, "PIPELINE_LAYOUT must be at index 1");
    static_assert(!PIPELINE_LAYOUT_Slot::nullable, "PIPELINE_LAYOUT is required");

    static_assert(PIPELINE_CACHE_Slot::index == 2, "PIPELINE_CACHE must be at index 2");
    static_assert(!PIPELINE_CACHE_Slot::nullable, "PIPELINE_CACHE is required");

    // Type validations
    static_assert(std::is_same_v<SHADER_PROGRAM_Slot::Type, ShaderProgramDescriptorPtr>);
    static_assert(std::is_same_v<RENDER_PASS_Slot::Type, VkRenderPass>);
    static_assert(std::is_same_v<DESCRIPTOR_SET_LAYOUT_Slot::Type, VkDescriptorSetLayout>);
    static_assert(std::is_same_v<VIEWPORT_Slot::Type, VkViewportPtr>);
    static_assert(std::is_same_v<SCISSOR_Slot::Type, VkRect2DPtr>);
    static_assert(std::is_same_v<PIPELINE_Slot::Type, VkPipeline>);
    static_assert(std::is_same_v<PIPELINE_LAYOUT_Slot::Type, VkPipelineLayout>);
    static_assert(std::is_same_v<PIPELINE_CACHE_Slot::Type, VkPipelineCache>);
};

// Global compile-time validations
static_assert(GraphicsPipelineNodeConfig::INPUT_COUNT == 5);
static_assert(GraphicsPipelineNodeConfig::OUTPUT_COUNT == 3);

} // namespace Vixen::RenderGraph
