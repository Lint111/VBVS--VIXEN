#pragma once

#define VIXEN_COMPILE_TIME_RESOURCE_SYSTEM_H

/**
 * @file CompileTimeResourceSystem.h
 * @brief Zero-overhead compile-time resource type system
 *
 * Provides compile-time type validation with zero runtime overhead:
 * - Compile-time-only type validation (static_assert)
 * - Zero runtime overhead (type tags disappear after compilation)
 * - Natural C++ syntax (T&, T*, const T&)
 * - Support for shared_ptr<T> with proper ownership semantics
 *
 * This is the final form after Phase H refactoring (ResourceVariant eliminated).
 */

// Vulkan type definitions
#include <vulkan/vulkan.h>

#include "ResourceTypes.h"
#include "ResourceTypeTraits.h"  // Include for StripContainer
#include "Data/VariantDescriptors.h"
#include "Debug/DescriptorResourceTracker.h"  // Debug tracking for resources
#include <variant>
#include <type_traits>
#include <cstdint>
#include <any>
#include <cassert>
#include <memory>  // For std::shared_ptr
#include <functional>  // For std::function (wrapper extraction)
#include "BoolVector.h"  // Include BoolVector wrapper

// Forward declarations (same as PassThroughStorage.h)
struct SwapChainPublicVariables;
struct SwapChainBuffer;

namespace Vixen::RenderGraph::Data {
    struct BoolVector;
}

namespace ShaderManagement {
    struct CompiledProgram;
    struct ShaderDataBundle;
}

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::RenderGraph {
    struct ShaderProgramDescriptor;
    struct CameraData;
    struct LoopReference;
    enum class BoolOp : uint8_t;
    enum class SlotRole : uint8_t;
    struct InputState;

    // Type alias for convenience
    using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// ============================================================================
// COMPILE-TIME TYPE REGISTRY
// ============================================================================

template<typename T>
struct IsRegisteredType : std::false_type {};

#define REGISTER_COMPILE_TIME_TYPE(Type) \
    template<> struct IsRegisteredType<Type> : std::true_type {}

// Register Vulkan handle types
REGISTER_COMPILE_TIME_TYPE(VkImage);
REGISTER_COMPILE_TIME_TYPE(VkBuffer);
REGISTER_COMPILE_TIME_TYPE(VkImageView);
REGISTER_COMPILE_TIME_TYPE(VkSampler);
REGISTER_COMPILE_TIME_TYPE(VkSurfaceKHR);
REGISTER_COMPILE_TIME_TYPE(VkSwapchainKHR);
REGISTER_COMPILE_TIME_TYPE(VkRenderPass);
REGISTER_COMPILE_TIME_TYPE(VkFramebuffer);
REGISTER_COMPILE_TIME_TYPE(VkDescriptorSetLayout);
REGISTER_COMPILE_TIME_TYPE(VkDescriptorPool);
REGISTER_COMPILE_TIME_TYPE(VkDescriptorSet);
REGISTER_COMPILE_TIME_TYPE(VkCommandPool);
REGISTER_COMPILE_TIME_TYPE(VkSemaphore);
REGISTER_COMPILE_TIME_TYPE(VkFence);
REGISTER_COMPILE_TIME_TYPE(VkDevice);
REGISTER_COMPILE_TIME_TYPE(VkPhysicalDevice);
REGISTER_COMPILE_TIME_TYPE(VkInstance);
REGISTER_COMPILE_TIME_TYPE(VkPipeline);
REGISTER_COMPILE_TIME_TYPE(VkPipelineLayout);
REGISTER_COMPILE_TIME_TYPE(VkPipelineCache);
REGISTER_COMPILE_TIME_TYPE(VkShaderModule);
REGISTER_COMPILE_TIME_TYPE(VkCommandBuffer);
REGISTER_COMPILE_TIME_TYPE(VkQueue);
REGISTER_COMPILE_TIME_TYPE(VkBufferView);
REGISTER_COMPILE_TIME_TYPE(VkAccelerationStructureKHR);
REGISTER_COMPILE_TIME_TYPE(VkFormat);
REGISTER_COMPILE_TIME_TYPE(VkPushConstantRange);
REGISTER_COMPILE_TIME_TYPE(VkViewport);
REGISTER_COMPILE_TIME_TYPE(VkRect2D);
REGISTER_COMPILE_TIME_TYPE(VkResult);

// Register basic types
REGISTER_COMPILE_TIME_TYPE(uint32_t);
REGISTER_COMPILE_TIME_TYPE(uint64_t);
REGISTER_COMPILE_TIME_TYPE(uint8_t);
REGISTER_COMPILE_TIME_TYPE(int32_t);
REGISTER_COMPILE_TIME_TYPE(float);
REGISTER_COMPILE_TIME_TYPE(double);
REGISTER_COMPILE_TIME_TYPE(glm::vec2);
REGISTER_COMPILE_TIME_TYPE(glm::vec3);
REGISTER_COMPILE_TIME_TYPE(glm::vec4);
REGISTER_COMPILE_TIME_TYPE(glm::ivec2);
REGISTER_COMPILE_TIME_TYPE(glm::ivec3);
REGISTER_COMPILE_TIME_TYPE(glm::ivec4);
REGISTER_COMPILE_TIME_TYPE(glm::uvec2);
REGISTER_COMPILE_TIME_TYPE(glm::uvec3);
REGISTER_COMPILE_TIME_TYPE(glm::uvec4);
REGISTER_COMPILE_TIME_TYPE(glm::mat2);
REGISTER_COMPILE_TIME_TYPE(glm::mat3);
REGISTER_COMPILE_TIME_TYPE(glm::mat4);
REGISTER_COMPILE_TIME_TYPE(glm::mat2x3);
REGISTER_COMPILE_TIME_TYPE(glm::mat2x4);
REGISTER_COMPILE_TIME_TYPE(glm::mat3x2);
REGISTER_COMPILE_TIME_TYPE(glm::mat3x4);
REGISTER_COMPILE_TIME_TYPE(glm::mat4x2);
REGISTER_COMPILE_TIME_TYPE(glm::mat4x3);
REGISTER_COMPILE_TIME_TYPE(bool);
REGISTER_COMPILE_TIME_TYPE(PFN_vkQueuePresentKHR);
REGISTER_COMPILE_TIME_TYPE(Vixen::RenderGraph::Data::BoolVector);

// Forward declare types defined later in this file
struct ImageSamplerPair;  // Full definition below, after descriptor variant section
using DescriptorHandleVariant = std::variant<
    std::monostate, VkImageView, VkBuffer, VkBufferView, VkSampler, VkImage,
    VkAccelerationStructureKHR, ImageSamplerPair, SwapChainPublicVariables*,
    std::vector<VkImageView>, std::vector<VkBuffer>, std::vector<VkBufferView>,
    std::vector<VkSampler>, std::vector<VkAccelerationStructureKHR>
>;  // Forward declaration - full definition with rationale below

}  // Close namespace Vixen::RenderGraph to register types from other namespaces

// Forward declarations for types in other namespaces
namespace ShaderManagement {
    struct ShaderDataBundle;
    struct CompiledProgram;
}

// Register types from other namespaces (must be at global scope relative to IsRegisteredType)
template<> struct Vixen::RenderGraph::IsRegisteredType<::ShaderManagement::ShaderDataBundle> : std::true_type {};
template<> struct Vixen::RenderGraph::IsRegisteredType<::ShaderManagement::CompiledProgram> : std::true_type {};

namespace Vixen::RenderGraph {  // Reopen namespace

// Register application types
REGISTER_COMPILE_TIME_TYPE(CameraData);
REGISTER_COMPILE_TIME_TYPE(SwapChainPublicVariables);
REGISTER_COMPILE_TIME_TYPE(SwapChainBuffer);
REGISTER_COMPILE_TIME_TYPE(Vixen::Vulkan::Resources::VulkanDevice);
REGISTER_COMPILE_TIME_TYPE(ShaderProgramDescriptor);
REGISTER_COMPILE_TIME_TYPE(LoopReference);
REGISTER_COMPILE_TIME_TYPE(BoolOp);
REGISTER_COMPILE_TIME_TYPE(SlotRole);
REGISTER_COMPILE_TIME_TYPE(InputState);

// NOTE: ImageSamplerPair, DescriptorHandleVariant, and PassThroughStorage
// are registered later after their definitions

// Platform-specific types
#ifdef _WIN32
REGISTER_COMPILE_TIME_TYPE(HWND);
REGISTER_COMPILE_TIME_TYPE(HINSTANCE);
#endif

// ============================================================================
// COMPILE-TIME TYPE TAGS (Zero-size markers)
// ============================================================================

template<typename T> struct ValueTag { using storage_type = T; };
template<typename T> struct RefTag { using storage_type = T*; };
template<typename T> struct PtrTag { using storage_type = T*; };
template<typename T> struct ConstRefTag { using storage_type = const T*; };
template<typename T> struct ConstPtrTag { using storage_type = const T*; };

// Map C++ type to compile-time tag
template<typename T>
struct TypeToTag {
    using Bare = std::remove_cv_t<std::remove_reference_t<T>>;
    static constexpr bool is_lvalue_ref = std::is_lvalue_reference_v<T>;
    static constexpr bool is_pointer = std::is_pointer_v<Bare>;
    static constexpr bool is_const = std::is_const_v<std::remove_reference_t<T>>;
    using PointeeType = std::conditional_t<is_pointer, std::remove_pointer_t<Bare>, Bare>;
    static constexpr bool is_const_pointee = is_pointer && std::is_const_v<PointeeType>;
    using BaseType = std::remove_cv_t<PointeeType>;

    using type =
        std::conditional_t<is_pointer && is_const_pointee, ConstPtrTag<BaseType>,
        std::conditional_t<is_pointer, PtrTag<BaseType>,
        std::conditional_t<is_lvalue_ref && is_const, ConstRefTag<BaseType>,
        std::conditional_t<is_lvalue_ref, RefTag<BaseType>,
        ValueTag<Bare>>>>>;
};

template<typename T>
using TypeToTag_t = typename TypeToTag<T>::type;

// ============================================================================
// CONVERSION TYPE DETECTION
// ============================================================================

/**
 * @brief Concept to detect if a type declares a conversion_type typedef
 *
 * Wrapper types that convert to registered types should declare:
 *   using conversion_type = VkBuffer;  // or VkImage, etc.
 *
 * This enables the type system to recursively validate the conversion target
 * without requiring explicit registration of every wrapper type.
 *
 * Example:
 * @code
 * class ShaderCountersBuffer : public IDebugBuffer {
 * public:
 *     using conversion_type = VkBuffer;  // Declares: "I wrap a VkBuffer"
 *     operator VkBuffer() const { return buffer_; }
 * };
 * @endcode
 */
/**
 * @brief Extract the conversion target type from a wrapper
 *
 * Primary template returns void (no conversion).
 * Specialization extracts T::conversion_type when available.
 */
template<typename T, typename = void>
struct ConversionTypeOf { using type = void; };

template<typename T>
struct ConversionTypeOf<T, std::void_t<typename T::conversion_type>> {
    using type = typename T::conversion_type;
};

// Type trait for checking if a type has conversion_type
// Derived from ConversionTypeOf to ensure consistent behavior
template<typename T>
inline constexpr bool HasConversionType_v = !std::is_same_v<typename ConversionTypeOf<T>::type, void>;

// Keep concept for backward compatibility
template<typename T>
concept HasConversionType = HasConversionType_v<T>;

template<typename T>
using ConversionTypeOf_t = typename ConversionTypeOf<T>::type;

// ============================================================================
// INCOMPLETE TYPE DETECTION (SFINAE GUARD)
// ============================================================================
/**
 * @brief Detect if a type is complete (fully defined) vs incomplete (forward-declared)
 *
 * CRITICAL: This is used to catch the case where a wrapper type with conversion_type
 * is forward-declared instead of fully included. When forward-declared:
 * - HasConversionType_v<T> silently returns false (SFINAE selects primary template)
 * - descriptorExtractor_ is NOT captured
 * - GetDescriptorHandle() returns stale handles → validation errors
 *
 * See: HacknPlan #61 - 10 hours debugging due to this silent failure
 *
 * Usage: static_assert(IsCompleteType_v<T>, "Type must be complete (include full header, not forward declaration)");
 */
template<typename T, typename = void>
struct IsCompleteType : std::false_type {};

template<typename T>
struct IsCompleteType<T, std::void_t<decltype(sizeof(T))>> : std::true_type {};

template<typename T>
inline constexpr bool IsCompleteType_v = IsCompleteType<T>::value;

/**
 * @brief Check if a class type LOOKS like it should be a wrapper (heuristic)
 *
 * This is a heuristic to help catch misuse. A type is "wrapper-like" if:
 * - It's a class/struct type
 * - It's NOT a Vulkan handle (VkBuffer, etc.)
 * - It's NOT a fundamental type
 * - It's being passed as a pointer (T*)
 *
 * If a wrapper-like incomplete type is passed, we emit a compile error suggesting
 * the user check their includes.
 */
template<typename T>
inline constexpr bool IsVulkanHandle_v =
    std::is_same_v<T, VkBuffer> ||
    std::is_same_v<T, VkImageView> ||
    std::is_same_v<T, VkImage> ||
    std::is_same_v<T, VkSampler> ||
    std::is_same_v<T, VkBufferView> ||
    std::is_same_v<T, VkCommandPool> ||
    std::is_same_v<T, VkCommandBuffer> ||
    std::is_same_v<T, VkDescriptorSet> ||
    std::is_same_v<T, VkDescriptorPool> ||
    std::is_same_v<T, VkDescriptorSetLayout> ||
    std::is_same_v<T, VkPipeline> ||
    std::is_same_v<T, VkPipelineLayout> ||
    std::is_same_v<T, VkPipelineCache> ||
    std::is_same_v<T, VkRenderPass> ||
    std::is_same_v<T, VkFramebuffer> ||
    std::is_same_v<T, VkShaderModule> ||
    std::is_same_v<T, VkFence> ||
    std::is_same_v<T, VkSemaphore> ||
    std::is_same_v<T, VkEvent> ||
    std::is_same_v<T, VkQueryPool> ||
    std::is_same_v<T, VkDeviceMemory> ||
    std::is_same_v<T, VkInstance> ||
    std::is_same_v<T, VkPhysicalDevice> ||
    std::is_same_v<T, VkDevice> ||
    std::is_same_v<T, VkQueue> ||
    std::is_same_v<T, VkSurfaceKHR> ||
    std::is_same_v<T, VkSwapchainKHR> ||
    std::is_same_v<T, VkAccelerationStructureKHR>;

template<typename T>
inline constexpr bool IsWrapperLikeType_v =
    std::is_class_v<T> &&
    !IsVulkanHandle_v<T> &&
    !std::is_fundamental_v<T>;

// ============================================================================
// RECURSIVE COMPILE-TIME VALIDATION
// ============================================================================

// Recursive compile-time validation
// Handles: direct types, pointers, vectors, variants, conversions, and nested combinations
template<typename T>
struct IsValidTypeImpl {
    using Bare = std::remove_cv_t<std::remove_reference_t<T>>;
    using Depointer = std::remove_pointer_t<Bare>;
    using Clean = std::remove_cv_t<Depointer>;

    // Check if it's a std::vector<U>
    template<typename U>
    static constexpr bool is_vector_helper(const std::vector<U>*) { return true; }
    static constexpr bool is_vector_helper(...) { return false; }
    static constexpr bool is_vector = is_vector_helper(static_cast<Clean*>(nullptr));

    // Check if it's a std::array<U, N>
    template<typename U, std::size_t N>
    static constexpr bool is_array_helper(const std::array<U, N>*) { return true; }
    static constexpr bool is_array_helper(...) { return false; }
    static constexpr bool is_array = is_array_helper(static_cast<Clean*>(nullptr));

    // Check if it's a std::variant<Us...>
    template<typename... Us>
    static constexpr bool is_variant_helper(const std::variant<Us...>*) { return true; }
    static constexpr bool is_variant_helper(...) { return false; }
    static constexpr bool is_variant = is_variant_helper(static_cast<Clean*>(nullptr));

    // Check if it's a std::shared_ptr<U>
    template<typename U>
    static constexpr bool is_shared_ptr_helper(const std::shared_ptr<U>*) { return true; }
    static constexpr bool is_shared_ptr_helper(...) { return false; }
    static constexpr bool is_shared_ptr = is_shared_ptr_helper(static_cast<Clean*>(nullptr));

    // Extract element type from vector (specialization-based)
    template<typename U>
    struct VectorElementType { using type = void; };  // Default: void for non-vectors

    template<typename U>
    struct VectorElementType<std::vector<U>> { using type = U; };

    // Extract element type from std::array (specialization-based)
    template<typename U>
    struct ArrayElementType { using type = void; };  // Default: void for non-arrays

    template<typename U, std::size_t N>
    struct ArrayElementType<std::array<U, N>> { using type = U; };

    // Extract element type from std::shared_ptr (specialization-based)
    template<typename U>
    struct SharedPtrElementType { using type = void; };  // Default: void for non-shared_ptr

    template<typename U>
    struct SharedPtrElementType<std::shared_ptr<U>> { using type = U; };

    // Validate all types in a variant
    template<typename... Us>
    static constexpr bool all_variant_types_valid(const std::variant<Us...>*) {
        return (IsValidTypeImpl<Us>::value && ...);
    }

    // Helper: validate vector element
    template<typename VecType>
    static constexpr bool validate_vector_element() {
        using Element = typename VectorElementType<VecType>::type;
        if constexpr (std::is_same_v<Element, void>) {
            return false;  // Not a vector
        } else {
            return IsValidTypeImpl<Element>::value;
        }
    }

    // Helper: validate array element
    template<typename ArrType>
    static constexpr bool validate_array_element() {
        using Element = typename ArrayElementType<ArrType>::type;
        if constexpr (std::is_same_v<Element, void>) {
            return false;  // Not an array
        } else {
            return IsValidTypeImpl<Element>::value;
        }
    }

    // Helper: validate shared_ptr element
    template<typename PtrType>
    static constexpr bool validate_shared_ptr_element() {
        using Element = typename SharedPtrElementType<PtrType>::type;
        if constexpr (std::is_same_v<Element, void>) {
            return false;  // Not a shared_ptr
        } else {
            return IsValidTypeImpl<Element>::value;
        }
    }

    // Helper: validate conversion target type (recursive)
    template<typename WrapperType>
    static constexpr bool validate_conversion_target() {
        using Target = ConversionTypeOf_t<WrapperType>;
        if constexpr (std::is_same_v<Target, void>) {
            return false;  // No conversion_type declared
        } else {
            return IsValidTypeImpl<Target>::value;  // Recurse into target type
        }
    }

    // Check if Clean has a conversion_type typedef
    static constexpr bool has_conversion_type = HasConversionType<Clean>;

    // Check if Bare is a raw pointer
    static constexpr bool is_pointer = std::is_pointer_v<Bare>;

    static constexpr bool value = []() constexpr {
        // Priority 1: Check original type first (early exit if registered)
        if constexpr (IsRegisteredType<T>::value) {
            return true;
        }
        // Priority 2: Check both pointer type (Bare) and depointed type (Clean)
        else if constexpr (IsRegisteredType<Bare>::value || IsRegisteredType<Clean>::value) {
            return true;
        }
        // Priority 3: Raw pointers to any class/struct are valid as passthrough types
        // This allows interface pointers (e.g., IDebugCapture*) without explicit registration
        else if constexpr (is_pointer && (std::is_class_v<Clean> || std::is_void_v<Clean>)) {
            return true;
        }
        // Priority 4: Wrapper types with conversion_type - validate target recursively
        // Example: ShaderCountersBuffer has conversion_type = VkBuffer
        else if constexpr (has_conversion_type) {
            return validate_conversion_target<Clean>();
        }
        // Priority 5: Container decomposition - validate element types
        else if constexpr (is_vector) {
            return validate_vector_element<Clean>();
        } else if constexpr (is_array) {
            return validate_array_element<Clean>();
        } else if constexpr (is_shared_ptr) {
            return validate_shared_ptr_element<Clean>();
        } else if constexpr (is_variant) {
            return all_variant_types_valid(static_cast<Clean*>(nullptr));
        } else {
            return false;
        }
    }();
};

template<typename T>
inline constexpr bool IsValidType_v = IsValidTypeImpl<T>::value;

// ============================================================================
// FORWARD TYPE DEFINITIONS (Must be defined before registration)
// ============================================================================

/**
 * @brief Pair of ImageView and Sampler for VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
 *
 * Combined image samplers require both an image view and a sampler in a single binding.
 * This struct bundles them together for type-safe handling.
 */
struct ImageSamplerPair {
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    ImageSamplerPair() = default;
    ImageSamplerPair(VkImageView view, VkSampler samp) : imageView(view), sampler(samp) {}
};

// Register types defined above (after their definitions)
REGISTER_COMPILE_TIME_TYPE(ImageSamplerPair);

// ============================================================================
// DESCRIPTOR RESOURCE ENTRY (For inter-node communication with metadata)
// ============================================================================

// Forward declare Resource for DescriptorResourceEntry (inside namespace)
class Resource;

// Forward declare IDebugCapture for DescriptorResourceEntry
namespace Debug { class IDebugCapture; }

// Forward declare SlotRole (defined in ResourceConfig.h)
enum class SlotRole : uint8_t;

/**
 * @brief Descriptor resource entry with metadata
 *
 * Hybrid storage for both Resources and wrapper types:
 * - DescriptorHandleVariant: Cached handle (wrappers) or lazily extracted (Resources)
 * - Resource*: Source for lazy extraction (null for wrapper types)
 * - SlotRole: Execution phase information (Dependency vs Execute)
 * - IDebugCapture*: Optional debug capture interface
 * - Debug::DescriptorResourceDebugMetadata: Tracking metadata (zero-cost in Release)
 *
 * Supports two patterns:
 * 1. True Resources: resource != null, GetHandle() extracts fresh handle
 * 2. Wrapper types (conversion_type): resource == null, GetHandle() returns cached handle
 */
struct DescriptorResourceEntry {
    DescriptorHandleVariant handle;               // Cached or lazily extracted handle
    Resource* resource = nullptr;                 // Source Resource (null for wrappers)
    SlotRole slotRole = static_cast<SlotRole>(0); // Default to no role flags
    Debug::IDebugCapture* debugCapture = nullptr; // Non-owning, set if resource is debug-capturable
    uint32_t bindingIndex = UINT32_MAX;           // Shader binding index for tracking
    Debug::DescriptorResourceDebugMetadata debugMetadata;  // Tracking metadata (zero in Release)

    DescriptorResourceEntry() = default;
    explicit DescriptorResourceEntry(DescriptorHandleVariant h, Resource* res = nullptr, SlotRole role = static_cast<SlotRole>(0), Debug::IDebugCapture* dbg = nullptr, uint32_t binding = UINT32_MAX)
        : handle(std::move(h)), resource(res), slotRole(role), debugCapture(dbg), bindingIndex(binding) {
        // Initialize debug metadata with tracking ID
        debugMetadata.Initialize("DescriptorResourceEntry");
#if VIXEN_DEBUG_DESCRIPTOR_TRACKING
        // Record initial handle value
        debugMetadata.RecordOriginalHandle(Debug::GetHandleValueForTracking(handle));
        TRACK_RESOURCE_CREATED(debugMetadata.trackingId, binding,
                              Debug::GetHandleValueForTracking(handle),
                              Debug::GetHandleTypeNameForTracking(handle),
                              "DescriptorResourceEntry::ctor");
#endif
    }

    /**
     * @brief Extract descriptor handle (lazy for Resources, cached for wrappers)
     *
     * If resource pointer exists, extracts fresh handle from Resource::GetDescriptorHandle().
     * Otherwise returns cached handle (for wrapper types with conversion_type).
     */
    DescriptorHandleVariant GetHandle() const;
};

// ============================================================================
// DESCRIPTOR HANDLE VARIANT (For inter-node communication)
// ============================================================================

// DescriptorHandleVariant: Domain-specific runtime variant for descriptor communication
//
// DESIGN RATIONALE:
// ResourceV3 provides compile-time type safety within Resource storage using PassThroughStorage.
// However, descriptor gathering nodes (DescriptorResourceGathererNode) collect heterogeneous
// descriptor handles from variadic inputs and pass them to descriptor set creation nodes.
//
// This requires runtime polymorphism because:
// 1. Different bindings have different handle types (VkImageView, VkBuffer, VkSampler, etc.)
// 2. The binding array is dynamically sized based on shader reflection
// 3. Each binding's type is only known at runtime from SPIRV reflection metadata
//
// This is NOT a violation of ResourceV3's philosophy - it's a domain-specific inter-node
// communication protocol. ResourceV3 handles type safety WITHIN resource storage; this variant
// handles type safety for the descriptor binding protocol BETWEEN nodes.
//
// Alternative considered: Separate typed outputs (std::vector<VkImageView>, std::vector<VkBuffer>, etc.)
// Rejected because: Would require NxM connections for N descriptor types x M pipeline stages,
// and doesn't match the semantic model of "array of bindings" that Vulkan uses.
// (Forward declared above for type registration - this comment documents the rationale)

// ============================================================================
// PASS-THROUGH STORAGE
// ============================================================================
// PassThroughStorage: Type-erased storage for any registered resource type.
// Supports value, reference, and pointer storage modes with compile-time validation.
// Used by ResourceVariant and VariadicTypedNode for heterogeneous type handling.

class PassThroughStorage {
public:
    enum class Mode : uint8_t { Empty, Value, Reference, Pointer };

    PassThroughStorage() = default;

    // Setters (compile-time tag dispatch)
    template<typename T>
    void Set(T&& value, ValueTag<std::decay_t<T>>) {
        static_assert(IsValidType_v<std::decay_t<T>>, "Type not registered");
        // Store by value - copy into type-erased storage
        valueStorage_ = std::make_any<std::decay_t<T>>(std::forward<T>(value));
        mode_ = Mode::Value;
    }

    template<typename T, typename U>
    void Set(U&& value, RefTag<T>) {
        static_assert(IsValidType_v<T>, "Type not registered");
        // Use std::addressof to get the actual object address (not the parameter's address)
        // When U is T&, this correctly captures the address of the original object
        refPtr_ = const_cast<void*>(static_cast<const void*>(std::addressof(value)));
        mode_ = Mode::Reference;
    }

    template<typename T, typename U>
    void Set(U&& value, ConstRefTag<T>) {
        static_assert(IsValidType_v<T>, "Type not registered");
        // Use std::addressof to get the actual object address (not the parameter's address)
        // When U is T&, std::addressof(value) returns the address of the original object
        constRefPtr_ = static_cast<const void*>(std::addressof(value));
        mode_ = Mode::Reference;
    }

    template<typename T>
    void Set(T* value, PtrTag<T>) {
        static_assert(IsValidType_v<T*>, "Type not registered");
        refPtr_ = static_cast<void*>(value);
        mode_ = Mode::Pointer;
    }

    template<typename T>
    void Set(const T* value, ConstPtrTag<T>) {
        static_assert(IsValidType_v<const T*>, "Type not registered");
        constRefPtr_ = static_cast<const void*>(value);
        mode_ = Mode::Pointer;
    }

    // Getters (compile-time tag dispatch)
    template<typename T>
    T Get(ValueTag<T>) const {
        static_assert(IsValidType_v<T>, "Type not registered");
        #ifndef NDEBUG
        if (!valueStorage_.has_value()) return T{};
        #endif
        return std::any_cast<T>(valueStorage_);
    }

    template<typename T>
    T& Get(RefTag<T>) const {
        static_assert(IsValidType_v<T>, "Type not registered");
        return *static_cast<T*>(refPtr_);
    }

    template<typename T>
    const T& Get(ConstRefTag<T>) const {
        static_assert(IsValidType_v<T>, "Type not registered");
        return *static_cast<const T*>(constRefPtr_);
    }

    template<typename T>
    T* Get(PtrTag<T>) const {
        static_assert(IsValidType_v<T*>, "Type not registered");
        return static_cast<T*>(refPtr_);
    }

    template<typename T>
    const T* Get(ConstPtrTag<T>) const {
        static_assert(IsValidType_v<const T*>, "Type not registered");
        return static_cast<const T*>(constRefPtr_);
    }

    bool IsEmpty() const { return mode_ == Mode::Empty; }

    // Clear all storage and reset to empty state
    void Clear() {
        valueStorage_.reset();
        refPtr_ = nullptr;
        constRefPtr_ = nullptr;
        mode_ = Mode::Empty;
    }

private:
    std::any valueStorage_;  // Type-erased value storage (for Mode::Value)
    void* refPtr_ = nullptr;  // For Mode::Reference and Mode::Pointer
    const void* constRefPtr_ = nullptr;  // For const references/pointers
    Mode mode_ = Mode::Empty;
};

// Register PassThroughStorage after its definition
REGISTER_COMPILE_TIME_TYPE(PassThroughStorage);

// Register DescriptorHandleVariant (now that it's fully defined and all elements are registered)
REGISTER_COMPILE_TIME_TYPE(DescriptorHandleVariant);
REGISTER_COMPILE_TIME_TYPE(DescriptorResourceEntry);

// ============================================================================
// RESOURCE CLASS (Drop-in replacement for old Resource)
// ============================================================================

class Resource {
public:
    Resource() = default;

    // Create resource (same API as old Resource::Create)
    template<typename VulkanType>
    static Resource Create(const ResourceDescriptorVariant& descriptor) {
        Resource res;
        res.descriptor_ = descriptor;
        return res;
    }

    // Prevent copying
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;

    // Allow moving
    Resource(Resource&&) noexcept = default;
    Resource& operator=(Resource&&) noexcept = default;

    /**
     * @brief Clear resource handle and invalidate descriptor extractor
     *
     * CRITICAL: Must be called BEFORE destroying wrapper objects that were
     * stored via SetHandle with conversion_type pattern. The descriptor extractor
     * lambda captures a pointer to the wrapper object - if the wrapper is freed
     * before Clear() is called, subsequent GetDescriptorHandle() calls will
     * access freed memory (undefined behavior).
     *
     * Usage in CleanupImpl:
     *   // First clear the output resources to invalidate extractors
     *   Resource* res = GetOutput(MyConfig::DEBUG_BUFFER, 0);
     *   if (res) res->Clear();
     *   // Now safe to destroy the wrapper
     *   debugBuffer_.reset();
     */
    void Clear() {
        storage_.Clear();
        descriptorExtractor_ = nullptr;
        interfaces_.clear();
        isSet_ = false;
    }

    // Type-to-ResourceType mapping for automatic type deduction
    // Handles all descriptor handle types from DescriptorHandleVariant
    template<typename T>
    static constexpr ResourceType DeduceResourceType() {
        using BaseType = std::remove_cvref_t<T>;

        // Single Vulkan handle types (used in descriptors)
        if constexpr (std::is_same_v<BaseType, VkImageView>) {
            return ResourceType::ImageView;
        }
        else if constexpr (std::is_same_v<BaseType, VkImage>) {
            return ResourceType::Image;
        }
        else if constexpr (std::is_same_v<BaseType, VkBuffer>) {
            return ResourceType::Buffer;
        }
        else if constexpr (std::is_same_v<BaseType, VkBufferView>) {
            return ResourceType::Buffer;  // BufferView is a view into a buffer
        }
        else if constexpr (std::is_same_v<BaseType, VkSampler>) {
            return ResourceType::ImageView;  // Samplers are typically paired with images
        }
        else if constexpr (std::is_same_v<BaseType, VkAccelerationStructureKHR>) {
            return ResourceType::AccelerationStructure;
        }
        // Composite types (combined descriptors)
        else if constexpr (std::is_same_v<BaseType, ImageSamplerPair>) {
            return ResourceType::ImageView;  // Combined image sampler
        }
        // Vector types (descriptor arrays)
        else if constexpr (std::is_same_v<BaseType, std::vector<VkImageView>>) {
            return ResourceType::ImageView;
        }
        else if constexpr (std::is_same_v<BaseType, std::vector<VkBuffer>>) {
            return ResourceType::Buffer;
        }
        else if constexpr (std::is_same_v<BaseType, std::vector<VkBufferView>>) {
            return ResourceType::Buffer;
        }
        else if constexpr (std::is_same_v<BaseType, std::vector<VkSampler>>) {
            return ResourceType::ImageView;
        }
        else if constexpr (std::is_same_v<BaseType, std::vector<VkAccelerationStructureKHR>>) {
            return ResourceType::AccelerationStructure;
        }
        // SwapChain and other pointer types
        else if constexpr (std::is_pointer_v<BaseType>) {
            return ResourceType::PassThroughStorage;
        }
        // Default: PassThroughStorage for non-descriptor types (shared_ptr, custom structs, etc.)
        else {
            return ResourceType::PassThroughStorage;
        }
    }

