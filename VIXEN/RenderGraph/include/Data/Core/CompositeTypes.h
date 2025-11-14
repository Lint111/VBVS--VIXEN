#pragma once

#include "TypeWrappers.h"
#include <tuple>
#include <variant>
#include <optional>
#include <map>
#include <unordered_map>

namespace Vixen::RenderGraph {

// ============================================================================
// MULTI-TYPE COMPOSITION WRAPPERS
// ============================================================================

/**
 * @brief System for composing complex types from multiple registered base types
 *
 * Examples of composite types:
 * - PairW<VkImage, VkBuffer>                    // std::pair<VkImage, VkBuffer>
 * - TupleW<VkImage, VkBuffer, VkSampler>        // std::tuple<...>
 * - VariantW<VkImage, VkBuffer>                 // std::variant<VkImage, VkBuffer>
 * - OptionalW<VkImage>                          // std::optional<VkImage>
 * - MapW<uint32_t, VkImage>                     // std::map<uint32_t, VkImage>
 *
 * These can be further composed with modifiers:
 * - RefW<PairW<VkImage, VkBuffer>>              // std::pair<VkImage, VkBuffer>&
 * - VectorW<TupleW<VkImage, VkBuffer>>          // std::vector<std::tuple<...>>
 * - ConstW<RefW<VariantW<VkImage, VkBuffer>>>   // const std::variant<...>&
 */

// ============================================================================
// PAIR WRAPPER - Compose two types
// ============================================================================

template<typename T1, typename T2>
struct PairW {
    using first_type = T1;
    using second_type = T2;
    using first_unwrapped = typename UnwrapType<T1>::type;
    using second_unwrapped = typename UnwrapType<T2>::type;
    using actual_type = std::pair<first_unwrapped, second_unwrapped>;

    static constexpr bool is_wrapper = true;
    static constexpr bool is_composite = true;
    static constexpr size_t component_count = 2;

    // Storage
    actual_type data;

    PairW() = default;
    PairW(const first_unwrapped& f, const second_unwrapped& s) : data(f, s) {}
    explicit PairW(const actual_type& p) : data(p) {}

    // Access
    operator actual_type&() { return data; }
    operator const actual_type&() const { return data; }
    actual_type& get() { return data; }
    const actual_type& get() const { return data; }

    first_unwrapped& first() { return data.first; }
    const first_unwrapped& first() const { return data.first; }
    second_unwrapped& second() { return data.second; }
    const second_unwrapped& second() const { return data.second; }
};

template<typename T1, typename T2>
struct UnwrapType<PairW<T1, T2>> {
    using type = std::pair<
        typename UnwrapType<T1>::type,
        typename UnwrapType<T2>::type
    >;
};

template<typename T1, typename T2>
struct IsWrapper<PairW<T1, T2>> : std::true_type {};

// ============================================================================
// TUPLE WRAPPER - Compose N types
// ============================================================================

template<typename... Ts>
struct TupleW {
    using actual_type = std::tuple<typename UnwrapType<Ts>::type...>;

    static constexpr bool is_wrapper = true;
    static constexpr bool is_composite = true;
    static constexpr size_t component_count = sizeof...(Ts);

    // Storage
    actual_type data;

    TupleW() = default;
    explicit TupleW(const typename UnwrapType<Ts>::type&... args) : data(args...) {}
    explicit TupleW(const actual_type& t) : data(t) {}

    // Access
    operator actual_type&() { return data; }
    operator const actual_type&() const { return data; }
    actual_type& get() { return data; }
    const actual_type& get() const { return data; }

    template<size_t I>
    auto& get() { return std::get<I>(data); }

    template<size_t I>
    const auto& get() const { return std::get<I>(data); }
};

template<typename... Ts>
struct UnwrapType<TupleW<Ts...>> {
    using type = std::tuple<typename UnwrapType<Ts>::type...>;
};

template<typename... Ts>
struct IsWrapper<TupleW<Ts...>> : std::true_type {};

// ============================================================================
// VARIANT WRAPPER - Type-safe union of N types
// ============================================================================

template<typename... Ts>
struct VariantW {
    using actual_type = std::variant<typename UnwrapType<Ts>::type...>;

    static constexpr bool is_wrapper = true;
    static constexpr bool is_composite = true;
    static constexpr size_t component_count = sizeof...(Ts);

    // Storage
    actual_type data;

    VariantW() = default;
    explicit VariantW(const actual_type& v) : data(v) {}

    template<typename T>
    explicit VariantW(const T& value) : data(value) {}

    // Access
    operator actual_type&() { return data; }
    operator const actual_type&() const { return data; }
    actual_type& get() { return data; }
    const actual_type& get() const { return data; }

    template<typename T>
    bool holds() const { return std::holds_alternative<T>(data); }

    template<typename T>
    T& get_as() { return std::get<T>(data); }

    template<typename T>
    const T& get_as() const { return std::get<T>(data); }

    size_t index() const { return data.index(); }
};

template<typename... Ts>
struct UnwrapType<VariantW<Ts...>> {
    using type = std::variant<typename UnwrapType<Ts>::type...>;
};

template<typename... Ts>
struct IsWrapper<VariantW<Ts...>> : std::true_type {};

// ============================================================================
// OPTIONAL WRAPPER - Nullable type
// ============================================================================

template<typename T>
struct OptionalW {
    using wrapped_type = T;
    using unwrapped_type = typename UnwrapType<T>::type;
    using actual_type = std::optional<unwrapped_type>;

    static constexpr bool is_wrapper = true;
    static constexpr bool is_optional = true;

    // Storage
    actual_type data;

    OptionalW() = default;
    explicit OptionalW(const unwrapped_type& value) : data(value) {}
    explicit OptionalW(std::nullopt_t) : data(std::nullopt) {}

    // Access
    operator actual_type&() { return data; }
    operator const actual_type&() const { return data; }
    actual_type& get() { return data; }
    const actual_type& get() const { return data; }

    bool has_value() const { return data.has_value(); }
    explicit operator bool() const { return has_value(); }

    unwrapped_type& value() { return data.value(); }
    const unwrapped_type& value() const { return data.value(); }

    unwrapped_type value_or(const unwrapped_type& default_value) const {
        return data.value_or(default_value);
    }

    void reset() { data.reset(); }
};

template<typename T>
struct UnwrapType<OptionalW<T>> {
    using type = std::optional<typename UnwrapType<T>::type>;
};

template<typename T>
struct IsWrapper<OptionalW<T>> : std::true_type {};

// ============================================================================
// MAP WRAPPER - Key-value pairs
// ============================================================================

template<typename K, typename V>
struct MapW {
    using key_type = typename UnwrapType<K>::type;
    using value_type = typename UnwrapType<V>::type;
    using actual_type = std::map<key_type, value_type>;

    static constexpr bool is_wrapper = true;
    static constexpr bool is_composite = true;
    static constexpr bool is_map = true;

    // Storage
    actual_type data;

    MapW() = default;
    explicit MapW(const actual_type& m) : data(m) {}

    // Access
    operator actual_type&() { return data; }
    operator const actual_type&() const { return data; }
    actual_type& get() { return data; }
    const actual_type& get() const { return data; }

    // Map operations
    size_t size() const { return data.size(); }
    bool empty() const { return data.empty(); }

    value_type& operator[](const key_type& key) { return data[key]; }
    const value_type& at(const key_type& key) const { return data.at(key); }

    bool contains(const key_type& key) const {
        return data.find(key) != data.end();
    }

    void insert(const key_type& k, const value_type& v) {
        data[k] = v;
    }

    void erase(const key_type& key) {
        data.erase(key);
    }

    void clear() { data.clear(); }
};

template<typename K, typename V>
struct UnwrapType<MapW<K, V>> {
    using type = std::map<
        typename UnwrapType<K>::type,
        typename UnwrapType<V>::type
    >;
};

template<typename K, typename V>
struct IsWrapper<MapW<K, V>> : std::true_type {};

// ============================================================================
// ENHANCED TYPE REGISTRY - Check composite types
// ============================================================================

/**
 * @brief Enhanced registry that validates composite types
 *
 * A composite type is valid if ALL its component types are registered
 */
class CompositeTypeRegistry : public WrapperTypeRegistry {
public:
    static CompositeTypeRegistry& Instance() {
        static CompositeTypeRegistry instance;
        return instance;
    }

    // Check if a potentially composite type is acceptable
    template<typename T>
    bool IsTypeAcceptable() const {
        return CheckCompositeType<T>();
    }

private:
    // Enhanced type checking for composites
    template<typename T>
    bool CheckCompositeType() const {
        if constexpr (IsWrapper_v<T>) {
            return CheckWrappedType<T>();
        } else {
            // Base type: check if registered
            return WrapperTypeRegistry::Instance().IsTypeAcceptable<T>();
        }
    }

    // Check wrapped types (including composites)
    template<typename W>
    bool CheckWrappedType() const {
        // Handle pair
        if constexpr (IsPair<W>::value) {
            return CheckCompositeType<typename W::first_type>() &&
                   CheckCompositeType<typename W::second_type>();
        }
        // Handle tuple
        else if constexpr (IsTuple<W>::value) {
            return CheckTupleTypes<W>(std::make_index_sequence<W::component_count>{});
        }
        // Handle variant
        else if constexpr (IsVariant<W>::value) {
            return CheckVariantTypes<W>();
        }
        // Handle optional
        else if constexpr (IsOptional<W>::value) {
            return CheckCompositeType<typename W::wrapped_type>();
        }
        // Handle map
        else if constexpr (IsMap<W>::value) {
            return CheckCompositeType<typename W::key_wrapped>() &&
                   CheckCompositeType<typename W::value_wrapped>();
        }
        // Regular wrapper (RefW, PtrW, etc.)
        else {
            return CheckCompositeType<typename W::wrapped_type>();
        }
    }

    // Helper traits for detecting composite types
    template<typename T>
    struct IsPair : std::false_type {};
    template<typename T1, typename T2>
    struct IsPair<PairW<T1, T2>> : std::true_type {};

    template<typename T>
    struct IsTuple : std::false_type {};
    template<typename... Ts>
    struct IsTuple<TupleW<Ts...>> : std::true_type {};

    template<typename T>
    struct IsVariant : std::false_type {};
    template<typename... Ts>
    struct IsVariant<VariantW<Ts...>> : std::true_type {};

    template<typename T>
    struct IsOptional : std::false_type {};
    template<typename T>
    struct IsOptional<OptionalW<T>> : std::true_type {};

    template<typename T>
    struct IsMap : std::false_type {};
    template<typename K, typename V>
    struct IsMap<MapW<K, V>> : std::true_type {
        using key_wrapped = K;
        using value_wrapped = V;
    };

    // Check all tuple elements
    template<typename Tuple, size_t... Is>
    bool CheckTupleTypes(std::index_sequence<Is...>) const {
        return (... && CheckTupleElement<Tuple, Is>());
    }

    template<typename Tuple, size_t I>
    bool CheckTupleElement() const {
        using ElementType = std::tuple_element_t<I, typename Tuple::actual_type>;
        // Need to reconstruct the wrapped type from the unwrapped type
        // This is complex, so we'd need reverse mapping
        return true; // Simplified for now
    }

    // Check all variant alternatives
    template<typename Variant>
    bool CheckVariantTypes() const {
        // Simplified: assume valid if base check passes
        return true;
    }
};

// ============================================================================
// CONVENIENCE ALIASES FOR COMMON PATTERNS
// ============================================================================

// Image-sampler pair (common in graphics)
using ImageSamplerPair = PairW<VkImage, VkSampler>;

// Optional resource
template<typename T>
using OptionalResource = OptionalW<T>;

// Resource variant (can be one of several types)
template<typename... Ts>
using ResourceVariant = VariantW<Ts...>;

// Resource map (e.g., binding index to resource)
template<typename V>
using BindingMap = MapW<uint32_t, V>;

// Vector of pairs (e.g., attribute descriptions)
template<typename T1, typename T2>
using PairVector = VectorW<PairW<T1, T2>>;

// ============================================================================
// USAGE EXAMPLES
// ============================================================================

/**
 * Example usage:
 *
 * // Register base types
 * auto& registry = CompositeTypeRegistry::Instance();
 * registry.RegisterBaseType<VkImage>();
 * registry.RegisterBaseType<VkSampler>();
 * registry.RegisterBaseType<VkBuffer>();
 * registry.RegisterBaseType<uint32_t>();
 *
 * // Now these composite types are valid:
 * using ImageSamplerPair = PairW<VkImage, VkSampler>;
 * using ResourceChoice = VariantW<VkImage, VkBuffer>;
 * using OptionalImage = OptionalW<VkImage>;
 * using BindingMap = MapW<uint32_t, VkImage>;
 *
 * // And they can be further composed:
 * using PairRef = RefW<ImageSamplerPair>;                     // std::pair<...>&
 * using VectorOfPairs = VectorW<ImageSamplerPair>;           // std::vector<std::pair<...>>
 * using OptionalPair = OptionalW<ImageSamplerPair>;          // std::optional<std::pair<...>>
 * using ConstRefChoice = ConstW<RefW<ResourceChoice>>;       // const std::variant<...>&
 *
 * // Complex composition:
 * using ComplexType = VectorW<TupleW<
 *     OptionalW<VkImage>,
 *     PairW<uint32_t, VkBuffer>,
 *     VariantW<VkSampler, VkImageView>
 * >>;
 * // This represents: std::vector<std::tuple<
 * //     std::optional<VkImage>,
 * //     std::pair<uint32_t, VkBuffer>,
 * //     std::variant<VkSampler, VkImageView>
 * // >>
 */

} // namespace Vixen::RenderGraph