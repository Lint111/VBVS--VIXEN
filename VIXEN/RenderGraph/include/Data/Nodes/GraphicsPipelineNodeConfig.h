#pragma once

#include "Data/Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"

// Forward declare ShaderDataBundle
namespace ShaderManagement {
    struct ShaderDataBundle;
}

namespace Vixen::RenderGraph {

// Type aliases
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;
using ShaderDataBundlePtr = ShaderManagement::ShaderDataBundle*;

/**
 * @brief Pure constexpr resource configuration for GraphicsPipelineNode
 *
 * Inputs:
 * - VULKAN_DEVICE (VulkanDevice*) - VulkanDevice pointer (contains device, gpu, memory properties)
 * - SHADER_DATA_BUNDLE (ShaderDataBundlePtr) - Shader reflection data from ShaderLibraryNode
 * - RENDER_PASS (VkRenderPass) - Render pass from RenderPassNode
 * - DESCRIPTOR_SET_LAYOUT (VkDescriptorSetLayout) - Descriptor layout from DescriptorSetNode
 *
 * Outputs:
 * - PIPELINE (VkPipeline) - Graphics pipeline handle
 * - PIPELINE_LAYOUT (VkPipelineLayout) - Pipeline layout handle
 * - PIPELINE_CACHE (VkPipelineCache) - Pipeline cache for optimization
 * - VULKAN_DEVICE_OUT (VulkanDevice*) - Device passthrough
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
 * Note: Pipelines are swapchain-independent. Viewport/scissor are set dynamically at execute-time.
 */
// Compile-time slot counts
namespace GraphicsPipelineNodeCounts {
    static constexpr size_t INPUTS = 4;  // Removed SWAPCHAIN_INFO (pipelines are swapchain-independent)
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

    // ===== INPUTS (4) =====
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(SHADER_DATA_BUNDLE, ShaderDataBundlePtr, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(RENDER_PASS, VkRenderPass, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(DESCRIPTOR_SET_LAYOUT, VkDescriptorSetLayout, 3,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (4) =====
    OUTPUT_SLOT(PIPELINE, VkPipeline, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(PIPELINE_LAYOUT, VkPipelineLayout, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(PIPELINE_CACHE, VkPipelineCache, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevice*, 3,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    GraphicsPipelineNodeConfig() {
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor shaderBundleDesc{"ShaderDataBundle*"};
        INIT_INPUT_DESC(SHADER_DATA_BUNDLE, "shader_data_bundle", ResourceLifetime::Persistent, shaderBundleDesc);

        INIT_INPUT_DESC(RENDER_PASS, "render_pass", ResourceLifetime::Persistent, BufferDescription{});
        INIT_INPUT_DESC(DESCRIPTOR_SET_LAYOUT, "descriptor_set_layout", ResourceLifetime::Persistent, BufferDescription{});

        INIT_OUTPUT_DESC(PIPELINE, "pipeline", ResourceLifetime::Persistent, BufferDescription{});
        INIT_OUTPUT_DESC(PIPELINE_LAYOUT, "pipeline_layout", ResourceLifetime::Persistent, BufferDescription{});
        INIT_OUTPUT_DESC(PIPELINE_CACHE, "pipeline_cache", ResourceLifetime::Persistent, BufferDescription{});
        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device_out", ResourceLifetime::Persistent, vulkanDeviceDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(GraphicsPipelineNodeConfig, GraphicsPipelineNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0);
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable);
    static_assert(SHADER_DATA_BUNDLE_Slot::index == 1);
    static_assert(!SHADER_DATA_BUNDLE_Slot::nullable);
    static_assert(RENDER_PASS_Slot::index == 2);
    static_assert(!RENDER_PASS_Slot::nullable);
    static_assert(DESCRIPTOR_SET_LAYOUT_Slot::index == 3);
    static_assert(!DESCRIPTOR_SET_LAYOUT_Slot::nullable);

    static_assert(PIPELINE_Slot::index == 0);
    static_assert(!PIPELINE_Slot::nullable);
    static_assert(PIPELINE_LAYOUT_Slot::index == 1);
    static_assert(!PIPELINE_LAYOUT_Slot::nullable);
    static_assert(PIPELINE_CACHE_Slot::index == 2);
    static_assert(!PIPELINE_CACHE_Slot::nullable);
    static_assert(VULKAN_DEVICE_OUT_Slot::index == 3);
    static_assert(!VULKAN_DEVICE_OUT_Slot::nullable);

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<SHADER_DATA_BUNDLE_Slot::Type, ShaderDataBundlePtr>);
    static_assert(std::is_same_v<RENDER_PASS_Slot::Type, VkRenderPass>);
    static_assert(std::is_same_v<DESCRIPTOR_SET_LAYOUT_Slot::Type, VkDescriptorSetLayout>);
    static_assert(std::is_same_v<PIPELINE_Slot::Type, VkPipeline>);
    static_assert(std::is_same_v<PIPELINE_LAYOUT_Slot::Type, VkPipelineLayout>);
    static_assert(std::is_same_v<PIPELINE_CACHE_Slot::Type, VkPipelineCache>);
    static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevice*>);
};

} // namespace Vixen::RenderGraph

