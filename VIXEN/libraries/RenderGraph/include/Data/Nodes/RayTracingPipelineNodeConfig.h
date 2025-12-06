#pragma once
#include "Data/Core/ResourceConfig.h"
#include "Data/Nodes/AccelerationStructureNodeConfig.h"
#include "VulkanDevice.h"

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// ============================================================================
// RAY TRACING PIPELINE DATA (Phase K - Hardware RT)
// ============================================================================

/**
 * @brief Shader Binding Table region descriptor
 *
 * Each region (raygen, miss, hit, callable) has its own memory region
 * in the SBT buffer with specific alignment requirements.
 */
struct SBTRegion {
    VkDeviceAddress deviceAddress = 0;
    VkDeviceSize stride = 0;  // Handle size aligned to shaderGroupHandleAlignment
    VkDeviceSize size = 0;    // Total region size
};

/**
 * @brief Complete Shader Binding Table for ray tracing dispatch
 */
struct ShaderBindingTable {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize totalSize = 0;

    // Regions for vkCmdTraceRaysKHR
    SBTRegion raygenRegion;
    SBTRegion missRegion;
    SBTRegion hitRegion;
    SBTRegion callableRegion;  // Not used for voxels, but included for completeness

    bool IsValid() const {
        return buffer != VK_NULL_HANDLE &&
               raygenRegion.deviceAddress != 0 &&
               missRegion.deviceAddress != 0 &&
               hitRegion.deviceAddress != 0;
    }
};

/**
 * @brief Ray tracing pipeline and SBT data
 */
struct RayTracingPipelineData {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

    ShaderBindingTable sbt;

    // Shader stage info for debugging
    uint32_t raygenShaderCount = 0;
    uint32_t missShaderCount = 0;
    uint32_t hitShaderCount = 0;    // Includes closest-hit, any-hit, intersection

    bool IsValid() const {
        return pipeline != VK_NULL_HANDLE && sbt.IsValid();
    }
};

// ============================================================================
// NODE CONFIG
// ============================================================================

namespace RayTracingPipelineNodeCounts {
    static constexpr size_t INPUTS = 5;
    static constexpr size_t OUTPUTS = 1;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for RayTracingPipelineNode
 *
 * Creates VK_KHR_ray_tracing_pipeline and builds Shader Binding Table.
 *
 * Pipeline Stages:
 * - Ray Generation (.rgen): Generates primary rays from camera
 * - Intersection (.rint): Custom AABB intersection for voxels
 * - Closest Hit (.rchit): Shading on ray hit
 * - Miss (.rmiss): Background color when no hit
 *
 * Inputs: 5 (Device, AccelStructData, RayGen, Intersection, ClosestHit, Miss shaders)
 * Outputs: 1 (RayTracingPipelineData with pipeline + SBT)
 */
CONSTEXPR_NODE_CONFIG(RayTracingPipelineNodeConfig,
                      RayTracingPipelineNodeCounts::INPUTS,
                      RayTracingPipelineNodeCounts::OUTPUTS,
                      RayTracingPipelineNodeCounts::ARRAY_MODE) {

    // ===== INPUTS =====

    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Acceleration structure for descriptor set binding
    INPUT_SLOT(ACCELERATION_STRUCTURE_DATA, AccelerationStructureData*, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Shader modules (SPIR-V compiled)
    INPUT_SLOT(RAYGEN_SHADER, VkShaderModule, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(MISS_SHADER, VkShaderModule, 3,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Hit group contains: intersection + closest-hit
    // For voxel RT, we use intersection shader for AABB testing
    INPUT_SLOT(HIT_GROUP_SHADERS, VkShaderModule, 4,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS =====

    OUTPUT_SLOT(RT_PIPELINE_DATA, RayTracingPipelineData*, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ===== PARAMETERS =====

    static constexpr const char* PARAM_MAX_RAY_RECURSION = "max_ray_recursion";
    static constexpr const char* PARAM_OUTPUT_WIDTH = "output_width";
    static constexpr const char* PARAM_OUTPUT_HEIGHT = "output_height";

    // Constructor
    RayTracingPipelineNodeConfig() {
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        HandleDescriptor accelDesc{"AccelerationStructureData*"};
        INIT_INPUT_DESC(ACCELERATION_STRUCTURE_DATA, "acceleration_structure", ResourceLifetime::Persistent, accelDesc);

        HandleDescriptor shaderDesc{"VkShaderModule"};
        INIT_INPUT_DESC(RAYGEN_SHADER, "raygen_shader", ResourceLifetime::Persistent, shaderDesc);
        INIT_INPUT_DESC(MISS_SHADER, "miss_shader", ResourceLifetime::Persistent, shaderDesc);
        INIT_INPUT_DESC(HIT_GROUP_SHADERS, "hit_group_shaders", ResourceLifetime::Persistent, shaderDesc);

        HandleDescriptor pipelineDesc{"RayTracingPipelineData*"};
        INIT_OUTPUT_DESC(RT_PIPELINE_DATA, "rt_pipeline", ResourceLifetime::Persistent, pipelineDesc);
    }

    VALIDATE_NODE_CONFIG(RayTracingPipelineNodeConfig, RayTracingPipelineNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0);
    static_assert(ACCELERATION_STRUCTURE_DATA_Slot::index == 1);
    static_assert(RAYGEN_SHADER_Slot::index == 2);
    static_assert(MISS_SHADER_Slot::index == 3);
    static_assert(HIT_GROUP_SHADERS_Slot::index == 4);
    static_assert(RT_PIPELINE_DATA_Slot::index == 0);
};

} // namespace Vixen::RenderGraph
