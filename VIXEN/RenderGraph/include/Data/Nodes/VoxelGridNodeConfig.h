#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;

// Compile-time slot counts
namespace VoxelGridNodeCounts {
    static constexpr size_t INPUTS = 2;
    static constexpr size_t OUTPUTS = 3;  // Changed: 4 → 5 (added material buffer)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for VoxelGridNode
 *
 * Generates procedural voxel scenes and uploads sparse octree to GPU.
 * Outputs SSBO buffers for octree-based ray marching.
 *
 * Inputs: 2 (VULKAN_DEVICE_IN, COMMAND_POOL)
 * Outputs: 3 (OCTREE_NODES_BUFFER, OCTREE_BRICKS_BUFFER, OCTREE_MATERIALS_BUFFER)
 */
CONSTEXPR_NODE_CONFIG(VoxelGridNodeConfig,
                      VoxelGridNodeCounts::INPUTS,
                      VoxelGridNodeCounts::OUTPUTS,
                      VoxelGridNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (2) =====
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevicePtr, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (3) =====

    OUTPUT_SLOT(OCTREE_NODES_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(OCTREE_BRICKS_BUFFER, VkBuffer, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(OCTREE_MATERIALS_BUFFER, VkBuffer, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ===== PARAMETERS =====
    static constexpr const char* PARAM_RESOLUTION = "resolution";
    static constexpr const char* PARAM_SCENE_TYPE = "scene_type";

    // Constructor for runtime descriptor initialization
    VoxelGridNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        CommandPoolDescriptor commandPoolDesc{};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        // Initialize SSBO buffer descriptors for octree
        BufferDescriptor octreeNodesDesc{};
        octreeNodesDesc.size = 4096 * 36;  // Initial capacity: 4096 nodes * 36 bytes
        octreeNodesDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(OCTREE_NODES_BUFFER, "octree_nodes_buffer", ResourceLifetime::Persistent, octreeNodesDesc);

        BufferDescriptor octreeBricksDesc{};
        octreeBricksDesc.size = 1024 * 512;  // Initial capacity: 1024 bricks * 512 bytes
        octreeBricksDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(OCTREE_BRICKS_BUFFER, "octree_bricks_buffer", ResourceLifetime::Persistent, octreeBricksDesc);

        BufferDescriptor octreeMaterialsDesc{};
        octreeMaterialsDesc.size = 256 * 32;  // Capacity: 256 materials * 32 bytes
        octreeMaterialsDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(OCTREE_MATERIALS_BUFFER, "octree_materials_buffer", ResourceLifetime::Persistent, octreeMaterialsDesc);
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == VoxelGridNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == VoxelGridNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == VoxelGridNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(OCTREE_NODES_BUFFER_Slot::index == 0, "OCTREE_NODES_BUFFER must be at index 0");
    static_assert(OCTREE_BRICKS_BUFFER_Slot::index == 1, "OCTREE_BRICKS_BUFFER must be at index 1");
    static_assert(OCTREE_MATERIALS_BUFFER_Slot::index == 2, "OCTREE_MATERIALS_BUFFER must be at index 2");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<OCTREE_NODES_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_BRICKS_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_MATERIALS_BUFFER_Slot::Type, VkBuffer>);
};

// Global compile-time validations
static_assert(VoxelGridNodeConfig::INPUT_COUNT == VoxelGridNodeCounts::INPUTS);
static_assert(VoxelGridNodeConfig::OUTPUT_COUNT == VoxelGridNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph
