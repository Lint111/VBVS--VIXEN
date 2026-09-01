#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts
namespace BodyOctreeSceneNodeCounts {
    static constexpr size_t INPUTS  = 3;  // VULKAN_DEVICE_IN, COMMAND_POOL, CURRENT_FRAME_INDEX
    // 4 octree buffers + 2 SDF buffers + instance buffer + instance count + mip pool buffer (Inc1 M3)
    // + 2 shell buffers (Surface-Shell ESVO cache, main) + tier-ref table buffer (Tiered-ESVO Inc2 M3)
    // + occupancy grid buffer (Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13) — merge of parallel features.
    // + RTQUERY_TLAS handle (W-RTQUERY Slice A: per-brick-AABB TLAS for the ray_query backend).
    // + raster-proxy AABB buffer and its exact live element count (Slice B2 binder seam).
    static constexpr size_t OUTPUTS = 16;
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
 * INSTANCE_BUFFER (VkBuffer) + INSTANCE_COUNT (int32_t) feed the instanced draw.
 *
 * Inputs: 3 (VULKAN_DEVICE_IN, COMMAND_POOL, CURRENT_FRAME_INDEX)
 * Outputs: 16 (the octree, instance, SDF, shell, tier-ref, occupancy, and RT-query
 *              outputs above, followed by PROXY_AABB_BUFFER + PROXY_AABB_COUNT).
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

    // ===== OUTPUTS (16) =====
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

