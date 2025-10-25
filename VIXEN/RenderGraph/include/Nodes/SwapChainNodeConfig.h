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
    static constexpr size_t OUTPUTS = 5;  // SWAPCHAIN_IMAGES, HANDLE, PUBLIC, WIDTH_OUT, HEIGHT_OUT
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

    // ===== OUTPUTS (5) =====
    // Swapchain image views (required for framebuffer attachments)
    CONSTEXPR_OUTPUT(SWAPCHAIN_IMAGES, VkImageView, 0, false);

    // Additional outputs: swapchain handle and public variables
    CONSTEXPR_OUTPUT(SWAPCHAIN_HANDLE, VkSwapchainKHR, 1, false);
    CONSTEXPR_OUTPUT(SWAPCHAIN_PUBLIC, ::SwapChainPublicVariables*, 2, true);

    // Width and height outputs (for downstream nodes)
    CONSTEXPR_OUTPUT(WIDTH_OUT, uint32_t, 3, false);
    CONSTEXPR_OUTPUT(HEIGHT_OUT, uint32_t, 4, false);


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

        INIT_OUTPUT_DESC(WIDTH_OUT, "width_out",
            ResourceLifetime::Persistent,
            BufferDescription{}  // uint32_t width
        );

        INIT_OUTPUT_DESC(HEIGHT_OUT, "height_out",
            ResourceLifetime::Persistent,
            BufferDescription{}  // uint32_t height
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

    static_assert(SWAPCHAIN_IMAGES_Slot::index == 0, "SWAPCHAIN_IMAGES must be at index 0");
    static_assert(!SWAPCHAIN_IMAGES_Slot::nullable, "SWAPCHAIN_IMAGES is required");

    static_assert(SWAPCHAIN_HANDLE_Slot::index == 1, "SWAPCHAIN_HANDLE must be at index 1");
    static_assert(!SWAPCHAIN_HANDLE_Slot::nullable, "SWAPCHAIN_HANDLE is required");

    static_assert(SWAPCHAIN_PUBLIC_Slot::index == 2, "SWAPCHAIN_PUBLIC must be at index 2");
    static_assert(SWAPCHAIN_PUBLIC_Slot::nullable, "SWAPCHAIN_PUBLIC may be nullable");

    static_assert(WIDTH_OUT_Slot::index == 3, "WIDTH_OUT must be at index 3");
    static_assert(!WIDTH_OUT_Slot::nullable, "WIDTH_OUT is required");

    static_assert(HEIGHT_OUT_Slot::index == 4, "HEIGHT_OUT must be at index 4");
    static_assert(!HEIGHT_OUT_Slot::nullable, "HEIGHT_OUT is required");

    // Type validations
    static_assert(std::is_same_v<HWND_Slot::Type, ::HWND>);
    static_assert(std::is_same_v<HINSTANCE_Slot::Type, ::HINSTANCE>);
    static_assert(std::is_same_v<WIDTH_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<HEIGHT_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<INSTANCE_Slot::Type, VkInstance>);
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);

    static_assert(std::is_same_v<SWAPCHAIN_IMAGES_Slot::Type, VkImageView>);
    static_assert(std::is_same_v<SWAPCHAIN_HANDLE_Slot::Type, VkSwapchainKHR>);
    static_assert(std::is_same_v<SWAPCHAIN_PUBLIC_Slot::Type, ::SwapChainPublicVariables*>);
    static_assert(std::is_same_v<WIDTH_OUT_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<HEIGHT_OUT_Slot::Type, uint32_t>);
};

// Global compile-time validations
static_assert(SwapChainNodeConfig::INPUT_COUNT == 6);
static_assert(SwapChainNodeConfig::OUTPUT_COUNT == 5);

} // namespace Vixen::RenderGraph
