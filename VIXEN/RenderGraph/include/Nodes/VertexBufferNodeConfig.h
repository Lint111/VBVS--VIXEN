#pragma once

#include "Core/ResourceConfig.h"

namespace Vixen::RenderGraph {

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
CONSTEXPR_NODE_CONFIG(VertexBufferNodeConfig, 0, 2, false) {
    // ===== PARAMETER NAMES =====
    static constexpr const char* PARAM_VERTEX_COUNT = "vertexCount";
    static constexpr const char* PARAM_VERTEX_STRIDE = "vertexStride";
    static constexpr const char* PARAM_USE_TEXTURE = "useTexture";
    static constexpr const char* PARAM_INDEX_COUNT = "indexCount";

    // ===== INPUTS (0) =====
    // No inputs - vertex data provided via parameters or embedded

    // ===== OUTPUTS (2) =====
    // Vertex buffer handle
    CONSTEXPR_OUTPUT(VERTEX_BUFFER, VkBuffer, 0, false);

    // Index buffer handle (nullable - may not use indexed rendering)
    CONSTEXPR_OUTPUT(INDEX_BUFFER, VkBuffer, 1, true);

    VertexBufferNodeConfig() {
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
    }

    // Compile-time validations
    static_assert(VERTEX_BUFFER_Slot::index == 0, "VERTEX_BUFFER must be at index 0");
    static_assert(!VERTEX_BUFFER_Slot::nullable, "VERTEX_BUFFER is required");

    static_assert(INDEX_BUFFER_Slot::index == 1, "INDEX_BUFFER must be at index 1");
    static_assert(INDEX_BUFFER_Slot::nullable, "INDEX_BUFFER is optional");

    // Type validations
    static_assert(std::is_same_v<VERTEX_BUFFER_Slot::Type, VkBuffer>);
    static_assert(std::is_same_v<INDEX_BUFFER_Slot::Type, VkBuffer>);
};

// Global compile-time validations
static_assert(VertexBufferNodeConfig::INPUT_COUNT == 0);
static_assert(VertexBufferNodeConfig::OUTPUT_COUNT == 2);

} // namespace Vixen::RenderGraph