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
using ShaderDataBundlePtr = std::shared_ptr<ShaderManagement::ShaderDataBundle>;

/**
 * @brief Pure constexpr resource configuration for ComputePipelineNode
 *
 * Inputs:
 * - VULKAN_DEVICE (VulkanDevicePtr) - VulkanDevice pointer (contains device, gpu, memory properties)
 * - SHADER_DATA_BUNDLE (ShaderDataBundlePtr) - Shader reflection data from ShaderLibraryNode
 * - DESCRIPTOR_SET_LAYOUT (VkDescriptorSetLayout) - Optional (auto-generated if not provided)
 *
 * Outputs:
 * - PIPELINE (VkPipeline) - Compute pipeline handle
 * - PIPELINE_LAYOUT (VkPipelineLayout) - Pipeline layout handle
 * - PIPELINE_CACHE (VkPipelineCache) - Pipeline cache for optimization
 * - VULKAN_DEVICE_OUT (VulkanDevicePtr) - Device passthrough
 *
 * Parameters:
 * - WORKGROUP_SIZE_X (uint32_t) - Workgroup size X (default: 0 = extract from shader)
 * - WORKGROUP_SIZE_Y (uint32_t) - Workgroup size Y (default: 0 = extract from shader)
 * - WORKGROUP_SIZE_Z (uint32_t) - Workgroup size Z (default: 0 = extract from shader)
 */
// Compile-time slot counts
namespace ComputePipelineNodeCounts {
    static constexpr size_t INPUTS = 3;
    static constexpr size_t OUTPUTS = 4;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(ComputePipelineNodeConfig,
                      ComputePipelineNodeCounts::INPUTS,
                      ComputePipelineNodeCounts::OUTPUTS,
                      ComputePipelineNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* WORKGROUP_SIZE_X = "workgroupSizeX";
    static constexpr const char* WORKGROUP_SIZE_Y = "workgroupSizeY";
    static constexpr const char* WORKGROUP_SIZE_Z = "workgroupSizeZ";

    // ===== INPUTS (3) =====
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

    INPUT_SLOT(DESCRIPTOR_SET_LAYOUT, VkDescriptorSetLayout, 2,
        SlotNullability::Optional,  // Auto-generated if not provided
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

    ComputePipelineNodeConfig() {
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor shaderBundleDesc{"ShaderDataBundle*"};
        INIT_INPUT_DESC(SHADER_DATA_BUNDLE, "shader_data_bundle", ResourceLifetime::Persistent, shaderBundleDesc);

        INIT_INPUT_DESC(DESCRIPTOR_SET_LAYOUT, "descriptor_set_layout", ResourceLifetime::Persistent, BufferDescription{});

        INIT_OUTPUT_DESC(PIPELINE, "pipeline", ResourceLifetime::Persistent, BufferDescription{});
        INIT_OUTPUT_DESC(PIPELINE_LAYOUT, "pipeline_layout", ResourceLifetime::Persistent, BufferDescription{});
        INIT_OUTPUT_DESC(PIPELINE_CACHE, "pipeline_cache", ResourceLifetime::Persistent, BufferDescription{});
        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device_out", ResourceLifetime::Persistent, vulkanDeviceDesc);
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == ComputePipelineNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == ComputePipelineNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == ComputePipelineNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0);
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable);
    static_assert(SHADER_DATA_BUNDLE_Slot::index == 1);
    static_assert(!SHADER_DATA_BUNDLE_Slot::nullable);
    static_assert(DESCRIPTOR_SET_LAYOUT_Slot::index == 2);
    static_assert(DESCRIPTOR_SET_LAYOUT_Slot::nullable);  // Optional

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
    static_assert(std::is_same_v<DESCRIPTOR_SET_LAYOUT_Slot::Type, VkDescriptorSetLayout>);
    static_assert(std::is_same_v<PIPELINE_Slot::Type, VkPipeline>);
    static_assert(std::is_same_v<PIPELINE_LAYOUT_Slot::Type, VkPipelineLayout>);
    static_assert(std::is_same_v<PIPELINE_CACHE_Slot::Type, VkPipelineCache>);
    static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevice*>);
};

// Global compile-time validations
static_assert(ComputePipelineNodeConfig::INPUT_COUNT == ComputePipelineNodeCounts::INPUTS);
static_assert(ComputePipelineNodeConfig::OUTPUT_COUNT == ComputePipelineNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph
