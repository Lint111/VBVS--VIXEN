#pragma once

#include "TypeValidation.h"
#include "TypeWrappers.h"
#include "ResourceVariantMigration.h"
#include "Data/Core/ResourceTypes.h"
#include "Data/VariantDescriptors.h"

namespace Vixen::RenderGraph {

// ============================================================================
// INTEGRATED RESOURCE CLASS - Uses new type system with backward compatibility
// ============================================================================

/**
 * @brief Enhanced Resource class that integrates the new type system
 *
 * Features:
 * - Cached type validation for performance
 * - Support for wrapper types (RefW, PtrW, VectorW, etc.)
 * - Backward compatible with existing code
 * - Seamless handling of pointers and references
 * - No N×M registry explosion
 */
class ResourceV2 {
public:
    ResourceV2() = default;

    // ========================================================================
    // CREATION API - Compatible with existing Resource class
    // ========================================================================

    /**
     * @brief Create resource with specific type and descriptor
     *
     * Backward compatible with existing Resource::Create<T>()
     */
    template<typename T>
    static ResourceV2 Create(const ResourceDescriptorVariant& descriptor) {
        ResourceV2 res;

        // Use cached validation
        if (!CachedTypeRegistry::Instance().IsTypeAcceptable<T>()) {
            throw std::runtime_error("Type not registered in cached type system");
        }

        // Map to ResourceType (for backward compatibility)
        res.type_ = MapToResourceType<T>();
        res.descriptor_ = descriptor;
        res.lifetime_ = ResourceLifetime::Transient;

        return res;
    }

    /**
     * @brief Create resource from ResourceType enum (runtime dispatch)
     *
     * Maintains compatibility with existing CreateFromType()
     */
    static ResourceV2 CreateFromType(ResourceType type, const ResourceDescriptorVariant& desc) {
        ResourceV2 res;
        res.type_ = type;
        res.descriptor_ = desc;
        res.lifetime_ = ResourceLifetime::Transient;
        return res;
    }

    // ========================================================================
    // HANDLE MANAGEMENT - Enhanced with wrapper support
    // ========================================================================

    /**
     * @brief Set handle value - supports all type patterns
     *
     * Accepts:
     * - Base types (VkImage, VkBuffer)
     * - Pointers (VkImage*, const VkBuffer*)
     * - References (through wrapper RefW<T>)
     * - Vectors (std::vector<VkImage>)
     * - Wrappers (RefW<T>, PtrW<T>, VectorW<T>, etc.)
     * - Composites (PairW<T1, T2>, TupleW<T...>, etc.)
     */
    template<typename T>
    void SetHandle(T&& value) {
        // Use cached validation for performance
        using DecayedT = std::decay_t<T>;
        if (!CachedTypeRegistry::Instance().IsTypeAcceptable<DecayedT>()) {
            throw std::runtime_error("Type not acceptable");
        }

        // Determine storage strategy based on type pattern
        if constexpr (IsWrapper_v<DecayedT>) {
            // Wrapper types - store using type erasure
            SetWrapperHandle(std::forward<T>(value));
        } else if constexpr (std::is_pointer_v<DecayedT>) {
            // Raw pointer - store with validation
            SetPointerHandle(value);
        } else if constexpr (IsTypeInVariant_v<DecayedT, ResourceVariant>) {
            // Type is in old variant - use existing storage
            SetVariantHandle(value);
        } else {
            // Use type erasure for new types
            SetErasedHandle(std::forward<T>(value));
        }

        isSet_ = true;
    }

    /**
     * @brief Get handle value - type-safe retrieval
     *
     * Returns the stored value with proper type checking
     */
    template<typename T>
    T GetHandle() const {
        if (!isSet_) {
            return T{};  // Return default for unset handles
        }

        // Try different storage strategies
        if constexpr (IsWrapper_v<T>) {
            return GetWrapperHandle<T>();
        } else if constexpr (std::is_pointer_v<T>) {
            return GetPointerHandle<T>();
        } else if constexpr (IsTypeInVariant_v<T, ResourceVariant>) {
            return GetVariantHandle<T>();
        } else {
            return GetErasedHandle<T>();
        }
    }

    /**
     * @brief Check if handle is set and valid
     */
    bool IsValid() const { return isSet_; }

    /**
     * @brief Check if handle holds specific type
     */
    template<typename T>
    bool HoldsType() const {
        return isSet_ && typeHash_ == TypeHasher::Hash<T>();
    }

    // ========================================================================
    // RESOURCE METADATA - Compatible with existing Resource class
    // ========================================================================

    ResourceType GetType() const { return type_; }
    ResourceLifetime GetLifetime() const { return lifetime_; }
    void SetLifetime(ResourceLifetime lifetime) { lifetime_ = lifetime; }

