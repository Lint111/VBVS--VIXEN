#pragma once

// Enable new variant-based resource system (disables legacy Resource.h)
#define USE_RESOURCE_VARIANT_SYSTEM

#include "Headers.h"
#include "ResourceTypes.h"
#include <variant>
#include <optional>
#include <memory>
#include <string>

namespace Vixen::RenderGraph {

// ============================================================================
// BASE DESCRIPTOR CLASS
// ============================================================================

/**
 * @brief Base descriptor for resources
 * Provides validation interface for all resource descriptors
 */
struct ResourceDescriptorBase {
    virtual ~ResourceDescriptorBase() = default;
    virtual bool Validate() const { return true; }
    virtual std::unique_ptr<ResourceDescriptorBase> Clone() const = 0;
};

// ============================================================================
// SPECIFIC DESCRIPTOR TYPES
// ============================================================================

/**
 * @brief Image resource descriptor
 */
struct ImageDescriptor : ResourceDescriptorBase {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    ResourceUsage usage = ResourceUsage::None;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;

    bool Validate() const override {
        return width > 0 && height > 0 && format != VK_FORMAT_UNDEFINED;
    }

    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<ImageDescriptor>(*this);
    }
};

/**
 * @brief Buffer resource descriptor
 */
struct BufferDescriptor : ResourceDescriptorBase {
    VkDeviceSize size = 0;
    ResourceUsage usage = ResourceUsage::None;
    VkMemoryPropertyFlags memoryProperties = 0;

    bool Validate() const override {
        return size > 0;
    }

    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<BufferDescriptor>(*this);
    }
};

/**
 * @brief Simple handle descriptor (for VkSurface, VkSwapchain, etc.)
 */
struct HandleDescriptor : ResourceDescriptorBase {
    std::string handleTypeName; // For debugging

    HandleDescriptor(const std::string& typeName = "GenericHandle")
        : handleTypeName(typeName) {}

    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<HandleDescriptor>(*this);
    }
};

/**
 * @brief Command pool descriptor
 */
struct CommandPoolDescriptor : ResourceDescriptorBase {
    VkCommandPoolCreateFlags flags = 0;
    uint32_t queueFamilyIndex = 0;

    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<CommandPoolDescriptor>(*this);
    }
};

/**
 * @brief Shader program descriptor (pointer to external data)
 */
struct ShaderProgramDescriptor : ResourceDescriptorBase {
    std::string shaderName; // For debugging/identification

    ShaderProgramDescriptor(const std::string& name = "")
        : shaderName(name) {}

    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<ShaderProgramDescriptor>(*this);
    }
};

// ============================================================================
// SINGLE SOURCE OF TRUTH: RESOURCE TYPE REGISTRY
// ============================================================================

/**
 * @brief Master list of all resource types
 * 
 * Format: RESOURCE_TYPE(HandleType, DescriptorType, ResourceTypeEnum)
 * - HandleType: The Vulkan/C++ type stored in the resource
 * - DescriptorType: The descriptor class (or HandleDescriptor for simple types)
 * - ResourceTypeEnum: The ResourceType enum value
 * 
 * To add a new type, add ONE line here. Everything else auto-generates.
 * 
 * Example:
 * RESOURCE_TYPE(VkImage, ImageDescriptor, ResourceType::Image)
 * RESOURCE_TYPE(VkBuffer, BufferDescriptor, ResourceType::Buffer)
 * RESOURCE_TYPE(VkSurface, HandleDescriptor, ResourceType::Image)  // Simple handle
 */
#define RESOURCE_TYPE_REGISTRY \
    RESOURCE_TYPE(VkImage, ImageDescriptor, ResourceType::Image) \
    RESOURCE_TYPE(VkBuffer, BufferDescriptor, ResourceType::Buffer) \
    RESOURCE_TYPE(VkImageView, HandleDescriptor, ResourceType::Image) \
    RESOURCE_TYPE(VkSampler, HandleDescriptor, ResourceType::Buffer) \
    RESOURCE_TYPE(VkSurfaceKHR, HandleDescriptor, ResourceType::Image) \
    RESOURCE_TYPE(VkSwapchainKHR, HandleDescriptor, ResourceType::Buffer) \
    RESOURCE_TYPE(VkRenderPass, HandleDescriptor, ResourceType::Buffer) \
    RESOURCE_TYPE(VkFramebuffer, HandleDescriptor, ResourceType::Buffer) \
    RESOURCE_TYPE(VkDescriptorSetLayout, HandleDescriptor, ResourceType::Buffer) \
    RESOURCE_TYPE(VkDescriptorPool, HandleDescriptor, ResourceType::Buffer) \
    RESOURCE_TYPE(VkDescriptorSet, HandleDescriptor, ResourceType::Buffer) \
    RESOURCE_TYPE(VkCommandPool, CommandPoolDescriptor, ResourceType::Buffer) \
    RESOURCE_TYPE(VkSemaphore, HandleDescriptor, ResourceType::Buffer) \
    RESOURCE_TYPE(VkFence, HandleDescriptor, ResourceType::Buffer) \
    RESOURCE_TYPE_LAST(VkAccelerationStructureKHR, HandleDescriptor, ResourceType::AccelerationStructure)

// ============================================================================
// AUTO-GENERATED VARIANTS
// ============================================================================

/**
 * @brief Variant holding all possible resource handle types
 * Auto-generated from RESOURCE_TYPE_REGISTRY
 */
using ResourceHandleVariant = std::variant<
    std::monostate,  // Empty/uninitialized
#define RESOURCE_TYPE(HandleType, DescriptorType, ResType) HandleType,
#define RESOURCE_TYPE_LAST(HandleType, DescriptorType, ResType) HandleType,
    RESOURCE_TYPE_REGISTRY
#undef RESOURCE_TYPE
#undef RESOURCE_TYPE_LAST
    const ShaderManagement::CompiledProgram*  // Special case: shader program pointer
>;

/**
 * @brief Variant holding all possible resource descriptor types
 * Auto-generated from unique descriptors in RESOURCE_TYPE_REGISTRY
 * 
 * Note: We list each descriptor type once, even if multiple handle types use it
 */
using ResourceDescriptorVariant = std::variant<
    std::monostate,
    ImageDescriptor,
    BufferDescriptor,
    HandleDescriptor,
    CommandPoolDescriptor,
    ShaderProgramDescriptor
>;

// ============================================================================
// AUTO-GENERATED TYPE TRAITS
// ============================================================================

/**
 * @brief Default type traits (for unregistered types)
 */
template<typename T>
struct ResourceTypeTraits {
    using DescriptorT = HandleDescriptor;
    static constexpr ResourceType resourceType = ResourceType::Buffer;
    static constexpr bool isValid = false;
};

/**
 * @brief Specialized type traits for each registered type
 * Auto-generated from RESOURCE_TYPE_REGISTRY
 */
#define RESOURCE_TYPE(HandleType, DescriptorType, ResType) \
    template<> struct ResourceTypeTraits<HandleType> { \
        using DescriptorT = DescriptorType; \
        static constexpr ResourceType resourceType = ResType; \
        static constexpr bool isValid = true; \
    };
#define RESOURCE_TYPE_LAST(HandleType, DescriptorType, ResType) \
    template<> struct ResourceTypeTraits<HandleType> { \
        using DescriptorT = DescriptorType; \
        static constexpr ResourceType resourceType = ResType; \
        static constexpr bool isValid = true; \
    };
RESOURCE_TYPE_REGISTRY
#undef RESOURCE_TYPE
#undef RESOURCE_TYPE_LAST

// Special case for shader program pointers (forward declaration for CompiledProgram)
namespace ShaderManagement { struct CompiledProgram; }
template<> struct ResourceTypeTraits<const ShaderManagement::CompiledProgram*> {
    using DescriptorT = ShaderProgramDescriptor;
    static constexpr ResourceType resourceType = ResourceType::Buffer;
    static constexpr bool isValid = true;
};

// ============================================================================
// HELPER: MACRO-GENERATED RESOURCE INITIALIZATION
// ============================================================================

/**
 * @brief Initialize resource variant from ResourceType enum
 * 
 * Auto-generated dispatch using RESOURCE_TYPE_REGISTRY - no manual switch needed!
 * Each ResourceType maps to its correct handle type and descriptor type.
 */
inline bool InitializeResourceFromType(
    ResourceType type,
    ResourceDescriptorBase* desc,
    ResourceHandleVariant& outHandle,
    ResourceDescriptorVariant& outDescriptor)
{
    // Macro generates if-else chain matching ResourceType to concrete types
    #define RESOURCE_TYPE(HandleType, DescriptorType, ResType) \
        if (type == ResType) { \
            if (auto* typedDesc = dynamic_cast<DescriptorType*>(desc)) { \
                outDescriptor = *typedDesc; \
                outHandle = HandleType{}; \
                return true; \
            } \
        }
    #define RESOURCE_TYPE_LAST(HandleType, DescriptorType, ResType) \
        if (type == ResType) { \
            if (auto* typedDesc = dynamic_cast<DescriptorType*>(desc)) { \
                outDescriptor = *typedDesc; \
                outHandle = HandleType{}; \
                return true; \
            } \
        }
    
    RESOURCE_TYPE_REGISTRY
    
    #undef RESOURCE_TYPE
    #undef RESOURCE_TYPE_LAST
    
    // Unknown type - use default
    return false;
}

// ============================================================================
// UNIFIED RESOURCE CLASS
// ============================================================================

/**
 * @brief Type-safe resource container using std::variant
 * 
 * Eliminates manual type checking and casting by using compile-time type info
 * from slot definitions.
 * 
 * Example usage:
 * ```cpp
 * // Create resource with type-specific descriptor
 * Resource res = Resource::Create<VkImage>(ImageDescriptor{1920, 1080, ...});
 * 
 * // Set handle (type-checked at compile time)
 * res.SetHandle<VkImage>(myImage);
 * 
 * // Get handle (type-checked at compile time)
 * VkImage img = res.GetHandle<VkImage>();
 * ```
 */
class Resource {
public:
    Resource() = default;

    /**
     * @brief Create resource with specific type and descriptor
     */
    template<typename VulkanType>
    static Resource Create(const typename ResourceTypeTraits<VulkanType>::DescriptorT& descriptor) {
        Resource res;
        res.type = ResourceTypeTraits<VulkanType>::resourceType;
        res.descriptor = descriptor;
        res.handle = VulkanType{}; // Initialize with null handle
        return res;
    }

    /**
     * @brief Create resource from ResourceType enum (runtime dispatch)
     */
    static Resource CreateFromType(ResourceType type, std::unique_ptr<ResourceDescriptorBase> desc);

    // Prevent copying (resources are unique)
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;

    // Allow moving
    Resource(Resource&&) noexcept = default;
    Resource& operator=(Resource&&) noexcept = default;

    /**
     * @brief Set handle value (compile-time type-safe)
     */
    template<typename VulkanType>
    void SetHandle(VulkanType value) {
        static_assert(ResourceTypeTraits<VulkanType>::isValid, "Type not registered");
        handle = value;
    }

    /**
     * @brief Get handle value (compile-time type-safe)
     */
    template<typename VulkanType>
    VulkanType GetHandle() const {
        static_assert(ResourceTypeTraits<VulkanType>::isValid, "Type not registered");
        if (auto* ptr = std::get_if<VulkanType>(&handle)) {
            return *ptr;
        }
        return VulkanType{}; // Return null handle if type mismatch
    }

    /**
     * @brief Check if handle is set
     */
    bool IsValid() const {
        return !std::holds_alternative<std::monostate>(handle);
    }

    /**
     * @brief Get descriptor as specific type
     */
    template<typename DescType>
    const DescType* GetDescriptor() const {
        if (auto* ptr = std::get_if<DescType>(&descriptor)) {
            return ptr;
        }
        return nullptr;
    }

    /**
     * @brief Get descriptor (mutable)
     */
    template<typename DescType>
    DescType* GetDescriptorMutable() {
        if (auto* ptr = std::get_if<DescType>(&descriptor)) {
            return ptr;
        }
        return nullptr;
    }

    // Legacy API support (for gradual migration)
    ResourceType GetType() const { return type; }
    ResourceLifetime GetLifetime() const { return lifetime; }
    
    void SetLifetime(ResourceLifetime lt) { lifetime = lt; }

private:
    ResourceType type = ResourceType::Image;
    ResourceLifetime lifetime = ResourceLifetime::Transient;
    ResourceHandleVariant handle;
    ResourceDescriptorVariant descriptor;

    friend class RenderGraph;
};

// ============================================================================
// RESOURCE SCHEMA DESCRIPTOR
// ============================================================================

/**
 * @brief Schema descriptor for node inputs/outputs
 * 
 * Replaces old ResourceDescriptor with variant-based approach
 */
struct ResourceSlotDescriptor {
    std::string name;
    ResourceType type;
    ResourceLifetime lifetime;
    ResourceDescriptorVariant descriptor;
    bool optional = false;

    // Default constructor
    ResourceSlotDescriptor() = default;

    // Construct from specific Vulkan type
    template<typename VulkanType>
    ResourceSlotDescriptor(
        const std::string& n,
        ResourceLifetime lt,
        const typename ResourceTypeTraits<VulkanType>::DescriptorT& desc,
        bool opt = false
    ) : name(n),
        type(ResourceTypeTraits<VulkanType>::resourceType),
        lifetime(lt),
        descriptor(desc),
        optional(opt) {}

    // Construct with explicit ResourceType (for legacy compatibility)
    ResourceSlotDescriptor(
        const std::string& n,
        ResourceType t,
        ResourceLifetime lt,
        ResourceDescriptorVariant desc,
        bool opt = false
    ) : name(n), type(t), lifetime(lt), descriptor(std::move(desc)), optional(opt) {}
};

// ============================================================================
// LEGACY COMPATIBILITY TYPEDEFS
// ============================================================================

// Map old names to new variant-based types for gradual migration
using ResourceDescriptor = ResourceSlotDescriptor;
using ImageDescription = ImageDescriptor;
using BufferDescription = BufferDescriptor;

// Cleanup macro
#undef RESOURCE_TYPE_REGISTRY

} // namespace Vixen::RenderGraph
