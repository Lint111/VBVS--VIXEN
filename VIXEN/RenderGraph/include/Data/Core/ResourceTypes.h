#pragma once

#include <cstdint>

namespace Vixen::RenderGraph {

/**
 * @brief Resource type enumeration
 */
enum class ResourceType {
    Image,           // 2D texture, render target
    Buffer,          // Vertex, index, uniform, storage buffer
    CubeMap,         // Cubemap texture
    Image3D,         // 3D texture
    StorageImage,    // Storage image for compute
    AccelerationStructure  // Ray tracing AS
};

/**
 * @brief Resource usage flags
 */
enum class ResourceUsage : uint32_t {
    None                  = 0,
    TransferSrc           = 1 << 0,
    TransferDst           = 1 << 1,
    Sampled               = 1 << 2,
    Storage               = 1 << 3,
    ColorAttachment       = 1 << 4,
    DepthStencilAttachment = 1 << 5,
    InputAttachment       = 1 << 6,
    VertexBuffer          = 1 << 7,
    IndexBuffer           = 1 << 8,
    UniformBuffer         = 1 << 9,
    StorageBuffer         = 1 << 10,
    IndirectBuffer        = 1 << 11,
    CommandPool           = 1 << 12,
    ShaderModuleType      = 1 << 13
};

// Bitwise operators for ResourceUsage flags
constexpr inline ResourceUsage operator|(ResourceUsage a, ResourceUsage b) {
    return static_cast<ResourceUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr inline ResourceUsage operator&(ResourceUsage a, ResourceUsage b) {
    return static_cast<ResourceUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr inline ResourceUsage operator~(ResourceUsage a) {
    return static_cast<ResourceUsage>(~static_cast<uint32_t>(a));
}

constexpr inline ResourceUsage& operator|=(ResourceUsage& a, ResourceUsage b) {
    a = a | b;
    return a;
}

constexpr inline ResourceUsage& operator&=(ResourceUsage& a, ResourceUsage b) {
    a = a & b;
    return a;
}

constexpr inline bool operator==(ResourceUsage a, ResourceUsage b) {
    return static_cast<uint32_t>(a) == static_cast<uint32_t>(b);
}

constexpr inline bool operator!=(ResourceUsage a, ResourceUsage b) {
    return static_cast<uint32_t>(a) != static_cast<uint32_t>(b);
}

constexpr inline bool HasUsage(ResourceUsage flags, ResourceUsage check) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(check)) != 0;
}

/**
 * @brief Resource lifetime hint
 */
enum class ResourceLifetime {
    Transient,   // Short-lived, can be aliased
    Persistent,  // Long-lived, externally managed
    Imported,    // External resource (swapchain, etc.)
    Static       // Immutable after creation (from original enum)
};

} // namespace Vixen::RenderGraph