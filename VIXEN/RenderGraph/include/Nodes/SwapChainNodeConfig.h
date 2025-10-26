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
    static constexpr size_t INPUTS = 6;
    static constexpr size_t OUTPUTS = 4;  // SWAPCHAIN_IMAGES, HANDLE, PUBLIC, WIDTH_OUT, HEIGHT_OUT, IMAGE_INDEX, IMAGE_AVAILABLE_SEMAPHORE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(SwapChainNodeConfig,
                      SwapChainNodeCounts::INPUTS,
                      SwapChainNodeCounts::OUTPUTS,
                      SwapChainNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (6) =====
    // Required window handles from WindowNode
    CONSTEXPR_INPUT(HWND, ::HWND, 0, false);
    CONSTEXPR_INPUT(HINSTANCE, ::HINSTANCE, 1, false);

    // Required width and height parameters from WindowNode
    CONSTEXPR_INPUT(WIDTH, uint32_t, 2, false);
    CONSTEXPR_INPUT(HEIGHT, uint32_t, 3, false);

    // Required Vulkan instance from InstanceNode
    CONSTEXPR_INPUT(INSTANCE, VkInstance, 4, false);

    // VulkanDevice pointer (contains device, gpu, memory properties, etc.)
    CONSTEXPR_INPUT(VULKAN_DEVICE_IN, VulkanDevicePtr, 5, false);

    // ===== OUTPUTS (4) =====    

    // Additional outputs: swapchain handle and public variables
    CONSTEXPR_OUTPUT(SWAPCHAIN_HANDLE, VkSwapchainKHR, 0, false);
    CONSTEXPR_OUTPUT(SWAPCHAIN_PUBLIC, SwapChainPublicVariablesPtr, 1, true);

    // Current frame image index (updated per frame in Execute())
    CONSTEXPR_OUTPUT(IMAGE_INDEX, uint32_t, 2, false);
    
    // Image available semaphore (signaled when image is acquired, for queue submission wait)
    CONSTEXPR_OUTPUT(IMAGE_AVAILABLE_SEMAPHORE, VkSemaphore, 3, false);


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
        
        INIT_OUTPUT_DESC(IMAGE_AVAILABLE_SEMAPHORE, "image_available_semaphore",
            ResourceLifetime::Transient,
            BufferDescription{}  // VkSemaphore for queue wait
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

    static_assert(SWAPCHAIN_HANDLE_Slot::index == 0, "SWAPCHAIN_HANDLE must be at index 0");
    static_assert(!SWAPCHAIN_HANDLE_Slot::nullable, "SWAPCHAIN_HANDLE is required");

    static_assert(SWAPCHAIN_PUBLIC_Slot::index == 1, "SWAPCHAIN_PUBLIC must be at index 1");
    static_assert(SWAPCHAIN_PUBLIC_Slot::nullable, "SWAPCHAIN_PUBLIC may be nullable");

    static_assert(IMAGE_INDEX_Slot::index == 2, "IMAGE_INDEX must be at index 2");
    static_assert(!IMAGE_INDEX_Slot::nullable, "IMAGE_INDEX is required");

    static_assert(IMAGE_AVAILABLE_SEMAPHORE_Slot::index == 3, "IMAGE_AVAILABLE_SEMAPHORE must be at index 3");
    static_assert(!IMAGE_AVAILABLE_SEMAPHORE_Slot::nullable, "IMAGE_AVAILABLE_SEMAPHORE is required");

    // Type validations
    static_assert(std::is_same_v<HWND_Slot::Type, ::HWND>);
    static_assert(std::is_same_v<HINSTANCE_Slot::Type, ::HINSTANCE>);
    static_assert(std::is_same_v<WIDTH_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<HEIGHT_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<INSTANCE_Slot::Type, VkInstance>);
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);

    static_assert(std::is_same_v<SWAPCHAIN_HANDLE_Slot::Type, VkSwapchainKHR>);
    static_assert(std::is_same_v<SWAPCHAIN_PUBLIC_Slot::Type, ::SwapChainPublicVariables*>);
    static_assert(std::is_same_v<IMAGE_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<IMAGE_AVAILABLE_SEMAPHORE_Slot::Type, VkSemaphore>);
};

// Global compile-time validations
static_assert(SwapChainNodeConfig::INPUT_COUNT == SwapChainNodeCounts::INPUTS);
static_assert(SwapChainNodeConfig::OUTPUT_COUNT == SwapChainNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph
