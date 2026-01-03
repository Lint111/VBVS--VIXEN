#pragma once
#include "Data/Core/ResourceConfig.h"
#include "Data/Nodes/VoxelAABBConverterNodeConfig.h"  // Includes CashSystem types
#include "AccelerationStructureCacher.h"  // ASBuildMode enum
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;
using RTXCapabilities = Vixen::Vulkan::Resources::RTXCapabilities;

// ============================================================================
// ACCELERATION STRUCTURE DATA (Phase K - Hardware RT)
// ============================================================================
// Use type from CashSystem to avoid duplication.
// The cacher owns the BLAS/TLAS build logic and data structures.

using AccelerationStructureData = CashSystem::AccelerationStructureData;
using ASBuildMode = CashSystem::ASBuildMode;

// ============================================================================
// NODE CONFIG
// ============================================================================

namespace AccelerationStructureNodeCounts {
    static constexpr size_t INPUTS = 5;  // VULKAN_DEVICE_IN, COMMAND_POOL, AABB_DATA, IMAGE_INDEX, BUILD_MODE
    static constexpr size_t OUTPUTS = 2;  // ACCELERATION_STRUCTURE_DATA + TLAS_HANDLE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for AccelerationStructureNode
 *
 * Builds BLAS from voxel AABBs and TLAS. Supports two modes:
 * - Static (default): Single TLAS built once during Compile, cached
 * - Dynamic: Per-frame TLAS rebuild from mutable instance list
 *
 * Inputs: 5
 * - VULKAN_DEVICE_IN, COMMAND_POOL, AABB_DATA: Required for both modes
 * - IMAGE_INDEX: Optional, required for Dynamic mode (per-frame indexing)
 * - BUILD_MODE: Optional, defaults to Static
 *
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

    // Swapchain image index for Dynamic mode (per-frame TLAS selection)
    // Optional - only required when BUILD_MODE is Dynamic
    INPUT_SLOT(IMAGE_INDEX, uint32_t, 3,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Build mode selection (Static by default)
    // Static: Build once during Compile, cache BLAS+TLAS
    // Dynamic: Cache BLAS, rebuild TLAS per-frame from instances
    INPUT_SLOT(BUILD_MODE, ASBuildMode, 4,
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

        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_INPUT_DESC(IMAGE_INDEX, "image_index", ResourceLifetime::Transient, uint32Desc);

        HandleDescriptor buildModeDesc{"ASBuildMode"};
        INIT_INPUT_DESC(BUILD_MODE, "build_mode", ResourceLifetime::Transient, buildModeDesc);

        HandleDescriptor accelStructDesc{"AccelerationStructureData*"};
        INIT_OUTPUT_DESC(ACCELERATION_STRUCTURE_DATA, "acceleration_structure", ResourceLifetime::Persistent, accelStructDesc);

        HandleDescriptor tlasHandleDesc{"VkAccelerationStructureKHR"};
        INIT_OUTPUT_DESC(TLAS_HANDLE, "tlas_handle", ResourceLifetime::Persistent, tlasHandleDesc);
    }

    VALIDATE_NODE_CONFIG(AccelerationStructureNodeConfig, AccelerationStructureNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0);
    static_assert(COMMAND_POOL_Slot::index == 1);
    static_assert(AABB_DATA_Slot::index == 2);
    static_assert(IMAGE_INDEX_Slot::index == 3);
    static_assert(BUILD_MODE_Slot::index == 4);
    static_assert(ACCELERATION_STRUCTURE_DATA_Slot::index == 0);
    static_assert(TLAS_HANDLE_Slot::index == 1);
};

} // namespace Vixen::RenderGraph