    const ResourceDescriptorVariant& GetDescriptor() const { return descriptor_; }

    template<typename DescType>
    const DescType* GetDescriptor() const {
        if (auto* ptr = std::get_if<DescType>(&descriptor_)) {
            return ptr;
        }
        return nullptr;
    }

    template<typename DescType>
    DescType* GetDescriptorMutable() {
        if (auto* ptr = std::get_if<DescType>(&descriptor_)) {
            return ptr;
        }
        return nullptr;
    }

    // ========================================================================
    // MOVE SEMANTICS - Resources are move-only
    // ========================================================================

    ResourceV2(const ResourceV2&) = delete;
    ResourceV2& operator=(const ResourceV2&) = delete;

    ResourceV2(ResourceV2&&) noexcept = default;
    ResourceV2& operator=(ResourceV2&&) noexcept = default;

private:
    // ========================================================================
    // STORAGE IMPLEMENTATION
    // ========================================================================

    /**
     * @brief Multi-tier storage strategy
     *
     * Tier 1: ResourceVariant - for types in old registry (fast path)
     * Tier 2: Type-erased storage - for new types and wrappers
     * Tier 3: Pointer storage - for raw pointers and references
     */

    ResourceVariant variantStorage_;     // Old variant for backward compatibility
    std::shared_ptr<void> erasedStorage_; // Type-erased storage for new types
    void* pointerStorage_ = nullptr;      // Non-owning pointer storage

    // Metadata
    ResourceType type_ = ResourceType::Buffer;
    ResourceLifetime lifetime_ = ResourceLifetime::Transient;
    ResourceDescriptorVariant descriptor_;
    size_t typeHash_ = 0;  // Hash of stored type for validation
    bool isSet_ = false;

    // ========================================================================
    // STORAGE HELPERS
    // ========================================================================

    template<typename T>
    void SetVariantHandle(const T& value) {
        variantStorage_ = value;
        typeHash_ = TypeHasher::Hash<T>();
    }

    template<typename T>
    T GetVariantHandle() const {
        if (auto* ptr = std::get_if<T>(&variantStorage_)) {
            return *ptr;
        }
        return T{};
    }

    template<typename W>
    void SetWrapperHandle(W&& wrapper) {
        // Store wrapper using type erasure
        using WrapperType = std::decay_t<W>;
        erasedStorage_ = std::make_shared<TypeHolder<WrapperType>>(std::forward<W>(wrapper));
        typeHash_ = TypeHasher::Hash<WrapperType>();
    }

    template<typename W>
    W GetWrapperHandle() const {
        if (!erasedStorage_ || typeHash_ != TypeHasher::Hash<W>()) {
            return W{};
        }

        if (auto* holder = static_cast<TypeHolder<W>*>(erasedStorage_.get())) {
            return holder->value;
        }
        return W{};
    }

    template<typename T>
    void SetPointerHandle(T ptr) {
        static_assert(std::is_pointer_v<T>, "SetPointerHandle requires pointer type");
        pointerStorage_ = const_cast<void*>(static_cast<const void*>(ptr));
        typeHash_ = TypeHasher::Hash<T>();
    }

    template<typename T>
    T GetPointerHandle() const {
        static_assert(std::is_pointer_v<T>, "GetPointerHandle requires pointer type");
        if (typeHash_ != TypeHasher::Hash<T>() || !pointerStorage_) {
            return nullptr;
        }
        return static_cast<T>(pointerStorage_);
    }

    template<typename T>
    void SetErasedHandle(T&& value) {
        using ValueType = std::decay_t<T>;
        erasedStorage_ = std::make_shared<TypeHolder<ValueType>>(std::forward<T>(value));
        typeHash_ = TypeHasher::Hash<ValueType>();
    }

    template<typename T>
    T GetErasedHandle() const {
        if (!erasedStorage_ || typeHash_ != TypeHasher::Hash<T>()) {
            return T{};
        }

        if (auto* holder = static_cast<TypeHolder<T>*>(erasedStorage_.get())) {
            return holder->value;
        }
        return T{};
    }

    // Type erasure holder
    struct TypeHolderBase {
        virtual ~TypeHolderBase() = default;
    };

    template<typename T>
    struct TypeHolder : TypeHolderBase {
        T value;
        explicit TypeHolder(T&& v) : value(std::move(v)) {}
        explicit TypeHolder(const T& v) : value(v) {}
    };

    // ========================================================================
    // TYPE MAPPING - Map C++ types to ResourceType enum
    // ========================================================================

