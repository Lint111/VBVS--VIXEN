#pragma once

// Enable new variant-based resource system (disables legacy Resource.h)
#define USE_RESOURCE_VARIANT_SYSTEM

#include "Headers.h"
#include "Data/Core/ResourceTypes.h"
#include <variant>
#include <optional>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include "Data/VariantDescriptors.h"
#include "ResourceTypeTraits.h"  // NEW: Enhanced type trait system
// TEMPORARILY REMOVED - ShaderManagement integration incomplete
// #include "ShaderManagement/ShaderProgram.h"

// Global namespace forward declarations
struct SwapChainPublicVariables;
struct SwapChainBuffer;  // For field extraction support
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
using LoopReferencePtr = const Vixen::RenderGraph::LoopReference*;  // Phase 0.4
using BoolOpEnum = Vixen::RenderGraph::BoolOp;  // Phase 0.4

// Workaround for std::vector<bool> which has special semantics that break std::variant
// Use a wrapper with proper copy semantics
struct BoolVector {
    std::vector<bool> data;
    
    BoolVector() = default;
    BoolVector(const BoolVector& other) : data(other.data) {}
    BoolVector(BoolVector&& other) noexcept : data(std::move(other.data)) {}
    BoolVector& operator=(const BoolVector& other) {
        if (this != &other) data = other.data;
        return *this;
    }
    BoolVector& operator=(BoolVector&& other) noexcept {
        if (this != &other) data = std::move(other.data);
        return *this;
    }
    
    // Implicit conversion from std::vector<bool>
    BoolVector(const std::vector<bool>& v) : data(v) {}
    BoolVector(std::vector<bool>&& v) : data(std::move(v)) {}
    
    // Implicit conversion to std::vector<bool>
    operator std::vector<bool>&() { return data; }
    operator const std::vector<bool>&() const { return data; }
    
    // Convenience methods
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }
    size_t size() const { return data.size(); }
    bool empty() const { return data.empty(); }
    bool operator[](size_t i) const { return data[i]; }
    auto operator[](size_t i) { return data[i]; }
};

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
// PLATFORM-SPECIFIC TYPE COMPATIBILITY
// ============================================================================

// Windows-specific types: Define distinct placeholders on non-Windows platforms
// This allows ResourceVariant to compile cross-platform even though these
// types are only meaningful on Windows
// NOTE: Must be distinct types to avoid std::variant duplicate type errors
#ifndef _WIN32
struct HWND_Placeholder { void* ptr; };       // Window handle (Windows only)
struct HINSTANCE_Placeholder { void* ptr; };  // Instance handle (Windows only)
using HWND = HWND_Placeholder*;
using HINSTANCE = HINSTANCE_Placeholder*;
#endif

// ============================================================================
// SINGLE SOURCE OF TRUTH: RESOURCE TYPE REGISTRY
// ============================================================================

/**
 * @brief Master list of base resource types
 *
 * Format: RESOURCE_TYPE(HandleType, DescriptorType, ResourceTypeEnum)
 * - HandleType: The Vulkan/C++ type stored in the resource
 * - DescriptorType: The descriptor class (or HandleDescriptor for simple types)
 * - ResourceTypeEnum: The ResourceType enum value
 *
 * To add a new type, add ONE line here. Container variants (std::vector<T>) auto-generate.
 *
 * Example:
 * RESOURCE_TYPE(VkImage, ImageDescriptor, ResourceType::Image)
 * → Auto-generates: VkImage, std::vector<VkImage>
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
    RESOURCE_TYPE(LoopReferencePtr,                HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE(BoolOpEnum,                      HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE_BOOL_ONLY(bool,                  HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE_NO_VECTOR(BoolVector,            HandleDescriptor,      ResourceType::Buffer) \
    RESOURCE_TYPE_LAST(VkBufferView,               HandleDescriptor,      ResourceType::Buffer)

// NOTE: VkSemaphoreArrayPtr and legacy vector typedefs removed - use std::vector<T> auto-generation instead
// NOTE: Phase G.2 storage/3D images handled via VkImage + StorageImageDescriptor/Texture3DDescriptor

// ============================================================================
// AUTO-GENERATED TEMPLATE WRAPPER HELPERS
// ============================================================================

/**
 * @brief Helper macros to expand wrappers for each base type
 * Generates all combinations: BaseType + std::vector<BaseType>
 * 
 * Special case: bool and BoolVector don't get auto-vectorized
 * - bool → BoolVector (not std::vector<bool> due to vector<bool> specialization)
 * - BoolVector → no vector wrapper needed
 */

// Generate base type + vector wrapper (for most types)
#define EXPAND_WITH_WRAPPERS(HandleType) \
    HandleType, \
    std::vector<HandleType>,

// For bool: base type only (BoolVector is separately registered)
#define EXPAND_BOOL_ONLY(HandleType) \
    HandleType,

// For BoolVector: no vector wrapper
#define EXPAND_NO_VECTOR(HandleType) \
    HandleType,

// For the last entry (no trailing comma)
#define EXPAND_WITH_WRAPPERS_LAST(HandleType) \
    HandleType, \
    std::vector<HandleType>
#define EXPAND_WITH_WRAPPERS_LAST(HandleType) \
    HandleType, \
    std::vector<HandleType>

// ============================================================================
// AUTO-GENERATED VARIANTS
// ============================================================================

/**
 * @brief Variant holding all possible resource handle types + auto-generated wrappers
 * 
 * For each type T in RESOURCE_TYPE_REGISTRY, generates:
 * - T (base type)
 * - std::vector<T> (array wrapper)
 * 
 * NOTE: std::vector<ResourceVariant> is NOT in this variant (C++ doesn't allow recursive types).
 * It's handled as a meta-type through ResourceTypeTraits validation (IsResourceVariantContainer_v).
 * 
 * To add more wrappers (e.g., T*, std::shared_ptr<T>), modify EXPAND_WITH_WRAPPERS macro above.
 */
using ResourceVariant = std::variant<
    std::monostate,  // Empty/uninitialized
#define RESOURCE_TYPE(HandleType, DescriptorType, ResType) EXPAND_WITH_WRAPPERS(HandleType)
#define RESOURCE_TYPE_BOOL_ONLY(HandleType, DescriptorType, ResType) EXPAND_BOOL_ONLY(HandleType)
#define RESOURCE_TYPE_NO_VECTOR(HandleType, DescriptorType, ResType) EXPAND_NO_VECTOR(HandleType)
#define RESOURCE_TYPE_LAST(HandleType, DescriptorType, ResType) EXPAND_WITH_WRAPPERS_LAST(HandleType)
    RESOURCE_TYPE_REGISTRY
#undef RESOURCE_TYPE
#undef RESOURCE_TYPE_BOOL_ONLY
#undef RESOURCE_TYPE_NO_VECTOR
#undef RESOURCE_TYPE_LAST
>;

// Note: ResourceDescriptorVariant is defined in Data/VariantDescriptors.h

// ============================================================================
// VARIANT TYPE CHECKING HELPERS
// ============================================================================

/**
 * @brief Check if T is the macro-generated ResourceVariant
 */
template<typename T>
struct IsResourceVariant : std::false_type {};

template<>
struct IsResourceVariant<ResourceVariant> : std::true_type {};

template<typename T>
inline constexpr bool IsResourceVariant_v = IsResourceVariant<T>::value;

/**
 * @brief Check if T is a container of ResourceVariant
 *
 * Accepts:
 * - vector<ResourceVariant>
 * - array<ResourceVariant, N>
 * - ResourceVariant[] (C-style arrays)
 */
template<typename T>
inline constexpr bool IsResourceVariantContainer_v =
    StripContainer<T>::isContainer &&
    IsResourceVariant_v<typename StripContainer<T>::Type>;

/**
 * @brief Check if T is ResourceVariant in any form (scalar or container)
 */
template<typename T>
inline constexpr bool IsAnyResourceVariant_v =
    IsResourceVariant_v<T> || IsResourceVariantContainer_v<T>;

// ============================================================================
// AUTO-GENERATED TYPE TRAITS (BASE IMPLEMENTATION)
// ============================================================================

/**
 * @brief Internal base implementation - direct type registration only
 *
 * This is the raw auto-generated traits for types in RESOURCE_TYPE_REGISTRY.
 * Use ResourceTypeTraits<T> instead, which adds array/variant support.
 */
template<typename T>
struct ResourceTypeTraitsImpl {
    using DescriptorT = HandleDescriptor;
    static constexpr ResourceType resourceType = ResourceType::Buffer;
    static constexpr bool isValid = false;
};

/**
 * @brief Specialized type traits for base types
 * Auto-generated from RESOURCE_TYPE_REGISTRY
 */
#define RESOURCE_TYPE(HandleType, DescriptorType, ResType) \
    template<> struct ResourceTypeTraitsImpl<HandleType> { \
        using DescriptorT = DescriptorType; \
        static constexpr ResourceType resourceType = ResType; \
        static constexpr bool isValid = true; \
    };
#define RESOURCE_TYPE_BOOL_ONLY(HandleType, DescriptorType, ResType) \
    template<> struct ResourceTypeTraitsImpl<HandleType> { \
        using DescriptorT = DescriptorType; \
        static constexpr ResourceType resourceType = ResType; \
        static constexpr bool isValid = true; \
    };
#define RESOURCE_TYPE_NO_VECTOR(HandleType, DescriptorType, ResType) \
    template<> struct ResourceTypeTraitsImpl<HandleType> { \
        using DescriptorT = DescriptorType; \
        static constexpr ResourceType resourceType = ResType; \
        static constexpr bool isValid = true; \
    };
#define RESOURCE_TYPE_LAST(HandleType, DescriptorType, ResType) \
    template<> struct ResourceTypeTraitsImpl<HandleType> { \
        using DescriptorT = DescriptorType; \
        static constexpr ResourceType resourceType = ResType; \
        static constexpr bool isValid = true; \
    };
RESOURCE_TYPE_REGISTRY
#undef RESOURCE_TYPE
#undef RESOURCE_TYPE_BOOL_ONLY
#undef RESOURCE_TYPE_NO_VECTOR
#undef RESOURCE_TYPE_LAST

// Forward declaration to avoid circular dependency
class Resource;

/**
 * @brief Explicit registration for ResourceVariant itself
 *
 * This makes the variant type itself a valid slot type!
 * Slots typed as ResourceVariant accept ANY registered type.
 */
template<>
struct ResourceTypeTraitsImpl<ResourceVariant> {
    using DescriptorT = HandleDescriptor;
    static constexpr ResourceType resourceType = ResourceType::Buffer;  // Generic fallback
    static constexpr bool isValid = true;
    static constexpr bool isVariantType = true;
};

// ============================================================================
// ENHANCED RESOURCE TYPE TRAITS (Array & Variant Support)
// ============================================================================

/**
 * @brief Enhanced type traits with automatic array/vector support
 *
 * KEY FEATURES:
 * 1. If T is registered, then vector<T> and array<T, N> are also valid!
 * 2. ResourceVariant (the macro-generated variant) is valid!
 * 3. vector<ResourceVariant> and array<ResourceVariant, N> are valid!
 * 4. Custom variants (std::variant<...>) valid if ALL element types are registered!
 *
 * Note: ResourceVariant already includes vector<T> for all registered types T,
 * but this system also validates array<T, N> and custom variants.
 *
 * Validation logic:
 * 1. Check if T is directly registered (ResourceTypeTraitsImpl<T>::isValid)
 * 2. If not, check if T is a container (vector/array)
 *    - If yes, unwrap and check if base type is registered
 * 3. Check if T is ResourceVariant (using helper)
 * 4. Check if T is a container of ResourceVariant (using helper)
 * 5. Check if T is a custom variant with all registered types (NEW!)
 *
 * Examples:
 * - VkImage: Registered directly → isValid = true ✅
 * - vector<VkImage>: Registered directly (auto-generated in variant) → isValid = true ✅
 * - array<VkImage, 10>: Not registered, but VkImage is → isValid = true ✅
 * - ResourceVariant: Special variant type → isValid = true ✅
 * - vector<ResourceVariant>: Container of variant → isValid = true ✅
 * - variant<VkImage, VkBuffer>: Custom variant, all types registered → isValid = true ✅
 * - variant<VkImage, UnknownType>: Custom variant, UnknownType not registered → isValid = false ❌
 * - vector<UnknownType>: Not registered, UnknownType not registered → isValid = false ❌
 */
template<typename T>
struct ResourceTypeTraits {
    using BaseType = typename StripContainer<T>::Type;

    // Check if T is a custom variant (not ResourceVariant) with all types registered
    static constexpr bool isCustomVariant =
        IsVariant_v<T> &&
        !IsResourceVariant_v<T> &&
        AllVariantTypesRegistered_v<T>;

    // Check if T is a container of custom variant
    static constexpr bool isCustomVariantContainer =
        StripContainer<T>::isContainer &&
        IsVariant_v<BaseType> &&
        !IsResourceVariant_v<BaseType> &&
        AllVariantTypesRegistered_v<BaseType>;

    // Validation using helpers
    static constexpr bool isValid =
        ResourceTypeTraitsImpl<T>::isValid ||           // Direct registration
        (StripContainer<T>::isContainer &&
         ResourceTypeTraitsImpl<BaseType>::isValid) ||  // Container of registered type
        IsResourceVariant_v<T> ||                       // ResourceVariant itself
        IsResourceVariantContainer_v<T> ||              // Container of ResourceVariant
        isCustomVariant ||                              // Custom variant (NEW!)
        isCustomVariantContainer;                       // Container of custom variant (NEW!)

    // Variant type checks (using helpers)
    static constexpr bool isVariantType = IsResourceVariant_v<T>;
    static constexpr bool isVariantContainer = IsResourceVariantContainer_v<T>;
    static constexpr bool isAnyVariant = IsAnyResourceVariant_v<T>;

    // Custom variant checks (NEW!)
    // These are exposed so users can distinguish between:
    // - ResourceVariant (full variant)
    // - Custom variants (type-safe subsets)
    static constexpr bool isResourceVariant = IsResourceVariant_v<T>;

    // Use base type's descriptor and resource type (fallback for variant)
    using DescriptorT = typename std::conditional_t<
        isAnyVariant,
        std::type_identity<HandleDescriptor>,
        std::type_identity<typename ResourceTypeTraitsImpl<BaseType>::DescriptorT>
    >::type;

    static constexpr ResourceType resourceType =
        isAnyVariant ? ResourceType::Buffer : ResourceTypeTraitsImpl<BaseType>::resourceType;

    // Container metadata
    static constexpr bool isContainer = StripContainer<T>::isContainer;
    static constexpr bool isVector = StripContainer<T>::isVector;
    static constexpr bool isArray = StripContainer<T>::isArray;
    static constexpr size_t arraySize = StripContainer<T>::arraySize;
};

// ============================================================================
// TYPE INITIALIZER VISITOR (Runtime variant initialization)
// ============================================================================

// Visitor pattern: Try to cast descriptor and initialize handle for matching type
struct TypeInitializer {
    ResourceType targetType;
    ResourceDescriptorBase* descriptor;
    ResourceVariant& handle;
    ResourceDescriptorVariant& descVariant;
    bool success = false;

    // Generic handler for all types
    template<typename HandleType>
    void operator()(const HandleType&) {
        using Traits = ResourceTypeTraits<HandleType>;

        // Skip monostate (uninitialized variant state)
        if constexpr (std::is_same_v<HandleType, std::monostate>) {
            return;
        }

        // Check if this is the target type
        if (Traits::resourceType != targetType) {
            return;
        }

        // Try to cast descriptor to expected type
        if (auto* typedDesc = dynamic_cast<typename Traits::DescriptorT*>(descriptor)) {
            descVariant = *typedDesc;
            handle = HandleType{};
            success = true;
        }
    }
};

// ============================================================================
// HELPER: MACRO-GENERATED RESOURCE INITIALIZATION
// ============================================================================

/**
 * @brief Initialize resource variant from ResourceType enum
 * 
 * Uses std::visit pattern for type-safe dispatch without macro-generated if-else chains.
 * Each ResourceType maps to its correct handle type and descriptor type.
 */
inline bool InitializeResourceFromType(
    ResourceType type,
    ResourceDescriptorBase* desc,
    ResourceVariant& outHandle,
    ResourceDescriptorVariant& outDescriptor)
{
    
    // Visit all possible types in the variant
    TypeInitializer visitor{type, desc, outHandle, outDescriptor};
    std::visit(visitor, ResourceVariant{});
    
    return visitor.success;
}

// ============================================================================
// UNIFIED RESOURCE CLASS
// ============================================================================

/**
 * @brief Wrapper for ResourceVariant when used as a pass-through value
 *
 * Distinguishes between:
 * - ResourceVariant as a container alternative (e.g., VkImage stored in variant)
 * - ResourceVariant itself as the value (pass-through semantic)
 */
struct VariantHandle {
    ResourceVariant value;

    VariantHandle() = default;
    explicit VariantHandle(ResourceVariant&& v) : value(std::move(v)) {}
};

