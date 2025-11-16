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

#include "Headers.h"  // Include for Vulkan type definitions
#include "ResourceTypes.h"
#include "ResourceTypeTraits.h"  // Include for StripContainer
#include "../VariantDescriptors.h"
#include <variant>
#include <type_traits>
#include <cstdint>
#include <any>
#include <cassert>
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

// Recursive compile-time validation
// Handles: direct types, pointers, vectors, variants, and nested combinations
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

    static constexpr bool value = []() constexpr {
        // Check original type first (early exit if registered)
        if constexpr (IsRegisteredType<T>::value) {
            return true;
        }
        // Check both pointer type (Bare) and depointed type (Clean)
        else if constexpr (IsRegisteredType<Bare>::value || IsRegisteredType<Clean>::value) {
            return true;
        }
        // Only decompose if not directly registered
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

    // SetHandle - natural C++ types
    template<typename T>
    void SetHandle(T&& value) {
        static_assert(IsValidType_v<T>, "Type not registered");
        using Tag = TypeToTag_t<T>;
        // For reference types: pass directly to preserve address
        // For value types: forward to enable move semantics
        if constexpr (std::is_lvalue_reference_v<T>) {
            storage_.Set(value, Tag{});
        } else {
            storage_.Set(std::forward<T>(value), Tag{});
        }
        isSet_ = true;
    }

    // GetHandle - natural C++ types
    template<typename T>
    T GetHandle() const {
        static_assert(IsValidType_v<T>, "Type not registered");
        using Tag = TypeToTag_t<T>;
        auto&& result = storage_.Get(Tag{});

        // DEBUG: Log vector reference addresses
        if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, std::vector<VkSemaphore>>) {
            using VecT = std::vector<VkSemaphore>;
            const VecT* vecPtr = nullptr;
            if constexpr (std::is_lvalue_reference_v<decltype(result)>) {
                vecPtr = &result;
            }
            std::cout << "[Resource::GetHandle] Returning vector reference, address: "
                      << vecPtr << ", size: " << (vecPtr ? vecPtr->size() : 0)
                      << ", capacity: " << (vecPtr ? vecPtr->capacity() : 0) << std::endl;
        }

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


    // Extract handle as DescriptorHandleVariant for inter-node communication
    // This method attempts to extract common descriptor handle types
    DescriptorHandleVariant GetDescriptorHandle() const {
        // Try each descriptor type in order of likelihood
        // Note: This uses template specialization - only types that were Set() will succeed

        // Try ImageSamplerPair (for combined image samplers)
        try { return DescriptorHandleVariant{GetHandle<ImageSamplerPair>()}; } catch (...) {}

        // Try VkImageView (most common for sampledImage descriptors)
        try { return DescriptorHandleVariant{GetHandle<VkImageView>()}; } catch (...) {}

        // Try VkBuffer (for uniform/storage buffers)
        try { return DescriptorHandleVariant{GetHandle<VkBuffer>()}; } catch (...) {}

        // Try VkBufferView (for texel buffers)
        try { return DescriptorHandleVariant{GetHandle<VkBufferView>()}; } catch (...) {}

        // Try VkSampler (for sampler descriptors)
        try { return DescriptorHandleVariant{GetHandle<VkSampler>()}; } catch (...) {}

        // Try VkImage (for storage images when separate from view)
        try { return DescriptorHandleVariant{GetHandle<VkImage>()}; } catch (...) {}

        // Try VkAccelerationStructureKHR (for ray tracing)
        try { return DescriptorHandleVariant{GetHandle<VkAccelerationStructureKHR>()}; } catch (...) {}

        // Try SwapChainPublicVariables* (for swapchain resources)
        try { return DescriptorHandleVariant{GetHandle<SwapChainPublicVariables*>()}; } catch (...) {}

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
};

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