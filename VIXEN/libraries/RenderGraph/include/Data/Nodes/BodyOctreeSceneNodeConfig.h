#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts
namespace BodyOctreeSceneNodeCounts {
    static constexpr size_t INPUTS  = 2;  // VULKAN_DEVICE_IN, COMMAND_POOL
    static constexpr size_t OUTPUTS = 6;  // 4 octree buffers + instance buffer + instance count
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for BodyOctreeSceneNode (SP2 Task 5b).
 *
 * Builds the (<=3) per-kind sparse shell octrees, serializes + concatenates them
 * (Vixen::SVO::Concatenate), and uploads the result as GPU buffers in the EXACT
 * format the ray-march shader consumes. Plus a per-body instance SSBO + count.
 *
 * The four octree output slots are deliberately named/typed IDENTICALLY to
 * VoxelGridNode's (OCTREE_NODES_BUFFER, OCTREE_BRICKS_BUFFER,
 * OCTREE_MATERIALS_BUFFER, OCTREE_CONFIG_BUFFER — all VkBuffer) so Task 8 can wire
 * this node where VoxelGridNode was, with no shader/descriptor changes. The new
 * INSTANCE_BUFFER (VkBuffer) + INSTANCE_COUNT (uint32_t) feed the instanced draw.
 *
 * Inputs: 2 (VULKAN_DEVICE_IN, COMMAND_POOL)
 * Outputs: 6 (OCTREE_NODES_BUFFER, OCTREE_BRICKS_BUFFER, OCTREE_MATERIALS_BUFFER,
 *             OCTREE_CONFIG_BUFFER, INSTANCE_BUFFER, INSTANCE_COUNT)
 */
CONSTEXPR_NODE_CONFIG(BodyOctreeSceneNodeConfig,
                      BodyOctreeSceneNodeCounts::INPUTS,
                      BodyOctreeSceneNodeCounts::OUTPUTS,
                      BodyOctreeSceneNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (2) — mirror VoxelGridNode =====
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

    // ===== OUTPUTS (6) =====
    // Same names/types as VoxelGridNode's octree slots (so Task 8 can swap nodes).
    OUTPUT_SLOT(OCTREE_NODES_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(OCTREE_BRICKS_BUFFER, VkBuffer, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(OCTREE_MATERIALS_BUFFER, VkBuffer, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(OCTREE_CONFIG_BUFFER, VkBuffer, 3,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // New: per-body instance SSBO (BodyInstanceGpu records, 32 bytes each) + count.
    OUTPUT_SLOT(INSTANCE_BUFFER, VkBuffer, 4,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(INSTANCE_COUNT, uint32_t, 5,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Constructor: runtime descriptor initialization
    BodyOctreeSceneNodeConfig() {
        // ----- Inputs -----
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        CommandPoolDescriptor commandPoolDesc{};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        // ----- Octree SSBO outputs (persistent — survive recompile, freed only at FinalTeardown) -----
        BufferDescriptor octreeNodesDesc{};
        octreeNodesDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(OCTREE_NODES_BUFFER, "octree_nodes_buffer", ResourceLifetime::Persistent, octreeNodesDesc);

        BufferDescriptor octreeBricksDesc{};
        octreeBricksDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(OCTREE_BRICKS_BUFFER, "octree_bricks_buffer", ResourceLifetime::Persistent, octreeBricksDesc);

        BufferDescriptor octreeMaterialsDesc{};
        octreeMaterialsDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(OCTREE_MATERIALS_BUFFER, "octree_materials_buffer", ResourceLifetime::Persistent, octreeMaterialsDesc);

        // Octree config UBO — 3 x 256-byte OctreeConfig (std140).
        BufferDescriptor octreeConfigDesc{};
        octreeConfigDesc.usage = ResourceUsage::UniformBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(OCTREE_CONFIG_BUFFER, "octree_config_buffer", ResourceLifetime::Persistent, octreeConfigDesc);

        // Instance SSBO — per-body BodyInstanceGpu records.
        BufferDescriptor instanceDesc{};
        instanceDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(INSTANCE_BUFFER, "body_instance_buffer", ResourceLifetime::Persistent, instanceDesc);

        // Instance count — transient scalar value.
        BufferDescriptor instanceCountDesc{};
        INIT_OUTPUT_DESC(INSTANCE_COUNT, "body_instance_count", ResourceLifetime::Transient, instanceCountDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(BodyOctreeSceneNodeConfig, BodyOctreeSceneNodeCounts);

    // ----- Index validations -----
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(OCTREE_NODES_BUFFER_Slot::index == 0, "OCTREE_NODES_BUFFER must be at index 0");
    static_assert(OCTREE_BRICKS_BUFFER_Slot::index == 1, "OCTREE_BRICKS_BUFFER must be at index 1");
    static_assert(OCTREE_MATERIALS_BUFFER_Slot::index == 2, "OCTREE_MATERIALS_BUFFER must be at index 2");
    static_assert(OCTREE_CONFIG_BUFFER_Slot::index == 3, "OCTREE_CONFIG_BUFFER must be at index 3");
    static_assert(INSTANCE_BUFFER_Slot::index == 4, "INSTANCE_BUFFER must be at index 4");
    static_assert(INSTANCE_COUNT_Slot::index == 5, "INSTANCE_COUNT must be at index 5");

    // ----- Type validations -----
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<OCTREE_NODES_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_BRICKS_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_MATERIALS_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_CONFIG_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<INSTANCE_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<INSTANCE_COUNT_Slot::Type, uint32_t>);
};

} // namespace Vixen::RenderGraph
