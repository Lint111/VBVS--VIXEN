#pragma once

#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

/**
 * @brief Resource configuration for UIRenderNode (RmlUi → Vulkan).
 *
 * UIRenderNode mirrors GeometryRenderNode's per-frame sync/target inputs, but OWNS its pipeline,
 * geometry, textures, render pass, and framebuffers internally (via VixenRmlRenderInterface) — so it
 * drops PIPELINE/PIPELINE_LAYOUT/DESCRIPTOR_SETS/VERTEX_BUFFER/RENDER_PASS/FRAMEBUFFERS. It builds a
 * color-only render pass + framebuffers from SWAPCHAIN_INFO, so its pipeline is format-compatible.
 *
 * Inputs (8): SWAPCHAIN_INFO, COMMAND_POOL, VULKAN_DEVICE, IMAGE_INDEX, CURRENT_FRAME_INDEX,
 *   IN_FLIGHT_FENCE, IMAGE_AVAILABLE_SEMAPHORES_ARRAY, RENDER_COMPLETE_SEMAPHORES_ARRAY.
 * Outputs (2): COMMAND_BUFFERS, RENDER_COMPLETE_SEMAPHORE.
 * Parameters: rmlDocumentPath, fontPath.
 */
namespace UIRenderNodeCounts {
    static constexpr size_t INPUTS = 8;
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Array;
}

CONSTEXPR_NODE_CONFIG(UIRenderNodeConfig,
                      UIRenderNodeCounts::INPUTS,
                      UIRenderNodeCounts::OUTPUTS,
                      UIRenderNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* RML_DOCUMENT_PATH = "rmlDocumentPath";
    static constexpr const char* FONT_PATH = "fontPath";

    // ===== INPUTS (8) =====
    INPUT_SLOT(SWAPCHAIN_INFO, SwapChainPublicVariables*, 0,
        SlotNullability::Required, SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 1,
        SlotNullability::Required, SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(VULKAN_DEVICE, VulkanDevice*, 2,
        SlotNullability::Required, SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_INDEX, uint32_t, 3,
        SlotNullability::Required, SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 4,
        SlotNullability::Required, SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(IN_FLIGHT_FENCE, VkFence, 5,
        SlotNullability::Required, SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 6,
        SlotNullability::Required, SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    INPUT_SLOT(RENDER_COMPLETE_SEMAPHORES_ARRAY, const std::vector<VkSemaphore>&, 7,
        SlotNullability::Required, SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    // ===== OUTPUTS (2) =====
    OUTPUT_SLOT(COMMAND_BUFFERS, VkCommandBuffer, 0,
        SlotNullability::Required, SlotMutability::WriteOnly);

    OUTPUT_SLOT(RENDER_COMPLETE_SEMAPHORE, VkSemaphore, 1,
        SlotNullability::Required, SlotMutability::WriteOnly);

    UIRenderNodeConfig() {
        HandleDescriptor swapchainInfoDesc{"SwapChainPublicVariables*"};
        INIT_INPUT_DESC(SWAPCHAIN_INFO, "swapchain_info", ResourceLifetime::Persistent, swapchainInfoDesc);

        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, BufferDescription{});

        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        INIT_INPUT_DESC(IMAGE_INDEX, "image_index", ResourceLifetime::Transient, BufferDescription{});
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index", ResourceLifetime::Transient, BufferDescription{});
        INIT_INPUT_DESC(IN_FLIGHT_FENCE, "in_flight_fence", ResourceLifetime::Transient, BufferDescription{});

        HandleDescriptor semaphoreArrayDesc{"VkSemaphore*"};
        INIT_INPUT_DESC(IMAGE_AVAILABLE_SEMAPHORES_ARRAY, "image_available_semaphores_array",
            ResourceLifetime::Persistent, semaphoreArrayDesc);
        INIT_INPUT_DESC(RENDER_COMPLETE_SEMAPHORES_ARRAY, "render_complete_semaphores_array",
            ResourceLifetime::Persistent, semaphoreArrayDesc);

        INIT_OUTPUT_DESC(COMMAND_BUFFERS, "command_buffers", ResourceLifetime::Transient, BufferDescription{});
        INIT_OUTPUT_DESC(RENDER_COMPLETE_SEMAPHORE, "render_complete_semaphore",
            ResourceLifetime::Transient, BufferDescription{});
    }

    VALIDATE_NODE_CONFIG(UIRenderNodeConfig, UIRenderNodeCounts);

    static_assert(SWAPCHAIN_INFO_Slot::index == 0, "SWAPCHAIN_INFO must be at index 0");
    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(VULKAN_DEVICE_Slot::index == 2, "VULKAN_DEVICE must be at index 2");
    static_assert(IMAGE_INDEX_Slot::index == 3, "IMAGE_INDEX must be at index 3");
    static_assert(CURRENT_FRAME_INDEX_Slot::index == 4, "CURRENT_FRAME_INDEX must be at index 4");
    static_assert(IN_FLIGHT_FENCE_Slot::index == 5, "IN_FLIGHT_FENCE must be at index 5");
    static_assert(IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::index == 6, "IMAGE_AVAILABLE_SEMAPHORES_ARRAY must be at index 6");
    static_assert(RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::index == 7, "RENDER_COMPLETE_SEMAPHORES_ARRAY must be at index 7");
    static_assert(COMMAND_BUFFERS_Slot::index == 0, "COMMAND_BUFFERS must be at index 0");
    static_assert(RENDER_COMPLETE_SEMAPHORE_Slot::index == 1, "RENDER_COMPLETE_SEMAPHORE must be at index 1");

    static_assert(std::is_same_v<SWAPCHAIN_INFO_Slot::Type, SwapChainPublicVariables*>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<VULKAN_DEVICE_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<IMAGE_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>);
    static_assert(std::is_same_v<IN_FLIGHT_FENCE_Slot::Type, VkFence>);
    static_assert(std::is_same_v<IMAGE_AVAILABLE_SEMAPHORES_ARRAY_Slot::Type, const std::vector<VkSemaphore>&>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORES_ARRAY_Slot::Type, const std::vector<VkSemaphore>&>);
    static_assert(std::is_same_v<COMMAND_BUFFERS_Slot::Type, VkCommandBuffer>);
    static_assert(std::is_same_v<RENDER_COMPLETE_SEMAPHORE_Slot::Type, VkSemaphore>);
};

} // namespace Vixen::RenderGraph
