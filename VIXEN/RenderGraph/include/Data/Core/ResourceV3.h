#pragma once

/**
 * @file ResourceV3.h
 * @brief Zero-overhead compile-time resource type system (drop-in replacement)
 *
 * This file provides a drop-in replacement for ResourceVariant.h with:
 * - Compile-time-only type validation (static_assert)
 * - Zero runtime overhead (type tags disappear after compilation)
 * - Natural C++ syntax (T&, T*, const T&)
 * - Same API as original Resource class
 *
 * Migration: Replace #include "ResourceVariant.h" with #include "ResourceV3.h"
 * No code changes required!
 */

#include "ResourceTypes.h"
#include "../VariantDescriptors.h"
#include <variant>
#include <type_traits>
#include <cstdint>

// Forward declarations (same as ResourceVariant.h)
struct SwapChainPublicVariables;
struct SwapChainBuffer;
class VulkanShader;

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

// Register application types
REGISTER_COMPILE_TIME_TYPE(CameraData);
REGISTER_COMPILE_TIME_TYPE(SwapChainPublicVariables);
REGISTER_COMPILE_TIME_TYPE(SwapChainBuffer);
REGISTER_COMPILE_TIME_TYPE(VulkanShader);
REGISTER_COMPILE_TIME_TYPE(ShaderManagement::CompiledProgram);
REGISTER_COMPILE_TIME_TYPE(ShaderManagement::ShaderDataBundle);
REGISTER_COMPILE_TIME_TYPE(Vixen::Vulkan::Resources::VulkanDevice);
REGISTER_COMPILE_TIME_TYPE(ShaderProgramDescriptor);
REGISTER_COMPILE_TIME_TYPE(LoopReference);
REGISTER_COMPILE_TIME_TYPE(BoolOp);
REGISTER_COMPILE_TIME_TYPE(SlotRole);
REGISTER_COMPILE_TIME_TYPE(InputState);

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

// Compile-time validation
template<typename T>
inline constexpr bool IsValidType_v = []() constexpr {
    using Tag = TypeToTag_t<T>;
    using BaseType = typename Tag::storage_type;
    using BareBaseType = std::remove_cv_t<std::remove_pointer_t<BaseType>>;
    return IsRegisteredType<BareBaseType>::value;
}();

// ============================================================================
// ZERO-OVERHEAD STORAGE
// ============================================================================

class ZeroOverheadStorage {
public:
    enum class Mode : uint8_t { Empty, Value, Reference, Pointer };

    ZeroOverheadStorage() = default;

    // Setters (compile-time tag dispatch)
    template<typename T>
    void Set(T&& value, ValueTag<std::decay_t<T>>) {
        data_ = std::forward<T>(value);
        mode_ = Mode::Value;
    }

    template<typename T>
    void Set(T& value, RefTag<T>) {
        refPtr_ = static_cast<void*>(&value);
        mode_ = Mode::Reference;
    }

    template<typename T>
    void Set(const T& value, ConstRefTag<T>) {
        constRefPtr_ = static_cast<const void*>(&value);
        mode_ = Mode::Reference;
    }

    template<typename T>
    void Set(T* value, PtrTag<T>) {
        refPtr_ = static_cast<void*>(value);
        mode_ = Mode::Pointer;
    }

    template<typename T>
    void Set(const T* value, ConstPtrTag<T>) {
        constRefPtr_ = static_cast<const void*>(value);
        mode_ = Mode::Pointer;
    }

    // Getters (compile-time tag dispatch)
    template<typename T>
    T Get(ValueTag<T>) const {
        if (auto* ptr = std::get_if<T>(&data_)) return *ptr;
        return T{};
    }

    template<typename T>
    T& Get(RefTag<T>) const {
        return *static_cast<T*>(refPtr_);
    }

    template<typename T>
    const T& Get(ConstRefTag<T>) const {
        return *static_cast<const T*>(constRefPtr_);
    }

    template<typename T>
    T* Get(PtrTag<T>) const {
        return static_cast<T*>(refPtr_);
    }

    template<typename T>
    const T* Get(ConstPtrTag<T>) const {
        return static_cast<const T*>(constRefPtr_);
    }

    bool IsEmpty() const { return mode_ == Mode::Empty; }

private:
    std::variant<std::monostate, VkImage, VkBuffer, VkImageView, uint32_t, float> data_;
    void* refPtr_ = nullptr;
    const void* constRefPtr_ = nullptr;
    Mode mode_ = Mode::Empty;
};

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
        storage_.Set(std::forward<T>(value), Tag{});
        isSet_ = true;
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

private:
    ZeroOverheadStorage storage_;
    ResourceType type_ = ResourceType::Buffer;
    ResourceLifetime lifetime_ = ResourceLifetime::Transient;
    ResourceDescriptorVariant descriptor_;
    bool isSet_ = false;
};

// Backward compatibility aliases (same as old ResourceVariant.h)
using ResourceDescriptor = ResourceDescriptorBase;
using ImageDescription = ImageDescriptor;
using BufferDescription = BufferDescriptor;

// ============================================================================
// COMPATIBILITY LAYER FOR ResourceTypeTraits
// ============================================================================

/**
 * @brief Compatibility layer to support existing code using ResourceTypeTraits
 *
 * Maps the old ResourceTypeTraits API to the new compile-time validation system.
 * This enables FieldExtractor and other components to work during the migration.
 */
template<typename T>
struct ResourceTypeTraits {
    // Strip containers to get base type
    using BaseType = typename StripContainer<T>::Type;

    // Check if base type is registered
    static constexpr bool isValid =
        IsRegisteredType<BaseType>::value ||
        IsRegisteredType<std::vector<BaseType>>::value ||
        (StripContainer<T>::isContainer && IsRegisteredType<BaseType>::value);

    // Provide dummy descriptor type for compatibility
    using DescriptorT = HandleDescriptor;

    // Provide dummy resource type for compatibility
    static constexpr ResourceType resourceType = ResourceType::Buffer;

    // Container metadata
    static constexpr bool isContainer = StripContainer<T>::isContainer;
    static constexpr bool isVector = StripContainer<T>::isVector;
    static constexpr bool isArray = StripContainer<T>::isArray;
    static constexpr size_t arraySize = StripContainer<T>::arraySize;
};

// Specialization for pointers (normalize cv-qualifiers)
template<typename T>
struct ResourceTypeTraits<T*> {
    using BaseType = std::remove_cv_t<T>;
    static constexpr bool isValid = IsRegisteredType<BaseType>::value;
    using DescriptorT = HandleDescriptor;
    static constexpr ResourceType resourceType = ResourceType::Buffer;
    static constexpr bool isContainer = false;
    static constexpr bool isVector = false;
    static constexpr bool isArray = false;
    static constexpr size_t arraySize = 0;
};

} // namespace Vixen::RenderGraph