    // SetHandle - natural C++ types
    template<typename T>
    void SetHandle(T&& value) {
        static_assert(IsValidType_v<T>, "Type not registered");
        using Tag = TypeToTag_t<T>;
        using CleanT = std::remove_cvref_t<T>;

        // For reference types: pass directly to preserve address
        // For value types: forward to enable move semantics
        if constexpr (std::is_lvalue_reference_v<T>) {
            storage_.Set(value, Tag{});
        } else {
            storage_.Set(std::forward<T>(value), Tag{});
        }

        // Automatically deduce and set the correct ResourceType
        type_ = DeduceResourceType<T>();
        isSet_ = true;

        // Capture descriptor extraction function for wrapper types
        // This enables GetDescriptorHandle() to extract the underlying handle
        // from wrapper types without knowing the concrete type at extraction time
        //
        // For pointer types (T*), check the pointee type for conversion_type
        // This allows ShaderCountersBuffer* to extract VkBuffer via ShaderCountersBuffer::conversion_type
        using PointeeT = std::conditional_t<std::is_pointer_v<CleanT>,
                                            std::remove_pointer_t<CleanT>,
                                            CleanT>;

        // ============================================================================
        // SFINAE GUARD: Detect incomplete types that might be missing conversion_type
        // ============================================================================
        // When a class type is forward-declared instead of fully defined, SFINAE causes
        // HasConversionType_v to silently return false, even if the class HAS conversion_type.
        // This leads to mysterious validation errors at runtime. See HacknPlan #61.
        //
        // We check:
        // 1. If CleanT is NOT a Vulkan handle (VkBuffer, VkPipeline, etc.)
        //    Note: Vulkan handles ARE pointer types (VkXxx_T*) so we must exclude them first
        // 2. If passing a pointer to a class type (T* where T is a class)
        // 3. If that class type is incomplete (forward-declared)
        //
        // If incomplete, we static_assert with a helpful message.
        if constexpr (!IsVulkanHandle_v<CleanT> && std::is_pointer_v<CleanT> && std::is_class_v<PointeeT>) {
            // For pointer-to-class types (excluding Vulkan handles), verify the pointee is complete
            static_assert(IsCompleteType_v<PointeeT>,
                "\n\n"
                "================================================================================\n"
                "COMPILE ERROR: Incomplete type passed to Resource::SetHandle()\n"
                "================================================================================\n"
                "\n"
                "The type pointed to is forward-declared, not fully defined.\n"
                "If this type has 'using conversion_type = VkBuffer/VkImageView/etc',\n"
                "the SFINAE detection will SILENTLY FAIL, causing:\n"
                "  - descriptorExtractor_ not being captured\n"
                "  - GetDescriptorHandle() returning stale handles\n"
                "  - 'Invalid VkBuffer Object' validation errors at runtime\n"
                "\n"
                "FIX: Include the full header in your NodeConfig.h file:\n"
                "  #include \"Path/To/YourWrapper.h\"  // NOT forward declaration!\n"
                "\n"
                "See: HacknPlan #61, Vixen-Docs/01-Architecture/Type-System/conversion_type-Pattern.md\n"
                "================================================================================\n"
            );
        }

        // Use the type trait (more reliable than inline requires)
        constexpr bool hasConversion = HasConversionType_v<PointeeT>;

        // Runtime debug check: For wrapper-like types without conversion_type, we can't easily
        // distinguish intentional (e.g., VulkanDevice*) from accidental (missing typedef).
        // The compile-time IsCompleteType_v check above catches the forward-declaration case.
        // For complete types without conversion_type, trust that it's intentional.

        if constexpr (HasConversionType_v<PointeeT>) {
            using ConversionTarget = ConversionTypeOf_t<PointeeT>;
            // Only capture extractor if ConversionTarget is a valid descriptor type
            // This is enforced at compile time by DescriptorHandleVariant construction
            constexpr bool isDescriptorType =
                std::is_same_v<ConversionTarget, VkBuffer> ||
                std::is_same_v<ConversionTarget, VkImageView> ||
                std::is_same_v<ConversionTarget, VkSampler> ||
                std::is_same_v<ConversionTarget, VkBufferView> ||
                std::is_same_v<ConversionTarget, VkImage> ||
                std::is_same_v<ConversionTarget, VkAccelerationStructureKHR>;

            if constexpr (isDescriptorType) {
#if VIXEN_DEBUG_DESCRIPTOR_TRACKING
                // Track extractor creation
                TRACK_EXTRACTOR_CREATED(resourceTrackingId_, 0, debugName_);
#endif

                descriptorExtractor_ = [this]() -> DescriptorHandleVariant {
                    try {
                        if constexpr (std::is_pointer_v<CleanT>) {
                            // For pointer types (e.g., ShaderCountersBuffer*):
                            // - SetHandle stored the pointer using PtrTag<PointeeT> (e.g., PtrTag<ShaderCountersBuffer>)
                            // - storage_.Get(PtrTag<PointeeT>{}) returns PointeeT* (e.g., ShaderCountersBuffer*)
                            // - We dereference once to get the wrapper object, then apply conversion
                            auto* wrapperPtr = storage_.Get(PtrTag<PointeeT>{});
                            if (wrapperPtr) {
                                ConversionTarget converted = static_cast<ConversionTarget>(*wrapperPtr);
#if VIXEN_DEBUG_DESCRIPTOR_TRACKING
                                TRACK_EXTRACTOR_CALLED(resourceTrackingId_, 0,
                                                      reinterpret_cast<uint64_t>(converted),
                                                      "ConversionTarget",
                                                      "descriptorExtractor_");
#endif
                                return DescriptorHandleVariant{converted};
                            }
                        } else {
                            // For value types: get pointer to value
                            auto* wrapper = storage_.Get(PtrTag<CleanT>{});
                            if (wrapper) {
                                ConversionTarget converted = static_cast<ConversionTarget>(*wrapper);
#if VIXEN_DEBUG_DESCRIPTOR_TRACKING
                                TRACK_EXTRACTOR_CALLED(resourceTrackingId_, 0,
                                                      reinterpret_cast<uint64_t>(converted),
                                                      "ConversionTarget",
                                                      "descriptorExtractor_");
#endif
                                return DescriptorHandleVariant{converted};
                            }
                        }
                    } catch (...) {
                        // Exception during extraction - return empty
                    }
                    return DescriptorHandleVariant{std::monostate{}};
                };
            }
        }
    }

    // GetHandle - natural C++ types
    template<typename T>
    T GetHandle() const {
        static_assert(IsValidType_v<T>, "Type not registered");
        using Tag = TypeToTag_t<T>;
        return storage_.Get(Tag{});
    }

    bool IsValid() const { return isSet_; }

    // Metadata (same as old Resource)
    ResourceType GetType() const { return type_; }
    ResourceLifetime GetLifetime() const { return lifetime_; }
    void SetLifetime(ResourceLifetime lt) { lifetime_ = lt; }

    const ResourceDescriptorVariant& GetDescriptor() const { return descriptor_; }

    template<typename DescType>
    const DescType* GetDescriptor() const {
        if (auto* ptr = std::get_if<DescType>(&descriptor_)) return ptr;
        return nullptr;
    }

    template<typename DescType>
    DescType* GetDescriptorMutable() {
        if (auto* ptr = std::get_if<DescType>(&descriptor_)) return ptr;
        return nullptr;
    }

    // Interface extension mechanism
    // Allows resources to expose multiple interfaces (e.g., IDebugCapture, ISerializable)
    // Note: interface pointers are non-owning - caller must ensure lifetime
    template<typename InterfaceType>
    void SetInterface(InterfaceType* iface) {
        if (iface) {
            size_t typeHash = typeid(InterfaceType).hash_code();
            // Replace existing interface of same type, or add new
            for (auto& entry : interfaces_) {
                if (entry.typeHash == typeHash) {
                    entry.ptr = static_cast<void*>(iface);
                    return;
                }
            }
            interfaces_.push_back({static_cast<void*>(iface), typeHash});
        }
    }

    template<typename InterfaceType>
    InterfaceType* GetInterface() const {
        size_t typeHash = typeid(InterfaceType).hash_code();
        for (const auto& entry : interfaces_) {
            if (entry.typeHash == typeHash) {
                return static_cast<InterfaceType*>(entry.ptr);
            }
        }
        return nullptr;
    }

    bool HasInterface() const { return !interfaces_.empty(); }
    size_t InterfaceCount() const { return interfaces_.size(); }


    // Extract handle as DescriptorHandleVariant for inter-node communication
    // This method attempts to extract common descriptor handle types
    DescriptorHandleVariant GetDescriptorHandle() const {
        // Try each descriptor type in order based on ResourceType (not arbitrary ordering)
        // This prevents ImageSamplerPair from incorrectly matching Buffer resources
        //
        // CRITICAL: Try exact type matches FIRST before composite/complex types
        // - VkBuffer/VkImageView/VkSampler are single handles (simple, exact matches)
        // - ImageSamplerPair is composite (struct with two handles)
        // - Try simple types first to avoid incorrect extraction

        // Handle based on ResourceType for better type safety
        // This ensures we try the most likely type first based on the Resource's declared type
        switch (type_) {
            case ResourceType::Buffer:
                // Buffer resources: try buffer handles first, then views
                try { return DescriptorHandleVariant{GetHandle<VkBuffer>()}; } catch (...) {}
                try { return DescriptorHandleVariant{GetHandle<VkBufferView>()}; } catch (...) {}
                // Try vector types for descriptor arrays
                try { return DescriptorHandleVariant{GetHandle<std::vector<VkBuffer>>()}; } catch (...) {}
                try { return DescriptorHandleVariant{GetHandle<std::vector<VkBufferView>>()}; } catch (...) {}
                break;

            case ResourceType::ImageView:
                // ImageView resources: try view first, then sampler (for combined samplers)
                try { return DescriptorHandleVariant{GetHandle<VkImageView>()}; } catch (...) {}
                try { return DescriptorHandleVariant{GetHandle<VkSampler>()}; } catch (...) {}
                try { return DescriptorHandleVariant{GetHandle<ImageSamplerPair>()}; } catch (...) {}
                // Try vector types for descriptor arrays
                try { return DescriptorHandleVariant{GetHandle<std::vector<VkImageView>>()}; } catch (...) {}
                try { return DescriptorHandleVariant{GetHandle<std::vector<VkSampler>>()}; } catch (...) {}
                break;

            case ResourceType::Image:
            case ResourceType::StorageImage:
            case ResourceType::Image3D:
            case ResourceType::CubeMap:
                // Image resources: try views first (most common), then raw images
                try { return DescriptorHandleVariant{GetHandle<VkImageView>()}; } catch (...) {}
                try { return DescriptorHandleVariant{GetHandle<VkImage>()}; } catch (...) {}
                try { return DescriptorHandleVariant{GetHandle<ImageSamplerPair>()}; } catch (...) {}
                // Try vector types for descriptor arrays
                try { return DescriptorHandleVariant{GetHandle<std::vector<VkImageView>>()}; } catch (...) {}
                break;

            case ResourceType::AccelerationStructure:
                // Acceleration structures for ray tracing
                try { return DescriptorHandleVariant{GetHandle<VkAccelerationStructureKHR>()}; } catch (...) {}
                try { return DescriptorHandleVariant{GetHandle<std::vector<VkAccelerationStructureKHR>>()}; } catch (...) {}
                break;

            case ResourceType::PassThroughStorage:
            default:
                // Unknown or pass-through types: try all types (original behavior)
                break;
        }

        // CRITICAL: Try wrapper type extraction FIRST for PassThroughStorage
        // This handles types with conversion_type (e.g., ShaderCountersBuffer -> VkBuffer)
        // Must come before GetHandle<VkBuffer>() attempts because Debug builds return
        // T{} (null) instead of throwing when the type doesn't match, causing null
        // handles to be returned before reaching the extractor.
        if (descriptorExtractor_) {
            auto extracted = descriptorExtractor_();
            if (!std::holds_alternative<std::monostate>(extracted)) {
                return extracted;
            }
        }

        // Fallback: Try all types in safe order (simple → complex)
        // Single handle types first - only return if handle is valid (non-null)
        try {
            auto buf = GetHandle<VkBuffer>();
            if (buf != VK_NULL_HANDLE) return DescriptorHandleVariant{buf};
        } catch (...) {}
        try {
            auto view = GetHandle<VkImageView>();
            if (view != VK_NULL_HANDLE) return DescriptorHandleVariant{view};
        } catch (...) {}
        try {
            auto sampler = GetHandle<VkSampler>();
            if (sampler != VK_NULL_HANDLE) return DescriptorHandleVariant{sampler};
        } catch (...) {}
        try {
            auto bufView = GetHandle<VkBufferView>();
            if (bufView != VK_NULL_HANDLE) return DescriptorHandleVariant{bufView};
        } catch (...) {}
        try {
            auto img = GetHandle<VkImage>();
            if (img != VK_NULL_HANDLE) return DescriptorHandleVariant{img};
        } catch (...) {}
        try {
            auto accel = GetHandle<VkAccelerationStructureKHR>();
            if (accel != VK_NULL_HANDLE) return DescriptorHandleVariant{accel};
        } catch (...) {}
        // Vector types (descriptor arrays)
        try { return DescriptorHandleVariant{GetHandle<std::vector<VkBuffer>>()}; } catch (...) {}
        try { return DescriptorHandleVariant{GetHandle<std::vector<VkImageView>>()}; } catch (...) {}
        try { return DescriptorHandleVariant{GetHandle<std::vector<VkSampler>>()}; } catch (...) {}
        try { return DescriptorHandleVariant{GetHandle<std::vector<VkBufferView>>()}; } catch (...) {}
        try { return DescriptorHandleVariant{GetHandle<std::vector<VkAccelerationStructureKHR>>()}; } catch (...) {}
        // Pointer and composite types LAST
        try { return DescriptorHandleVariant{GetHandle<SwapChainPublicVariables*>()}; } catch (...) {}
        try { return DescriptorHandleVariant{GetHandle<ImageSamplerPair>()}; } catch (...) {}

        // If nothing matched, return empty monostate
        return DescriptorHandleVariant{std::monostate{}};
    }

    // Legacy-style runtime factory translated into ResourceV3
    // Creates a Resource from a runtime ResourceType and a heap-allocated
    // ResourceDescriptorBase. This replaces the old InitializeResourceFromType
    // behavior by dynamically inspecting the incoming descriptor and storing
    // the corresponding compile-time descriptor variant in the Resource.
    static Resource CreateFromType(ResourceType type, std::unique_ptr<ResourceDescriptorBase> desc) {
        Resource res;
        res.type_ = type;
        res.lifetime_ = ResourceLifetime::Transient;

        // Convert the incoming polymorphic descriptor into the variant
        if (desc) {
            // Try to match each known descriptor alternative and move into variant
            if (auto* img = dynamic_cast<ImageDescriptor*>(desc.get())) {
                res.descriptor_ = *img;
            } else if (auto* buf = dynamic_cast<BufferDescriptor*>(desc.get())) {
                res.descriptor_ = *buf;
            } else if (auto* handle = dynamic_cast<HandleDescriptor*>(desc.get())) {
                res.descriptor_ = *handle;
            } else if (auto* cmd = dynamic_cast<CommandPoolDescriptor*>(desc.get())) {
                res.descriptor_ = *cmd;
            } else if (auto* shader = dynamic_cast<ShaderProgramHandleDescriptor*>(desc.get())) {
                res.descriptor_ = *shader;
            } else if (auto* stor = dynamic_cast<StorageImageDescriptor*>(desc.get())) {
                res.descriptor_ = *stor;
            } else if (auto* tex3 = dynamic_cast<Texture3DDescriptor*>(desc.get())) {
                res.descriptor_ = *tex3;
            } else if (auto* runtime = dynamic_cast<RuntimeStructDescriptor*>(desc.get())) {
                res.descriptor_ = *runtime;
            } else if (auto* runtimeBuf = dynamic_cast<RuntimeStructBuffer*>(desc.get())) {
                res.descriptor_ = *runtimeBuf;
            } else {
                // Unknown descriptor type: store a generic HandleDescriptor with runtime type name
                res.descriptor_ = HandleDescriptor("UnknownDescriptor");
            }
        } else {
            res.descriptor_ = HandleDescriptor("EmptyDescriptor");
        }

        // Note: We intentionally do NOT try to initialize or populate handle storage
        // here. Resource handles are set explicitly via SetHandle() in the new API.
        return res;
    }

private:
    PassThroughStorage storage_;
    ResourceType type_ = ResourceType::Buffer;
    ResourceLifetime lifetime_ = ResourceLifetime::Transient;
    ResourceDescriptorVariant descriptor_;
    bool isSet_ = false;

    // Interface extension storage (non-owning, supports multiple interfaces)
    struct InterfaceEntry {
        void* ptr = nullptr;
        size_t typeHash = 0;
    };
    std::vector<InterfaceEntry> interfaces_;

    // Wrapper type extraction function
    // Captured at SetHandle time when wrapper types with conversion_type are stored
    // This enables GetDescriptorHandle() to extract underlying handles without
    // knowing the concrete wrapper type at extraction time
    // Returns DescriptorHandleVariant containing the conversion_type value
    std::function<DescriptorHandleVariant()> descriptorExtractor_;

#if VIXEN_DEBUG_DESCRIPTOR_TRACKING
    // Debug tracking
    Debug::TrackingId resourceTrackingId_ = Debug::GenerateTrackingId();
    std::string debugName_;  // Optional debug name for tracking
#endif

public:
    // Debug tracking accessors
#if VIXEN_DEBUG_DESCRIPTOR_TRACKING
    Debug::TrackingId GetTrackingId() const { return resourceTrackingId_; }
    void SetDebugName(std::string_view name) { debugName_ = name; }
    std::string_view GetDebugName() const { return debugName_; }
#else
    Debug::TrackingId GetTrackingId() const { return 0; }
    void SetDebugName(std::string_view) {}
    std::string_view GetDebugName() const { return ""; }
#endif
};

inline DescriptorHandleVariant DescriptorResourceEntry::GetHandle() const {
    // Hybrid extraction: prefer Resource* (lazy), fallback to cached handle
    // For wrapper types with conversion_type, GetDescriptorHandle() uses the
    // descriptorExtractor_ lambda to extract the underlying handle. If the
    // wrapper's internal buffer is null/invalid, this returns monostate.
    // In that case, fall back to the cached handle from Compile time.
    DescriptorHandleVariant result;
    std::string_view extractionSource;

    if (resource) {
        auto extracted = resource->GetDescriptorHandle();
        // Only use extracted value if it's not monostate
        // This handles the case where wrapper's internal buffer became invalid
        if (!std::holds_alternative<std::monostate>(extracted)) {
            result = extracted;
            extractionSource = "Resource::GetDescriptorHandle";
        } else {
            // Fall through to cached handle if extraction failed
            result = handle;
            extractionSource = "cached_handle(extraction_failed)";
        }
    } else {
        result = handle;
        extractionSource = "cached_handle";
    }

#if VIXEN_DEBUG_DESCRIPTOR_TRACKING
    // Record extraction event
    const_cast<Debug::DescriptorResourceDebugMetadata&>(debugMetadata).RecordExtraction(
        Debug::GetHandleValueForTracking(result));

    TRACK_HANDLE_EXTRACTED(debugMetadata.trackingId, bindingIndex,
                          Debug::GetHandleValueForTracking(result),
                          Debug::GetHandleTypeNameForTracking(result),
                          "DescriptorResourceEntry::GetHandle",
                          extractionSource);

    // Check for handle mismatch
    if (debugMetadata.wasModified) {
        std::cout << "[TRACKING WARNING] Handle mismatch detected for binding " << bindingIndex
                  << " - original=0x" << std::hex << debugMetadata.originalHandleValue
                  << ", extracted=0x" << debugMetadata.lastExtractedValue << std::dec << std::endl;
    }
#endif

    return result;
}

// ============================================================================
// RESOURCE DESCRIPTOR WITH METADATA
// ============================================================================

/**
 * @brief Complete resource descriptor with metadata
 * Used by Schema to describe slot requirements
 */
struct ResourceDescriptor {
    std::string name;
    ResourceType type = ResourceType::Buffer;
    ResourceLifetime lifetime = ResourceLifetime::Transient;
    ResourceDescriptorVariant descriptor;  // Actual descriptor variant (ImageDescriptor, etc.)
    bool nullable = false;

    // Constructor for compatibility
    ResourceDescriptor() = default;

    ResourceDescriptor(
        std::string name_,
        ResourceType type_,
        ResourceLifetime lifetime_,
        const ResourceDescriptorVariant& desc_,
        bool nullable_ = false
    ) : name(std::move(name_)), type(type_), lifetime(lifetime_),
        descriptor(desc_), nullable(nullable_) {}
};

// Backward compatibility aliases (same as old ResourceVariant.h)
using ImageDescription = ImageDescriptor;
using BufferDescription = BufferDescriptor;

// ============================================================================
// RESOURCETYPETRAITS IMPLEMENTATION FOR RESOURCEV3
// ============================================================================
// Provide the implementation of ResourceTypeTraits using IsRegisteredType

template<typename T>
struct ResourceTypeTraits {
    // Strip containers to get base type
    using BaseType = typename StripContainer<T>::Type;

    // Use IsValidType_v which handles const, &, *, containers, and variants
    static constexpr bool isValid = IsValidType_v<T>;

    // Provide dummy descriptor type for compatibility
    using DescriptorT = HandleDescriptor;

    // Provide dummy resource type for compatibility
    static constexpr ResourceType resourceType = ResourceType::Buffer;

    // Container metadata
    static constexpr bool isContainer = StripContainer<T>::isContainer;
    static constexpr bool isVector = StripContainer<T>::isVector;
    static constexpr bool isArray = StripContainer<T>::isArray;
    static constexpr size_t arraySize = StripContainer<T>::arraySize;

    // Variant detection
    static constexpr bool isCustomVariant = false;
};

// Note: No specialization for pointers because Vulkan handles are typedef'd pointers
// but should be treated as opaque types, not decomposed into their pointer components.
// VkSwapchainKHR, VkImage, etc. are registered as complete types.

} // namespace Vixen::RenderGraph