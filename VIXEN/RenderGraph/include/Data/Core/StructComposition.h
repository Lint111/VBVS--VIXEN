#pragma once

#include "CompositeTypes.h"
#include <tuple>
#include <type_traits>

namespace Vixen::RenderGraph {

// ============================================================================
// STRUCT DECOMPOSITION SYSTEM
// ============================================================================

/**
 * @brief System for decomposing custom structs/classes into base components
 *
 * Any struct can be registered as a composition of base types:
 *
 * struct CameraData {
 *     glm::mat4 view;
 *     glm::mat4 projection;
 *     glm::vec3 position;
 * };
 *
 * REGISTER_STRUCT_COMPOSITION(CameraData,
 *     glm::mat4,  // view
 *     glm::mat4,  // projection
 *     glm::vec3   // position
 * );
 *
 * Now CameraData is valid if glm::mat4 and glm::vec3 are registered base types.
 */

// ============================================================================
// STRUCT WRAPPER - Represents a struct as a composition
// ============================================================================

template<typename StructType, typename... ComponentTypes>
struct StructW {
    using struct_type = StructType;
    using components = std::tuple<ComponentTypes...>;
    using actual_type = StructType;

    static constexpr bool is_wrapper = true;
    static constexpr bool is_struct = true;
    static constexpr size_t component_count = sizeof...(ComponentTypes);

    // Storage
    actual_type data;

    StructW() = default;
    explicit StructW(const actual_type& s) : data(s) {}
    explicit StructW(actual_type&& s) : data(std::move(s)) {}

    // Access
    operator actual_type&() { return data; }
    operator const actual_type&() const { return data; }
    actual_type& get() { return data; }
    const actual_type& get() const { return data; }
};

template<typename S, typename... Cs>
struct UnwrapType<StructW<S, Cs...>> {
    using type = S;
};

template<typename S, typename... Cs>
struct IsWrapper<StructW<S, Cs...>> : std::true_type {};

// ============================================================================
// FIELD ACCESSOR - Access struct fields by index
// ============================================================================

/**
 * @brief Type-safe field accessor for decomposed structs
 *
 * Allows accessing struct fields by index with proper type checking
 */
template<size_t I, typename StructType>
struct FieldAccessor {
    // To be specialized for each registered struct
};

// ============================================================================
// STRUCT DECOMPOSER - Extract component types from struct
// ============================================================================

/**
 * @brief Trait to get component types from a struct
 *
 * Specialized for each registered struct type
 */
template<typename T>
struct StructDecomposer {
    static constexpr bool is_decomposable = false;
    using components = std::tuple<>;
};

// ============================================================================
// REGISTRATION MACROS
// ============================================================================

/**
 * @brief Register a struct as a composition of types
 *
 * Usage:
 * REGISTER_STRUCT_COMPOSITION(MyStruct, Type1, Type2, Type3)
 *
 * This enables:
 * - Automatic validation if Type1, Type2, Type3 are registered
 * - Use in ResourceVariant
 * - Composition with wrappers (RefW<MyStruct>, VectorW<MyStruct>, etc.)
 */
#define REGISTER_STRUCT_COMPOSITION(StructType, ...) \
    template<> \
    struct StructDecomposer<StructType> { \
        static constexpr bool is_decomposable = true; \
        using components = std::tuple<__VA_ARGS__>; \
        using wrapper_type = StructW<StructType, __VA_ARGS__>; \
    }; \
    \
    template<> \
    struct IsWrapper<StructType> : std::false_type {}; \
    \
    inline bool Register##StructType() { \
        CompositeTypeRegistry::Instance().RegisterCompositeStruct<StructType, __VA_ARGS__>(); \
        return true; \
    } \
    static bool g_##StructType##_registered = Register##StructType();

// ============================================================================
// FIELD MAPPING - Map struct fields to component types
// ============================================================================

/**
 * @brief Define field accessors for a struct
 *
 * Usage:
 * DEFINE_STRUCT_FIELDS(CameraData,
 *     FIELD(0, view, glm::mat4),
 *     FIELD(1, projection, glm::mat4),
 *     FIELD(2, position, glm::vec3)
 * )
 */
