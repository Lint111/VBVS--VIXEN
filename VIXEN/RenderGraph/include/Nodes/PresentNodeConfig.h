#pragma once

#include "Core/ResourceConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Pure constexpr resource configuration for PresentNode
 *
 * Inputs:
 * - SWAPCHAIN (VkSwapchainKHR) - Swapchain from SwapChainNode
 * - IMAGE_INDEX (uint32_t) - Index of swapchain image to present
 * - QUEUE (VkQueue) - Present queue from DeviceNode
 * - RENDER_COMPLETE_SEMAPHORE (VkSemaphore) - Semaphore to wait on before presenting
 * - PRESENT_FUNCTION (PFN_vkQueuePresentKHR) - Function pointer to vkQueuePresentKHR
 *
 * Outputs:
 * - PRESENT_RESULT (VkResult*) - Result of the present operation
 *
 * Parameters:
 * - WAIT_FOR_IDLE (bool) - Whether to wait for device idle after present (default: true for compatibility)
 *
 * ALL type checking happens at compile time!
 */
// Compile-time slot counts (declared early for reuse)
namespace PresentNodeCounts {
    static constexpr size_t INPUTS = 5;
    static constexpr size_t OUTPUTS = 1;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(PresentNodeConfig, 
                      PresentNodeCounts::INPUTS, 
                      PresentNodeCounts::OUTPUTS, 
                      PresentNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* WAIT_FOR_IDLE = "waitForIdle";

    // ===== INPUTS (5) =====
    // Swapchain from SwapChainNode
    CONSTEXPR_INPUT(SWAPCHAIN, VkSwapchainKHR, 0, false);

    // Index of swapchain image to present
    CONSTEXPR_INPUT(IMAGE_INDEX, uint32_t, 1, false);

    // Present queue from DeviceNode
    CONSTEXPR_INPUT(QUEUE, VkQueue, 2, false);

    // Semaphore to wait on before presenting (from rendering completion)
    CONSTEXPR_INPUT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 3, false);

    // Function pointer to vkQueuePresentKHR (from device extension)
    CONSTEXPR_INPUT(PRESENT_FUNCTION, PFN_vkQueuePresentKHR, 4, false);

    // ===== OUTPUTS (1) =====
    // Result of the present operation (pointer to VkResult)
    CONSTEXPR_OUTPUT(PRESENT_RESULT, VkResultPtr, 0, false);

    PresentNodeConfig() {
        // Initialize input descriptors
        INIT_INPUT_DESC(SWAPCHAIN, "swapchain",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_INPUT_DESC(IMAGE_INDEX, "image_index",
            ResourceLifetime::Transient,
            BufferDescription{}  // Index value
        );

        INIT_INPUT_DESC(QUEUE, "queue",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_INPUT_DESC(RENDER_COMPLETE_SEMAPHORE, "render_complete_semaphore",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_INPUT_DESC(PRESENT_FUNCTION, "present_function",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Function pointer
        );

        // Initialize output descriptors
        INIT_OUTPUT_DESC(PRESENT_RESULT, "present_result",
            ResourceLifetime::Transient,
            BufferDescription{}  // Result pointer
        );
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == PresentNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == PresentNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == PresentNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(SWAPCHAIN_Slot::index == 0, "SWAPCHAIN must be at index 0");
    static_assert(!SWAPCHAIN_Slot::nullable, "SWAPCHAIN is required");

    static_assert(IMAGE_INDEX_Slot::index == 1, "IMAGE_INDEX must be at index 1");
    static_assert(!IMAGE_INDEX_Slot::nullable, "IMAGE_INDEX is required");

    static_assert(QUEUE_Slot::index == 2, "QUEUE must be at index 2");
    static_assert(!QUEUE_Slot::nullable, "QUEUE is required");

    static_assert(RENDER_COMPLETE_SEMAPHORE_Slot::index == 3, "RENDER_COMPLETE_SEMAPHORE must be at index 3");
    static_assert(!RENDER_COMPLETE_SEMAPHORE_Slot::nullable, "RENDER_COMPLETE_SEMAPHORE is required");

    static_assert(PRESENT_FUNCTION_Slot::index == 4, "PRESENT_FUNCTION must be at index 4");
    static_assert(!PRESENT_FUNCTION_Slot::nullable, "PRESENT_FUNCTION is required");

    static_assert(PRESENT_RESULT_Slot::index == 0, "PRESENT_RESULT must be at index 0");
    static_assert(!PRESENT_RESULT_Slot::nullable, "PRESENT_RESULT is required");

    // Type validations
    static_assert(std::is_same_v<SWAPCHAIN_Slot::Type, VkSwapchainKHR>);
    static_assert(std::is_same_v<IMAGE_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<QUEUE_Slot::Type, VkQueue>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORE_Slot::Type, VkSemaphore>);
    static_assert(std::is_same_v<PRESENT_FUNCTION_Slot::Type, PFN_vkQueuePresentKHR>);
    static_assert(std::is_same_v<PRESENT_RESULT_Slot::Type, VkResultPtr>);
};

// Global compile-time validations
static_assert(PresentNodeConfig::INPUT_COUNT == 5);
static_assert(PresentNodeConfig::OUTPUT_COUNT == 1);

} // namespace Vixen::RenderGraph
