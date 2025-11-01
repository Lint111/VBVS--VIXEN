#pragma once
#include "Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;

// Compile-time slot counts (declared early for reuse)
namespace FrameSyncNodeCounts {
    static constexpr size_t INPUTS = 1;
    static constexpr size_t OUTPUTS = 4;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for FrameSyncNode
 *
 * Phase 0.2: Frame-in-flight synchronization primitives
 * Creates MAX_FRAMES_IN_FLIGHT fences and semaphores for CPU-GPU sync
 *
 * Inputs: 1 (VULKAN_DEVICE: VulkanDevicePtr, required)
 * Outputs: 4 (CURRENT_FRAME_INDEX, IN_FLIGHT_FENCE, IMAGE_AVAILABLE_SEMAPHORE, RENDER_COMPLETE_SEMAPHORE)
 */
CONSTEXPR_NODE_CONFIG(FrameSyncNodeConfig,
                      FrameSyncNodeCounts::INPUTS,
                      FrameSyncNodeCounts::OUTPUTS,
                      FrameSyncNodeCounts::ARRAY_MODE) {
    // Compile-time output slot definitions
    CONSTEXPR_OUTPUT(CURRENT_FRAME_INDEX, uint32_t, 0, false);
    CONSTEXPR_OUTPUT(IN_FLIGHT_FENCE, VkFence, 1, false);
    CONSTEXPR_OUTPUT(IMAGE_AVAILABLE_SEMAPHORE, VkSemaphore, 2, false);
    CONSTEXPR_OUTPUT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 3, false);

    // Input: VulkanDevice pointer
    CONSTEXPR_INPUT(VULKAN_DEVICE, VulkanDevicePtr, 0, false);

    // Compile-time constants
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    // Constructor for runtime descriptor initialization
    FrameSyncNodeConfig() {
        // Initialize input descriptor
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        // Initialize output descriptors
        HandleDescriptor uint32Desc{"uint32_t"};
        INIT_OUTPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, uint32Desc);

        HandleDescriptor fenceDesc{"VkFence"};
        INIT_OUTPUT_DESC(IN_FLIGHT_FENCE, "in_flight_fence", ResourceLifetime::Persistent, fenceDesc);

        HandleDescriptor semaphoreDesc{"VkSemaphore"};
        INIT_OUTPUT_DESC(IMAGE_AVAILABLE_SEMAPHORE, "image_available_semaphore", ResourceLifetime::Persistent, semaphoreDesc);
        INIT_OUTPUT_DESC(RENDER_COMPLETE_SEMAPHORE, "render_complete_semaphore", ResourceLifetime::Persistent, semaphoreDesc);
    }

    // Compile-time validation using declared constants
    static_assert(INPUT_COUNT == FrameSyncNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == FrameSyncNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == FrameSyncNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(VULKAN_DEVICE_Slot::index == 0, "VULKAN_DEVICE input must be at index 0");
    static_assert(!VULKAN_DEVICE_Slot::nullable, "VULKAN_DEVICE input is required");

    static_assert(CURRENT_FRAME_INDEX_Slot::index == 0, "CURRENT_FRAME_INDEX must be at index 0");
    static_assert(IN_FLIGHT_FENCE_Slot::index == 1, "IN_FLIGHT_FENCE must be at index 1");
    static_assert(IMAGE_AVAILABLE_SEMAPHORE_Slot::index == 2, "IMAGE_AVAILABLE_SEMAPHORE must be at index 2");
    static_assert(RENDER_COMPLETE_SEMAPHORE_Slot::index == 3, "RENDER_COMPLETE_SEMAPHORE must be at index 3");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<IN_FLIGHT_FENCE_Slot::Type, VkFence>);
    static_assert(std::is_same_v<IMAGE_AVAILABLE_SEMAPHORE_Slot::Type, VkSemaphore>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORE_Slot::Type, VkSemaphore>);
};

// Global compile-time validations
static_assert(FrameSyncNodeConfig::INPUT_COUNT == FrameSyncNodeCounts::INPUTS);
static_assert(FrameSyncNodeConfig::OUTPUT_COUNT == FrameSyncNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph