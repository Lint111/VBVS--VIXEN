#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDevice.h"
#include <glm/glm.hpp>

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// ============================================================================
// VOXEL AABB DATA STRUCTURES (Phase K - Hardware RT)
// ============================================================================

/**
 * @brief Single voxel AABB for acceleration structure building
 *
 * Layout matches VkAabbPositionsKHR (6 floats, tightly packed)
 */
struct VoxelAABB {
    glm::vec3 min;  // Minimum corner (x, y, z)
    glm::vec3 max;  // Maximum corner (x+1, y+1, z+1)
};
static_assert(sizeof(VoxelAABB) == 24, "VoxelAABB must be 24 bytes for VkAabbPositionsKHR");

/**
 * @brief Complete AABB data output from VoxelAABBConverterNode
 *
 * Single struct containing all data needed for AccelerationStructureNode
 */
struct VoxelAABBData {
    VkBuffer aabbBuffer = VK_NULL_HANDLE;           // Buffer containing VoxelAABB array
    VkDeviceMemory aabbBufferMemory = VK_NULL_HANDLE;
    uint32_t aabbCount = 0;                         // Number of AABBs (solid voxels)
    VkDeviceSize aabbBufferSize = 0;                // Total buffer size in bytes

    // Material ID buffer - one uint32 per AABB, indexed by gl_PrimitiveID
    VkBuffer materialIdBuffer = VK_NULL_HANDLE;
    VkDeviceMemory materialIdBufferMemory = VK_NULL_HANDLE;
    VkDeviceSize materialIdBufferSize = 0;

    // Original grid info for SVO lookup in shaders
    uint32_t gridResolution = 0;                    // Original grid size (e.g., 128)
    float voxelSize = 1.0f;                         // Size of each voxel in world units

    bool IsValid() const {
        return aabbBuffer != VK_NULL_HANDLE && aabbCount > 0;
    }
};

// ============================================================================
// NODE CONFIG
// ============================================================================

namespace VoxelAABBConverterNodeCounts {
    static constexpr size_t INPUTS = 3;
    static constexpr size_t OUTPUTS = 3;  // AABB_DATA + AABB_BUFFER + MATERIAL_ID_BUFFER
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for VoxelAABBConverterNode
 *
 * Converts sparse voxel octree to AABB buffer for BLAS construction.
 * Iterates octree leaf nodes and emits one AABB per solid voxel.
 *
 * Inputs: 3 (VULKAN_DEVICE_IN, COMMAND_POOL, VOXEL_GRID_DATA)
 * Outputs: 1 (AABB_DATA - single struct with buffer + count + memory)
 */
CONSTEXPR_NODE_CONFIG(VoxelAABBConverterNodeConfig,
                      VoxelAABBConverterNodeCounts::INPUTS,
                      VoxelAABBConverterNodeCounts::OUTPUTS,
                      VoxelAABBConverterNodeCounts::ARRAY_MODE) {

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

    // Octree nodes buffer from VoxelGridNode
    // Contains esvoNodes for traversal to find solid voxels
    INPUT_SLOT(OCTREE_NODES_BUFFER, VkBuffer, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS =====

    // Single output containing all AABB data (pointer for persistent storage)
    OUTPUT_SLOT(AABB_DATA, VoxelAABBData*, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Raw AABB buffer for shader descriptor binding (intersection shader needs this)
    OUTPUT_SLOT(AABB_BUFFER, VkBuffer, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Material ID buffer - one uint8 per AABB, indexed by gl_PrimitiveID in RT shaders
    OUTPUT_SLOT(MATERIAL_ID_BUFFER, VkBuffer, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ===== PARAMETERS =====
    static constexpr const char* PARAM_GRID_RESOLUTION = "grid_resolution";
    static constexpr const char* PARAM_VOXEL_SIZE = "voxel_size";

    // Constructor
    VoxelAABBConverterNodeConfig() {
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        CommandPoolDescriptor commandPoolDesc{};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        BufferDescriptor octreeNodesDesc{};
        INIT_INPUT_DESC(OCTREE_NODES_BUFFER, "octree_nodes_buffer", ResourceLifetime::Persistent, octreeNodesDesc);

        HandleDescriptor aabbDataDesc{"VoxelAABBData"};
        INIT_OUTPUT_DESC(AABB_DATA, "aabb_data", ResourceLifetime::Persistent, aabbDataDesc);

        BufferDescriptor aabbBufferDesc{};
        INIT_OUTPUT_DESC(AABB_BUFFER, "aabb_buffer", ResourceLifetime::Persistent, aabbBufferDesc);

        BufferDescriptor materialIdBufferDesc{};
        INIT_OUTPUT_DESC(MATERIAL_ID_BUFFER, "material_id_buffer", ResourceLifetime::Persistent, materialIdBufferDesc);
    }

    VALIDATE_NODE_CONFIG(VoxelAABBConverterNodeConfig, VoxelAABBConverterNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0);
    static_assert(COMMAND_POOL_Slot::index == 1);
    static_assert(OCTREE_NODES_BUFFER_Slot::index == 2);
    static_assert(AABB_DATA_Slot::index == 0);
    static_assert(AABB_BUFFER_Slot::index == 1);
    static_assert(MATERIAL_ID_BUFFER_Slot::index == 2);
};

} // namespace Vixen::RenderGraph
