#pragma once

#include "TypePattern.h"
#include "ResourceTypes.h"
#include "VariantDescriptors.h"
#include <variant>
#include <memory>
#include <unordered_map>

// Forward declarations from existing system
struct SwapChainPublicVariables;
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
// SIMPLIFIED TYPE REGISTRY - Just register base types
// ============================================================================

/**
 * @brief Registry of base resource types
 *
 * No more N×M explosion! Just register base types and the system
 * automatically handles pointers, references, containers, etc.
 */
class ResourceTypeRegistry {
public:
    struct TypeInfo {
        ResourceType resourceType;
        std::type_index typeIndex;
        size_t descriptorTypeHash;
        std::string typeName;
    };

    // Singleton access
    static ResourceTypeRegistry& Instance() {
        static ResourceTypeRegistry instance;
        return instance;
    }

    // Register a base type
    template<typename T, typename DescriptorT>
    void RegisterBaseType(ResourceType resType, const std::string& name) {
        static_assert(!std::is_pointer_v<T>, "Register base types only");
        static_assert(!std::is_reference_v<T>, "Register base types only");

        TypeInfo info{
            resType,
            std::type_index(typeid(T)),
            typeid(DescriptorT).hash_code(),
            name
        };

        baseTypes[typeid(T).hash_code()] = info;
        typesByName[name] = typeid(T).hash_code();
    }

    // Check if a complex type is acceptable (with all modifiers)
    template<typename T>
    bool IsTypeAcceptable() const {
        using Pattern = TypePattern<T>;
        using BaseType = typename Pattern::BaseType;
        return baseTypes.count(typeid(BaseType).hash_code()) > 0;
    }

    // Get type info for a complex type
    template<typename T>
    const TypeInfo* GetTypeInfo() const {
        using Pattern = TypePattern<T>;
        using BaseType = typename Pattern::BaseType;

        auto it = baseTypes.find(typeid(BaseType).hash_code());
        return it != baseTypes.end() ? &it->second : nullptr;
    }

private:
    ResourceTypeRegistry() = default;
    std::unordered_map<size_t, TypeInfo> baseTypes;
    std::unordered_map<std::string, size_t> typesByName;
};

// ============================================================================
// SMART RESOURCE VARIANT - Handles all type patterns automatically
// ============================================================================

/**
 * @brief Enhanced ResourceVariant that accepts T, T*, T&, vector<T>, etc.
 *
 * Key improvements:
 * - Register VkImage once, automatically accept VkImage*, const VkImage&, vector<VkImage>
 * - No variant explosion - internal storage uses type erasure
 * - Type-safe compile-time access through templates
 * - Supports persistent stack objects through pointers/references
 */
class ResourceVariantV2 {
public:
    ResourceVariantV2() = default;

    // Set value - accepts any registered pattern
    template<typename T>
    void Set(T&& value) {
        using DecayedType = std::decay_t<T>;
        using Pattern = TypePattern<DecayedType>;

        auto& registry = ResourceTypeRegistry::Instance();
        if (!registry.IsTypeAcceptable<DecayedType>()) {
            throw std::runtime_error("Type not registered: " + std::string(typeid(DecayedType).name()));
        }

        // Store type info
        if (auto* info = registry.GetTypeInfo<DecayedType>()) {
            resourceType = info->resourceType;
            baseTypeHash = typeid(typename Pattern::BaseType).hash_code();
            modifiers = Pattern::modifiers;
        }

        // Handle different storage modes
        if constexpr (Pattern::isPointer) {
            // Store pointer (non-owning)
            storage = TypeErasedStorage::StorePointer(value);
        } else if constexpr (Pattern::isReference) {
            // Store reference as pointer (non-owning)
            storage = TypeErasedStorage::StoreReference(value);
        } else if constexpr (Pattern::isRefWrapper) {
            // Store reference_wrapper
            storage = TypeErasedStorage::StoreReference(value.get());
        } else if constexpr (Pattern::isSharedPtr) {
            // Store shared_ptr
            storage = TypeErasedStorage::StoreShared(std::forward<T>(value));
        } else if constexpr (Pattern::isUniquePtr) {
            // Store unique_ptr (transfers ownership)
            storage = TypeErasedStorage::StoreUnique(std::forward<T>(value));
        } else if constexpr (Pattern::isVector) {
            // Store vector by value
            storage = TypeErasedStorage::StoreValue(value);
        } else {
            // Store regular value
            storage = TypeErasedStorage::StoreValue(value);
        }
    }

    // Get value - type-safe compile-time access
    template<typename T>
    T Get() const {
        using Pattern = TypePattern<T>;

        // Verify type matches
        auto& registry = ResourceTypeRegistry::Instance();
        if (!registry.IsTypeAcceptable<T>()) {
            throw std::runtime_error("Type not registered");
        }

        // Extract value based on pattern
        if constexpr (Pattern::isPointer) {
            return storage.GetPointer<std::remove_pointer_t<T>>();
        } else if constexpr (Pattern::isReference) {
            return storage.GetReference<std::remove_reference_t<T>>();
        } else {
            return storage.GetValue<T>();
        }
    }

    // Check if holds specific type (including modifiers)
    template<typename T>
    bool HoldsType() const {
        using Pattern = TypePattern<T>;
        return baseTypeHash == typeid(typename Pattern::BaseType).hash_code() &&
               modifiers == Pattern::modifiers;
    }

    // Check if holds base type (ignoring modifiers)
    template<typename T>
    bool HoldsBaseType() const {
        return baseTypeHash == typeid(T).hash_code();
    }

    bool IsValid() const { return storage.IsValid(); }
    ResourceType GetResourceType() const { return resourceType; }
    TypeModifier GetModifiers() const { return modifiers; }

    // Helper to check specific modifiers
    bool IsPointer() const { return (modifiers & TypeModifier::Pointer) != TypeModifier::None; }
    bool IsReference() const { return (modifiers & TypeModifier::Reference) != TypeModifier::None; }
    bool IsConst() const { return (modifiers & TypeModifier::Const) != TypeModifier::None; }
    bool IsVector() const { return (modifiers & TypeModifier::Vector) != TypeModifier::None; }
    bool IsArray() const { return (modifiers & TypeModifier::Array) != TypeModifier::None; }

private:
    TypeErasedStorage storage;
    ResourceType resourceType = ResourceType::Buffer;
    size_t baseTypeHash = 0;
    TypeModifier modifiers = TypeModifier::None;
};

// ============================================================================
// ENHANCED RESOURCE CLASS
// ============================================================================

/**
 * @brief Resource wrapper with enhanced type handling
 *
 * Supports all type patterns without registry explosion
 */
class ResourceV2 {
public:
    ResourceV2() = default;

    // Create resource with specific type and descriptor
    template<typename T>
    static ResourceV2 Create(ResourceType resType, const ResourceDescriptorVariant& desc) {
        ResourceV2 res;
        res.type = resType;
        res.descriptor = desc;
        res.variant.Set(T{});  // Initialize with default value
        return res;
    }

    // Set handle - accepts any pattern of registered types
    template<typename T>
    void SetHandle(T&& value) {
        variant.Set(std::forward<T>(value));
    }

    // Get handle - type-safe access
    template<typename T>
    T GetHandle() const {
        return variant.Get<T>();
    }

    // Check type
    template<typename T>
    bool HoldsType() const {
        return variant.HoldsType<T>();
    }

    bool IsValid() const { return variant.IsValid(); }
    ResourceType GetType() const { return type; }
    ResourceLifetime GetLifetime() const { return lifetime; }
    void SetLifetime(ResourceLifetime lt) { lifetime = lt; }

    const ResourceDescriptorVariant& GetDescriptor() const { return descriptor; }

private:
    ResourceV2 type = ResourceType::Buffer;
    ResourceLifetime lifetime = ResourceLifetime::Transient;
    ResourceDescriptorVariant descriptor;
    ResourceVariantV2 variant;
};

// ============================================================================
// REGISTRATION HELPER MACROS
// ============================================================================

/**
 * @brief Simple macro to register base types
 *
 * Usage: REGISTER_RESOURCE_TYPE(VkImage, ImageDescriptor, ResourceType::Image)
 *
 * This automatically enables:
 * - VkImage, VkImage*, const VkImage*, VkImage&, const VkImage&
 * - std::vector<VkImage>, std::vector<VkImage>*, const std::vector<VkImage>&
 * - std::shared_ptr<VkImage>, std::unique_ptr<VkImage>
 * - std::reference_wrapper<VkImage>
 */
#define REGISTER_RESOURCE_TYPE(Type, Descriptor, ResType) \
    ResourceTypeRegistry::Instance().RegisterBaseType<Type, Descriptor>( \
        ResType, #Type \
    )

// ============================================================================
// STATIC REGISTRATION
// ============================================================================

inline void RegisterCoreResourceTypes() {
    auto& registry = ResourceTypeRegistry::Instance();

    // Vulkan handles
    registry.RegisterBaseType<VkImage, ImageDescriptor>(ResourceType::Image, "VkImage");
    registry.RegisterBaseType<VkBuffer, BufferDescriptor>(ResourceType::Buffer, "VkBuffer");
    registry.RegisterBaseType<VkImageView, HandleDescriptor>(ResourceType::Image, "VkImageView");
    registry.RegisterBaseType<VkSampler, HandleDescriptor>(ResourceType::Buffer, "VkSampler");
    registry.RegisterBaseType<VkSurfaceKHR, HandleDescriptor>(ResourceType::Image, "VkSurfaceKHR");
    registry.RegisterBaseType<VkSwapchainKHR, HandleDescriptor>(ResourceType::Buffer, "VkSwapchainKHR");
    registry.RegisterBaseType<VkRenderPass, HandleDescriptor>(ResourceType::Buffer, "VkRenderPass");
    registry.RegisterBaseType<VkFramebuffer, HandleDescriptor>(ResourceType::Buffer, "VkFramebuffer");
    registry.RegisterBaseType<VkDescriptorSetLayout, HandleDescriptor>(ResourceType::Buffer, "VkDescriptorSetLayout");
    registry.RegisterBaseType<VkDescriptorPool, HandleDescriptor>(ResourceType::Buffer, "VkDescriptorPool");
    registry.RegisterBaseType<VkDescriptorSet, HandleDescriptor>(ResourceType::Buffer, "VkDescriptorSet");
    registry.RegisterBaseType<VkCommandPool, CommandPoolDescriptor>(ResourceType::Buffer, "VkCommandPool");
    registry.RegisterBaseType<VkSemaphore, HandleDescriptor>(ResourceType::Buffer, "VkSemaphore");
    registry.RegisterBaseType<VkFence, HandleDescriptor>(ResourceType::Buffer, "VkFence");
    registry.RegisterBaseType<VkDevice, HandleDescriptor>(ResourceType::Buffer, "VkDevice");
    registry.RegisterBaseType<VkPhysicalDevice, HandleDescriptor>(ResourceType::Buffer, "VkPhysicalDevice");
    registry.RegisterBaseType<VkInstance, HandleDescriptor>(ResourceType::Buffer, "VkInstance");
    registry.RegisterBaseType<VkPipeline, HandleDescriptor>(ResourceType::Buffer, "VkPipeline");
    registry.RegisterBaseType<VkPipelineLayout, HandleDescriptor>(ResourceType::Buffer, "VkPipelineLayout");
    registry.RegisterBaseType<VkPipelineCache, HandleDescriptor>(ResourceType::Buffer, "VkPipelineCache");
    registry.RegisterBaseType<VkShaderModule, HandleDescriptor>(ResourceType::Buffer, "VkShaderModule");
    registry.RegisterBaseType<VkCommandBuffer, HandleDescriptor>(ResourceType::Buffer, "VkCommandBuffer");
    registry.RegisterBaseType<VkQueue, HandleDescriptor>(ResourceType::Buffer, "VkQueue");
    registry.RegisterBaseType<VkBufferView, HandleDescriptor>(ResourceType::Buffer, "VkBufferView");

    // Basic types
    registry.RegisterBaseType<uint32_t, HandleDescriptor>(ResourceType::Buffer, "uint32_t");
    registry.RegisterBaseType<uint64_t, HandleDescriptor>(ResourceType::Buffer, "uint64_t");
    registry.RegisterBaseType<uint8_t, HandleDescriptor>(ResourceType::Buffer, "uint8_t");
    registry.RegisterBaseType<bool, HandleDescriptor>(ResourceType::Buffer, "bool");
    registry.RegisterBaseType<VkFormat, HandleDescriptor>(ResourceType::Buffer, "VkFormat");
    registry.RegisterBaseType<VkPushConstantRange, HandleDescriptor>(ResourceType::Buffer, "VkPushConstantRange");

    // Application types
    registry.RegisterBaseType<VulkanShader, HandleDescriptor>(ResourceType::Buffer, "VulkanShader");
    registry.RegisterBaseType<SwapChainPublicVariables, HandleDescriptor>(ResourceType::Buffer, "SwapChainPublicVariables");
    registry.RegisterBaseType<CameraData, HandleDescriptor>(ResourceType::Buffer, "CameraData");
    registry.RegisterBaseType<InputState, HandleDescriptor>(ResourceType::Buffer, "InputState");

    // Platform-specific types
#ifdef _WIN32
    registry.RegisterBaseType<HWND, HandleDescriptor>(ResourceType::Buffer, "HWND");
    registry.RegisterBaseType<HINSTANCE, HandleDescriptor>(ResourceType::Buffer, "HINSTANCE");
#endif
}

// Auto-register types on startup
struct ResourceTypeInitializer {
    ResourceTypeInitializer() {
        RegisterCoreResourceTypes();
    }
};

// Static initializer
inline ResourceTypeInitializer g_resourceTypeInit;

} // namespace Vixen::RenderGraph