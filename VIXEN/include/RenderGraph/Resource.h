#pragma once

#include "../Headers.h"
#include <string>
#include <variant>
#include <optional>

namespace Vixen::RenderGraph {

// Forward declarations
class NodeInstance;

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
    IndirectBuffer        = 1 << 11
};

inline ResourceUsage operator|(ResourceUsage a, ResourceUsage b) {
    return static_cast<ResourceUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ResourceUsage operator&(ResourceUsage a, ResourceUsage b) {
    return static_cast<ResourceUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasUsage(ResourceUsage flags, ResourceUsage check) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(check)) != 0;
}

/**
 * @brief Resource lifetime classification
 */
enum class ResourceLifetime {
    Transient,    // Short-lived, can be aliased
    Persistent,   // Long-lived, externally managed
    Imported      // External resource (swapchain, etc.)
};

/**
 * @brief Image resource description
 */
struct ImageDescription {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    ResourceUsage usage = ResourceUsage::None;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;

    bool operator==(const ImageDescription& other) const {
        return width == other.width &&
               height == other.height &&
               depth == other.depth &&
               mipLevels == other.mipLevels &&
               arrayLayers == other.arrayLayers &&
               format == other.format &&
               samples == other.samples &&
               usage == other.usage &&
               tiling == other.tiling;
    }
};

/**
 * @brief Buffer resource description
 */
struct BufferDescription {
    VkDeviceSize size = 0;
    ResourceUsage usage = ResourceUsage::None;
    VkMemoryPropertyFlags memoryProperties = 0;

    bool operator==(const BufferDescription& other) const {
        return size == other.size &&
               usage == other.usage &&
               memoryProperties == other.memoryProperties;
    }
};

/**
 * @brief Resource descriptor (schema definition)
 * Used by NodeType to describe expected inputs/outputs
 */
struct ResourceDescriptor {
    std::string name;
    ResourceType type;
    ResourceLifetime lifetime;
    
    // Type-specific description
    std::variant<ImageDescription, BufferDescription> description;

    // Optional - for validation
    bool optional = false;

    ResourceDescriptor() = default;
    
    ResourceDescriptor(
        const std::string& n,
        ResourceType t,
        ResourceLifetime l,
        const std::variant<ImageDescription, BufferDescription>& desc
    ) : name(n), type(t), lifetime(l), description(desc) {}
};

/**
 * @brief Actual resource instance
 */
class Resource {
public:
    Resource() = default;
    
    Resource(
        ResourceType type,
        ResourceLifetime lifetime,
        const std::variant<ImageDescription, BufferDescription>& description
    );

    ~Resource();

    // Prevent copying (resources are unique)
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;

    // Allow moving
    Resource(Resource&& other) noexcept;
    Resource& operator=(Resource&& other) noexcept;

    // Getters
    ResourceType GetType() const { return type; }
    ResourceLifetime GetLifetime() const { return lifetime; }
    VkImage GetImage() const { return image; }
    VkBuffer GetBuffer() const { return buffer; }
    VkDeviceMemory GetMemory() const { return memory; }
    VkImageView GetImageView() const { return imageView; }
    size_t GetMemorySize() const { return memorySize; }
    const ImageDescription* GetImageDescription() const;
    const BufferDescription* GetBufferDescription() const;

    // State tracking
    void SetCurrentLayout(VkImageLayout layout) { currentLayout = layout; }
    VkImageLayout GetCurrentLayout() const { return currentLayout; }
    
    void SetOwningNode(NodeInstance* node) { owningNode = node; }
    NodeInstance* GetOwningNode() const { return owningNode; }

    // Allocation (managed by ResourceAllocator)
    void AllocateImage(VkDevice device, const ImageDescription& desc);
    void AllocateBuffer(VkDevice device, const BufferDescription& desc);
    void CreateImageView(VkDevice device, VkImageAspectFlags aspectMask);
    void Destroy(VkDevice device);

    bool IsAllocated() const { return (image != VK_NULL_HANDLE || buffer != VK_NULL_HANDLE); }
    bool IsValid() const { return IsAllocated(); }

private:
    ResourceType type = ResourceType::Image;
    ResourceLifetime lifetime = ResourceLifetime::Transient;
    std::variant<ImageDescription, BufferDescription> description;

    // Vulkan resources
    VkImage image = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    size_t memorySize = 0;

    // State tracking
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    NodeInstance* owningNode = nullptr;

    // Helper for memory allocation
    uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
};

/**
 * @brief Resource handle for graph connections
 */
struct ResourceHandle {
    uint32_t nodeIndex = 0;
    uint32_t resourceIndex = 0;
    
    bool IsValid() const { return nodeIndex != UINT32_MAX && resourceIndex != UINT32_MAX; }
    
    bool operator==(const ResourceHandle& other) const {
        return nodeIndex == other.nodeIndex && resourceIndex == other.resourceIndex;
    }
};

} // namespace Vixen::RenderGraph
