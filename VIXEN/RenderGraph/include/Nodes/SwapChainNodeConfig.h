#pragma once

#include "Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;

/**
 * @brief Pure constexpr resource configuration for SwapChainNode
 *
 * Inputs:
 * - HWND (HWND) - Window handle from WindowNode
 * - HINSTANCE (HINSTANCE) - Instance handle from WindowNode
 * - WIDTH (uint32_t) - Window width from WindowNode
 * - HEIGHT (uint32_t) - Window height from WindowNode
 * - INSTANCE (VkInstance) - Vulkan instance from InstanceNode
 * - VULKAN_DEVICE (VulkanDevicePtr) - VulkanDevice pointer (contains device, gpu, memory properties)
 *
 * Outputs:
 * - SWAPCHAIN_IMAGES (VkImage[]) - Color images for rendering
 * - SWAPCHAIN_HANDLE (VkSwapchainKHR) - Swapchain handle
 * - SWAPCHAIN_PUBLIC (SwapChainPublicVariables*) - Public swapchain state
 *
 * Note: Surface (VkSurfaceKHR) is created internally via CreateSurface() using HWND/HINSTANCE
 *
 * ALL type checking happens at compile time!
 */
// Compile-time slot counts (declared early for reuse)
namespace SwapChainNodeCounts {
    static constexpr size_t INPUTS = 10;  // Phase 0.7: Added PRESENT_FENCES_ARRAY
    static constexpr size_t OUTPUTS = 3;  // Phase 0.5: Removed single semaphore outputs (use arrays from FrameSyncNode)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(SwapChainNodeConfig,
                      SwapChainNodeCounts::INPUTS,
                      SwapChainNodeCounts::OUTPUTS,
                      SwapChainNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* IMAGE_USAGE_FLAGS = "imageUsageFlags";

