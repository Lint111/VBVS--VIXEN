#pragma once

/**
 * @file Resource.h
 * @brief Clean, type-safe resource system for RenderGraph
 * 
 * ARCHITECTURE:
 * 
 * 1. ResourceTraits<T> - Maps resource type to descriptor
 *    Example: ResourceTraits<VkImage>::DescriptorType = ImageDescriptor
 * 
 * 2. Descriptor<Derived, T> - CRTP base for typed descriptors
 *    - Stores metadata/config for resource type T
 *    - Automatically provides Clone() and type association
 *    Example: ImageDescriptor : Descriptor<ImageDescriptor, VkImage>
 * 
 * 3. Resource<T> - Typed resource container
 *    - Stores actual resource value of type T
 *    - Automatically uses correct descriptor via ResourceTraits
 *    - No bloated null fields - only what's needed for T
 *    Example: Resource<VkImage> only has VkImage + ImageDescriptor
 * 
 * 4. IResource - Base interface for polymorphic storage
 *    - Allows storing different Resource<T> types in containers
 *    - Provides As<T>() for type-safe downcasting
 * 
 * USAGE:
 * 
 *   // Create typed resource
 *   auto imgRes = std::make_unique<Resource<VkImage>>(
 *       ResourceLifetime::Persistent,
 *       ImageDescriptor{.width=1024, .height=768}
 *   );
 * 
 *   // Access resource and descriptor
 *   VkImage img = imgRes->Get();                    // Get the actual VkImage
 *   ImageDescriptor* desc = imgRes->GetDescriptor(); // Get metadata
 * 
 *   // Polymorphic storage
 *   IResource* base = imgRes.get();
 *   if (auto* typed = base->As<VkImage>()) {
 *       VkImage img = typed->Get();
 *   }
 * 
 * BENEFITS:
 * - Type-safe: Compile-time type checking
 * - No bloat: No null VkBuffer when you have VkImage
 * - Clean API: Just Get() and GetDescriptor()
 * - Extensible: Add new types by specializing ResourceTraits
 */

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

// ============================================================================
// TYPE TRAITS - Maps resource types to their descriptors
// ============================================================================

// Forward declarations of descriptors
struct ImageDescriptor;
struct BufferDescriptor;
struct CommandPoolDescriptor;
struct DeviceDescriptor;
struct ShaderProgramDescriptor;

/**
 * @brief Trait to map resource type T to its descriptor type
 * 
 * Usage:
 *   using DescType = ResourceTraits<VkImage>::DescriptorType;  // ImageDescriptor
 *   using ResType = ResourceTraits<VkImage>::ResourceType;      // VkImage
 */
template<typename T>
struct ResourceTraits;

// Specializations
template<>
struct ResourceTraits<VkImage> {
    using ResourceType_t = VkImage;
    using DescriptorType = ImageDescriptor;
};

template<>
struct ResourceTraits<VkBuffer> {
    using ResourceType_t = VkBuffer;
    using DescriptorType = BufferDescriptor;
};

template<>
struct ResourceTraits<VkCommandPool> {
    using ResourceType_t = VkCommandPool;
    using DescriptorType = CommandPoolDescriptor;
};

template<>
struct ResourceTraits<VkDevice> {
    using ResourceType_t = VkDevice;
    using DescriptorType = DeviceDescriptor;
};

template<>
struct ResourceTraits<ShaderManagement::CompiledProgram> {
    using ResourceType_t = ShaderManagement::CompiledProgram;
    using DescriptorType = ShaderProgramDescriptor;
};

// Helper alias
template<typename T>
using DescriptorFor = typename ResourceTraits<T>::DescriptorType;

// ============================================================================
// BASE DESCRIPTOR INTERFACE
// ============================================================================

/**
 * @brief Base descriptor interface for resource metadata
 */
class IDescriptor {
public:
    virtual ~IDescriptor() = default;
    virtual std::unique_ptr<IDescriptor> Clone() const = 0;
    virtual ResourceType GetResourceType() const = 0;
};

/**
 * @brief CRTP base for typed descriptors
 * 
 * Automatically provides Clone() and type association.
 */
template<typename Derived, typename ResourceT>
class Descriptor : public IDescriptor {
public:
    using ResourceType_t = ResourceT;
    
    std::unique_ptr<IDescriptor> Clone() const override {
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    }
};

// ============================================================================
// TYPED DESCRIPTORS (metadata for each resource type)
// ============================================================================

/**
 * @brief Image resource descriptor - stores VkImage metadata
 */
struct ImageDescriptor : public Descriptor<ImageDescriptor, VkImage> {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    ResourceUsage usage = ResourceUsage::None;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    ResourceType GetResourceType() const override { 
        return ResourceType::Image; 
    }
};

/**
 * @brief Buffer resource descriptor - stores VkBuffer metadata
 */
struct BufferDescriptor : public Descriptor<BufferDescriptor, VkBuffer> {
    VkDeviceSize size = 0;
    ResourceUsage usage = ResourceUsage::None;
    VkMemoryPropertyFlags memoryProperties = 0;
    
    ResourceType GetResourceType() const override { 
        return ResourceType::Buffer; 
    }
};

/**
 * @brief Command pool descriptor - stores VkCommandPool metadata
 */
struct CommandPoolDescriptor : public Descriptor<CommandPoolDescriptor, VkCommandPool> {
    VkCommandPoolCreateFlags flags = 0;
    uint32_t queueFamilyIndex = 0;
    
    ResourceType GetResourceType() const override { 
        return ResourceType::Buffer; // TODO: Add CommandPool to ResourceType enum
    }
};

/**
 * @brief Device descriptor - stores VkDevice metadata
 */
struct DeviceDescriptor : public Descriptor<DeviceDescriptor, VkDevice> {
    VkDevice device = VK_NULL_HANDLE;
    
    ResourceType GetResourceType() const override { 
        return ResourceType::Buffer; // TODO: Add Device to ResourceType enum
    }
};

/**
 * @brief Shader program descriptor - defined in ShaderLibraryNodeConfig.h
 * Forward declaration only - actual definition has VkShaderModule and stages
 */
struct ShaderProgramDescriptor;
// NOTE: Full definition in Nodes/ShaderLibraryNodeConfig.h to avoid circular dependency

// Backward compatibility aliases
using ResourceDescription = IDescriptor;
using ImageDescription = ImageDescriptor;
using BufferDescription = BufferDescriptor;
using CommandPoolDescription = CommandPoolDescriptor;
using DeviceObjectDescription = DeviceDescriptor;

// Forward declare Resource template (needed for IResource::As<T>())
template<typename T>
class Resource;

// ============================================================================
// BASE RESOURCE INTERFACE
// ============================================================================

/**
 * @brief Base resource interface for type-erased storage
 */
class IResource {
public:
    virtual ~IResource() = default;
    
    // Resource metadata
    virtual ResourceType GetType() const = 0;
    virtual ResourceLifetime GetLifetime() const = 0;
    virtual const IDescriptor* GetDescriptor() const = 0;
    
    // Type-safe descriptor access with template
    template<typename DescriptorType>
    const DescriptorType* GetDescription() const {
        const IDescriptor* desc = GetDescriptor();
        return dynamic_cast<const DescriptorType*>(desc);
    }
    
    template<typename DescriptorType>
    DescriptorType* GetDescription() {
        IDescriptor* desc = const_cast<IDescriptor*>(GetDescriptor());
        return dynamic_cast<DescriptorType*>(desc);
    }
    
    // Resource-specific accessors (return null/default if wrong type)
    virtual VkImageView GetImageView() const { return VK_NULL_HANDLE; }
    virtual VkBuffer GetBuffer() const { return VK_NULL_HANDLE; }
    virtual VkCommandPool GetCommandPool() const { return VK_NULL_HANDLE; }
    
    // Mutators for specific resource types (no-op if wrong type)
    virtual void SetImageView(VkImageView view) {}
    virtual void SetBuffer(VkBuffer buffer) {}
    virtual void SetCommandPool(VkCommandPool pool) {}
    
    // Memory footprint
    virtual size_t GetMemorySize() const = 0;
    
    // State management
    virtual void SetOwningNode(NodeInstance* node) = 0;
    virtual NodeInstance* GetOwningNode() const = 0;
    
    virtual void SetDeviceDependency(Vixen::Vulkan::Resources::VulkanDevice* dev) = 0;
    virtual Vixen::Vulkan::Resources::VulkanDevice* GetDeviceDependency() const = 0;
    
    virtual bool IsAllocated() const = 0;
    virtual bool IsValid() const = 0;
    
    // Type-safe downcasting
    template<typename T>
    Resource<T>* As() {
        return dynamic_cast<Resource<T>*>(this);
    }
    
    template<typename T>
    const Resource<T>* As() const {
        return dynamic_cast<const Resource<T>*>(this);
    }
};

// ============================================================================
// TYPED RESOURCE (stores actual resource handle/data)
// ============================================================================

/**
 * @brief Typed resource container
 * 
 * Stores the actual resource data for type T.
 * No bloated null fields - only what's needed for T.
 * Automatically uses the correct descriptor type via ResourceTraits.
 */
template<typename T>
class Resource : public IResource {
public:
    using DescriptorType = DescriptorFor<T>;

    Resource() = default;
    
    Resource(ResourceLifetime lifetime, const DescriptorType& desc)
        : lifetime(lifetime), descriptor(std::make_unique<DescriptorType>(desc)) {}
    
    ~Resource() = default;

    // Prevent copying (resources are unique)
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;

    // Allow moving
    Resource(Resource&&) noexcept = default;
    Resource& operator=(Resource&&) noexcept = default;

    // Access the resource value
    T& Get() { return value; }
    const T& Get() const { return value; }
    void Set(const T& val) { value = val; }
    
    // Access descriptor (metadata)
    DescriptorType* GetDescriptor() { 
        return descriptor.get(); 
    }
    
    const DescriptorType* GetDescriptor() const { 
        return descriptor.get(); 
    }

    // IResource interface
    ResourceType GetType() const override { 
        return descriptor ? descriptor->GetResourceType() : ResourceType::Image; 
    }
    
    ResourceLifetime GetLifetime() const override { 
        return lifetime; 
    }
    
    const IDescriptor* GetDescriptor() const override { 
        return descriptor.get(); 
    }
    
    void SetOwningNode(NodeInstance* node) override { 
        owningNode = node; 
    }
    
    NodeInstance* GetOwningNode() const override { 
        return owningNode; 
    }
    
    void SetDeviceDependency(Vixen::Vulkan::Resources::VulkanDevice* dev) override { 
        deviceDependency = dev; 
    }
    
    Vixen::Vulkan::Resources::VulkanDevice* GetDeviceDependency() const override { 
        return deviceDependency; 
    }
    
    bool IsAllocated() const override {
        if constexpr (std::is_pointer_v<T>) {
            return value != nullptr;
        } else {
            return value != VK_NULL_HANDLE;
        }
    }
    
    bool IsValid() const override { 
        return IsAllocated(); 
    }

private:
    T value = {}; // The actual resource (VkImage, VkBuffer, etc.)
    std::unique_ptr<DescriptorType> descriptor; // Metadata for this resource
    ResourceLifetime lifetime = ResourceLifetime::Transient;
    NodeInstance* owningNode = nullptr;
    Vixen::Vulkan::Resources::VulkanDevice* deviceDependency = nullptr;
};

// ============================================================================
// SPECIALIZED RESOURCES (for complex types needing extra state)
// ============================================================================

/**
 * @brief Specialized Image resource with image-specific state
 */
template<>
class Resource<VkImage> : public IResource {
public:
    Resource() = default;
    
    Resource(ResourceLifetime lifetime, const ImageDescriptor& desc)
        : lifetime(lifetime), descriptor(std::make_unique<ImageDescriptor>(desc)) {}
    
    ~Resource() = default;

    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
    Resource(Resource&&) noexcept = default;
    Resource& operator=(Resource&&) noexcept = default;

    // Access
    VkImage& Get() { return image; }
    const VkImage& Get() const { return image; }
    void Set(VkImage img) { image = img; }
    
    ImageDescriptor* GetDescriptor() { return descriptor.get(); }
    const ImageDescriptor* GetDescriptor() const { return descriptor.get(); }

    // Image-specific state
    VkImageView GetImageView() const { return imageView; }
    void SetImageView(VkImageView view) { imageView = view; }
    
    VkImageLayout GetCurrentLayout() const { return currentLayout; }
    void SetCurrentLayout(VkImageLayout layout) { currentLayout = layout; }
    
