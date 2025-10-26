#pragma once

#include "Core/ResourceConfig.h"
// TEMPORARILY REMOVED - using VulkanShader directly for MVP
// #include "ShaderManagement/ShaderProgram.h"
#include "VulkanResources/VulkanDevice.h"

// Forward declare for stub
class VulkanShader;

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;
using VulkanShaderPtr = VulkanShader*;

/**
 * @brief Pure constexpr resource configuration for GraphicsPipelineNode
 *
 * Inputs:
 * - VULKAN_DEVICE (VulkanDevicePtr) - VulkanDevice pointer (contains device, gpu, memory properties)
 * - SHADER_STAGES (VulkanShaderPtr) - Shader stages from VulkanShader (temporary MVP approach)
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
    static constexpr size_t INPUTS = 5;  // DEVICE, SHADER_STAGES, RENDER_PASS, DESCRIPTOR_SET_LAYOUT, SWAPCHAIN_INFO
    static constexpr size_t OUTPUTS = 4;
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

    // ===== INPUTS (6) =====
    // VulkanDevice pointer (contains device, gpu, memory properties, etc.)
    CONSTEXPR_INPUT(VULKAN_DEVICE_IN, VulkanDevicePtr, 0, false);

    // Shader stages from VulkanShader (MVP - temporary until ShaderManagement integrated)
    CONSTEXPR_INPUT(SHADER_STAGES, VulkanShaderPtr, 1, false);

    // Render pass from RenderPassNode
    CONSTEXPR_INPUT(RENDER_PASS, VkRenderPass, 2, false);

    // Descriptor set layout from DescriptorSetNode
    CONSTEXPR_INPUT(DESCRIPTOR_SET_LAYOUT, VkDescriptorSetLayout, 3, false);

    // Swapchain info for viewport/scissor dimensions
    CONSTEXPR_INPUT(SWAPCHAIN_INFO, SwapChainPublicVariablesPtr, 4, false);

    // ===== OUTPUTS (3) =====
    // Graphics pipeline handle
    CONSTEXPR_OUTPUT(PIPELINE, VkPipeline, 0, false);

    // Pipeline layout handle
    CONSTEXPR_OUTPUT(PIPELINE_LAYOUT, VkPipelineLayout, 1, false);

    // Pipeline cache for optimization
    CONSTEXPR_OUTPUT(PIPELINE_CACHE, VkPipelineCache, 2, false);

	CONSTEXPR_OUTPUT(VULKAN_DEVICE_OUT, VulkanDevicePtr, 3, false);

    GraphicsPipelineNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor shaderDesc{"VulkanShader*"};
        INIT_INPUT_DESC(SHADER_STAGES, "shader_stages",
            ResourceLifetime::Persistent,
            shaderDesc  // VulkanShader pointer
        );

        INIT_INPUT_DESC(RENDER_PASS, "render_pass",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_INPUT_DESC(DESCRIPTOR_SET_LAYOUT, "descriptor_set_layout",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        HandleDescriptor swapchainInfoDesc{"SwapChainPublicVariables*"};
        INIT_INPUT_DESC(SWAPCHAIN_INFO, "swapchain_info",
            ResourceLifetime::Persistent,
            swapchainInfoDesc
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

		INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device_out", ResourceLifetime::Persistent, vulkanDeviceDesc);
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == GraphicsPipelineNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == GraphicsPipelineNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == GraphicsPipelineNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE input must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE input is required");

    static_assert(SHADER_STAGES_Slot::index == 1, "SHADER_STAGES must be at index 1");
    static_assert(!SHADER_STAGES_Slot::nullable, "SHADER_STAGES is required");

    static_assert(RENDER_PASS_Slot::index == 2, "RENDER_PASS must be at index 2");
    static_assert(!RENDER_PASS_Slot::nullable, "RENDER_PASS is required");

    static_assert(DESCRIPTOR_SET_LAYOUT_Slot::index == 3, "DESCRIPTOR_SET_LAYOUT must be at index 3");
    static_assert(!DESCRIPTOR_SET_LAYOUT_Slot::nullable, "DESCRIPTOR_SET_LAYOUT is required");

    static_assert(SWAPCHAIN_INFO_Slot::index == 4, "SWAPCHAIN_INFO must be at index 4");
    static_assert(!SWAPCHAIN_INFO_Slot::nullable, "SWAPCHAIN_INFO is required");

    static_assert(PIPELINE_Slot::index == 0, "PIPELINE must be at index 0");
    static_assert(!PIPELINE_Slot::nullable, "PIPELINE is required");

    static_assert(PIPELINE_LAYOUT_Slot::index == 1, "PIPELINE_LAYOUT must be at index 1");
    static_assert(!PIPELINE_LAYOUT_Slot::nullable, "PIPELINE_LAYOUT is required");

    static_assert(PIPELINE_CACHE_Slot::index == 2, "PIPELINE_CACHE must be at index 2");
    static_assert(!PIPELINE_CACHE_Slot::nullable, "PIPELINE_CACHE is required");

	static_assert(VULKAN_DEVICE_OUT_Slot::index == 3, "VULKAN_DEVICE_OUT must be at index 3");
	static_assert(!VULKAN_DEVICE_OUT_Slot::nullable, "VULKAN_DEVICE_OUT is required");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<SHADER_STAGES_Slot::Type, VulkanShaderPtr>);
    static_assert(std::is_same_v<RENDER_PASS_Slot::Type, VkRenderPass>);
    static_assert(std::is_same_v<DESCRIPTOR_SET_LAYOUT_Slot::Type, VkDescriptorSetLayout>);
    static_assert(std::is_same_v<SWAPCHAIN_INFO_Slot::Type, SwapChainPublicVariablesPtr>);
    static_assert(std::is_same_v<PIPELINE_Slot::Type, VkPipeline>);
    static_assert(std::is_same_v<PIPELINE_LAYOUT_Slot::Type, VkPipelineLayout>);
    static_assert(std::is_same_v<PIPELINE_CACHE_Slot::Type, VkPipelineCache>);
	static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevicePtr>);
};

// Global compile-time validations
static_assert(GraphicsPipelineNodeConfig::INPUT_COUNT == GraphicsPipelineNodeCounts::INPUTS);
static_assert(GraphicsPipelineNodeConfig::OUTPUT_COUNT == GraphicsPipelineNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph
