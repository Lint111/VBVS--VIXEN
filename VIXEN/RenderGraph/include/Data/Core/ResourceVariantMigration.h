#pragma once

#include "TypeValidation.h"
#include "ResourceVariant.h"  // Existing system
#include <type_traits>

namespace Vixen::RenderGraph {

// ============================================================================
// MIGRATION WRAPPER - Bridge between old and new type systems
// ============================================================================

/**
 * @brief Migration wrapper that provides backward compatibility
 *
 * This allows existing code using ResourceVariant and ResourceTypeTraits
 * to work with the new cached validation system without modifications.
 *
 * Strategy:
 * 1. Keep existing API surface intact
 * 2. Redirect validation to new cached system
 * 3. Maintain existing variant storage
 * 4. Gradually migrate internals to new system
 */

// ============================================================================
// ENHANCED RESOURCE TYPE TRAITS - Redirect to new system
// ============================================================================

/**
 * @brief Enhanced ResourceTypeTraits that uses cached validation
 *
 * This replaces the existing ResourceTypeTraits template to use our
 * new cached validation system while maintaining the same API.
 */
template<typename T>
struct ResourceTypeTraitsV2 {
    // Maintain backward compatibility with existing code
    using BaseType = typename StripContainer<T>::Type;
    using DecayedBase = std::remove_cv_t<std::remove_reference_t<BaseType>>;
    using DecayedT = std::remove_cv_t<std::remove_reference_t<T>>;

    // Use cached validation system
    static constexpr bool isValid = []() {
        // At compile time, we can't access runtime cache,
        // but we can still check using the recursive validator
        return CachedTypeRegistry::Instance().IsTypeAcceptable<T>();
    }();

    // Maintain existing API
    static constexpr bool isVariantType = IsResourceVariant_v<T>;
    static constexpr bool isContainer = StripContainer<T>::isContainer;
    static constexpr bool isVector = StripContainer<T>::isVector;
    static constexpr bool isArray = StripContainer<T>::isArray;
    static constexpr size_t arraySize = StripContainer<T>::arraySize;

    // For descriptor and resourceType, we need to maintain compatibility
    // with the old registry system
    using DescriptorT = typename std::conditional_t<
        isVariantType,
        std::type_identity<HandleDescriptor>,
        std::type_identity<typename ResourceTypeTraitsImpl<DecayedBase>::DescriptorT>
    >::type;

    static constexpr ResourceType resourceType =
        isVariantType ? ResourceType::Buffer : ResourceTypeTraitsImpl<DecayedBase>::resourceType;
};

// ============================================================================
// MIGRATION REGISTRY - Synchronize old and new systems
// ============================================================================

/**
 * @brief Registry that synchronizes between old and new type systems
 */
class MigrationRegistry {
public:
    static MigrationRegistry& Instance() {
        static MigrationRegistry instance;
        return instance;
    }

    /**
     * @brief Register a type in both old and new systems
     *
     * This ensures types are registered in both the macro-based
     * RESOURCE_TYPE_REGISTRY and the new cached validation system.
     */
    template<typename T, typename DescriptorT>
    void RegisterType(ResourceType resType) {
        // Register in new cached system
        CachedTypeRegistry::Instance().RegisterBaseType<T>();

        // The old system uses macro registration, so we can't dynamically
        // add to it. Types must still be in RESOURCE_TYPE_REGISTRY.
        // But we can validate consistency.
        ValidateRegistration<T>(resType);
    }

    /**
     * @brief Check if type is valid using new cached system
     *
     * This is a drop-in replacement for ResourceTypeTraits<T>::isValid
     */
    template<typename T>
    bool IsTypeValid() {
        return CachedTypeRegistry::Instance().IsTypeAcceptable<T>();
    }

    /**
     * @brief Enable wrapper types for a base type
     *
     * Automatically enables RefW<T>, PtrW<T>, VectorW<T>, etc.
     */
    template<typename T>
    void EnableWrappers() {
        // Base type should already be registered
        if (!IsTypeValid<T>()) {
            throw std::runtime_error("Base type not registered");
        }

        // Wrappers are automatically valid in new system
        // Just validate they work
        ValidateWrapper<RefW<T>>();
        ValidateWrapper<PtrW<T>>();
        ValidateWrapper<VectorW<T>>();
        ValidateWrapper<OptionalW<T>>();
    }

private:
    MigrationRegistry() {
        // Initialize with existing base types from RESOURCE_TYPE_REGISTRY
        InitializeFromExistingRegistry();
    }

