#pragma once

#include "Headers.h"
#include <string>
#include <optional>
#include <memory>
#include "Core/ResourceTypes.h"

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace ShaderManagement {
    struct CompiledProgram;
}

namespace Vixen::RenderGraph {

// Forward declarations
class NodeInstance;

// Extended resource usage flags (ResourceType/ResourceLifetime in ResourceTypes.h)
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
 * @brief Base resource description
 */
struct ResourceDescription {
    ResourceType type = ResourceType::Image;

    ResourceDescription(ResourceType t) : type(t) {}
    virtual ~ResourceDescription() = default;
    virtual bool operator==(const ResourceDescription& other) const = 0;
    // Polymorphic clone to allow copying via base pointer
    virtual std::unique_ptr<ResourceDescription> Clone() const = 0;
};

/**
 * @brief Image resource description
 */
struct ImageDescription : public ResourceDescription {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    ResourceUsage usage = ResourceUsage::None;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;

    ImageDescription() : ResourceDescription(ResourceType::Image) {}
    ImageDescription(const ImageDescription&) = default;
    ImageDescription& operator=(const ImageDescription&) = default;

    bool operator==(const ResourceDescription& other) const override {
        auto* otherImg = dynamic_cast<const ImageDescription*>(&other);
        if (!otherImg) return false;
        return width == otherImg->width &&
               height == otherImg->height &&
               depth == otherImg->depth &&
               mipLevels == otherImg->mipLevels &&
               arrayLayers == otherImg->arrayLayers &&
               format == otherImg->format &&
               samples == otherImg->samples &&
               usage == otherImg->usage &&
               tiling == otherImg->tiling;
    }
    std::unique_ptr<ResourceDescription> Clone() const override {
        return std::make_unique<ImageDescription>(*this);
    }
};

/**
 * @brief Buffer resource description
 */
struct BufferDescription : public ResourceDescription {
    VkDeviceSize size = 0;
    ResourceUsage usage = ResourceUsage::None;
    VkMemoryPropertyFlags memoryProperties = 0;

    BufferDescription() : ResourceDescription(ResourceType::Buffer) {}
    BufferDescription(const BufferDescription&) = default;
    BufferDescription& operator=(const BufferDescription&) = default;

    bool operator==(const ResourceDescription& other) const override {
        auto* otherBuf = dynamic_cast<const BufferDescription*>(&other);
        if (!otherBuf) return false;
        return size == otherBuf->size &&
               usage == otherBuf->usage &&
               memoryProperties == otherBuf->memoryProperties;
    }
    std::unique_ptr<ResourceDescription> Clone() const override {
        return std::make_unique<BufferDescription>(*this);
    }
};

/**
 * @brief Command pool resource description
 * Used for command buffer allocations
 */
struct CommandPoolDescription : public ResourceDescription {
    VkCommandPoolCreateFlags flags = 0;
    uint32_t queueFamilyIndex = 0;

    CommandPoolDescription() : ResourceDescription(ResourceType::Buffer) {} // CommandPool not in enum, using Buffer
    CommandPoolDescription(const CommandPoolDescription&) = default;
    CommandPoolDescription& operator=(const CommandPoolDescription&) = default;

    bool operator==(const ResourceDescription& other) const override {
        auto* otherPool = dynamic_cast<const CommandPoolDescription*>(&other);
        if (!otherPool) return false;
        return flags == otherPool->flags &&
               queueFamilyIndex == otherPool->queueFamilyIndex;
    }
    std::unique_ptr<ResourceDescription> Clone() const override {
        return std::make_unique<CommandPoolDescription>(*this);
    }
};

/**
 * @brief Device object resource description
 * Wraps a VulkanDevice pointer
 */
struct DeviceObjectDescription : public ResourceDescription {
    VkDevice device = VK_NULL_HANDLE;

    DeviceObjectDescription() : ResourceDescription(ResourceType::Buffer) {} // DeviceObject not in enum, using Buffer
    DeviceObjectDescription(const DeviceObjectDescription&) = default;
    DeviceObjectDescription& operator=(const DeviceObjectDescription&) = default;

