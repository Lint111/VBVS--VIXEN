#pragma once

#include "Data/Core/ResourceConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Spreads SwapChainPublicVariables struct into individual typed outputs
 *
 * Takes SwapChainPublicVariables* pointer and exposes its members as separate outputs.
 * This allows downstream nodes to access specific swapchain resources without
 * needing to understand the whole struct.
 *
 * Input:
 * - SWAPCHAIN_PUBLIC (SwapChainPublicVariables*) - Pointer to swapchain public state
 *
 * Outputs:
 * - IMAGE_VIEWS (std::vector<VkImageView>*) - Pointer to swapchain image views array
 * - IMAGES (std::vector<VkImage>*) - Pointer to swapchain images array
 * - IMAGE_COUNT (uint32_t) - Number of swapchain images
 * - FORMAT (VkFormat) - Swapchain image format
 * - EXTENT (VkExtent2D) - Swapchain image extent
 *
 * Type ID: 120
 */
namespace SwapChainStructSpreaderNodeCounts {
    static constexpr size_t INPUTS = 1;
    static constexpr size_t OUTPUTS = 5;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(SwapChainStructSpreaderNodeConfig,
                      SwapChainStructSpreaderNodeCounts::INPUTS,
                      SwapChainStructSpreaderNodeCounts::OUTPUTS,
                      SwapChainStructSpreaderNodeCounts::ARRAY_MODE) {

    // ===== INPUTS (1) =====
    INPUT_SLOT(SWAPCHAIN_PUBLIC, SwapChainPublicVariablesPtr, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (5) =====
    OUTPUT_SLOT(IMAGE_VIEWS, VkImageViewVectorPtr, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(IMAGES, VkImageVectorPtr, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(IMAGE_COUNT, uint32_t, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(FORMAT, VkFormat, 3,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(EXTENT, VkExtent2D, 4,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    SwapChainStructSpreaderNodeConfig() {
        HandleDescriptor publicVarsDesc{"SwapChainPublicVariables*"};
        INIT_INPUT_DESC(SWAPCHAIN_PUBLIC, "swapchain_public",
            ResourceLifetime::Persistent,
            publicVarsDesc);

        HandleDescriptor imageViewsDesc{"std::vector<VkImageView>*"};
        INIT_OUTPUT_DESC(IMAGE_VIEWS, "image_views",
            ResourceLifetime::Persistent,
            imageViewsDesc);

        HandleDescriptor imagesDesc{"std::vector<VkImage>*"};
        INIT_OUTPUT_DESC(IMAGES, "images",
            ResourceLifetime::Persistent,
            imagesDesc);

        HandleDescriptor countDesc{"uint32_t"};
        INIT_OUTPUT_DESC(IMAGE_COUNT, "image_count",
            ResourceLifetime::Persistent,
            countDesc);

        HandleDescriptor formatDesc{"VkFormat"};
        INIT_OUTPUT_DESC(FORMAT, "format",
            ResourceLifetime::Persistent,
            formatDesc);

        HandleDescriptor extentDesc{"VkExtent2D"};
        INIT_OUTPUT_DESC(EXTENT, "extent",
            ResourceLifetime::Persistent,
            extentDesc);
    }

    static_assert(INPUT_COUNT == SwapChainStructSpreaderNodeCounts::INPUTS);
    static_assert(OUTPUT_COUNT == SwapChainStructSpreaderNodeCounts::OUTPUTS);
    static_assert(ARRAY_MODE == SwapChainStructSpreaderNodeCounts::ARRAY_MODE);
};

} // namespace Vixen::RenderGraph
