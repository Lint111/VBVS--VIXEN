// Example of recursive type validation with compile-time "caching"
#include "Headers.h"
#include <type_traits>
#include <vector>
#include <array>
#include <variant>
#include <iostream>

// Forward declare for recursive checking
template<typename T, typename = void>
struct TypeValidator;

// "Cache" for already validated types - template specializations act as compile-time cache
template<typename T>
struct ValidatedTypeCache {
    static constexpr bool value = false;
};

// Mark base types as validated (our "cache entries")
template<> struct ValidatedTypeCache<VkImage> { static constexpr bool value = true; };
template<> struct ValidatedTypeCache<VkBuffer> { static constexpr bool value = true; };
template<> struct ValidatedTypeCache<VkImageView> { static constexpr bool value = true; };
template<> struct ValidatedTypeCache<VkSwapchainKHR> { static constexpr bool value = true; };
template<> struct ValidatedTypeCache<uint32_t> { static constexpr bool value = true; };
template<> struct ValidatedTypeCache<float> { static constexpr bool value = true; };

// Helper to detect containers
template<typename T> struct IsVector : std::false_type {};
template<typename T, typename A> struct IsVector<std::vector<T, A>> : std::true_type {};

template<typename T> struct IsArray : std::false_type {};
template<typename T, size_t N> struct IsArray<std::array<T, N>> : std::true_type {};

template<typename T> struct IsVariant : std::false_type {};
template<typename... Ts> struct IsVariant<std::variant<Ts...>> : std::true_type {};

// Recursive type validator with memoization
template<typename T, typename Enable>
struct TypeValidator {
    // Check cache first
    static constexpr bool cached = ValidatedTypeCache<T>::value;

    // If not cached, recursively validate
    static constexpr bool value = cached;
};

// Specialization for vectors - recursive validation
template<typename T>
struct TypeValidator<T, std::enable_if_t<IsVector<T>::value>> {
    using ElementType = typename T::value_type;
    // Recursively validate element type
    static constexpr bool value = TypeValidator<ElementType>::value;
};

// Specialization for arrays - recursive validation
template<typename T>
struct TypeValidator<T, std::enable_if_t<IsArray<T>::value>> {
    using ElementType = typename T::value_type;
    // Recursively validate element type
    static constexpr bool value = TypeValidator<ElementType>::value;
};

// Specialization for variants - validate all types
template<typename... Ts>
struct TypeValidator<std::variant<Ts...>, void> {
    // Recursively validate all variant types (fold expression)
    static constexpr bool value = (TypeValidator<Ts>::value && ...);
};

// Compile-time hash for type (simplified - real implementation would be more complex)
template<typename T>
struct TypeHash {
    static constexpr size_t value = typeid(T).hash_code();  // Note: not actually constexpr in standard C++
};

// In practice, we'd use a compile-time string hash of the type name:
template<typename T>
struct CompileTimeTypeHash {
    // This is a simplified version - real implementation would hash the type name
    static constexpr size_t value = sizeof(T) ^ alignof(T);  // Simple compile-time computable hash
};

// Advanced: Composite type validation with "hash-based caching"
template<typename T>
struct AdvancedTypeValidator {
    // Compute a "hash" for the composite type
    static constexpr size_t type_hash = CompileTimeTypeHash<T>::value;

    // Check if this exact composite type was validated before
    // In practice, this would use template specialization as cache
    static constexpr bool is_cached = false;  // Would check specialization

    // If not cached, perform recursive validation
    static constexpr bool is_valid = TypeValidator<T>::value;

    // "Cache" the result (would be done via specialization)
    static constexpr bool value = is_valid;
};

// Example of how complex nested types are validated recursively
template<typename T>
void test_type() {
    std::cout << "Type: " << typeid(T).name() << "\n";
    std::cout << "  Valid: " << TypeValidator<T>::value << "\n";
    std::cout << "  Hash: " << CompileTimeTypeHash<T>::value << "\n";
    std::cout << "\n";
}

int main() {
    std::cout << "=== Recursive Type Validation Demo ===\n\n";

    // Test basic types (cached)
    test_type<VkImage>();
    test_type<VkSwapchainKHR>();

    // Test containers (recursive validation)
    test_type<std::vector<VkImage>>();
    test_type<std::array<VkBuffer, 10>>();

    // Test nested containers (deep recursion)
    test_type<std::vector<std::vector<VkImageView>>>();

    // Test variants (validate all types)
    test_type<std::variant<VkImage, VkBuffer, uint32_t>>();

    // Test invalid variant (contains unregistered type)
    struct UnregisteredType {};
    test_type<std::variant<VkImage, UnregisteredType>>();

    // Compile-time assertions
    static_assert(TypeValidator<VkImage>::value, "VkImage should be valid");
    static_assert(TypeValidator<std::vector<VkImage>>::value, "vector<VkImage> should be valid");
    static_assert(TypeValidator<std::variant<VkImage, VkBuffer>>::value, "variant should be valid");
    static_assert(!TypeValidator<UnregisteredType>::value, "UnregisteredType should be invalid");
    static_assert(!TypeValidator<std::vector<UnregisteredType>>::value, "vector<UnregisteredType> should be invalid");

    std::cout << "=== Compile-Time Validation Complete ===\n";
    std::cout << "All recursive type checks performed at compile time!\n";
    std::cout << "Template specializations act as compile-time cache.\n";

    return 0;
}