    VkDeviceMemory GetMemory() const { return memory; }
    void SetMemory(VkDeviceMemory mem) { memory = mem; }
    
    size_t GetMemorySize() const { return memorySize; }
    void SetMemorySize(size_t size) { memorySize = size; }
    
    // Allocation helpers
    void AllocateImage(VkDevice device, const ImageDescriptor& desc);
    void CreateImageView(VkDevice device, VkImageAspectFlags aspectMask);
    void Destroy(VkDevice device);

    // IResource interface
    ResourceType GetType() const override { return ResourceType::Image; }
    ResourceLifetime GetLifetime() const override { return lifetime; }
    const IDescriptor* GetDescriptor() const override { return descriptor.get(); }
    
    // Image-specific overrides
    VkImageView GetImageView() const override { return imageView; }
    void SetImageView(VkImageView view) override { imageView = view; }
    size_t GetMemorySize() const override { return memorySize; }
    
    void SetOwningNode(NodeInstance* node) override { owningNode = node; }
    NodeInstance* GetOwningNode() const override { return owningNode; }
    void SetDeviceDependency(Vixen::Vulkan::Resources::VulkanDevice* dev) override { deviceDependency = dev; }
    Vixen::Vulkan::Resources::VulkanDevice* GetDeviceDependency() const override { return deviceDependency; }
    bool IsAllocated() const override { return image != VK_NULL_HANDLE; }
    bool IsValid() const override { return IsAllocated(); }

private:
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    size_t memorySize = 0;
    std::unique_ptr<ImageDescriptor> descriptor;
    ResourceLifetime lifetime = ResourceLifetime::Transient;
    NodeInstance* owningNode = nullptr;
    Vixen::Vulkan::Resources::VulkanDevice* deviceDependency = nullptr;
    
    uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
};

/**
 * @brief Specialized Buffer resource with buffer-specific state
 */
template<>
class Resource<VkBuffer> : public IResource {
public:
    Resource() = default;
    
    Resource(ResourceLifetime lifetime, const BufferDescriptor& desc)
        : lifetime(lifetime), descriptor(std::make_unique<BufferDescriptor>(desc)) {}
    
    ~Resource() = default;

    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
    Resource(Resource&&) noexcept = default;
    Resource& operator=(Resource&&) noexcept = default;

    // Access
    VkBuffer& Get() { return buffer; }
    const VkBuffer& Get() const { return buffer; }
    void Set(VkBuffer buf) { buffer = buf; }
    
    BufferDescriptor* GetDescriptor() { return descriptor.get(); }
    const BufferDescriptor* GetDescriptor() const { return descriptor.get(); }

    // Buffer-specific state
    VkDeviceMemory GetMemory() const { return memory; }
    void SetMemory(VkDeviceMemory mem) { memory = mem; }
    
    size_t GetMemorySize() const { return memorySize; }
    void SetMemorySize(size_t size) { memorySize = size; }
    
    // Allocation helpers
    void AllocateBuffer(VkDevice device, const BufferDescriptor& desc);
    void Destroy(VkDevice device);

    // IResource interface
    ResourceType GetType() const override { return ResourceType::Buffer; }
    ResourceLifetime GetLifetime() const override { return lifetime; }
    const IDescriptor* GetDescriptor() const override { return descriptor.get(); }
    
    // Buffer-specific overrides
    VkBuffer GetBuffer() const override { return buffer; }
    void SetBuffer(VkBuffer buf) override { buffer = buf; }
    size_t GetMemorySize() const override { return memorySize; }
    
    void SetOwningNode(NodeInstance* node) override { owningNode = node; }
    NodeInstance* GetOwningNode() const override { return owningNode; }
    void SetDeviceDependency(Vixen::Vulkan::Resources::VulkanDevice* dev) override { deviceDependency = dev; }
    Vixen::Vulkan::Resources::VulkanDevice* GetDeviceDependency() const override { return deviceDependency; }
    bool IsAllocated() const override { return buffer != VK_NULL_HANDLE; }
    bool IsValid() const override { return IsAllocated(); }

private:
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    size_t memorySize = 0;
    std::unique_ptr<BufferDescriptor> descriptor;
    ResourceLifetime lifetime = ResourceLifetime::Transient;
    NodeInstance* owningNode = nullptr;
    Vixen::Vulkan::Resources::VulkanDevice* deviceDependency = nullptr;
    
    uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
};

/**
 * @brief Specialized CommandPool resource
 */
template<>
class Resource<VkCommandPool> : public IResource {
public:
    Resource() = default;
    
    Resource(ResourceLifetime lifetime, const CommandPoolDescriptor& desc)
        : lifetime(lifetime), descriptor(std::make_unique<CommandPoolDescriptor>(desc)) {}
    
    ~Resource() = default;

    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
    Resource(Resource&&) noexcept = default;
    Resource& operator=(Resource&&) noexcept = default;

    // Access
    VkCommandPool& Get() { return commandPool; }
    const VkCommandPool& Get() const { return commandPool; }
    void Set(VkCommandPool pool) { commandPool = pool; }
    
    CommandPoolDescriptor* GetDescriptor() { return descriptor.get(); }
    const CommandPoolDescriptor* GetDescriptor() const { return descriptor.get(); }

    // IResource interface
    ResourceType GetType() const override { return ResourceType::Buffer; } // TODO: Add CommandPool to ResourceType enum
    ResourceLifetime GetLifetime() const override { return lifetime; }
    const IDescriptor* GetDescriptor() const override { return descriptor.get(); }
    
    // CommandPool-specific overrides
    VkCommandPool GetCommandPool() const override { return commandPool; }
    void SetCommandPool(VkCommandPool pool) override { commandPool = pool; }
    size_t GetMemorySize() const override { return 0; } // Command pools don't have direct memory size
    
    void SetOwningNode(NodeInstance* node) override { owningNode = node; }
    NodeInstance* GetOwningNode() const override { return owningNode; }
    void SetDeviceDependency(Vixen::Vulkan::Resources::VulkanDevice* dev) override { deviceDependency = dev; }
    Vixen::Vulkan::Resources::VulkanDevice* GetDeviceDependency() const override { return deviceDependency; }
    bool IsAllocated() const override { return commandPool != VK_NULL_HANDLE; }
    bool IsValid() const override { return IsAllocated(); }

private:
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::unique_ptr<CommandPoolDescriptor> descriptor;
    ResourceLifetime lifetime = ResourceLifetime::Transient;
    NodeInstance* owningNode = nullptr;
    Vixen::Vulkan::Resources::VulkanDevice* deviceDependency = nullptr;
};

// ============================================================================
// RESOURCE SCHEMA & HANDLES
// ============================================================================

/**
 * @brief Resource descriptor (schema definition)
 * Used by NodeType to describe expected inputs/outputs
 */
struct ResourceDescriptor {
    std::string name;
    ResourceType type;
    ResourceLifetime lifetime;
    std::unique_ptr<IDescriptor> descriptor;
    bool optional = false;

    ResourceDescriptor() = default;

    ResourceDescriptor(const ResourceDescriptor& other)
        : name(other.name), type(other.type), lifetime(other.lifetime), optional(other.optional) {
        if (other.descriptor) descriptor = other.descriptor->Clone();
    }

    ResourceDescriptor& operator=(const ResourceDescriptor& other) {
        if (this == &other) return *this;
        name = other.name;
        type = other.type;
        lifetime = other.lifetime;
        optional = other.optional;
        if (other.descriptor) descriptor = other.descriptor->Clone();
        else descriptor.reset();
        return *this;
    }

    ResourceDescriptor(ResourceDescriptor&&) noexcept = default;
    ResourceDescriptor& operator=(ResourceDescriptor&&) noexcept = default;

    template<typename DescType>
    ResourceDescriptor(
        const std::string& n,
        ResourceType t,
        ResourceLifetime l,
        const DescType& desc,
        bool opt = false
    ) : name(n), type(t), lifetime(l), descriptor(std::make_unique<DescType>(desc)), optional(opt) {}

    ResourceDescriptor(
        const std::string& n,
        ResourceType t,
        ResourceLifetime l,
        std::unique_ptr<IDescriptor> descPtr,
        bool opt = false
    ) : name(n), type(t), lifetime(l), descriptor(std::move(descPtr)), optional(opt) {}
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
