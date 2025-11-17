#pragma once

#include <type_traits>
#include <variant>
#include <vector>
#include <array>

namespace Vixen::RenderGraph {

// ============================================================================
// RECURSIVE TYPE VALIDATION WITH COMPILE-TIME CACHING
// ============================================================================

/**
 * @brief Compile-time recursive type validator with memoization
 *
 * This system performs recursive validation of complex types at compile time.
 * Template specializations act as a compile-time "cache" - once a type is
 * validated, the result is memoized via specialization.
 *
 * The "expensive" validation happens ONCE at compile time when the template
 * is first instantiated. Subsequent uses of the same type reuse the result.
 */

// Forward declaration
template<typename T, typename Enable = void>
struct RecursiveTypeValidator;

// ============================================================================
// LEVEL 1: Direct Type Registry (Base Cache)
// ============================================================================

// All registered types are explicitly specialized - this is our "base cache"
template<typename T>
struct IsDirectlyRegistered : std::false_type {};

// Macro to register a type (creates compile-time cache entry)
#define REGISTER_TYPE(Type) \
    template<> struct IsDirectlyRegistered<Type> : std::true_type {}

// Register Vulkan types
REGISTER_TYPE(VkImage);
REGISTER_TYPE(VkBuffer);
REGISTER_TYPE(VkImageView);
REGISTER_TYPE(VkSampler);
REGISTER_TYPE(VkSwapchainKHR);
REGISTER_TYPE(VkRenderPass);
REGISTER_TYPE(VkFramebuffer);
REGISTER_TYPE(VkPipeline);
REGISTER_TYPE(VkPipelineLayout);
REGISTER_TYPE(VkDescriptorSet);
REGISTER_TYPE(VkDescriptorSetLayout);
REGISTER_TYPE(VkCommandBuffer);
REGISTER_TYPE(VkQueue);
REGISTER_TYPE(VkDevice);
REGISTER_TYPE(VkInstance);

// Register basic types
REGISTER_TYPE(uint32_t);
REGISTER_TYPE(uint64_t);
REGISTER_TYPE(float);
REGISTER_TYPE(double);
REGISTER_TYPE(bool);

// ============================================================================
// LEVEL 2: Container Detection and Unwrapping
// ============================================================================

// Helper to detect and unwrap std::vector
template<typename T>
struct UnwrapVector {
    static constexpr bool is_vector = false;
    using type = T;
};

template<typename T, typename Alloc>
struct UnwrapVector<std::vector<T, Alloc>> {
    static constexpr bool is_vector = true;
    using type = T;
};

// Helper to detect and unwrap std::array
template<typename T>
struct UnwrapArray {
    static constexpr bool is_array = false;
    using type = T;
    static constexpr size_t size = 0;
};

template<typename T, size_t N>
struct UnwrapArray<std::array<T, N>> {
    static constexpr bool is_array = true;
    using type = T;
    static constexpr size_t size = N;
};

// Helper to detect std::variant
template<typename T>
struct IsVariantType : std::false_type {};

template<typename... Ts>
struct IsVariantType<std::variant<Ts...>> : std::true_type {};

// ============================================================================
// LEVEL 3: Recursive Validation Logic
// ============================================================================

// Primary template - for non-container, non-variant types
template<typename T, typename Enable>
struct RecursiveTypeValidator {
    // Step 1: Check if directly registered (base case)
    static constexpr bool is_directly_registered = IsDirectlyRegistered<T>::value;

    // Step 2: If it's a pointer type (Vulkan handles), check the type itself
    // (We removed pointer decomposition to handle Vulkan handles correctly)
    static constexpr bool value = is_directly_registered;

    // Metadata for debugging
    static constexpr bool is_container = false;
    static constexpr bool is_variant = false;
    static constexpr const char* validation_path = "direct";
};

// Specialization for std::vector - recursive validation
template<typename T>
struct RecursiveTypeValidator<T, std::enable_if_t<UnwrapVector<T>::is_vector>> {
    using ElementType = typename UnwrapVector<T>::type;

