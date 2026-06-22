// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::Vulkan::Resources {
    struct IRenderTarget; // abstract render target interface (for extent-driven sizing)
}

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts (declared early for reuse)
namespace StorageBufferNodeCounts {
    static constexpr size_t INPUTS  = 2;  // VULKAN_DEVICE_IN, SWAPCHAIN_INFO (optional)
    static constexpr size_t OUTPUTS = 2;  // STORAGE_BUFFER, BUFFER_SIZE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for StorageBufferNode
 *
 * Allocates a GENERIC, zero-initialised Vulkan storage buffer (SSBO) of an
 * arbitrary byte size (unlike InstanceBufferNode, which is hard-sized to
 * gridDim^2 * sizeof(mat4) and filled with model matrices). Used to back a
 * descriptor binding 0 of an arbitrary shape.
 *
 * Size is resolved at compile time, in priority order:
 *   1. If SWAPCHAIN_INFO is connected: extent.width * extent.height *
 *      PARAM_BYTES_PER_PIXEL — extent-driven, so it re-sizes through the normal
 *      swapchain resize/recompile cascade (like depth/framebuffer nodes).
 *   2. Else PARAM_SIZE_BYTES (explicit bytes).
 *   3. Else PARAM_ELEMENT_COUNT * PARAM_ELEMENT_STRIDE.
 * This keeps the node generic and reusable: the swapchain input is OPTIONAL.
 *
 * Inputs: 2
 *   - VULKAN_DEVICE_IN (VulkanDevice*)    - Device for allocation
 *   - SWAPCHAIN_INFO   (IRenderTarget*)   - OPTIONAL; drives extent-based sizing
 * Outputs: 2
 *   - STORAGE_BUFFER (VkBuffer)  - Zero-initialised storage buffer
 *   - BUFFER_SIZE    (uint32_t)  - Allocated size in bytes
 * Parameters: sizeBytes, elementCount, elementStride, bytesPerPixel
 */
CONSTEXPR_NODE_CONFIG(StorageBufferNodeConfig,
                      StorageBufferNodeCounts::INPUTS,
                      StorageBufferNodeCounts::OUTPUTS,
                      StorageBufferNodeCounts::ARRAY_MODE) {

    // ----- Parameter name constants -----
    static constexpr const char* PARAM_SIZE_BYTES      = "sizeBytes";      // explicit byte size (0 = use element/extent form)
    static constexpr const char* PARAM_ELEMENT_COUNT   = "elementCount";   // number of elements
    static constexpr const char* PARAM_ELEMENT_STRIDE  = "elementStride";  // bytes per element
    static constexpr const char* PARAM_BYTES_PER_PIXEL = "bytesPerPixel";  // extent-driven: bytes per swapchain pixel

    // ----- Input slots -----
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    /** @brief OPTIONAL swapchain info — when connected, size = extent * bytesPerPixel. */
    INPUT_SLOT(SWAPCHAIN_INFO, Vixen::Vulkan::Resources::IRenderTarget*, 1,
        SlotNullability::Optional,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ----- Output slots -----
    OUTPUT_SLOT(STORAGE_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(BUFFER_SIZE, uint32_t, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ----- Constructor: runtime descriptor initialization -----
    StorageBufferNodeConfig() {
        // Input: VulkanDevice
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        // Input: optional swapchain (extent-driven sizing)
        HandleDescriptor swapchainDesc{"IRenderTarget*"};
        INIT_INPUT_DESC(SWAPCHAIN_INFO, "swapchain_info", ResourceLifetime::Persistent, swapchainDesc);

        // Output: storage buffer (persistent — survives recompile until size grows)
        BufferDescription storageBufDesc{};
        storageBufDesc.usage            = ResourceUsage::StorageBuffer;
        storageBufDesc.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        INIT_OUTPUT_DESC(STORAGE_BUFFER, "storage_buffer",
            ResourceLifetime::Persistent, storageBufDesc);

        // Output: byte size (transient scalar value)
        BufferDescription sizeDesc{};
        INIT_OUTPUT_DESC(BUFFER_SIZE, "buffer_size",
            ResourceLifetime::Transient, sizeDesc);
    }

    // ----- Compile-time validation -----
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE_IN must not be nullable");
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>,
                  "VULKAN_DEVICE_IN must be VulkanDevice*");

    static_assert(STORAGE_BUFFER_Slot::index == 0, "STORAGE_BUFFER must be at index 0");
    static_assert(!STORAGE_BUFFER_Slot::nullable, "STORAGE_BUFFER must not be nullable");
    static_assert(std::is_same_v<STORAGE_BUFFER_Slot::Type, VkBuffer>,
                  "STORAGE_BUFFER must be VkBuffer");

    static_assert(BUFFER_SIZE_Slot::index == 1, "BUFFER_SIZE must be at index 1");
    static_assert(std::is_same_v<BUFFER_SIZE_Slot::Type, uint32_t>,
                  "BUFFER_SIZE must be uint32_t");

    VALIDATE_NODE_CONFIG(StorageBufferNodeConfig, StorageBufferNodeCounts);
};

} // namespace Vixen::RenderGraph