    void InitializeFromExistingRegistry() {
        // Register all types that are in the macro-based registry
        // This ensures the new system knows about all existing types

        auto& cache = CachedTypeRegistry::Instance();

        // Register Vulkan handles
        cache.RegisterBaseType<VkImage>();
        cache.RegisterBaseType<VkBuffer>();
        cache.RegisterBaseType<VkImageView>();
        cache.RegisterBaseType<VkSampler>();
        cache.RegisterBaseType<VkSurfaceKHR>();
        cache.RegisterBaseType<VkSwapchainKHR>();
        cache.RegisterBaseType<VkRenderPass>();
        cache.RegisterBaseType<VkFramebuffer>();
        cache.RegisterBaseType<VkDescriptorSetLayout>();
        cache.RegisterBaseType<VkDescriptorPool>();
        cache.RegisterBaseType<VkDescriptorSet>();
        cache.RegisterBaseType<VkCommandPool>();
        cache.RegisterBaseType<VkSemaphore>();
        cache.RegisterBaseType<VkFence>();
        cache.RegisterBaseType<VkDevice>();
        cache.RegisterBaseType<VkPhysicalDevice>();
        cache.RegisterBaseType<VkInstance>();
        cache.RegisterBaseType<VkPipeline>();
        cache.RegisterBaseType<VkPipelineLayout>();
        cache.RegisterBaseType<VkPipelineCache>();
        cache.RegisterBaseType<VkShaderModule>();
        cache.RegisterBaseType<VkCommandBuffer>();
        cache.RegisterBaseType<VkQueue>();
        cache.RegisterBaseType<VkBufferView>();
        cache.RegisterBaseType<VkFormat>();
        cache.RegisterBaseType<VkPushConstantRange>();

        // Register basic types
        cache.RegisterBaseType<uint32_t>();
        cache.RegisterBaseType<uint64_t>();
        cache.RegisterBaseType<uint8_t>();
        cache.RegisterBaseType<bool>();
        cache.RegisterBaseType<float>();

        // Register application types
        cache.RegisterBaseType<CameraData>();
        cache.RegisterBaseType<SwapChainPublicVariables>();
        cache.RegisterBaseType<VulkanShader>();

        // Platform-specific types
#ifdef _WIN32
        cache.RegisterBaseType<HWND>();
        cache.RegisterBaseType<HINSTANCE>();
#endif
    }

    template<typename T>
    void ValidateRegistration(ResourceType resType) {
        // Verify the type is properly registered
        if (!IsTypeValid<T>()) {
            throw std::runtime_error("Type registration failed");
        }
    }

    template<typename W>
    void ValidateWrapper() {
        // Just check that wrapper is valid
        if (!IsTypeValid<W>()) {
            throw std::runtime_error("Wrapper validation failed");
        }
    }
};

// ============================================================================
// MIGRATION MACROS - Gradual transition
// ============================================================================

/**
 * @brief Enable new validation system for a specific type
 *
 * Use this to gradually migrate types to the new system
 */
#define ENABLE_NEW_VALIDATION(Type) \
    static_assert(MigrationRegistry::Instance().IsTypeValid<Type>(), \
                  #Type " not valid in new validation system")

/**
 * @brief Register type in both old and new systems
 *
 * During migration, use this instead of RESOURCE_TYPE macro
 */
#define MIGRATE_RESOURCE_TYPE(Type, Descriptor, ResType) \
    MigrationRegistry::Instance().RegisterType<Type, Descriptor>(ResType)

// ============================================================================
// COMPATIBILITY LAYER - ResourceVariant with new validation
// ============================================================================

/**
 * @brief Enhanced ResourceVariant that uses cached validation
 *
 * Drop-in replacement for existing ResourceVariant with:
 * - Cached type validation
 * - Support for wrapper types
 * - Backward compatible API
 */
class ResourceVariantV3 {
public:
    ResourceVariantV3() = default;

    // Set value with cached validation
    template<typename T>
    void Set(const T& value) {
        // Use new cached validation
        if (!CachedTypeRegistry::Instance().IsTypeAcceptable<T>()) {
            throw std::runtime_error("Type not acceptable");
        }

        // Store in existing variant (maintains compatibility)
        if constexpr (IsTypeInVariant_v<T, ResourceVariant>) {
            storage = value;
        } else if constexpr (std::is_pointer_v<T>) {
            // Handle pointer types through wrapper
            using BaseType = std::remove_pointer_t<T>;
            if (CachedTypeRegistry::Instance().IsTypeAcceptable<BaseType>()) {
                // Store as pointer wrapper
                ptrStorage = const_cast<void*>(static_cast<const void*>(value));
                typeInfo = typeid(T).hash_code();
            }
        } else {
            // Use type erasure for types not in variant
            erasedStorage = std::make_shared<TypeHolder<T>>(value);
            typeInfo = typeid(T).hash_code();
        }
    }

    // Get value with type checking
    template<typename T>
    T Get() const {
        // Check if stored in variant
        if constexpr (IsTypeInVariant_v<T, ResourceVariant>) {
            if (auto* ptr = std::get_if<T>(&storage)) {
                return *ptr;
            }
        }

        // Check if stored as pointer
        if constexpr (std::is_pointer_v<T>) {
            if (typeInfo == typeid(T).hash_code() && ptrStorage) {
                return static_cast<T>(ptrStorage);
            }
        }

        // Check type-erased storage
        if (erasedStorage && typeInfo == typeid(T).hash_code()) {
            if (auto* holder = dynamic_cast<TypeHolder<T>*>(erasedStorage.get())) {
                return holder->value;
            }
        }

        return T{};  // Return default if not found
    }

    // Check if holds specific type
    template<typename T>
    bool HoldsType() const {
        if constexpr (IsTypeInVariant_v<T, ResourceVariant>) {
            return std::holds_alternative<T>(storage);
        } else {
            return typeInfo == typeid(T).hash_code();
        }
    }

    bool IsValid() const {
        return !std::holds_alternative<std::monostate>(storage) ||
               erasedStorage != nullptr ||
               ptrStorage != nullptr;
    }

private:
    // Hybrid storage approach
    ResourceVariant storage;  // Existing variant for registered types
    std::shared_ptr<void> erasedStorage;  // Type erasure for new types
    void* ptrStorage = nullptr;  // Pointer storage
    size_t typeInfo = 0;  // Type hash for validation

    // Type erasure holder
    struct TypeHolderBase {
        virtual ~TypeHolderBase() = default;
    };

    template<typename T>
    struct TypeHolder : TypeHolderBase {
        T value;
        explicit TypeHolder(const T& v) : value(v) {}
    };
};

// ============================================================================
// GRADUAL MIGRATION PATH
// ============================================================================

/**
 * Migration strategy:
 *
 * Phase 1 (Current): Add migration wrapper alongside existing system
 * - Existing code continues to work unchanged
 * - New code can use enhanced features
 * - Both systems run in parallel
 *
 * Phase 2: Gradual adoption
 * - Replace ResourceTypeTraits with ResourceTypeTraitsV2 in headers
 * - Update Resource class to use ResourceVariantV3 internally
 * - Add ENABLE_NEW_VALIDATION checks to critical paths
 *
 * Phase 3: Full migration
 * - Replace ResourceVariant with ResourceVariantV3 everywhere
 * - Remove old RESOURCE_TYPE_REGISTRY macro system
 * - Use only cached validation system
 *
 * Phase 4: Cleanup
 * - Remove migration wrapper
 * - Rename V3 classes to final names
 * - Remove backward compatibility code
 */

// Type alias for gradual migration
#ifdef USE_NEW_TYPE_SYSTEM
    using ActiveResourceVariant = ResourceVariantV3;
    template<typename T>
    using ActiveResourceTypeTraits = ResourceTypeTraitsV2<T>;
#else
    using ActiveResourceVariant = ResourceVariant;
    template<typename T>
    using ActiveResourceTypeTraits = ResourceTypeTraits<T>;
#endif

} // namespace Vixen::RenderGraph