/**
 * @brief Type-safe resource container using std::variant
 *
 * Eliminates manual type checking and casting by using compile-time type info
 * from slot definitions.
 *
 * Unified storage for three cases:
 * 1. ResourceVariant - Single element from variant alternatives (VkImage, VkBuffer, etc.)
 * 2. VariantHandle - ResourceVariant itself as pass-through value
 * 3. std::vector<VariantHandle> - Vector of pass-through ResourceVariants
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
        res.handleStorage = ResourceVariant{VulkanType{}}; // Initialize with null handle
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
    void SetHandle(VulkanType&& value) {
        static_assert(ResourceTypeTraits<VulkanType>::isValid, "Type not registered");

        if constexpr (std::is_same_v<std::decay_t<VulkanType>, ResourceVariant>) {
            // Case 2: ResourceVariant as a handle (pass-through variant)
            handleStorage = VariantHandle{std::forward<VulkanType>(value)};
        } else if constexpr (std::is_same_v<std::decay_t<VulkanType>, std::vector<ResourceVariant>>) {
            // Case 3: std::vector<ResourceVariant> - convert to vector<VariantHandle>
            std::vector<VariantHandle> wrapped;
            wrapped.reserve(value.size());
            for (auto& v : value) {
                wrapped.emplace_back(std::move(v));
            }
            handleStorage = std::move(wrapped);
        } else {
            // Case 1: Regular registered types (VkImage, VkBuffer, etc.)
            handleStorage = ResourceVariant{std::forward<VulkanType>(value)};
        }
    }

    /**
     * @brief Get handle value (compile-time type-safe)
     */
    template<typename VulkanType>
    VulkanType GetHandle() const {
        static_assert(ResourceTypeTraits<VulkanType>::isValid, "Type not registered");

        if constexpr (std::is_same_v<VulkanType, ResourceVariant>) {
            // Case 2: Get ResourceVariant itself (pass-through)
            if (auto* ptr = std::get_if<VariantHandle>(&handleStorage)) {
                return ptr->value;
            }
            return ResourceVariant{};  // Return monostate variant if not set
        } else if constexpr (std::is_same_v<VulkanType, std::vector<ResourceVariant>>) {
            // Case 3: Get std::vector<ResourceVariant> - unwrap from vector<VariantHandle>
            if (auto* ptr = std::get_if<std::vector<VariantHandle>>(&handleStorage)) {
                std::vector<ResourceVariant> result;
                result.reserve(ptr->size());
                for (const auto& wrapped : *ptr) {
                    result.push_back(wrapped.value);
                }
                return result;
            }
            return std::vector<ResourceVariant>{};  // Return empty vector if not set
        } else {
            // Case 1: Regular registered types - extract from ResourceVariant
            if (auto* variantPtr = std::get_if<ResourceVariant>(&handleStorage)) {
                if (auto* ptr = std::get_if<VulkanType>(variantPtr)) {
                    return *ptr;
                }
            }
            return VulkanType{}; // Return null handle if type mismatch
        }
    }


    /**
     * @brief Check if handle is set
     */
    bool IsValid() const {
        // Check if we have a ResourceVariant stored (case 1)
        if (auto* variantPtr = std::get_if<ResourceVariant>(&handleStorage)) {
            return !std::holds_alternative<std::monostate>(*variantPtr);
        }
        // Cases 2 and 3 (VariantHandle or vector) are always valid if present
        return !std::holds_alternative<std::monostate>(handleStorage);
    }

    /**
     * @brief Get handle as variant (for generic processing)
     *
     * Returns the ResourceVariant for case 1, or empty variant for cases 2/3.
     * For full access to all cases, use GetHandle<T>() with specific type.
     */
    const ResourceVariant& GetHandleVariant() const {
        if (auto* variantPtr = std::get_if<ResourceVariant>(&handleStorage)) {
            return *variantPtr;
        }
        // For cases 2 and 3, return a static empty variant
        static const ResourceVariant emptyVariant;
        return emptyVariant;
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
    ResourceDescriptorVariant descriptor;

    // Unified handle storage for all three cases:
    // 1. ResourceVariant - Regular handle (VkImage, VkBuffer, etc.)
    // 2. VariantHandle - ResourceVariant itself as pass-through value
    // 3. std::vector<VariantHandle> - Vector of pass-through ResourceVariants
    std::variant<
        std::monostate,              // Uninitialized state
        ResourceVariant,             // Case 1: Single element from variant alternatives
        VariantHandle,               // Case 2: ResourceVariant as pass-through
        std::vector<VariantHandle>   // Case 3: Vector of pass-throughs
    > handleStorage;

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
