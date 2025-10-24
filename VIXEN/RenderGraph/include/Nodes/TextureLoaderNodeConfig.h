#pragma once

#include "Core/ResourceConfig.h"

namespace Vixen::RenderGraph {

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
    static constexpr size_t INPUTS = 0;
    static constexpr size_t OUTPUTS = 3;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(TextureLoaderNodeConfig,
                      TextureLoaderNodeCounts::INPUTS,
                      TextureLoaderNodeCounts::OUTPUTS,
                      TextureLoaderNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* FILE_PATH = "filePath";
    static constexpr const char* UPLOAD_MODE = "uploadMode";
    static constexpr const char* GENERATE_MIPMAPS = "generateMipmaps";
    static constexpr const char* SAMPLER_FILTER = "samplerFilter";
    static constexpr const char* SAMPLER_ADDRESS_MODE = "samplerAddressMode";

    // ===== OUTPUTS (3) =====
    CONSTEXPR_OUTPUT(TEXTURE_IMAGE, VkImage, 0, false);
    CONSTEXPR_OUTPUT(TEXTURE_VIEW, VkImageView, 1, false);
    CONSTEXPR_OUTPUT(TEXTURE_SAMPLER, VkSampler, 2, false);

    TextureLoaderNodeConfig() {
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
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == TextureLoaderNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == TextureLoaderNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == TextureLoaderNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(TEXTURE_IMAGE_Slot::index == 0, "TEXTURE_IMAGE must be at index 0");
    static_assert(!TEXTURE_IMAGE_Slot::nullable, "TEXTURE_IMAGE is required");

    static_assert(TEXTURE_VIEW_Slot::index == 1, "TEXTURE_VIEW must be at index 1");
    static_assert(!TEXTURE_VIEW_Slot::nullable, "TEXTURE_VIEW is required");

    static_assert(TEXTURE_SAMPLER_Slot::index == 2, "TEXTURE_SAMPLER must be at index 2");
    static_assert(!TEXTURE_SAMPLER_Slot::nullable, "TEXTURE_SAMPLER is required");
};

} // namespace Vixen::RenderGraph
