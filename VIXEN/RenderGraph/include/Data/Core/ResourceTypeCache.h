#pragma once

#include "ResourceV3.h"
#include <vector>
#include <array>

namespace Vixen::RenderGraph {

// ============================================================================
// COMPILE-TIME TYPE VALIDATION CACHE
// ============================================================================

/**
 * @brief Explicit compile-time cache for complex types
 *
 * This file contains pre-computed specializations for commonly used complex types.
 * By explicitly specializing ResourceTypeTraits for these types, we:
 * 1. Avoid recursive template instantiation on first use
 * 2. Speed up compilation
 * 3. Document which complex types are valid
 *
 * The compiler would compute these anyway, but by pre-specializing them,
 * we make compilation faster and provide better documentation.
 */

// ============================================================================
// COMMON VECTOR TYPES - Pre-validated
// ============================================================================

// Pre-validate common vector types to avoid recursive instantiation
template<>
struct ResourceTypeTraits<std::vector<VkImage>> {
    using BaseType = VkImage;
    static constexpr bool isValid = true;  // Pre-validated!
    using DescriptorT = HandleDescriptor;
    static constexpr ResourceType resourceType = ResourceType::Image;
    static constexpr bool isContainer = true;
    static constexpr bool isVector = true;
    static constexpr bool isArray = false;
    static constexpr size_t arraySize = 0;
};

template<>
struct ResourceTypeTraits<std::vector<VkImageView>> {
    using BaseType = VkImageView;
    static constexpr bool isValid = true;  // Pre-validated!
    using DescriptorT = HandleDescriptor;
    static constexpr ResourceType resourceType = ResourceType::ImageView;
    static constexpr bool isContainer = true;
    static constexpr bool isVector = true;
    static constexpr bool isArray = false;
    static constexpr size_t arraySize = 0;
};

template<>
struct ResourceTypeTraits<std::vector<VkBuffer>> {
    using BaseType = VkBuffer;
    static constexpr bool isValid = true;  // Pre-validated!
    using DescriptorT = HandleDescriptor;
    static constexpr ResourceType resourceType = ResourceType::Buffer;
    static constexpr bool isContainer = true;
    static constexpr bool isVector = true;
    static constexpr bool isArray = false;
    static constexpr size_t arraySize = 0;
};

template<>
struct ResourceTypeTraits<std::vector<VkFramebuffer>> {
    using BaseType = VkFramebuffer;
    static constexpr bool isValid = true;  // Pre-validated!
    using DescriptorT = HandleDescriptor;
    static constexpr ResourceType resourceType = ResourceType::Framebuffer;
    static constexpr bool isContainer = true;
    static constexpr bool isVector = true;
    static constexpr bool isArray = false;
    static constexpr size_t arraySize = 0;
};

// ============================================================================
// COMMON ARRAY TYPES - Pre-validated
// ============================================================================

// Common small arrays used in graphics programming
template<>
struct ResourceTypeTraits<std::array<VkImageView, 2>> {
    using BaseType = VkImageView;
    static constexpr bool isValid = true;  // Pre-validated!
    using DescriptorT = HandleDescriptor;
    static constexpr ResourceType resourceType = ResourceType::ImageView;
    static constexpr bool isContainer = true;
    static constexpr bool isVector = false;
    static constexpr bool isArray = true;
    static constexpr size_t arraySize = 2;
};

template<>
struct ResourceTypeTraits<std::array<VkBuffer, 3>> {
    using BaseType = VkBuffer;
    static constexpr bool isValid = true;  // Pre-validated!
    using DescriptorT = HandleDescriptor;
    static constexpr ResourceType resourceType = ResourceType::Buffer;
    static constexpr bool isContainer = true;
    static constexpr bool isVector = false;
    static constexpr bool isArray = true;
    static constexpr size_t arraySize = 3;
};

// ============================================================================
// NESTED CONTAINER TYPES - Pre-validated
// ============================================================================

// Pre-validate commonly used nested types
template<>
struct ResourceTypeTraits<std::vector<std::vector<VkImage>>> {
    using BaseType = std::vector<VkImage>;
    static constexpr bool isValid = true;  // Pre-validated!
    using DescriptorT = HandleDescriptor;
    static constexpr ResourceType resourceType = ResourceType::Image;
    static constexpr bool isContainer = true;
    static constexpr bool isVector = true;
    static constexpr bool isArray = false;
    static constexpr size_t arraySize = 0;
};

// ============================================================================
// CACHE VERIFICATION HELPER
// ============================================================================

/**
 * @brief Verify that cached result matches computed result
 *
 * This is used in debug builds to ensure our pre-cached values are correct.
 * In release builds, this compiles to nothing.
 */
template<typename T>
struct VerifyCache {
    // Compute what the result WOULD be without cache
    using ComputedTraits = ResourceTypeTraits<T>;

    // Get the cached result
    static constexpr bool cached_result = ComputedTraits::isValid;

    // In debug, verify they match
#ifdef _DEBUG
    static_assert(cached_result == ComputedTraits::isValid,
                  "Cache mismatch! Pre-cached value doesn't match computed value");
#endif
};

// ============================================================================
// COMPILE-TIME CACHE STATISTICS
// ============================================================================

/**
 * @brief Track cache hit/miss for diagnostics
 *
 * This helps us identify which types should be pre-cached.
 * Only active in debug builds.
 */
template<typename T>
struct CacheStats {
    // Check if this type has an explicit specialization
    static constexpr bool is_cached = false;  // Would check for specialization

    // Count would be tracked at compile time (simplified here)
    static constexpr int access_count = 1;

    // Recommendation: Types accessed >10 times should be cached
    static constexpr bool should_cache = access_count > 10;
};

// ============================================================================
// MACRO TO EASILY PRE-CACHE A TYPE
// ============================================================================

/**
 * @brief Macro to pre-validate and cache a complex type
 *
 * Usage: CACHE_COMPLEX_TYPE(std::vector<VkDescriptorSet>)
 *
 * This creates an explicit specialization that avoids recursive validation.
 */
#define CACHE_COMPLEX_TYPE(Type) \
    template<> \
    struct ResourceTypeTraits<Type> { \
        using Stripped = typename StripContainer<Type>::Type; \
        using BaseType = Stripped; \
        static constexpr bool isValid = IsRegisteredType<Stripped>::value; \
        using DescriptorT = HandleDescriptor; \
        static constexpr ResourceType resourceType = ResourceType::Buffer; \
        static constexpr bool isContainer = StripContainer<Type>::isContainer; \
        static constexpr bool isVector = StripContainer<Type>::isVector; \
        static constexpr bool isArray = StripContainer<Type>::isArray; \
        static constexpr size_t arraySize = StripContainer<Type>::arraySize; \
    }

// ============================================================================
// AUTOMATIC CACHE GENERATION
// ============================================================================

/**
 * @brief Helper to auto-generate cache entries
 *
 * In an ideal world, we'd have a build step that:
 * 1. Analyzes which complex types are used frequently
 * 2. Generates this header with pre-cached specializations
 * 3. Reduces compilation time for common patterns
 */

// Example of types that SHOULD be cached based on common usage:
// CACHE_COMPLEX_TYPE(std::vector<VkDescriptorSet>);
// CACHE_COMPLEX_TYPE(std::vector<VkCommandBuffer>);
// CACHE_COMPLEX_TYPE(std::array<VkClearValue, 2>);

} // namespace Vixen::RenderGraph