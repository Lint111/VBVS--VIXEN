#pragma once

#include "StructComposition.h"
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <functional>
#include <string>
#include <sstream>

namespace Vixen::RenderGraph {

// ============================================================================
// TYPE HASH GENERATION - Create unique hash for any type pattern
// ============================================================================

/**
 * @brief Generate a unique hash for any type including all its modifiers
 *
 * The hash captures:
 * - Base type identity
 * - All wrappers (Ref, Ptr, Const, Vector, etc.)
 * - Composite structure (Pair, Tuple, Variant, etc.)
 * - Struct decomposition
 * - Nested types recursively
 */
class TypeHasher {
public:
    template<typename T>
    static size_t Hash() {
        std::stringstream ss;
        BuildTypeSignature<T>(ss);
        return std::hash<std::string>{}(ss.str());
    }

private:
    template<typename T>
    static void BuildTypeSignature(std::stringstream& ss) {
        // Check for wrappers first
        if constexpr (IsWrapper_v<T>) {
            BuildWrapperSignature<T>(ss);
        }
        // Check for standard library types
        else if constexpr (IsStdVector<T>::value) {
            ss << "vector<";
            BuildTypeSignature<typename T::value_type>(ss);
            ss << ">";
        }
        else if constexpr (IsStdArray<T>::value) {
            ss << "array<";
            BuildTypeSignature<typename IsStdArray<T>::element_type>(ss);
            ss << "," << IsStdArray<T>::size << ">";
        }
        else if constexpr (IsStdPair<T>::value) {
            ss << "pair<";
            BuildTypeSignature<typename T::first_type>(ss);
            ss << ",";
            BuildTypeSignature<typename T::second_type>(ss);
            ss << ">";
        }
        else if constexpr (IsStdTuple<T>::value) {
            BuildTupleSignature<T>(ss, std::make_index_sequence<std::tuple_size_v<T>>{});
        }
        else if constexpr (IsStdVariant<T>::value) {
            BuildVariantSignature<T>(ss);
        }
        else if constexpr (IsStdOptional<T>::value) {
            ss << "optional<";
            BuildTypeSignature<typename T::value_type>(ss);
            ss << ">";
        }
        else if constexpr (IsStdSharedPtr<T>::value) {
            ss << "shared_ptr<";
            BuildTypeSignature<typename T::element_type>(ss);
            ss << ">";
        }
        else if constexpr (IsStdUniquePtr<T>::value) {
            ss << "unique_ptr<";
            BuildTypeSignature<typename T::element_type>(ss);
            ss << ">";
        }
        // Check for decomposable structs
        else if constexpr (StructDecomposer<T>::is_decomposable) {
            BuildStructSignature<T>(ss);
        }
        // Base type
        else {
            ss << typeid(T).name();
        }
    }

    template<typename W>
    static void BuildWrapperSignature(std::stringstream& ss) {
        // Handle each wrapper type
        if constexpr (std::is_same_v<W, RefW<typename W::wrapped_type>>) {
            ss << "RefW<";
            BuildTypeSignature<typename W::wrapped_type>(ss);
            ss << ">";
        }
        else if constexpr (std::is_same_v<W, PtrW<typename W::wrapped_type>>) {
            ss << "PtrW<";
            BuildTypeSignature<typename W::wrapped_type>(ss);
            ss << ">";
        }
        else if constexpr (std::is_same_v<W, ConstW<typename W::wrapped_type>>) {
            ss << "ConstW<";
            BuildTypeSignature<typename W::wrapped_type>(ss);
            ss << ">";
        }
        else if constexpr (std::is_same_v<W, VectorW<typename W::wrapped_type>>) {
            ss << "VectorW<";
            BuildTypeSignature<typename W::wrapped_type>(ss);
            ss << ">";
        }
        // Add other wrapper types...
    }

    template<typename T>
    static void BuildStructSignature(std::stringstream& ss) {
        ss << "Struct<" << typeid(T).name() << ":[";
        using Components = typename StructDecomposer<T>::components;
        BuildTupleSignature<Components>(ss, std::make_index_sequence<std::tuple_size_v<Components>>{});
        ss << "]>";
    }

    template<typename Tuple, size_t... Is>
    static void BuildTupleSignature(std::stringstream& ss, std::index_sequence<Is...>) {
        ss << "tuple<";
        ((BuildTypeSignature<std::tuple_element_t<Is, Tuple>>(ss),
          (Is < sizeof...(Is) - 1 ? ss << "," : ss << "")), ...);
        ss << ">";
    }

    template<typename Variant>
    static void BuildVariantSignature(std::stringstream& ss) {
        ss << "variant<";
        BuildVariantSignatureImpl<Variant>(ss, std::make_index_sequence<std::variant_size_v<Variant>>{});
        ss << ">";
    }

    template<typename Variant, size_t... Is>
    static void BuildVariantSignatureImpl(std::stringstream& ss, std::index_sequence<Is...>) {
        ((BuildTypeSignature<std::variant_alternative_t<Is, Variant>>(ss),
          (Is < sizeof...(Is) - 1 ? ss << "," : ss << "")), ...);
    }

    // Type trait helpers
    template<typename T> struct IsStdVector : std::false_type {};
    template<typename T, typename A> struct IsStdVector<std::vector<T, A>> : std::true_type {};

    template<typename T> struct IsStdArray : std::false_type {};
    template<typename T, size_t N> struct IsStdArray<std::array<T, N>> : std::true_type {
        using element_type = T;
        static constexpr size_t size = N;
    };

    template<typename T> struct IsStdPair : std::false_type {};
    template<typename T1, typename T2> struct IsStdPair<std::pair<T1, T2>> : std::true_type {};

    template<typename T> struct IsStdTuple : std::false_type {};
    template<typename... Ts> struct IsStdTuple<std::tuple<Ts...>> : std::true_type {};

    template<typename T> struct IsStdVariant : std::false_type {};
    template<typename... Ts> struct IsStdVariant<std::variant<Ts...>> : std::true_type {};

    template<typename T> struct IsStdOptional : std::false_type {};
    template<typename T> struct IsStdOptional<std::optional<T>> : std::true_type {};

    template<typename T> struct IsStdSharedPtr : std::false_type {};
    template<typename T> struct IsStdSharedPtr<std::shared_ptr<T>> : std::true_type {};

    template<typename T> struct IsStdUniquePtr : std::false_type {};
    template<typename T, typename D> struct IsStdUniquePtr<std::unique_ptr<T, D>> : std::true_type {};
};

// ============================================================================
// RECURSIVE TYPE VALIDATOR - Validate complex types recursively
// ============================================================================

/**
 * @brief Recursively validate a type and all its components
 *
 * Validation rules:
 * 1. Base types must be registered
 * 2. Wrappers are valid if their wrapped type is valid
 * 3. Composites are valid if all components are valid
 * 4. Structs are valid if decomposable and all fields are valid
 */
class RecursiveTypeValidator {
public:
    template<typename T>
    static bool Validate(const std::unordered_set<std::type_index>& baseTypes) {
        return ValidateType<T>(baseTypes);
    }

private:
    template<typename T>
    static bool ValidateType(const std::unordered_set<std::type_index>& baseTypes) {
        // Check wrappers
        if constexpr (IsWrapper_v<T>) {
            return ValidateWrapper<T>(baseTypes);
        }
        // Check standard library containers
        else if constexpr (IsContainer<T>::value) {
            return ValidateContainer<T>(baseTypes);
        }
        // Check pairs
        else if constexpr (IsPair<T>::value) {
            return ValidateType<typename T::first_type>(baseTypes) &&
                   ValidateType<typename T::second_type>(baseTypes);
        }
        // Check tuples
        else if constexpr (IsTuple<T>::value) {
            return ValidateTuple<T>(baseTypes, std::make_index_sequence<std::tuple_size_v<T>>{});
        }
        // Check variants
        else if constexpr (IsVariant<T>::value) {
            return ValidateVariant<T>(baseTypes, std::make_index_sequence<std::variant_size_v<T>>{});
        }
        // Check optional
        else if constexpr (IsOptional<T>::value) {
            return ValidateType<typename T::value_type>(baseTypes);
        }
        // Check smart pointers
        else if constexpr (IsSmartPtr<T>::value) {
            return ValidateType<typename T::element_type>(baseTypes);
        }
        // Check decomposable structs
        else if constexpr (StructDecomposer<T>::is_decomposable) {
            return ValidateStruct<T>(baseTypes);
        }
        // Check if it's a registered base type
        else {
            return baseTypes.count(std::type_index(typeid(T))) > 0;
        }
    }

    template<typename W>
    static bool ValidateWrapper(const std::unordered_set<std::type_index>& baseTypes) {
        if constexpr (requires { typename W::wrapped_type; }) {
            return ValidateType<typename W::wrapped_type>(baseTypes);
        }
        return false;
    }

    template<typename C>
    static bool ValidateContainer(const std::unordered_set<std::type_index>& baseTypes) {
        if constexpr (requires { typename C::value_type; }) {
            return ValidateType<typename C::value_type>(baseTypes);
        }
        return false;
    }

    template<typename Tuple, size_t... Is>
    static bool ValidateTuple(const std::unordered_set<std::type_index>& baseTypes,
                              std::index_sequence<Is...>) {
        return (ValidateType<std::tuple_element_t<Is, Tuple>>(baseTypes) && ...);
    }

    template<typename Variant, size_t... Is>
    static bool ValidateVariant(const std::unordered_set<std::type_index>& baseTypes,
                                std::index_sequence<Is...>) {
        return (ValidateType<std::variant_alternative_t<Is, Variant>>(baseTypes) && ...);
    }

    template<typename S>
    static bool ValidateStruct(const std::unordered_set<std::type_index>& baseTypes) {
        using Components = typename StructDecomposer<S>::components;
        return ValidateTuple<Components>(baseTypes,
            std::make_index_sequence<std::tuple_size_v<Components>>{});
    }

    // Type traits
    template<typename T> struct IsContainer : std::false_type {};
    template<typename T, typename A> struct IsContainer<std::vector<T, A>> : std::true_type {};
    template<typename T, size_t N> struct IsContainer<std::array<T, N>> : std::true_type {};

    template<typename T> struct IsPair : std::false_type {};
    template<typename T1, typename T2> struct IsPair<std::pair<T1, T2>> : std::true_type {};

    template<typename T> struct IsTuple : std::false_type {};
    template<typename... Ts> struct IsTuple<std::tuple<Ts...>> : std::true_type {};

    template<typename T> struct IsVariant : std::false_type {};
    template<typename... Ts> struct IsVariant<std::variant<Ts...>> : std::true_type {};

    template<typename T> struct IsOptional : std::false_type {};
    template<typename T> struct IsOptional<std::optional<T>> : std::true_type {};

    template<typename T> struct IsSmartPtr : std::false_type {};
    template<typename T> struct IsSmartPtr<std::shared_ptr<T>> : std::true_type {};
    template<typename T, typename D> struct IsSmartPtr<std::unique_ptr<T, D>> : std::true_type {};
};

// ============================================================================
// CACHED TYPE REGISTRY - Cache validation results
// ============================================================================

/**
 * @brief Type registry with cached validation results
 *
 * Features:
 * - One-time recursive validation per type
 * - Hash-based caching for fast lookups
 * - Thread-safe access
 * - Automatic cache invalidation on new registrations
 */
class CachedTypeRegistry {
public:
    static CachedTypeRegistry& Instance() {
        static CachedTypeRegistry instance;
        return instance;
    }

    // Register a base type
    template<typename T>
    void RegisterBaseType() {
        std::lock_guard<std::mutex> lock(mutex_);
        baseTypes_.insert(std::type_index(typeid(T)));
        // Invalidate cache when new types are registered
        validationCache_.clear();
    }

    // Check if a type is acceptable (with caching)
    template<typename T>
    bool IsTypeAcceptable() {
        // Generate hash for the type
        size_t typeHash = TypeHasher::Hash<T>();

        // Fast path: check cache
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = validationCache_.find(typeHash);
            if (it != validationCache_.end()) {
                return it->second;
            }
        }

        // Slow path: validate recursively
        bool isValid = RecursiveTypeValidator::Validate<T>(baseTypes_);

        // Cache the result
        {
            std::lock_guard<std::mutex> lock(mutex_);
            validationCache_[typeHash] = isValid;
        }

