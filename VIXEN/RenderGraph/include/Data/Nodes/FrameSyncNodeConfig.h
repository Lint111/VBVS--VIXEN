#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"
#include "Data/Core/ResourceVariant.h"

namespace Vixen::RenderGraph {


// Compile-time slot counts (declared early for reuse)
namespace FrameSyncNodeCounts {
    static constexpr size_t INPUTS = 1;  // Back to 1: only VulkanDevice
    static constexpr size_t OUTPUTS = 5;  // Phase 0.6: Added PRESENT_FENCES_ARRAY for VK_KHR_swapchain_maintenance1
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for FrameSyncNode
 *
 * Phase 0.6: CORRECT two-tier synchronization per Vulkan validation guide
 * https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
 *
 * - Creates MAX_FRAMES_IN_FLIGHT fences for CPU-GPU sync
 * - Creates MAX_FRAMES_IN_FLIGHT imageAvailable semaphores (per-FLIGHT)
 * - Creates MAX_SWAPCHAIN_IMAGES renderComplete semaphores (per-IMAGE)
 *
 * CRITICAL INDEXING:
 * - Acquisition semaphores: Indexed by FRAME (currentFrameIndex)
 * - Render complete semaphores: Indexed by IMAGE (currentImageIndex)
 * - Prevents "semaphore still in use by swapchain" errors
 *
 * Inputs: 1 (VULKAN_DEVICE)
 * Outputs: 5 (CURRENT_FRAME_INDEX, IN_FLIGHT_FENCE, IMAGE_AVAILABLE_SEMAPHORES_ARRAY, RENDER_COMPLETE_SEMAPHORES_ARRAY, PRESENT_FENCES_ARRAY)
 */
CONSTEXPR_NODE_CONFIG(FrameSyncNodeConfig,
                      FrameSyncNodeCounts::INPUTS,
                      FrameSyncNodeCounts::OUTPUTS,
                      FrameSyncNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (1) =====
    INPUT_SLOT(VULKAN_DEVICE, VulkanDevicePtr, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (5) =====
    OUTPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(IN_FLIGHT_FENCE, VkFence, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, std::vector<VkSemaphore>, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(RENDER_COMPLETE_SEMAPHORES_ARRAY, std::vector<VkSemaphore>, 3,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(PRESENT_FENCES_ARRAY, std::vector<VkFence>, 4,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Compile-time constants
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 4;  // CPU-GPU sync (fences) + both semaphore types
    static constexpr uint32_t MAX_SWAPCHAIN_IMAGES = 3;  // Swapchain image count hint

    // Constructor for runtime descriptor initialization
    FrameSyncNodeConfig() {
        // Initialize input descriptor
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        // Initialize output descriptors
        HandleDescriptor frameIndexDesc{"uint32_t"};
        INIT_OUTPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, frameIndexDesc);

        HandleDescriptor fenceDesc{"VkFence"};
        INIT_OUTPUT_DESC(IN_FLIGHT_FENCE, "in_flight_fence", ResourceLifetime::Persistent, fenceDesc);        

        HandleDescriptor semaphoreArrayDesc{"VkSemaphoreArrayPtr"};
        INIT_OUTPUT_DESC(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, "image_available_semaphores_array", ResourceLifetime::Persistent, semaphoreArrayDesc);
        INIT_OUTPUT_DESC(RENDER_COMPLETE_SEMAPHORES_ARRAY, "render_complete_semaphores_array", ResourceLifetime::Persistent, semaphoreArrayDesc);

        HandleDescriptor fenceArrayDesc{"VkFenceArrayPtr"};
        INIT_OUTPUT_DESC(PRESENT_FENCES_ARRAY, "present_fences_array", ResourceLifetime::Persistent, fenceArrayDesc);
    }

    // Compile-time validation using declared constants
    static_assert(INPUT_COUNT == FrameSyncNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == FrameSyncNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == FrameSyncNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(VULKAN_DEVICE_Slot::index == 0, "VULKAN_DEVICE input must be at index 0");
    static_assert(!VULKAN_DEVICE_Slot::nullable, "VULKAN_DEVICE input is required");

    static_assert(CURRENT_FRAME_INDEX_Slot::index == 0, "CURRENT_FRAME_INDEX must be at index 0");
    static_assert(IN_FLIGHT_FENCE_Slot::index == 1, "IN_FLIGHT_FENCE must be at index 1");
    static_assert(IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::index == 2, "IMAGE_AVAILABLE_SEMAPHORES_ARRAY must be at index 2");
    static_assert(RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::index == 3, "RENDER_COMPLETE_SEMAPHORES_ARRAY must be at index 3");
    static_assert(PRESENT_FENCES_ARRAY_Slot::index == 4, "PRESENT_FENCES_ARRAY must be at index 4");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<IN_FLIGHT_FENCE_Slot::Type, VkFence>);
    static_assert(std::is_same_v<IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::Type, std::vector<VkSemaphore>>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::Type, std::vector<VkSemaphore>>);
    static_assert(std::is_same_v<PRESENT_FENCES_ARRAY_Slot::Type, std::vector<VkFence>>);
};

// Global compile-time validations
static_assert(FrameSyncNodeConfig::INPUT_COUNT == FrameSyncNodeCounts::INPUTS);
static_assert(FrameSyncNodeConfig::OUTPUT_COUNT == FrameSyncNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph