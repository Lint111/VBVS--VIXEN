#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

// CRITICAL: Full includes required for wrapper types with conversion_type
// The Resource::SetHandle() template uses HasConversionType_v<T> to detect
// wrapper types that convert to VkBuffer/VkImageView/etc. This SFINAE check
// requires the full class definition to see the conversion_type typedef.
// Forward declarations cause HasConversionType_v to return false, which breaks
// descriptor extraction in DescriptorResourceGathererNode.
// See: CompileTimeResourceSystem.h HasConversionType_v, HacknPlan #61
#include "Debug/ShaderCountersBuffer.h"
#include "Debug/RayTraceBuffer.h"

// Forward declarations for non-wrapper types (don't need conversion_type detection)
namespace Vixen::RenderGraph::Debug {
    class IDebugCapture;
}

// Forward declaration for CashSystem cached scene data
namespace CashSystem {
    struct VoxelSceneData;
}

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts
namespace VoxelGridNodeCounts {
    static constexpr size_t INPUTS = 2;
    static constexpr size_t OUTPUTS = 10;  // Slots 0-9: 3 octree + debug + config + 2 compressed + brick grid + scene data + shader counters
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}
// NOTE: Wrapper types (RayTraceBuffer*, ShaderCountersBuffer*) are used for slots
// that need both descriptor binding AND CPU readback. The Resource system
// automatically extracts VkBuffer via conversion_type for descriptor binding.

/**
 * @brief Configuration for VoxelGridNode
 *
 * Generates procedural voxel scenes and uploads sparse octree to GPU.
 * Outputs SSBO buffers for octree-based ray marching.
 *
 * Inputs: 2 (VULKAN_DEVICE_IN, COMMAND_POOL)
 * Outputs: 9 (OCTREE_NODES_BUFFER, OCTREE_BRICKS_BUFFER, OCTREE_MATERIALS_BUFFER, DEBUG_CAPTURE_BUFFER, OCTREE_CONFIG_BUFFER, COMPRESSED_COLOR_BUFFER, COMPRESSED_NORMAL_BUFFER, BRICK_GRID_LOOKUP_BUFFER)
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

    // Debug capture buffer - wrapper type with conversion_type = VkBuffer
    // Resource system automatically extracts VkBuffer for descriptor binding
    // Node also provides ReadDebugCapture() for CPU-side access
    OUTPUT_SLOT(DEBUG_CAPTURE_BUFFER, Debug::RayTraceBuffer*, 3,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // Octree configuration UBO - contains scale parameters for shader
    // Eliminates hard-coded constants in shader, allows runtime configuration
    OUTPUT_SLOT(OCTREE_CONFIG_BUFFER, VkBuffer, 4,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // DXT compressed color buffer (shader binding 6)
    // 32 DXT1 blocks per brick, 8 bytes (uvec2) per block = 256 bytes/brick
    // Optional: only populated if octree provides compressed data
    OUTPUT_SLOT(COMPRESSED_COLOR_BUFFER, VkBuffer, 6,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // DXT compressed normal buffer (shader binding 7)
    // 32 DXT blocks per brick, 16 bytes (uvec4) per block = 512 bytes/brick
    // Optional: only populated if octree provides compressed data
    OUTPUT_SLOT(COMPRESSED_NORMAL_BUFFER, VkBuffer, 5,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // Brick grid lookup buffer - maps (brickX, brickY, brickZ) to brick index
    // Size: bricksPerAxis^3 * sizeof(uint32_t)
    // Value: brick index (0 to numBricks-1) or 0xFFFFFFFF for empty bricks
    // Used by hardware RT shaders to look up correct compressed buffer offset
    OUTPUT_SLOT(BRICK_GRID_LOOKUP_BUFFER, VkBuffer, 7,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // Cached voxel scene data - provides readonly reference for AccelerationStructureNode
    // Contains CPU+GPU scene data created by VoxelSceneCacher
    // Used by AccelerationStructureCacher to build BLAS/TLAS from scene geometry
    OUTPUT_SLOT(VOXEL_SCENE_DATA, CashSystem::VoxelSceneData*, 8,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // Shader counters buffer - wrapper type with conversion_type = VkBuffer
    // Resource system automatically extracts VkBuffer for descriptor binding
    // Node also provides ReadShaderCounters() for CPU-side access
    OUTPUT_SLOT(SHADER_COUNTERS_BUFFER, Debug::ShaderCountersBuffer*, 9,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    // Slot index verification: 0-9 = 10 slots total
    static_assert(VoxelGridNodeCounts::OUTPUTS == 10, "OUTPUT slot count mismatch");

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

        // Compressed color buffer - DXT1 blocks (256 bytes/brick, 8 bytes/block * 32 blocks)
        BufferDescriptor compressedColorDesc{};
        compressedColorDesc.size = 1024 * 256;  // Initial capacity: 1024 bricks * 256 bytes
        compressedColorDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(COMPRESSED_COLOR_BUFFER, "compressed_color_buffer", ResourceLifetime::Persistent, compressedColorDesc);

        // Compressed normal buffer - DXT blocks (512 bytes/brick, 16 bytes/block * 32 blocks)
        BufferDescriptor compressedNormalDesc{};
        compressedNormalDesc.size = 1024 * 512;  // Initial capacity: 1024 bricks * 512 bytes
        compressedNormalDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(COMPRESSED_NORMAL_BUFFER, "compressed_normal_buffer", ResourceLifetime::Persistent, compressedNormalDesc);

        // Brick grid lookup buffer - maps grid coords to brick index
        BufferDescriptor brickGridLookupDesc{};
        brickGridLookupDesc.size = 64 * 64 * 64 * sizeof(uint32_t);  // Max 64^3 bricks = 1MB
        brickGridLookupDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferDst;
        INIT_OUTPUT_DESC(BRICK_GRID_LOOKUP_BUFFER, "brick_grid_lookup_buffer", ResourceLifetime::Persistent, brickGridLookupDesc);

        // Cached scene data handle - provides readonly reference for downstream nodes
        HandleDescriptor voxelSceneDataDesc{"CashSystem::VoxelSceneData*"};
        INIT_OUTPUT_DESC(VOXEL_SCENE_DATA, "voxel_scene_data", ResourceLifetime::Persistent, voxelSceneDataDesc);

        // Shader counters buffer - 64 bytes (GPUShaderCounters struct)
        BufferDescriptor shaderCountersDesc{};
        shaderCountersDesc.size = 64;  // sizeof(GPUShaderCounters) = 64 bytes
        shaderCountersDesc.usage = ResourceUsage::StorageBuffer;
        INIT_OUTPUT_DESC(SHADER_COUNTERS_BUFFER, "shader_counters_buffer", ResourceLifetime::Persistent, shaderCountersDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(VoxelGridNodeConfig, VoxelGridNodeCounts);

    // Index validations - verify slot indices are contiguous (0-9)
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(OCTREE_NODES_BUFFER_Slot::index == 0, "OCTREE_NODES_BUFFER must be at index 0");
    static_assert(OCTREE_BRICKS_BUFFER_Slot::index == 1, "OCTREE_BRICKS_BUFFER must be at index 1");
    static_assert(OCTREE_MATERIALS_BUFFER_Slot::index == 2, "OCTREE_MATERIALS_BUFFER must be at index 2");
    static_assert(DEBUG_CAPTURE_BUFFER_Slot::index == 3, "DEBUG_CAPTURE_BUFFER must be at index 3");
    static_assert(OCTREE_CONFIG_BUFFER_Slot::index == 4, "OCTREE_CONFIG_BUFFER must be at index 4");
    static_assert(COMPRESSED_NORMAL_BUFFER_Slot::index == 5, "COMPRESSED_NORMAL_BUFFER must be at index 5");
    static_assert(COMPRESSED_COLOR_BUFFER_Slot::index == 6, "COMPRESSED_COLOR_BUFFER must be at index 6");
    static_assert(BRICK_GRID_LOOKUP_BUFFER_Slot::index == 7, "BRICK_GRID_LOOKUP_BUFFER must be at index 7");
    static_assert(VOXEL_SCENE_DATA_Slot::index == 8, "VOXEL_SCENE_DATA must be at index 8");
    static_assert(SHADER_COUNTERS_BUFFER_Slot::index == 9, "SHADER_COUNTERS_BUFFER must be at index 9");

    // Type validations - wrapper types use conversion_type for descriptor extraction
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<OCTREE_NODES_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_BRICKS_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<OCTREE_MATERIALS_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<DEBUG_CAPTURE_BUFFER_Slot::Type, Debug::RayTraceBuffer*>);  // Wrapper with conversion_type = VkBuffer
    static_assert(std::is_same_v<OCTREE_CONFIG_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<COMPRESSED_COLOR_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<COMPRESSED_NORMAL_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<BRICK_GRID_LOOKUP_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<VOXEL_SCENE_DATA_Slot::Type, CashSystem::VoxelSceneData*>);
    static_assert(std::is_same_v<SHADER_COUNTERS_BUFFER_Slot::Type, Debug::ShaderCountersBuffer*>);  // Wrapper with conversion_type = VkBuffer
};

} // namespace Vixen::RenderGraph
