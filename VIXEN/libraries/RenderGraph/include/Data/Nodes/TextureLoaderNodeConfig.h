#pragma once

#include "Data/Core/ResourceConfig.h"
#include "VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

/**
 * @brief Pure constexpr resource configuration for TextureLoaderNode
 * 
 * Loads textures from disk and uploads to GPU memory.
 * 
 * Inputs: None (loads from file parameter)
 * 
 * Outputs:
 * - TEXTURE_IMAGE (VkImage) - Loaded texture image
 * - TEXTURE_VIEW (VkImageView) - Image view for shader access
 * - TEXTURE_SAMPLER (VkSampler) - Configured sampler
 * 
 * Parameters:
 * - FILE_PATH (string) - Path to texture file
 * - UPLOAD_MODE (string) - "Optimal" or "Linear"
 * - GENERATE_MIPMAPS (bool) - Whether to generate mipmaps
 * - SAMPLER_FILTER (string) - "Linear" or "Nearest"
 * - SAMPLER_ADDRESS_MODE (string) - "Repeat", "Clamp", or "Mirror"
 * 
 * Type ID: 112
 */
namespace TextureLoaderNodeCounts {
    static constexpr size_t INPUTS = 1;  // Changed from 0
    static constexpr size_t OUTPUTS = 4; // Changed from 3
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(TextureLoaderNodeConfig,
                      TextureLoaderNodeCounts::INPUTS,
                      TextureLoaderNodeCounts::OUTPUTS,
                      TextureLoaderNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (1) =====
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== PARAMETER NAMES =====
    static constexpr const char* FILE_PATH = "filePath";
    static constexpr const char* UPLOAD_MODE = "uploadMode";
    static constexpr const char* GENERATE_MIPMAPS = "generateMipmaps";
    static constexpr const char* SAMPLER_FILTER = "samplerFilter";
    static constexpr const char* SAMPLER_ADDRESS_MODE = "samplerAddressMode";

    // ===== OUTPUTS (4) =====
    OUTPUT_SLOT(TEXTURE_IMAGE, VkImage, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(TEXTURE_VIEW, VkImageView, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(TEXTURE_SAMPLER, VkSampler, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevice*, 3,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    TextureLoaderNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        // Initialize output descriptors
        INIT_OUTPUT_DESC(TEXTURE_IMAGE, "texture_image",
            ResourceLifetime::Persistent,
            ImageDescription{}
        );

        INIT_OUTPUT_DESC(TEXTURE_VIEW, "texture_view",
            ResourceLifetime::Persistent,
            ImageDescription{}
        );

        INIT_OUTPUT_DESC(TEXTURE_SAMPLER, "texture_sampler",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "VulkanDevice*",
            ResourceLifetime::Persistent,
            vulkanDeviceDesc
        );
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(TextureLoaderNodeConfig, TextureLoaderNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE input must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE input is required");
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);

    static_assert(TEXTURE_IMAGE_Slot::index == 0, "TEXTURE_IMAGE must be at index 0");
    static_assert(!TEXTURE_IMAGE_Slot::nullable, "TEXTURE_IMAGE is required");

    static_assert(TEXTURE_VIEW_Slot::index == 1, "TEXTURE_VIEW must be at index 1");
    static_assert(!TEXTURE_VIEW_Slot::nullable, "TEXTURE_VIEW is required");

    static_assert(TEXTURE_SAMPLER_Slot::index == 2, "TEXTURE_SAMPLER must be at index 2");
    static_assert(!TEXTURE_SAMPLER_Slot::nullable, "TEXTURE_SAMPLER is required");

    static_assert(VULKAN_DEVICE_OUT_Slot::index == 3, "DEVICE_OUT must be at index 3");
    static_assert(!VULKAN_DEVICE_OUT_Slot::nullable, "DEVICE_OUT is required");
    static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevice*>, "DEVICE_OUT must be VkDevice");
};

} // namespace Vixen::RenderGraph
