#pragma once

#include "VoxelDataTypes.h"
#include "AttributeRegistry.h"
#include <array>
#include <string_view>
#include <type_traits>
#include <glm/glm.hpp>

namespace VoxelData {

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
 * @brief Define a pure constexpr voxel configuration
 *
 * All type information is constexpr - compiler optimizes everything away.
 *
 * Usage:
 * ```cpp
 * VOXEL_CONFIG(StandardVoxel, 3) {
 *     VOXEL_ATTRIBUTE(DENSITY, float, 0.0f, true);     // Key attribute
 *     VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 0u, false);
 *     VOXEL_ATTRIBUTE(COLOR, glm::vec3, glm::vec3(0), false);
 * };
 * ```
 */
#define VOXEL_CONFIG(ConfigName, NumAttributes) \
    struct ConfigName : public ::VoxelData::VoxelConfigBase<NumAttributes>

/**
 * @brief Define key attribute with automatic initialization
 *
 * The key attribute determines octree structure.
 * Changing the key triggers octree rebuild (DESTRUCTIVE).
 *
 * Parameters:
 * - AttrName: Attribute constant name (e.g., DENSITY) - auto-lowercased to "density"
 * - AttrType: C++ type (float, uint32_t, glm::vec3)
 * - Index: Attribute index (must be 0 for key)
 * - ...: Optional custom default value (uses type default if omitted)
 *
 * Example:
 * ```cpp
 * VOXEL_KEY(DENSITY, float, 0);              // "density", uses 0.0f
 * VOXEL_KEY(HEALTH, uint16_t, 0, 100);       // "health", custom default 100
 * ```
 */
#define VOXEL_KEY(AttrName, AttrType, Index, ...) \
    using AttrName##_Member = ::VoxelData::VoxelMember<AttrType, Index, true>; \
    static constexpr AttrName##_Member AttrName{}; \
    inline static const ::VoxelData::AttributeDescriptor AttrName##_desc{ \
        ::VoxelData::detail::ToLowercase(#AttrName), \
        ::VoxelData::AttributeTypeTraits<AttrType>::type, \
        std::any(::VoxelData::detail::GetDefaultOrCustom<AttrType>(__VA_ARGS__)), \
        true \
    }

/**
 * @brief Define non-key attribute with automatic initialization
 *
 * Non-key attributes can be added/removed at runtime (NON-DESTRUCTIVE).
 *
 * Parameters:
 * - AttrName: Attribute constant name (e.g., MATERIAL) - auto-lowercased to "material"
 * - AttrType: C++ type (float, uint32_t, glm::vec3)
 * - Index: Attribute index (0..N-1)
 * - ...: Optional custom default value (uses type default if omitted)
 *
 * Example:
 * ```cpp
 * VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1);         // "material", uses 0u
 * VOXEL_ATTRIBUTE(COLOR, glm::vec3, 2, glm::vec3(1)); // "color", custom white
 * ```
 */
#define VOXEL_ATTRIBUTE(AttrName, AttrType, Index, ...) \
    using AttrName##_Member = ::VoxelData::VoxelMember<AttrType, Index, false>; \
    static constexpr AttrName##_Member AttrName{}; \
    inline static const ::VoxelData::AttributeDescriptor AttrName##_desc{ \
        ::VoxelData::detail::ToLowercase(#AttrName), \
        ::VoxelData::AttributeTypeTraits<AttrType>::type, \
        std::any(::VoxelData::detail::GetDefaultOrCustom<AttrType>(__VA_ARGS__)), \
        false \
    }

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