    // Recursively validate element type
    static constexpr bool element_valid = RecursiveTypeValidator<ElementType>::value;

    static constexpr bool value = element_valid;

    // Metadata
    static constexpr bool is_container = true;
    static constexpr bool is_variant = false;
    static constexpr const char* validation_path = "vector->element";
};

// Specialization for std::array - recursive validation
template<typename T>
struct RecursiveTypeValidator<T, std::enable_if_t<UnwrapArray<T>::is_array>> {
    using ElementType = typename UnwrapArray<T>::type;

    // Recursively validate element type
    static constexpr bool element_valid = RecursiveTypeValidator<ElementType>::value;

    static constexpr bool value = element_valid;

    // Metadata
    static constexpr bool is_container = true;
    static constexpr bool is_variant = false;
    static constexpr const char* validation_path = "array->element";
};

// Specialization for std::variant - validate ALL types
template<typename... Ts>
struct RecursiveTypeValidator<std::variant<Ts...>, void> {
    // Recursively validate all types in variant (fold expression)
    static constexpr bool all_types_valid = (RecursiveTypeValidator<Ts>::value && ...);

    static constexpr bool value = all_types_valid;

    // Metadata
    static constexpr bool is_container = false;
    static constexpr bool is_variant = true;
    static constexpr const char* validation_path = "variant->all_types";
};

// ============================================================================
// LEVEL 4: Compile-Time Type Hash (for composite type caching)
// ============================================================================

/**
 * @brief Compile-time type hash for memoization
 *
 * In a real implementation, this would compute a hash of the type's
 * structure to create a unique identifier for composite types.
 * This allows us to "cache" validation results for complex types.
 */
template<typename T>
struct CompileTimeTypeHash {
    // Simplified hash combining size, alignment, and type traits
    static constexpr size_t base_hash = sizeof(T) ^ (alignof(T) << 8);

    // Include container/variant status in hash
    static constexpr size_t container_hash =
        UnwrapVector<T>::is_vector ? 0x1000 :
        UnwrapArray<T>::is_array ? 0x2000 :
        IsVariantType<T>::value ? 0x3000 : 0;

    static constexpr size_t value = base_hash ^ container_hash;
};

// ============================================================================
// LEVEL 5: Validation Result Cache (via Template Specialization)
// ============================================================================

/**
 * @brief Cache for validation results
 *
 * Template specializations act as a compile-time cache.
 * Once a complex type is validated, we can specialize this
 * template to "cache" the result.
 */
template<size_t Hash, typename T>
struct ValidationCache {
    static constexpr bool is_cached = false;
    static constexpr bool cached_result = false;
};

// Example: Pre-cache common complex types
template<>
struct ValidationCache<CompileTimeTypeHash<std::vector<VkImage>>::value, std::vector<VkImage>> {
    static constexpr bool is_cached = true;
    static constexpr bool cached_result = true;  // Pre-validated
};

// ============================================================================
// MAIN VALIDATION INTERFACE
// ============================================================================

/**
 * @brief Main type validation interface with caching
 *
 * This checks the cache first, then performs recursive validation if needed.
 * The result is compile-time constant.
 */
template<typename T>
struct ValidateType {
    // Compute hash for this type
    static constexpr size_t type_hash = CompileTimeTypeHash<T>::value;

    // Check cache first
    static constexpr bool is_cached = ValidationCache<type_hash, T>::is_cached;
    static constexpr bool cached_result = ValidationCache<type_hash, T>::cached_result;

    // If not cached, perform recursive validation
    static constexpr bool computed_result = RecursiveTypeValidator<T>::value;

    // Final result (use cached if available, otherwise compute)
    static constexpr bool value = is_cached ? cached_result : computed_result;

    // Debug info
    static constexpr const char* validation_method = is_cached ? "cached" : "computed";
};

// Convenience alias
template<typename T>
inline constexpr bool ValidateType_v = ValidateType<T>::value;

} // namespace Vixen::RenderGraph