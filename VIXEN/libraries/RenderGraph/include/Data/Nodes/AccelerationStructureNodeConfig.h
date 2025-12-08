#pragma once
#include "Data/Core/ResourceConfig.h"
#include "Data/Nodes/VoxelAABBConverterNodeConfig.h"
#include "VulkanDevice.h"

// Forward declaration for CashSystem cached scene data
namespace CashSystem {
    struct VoxelSceneData;
}

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;
using RTXCapabilities = Vixen::Vulkan::Resources::RTXCapabilities;

// ============================================================================
// ACCELERATION STRUCTURE DATA (Phase K - Hardware RT)
// ============================================================================

/**
 * @brief Acceleration structure handles for ray tracing
 *
 * Contains both BLAS (geometry) and TLAS (instances) for the scene.
 * BLAS: Built from voxel AABBs (procedural geometry)
 * TLAS: Contains single instance of the BLAS (static scene)
 */
struct AccelerationStructureData {
    // Bottom-Level Acceleration Structure (geometry)
    VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
    VkBuffer blasBuffer = VK_NULL_HANDLE;
    VkDeviceMemory blasMemory = VK_NULL_HANDLE;
    VkDeviceAddress blasDeviceAddress = 0;

    // Top-Level Acceleration Structure (instances)
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
    VkBuffer tlasBuffer = VK_NULL_HANDLE;
    VkDeviceMemory tlasMemory = VK_NULL_HANDLE;
    VkDeviceAddress tlasDeviceAddress = 0;

    // Instance buffer (for TLAS)
    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory instanceMemory = VK_NULL_HANDLE;

    // Scratch buffer (temporary, needed during build)
    VkBuffer scratchBuffer = VK_NULL_HANDLE;
    VkDeviceMemory scratchMemory = VK_NULL_HANDLE;

    // Metadata
    uint32_t primitiveCount = 0;  // Number of AABBs in BLAS

    bool IsValid() const {
        return blas != VK_NULL_HANDLE && tlas != VK_NULL_HANDLE;
    }
};

// ============================================================================
// NODE CONFIG
// ============================================================================

namespace AccelerationStructureNodeCounts {
    static constexpr size_t INPUTS = 4;  // +VOXEL_SCENE_DATA (from VoxelGridNode)
    static constexpr size_t OUTPUTS = 2;  // ACCELERATION_STRUCTURE_DATA + TLAS_HANDLE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for AccelerationStructureNode
 *
 * Builds BLAS from voxel AABBs and TLAS containing single static instance.
 * For dynamic scenes, TLAS can be rebuilt each frame while BLAS stays static.
 *
 * Inputs: 4 (VULKAN_DEVICE_IN, COMMAND_POOL, AABB_DATA, VOXEL_SCENE_DATA)
 * Outputs: 2 (ACCELERATION_STRUCTURE_DATA, TLAS_HANDLE)
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

    // AABB data from VoxelAABBConverterNode
    INPUT_SLOT(AABB_DATA, VoxelAABBData*, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Cached voxel scene data from VoxelGridNode
    // Required for AccelerationStructureCacher to build BLAS/TLAS using GetOrCreate pattern
    // Contains scene geometry (ESVO nodes, bricks) and metadata
    INPUT_SLOT(VOXEL_SCENE_DATA, CashSystem::VoxelSceneData*, 3,
        SlotNullability::Optional,
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

        HandleDescriptor voxelSceneDataDesc{"CashSystem::VoxelSceneData*"};
        INIT_INPUT_DESC(VOXEL_SCENE_DATA, "voxel_scene_data", ResourceLifetime::Persistent, voxelSceneDataDesc);

        HandleDescriptor accelStructDesc{"AccelerationStructureData*"};
        INIT_OUTPUT_DESC(ACCELERATION_STRUCTURE_DATA, "acceleration_structure", ResourceLifetime::Persistent, accelStructDesc);

        HandleDescriptor tlasHandleDesc{"VkAccelerationStructureKHR"};
        INIT_OUTPUT_DESC(TLAS_HANDLE, "tlas_handle", ResourceLifetime::Persistent, tlasHandleDesc);
    }

    VALIDATE_NODE_CONFIG(AccelerationStructureNodeConfig, AccelerationStructureNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0);
    static_assert(COMMAND_POOL_Slot::index == 1);
    static_assert(AABB_DATA_Slot::index == 2);
    static_assert(VOXEL_SCENE_DATA_Slot::index == 3);
    static_assert(ACCELERATION_STRUCTURE_DATA_Slot::index == 0);
    static_assert(TLAS_HANDLE_Slot::index == 1);
};

} // namespace Vixen::RenderGraph
