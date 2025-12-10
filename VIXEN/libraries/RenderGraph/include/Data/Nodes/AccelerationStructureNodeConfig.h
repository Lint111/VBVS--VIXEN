#pragma once
#include "Data/Core/ResourceConfig.h"
#include "Data/Nodes/VoxelAABBConverterNodeConfig.h"  // Includes CashSystem types
#include "VulkanDevice.h"

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;
using RTXCapabilities = Vixen::Vulkan::Resources::RTXCapabilities;

// ============================================================================
// ACCELERATION STRUCTURE DATA (Phase K - Hardware RT)
// ============================================================================
// Use type from CashSystem to avoid duplication.
// The cacher owns the BLAS/TLAS build logic and data structures.

using AccelerationStructureData = CashSystem::AccelerationStructureData;

// ============================================================================
// NODE CONFIG
// ============================================================================

namespace AccelerationStructureNodeCounts {
    static constexpr size_t INPUTS = 3;  // VULKAN_DEVICE_IN, COMMAND_POOL, AABB_DATA
    static constexpr size_t OUTPUTS = 2;  // ACCELERATION_STRUCTURE_DATA + TLAS_HANDLE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for AccelerationStructureNode
 *
 * Builds BLAS from voxel AABBs and TLAS containing single static instance.
 * For dynamic scenes, TLAS can be rebuilt each frame while BLAS stays static.
 *
 * Inputs: 3 (VULKAN_DEVICE_IN, COMMAND_POOL, AABB_DATA)
 * Outputs: 2 (ACCELERATION_STRUCTURE_DATA, TLAS_HANDLE)
 *
 * Note: VOXEL_SCENE_DATA input was removed - AABB extraction is now handled
 * by VoxelAABBCacher in VoxelAABBConverterNode. This node only builds BLAS/TLAS.
 */
CONSTEXPR_NODE_CONFIG(AccelerationStructureNodeConfig,
                      AccelerationStructureNodeCounts::INPUTS,
                      AccelerationStructureNodeCounts::OUTPUTS,
                      AccelerationStructureNodeCounts::ARRAY_MODE) {

    // ===== INPUTS =====

    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // AABB data from VoxelAABBConverterNode (via VoxelAABBCacher)
    INPUT_SLOT(AABB_DATA, VoxelAABBData*, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS =====

    // Complete acceleration structure data (BLAS + TLAS)
    OUTPUT_SLOT(ACCELERATION_STRUCTURE_DATA, AccelerationStructureData*, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // TLAS handle for descriptor binding (extracted from AccelerationStructureData)
    // Used by DescriptorResourceGathererNode for variadic resource wiring
    OUTPUT_SLOT(TLAS_HANDLE, VkAccelerationStructureKHR, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ===== PARAMETERS =====

    // Build flags for acceleration structure
    static constexpr const char* PARAM_PREFER_FAST_TRACE = "prefer_fast_trace";
    static constexpr const char* PARAM_ALLOW_UPDATE = "allow_update";
    static constexpr const char* PARAM_ALLOW_COMPACTION = "allow_compaction";

    // Constructor
    AccelerationStructureNodeConfig() {
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        CommandPoolDescriptor commandPoolDesc{};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        HandleDescriptor aabbDataDesc{"VoxelAABBData*"};
        INIT_INPUT_DESC(AABB_DATA, "aabb_data", ResourceLifetime::Persistent, aabbDataDesc);

        HandleDescriptor accelStructDesc{"AccelerationStructureData*"};
        INIT_OUTPUT_DESC(ACCELERATION_STRUCTURE_DATA, "acceleration_structure", ResourceLifetime::Persistent, accelStructDesc);

        HandleDescriptor tlasHandleDesc{"VkAccelerationStructureKHR"};
        INIT_OUTPUT_DESC(TLAS_HANDLE, "tlas_handle", ResourceLifetime::Persistent, tlasHandleDesc);
    }

    VALIDATE_NODE_CONFIG(AccelerationStructureNodeConfig, AccelerationStructureNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0);
    static_assert(COMMAND_POOL_Slot::index == 1);
    static_assert(AABB_DATA_Slot::index == 2);
    static_assert(ACCELERATION_STRUCTURE_DATA_Slot::index == 0);
    static_assert(TLAS_HANDLE_Slot::index == 1);
};

} // namespace Vixen::RenderGraph
