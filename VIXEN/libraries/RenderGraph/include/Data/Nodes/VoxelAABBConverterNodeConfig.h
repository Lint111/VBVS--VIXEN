#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDevice.h"
#include <AccelerationStructureCacher.h>  // CashSystem types

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// ============================================================================
// VOXEL AABB DATA STRUCTURES (Phase K - Hardware RT)
// ============================================================================
// Use types from CashSystem to avoid duplication.
// The cacher owns the AABB extraction logic and data structures.

using VoxelAABB = CashSystem::VoxelAABB;
using VoxelBrickMapping = CashSystem::VoxelBrickMapping;
using VoxelAABBData = CashSystem::VoxelAABBData;

// ============================================================================
// NODE CONFIG
// ============================================================================

namespace VoxelAABBConverterNodeCounts {
    static constexpr size_t INPUTS = 4;  // Added BRICK_GRID_LOOKUP_BUFFER
    static constexpr size_t OUTPUTS = 4;  // AABB_DATA + AABB_BUFFER + MATERIAL_ID_BUFFER + BRICK_MAPPING_BUFFER
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for VoxelAABBConverterNode
 *
 * Converts sparse voxel octree to AABB buffer for BLAS construction.
 * Iterates octree leaf nodes and emits one AABB per solid voxel.
 *
 * Inputs: 4 (VULKAN_DEVICE_IN, COMMAND_POOL, OCTREE_NODES_BUFFER, BRICK_GRID_LOOKUP_BUFFER)
 * Outputs: 4 (AABB_DATA, AABB_BUFFER, MATERIAL_ID_BUFFER, BRICK_MAPPING_BUFFER)
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

    // Brick grid lookup buffer from VoxelGridNode
    // Maps (brickX, brickY, brickZ) grid coord to brick index in compressed buffers
    // Optional: only used for compressed RTX shader paths
    INPUT_SLOT(BRICK_GRID_LOOKUP_BUFFER, VkBuffer, 3,
        SlotNullability::Optional,
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

    // Brick mapping buffer - one VoxelBrickMapping (uvec2) per AABB, indexed by gl_PrimitiveID
    // Used by compressed RTX shaders to access DXT-compressed color/normal buffers
    OUTPUT_SLOT(BRICK_MAPPING_BUFFER, VkBuffer, 3,
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

        BufferDescriptor brickGridLookupDesc{};
        INIT_INPUT_DESC(BRICK_GRID_LOOKUP_BUFFER, "brick_grid_lookup_buffer", ResourceLifetime::Persistent, brickGridLookupDesc);

        HandleDescriptor aabbDataDesc{"VoxelAABBData"};
        INIT_OUTPUT_DESC(AABB_DATA, "aabb_data", ResourceLifetime::Persistent, aabbDataDesc);

        BufferDescriptor aabbBufferDesc{};
        INIT_OUTPUT_DESC(AABB_BUFFER, "aabb_buffer", ResourceLifetime::Persistent, aabbBufferDesc);

        BufferDescriptor materialIdBufferDesc{};
        INIT_OUTPUT_DESC(MATERIAL_ID_BUFFER, "material_id_buffer", ResourceLifetime::Persistent, materialIdBufferDesc);

        BufferDescriptor brickMappingBufferDesc{};
        INIT_OUTPUT_DESC(BRICK_MAPPING_BUFFER, "brick_mapping_buffer", ResourceLifetime::Persistent, brickMappingBufferDesc);
    }

    VALIDATE_NODE_CONFIG(VoxelAABBConverterNodeConfig, VoxelAABBConverterNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0);
    static_assert(COMMAND_POOL_Slot::index == 1);
    static_assert(OCTREE_NODES_BUFFER_Slot::index == 2);
    static_assert(BRICK_GRID_LOOKUP_BUFFER_Slot::index == 3);
    static_assert(AABB_DATA_Slot::index == 0);
    static_assert(AABB_BUFFER_Slot::index == 1);
    static_assert(MATERIAL_ID_BUFFER_Slot::index == 2);
    static_assert(BRICK_MAPPING_BUFFER_Slot::index == 3);
};

} // namespace Vixen::RenderGraph
