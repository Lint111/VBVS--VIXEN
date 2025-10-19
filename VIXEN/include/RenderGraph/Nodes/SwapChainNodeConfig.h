#pragma once

#include "RenderGraph/ResourceConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Pure constexpr resource configuration for SwapChainNode
 *
 * Demonstrates:
 * - Input slot (SURFACE from WindowNode)
 * - Multiple output slots
 * - Optional/nullable resources (semaphores)
 *
 * ALL type checking happens at compile time!
 */
CONSTEXPR_NODE_CONFIG(SwapChainNodeConfig, 1, 3, false) {
    // ===== INPUTS (1) =====
    // Required surface from WindowNode
    CONSTEXPR_INPUT(SURFACE, VkSurfaceKHR, 0, false);

    // ===== OUTPUTS (3) =====
    // Swapchain images (required)
    CONSTEXPR_OUTPUT(SWAPCHAIN_IMAGES, VkImage, 0, false);

    // Synchronization semaphores (optional - node creates internally if not provided)
    CONSTEXPR_OUTPUT(IMAGE_AVAILABLE_SEM, VkSemaphore, 1, true);   // Nullable!
    CONSTEXPR_OUTPUT(RENDER_FINISHED_SEM, VkSemaphore, 2, true);   // Nullable!

    SwapChainNodeConfig() {
        // Initialize input descriptor
        INIT_INPUT_DESC(SURFACE, "surface",
            ResourceLifetime::Persistent,
            ImageDescription{}
        );

        // Initialize output descriptors
        INIT_OUTPUT_DESC(SWAPCHAIN_IMAGES, "swapchain_images",
            ResourceLifetime::Persistent,
            ImageDescription{
                .width = 0,  // Queried from surface
                .height = 0,
                .format = VK_FORMAT_UNDEFINED,
                .usage = ResourceUsage::ColorAttachment | ResourceUsage::TransferDst
            }
        );

        INIT_OUTPUT_DESC(IMAGE_AVAILABLE_SEM, "image_available_semaphore",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_OUTPUT_DESC(RENDER_FINISHED_SEM, "render_finished_semaphore",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );
    }

    // Compile-time validations (optional but recommended)
    static_assert(SURFACE_Slot::index == 0, "SURFACE input must be at index 0");
    static_assert(!SURFACE_Slot::nullable, "SURFACE input is required");

    static_assert(SWAPCHAIN_IMAGES_Slot::index == 0, "SWAPCHAIN_IMAGES must be at index 0");
    static_assert(!SWAPCHAIN_IMAGES_Slot::nullable, "SWAPCHAIN_IMAGES is required");

    static_assert(IMAGE_AVAILABLE_SEM_Slot::nullable, "IMAGE_AVAILABLE_SEM must be optional");
    static_assert(RENDER_FINISHED_SEM_Slot::nullable, "RENDER_FINISHED_SEM must be optional");

    // Type validations
    static_assert(std::is_same_v<SURFACE_Slot::Type, VkSurfaceKHR>);
    static_assert(std::is_same_v<SWAPCHAIN_IMAGES_Slot::Type, VkImage>);
    static_assert(std::is_same_v<IMAGE_AVAILABLE_SEM_Slot::Type, VkSemaphore>);
    static_assert(std::is_same_v<RENDER_FINISHED_SEM_Slot::Type, VkSemaphore>);
};

// Global compile-time validations
static_assert(SwapChainNodeConfig::INPUT_COUNT == 1);
static_assert(SwapChainNodeConfig::OUTPUT_COUNT == 3);

} // namespace Vixen::RenderGraph
