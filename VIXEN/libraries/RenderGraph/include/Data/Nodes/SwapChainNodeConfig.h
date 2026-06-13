#pragma once

#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

struct GLFWwindow;  // cross-platform window handle (GLFW); concrete type only needed in the .cpp

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (forward declared - use VulkanDevice* in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

/**
 * @brief Pure constexpr resource configuration for SwapChainNode
 *
 * Inputs:
 * - WINDOW (GLFWwindow*) - cross-platform window handle from WindowNode
 * - WIDTH (uint32_t) - Window width from WindowNode
 * - HEIGHT (uint32_t) - Window height from WindowNode
 * - INSTANCE (VkInstance) - Vulkan instance from InstanceNode
 * - VULKAN_DEVICE (VulkanDevice*) - VulkanDevice pointer (contains device, gpu, memory properties)
 *
 * Outputs:
 * - SWAPCHAIN_IMAGES (VkImage[]) - Color images for rendering
 * - SWAPCHAIN_HANDLE (VkSwapchainKHR) - Swapchain handle
 * - SWAPCHAIN_PUBLIC (SwapChainPublicVariables*) - Public swapchain state
 *
 * Note: Surface (VkSurfaceKHR) is created internally via glfwCreateWindowSurface() using WINDOW
 *
 * ALL type checking happens at compile time!
 */
// Compile-time slot counts (declared early for reuse)
namespace SwapChainNodeCounts {
    static constexpr size_t INPUTS = 7;   // FR-3: renderComplete + presentFences moved to OUTPUTS (owned here)
    static constexpr size_t OUTPUTS = 6;  // + RENDER_COMPLETE_SEMAPHORES_ARRAY, PRESENT_FENCES_ARRAY (per-image, sized to actual count)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(SwapChainNodeConfig,
                      SwapChainNodeCounts::INPUTS,
                      SwapChainNodeCounts::OUTPUTS,
                      SwapChainNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* IMAGE_USAGE_FLAGS = "imageUsageFlags";

    // ===== INPUTS (9) =====
    INPUT_SLOT(WINDOW, GLFWwindow*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(WIDTH, uint32_t, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(HEIGHT, uint32_t, 2,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(INSTANCE, VkInstance, 3,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 4,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 5,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 6,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== OUTPUTS (6) =====
    OUTPUT_SLOT(SWAPCHAIN_HANDLE, VkSwapchainKHR, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(SWAPCHAIN_PUBLIC, SwapChainPublicVariables*, 1,
        SlotNullability::Optional,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(IMAGE_INDEX, uint32_t, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(CURRENT_FRAME_IMAGE_VIEW, VkImageView, 3,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // FR-3: per-IMAGE sync resources are owned here (sized to the exact swapchain
    // image count), not pre-sized to a constant in FrameSyncNode.
    OUTPUT_SLOT(RENDER_COMPLETE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 4,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(PRESENT_FENCES_ARRAY, const std::vector<VkFence>&, 5,
        SlotNullability::Required,
        SlotMutability::WriteOnly);


    SwapChainNodeConfig() {
        // Cross-platform window handle
        HandleDescriptor windowDesc{"GLFWwindow"};
        INIT_INPUT_DESC(WINDOW, "window",
            ResourceLifetime::Persistent,
            windowDesc
        );

        // Width parameter
        INIT_INPUT_DESC(WIDTH, "width",
            ResourceLifetime::Transient,
            BufferDescription{}
        );

        // Height parameter
        INIT_INPUT_DESC(HEIGHT, "height",
            ResourceLifetime::Transient,
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

        // Phase 0.4: imageAvailable semaphores (per-FLIGHT) + frame index from FrameSyncNode
        HandleDescriptor semaphoreArrayDesc{"VkSemaphoreArrayPtr"};
        INIT_INPUT_DESC(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, "image_available_semaphores_array", ResourceLifetime::Persistent, semaphoreArrayDesc);

        HandleDescriptor frameIndexDesc{"uint32_t"};
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, frameIndexDesc);

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

        HandleDescriptor imageViewDesc{"VkImageView"};
        INIT_OUTPUT_DESC(CURRENT_FRAME_IMAGE_VIEW, "current_frame_image_view",
            ResourceLifetime::Transient,
            imageViewDesc  // VkImageView for current frame's swapchain image
        );

        // FR-3: per-IMAGE sync arrays produced here, sized to the actual swapchain image count
        HandleDescriptor renderCompleteDesc{"VkSemaphoreArrayPtr"};
        INIT_OUTPUT_DESC(RENDER_COMPLETE_SEMAPHORES_ARRAY, "render_complete_semaphores_array", ResourceLifetime::Persistent, renderCompleteDesc);
        HandleDescriptor presentFenceDesc{"VkFenceArrayPtr"};
        INIT_OUTPUT_DESC(PRESENT_FENCES_ARRAY, "present_fences_array", ResourceLifetime::Persistent, presentFenceDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(SwapChainNodeConfig, SwapChainNodeCounts);

    static_assert(WINDOW_Slot::index == 0, "WINDOW input must be at index 0");
    static_assert(!WINDOW_Slot::nullable, "WINDOW input is required");

    static_assert(WIDTH_Slot::index == 1, "WIDTH input must be at index 1");
    static_assert(!WIDTH_Slot::nullable, "WIDTH input is required");

    static_assert(HEIGHT_Slot::index == 2, "HEIGHT input must be at index 2");
    static_assert(!HEIGHT_Slot::nullable, "HEIGHT input is required");

    static_assert(INSTANCE_Slot::index == 3, "INSTANCE input must be at index 3");
    static_assert(!INSTANCE_Slot::nullable, "INSTANCE input is required");

    static_assert(VULKAN_DEVICE_IN_Slot::index == 4, "VULKAN_DEVICE input must be at index 4");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE input is required");

    static_assert(IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::index == 5, "IMAGE_AVAILABLE_SEMAPHORES_ARRAY must be at index 5");
    static_assert(!IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::nullable, "IMAGE_AVAILABLE_SEMAPHORES_ARRAY is required");

    static_assert(CURRENT_FRAME_INDEX_Slot::index == 6, "CURRENT_FRAME_INDEX must be at index 6");
    static_assert(!CURRENT_FRAME_INDEX_Slot::nullable, "CURRENT_FRAME_INDEX is required");

    static_assert(SWAPCHAIN_HANDLE_Slot::index == 0, "SWAPCHAIN_HANDLE must be at index 0");
    static_assert(!SWAPCHAIN_HANDLE_Slot::nullable, "SWAPCHAIN_HANDLE is required");

    static_assert(SWAPCHAIN_PUBLIC_Slot::index == 1, "SWAPCHAIN_PUBLIC must be at index 1");
    static_assert(SWAPCHAIN_PUBLIC_Slot::nullable, "SWAPCHAIN_PUBLIC may be nullable");

    static_assert(IMAGE_INDEX_Slot::index == 2, "IMAGE_INDEX must be at index 2");
    static_assert(!IMAGE_INDEX_Slot::nullable, "IMAGE_INDEX is required");

    static_assert(CURRENT_FRAME_IMAGE_VIEW_Slot::index == 3, "CURRENT_FRAME_IMAGE_VIEW must be at index 3");
    static_assert(!CURRENT_FRAME_IMAGE_VIEW_Slot::nullable, "CURRENT_FRAME_IMAGE_VIEW is required");

    static_assert(RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::index == 4, "RENDER_COMPLETE_SEMAPHORES_ARRAY output must be at index 4");
    static_assert(!RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::nullable, "RENDER_COMPLETE_SEMAPHORES_ARRAY output is required");

    static_assert(PRESENT_FENCES_ARRAY_Slot::index == 5, "PRESENT_FENCES_ARRAY output must be at index 5");
    static_assert(!PRESENT_FENCES_ARRAY_Slot::nullable, "PRESENT_FENCES_ARRAY output is required");

    // Type validations
    static_assert(std::is_same_v<WINDOW_Slot::Type, GLFWwindow*>);
    static_assert(std::is_same_v<WIDTH_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<HEIGHT_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<INSTANCE_Slot::Type, VkInstance>);
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::Type, const std::vector<VkSemaphore>&>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::Type, const std::vector<VkSemaphore>&>);
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<PRESENT_FENCES_ARRAY_Slot::Type, const std::vector<VkFence>&>);

    static_assert(std::is_same_v<SWAPCHAIN_HANDLE_Slot::Type, VkSwapchainKHR>);
    static_assert(std::is_same_v<SWAPCHAIN_PUBLIC_Slot::Type, ::SwapChainPublicVariables*>);
    static_assert(std::is_same_v<IMAGE_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<CURRENT_FRAME_IMAGE_VIEW_Slot::Type, VkImageView>);
};

} // namespace Vixen::RenderGraph
