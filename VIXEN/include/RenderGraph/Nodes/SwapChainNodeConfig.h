#pragma once

#include "RenderGraph/ResourceConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Pure constexpr resource configuration for SwapChainNode
 *
 * Inputs:
 * - HWND (HWND) - Window handle from WindowNode
 * - HINSTANCE (HINSTANCE) - Instance handle from WindowNode
 * - WIDTH (uint32_t) - Window width from WindowNode
 * - HEIGHT (uint32_t) - Window height from WindowNode
 * - INSTANCE (VkInstance) - Vulkan instance from InstanceNode
 * - PHYSICAL_DEVICE (VkPhysicalDevice) - GPU from DeviceNode
 * - DEVICE (VkDevice) - Logical device from DeviceNode
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
CONSTEXPR_NODE_CONFIG(SwapChainNodeConfig, 7, 3, false) {
    // ===== INPUTS (7) =====
    // Required window handles from WindowNode
    CONSTEXPR_INPUT(HWND, ::HWND, 0, false);
    CONSTEXPR_INPUT(HINSTANCE, ::HINSTANCE, 1, false);

    // Required width and height parameters from WindowNode
    CONSTEXPR_INPUT(WIDTH, uint32_t, 2, false);
    CONSTEXPR_INPUT(HEIGHT, uint32_t, 3, false);

    // Required Vulkan instance from InstanceNode
    CONSTEXPR_INPUT(INSTANCE, VkInstance, 4, false);

    // Required physical device from DeviceNode
    CONSTEXPR_INPUT(PHYSICAL_DEVICE, VkPhysicalDevice, 5, false);

    // Required logical device from DeviceNode
    CONSTEXPR_INPUT(DEVICE, VkDevice, 6, false);

    // ===== OUTPUTS (3) =====
    // Swapchain images (required)
    CONSTEXPR_OUTPUT(SWAPCHAIN_IMAGES, VkImage, 0, false);

    // Additional outputs: swapchain handle and public variables
    CONSTEXPR_OUTPUT(SWAPCHAIN_HANDLE, VkSwapchainKHR, 1, false);
    CONSTEXPR_OUTPUT(SWAPCHAIN_PUBLIC, ::SwapChainPublicVariables*, 2, true);


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

        // Physical device (GPU)
        INIT_INPUT_DESC(PHYSICAL_DEVICE, "physical_device",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        // Logical device
        INIT_INPUT_DESC(DEVICE, "device",
            ResourceLifetime::Persistent,
            BufferDescription{}
        );

        // Initialize output descriptors
        ImageDescription swapchainImgDesc{};
        swapchainImgDesc.width = 0;
        swapchainImgDesc.height = 0;
        swapchainImgDesc.format = VK_FORMAT_UNDEFINED;
        swapchainImgDesc.usage = ResourceUsage::ColorAttachment | ResourceUsage::TransferDst;

        INIT_OUTPUT_DESC(SWAPCHAIN_IMAGES, "swapchain_images",
            ResourceLifetime::Persistent,
            swapchainImgDesc
        );

        INIT_OUTPUT_DESC(SWAPCHAIN_HANDLE, "swapchain_handle",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque handle for VkSwapchainKHR
        );

        INIT_OUTPUT_DESC(SWAPCHAIN_PUBLIC, "swapchain_public",
            ResourceLifetime::Persistent,
            BufferDescription{}  // Opaque pointer to public variables
        );
    }

    // Compile-time validations (optional but recommended)
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

    static_assert(PHYSICAL_DEVICE_Slot::index == 5, "PHYSICAL_DEVICE input must be at index 5");
    static_assert(!PHYSICAL_DEVICE_Slot::nullable, "PHYSICAL_DEVICE input is required");

    static_assert(DEVICE_Slot::index == 6, "DEVICE input must be at index 6");
    static_assert(!DEVICE_Slot::nullable, "DEVICE input is required");

    static_assert(SWAPCHAIN_IMAGES_Slot::index == 0, "SWAPCHAIN_IMAGES must be at index 0");
    static_assert(!SWAPCHAIN_IMAGES_Slot::nullable, "SWAPCHAIN_IMAGES is required");

    static_assert(SWAPCHAIN_HANDLE_Slot::index == 1, "SWAPCHAIN_HANDLE must be at index 1");
    static_assert(!SWAPCHAIN_HANDLE_Slot::nullable, "SWAPCHAIN_HANDLE is required");

    static_assert(SWAPCHAIN_PUBLIC_Slot::index == 2, "SWAPCHAIN_PUBLIC must be at index 2");
    static_assert(SWAPCHAIN_PUBLIC_Slot::nullable, "SWAPCHAIN_PUBLIC may be nullable");

    // Type validations
    static_assert(std::is_same_v<HWND_Slot::Type, ::HWND>);
    static_assert(std::is_same_v<HINSTANCE_Slot::Type, ::HINSTANCE>);
    static_assert(std::is_same_v<WIDTH_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<HEIGHT_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<INSTANCE_Slot::Type, VkInstance>);
    static_assert(std::is_same_v<PHYSICAL_DEVICE_Slot::Type, VkPhysicalDevice>);
    static_assert(std::is_same_v<DEVICE_Slot::Type, VkDevice>);

    static_assert(std::is_same_v<SWAPCHAIN_IMAGES_Slot::Type, VkImage>);
    static_assert(std::is_same_v<SWAPCHAIN_HANDLE_Slot::Type, VkSwapchainKHR>);
    static_assert(std::is_same_v<SWAPCHAIN_PUBLIC_Slot::Type, ::SwapChainPublicVariables*>);
};

// Global compile-time validations
static_assert(SwapChainNodeConfig::INPUT_COUNT == 7);
static_assert(SwapChainNodeConfig::OUTPUT_COUNT == 3);

} // namespace Vixen::RenderGraph