    template<typename T>
    static ResourceType MapToResourceType() {
        // Use TypePattern to unwrap wrappers and get base type
        using Pattern = TypePattern<T>;
        using BaseType = typename Pattern::BaseType;

        // Map base types to ResourceType enum
        if constexpr (std::is_same_v<BaseType, VkImage>) {
            return ResourceType::Image;
        } else if constexpr (std::is_same_v<BaseType, VkBuffer>) {
            return ResourceType::Buffer;
        } else {
            // Default fallback
            return ResourceType::Buffer;
        }
    }
};

// ============================================================================
// BACKWARD COMPATIBILITY ADAPTER
// ============================================================================

/**
 * @brief Adapter that allows ResourceV2 to be used as Resource
 *
 * This provides a seamless migration path by allowing code expecting
 * Resource to work with ResourceV2.
 */
class ResourceAdapter {
public:
    explicit ResourceAdapter(ResourceV2& resource) : resource_(resource) {}

    // Provide Resource-like API
    template<typename T>
    void SetHandle(const T& value) {
        resource_.SetHandle(value);
    }

    template<typename T>
    T GetHandle() const {
        return resource_.GetHandle<T>();
    }

    bool IsValid() const {
        return resource_.IsValid();
    }

    ResourceType GetType() const {
        return resource_.GetType();
    }

    ResourceLifetime GetLifetime() const {
        return resource_.GetLifetime();
    }

    void SetLifetime(ResourceLifetime lifetime) {
        resource_.SetLifetime(lifetime);
    }

private:
    ResourceV2& resource_;
};

// ============================================================================
// MIGRATION HELPERS
// ============================================================================

/**
 * @brief Convert existing Resource to ResourceV2
 */
inline ResourceV2 MigrateResource(const Resource& oldResource) {
    ResourceV2 newResource = ResourceV2::CreateFromType(
        oldResource.GetType(),
        ResourceDescriptorVariant{}  // Would need to copy descriptor
    );
    newResource.SetLifetime(oldResource.GetLifetime());
    return newResource;
}

/**
 * @brief Batch migrate resources
 */
inline std::vector<ResourceV2> MigrateResources(const std::vector<Resource>& oldResources) {
    std::vector<ResourceV2> newResources;
    newResources.reserve(oldResources.size());

    for (const auto& res : oldResources) {
        newResources.push_back(MigrateResource(res));
    }

    return newResources;
}

// ============================================================================
// COMPILE-TIME FEATURE FLAGS
// ============================================================================

/**
 * @brief Feature flags for gradual migration
 */
namespace FeatureFlags {
    // Enable new type system features
    constexpr bool ENABLE_WRAPPER_TYPES = true;
    constexpr bool ENABLE_CACHED_VALIDATION = true;
    constexpr bool ENABLE_TYPE_ERASURE = true;

    // Compatibility modes
    constexpr bool MAINTAIN_OLD_VARIANT = true;  // Keep old variant storage
    constexpr bool STRICT_TYPE_CHECKING = false; // Extra validation (slower)
}

// ============================================================================
// USAGE EXAMPLES
// ============================================================================

/**
 * Example: Using new wrapper types
 *
 * // Create resource with reference wrapper
 * auto res = ResourceV2::Create<RefW<VkImage>>(ImageDescriptor{});
 *
 * VkImage image = ...;
 * res.SetHandle(RefW<VkImage>(image));
 *
 * // Retrieve reference
 * VkImage& imgRef = res.GetHandle<RefW<VkImage>>();
 *
 * Example: Using pointer types
 *
 * auto res = ResourceV2::Create<PtrW<VkBuffer>>(BufferDescriptor{});
 *
 * VkBuffer* buffer = ...;
 * res.SetHandle(PtrW<VkBuffer>(buffer));
 *
 * VkBuffer* bufferPtr = res.GetHandle<PtrW<VkBuffer>>();
 *
 * Example: Using composite types
 *
 * using ImageSamplerPair = PairW<VkImage, VkSampler>;
 * auto res = ResourceV2::Create<ImageSamplerPair>(HandleDescriptor{});
 *
 * VkImage img = ...;
 * VkSampler sampler = ...;
 * res.SetHandle(ImageSamplerPair{img, sampler});
 *
 * auto pair = res.GetHandle<ImageSamplerPair>();
 * VkImage retrievedImg = pair.first();
 * VkSampler retrievedSampler = pair.second();
 *
 * Example: Backward compatible usage
 *
 * // Old code using base types still works
 * auto res = ResourceV2::Create<VkImage>(ImageDescriptor{});
 * VkImage image = ...;
 * res.SetHandle(image);
 * VkImage retrieved = res.GetHandle<VkImage>();
 */

} // namespace Vixen::RenderGraph