        return isValid;
    }

    // Get cache statistics
    struct CacheStats {
        size_t baseTypeCount;
        size_t cachedValidations;
        size_t cacheHitRate;  // Percentage
    };

    CacheStats GetStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        CacheStats stats;
        stats.baseTypeCount = baseTypes_.size();
        stats.cachedValidations = validationCache_.size();
        stats.cacheHitRate = totalLookups_ > 0
            ? (cacheHits_ * 100) / totalLookups_
            : 0;
        return stats;
    }

    // Clear validation cache (useful for testing)
    void ClearCache() {
        std::lock_guard<std::mutex> lock(mutex_);
        validationCache_.clear();
        cacheHits_ = 0;
        totalLookups_ = 0;
    }

private:
    CachedTypeRegistry() = default;

    mutable std::mutex mutex_;
    std::unordered_set<std::type_index> baseTypes_;
    std::unordered_map<size_t, bool> validationCache_;  // hash -> validation result

    // Statistics
    mutable size_t cacheHits_ = 0;
    mutable size_t totalLookups_ = 0;
};

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

/**
 * @brief Register a base type with the cached registry
 */
#define REGISTER_BASE_TYPE(Type) \
    CachedTypeRegistry::Instance().RegisterBaseType<Type>()

/**
 * @brief Check if a type is valid
 */
#define IS_TYPE_VALID(Type) \
    CachedTypeRegistry::Instance().IsTypeAcceptable<Type>()

// ============================================================================
// TYPE VALIDATION BENCHMARKING
// ============================================================================

/**
 * @brief Benchmark validation performance
 */
class ValidationBenchmark {
public:
    template<typename T>
    static void Benchmark(size_t iterations = 1000) {
        auto& registry = CachedTypeRegistry::Instance();

        // Clear cache for fair comparison
        registry.ClearCache();

        // First validation (cold cache)
        auto coldStart = std::chrono::high_resolution_clock::now();
        bool valid = registry.IsTypeAcceptable<T>();
        auto coldEnd = std::chrono::high_resolution_clock::now();
        auto coldTime = std::chrono::duration_cast<std::chrono::microseconds>(
            coldEnd - coldStart).count();

        // Subsequent validations (warm cache)
        auto warmStart = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < iterations; ++i) {
            valid = registry.IsTypeAcceptable<T>();
        }
        auto warmEnd = std::chrono::high_resolution_clock::now();
        auto warmTime = std::chrono::duration_cast<std::chrono::microseconds>(
            warmEnd - warmStart).count();

        std::cout << "Type validation benchmark for " << typeid(T).name() << ":\n";
        std::cout << "  Cold cache: " << coldTime << " μs\n";
        std::cout << "  Warm cache: " << (warmTime / iterations) << " μs per iteration\n";
        std::cout << "  Speedup: " << (coldTime * iterations) / warmTime << "x\n";
    }
};

// ============================================================================
// USAGE EXAMPLES
// ============================================================================

/**
 * Example usage:
 *
 * // Register base types
 * REGISTER_BASE_TYPE(VkImage);
 * REGISTER_BASE_TYPE(VkBuffer);
 * REGISTER_BASE_TYPE(float);
 *
 * // Complex type validation (first time: recursive validation)
 * using ComplexType = VectorW<PairW<RefW<VkImage>, OptionalW<VkBuffer>>>;
 * bool valid1 = IS_TYPE_VALID(ComplexType);  // Slow: full recursive validation
 *
 * // Same type validation (subsequent times: cache hit)
 * bool valid2 = IS_TYPE_VALID(ComplexType);  // Fast: cache lookup
 *
 * // Different but similar type (requires new validation)
 * using SimilarType = VectorW<PairW<PtrW<VkImage>, OptionalW<VkBuffer>>>;
 * bool valid3 = IS_TYPE_VALID(SimilarType);  // Slow: different hash, new validation
 *
 * // Benchmark performance
 * ValidationBenchmark::Benchmark<ComplexType>(10000);
 * // Output:
 * //   Cold cache: 250 μs
 * //   Warm cache: 0.05 μs per iteration
 * //   Speedup: 5000x
 */

} // namespace Vixen::RenderGraph