#define FIELD(Index, Name, Type) \
    template<> \
    struct FieldAccessor<Index, StructType> { \
        using field_type = Type; \
        static constexpr const char* field_name = #Name; \
        static field_type& get(StructType& s) { return s.Name; } \
        static const field_type& get(const StructType& s) { return s.Name; } \
    };

#define DEFINE_STRUCT_FIELDS(StructType, ...) \
    __VA_ARGS__

// ============================================================================
// ENHANCED COMPOSITE REGISTRY
// ============================================================================

class CompositeTypeRegistry : public WrapperTypeRegistry {
public:
    // Register a composite struct
    template<typename StructType, typename... ComponentTypes>
    void RegisterCompositeStruct() {
        // Check if all component types are valid
        bool allValid = (IsTypeAcceptable<ComponentTypes>() && ...);
        if (allValid) {
            // Register the struct type itself as valid
            compositeStructs.insert(std::type_index(typeid(StructType)));
        }
    }

    // Check if a struct is registered as composite
    template<typename T>
    bool IsCompositeStruct() const {
        return compositeStructs.count(std::type_index(typeid(T))) > 0;
    }

    // Enhanced type checking
    template<typename T>
    bool IsTypeAcceptable() const {
        // First check base types and wrappers
        if (WrapperTypeRegistry::Instance().IsTypeAcceptable<T>()) {
            return true;
        }

        // Then check if it's a registered composite struct
        if (IsCompositeStruct<T>()) {
            return true;
        }

        // Check if it's a wrapper of a composite struct
        if constexpr (IsWrapper_v<T>) {
            return CheckWrappedComposite<T>();
        }

        // Check if it's decomposable and all components are valid
        if constexpr (StructDecomposer<T>::is_decomposable) {
            return CheckDecomposableStruct<T>();
        }

        return false;
    }

private:
    std::unordered_set<std::type_index> compositeStructs;

    template<typename W>
    bool CheckWrappedComposite() const {
        if constexpr (requires { typename W::wrapped_type; }) {
            using Inner = typename W::wrapped_type;
            return IsTypeAcceptable<Inner>();
        }
        return false;
    }

    template<typename T>
    bool CheckDecomposableStruct() const {
        using Components = typename StructDecomposer<T>::components;
        return CheckTupleComponents<Components>(
            std::make_index_sequence<std::tuple_size_v<Components>>{}
        );
    }

    template<typename Tuple, size_t... Is>
    bool CheckTupleComponents(std::index_sequence<Is...>) const {
        return (IsTypeAcceptable<std::tuple_element_t<Is, Tuple>>() && ...);
    }
};

// ============================================================================
// AUTOMATIC STRUCT SERIALIZATION
// ============================================================================

/**
 * @brief Serialize/deserialize structs based on their decomposition
 */
template<typename T>
class StructSerializer {
public:
    // Serialize struct to tuple of components
    static auto Decompose(const T& obj) {
        if constexpr (StructDecomposer<T>::is_decomposable) {
            using Components = typename StructDecomposer<T>::components;
            return DecomposeImpl(obj, std::make_index_sequence<
                std::tuple_size_v<Components>>{}
            );
        } else {
            return std::tuple<>{};
        }
    }

    // Reconstruct struct from tuple of components
    template<typename... Args>
    static T Compose(const std::tuple<Args...>& components) {
        return ComposeImpl(components, std::index_sequence_for<Args...>{});
    }

private:
    template<size_t... Is>
    static auto DecomposeImpl(const T& obj, std::index_sequence<Is...>) {
        return std::make_tuple(
            FieldAccessor<Is, T>::get(obj)...
        );
    }

    template<typename Tuple, size_t... Is>
    static T ComposeImpl(const Tuple& components, std::index_sequence<Is...>) {
        T result;
        ((FieldAccessor<Is, T>::get(result) = std::get<Is>(components)), ...);
        return result;
    }
};

// ============================================================================
// COMMON GRAPHICS STRUCT REGISTRATIONS
// ============================================================================

// Example: Register common Vulkan structs
struct VkExtent2D_Wrapper {
    uint32_t width;
    uint32_t height;
};

REGISTER_STRUCT_COMPOSITION(VkExtent2D_Wrapper, uint32_t, uint32_t)

DEFINE_STRUCT_FIELDS(VkExtent2D_Wrapper,
    FIELD(0, width, uint32_t),
    FIELD(1, height, uint32_t)
)

// Example: Camera data structure
struct CameraData {
    float viewMatrix[16];      // mat4
    float projectionMatrix[16]; // mat4
    float position[3];          // vec3
    float padding;
};

REGISTER_STRUCT_COMPOSITION(CameraData,
    ArrayW<float, 16>,  // viewMatrix
    ArrayW<float, 16>,  // projectionMatrix
    ArrayW<float, 3>,   // position
    float               // padding
)

DEFINE_STRUCT_FIELDS(CameraData,
    FIELD(0, viewMatrix, float[16]),
    FIELD(1, projectionMatrix, float[16]),
    FIELD(2, position, float[3]),
    FIELD(3, padding, float)
)

// ============================================================================
// REFLECTION UTILITIES
// ============================================================================

/**
 * @brief Get field information for a struct
 */
template<typename T>
class StructReflection {
public:
    static constexpr size_t FieldCount() {
        if constexpr (StructDecomposer<T>::is_decomposable) {
            return std::tuple_size_v<typename StructDecomposer<T>::components>;
        }
        return 0;
    }

    template<size_t I>
    static constexpr const char* FieldName() {
        return FieldAccessor<I, T>::field_name;
    }

    template<size_t I>
    static constexpr auto FieldType() {
        return std::type_identity<typename FieldAccessor<I, T>::field_type>{};
    }

    // Get all field names
    static std::vector<std::string> GetFieldNames() {
        return GetFieldNamesImpl(std::make_index_sequence<FieldCount()>{});
    }

private:
    template<size_t... Is>
    static std::vector<std::string> GetFieldNamesImpl(std::index_sequence<Is...>) {
        return { FieldAccessor<Is, T>::field_name... };
    }
};

// ============================================================================
// USAGE EXAMPLES
// ============================================================================

/**
 * Example usage:
 *
 * // Define a custom struct
 * struct MaterialData {
 *     VkImage albedoTexture;
 *     VkImage normalTexture;
 *     VkSampler sampler;
 *     float roughness;
 *     float metallic;
 * };
 *
 * // Register it as a composition
 * REGISTER_STRUCT_COMPOSITION(MaterialData,
 *     VkImage,  // albedoTexture
 *     VkImage,  // normalTexture
 *     VkSampler, // sampler
 *     float,     // roughness
 *     float      // metallic
 * )
 *
 * // Define field accessors
 * DEFINE_STRUCT_FIELDS(MaterialData,
 *     FIELD(0, albedoTexture, VkImage),
 *     FIELD(1, normalTexture, VkImage),
 *     FIELD(2, sampler, VkSampler),
 *     FIELD(3, roughness, float),
 *     FIELD(4, metallic, float)
 * )
 *
 * // Now MaterialData can be used in the type system:
 * RefW<MaterialData>                    // MaterialData&
 * VectorW<MaterialData>                  // std::vector<MaterialData>
 * OptionalW<MaterialData>                // std::optional<MaterialData>
 * PairW<uint32_t, MaterialData>         // std::pair<uint32_t, MaterialData>
 *
 * // And it supports reflection:
 * auto fieldNames = StructReflection<MaterialData>::GetFieldNames();
 * // Returns: ["albedoTexture", "normalTexture", "sampler", "roughness", "metallic"]
 *
 * // Decompose and compose:
 * MaterialData mat = ...;
 * auto components = StructSerializer<MaterialData>::Decompose(mat);
 * MaterialData reconstructed = StructSerializer<MaterialData>::Compose(components);
 */

} // namespace Vixen::RenderGraph