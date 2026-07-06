#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts
namespace BodyOctreeSceneNodeCounts {
    static constexpr size_t INPUTS  = 3;  // VULKAN_DEVICE_IN, COMMAND_POOL, CURRENT_FRAME_INDEX
    static constexpr size_t OUTPUTS = 9;  // 4 octree buffers + 2 SDF buffers + instance buffer + instance count + mip pool buffer
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
 * Inputs: 3 (VULKAN_DEVICE_IN, COMMAND_POOL, CURRENT_FRAME_INDEX)
 * Outputs: 9 (OCTREE_NODES_BUFFER, OCTREE_BRICKS_BUFFER, OCTREE_MATERIALS_BUFFER,
 *             OCTREE_CONFIG_BUFFER, OCTREE_SDF_BUFFER, OCTREE_BRICKLOOKUP_BUFFER,
 *             OCTREE_MIPPOOL_BUFFER, INSTANCE_BUFFER, INSTANCE_COUNT)
 */
CONSTEXPR_NODE_CONFIG(BodyOctreeSceneNodeConfig,
                      BodyOctreeSceneNodeCounts::INPUTS,
                      BodyOctreeSceneNodeCounts::OUTPUTS,
                      BodyOctreeSceneNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (3) — mirror VoxelGridNode + per-frame ring index =====
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

    // Per-frame ring index from FrameSyncNode — read each Execute to select which
    // ring buffer to upload instances into (mirrors DynamicInstanceBufferNodeConfig).
    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 2,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (8) =====
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

    // New: per-body instance SSBO (BodyInstanceGpu records, 64 bytes each) + count.
    OUTPUT_SLOT(INSTANCE_BUFFER, VkBuffer, 4,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // int32_t (signed) to match the shader's reflected `int instanceCount` push-constant field
    // (BodyInstanceRayMarch.comp, binding 10). Keeping the slot, the node member, and the shader field
    // all int32_t lets the PushConstantGatherer's reflection-driven any_cast<int32_t> succeed at Execute.
    OUTPUT_SLOT(INSTANCE_COUNT, int32_t, 5,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Inc2 M3: SoA-SDF brick data SSBO (binding 11) — float[] packed per-voxel SDF values.
    // Placeholder (1-byte pad) for binary/Procedural bodies; populated by ConcatenateSdf.
    OUTPUT_SLOT(OCTREE_SDF_BUFFER, VkBuffer, 6,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Inc2 M3: brick-grid lookup SSBO (binding 12) — uint32[bpa^3] grid-coord→brickIndex table.
    // Placeholder (1-byte pad) for binary/Procedural bodies; populated by ConcatenateSdf.
    OUTPUT_SLOT(OCTREE_BRICKLOOKUP_BUFFER, VkBuffer, 7,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Sparse-Mip ESVO LOD Inc1 M3: per-level filtered mip sample pool (binding 13) —
    // packed {value,coverage} floats, one per (node, channel). Placeholder (1-byte pad)
    // when a tree was never mip-baked (ConcatenateSdf's plain sibling, no mips);
    // populated by ConcatenateSdfWithMips (MipBake.h).
    OUTPUT_SLOT(OCTREE_MIPPOOL_BUFFER, VkBuffer, 8,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Constructor: runtime descriptor initialization
    BodyOctreeSceneNodeConfig() {
        // ----- Inputs -----
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        CommandPoolDescriptor commandPoolDesc{};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        // Per-frame ring index (transient scalar) — drives instance-buffer ring selection in ExecuteImpl.
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index",
            ResourceLifetime::Transient, BufferDescription{});

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

        // Octree config SSBO (binding 5, std430) — N x 432-byte OctreeConfig.
        // Changed from UBO to SSBO in I3.2 to support dynamic (count-unbounded) pool size.
        BufferDescriptor octreeConfigDesc{};
        octreeConfigDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(OCTREE_CONFIG_BUFFER, "octree_config_buffer", ResourceLifetime::Persistent, octreeConfigDesc);

        // Instance SSBO — per-body BodyInstanceGpu records.
        BufferDescriptor instanceDesc{};
        instanceDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(INSTANCE_BUFFER, "body_instance_buffer", ResourceLifetime::Persistent, instanceDesc);

        // Instance count — transient scalar value.
        BufferDescriptor instanceCountDesc{};
        INIT_OUTPUT_DESC(INSTANCE_COUNT, "body_instance_count", ResourceLifetime::Transient, instanceCountDesc);

        // Inc2 M3: SoA-SDF buffer — persistent (created once, survives recompile).
        // Empty placeholder for binary/Procedural bodies (sdfBricks is empty until ConcatenateSdf).
        BufferDescriptor sdfBufferDesc{};
        sdfBufferDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(OCTREE_SDF_BUFFER, "octree_sdf_buffer", ResourceLifetime::Persistent, sdfBufferDesc);

        // Inc2 M3: Brick-grid lookup buffer — persistent.
        BufferDescriptor brickLookupDesc{};
        brickLookupDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(OCTREE_BRICKLOOKUP_BUFFER, "octree_bricklookup_buffer", ResourceLifetime::Persistent, brickLookupDesc);

        // Sparse-Mip ESVO LOD Inc1 M3: mip pool buffer — persistent.
        BufferDescriptor mipPoolDesc{};
        mipPoolDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(OCTREE_MIPPOOL_BUFFER, "octree_mippool_buffer", ResourceLifetime::Persistent, mipPoolDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(BodyOctreeSceneNodeConfig, BodyOctreeSceneNodeCounts);

    // ----- Index validations -----
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(CURRENT_FRAME_INDEX_Slot::index == 2, "CURRENT_FRAME_INDEX must be at index 2");
    static_assert(!CURRENT_FRAME_INDEX_Slot::nullable, "CURRENT_FRAME_INDEX must not be nullable");
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>,
                  "CURRENT_FRAME_INDEX must be uint32_t");
    static_assert(OCTREE_NODES_BUFFER_Slot::index == 0, "OCTREE_NODES_BUFFER must be at index 0");
    static_assert(OCTREE_BRICKS_BUFFER_Slot::index == 1, "OCTREE_BRICKS_BUFFER must be at index 1");
    static_assert(OCTREE_MATERIALS_BUFFER_Slot::index == 2, "OCTREE_MATERIALS_BUFFER must be at index 2");
    static_assert(OCTREE_CONFIG_BUFFER_Slot::index == 3, "OCTREE_CONFIG_BUFFER must be at index 3");
    static_assert(INSTANCE_BUFFER_Slot::index == 4, "INSTANCE_BUFFER must be at index 4");
    static_assert(INSTANCE_COUNT_Slot::index == 5, "INSTANCE_COUNT must be at index 5");
    static_assert(OCTREE_SDF_BUFFER_Slot::index == 6, "OCTREE_SDF_BUFFER must be at index 6");
    static_assert(OCTREE_BRICKLOOKUP_BUFFER_Slot::index == 7, "OCTREE_BRICKLOOKUP_BUFFER must be at index 7");
    static_assert(OCTREE_MIPPOOL_BUFFER_Slot::index == 8, "OCTREE_MIPPOOL_BUFFER must be at index 8");

    // ----- Type validations -----
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<OCTREE_NODES_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_BRICKS_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_MATERIALS_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_CONFIG_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<INSTANCE_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<INSTANCE_COUNT_Slot::Type, int32_t>);
    static_assert(std::is_same_v<OCTREE_SDF_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_BRICKLOOKUP_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_MIPPOOL_BUFFER_Slot::Type, VkBuffer>);
};

} // namespace Vixen::RenderGraph
