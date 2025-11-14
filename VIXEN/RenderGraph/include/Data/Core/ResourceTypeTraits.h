#pragma once

#include <vector>
#include <array>
#include <variant>
#include <type_traits>

namespace Vixen::RenderGraph {

// Forward declare ResourceType enum
enum class ResourceType;

// Forward declare ResourceTypeTraitsImpl (defined in ResourceVariant.h after registry expansion)
template<typename T>
struct ResourceTypeTraitsImpl;

// ============================================================================
// TYPE UNWRAPPING - Strip container wrappers to get base type
// ============================================================================

/**
 * @brief Unwrap container types to extract base element type
 *
 * Usage:
 * - StripContainer<VkImage>::Type          → VkImage (identity)
 * - StripContainer<vector<VkImage>>::Type  → VkImage (unwrapped)
 * - StripContainer<array<VkImage, 5>>::Type → VkImage (unwrapped)
 */
template<typename T>
struct StripContainer {
    using Type = T;
    static constexpr bool isContainer = false;
    static constexpr bool isVector = false;
    static constexpr bool isArray = false;
    static constexpr size_t arraySize = 0;
};

// ---------------------------------------------------------------------------
// Normalize pointer cv-qualification of the pointee type
// Example: const Foo* -> Foo*
//          Foo const* -> Foo*
// Non-pointer types map to themselves.
// ---------------------------------------------------------------------------
template<typename T>
struct NormalizePointee {
    using type = T;
    static constexpr bool isPointer = false;
};

template<typename P>
struct NormalizePointee<P*> {
    using type = std::add_pointer_t<std::remove_cv_t<P>>;
    static constexpr bool isPointer = true;
};

template<typename T>
using NormalizePointee_t = typename NormalizePointee<T>::type;

// Specialization for std::vector<T>
template<typename T>
struct StripContainer<std::vector<T>> {
    using Type = T;
    static constexpr bool isContainer = true;
    static constexpr bool isVector = true;
    static constexpr bool isArray = false;
    static constexpr size_t arraySize = 0;  // Dynamic size
};

// Specialization for std::array<T, N>
template<typename T, size_t N>
struct StripContainer<std::array<T, N>> {
    using Type = T;
    static constexpr bool isContainer = true;
    static constexpr bool isVector = false;
    static constexpr bool isArray = true;
    static constexpr size_t arraySize = N;
};

// Specialization for C-style arrays T[]
template<typename T, size_t N>
struct StripContainer<T[N]> {
    using Type = T;
    static constexpr bool isContainer = true;
    static constexpr bool isVector = false;
    static constexpr bool isArray = true;
    static constexpr size_t arraySize = N;
};

// ============================================================================
// VARIANT TYPE CHECKING
// ============================================================================

/**
 * @brief Check if T is std::variant<...>
 */
template<typename T>
struct IsVariant : std::false_type {};

template<typename... Ts>
struct IsVariant<std::variant<Ts...>> : std::true_type {};

template<typename T>
inline constexpr bool IsVariant_v = IsVariant<T>::value;

/**
 * @brief Check if T is contained in variant<Ts...>
 */
template<typename T, typename Variant>
struct IsTypeInVariant;

template<typename T, typename... Ts>
struct IsTypeInVariant<T, std::variant<Ts...>> {
    static constexpr bool value = (std::is_same_v<T, Ts> || ...);
};

template<typename T, typename Variant>
inline constexpr bool IsTypeInVariant_v = IsTypeInVariant<T, Variant>::value;

/**
 * @brief Check if ALL types in a variant are registered
 *
 * For custom variants like std::variant<VkImage, VkBuffer, VkSampler>,
 * validates that every type in the parameter pack is registered.
 *
 * This enables type-safe subset variants of ResourceHandleVariant.
 *
 * Example:
 * - variant<VkImage, VkBuffer> → valid if both registered ✅
 * - variant<VkImage, UnknownType> → invalid (UnknownType not registered) ❌
 */
template<typename Variant>
struct AllVariantTypesRegistered : std::false_type {};

template<typename... Ts>
struct AllVariantTypesRegistered<std::variant<Ts...>> {
    // Forward declare ResourceTypeTraitsImpl to check registration
    template<typename T>
    static constexpr bool IsRegistered() {
        // Will be defined after RESOURCE_TYPE_REGISTRY expansion
        return ResourceTypeTraitsImpl<T>::isValid;
    }

    // Check all types in pack are registered (fold expression)
    static constexpr bool value = (IsRegistered<Ts>() && ...);
};

template<typename Variant>
inline constexpr bool AllVariantTypesRegistered_v = AllVariantTypesRegistered<Variant>::value;

// ============================================================================
// RECURSIVE UNWRAPPING - Handle nested containers
// ============================================================================

/**
 * @brief Recursively unwrap containers to get the innermost base type
 *
 * Examples:
 * - vector<VkImage> → VkImage
 * - vector<vector<VkImage>> → VkImage
 * - array<vector<VkImage>, 10> → VkImage
 */
template<typename T>
struct RecursiveStripContainer {
    using Stripped = typename StripContainer<T>::Type;
    using Type = typename std::conditional_t<
        StripContainer<T>::isContainer,
        RecursiveStripContainer<Stripped>,  // Keep unwrapping
        std::type_identity<T>               // Base case
    >::Type;
};

// ============================================================================
// ENHANCED RESOURCE TYPE TRAITS (Forward declaration)
// ============================================================================

/**
 * @brief Type traits for resource types with automatic array support
 *
 * RULE: If T is registered in RESOURCE_TYPE_REGISTRY, then:
 * - T is valid ✅
 * - vector<T> is valid ✅
 * - array<T, N> is valid ✅
 * - T[] is valid ✅
 *
 * RULE: If T is ResourceHandleVariant (the variant itself):
 * - T is valid ✅
 * - vector<T> is valid ✅
 *
 * This eliminates the need to register both scalar and array forms!
 *
 * Example:
 * // Only register base type:
 * RESOURCE_TYPE(VkImage, ImageDescriptor, ResourceType::Image)
 *
 * // All these automatically work:
 * ResourceTypeTraits<VkImage>::isValid                    → true ✅
 * ResourceTypeTraits<vector<VkImage>>::isValid            → true ✅
 * ResourceTypeTraits<array<VkImage, 10>>::isValid         → true ✅
 * ResourceTypeTraits<VkImage[5]>::isValid                 → true ✅
 */
template<typename T>
struct ResourceTypeTraits;

// ============================================================================
// COMPILE-TIME TYPE CHECKING HELPERS
// ============================================================================

/**
 * @brief Check if type T is acceptable for a resource slot
 *
 * Accepts:
 * - Any type registered in RESOURCE_TYPE_REGISTRY
 * - vector/array of any registered type
 * - ResourceHandleVariant itself
 * - vector/array of ResourceHandleVariant
 */
template<typename T>
inline constexpr bool IsValidResourceType_v = ResourceTypeTraits<T>::isValid;

/**
 * @brief Get base element type (unwrap all containers)
 */
template<typename T>
using BaseResourceType_t = typename RecursiveStripContainer<T>::Type;

/**
 * @brief Check if T is a container of resources
 */
template<typename T>
inline constexpr bool IsResourceContainer_v =
    StripContainer<T>::isContainer &&
    IsValidResourceType_v<typename StripContainer<T>::Type>;

} // namespace Vixen::RenderGraph
