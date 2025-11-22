#pragma once

#include "VoxelDataTypes.h"
#include "AttributeRegistry.h"
#include <VoxelComponents.h>  // Canonical component registry
#include <array>
#include <string_view>
#include <type_traits>
#include <glm/glm.hpp>

namespace VoxelData {

// ============================================================================
// Component Type Extraction - Get underlying type from Gaia component
// ============================================================================

template<typename Component>
struct ComponentValueType {
    // Default: assume component has .value member (scalar components)
    using type = decltype(std::declval<Component>().value);
};

// Specialization for Vec3 components (they don't have .value, they ARE the vec3)
template<typename T>
concept Vec3Component = requires(const T t) {
    { t.toVec3() } -> std::convertible_to<glm::vec3>;
};

template<Vec3Component Component>
struct ComponentValueType<Component> {
    using type = glm::vec3;
};

// ============================================================================
// Type Traits - Map C++ types to AttributeType with default values
// ============================================================================

template<typename T>
struct AttributeTypeTraits {
    static constexpr bool isValid = false;
};

template<>
struct AttributeTypeTraits<float> {
    static constexpr bool isValid = true;
    static constexpr AttributeType type = AttributeType::Float;
    static constexpr size_t componentCount = 1;
    static constexpr VoxelMemberFlags flags = VoxelMemberFlags::None;
    static float defaultValue() { return 0.0f; }
};

template<>
struct AttributeTypeTraits<uint32_t> {
    static constexpr bool isValid = true;
    static constexpr AttributeType type = AttributeType::Uint32;
    static constexpr size_t componentCount = 1;
    static constexpr VoxelMemberFlags flags = VoxelMemberFlags::None;
    static uint32_t defaultValue() { return 0u; }
};

template<>
struct AttributeTypeTraits<uint16_t> {
    static constexpr bool isValid = true;
    static constexpr AttributeType type = AttributeType::Uint16;
    static constexpr size_t componentCount = 1;
    static constexpr VoxelMemberFlags flags = VoxelMemberFlags::None;
    static uint16_t defaultValue() { return static_cast<uint16_t>(0); }
};

template<>
struct AttributeTypeTraits<uint8_t> {
    static constexpr bool isValid = true;
    static constexpr AttributeType type = AttributeType::Uint8;
    static constexpr size_t componentCount = 1;
    static constexpr VoxelMemberFlags flags = VoxelMemberFlags::None;
    static uint8_t defaultValue() { return static_cast<uint8_t>(0); }
};

template<>
struct AttributeTypeTraits<glm::vec3> {
    static constexpr bool isValid = true;
    static constexpr AttributeType type = AttributeType::Vec3;
    static constexpr size_t componentCount = 3;
    static constexpr VoxelMemberFlags flags = VoxelMemberFlags::Vec3;
    static glm::vec3 defaultValue() { return glm::vec3(0.0f); }
};

// ============================================================================
// VoxelMember - Compile-time attribute descriptor (like ResourceSlot)
// ============================================================================

/**
 * @brief Compile-time voxel attribute descriptor
 *
 * All information is constexpr - completely resolved at compile time.
 * Zero runtime overhead.
 *
 * Template Parameters:
 * - T: Attribute type (float, uint32_t, glm::vec3, etc.)
 * - Index: Attribute index (0..N-1)
 * - IsKey: If true, this attribute determines octree structure
 */
template<
    typename T,
    uint32_t Index,
    bool IsKey = false
>
struct VoxelMember {
    using Type = T;

    static constexpr uint32_t index = Index;
    static constexpr AttributeType attributeType = AttributeTypeTraits<T>::type;
    static constexpr bool isKey = IsKey;
    static constexpr size_t componentCount = AttributeTypeTraits<T>::componentCount;

    // Compile-time validation
    static_assert(AttributeTypeTraits<T>::isValid, "Unsupported voxel attribute type");

    // Default constructor for use as constant
    constexpr VoxelMember() = default;
};

// Forward declarations for detail namespace (defined later)
namespace detail {
    inline void RegisterAttributeExpanded(
        AttributeRegistry* registry,
        const std::string& name,
        AttributeType type,
        const std::any& defaultValue,
        bool isKey
    );
}

// ============================================================================
// VoxelConfigBase - Base class for voxel configurations
// ============================================================================

/**
 * @brief Pure constexpr voxel configuration base
 *
 * Similar to ResourceConfigBase, but for voxel attributes.
 */
template<size_t NumAttributes>
struct VoxelConfigBase {
    static constexpr size_t ATTRIBUTE_COUNT = NumAttributes;

protected:
    std::array<AttributeDescriptor, NumAttributes> attributes{};

public:
    // Get attribute descriptors for runtime registration
    const std::array<AttributeDescriptor, NumAttributes>& getAttributeDescriptors() const {
        return attributes;
    }

    // Register all attributes with an AttributeRegistry
    // Automatically expands vec3 → 3 float components behind the scenes
    void registerWith(AttributeRegistry* registry) const {
        for (const auto& attr : attributes) {
                detail::RegisterAttributeExpanded(registry, attr.name, attr.type, attr.defaultValue, attr.isKey);
        }
    }
};

// ============================================================================
// Macro API - Define voxel configurations with zero overhead
// ============================================================================

/**
 * @brief Define a voxel configuration using X-macro pattern (zero duplication!)
 *
 * Define attributes ONCE in a macro list, everything else auto-generated.
 *
 * Usage:
 * ```cpp
 * #define STANDARD_VOXEL_ATTRIBUTES(X) \
 *     X(KEY,       DENSITY,  float,     0) \
 *     X(ATTRIBUTE, MATERIAL, uint32_t,  1) \
 *     X(ATTRIBUTE, COLOR,    glm::vec3, 2)
 *
 * VOXEL_CONFIG(StandardVoxel, 3, STANDARD_VOXEL_ATTRIBUTES)
 * ```
 *
 * The X-macro automatically expands to:
 * - VOXEL_KEY/VOXEL_ATTRIBUTE declarations
 * - Constructor with initialized attributes array
 * - struct definition with base class
 */
#define VOXEL_CONFIG(ConfigName, NumAttributes, AttributeList) \
    struct ConfigName : public ::VoxelData::VoxelConfigBase<NumAttributes> { \
        AttributeList(VOXEL_CONFIG_EXPAND_DECL) \
        ConfigName() { \
            attributes = { AttributeList(VOXEL_CONFIG_EXPAND_INIT) }; \
        } \
    };

// Helper macros for X-macro expansion (component-based)
// Now extracts name and type from Component type
#define VOXEL_CONFIG_EXPAND_DECL(Type, Component, Index) \
    VOXEL_##Type##_COMPONENT(Component, Index);

#define VOXEL_CONFIG_EXPAND_INIT(Type, Component, Index) \
    component_desc_##Index,

/**
 * @brief Begin/End style (for backward compatibility, requires listing names twice)
 */
#define VOXEL_CONFIG_BEGIN(ConfigName, NumAttributes) \
    struct ConfigName : public ::VoxelData::VoxelConfigBase<NumAttributes> {

#define VOXEL_CONFIG_END(ConfigName, ...) \
    ConfigName() { \
        attributes = { __VA_ARGS__##_desc... }; \
    } \
}

/**
 * @brief Define key attribute from component type (NEW - component-based)
 *
 * The key attribute determines octree structure.
 * Component name and type extracted automatically from VoxelComponents.
 *
 * Parameters:
 * - Component: Component type from VoxelComponents (e.g., GaiaVoxel::Density)
 * - Index: Attribute index (must be 0 for key)
 *
 * Example:
 * ```cpp
 * VOXEL_KEY_COMPONENT(GaiaVoxel::Density, 0);
 * ```
 */
#define VOXEL_KEY_COMPONENT(Component, Index) \
    using ValueType_##Index = typename ::VoxelData::ComponentValueType<Component>::type; \
    using Member_##Index = ::VoxelData::VoxelMember<ValueType_##Index, Index, true>; \
    static constexpr Member_##Index component_member_##Index{}; \
    static inline const ::VoxelData::AttributeDescriptor component_desc_##Index{ \
        Component::Name, \
        ::VoxelData::AttributeTypeTraits<ValueType_##Index>::type, \
        std::any(Component{}), \
        true \
    }

/**
 * @brief Define non-key attribute from component type (NEW - component-based)
 *
 * Non-key attributes can be added/removed at runtime (NON-DESTRUCTIVE).
 * Component name and type extracted automatically from VoxelComponents.
 *
 * Parameters:
 * - Component: Component type from VoxelComponents (e.g., GaiaVoxel::Color)
 * - Index: Attribute index (0..N-1)
 *
 * Example:
 * ```cpp
 * VOXEL_ATTRIBUTE_COMPONENT(GaiaVoxel::Material, 1);
 * VOXEL_ATTRIBUTE_COMPONENT(GaiaVoxel::Color, 2);
 * ```
 */
#define VOXEL_ATTRIBUTE_COMPONENT(Component, Index) \
    using ValueType_##Index = typename ::VoxelData::ComponentValueType<Component>::type; \
    using Member_##Index = ::VoxelData::VoxelMember<ValueType_##Index, Index, false>; \
    static constexpr Member_##Index component_member_##Index{}; \
    static inline const ::VoxelData::AttributeDescriptor component_desc_##Index{ \
        Component::Name, \
        ::VoxelData::AttributeTypeTraits<ValueType_##Index>::type, \
        std::any(Component{}), \
        false \
    }

// ============================================================================
// OLD Macros (deprecated - kept for backward compatibility)
// ============================================================================

/**
 * @brief Define key attribute with automatic initialization (DEPRECATED)
 */
#define VOXEL_KEY(AttrName, AttrType, Index, ...) \
    using AttrName##_Member = ::VoxelData::VoxelMember<AttrType, Index, true>; \
    static constexpr AttrName##_Member AttrName{}; \
    static inline const ::VoxelData::AttributeDescriptor AttrName##_desc{ \
        ::VoxelData::detail::ToLowercase(#AttrName), \
        ::VoxelData::AttributeTypeTraits<AttrType>::type, \
        std::any(::VoxelData::detail::GetDefaultOrCustom<AttrType>(__VA_ARGS__)), \
        true \
    }; \
    static constexpr size_t AttrName##_INDEX = Index

/**
 * @brief Define non-key attribute with automatic initialization (DEPRECATED)
 */
#define VOXEL_ATTRIBUTE(AttrName, AttrType, Index, ...) \
    using AttrName##_Member = ::VoxelData::VoxelMember<AttrType, Index, false>; \
    static constexpr AttrName##_Member AttrName{}; \
    static inline const ::VoxelData::AttributeDescriptor AttrName##_desc{ \
        ::VoxelData::detail::ToLowercase(#AttrName), \
        ::VoxelData::AttributeTypeTraits<AttrType>::type, \
        std::any(::VoxelData::detail::GetDefaultOrCustom<AttrType>(__VA_ARGS__)), \
        false \
    }; \
    static constexpr size_t AttrName##_INDEX = Index

// Helper for default value resolution and string conversion
namespace detail {
    template<typename T, typename... Args>
    T GetDefaultOrCustom(Args&&... args) {
        if constexpr (sizeof...(Args) > 0) {
            return T(std::forward<Args>(args)...);
        } else {
            return AttributeTypeTraits<T>::defaultValue();
        }
    }

    // Convert uppercase constant name to lowercase runtime string
    // DENSITY → "density", MATERIAL_ID → "material_id"
    inline std::string ToLowercase(const char* str) {
        std::string result;
        for (const char* p = str; *p; ++p) {
            result += static_cast<char>(std::tolower(*p));
        }
        return result;
    }

    // Get flags for an AttributeType
    inline VoxelMemberFlags GetFlags(AttributeType type) {
        switch (type) {
            case AttributeType::Vec3: return VoxelMemberFlags::Vec3;
            default: return VoxelMemberFlags::None;
        }
    }

    // Register a single attribute, expanding vec3 into 3 components for storage
    // but keeping logical name for key operations
    inline void RegisterAttributeExpanded(
        AttributeRegistry* registry,
        const std::string& name,
        AttributeType type,
        const std::any& defaultValue,
        bool isKey
    ) {
        VoxelMemberFlags flags = GetFlags(type);

        if (hasFlag(flags, VoxelMemberFlags::Vec3)) {
            // Vec3 handling:
            // - Storage: 3 separate float arrays (name_x, name_y, name_z)
            // - Logical: Single vec3 accessor (name) for filters/operations
            glm::vec3 defaultVec = std::any_cast<glm::vec3>(defaultValue);

            if (isKey) {
                // Register as vec3 key - allows custom predicates
                // e.g., "normals in hemisphere" checks full vec3
                registry->registerKey(name, type, defaultValue);
            } else {
                // Non-key: Register logical vec3 + component storage
                registry->addAttribute(name, type, defaultValue);
            }

            // Always add component storage (for both key and non-key)
            // These are the actual arrays in AttributeStorage
            registry->addAttribute(name + "_x", AttributeType::Float, defaultVec.x);
            registry->addAttribute(name + "_y", AttributeType::Float, defaultVec.y);
            registry->addAttribute(name + "_z", AttributeType::Float, defaultVec.z);
        } else {
            // Scalar attribute - register as-is
            if (isKey) {
                registry->registerKey(name, type, defaultValue);
            } else {
                registry->addAttribute(name, type, defaultValue);
            }
        }
    }
}

/**
 * @brief Auto-initialize attributes array from descriptor list
 *
 * Call this in the config constructor to automatically populate the attributes array.
 *
 * Usage:
 * ```cpp
 * VOXEL_CONFIG(StandardVoxel, 3) {
 *     VOXEL_KEY(DENSITY, float, 0);
 *     VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1);
 *     VOXEL_ATTRIBUTE(COLOR, glm::vec3, 2);
 *
 *     StandardVoxel() {
 *         VOXEL_CONFIG_INIT(DENSITY, MATERIAL, COLOR);
 *     }
 * };
 * ```
 */
#define VOXEL_CONFIG_INIT(...) \
    attributes = { __VA_ARGS__##_desc... }

/**
 * @brief Validate voxel configuration at compile time
 *
 * Ensures attribute count matches expected value.
 *
 * Usage:
 * ```cpp
 * VALIDATE_VOXEL_CONFIG(StandardVoxel, 3);
 * ```
 */
#define VALIDATE_VOXEL_CONFIG(ConfigName, ExpectedCount) \
    static_assert(ConfigName::ATTRIBUTE_COUNT == ExpectedCount, \
        "Attribute count mismatch in " #ConfigName)

// ============================================================================
// Compile-Time Validation Helpers
// ============================================================================

/**
 * @brief Validate attribute type at compile time
 *
 * Usage:
 * ```cpp
 * static_assert(ValidateAttributeType<StandardVoxel::DENSITY, float>());
 * ```
 */
template<typename MemberType, typename ExpectedType>
constexpr bool ValidateAttributeType() {
    return std::is_same_v<typename MemberType::Type, ExpectedType>;
}

/**
 * @brief Validate attribute index at compile time
 */
template<typename MemberType, uint32_t ExpectedIndex>
constexpr bool ValidateAttributeIndex() {
    return MemberType::index == ExpectedIndex;
}

/**
 * @brief Validate that exactly one attribute is the key
 */
template<typename ConfigType>
constexpr bool HasExactlyOneKey() {
    // Todo: Implementation requires constexpr array iteration (C++20)
    // For now, rely on runtime registration checks
    return true;
}

} // namespace VoxelData