    bool operator==(const ResourceDescription& other) const override {
        auto* otherDev = dynamic_cast<const DeviceObjectDescription*>(&other);
        if (!otherDev) return false;
        return device == otherDev->device;
    }
    std::unique_ptr<ResourceDescription> Clone() const override {
        return std::make_unique<DeviceObjectDescription>(*this);
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

    // Type-specific description (polymorphic, unique ownership per descriptor)
    // We provide deep-copy semantics via a copy-constructor that clones the
    // underlying ResourceDescription so ResourceDescriptor remains copyable
    // for use in containers while storing the description as unique_ptr.
    std::unique_ptr<ResourceDescription> description;

    // Optional - for validation
    bool optional = false;

    // Deep-copy copy-constructor/assignment to clone the description so that
    // ResourceDescriptor remains copyable when stored in containers.
    ResourceDescriptor() = default;

    ResourceDescriptor(const ResourceDescriptor& other)
        : name(other.name), type(other.type), lifetime(other.lifetime), optional(other.optional) {
        if (other.description) description = other.description->Clone();
    }

    ResourceDescriptor& operator=(const ResourceDescriptor& other) {
        if (this == &other) return *this;
        name = other.name;
        type = other.type;
        lifetime = other.lifetime;
        optional = other.optional;
        if (other.description) description = other.description->Clone();
        else description.reset();
        return *this;
    }

    // Move operations
    ResourceDescriptor(ResourceDescriptor&&) noexcept = default;
    ResourceDescriptor& operator=(ResourceDescriptor&&) noexcept = default;

    // Construct from a concrete DescType (makes unique copy)
    template<typename DescType>
    ResourceDescriptor(
        const std::string& n,
        ResourceType t,
        ResourceLifetime l,
        const DescType& desc,
        bool opt = false
    ) : name(n), type(t), lifetime(l), description(std::make_unique<DescType>(desc)), optional(opt) {}

    // Construct from an already-created unique_ptr (takes ownership)
    ResourceDescriptor(
        const std::string& n,
        ResourceType t,
        ResourceLifetime l,
        std::unique_ptr<ResourceDescription> descPtr,
        bool opt = false
    ) : name(n), type(t), lifetime(l), description(std::move(descPtr)), optional(opt) {}
};

/**
 * @brief Actual resource instance
 */
class Resource {
public:
    Resource() = default;

    template<typename DescType>
    Resource(
        ResourceType type,
        ResourceLifetime lifetime,
        const DescType& desc
    ) : type(type), lifetime(lifetime), description(std::make_unique<DescType>(desc)) {}

    // Construct from an already-created description (takes ownership)
    Resource(
        ResourceType type,
        ResourceLifetime lifetime,
        std::unique_ptr<ResourceDescription> descPtr
    ) : type(type), lifetime(lifetime), description(std::move(descPtr)) {}

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
    VkCommandPool GetCommandPool() const { return commandPool; }
    VkDevice GetDevice() const { return device; }
    size_t GetMemorySize() const { return memorySize; }
    
    // Shader program pointer (for opaque data types)
    const ShaderManagement::CompiledProgram* GetCompiledProgram() const { return compiledProgram; }
    void SetCompiledProgram(const ShaderManagement::CompiledProgram* program) { compiledProgram = program; }

    template<typename T>
    const T* GetDescription() const {
        return dynamic_cast<const T*>(description.get());
    }

    const ImageDescription* GetImageDescription() const { return GetDescription<ImageDescription>(); }
    const BufferDescription* GetBufferDescription() const { return GetDescription<BufferDescription>(); }
    const CommandPoolDescription* GetCommandPoolDescription() const { return GetDescription<CommandPoolDescription>(); }
    const DeviceObjectDescription* GetDeviceObjectDescription() const { return GetDescription<DeviceObjectDescription>(); }

    // Setters for Vulkan handles
    void SetImage(VkImage img) { image = img; }
    void SetBuffer(VkBuffer buf) { buffer = buf; }
    void SetCommandPool(VkCommandPool pool) { commandPool = pool; }
    void SetDevice(VkDevice dev) { device = dev; }

    // State tracking
    void SetCurrentLayout(VkImageLayout layout) { currentLayout = layout; }
    VkImageLayout GetCurrentLayout() const { return currentLayout; }

    void SetOwningNode(NodeInstance* node) { owningNode = node; }
    NodeInstance* GetOwningNode() const { return owningNode; }

    // Device dependency tracking
    void SetDeviceDependency(Vixen::Vulkan::Resources::VulkanDevice* dev) { deviceDependency = dev; }
    Vixen::Vulkan::Resources::VulkanDevice* GetDeviceDependency() const { return deviceDependency; }

    // Allocation (managed by ResourceAllocator)
    void AllocateImage(VkDevice device, const ImageDescription& desc);
    void AllocateBuffer(VkDevice device, const BufferDescription& desc);
    void CreateImageView(VkDevice device, VkImageAspectFlags aspectMask);
    void Destroy(VkDevice device);

    bool IsAllocated() const { return (image != VK_NULL_HANDLE || buffer != VK_NULL_HANDLE || commandPool != VK_NULL_HANDLE || device != VK_NULL_HANDLE); }
    bool IsValid() const { return IsAllocated(); }

private:
    ResourceType type = ResourceType::Image;
    ResourceLifetime lifetime = ResourceLifetime::Transient;
    std::unique_ptr<ResourceDescription> description;

    // Vulkan resources
    VkImage image = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;  // For DeviceObject resources
    size_t memorySize = 0;

    // Opaque data pointers (for non-Vulkan types)
    const ShaderManagement::CompiledProgram* compiledProgram = nullptr;

    // State tracking
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    NodeInstance* owningNode = nullptr;

    // Device dependency (which VulkanDevice this resource belongs to)
    Vixen::Vulkan::Resources::VulkanDevice* deviceDependency = nullptr;

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