    // ===== INPUTS (10) =====
    INPUT_SLOT(HWND, ::HWND, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(HINSTANCE, ::HINSTANCE, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(WIDTH, uint32_t, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(HEIGHT, uint32_t, 3,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(INSTANCE, VkInstance, 4,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevicePtr, 5,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, std::vector<VkSemaphore>, 6,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(RENDER_COMPLETE_SEMAPHORES_ARRAY, std::vector<VkSemaphore>, 7,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 8,
        SlotNullability::Required,
        SlotRole::ExecuteOnly,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(PRESENT_FENCES_ARRAY, std::vector<VkFence>, 9,
        SlotNullability::Required,
        SlotRole::ExecuteOnly,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (3) =====
    OUTPUT_SLOT(SWAPCHAIN_HANDLE, VkSwapchainKHR, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(SWAPCHAIN_PUBLIC, SwapChainPublicVariablesPtr, 1,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(IMAGE_INDEX, uint32_t, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);


    SwapChainNodeConfig() {
        // Window handle
        INIT_INPUT_DESC(HWND, "hwnd",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        // Instance handle
        INIT_INPUT_DESC(HINSTANCE, "hinstance",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        // Width parameter
        INIT_INPUT_DESC(WIDTH, "width",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        // Height parameter
        INIT_INPUT_DESC(HEIGHT, "height",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        // Vulkan instance
        INIT_INPUT_DESC(INSTANCE, "instance",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        // VulkanDevice pointer
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        // Phase 0.4: Semaphore arrays and frame index from FrameSyncNode
        HandleDescriptor semaphoreArrayDesc{"VkSemaphoreArrayPtr"};
        INIT_INPUT_DESC(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, "image_available_semaphores_array", ResourceLifetime::Persistent, semaphoreArrayDesc);
        INIT_INPUT_DESC(RENDER_COMPLETE_SEMAPHORES_ARRAY, "render_complete_semaphores_array", ResourceLifetime::Persistent, semaphoreArrayDesc);

        HandleDescriptor frameIndexDesc{"uint32_t"};
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, frameIndexDesc);

        HandleDescriptor fenceArrayDesc{"VkFenceArrayPtr"};
        INIT_INPUT_DESC(PRESENT_FENCES_ARRAY, "present_fences_array", ResourceLifetime::Persistent, fenceArrayDesc);

        INIT_OUTPUT_DESC(SWAPCHAIN_HANDLE, "swapchain_handle",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle for VkSwapchainKHR
        );

        INIT_OUTPUT_DESC(SWAPCHAIN_PUBLIC, "swapchain_public",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque pointer to public variables
        );

        INIT_OUTPUT_DESC(IMAGE_INDEX, "image_index",
            ResourceLifetime::Transient,
            BufferDescription{}  // uint32_t current image index
        );
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == SwapChainNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == SwapChainNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == SwapChainNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(HWND_Slot::index == 0, "HWND input must be at index 0");
    static_assert(!HWND_Slot::nullable, "HWND input is required");

    static_assert(HINSTANCE_Slot::index == 1, "HINSTANCE input must be at index 1");
    static_assert(!HINSTANCE_Slot::nullable, "HINSTANCE input is required");

    static_assert(WIDTH_Slot::index == 2, "WIDTH input must be at index 2");
    static_assert(!WIDTH_Slot::nullable, "WIDTH input is required");

    static_assert(HEIGHT_Slot::index == 3, "HEIGHT input must be at index 3");
    static_assert(!HEIGHT_Slot::nullable, "HEIGHT input is required");

    static_assert(INSTANCE_Slot::index == 4, "INSTANCE input must be at index 4");
    static_assert(!INSTANCE_Slot::nullable, "INSTANCE input is required");

    static_assert(VULKAN_DEVICE_IN_Slot::index == 5, "VULKAN_DEVICE input must be at index 5");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE input is required");

    static_assert(IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::index == 6, "IMAGE_AVAILABLE_SEMAPHORES_ARRAY must be at index 6");
    static_assert(!IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::nullable, "IMAGE_AVAILABLE_SEMAPHORES_ARRAY is required");

    static_assert(RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::index == 7, "RENDER_COMPLETE_SEMAPHORES_ARRAY must be at index 7");
    static_assert(!RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::nullable, "RENDER_COMPLETE_SEMAPHORES_ARRAY is required");

    static_assert(CURRENT_FRAME_INDEX_Slot::index == 8, "CURRENT_FRAME_INDEX must be at index 8");
    static_assert(!CURRENT_FRAME_INDEX_Slot::nullable, "CURRENT_FRAME_INDEX is required");

    static_assert(PRESENT_FENCES_ARRAY_Slot::index == 9, "PRESENT_FENCES_ARRAY must be at index 9");
    static_assert(!PRESENT_FENCES_ARRAY_Slot::nullable, "PRESENT_FENCES_ARRAY is required");

    static_assert(SWAPCHAIN_HANDLE_Slot::index == 0, "SWAPCHAIN_HANDLE must be at index 0");
    static_assert(!SWAPCHAIN_HANDLE_Slot::nullable, "SWAPCHAIN_HANDLE is required");

    static_assert(SWAPCHAIN_PUBLIC_Slot::index == 1, "SWAPCHAIN_PUBLIC must be at index 1");
    static_assert(SWAPCHAIN_PUBLIC_Slot::nullable, "SWAPCHAIN_PUBLIC may be nullable");

    static_assert(IMAGE_INDEX_Slot::index == 2, "IMAGE_INDEX must be at index 2");
    static_assert(!IMAGE_INDEX_Slot::nullable, "IMAGE_INDEX is required");

    // Type validations
    static_assert(std::is_same_v<HWND_Slot::Type, ::HWND>);
    static_assert(std::is_same_v<HINSTANCE_Slot::Type, ::HINSTANCE>);
    static_assert(std::is_same_v<WIDTH_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<HEIGHT_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<INSTANCE_Slot::Type, VkInstance>);
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::Type, std::vector<VkSemaphore>>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::Type, std::vector<VkSemaphore>>);
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<PRESENT_FENCES_ARRAY_Slot::Type, std::vector<VkFence>>);

    static_assert(std::is_same_v<SWAPCHAIN_HANDLE_Slot::Type, VkSwapchainKHR>);
    static_assert(std::is_same_v<SWAPCHAIN_PUBLIC_Slot::Type, ::SwapChainPublicVariables*>);
    static_assert(std::is_same_v<IMAGE_INDEX_Slot::Type, uint32_t>);
};

// Global compile-time validations
static_assert(SwapChainNodeConfig::INPUT_COUNT == SwapChainNodeCounts::INPUTS);
static_assert(SwapChainNodeConfig::OUTPUT_COUNT == SwapChainNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph
