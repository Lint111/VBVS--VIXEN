#pragma once

// Enable new variant-based resource system (disables legacy Resource.h)
#define USE_RESOURCE_VARIANT_SYSTEM

#include "Headers.h"
#include "ResourceTypes.h"
#include <variant>
#include <optional>
#include <memory>
#include <string>
#include "Data/VariantDescriptors.h"
// TEMPORARILY REMOVED - ShaderManagement integration incomplete
// #include "ShaderManagement/ShaderProgram.h"

// Global namespace forward declarations
struct SwapChainPublicVariables;
class VulkanShader; // Forward declare for MVP shader approach

// Forward declare ShaderManagement types to avoid hard dependency
namespace ShaderManagement {
    struct CompiledProgram;
    struct ShaderDataBundle;
}

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;  // Forward declare
}

namespace Vixen::RenderGraph {
    struct ShaderProgramDescriptor;  // Forward declare from ShaderLibraryNodeConfig.h
}

// Forward declarations for Phase 0.4 loop system
namespace Vixen::RenderGraph {
    struct LoopReference;  // From LoopManager.h
    enum class BoolOp : uint8_t;  // From BoolOpNodeConfig.h
}

// Type aliases for pointer types (needed for variant registry - macros can't handle namespaces)
using SwapChainPublicVariablesPtr = SwapChainPublicVariables*;
using VulkanShaderPtr = VulkanShader*; // MVP approach
using ShaderProgramPtr = const ShaderManagement::CompiledProgram*;
using ShaderProgramDescriptorPtr = Vixen::RenderGraph::ShaderProgramDescriptor*;
using ShaderDataBundlePtr = std::shared_ptr<ShaderManagement::ShaderDataBundle>;
using VkViewportPtr = VkViewport*;
using VkRect2DPtr = VkRect2D*;
using VkResultPtr = VkResult*;
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;
using FramebufferVector = std::vector<VkFramebuffer>;
using DescriptorSetVector = std::vector<VkDescriptorSet>;
using LoopReferencePtr = const Vixen::RenderGraph::LoopReference*;  // Phase 0.4
using BoolOpEnum = Vixen::RenderGraph::BoolOp;  // Phase 0.4
using BoolVector = std::vector<bool>;  // Phase 0.4
using VkSemaphoreArrayPtr = const VkSemaphore*;  // Phase 0.4: Per-image semaphore arrays
using VkFenceVector = std::vector<VkFence>*;  // Phase 0.7: Fence vector pointer (supports empty() check)

namespace Vixen::RenderGraph {


// ============================================================================
// RESOURCE USAGE OPERATORS (bitwise for flags)
// ============================================================================

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
    RESOURCE_TYPE(VkImage,                         ImageDescriptor,       ResourceType::Image) \
    RESOURCE_TYPE(VkBuffer,                        BufferDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkImageView,                     HandleDescriptor,      ResourceType::Image) \
    RESOURCE_TYPE(VkSampler,                       HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkSurfaceKHR,                    HandleDescriptor,      ResourceType::Image) \
    RESOURCE_TYPE(VkSwapchainKHR,                  HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkRenderPass,                    HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkFramebuffer,                   HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkDescriptorSetLayout,           HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkDescriptorPool,                HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkDescriptorSet,                 HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkCommandPool,                   CommandPoolDescriptor, ResourceType::Buffer) \
    RESOURCE_TYPE(VkSemaphore,                     HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkSemaphoreArrayPtr,             HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkFence,                         HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkDevice,                        HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkPhysicalDevice,                HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkInstance,                      HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VulkanDevicePtr,                 HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VulkanShaderPtr,                 HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkFormat,                        HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(uint32_t,                        HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(uint64_t,                        HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(HWND,                            HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(HINSTANCE,                       HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(SwapChainPublicVariablesPtr,     HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(ShaderProgramPtr,                HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(ShaderProgramDescriptorPtr,      HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(ShaderDataBundlePtr,             HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkPipeline,                      HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkPipelineLayout,                HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkPipelineCache,                 HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkShaderModule,                  HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkCommandBuffer,                 HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkQueue,                         HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkViewportPtr,                   HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkRect2DPtr,                     HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(PFN_vkQueuePresentKHR,           HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(VkResultPtr,                     HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(FramebufferVector,               HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(DescriptorSetVector,             HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(LoopReferencePtr,                HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(BoolOpEnum,                      HandleDescriptor,       ResourceType::Buffer) \
    RESOURCE_TYPE(BoolVector,                      HandleDescriptor,       ResourceType::Buffer) \
    RESOURCE_TYPE(bool,                            HandleDescriptor,       ResourceType::Buffer) \
    RESOURCE_TYPE(VkFenceVector,                   HandleDescriptor,       ResourceType::Buffer) \
    RESOURCE_TYPE_LAST(VkBufferView,               HandleDescriptor,       ResourceType::Buffer)

// NOTE: Phase G.2 storage/3D images handled via VkImage + StorageImageDescriptor/Texture3DDescriptor
// We don't register separate types since VkImage already exists. The descriptor differentiates usage.

// ============================================================================
// AUTO-GENERATED TYPE TRAITS
// ============================================================================

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
#define RESOURCE_TYPE_LAST(HandleType, DescriptorType, ResType) HandleType
    RESOURCE_TYPE_REGISTRY
#undef RESOURCE_TYPE
#undef RESOURCE_TYPE_LAST
>;

// Note: ResourceDescriptorVariant is defined in Data/VariantDescriptors.h

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
