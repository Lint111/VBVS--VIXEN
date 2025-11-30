#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDevice.h"

// Forward declaration for debug capture
namespace Vixen::RenderGraph::Debug {
    class IDebugCapture;
}

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts
namespace VoxelGridNodeCounts {
    static constexpr size_t INPUTS = 2;
    static constexpr size_t OUTPUTS = 6;  // OCTREE_NODES, OCTREE_BRICKS, OCTREE_MATERIALS, DEBUG_CAPTURE_BUFFER, OCTREE_CONFIG, BRICK_BASE_INDEX
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for VoxelGridNode
 *
 * Generates procedural voxel scenes and uploads sparse octree to GPU.
 * Outputs SSBO buffers for octree-based ray marching.
 *
 * Inputs: 2 (VULKAN_DEVICE_IN, COMMAND_POOL)
 * Outputs: 6 (OCTREE_NODES_BUFFER, OCTREE_BRICKS_BUFFER, OCTREE_MATERIALS_BUFFER, DEBUG_CAPTURE_BUFFER, OCTREE_CONFIG_BUFFER, BRICK_BASE_INDEX_BUFFER)
 */
CONSTEXPR_NODE_CONFIG(VoxelGridNodeConfig,
                      VoxelGridNodeCounts::INPUTS,
                      VoxelGridNodeCounts::OUTPUTS,
                      VoxelGridNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (2) =====
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

    // Debug capture buffer - outputs VkBuffer; connect with SlotRole::Debug
    // When connecting: batch.ConnectVariadic(voxelNode, DEBUG_CAPTURE_BUFFER, gatherer, binding, SlotRole::Debug);
    // The IDebugCapture interface is attached via ctx.OutWithInterface() so gatherer can collect it
    OUTPUT_SLOT(DEBUG_CAPTURE_BUFFER, VkBuffer, 3,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // Octree configuration UBO - contains scale parameters for shader
    // Eliminates hard-coded constants in shader, allows runtime configuration
    OUTPUT_SLOT(OCTREE_CONFIG_BUFFER, VkBuffer, 4,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Brick base index buffer - per-node mapping to sparse brick array (binding 6)
    // For nodes at brickESVOScale with leafMask != 0, stores the starting index
    // into the sparse brickData array. 0xFFFFFFFF = no bricks at this node.
    OUTPUT_SLOT(BRICK_BASE_INDEX_BUFFER, VkBuffer, 5,
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

        // Debug capture buffer - for ray traversal analysis
        BufferDescriptor debugCaptureDesc{};
        debugCaptureDesc.size = 2048 * 64;  // Default: 2048 samples * ~64 bytes per DebugRaySample
        debugCaptureDesc.usage = ResourceUsage::StorageBuffer;
        INIT_OUTPUT_DESC(DEBUG_CAPTURE_BUFFER, "debug_capture_buffer", ResourceLifetime::Persistent, debugCaptureDesc);

        // Octree config UBO - scale and grid parameters
        BufferDescriptor octreeConfigDesc{};
        octreeConfigDesc.size = 64;  // OctreeConfig struct padded to 64 bytes (std140 alignment)
        octreeConfigDesc.usage = ResourceUsage::UniformBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(OCTREE_CONFIG_BUFFER, "octree_config_buffer", ResourceLifetime::Persistent, octreeConfigDesc);

        // Brick base index buffer - one uint32 per node
        BufferDescriptor brickBaseIndexDesc{};
        brickBaseIndexDesc.size = 4096 * 4;  // Initial capacity: 4096 nodes * 4 bytes
        brickBaseIndexDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(BRICK_BASE_INDEX_BUFFER, "brick_base_index_buffer", ResourceLifetime::Persistent, brickBaseIndexDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(VoxelGridNodeConfig, VoxelGridNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(OCTREE_NODES_BUFFER_Slot::index == 0, "OCTREE_NODES_BUFFER must be at index 0");
    static_assert(OCTREE_BRICKS_BUFFER_Slot::index == 1, "OCTREE_BRICKS_BUFFER must be at index 1");
    static_assert(OCTREE_MATERIALS_BUFFER_Slot::index == 2, "OCTREE_MATERIALS_BUFFER must be at index 2");
    static_assert(DEBUG_CAPTURE_BUFFER_Slot::index == 3, "DEBUG_CAPTURE_BUFFER must be at index 3");
    static_assert(OCTREE_CONFIG_BUFFER_Slot::index == 4, "OCTREE_CONFIG_BUFFER must be at index 4");
    static_assert(BRICK_BASE_INDEX_BUFFER_Slot::index == 5, "BRICK_BASE_INDEX_BUFFER must be at index 5");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<OCTREE_NODES_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_BRICKS_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_MATERIALS_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<DEBUG_CAPTURE_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_CONFIG_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<BRICK_BASE_INDEX_BUFFER_Slot::Type, VkBuffer>);
};

} // namespace Vixen::RenderGraph
