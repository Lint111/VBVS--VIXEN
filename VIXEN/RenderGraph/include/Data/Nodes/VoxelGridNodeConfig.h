#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;

// Compile-time slot counts
namespace VoxelGridNodeCounts {
    static constexpr size_t INPUTS = 2;
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for VoxelGridNode
 *
 * Generates or loads 3D voxel texture for raymarching.
 * Outputs combined image/sampler pair for shader binding.
 *
 * Inputs: 2 (VULKAN_DEVICE_IN, COMMAND_POOL)
 * Outputs: 2 (VOXEL_IMAGE, VOXEL_COMBINED_SAMPLER)
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

    // ===== OUTPUTS (2) =====
    OUTPUT_SLOT(VOXEL_IMAGE, VkImage, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(VOXEL_COMBINED_SAMPLER, ImageSamplerPair, 1,
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

        // Initialize output descriptors
        Texture3DDescriptor voxelImageDesc{};
        voxelImageDesc.format = VK_FORMAT_R8_UNORM;
        voxelImageDesc.width = 128;
        voxelImageDesc.height = 128;
        voxelImageDesc.depth = 128;
        voxelImageDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        INIT_OUTPUT_DESC(VOXEL_IMAGE, "voxel_image", ResourceLifetime::Persistent, voxelImageDesc);

        HandleDescriptor combinedSamplerDesc{"ImageSamplerPair"};
        INIT_OUTPUT_DESC(VOXEL_COMBINED_SAMPLER, "voxel_combined_sampler", ResourceLifetime::Persistent, combinedSamplerDesc);
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == VoxelGridNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == VoxelGridNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == VoxelGridNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(VOXEL_IMAGE_Slot::index == 0, "VOXEL_IMAGE must be at index 0");
    static_assert(VOXEL_COMBINED_SAMPLER_Slot::index == 1, "VOXEL_COMBINED_SAMPLER must be at index 1");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<VOXEL_IMAGE_Slot::Type, VkImage>);
    static_assert(std::is_same_v<VOXEL_COMBINED_SAMPLER_Slot::Type, ImageSamplerPair>);
};

// Global compile-time validations
static_assert(VoxelGridNodeConfig::INPUT_COUNT == VoxelGridNodeCounts::INPUTS);
static_assert(VoxelGridNodeConfig::OUTPUT_COUNT == VoxelGridNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph
