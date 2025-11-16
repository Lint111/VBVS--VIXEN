#pragma once

#include "Data/Core/ResourceConfig.h"
#include "VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

/**
 * @brief Pure constexpr resource configuration for PresentNode
 *
 * Inputs:
 * - VULKAN_DEVICE (VulkanDevice*) - VulkanDevice containing device, queue, etc.
 * - SWAPCHAIN (VkSwapchainKHR) - Swapchain from SwapChainNode
 * - IMAGE_INDEX (uint32_t) - Index of swapchain image to present
 * - RENDER_COMPLETE_SEMAPHORE (VkSemaphore) - Semaphore to wait on before presenting
 * - PRESENT_FUNCTION (PFN_vkQueuePresentKHR) - Function pointer to vkQueuePresentKHR
 *
 * Outputs:
 * - PRESENT_RESULT (VkResult*) - Result of the present operation
 * - DEVICE_OUT (VulkanDevice*) - VulkanDevice containing device, queue, etc.
 *
 * Parameters:
 * - WAIT_FOR_IDLE (bool) - Whether to wait for device idle after present (default: true for compatibility)
 *
 * ALL type checking happens at compile time!
 */
// Compile-time slot counts (declared early for reuse)
namespace PresentNodeCounts {
    static constexpr size_t INPUTS = 6;
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(PresentNodeConfig, 
                      PresentNodeCounts::INPUTS, 
                      PresentNodeCounts::OUTPUTS, 
                      PresentNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* WAIT_FOR_IDLE = "waitForIdle";

    // ===== INPUTS (6) =====
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(SWAPCHAIN, VkSwapchainKHR, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_INDEX, uint32_t, 2,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 3,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(PRESENT_FUNCTION, PFN_vkQueuePresentKHR, 4,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(PRESENT_FENCE_ARRAY, const std::vector<VkFence>&, 5,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (2) =====
    OUTPUT_SLOT(PRESENT_RESULT, VkResult*, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevice*, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    PresentNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device",
            ResourceLifetime::Persistent,
            vulkanDeviceDesc
        );

        INIT_INPUT_DESC(SWAPCHAIN, "swapchain",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_INPUT_DESC(IMAGE_INDEX, "image_index",
            ResourceLifetime::Transient,
            BufferDescription{}  // Index value
        );

        INIT_INPUT_DESC(RENDER_COMPLETE_SEMAPHORE, "render_complete_semaphore",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle
        );

        INIT_INPUT_DESC(PRESENT_FENCE_ARRAY, "present_fence_array",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle array
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

        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "VulkanDevice",
            ResourceLifetime::Transient,
            HandleDescriptor{}
        );
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(PresentNodeConfig, PresentNodeCounts);

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN_Slot must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE_IN_Slot is required");

    static_assert(SWAPCHAIN_Slot::index == 1, "SWAPCHAIN must be at index 1");
    static_assert(!SWAPCHAIN_Slot::nullable, "SWAPCHAIN is required");

    static_assert(IMAGE_INDEX_Slot::index == 2, "IMAGE_INDEX must be at index 2");
    static_assert(!IMAGE_INDEX_Slot::nullable, "IMAGE_INDEX is required");

    static_assert(RENDER_COMPLETE_SEMAPHORE_Slot::index == 3, "RENDER_COMPLETE_SEMAPHORE must be at index 3");
    static_assert(!RENDER_COMPLETE_SEMAPHORE_Slot::nullable, "RENDER_COMPLETE_SEMAPHORE is required");

    static_assert(PRESENT_FUNCTION_Slot::index == 4, "PRESENT_FUNCTION must be at index 4");
    static_assert(PRESENT_FUNCTION_Slot::nullable, "PRESENT_FUNCTION is optional (uses VulkanDevice::GetPresentFunction())");

    static_assert(PRESENT_FENCE_ARRAY_Slot::index == 5, "PRESENT_FENCE_ARRAY must be at index 5");
    static_assert(PRESENT_FENCE_ARRAY_Slot::nullable, "PRESENT_FENCE_ARRAY is optional");

    static_assert(PRESENT_RESULT_Slot::index == 0, "PRESENT_RESULT must be at index 0");
    static_assert(!PRESENT_RESULT_Slot::nullable, "PRESENT_RESULT is required");

    static_assert(VULKAN_DEVICE_OUT_Slot::index == 1, "VULKAN_DEVICE_OUT must be at index 1");
    static_assert(!VULKAN_DEVICE_OUT_Slot::nullable, "VULKAN_DEVICE_OUT is required");
    static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevice*>, "VULKAN_DEVICE_OUT must be VulkanDevice");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<SWAPCHAIN_Slot::Type, VkSwapchainKHR>);
    static_assert(std::is_same_v<IMAGE_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORE_Slot::Type, VkSemaphore>);
    static_assert(std::is_same_v<PRESENT_FENCE_ARRAY_Slot::Type, std::vector<VkFence>>);
    static_assert(std::is_same_v<PRESENT_FUNCTION_Slot::Type, PFN_vkQueuePresentKHR>);
    static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevice*>);
};

} // namespace Vixen::RenderGraph