    // Surface-Shell ESVO cache: the CURRENT read slot's compact pool (binding 11
    // replacement) + grid->shellSlot remap (binding 12 replacement). ExecuteImpl
    // re-emits these each frame with slot [frame&1] so the render binds the last
    // committed shell (mirrors INSTANCE_BUFFER's ring re-emit). Placeholder for
    // binary/Procedural bodies (no shell derived). Data domain is disjoint from
    // OCTREE_MIPPOOL_BUFFER above (near-surface brick compaction vs. interior-node
    // coarse samples) — both coexist as independent slots, no shared addressing.
    OUTPUT_SLOT(SHELL_DATA_BUFFER, VkBuffer, 9,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(SHELL_LOOKUP_BUFFER, VkBuffer, 10,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Tiered-ESVO Inc2 M3: tier-crossing reference table SSBO (binding 15) —
    // one Vixen::SVO::TierRef per registered tier-crossing leaf, concatenated
    // across all resident octrees. Placeholder (1-byte pad) when no tree in
    // the scene has any tier-crossing leaves; populated by MarkLeafAsTierCrossing
    // + Concatenate/ConcatenateSdf (M2's construction path).
    OUTPUT_SLOT(OCTREE_TIERREFTABLE_BUFFER, VkBuffer, 11,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13: concatenated per-recipe coarse
    // occupancy grid SSBO (binding 16) — dim^3 conservative min-|sd| floats per registered
    // procedural recipe, concatenated in registry Ids() order. Placeholder (1-byte pad)
    // when no procedural recipe has a derivable grid (non-whitelisted opcode, or no
    // recipes registered at all); populated via BodyOctreeSceneNode::SetOccupancyGrid.
    OUTPUT_SLOT(OCTREE_OCCUPANCYGRID_BUFFER, VkBuffer, 12,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // W-RTQUERY Slice A: TLAS handle for the ray_query traversal backend (VIXEN_RTQUERY_TRAVERSAL).
    // VK_NULL_HANDLE when the flag is off or the device lacks RTXCapabilities.rayQuery — same
    // "always emitted, placeholder when inactive" convention as the other optional slots above.
    // Deduced as ResourceType::AccelerationStructure from the C++ type alone (see
    // AccelerationStructureNodeConfig::TLAS_HANDLE for the identical precedent); a
    // DescriptorResourceGathererNode binding VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
    // to this slot needs no new gatherer code.
    OUTPUT_SLOT(RTQUERY_TLAS, VkAccelerationStructureKHR, 13,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Raster-proxy Slice B2: current shell read-slot's flattened proxy AABBs and
    // exact live record count. The SSBO allocation is grow-only, so consumers
    // must use PROXY_AABB_COUNT rather than infer a count from buffer capacity.
    OUTPUT_SLOT(PROXY_AABB_BUFFER, VkBuffer, 14,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(PROXY_AABB_COUNT, uint32_t, 15,
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

        // Surface-Shell ESVO cache — compact pool + grid remap (current read slot).
        BufferDescriptor shellDataDesc{};
        shellDataDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(SHELL_DATA_BUFFER, "shell_data_buffer", ResourceLifetime::Persistent, shellDataDesc);

        BufferDescriptor shellLookupDesc{};
        shellLookupDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(SHELL_LOOKUP_BUFFER, "shell_lookup_buffer", ResourceLifetime::Persistent, shellLookupDesc);

        // Tiered-ESVO Inc2 M3: tier-ref table buffer — persistent.
        BufferDescriptor tierRefTableDesc{};
        tierRefTableDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(OCTREE_TIERREFTABLE_BUFFER, "octree_tierreftable_buffer", ResourceLifetime::Persistent, tierRefTableDesc);

        // Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13: occupancy grid buffer — persistent.
        BufferDescriptor occupancyGridDesc{};
        occupancyGridDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(OCTREE_OCCUPANCYGRID_BUFFER, "octree_occupancygrid_buffer", ResourceLifetime::Persistent, occupancyGridDesc);

        // W-RTQUERY Slice A: TLAS handle output — HandleDescriptor mirrors
        // AccelerationStructureNodeConfig::TLAS_HANDLE exactly (same handle-passthrough
        // convention as VULKAN_DEVICE_IN above; not a buffer-usage resource).
        HandleDescriptor rtQueryTlasDesc{"VkAccelerationStructureKHR"};
        INIT_OUTPUT_DESC(RTQUERY_TLAS, "rtquery_tlas", ResourceLifetime::Persistent, rtQueryTlasDesc);

        BufferDescriptor proxyAabbDesc{};
        proxyAabbDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(PROXY_AABB_BUFFER, "proxy_aabb_buffer", ResourceLifetime::Persistent, proxyAabbDesc);

        BufferDescriptor proxyAabbCountDesc{};
        INIT_OUTPUT_DESC(PROXY_AABB_COUNT, "proxy_aabb_count", ResourceLifetime::Transient, proxyAabbCountDesc);
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
    static_assert(SHELL_DATA_BUFFER_Slot::index == 9, "SHELL_DATA_BUFFER must be at index 9");
    static_assert(SHELL_LOOKUP_BUFFER_Slot::index == 10, "SHELL_LOOKUP_BUFFER must be at index 10");
    static_assert(OCTREE_TIERREFTABLE_BUFFER_Slot::index == 11, "OCTREE_TIERREFTABLE_BUFFER must be at index 11");
    static_assert(OCTREE_OCCUPANCYGRID_BUFFER_Slot::index == 12, "OCTREE_OCCUPANCYGRID_BUFFER must be at index 12");
    static_assert(RTQUERY_TLAS_Slot::index == 13, "RTQUERY_TLAS must be at index 13");
    static_assert(PROXY_AABB_BUFFER_Slot::index == 14, "PROXY_AABB_BUFFER must be at index 14");
    static_assert(PROXY_AABB_COUNT_Slot::index == 15, "PROXY_AABB_COUNT must be at index 15");

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
    static_assert(std::is_same_v<SHELL_DATA_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<SHELL_LOOKUP_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_TIERREFTABLE_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_OCCUPANCYGRID_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<RTQUERY_TLAS_Slot::Type, VkAccelerationStructureKHR>);
    static_assert(std::is_same_v<PROXY_AABB_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<PROXY_AABB_COUNT_Slot::Type, uint32_t>);
};

} // namespace Vixen::RenderGraph
