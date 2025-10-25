#pragma once

#include "Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;

/**
 * @brief Pure constexpr resource configuration for VertexBufferNode
 *
 * Inputs: None (vertex data provided via parameters)
 *
 * Outputs:
 * - VERTEX_BUFFER (VkBuffer) - GPU buffer containing vertex data
 * - INDEX_BUFFER (VkBuffer) - GPU buffer containing index data (nullable)
 *
 * Parameters:
 * - VERTEX_COUNT (uint32_t) - Number of vertices
 * - VERTEX_STRIDE (uint32_t) - Size of each vertex in bytes
 * - USE_TEXTURE (bool) - Whether vertices use texture coordinates
 * - INDEX_COUNT (uint32_t) - Number of indices (0 = no indices)
 *
 * ALL type checking happens at compile time!
 */
// Compile-time slot counts (declared early for reuse)
namespace VertexBufferNodeCounts {
    static constexpr size_t INPUTS = 1;
    static constexpr size_t OUTPUTS = 3;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(VertexBufferNodeConfig,
                      VertexBufferNodeCounts::INPUTS,
                      VertexBufferNodeCounts::OUTPUTS,
                      VertexBufferNodeCounts::ARRAY_MODE) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* PARAM_VERTEX_COUNT = "vertexCount";
    static constexpr const char* PARAM_VERTEX_STRIDE = "vertexStride";
    static constexpr const char* PARAM_USE_TEXTURE = "useTexture";
    static constexpr const char* PARAM_INDEX_COUNT = "indexCount";

    // ===== INPUTS (1) =====
    // VulkanDevice pointer (contains device, gpu, memory properties, etc.)
    CONSTEXPR_INPUT(VULKAN_DEVICE_IN, VulkanDevicePtr, 0, false);

    // ===== OUTPUTS (3) =====
    // Vertex buffer handle
    CONSTEXPR_OUTPUT(VERTEX_BUFFER, VkBuffer, 0, false);

    // Index buffer handle (nullable - may not use indexed rendering)
    CONSTEXPR_OUTPUT(INDEX_BUFFER, VkBuffer, 1, true);

    // Device output for chaining
    CONSTEXPR_OUTPUT(VULKAN_DEVICE_OUT, VulkanDevicePtr, 2, false);

    VertexBufferNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        // Initialize output descriptors
        BufferDescription vertexBufDesc{};
        vertexBufDesc.size = 1024 * 1024; // Default 1MB
        vertexBufDesc.usage = ResourceUsage::VertexBuffer;
        vertexBufDesc.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        INIT_OUTPUT_DESC(VERTEX_BUFFER, "vertex_buffer",
            ResourceLifetime::Persistent,
            vertexBufDesc
        );

        BufferDescription indexBufDesc{};
        indexBufDesc.size = 256 * 1024; // Default 256KB
        indexBufDesc.usage = ResourceUsage::IndexBuffer;
        indexBufDesc.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        INIT_OUTPUT_DESC(INDEX_BUFFER, "index_buffer",
            ResourceLifetime::Persistent,
            indexBufDesc
        );

        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);
    }

    // Compile-time validations
    static_assert(INPUT_COUNT == VertexBufferNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == VertexBufferNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == VertexBufferNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE input must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE input is required");

    static_assert(VERTEX_BUFFER_Slot::index == 0, "VERTEX_BUFFER must be at index 0");
    static_assert(!VERTEX_BUFFER_Slot::nullable, "VERTEX_BUFFER is required");

    static_assert(INDEX_BUFFER_Slot::index == 1, "INDEX_BUFFER must be at index 1");
    static_assert(INDEX_BUFFER_Slot::nullable, "INDEX_BUFFER is optional");

    static_assert(VULKAN_DEVICE_OUT_Slot::index == 2, "DEVICE_OUT must be at index 2");
    static_assert(!VULKAN_DEVICE_OUT_Slot::nullable, "DEVICE_OUT is required");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<VERTEX_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<INDEX_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevicePtr>);
};

// Global compile-time validations
static_assert(VertexBufferNodeConfig::INPUT_COUNT == VertexBufferNodeCounts::INPUTS, "Input count mismatch");
static_assert(VertexBufferNodeConfig::OUTPUT_COUNT == VertexBufferNodeCounts::OUTPUTS, "Output count mismatch");

} // namespace Vixen::RenderGraph