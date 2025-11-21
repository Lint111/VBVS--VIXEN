#pragma once

#include <cstdint>
#include <string>
#include <any>

namespace VoxelData {

/**
 * Attribute type enumeration
 * Determines storage type and size
 */
enum class AttributeType : uint8_t {
    Float,      // 4 bytes
    Uint32,     // 4 bytes
    Uint16,     // 2 bytes
    Uint8,      // 1 byte
    Vec3,       // 12 bytes (stored as 3 separate float arrays)
};

/**
 * Get size of attribute type in bytes
 */
constexpr size_t getAttributeSize(AttributeType type) {
    switch (type) {
        case AttributeType::Float:  return sizeof(float);
        case AttributeType::Uint32: return sizeof(uint32_t);
        case AttributeType::Uint16: return sizeof(uint16_t);
        case AttributeType::Uint8:  return sizeof(uint8_t);
        case AttributeType::Vec3:   return sizeof(float);  // Component size
        default: return 0;
    }
}

/**
 * Get component count for attribute type
 * Scalars = 1, Vec3 = 3
 */
constexpr size_t getComponentCount(AttributeType type) {
    return (type == AttributeType::Vec3) ? 3 : 1;
}

/**
 * Attribute index - compile-time constant for fast attribute lookup
 *
 * Each attribute registered in AttributeRegistry gets a unique index.
 * Indices are stable across application lifetime (assigned at registration).
 *
 * Usage:
 *   - AttributeRegistry returns index when attribute registered
 *   - BrickView uses index for zero-cost lookups (no string hash)
 *   - DynamicVoxelScalar stores (index, value) pairs instead of (name, value)
 */
using AttributeIndex = uint16_t;
constexpr AttributeIndex INVALID_ATTRIBUTE_INDEX = static_cast<AttributeIndex>(-1);

/**
 * Attribute descriptor - metadata for a voxel attribute
 */
struct AttributeDescriptor {
    std::string name;
    AttributeType type;
    std::any defaultValue;
    AttributeIndex index;  // Unique index assigned at registration
    bool isKey;            // If true, determines octree structure

    AttributeDescriptor() : index(INVALID_ATTRIBUTE_INDEX), isKey(false) {}

    AttributeDescriptor(std::string n, AttributeType t, std::any def, AttributeIndex idx = INVALID_ATTRIBUTE_INDEX, bool key = false)
        : name(std::move(n)), type(t), defaultValue(std::move(def)), index(idx), isKey(key) {}

    // Get total number of arrays needed (1 for scalar, 3 for vec3)
    size_t arrayCount() const {
        return getComponentCount(type);
    }

    // Get element size in bytes
    size_t elementSize() const {
        return getAttributeSize(type);
    }
};

/**
 * Voxel member flags (for macro system)
 */
enum class VoxelMemberFlags : uint8_t {
    None = 0,
    Vec3 = 1 << 0,  // Member is vec3 → 3 arrays
    Key  = 1 << 1,  // Member determines octree structure
};

inline VoxelMemberFlags operator|(VoxelMemberFlags a, VoxelMemberFlags b) {
    return static_cast<VoxelMemberFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline VoxelMemberFlags operator&(VoxelMemberFlags a, VoxelMemberFlags b) {
    return static_cast<VoxelMemberFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline bool hasFlag(VoxelMemberFlags flags, VoxelMemberFlags flag) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

// ============================================================================
// Macro Infrastructure for Voxel Struct Definition
// ============================================================================

/**
 * Example usage:
 *
 * DEFINE_VOXEL_STRUCT(MyVoxel,
 *     VOXEL_MEMBER(density, float, 0.0f, VoxelMemberFlags::Key)
 *     VOXEL_MEMBER(material, uint32_t, 0u, VoxelMemberFlags::None)
 *     VOXEL_MEMBER(color, glm::vec3, glm::vec3(0), VoxelMemberFlags::Vec3)
 * )
 *
 * This generates:
 * - struct MyVoxel with members
 * - static method to register attributes with AttributeRegistry
 */

#define VOXEL_MEMBER(name, type, defaultVal, flags) \
    type name = defaultVal;

#define DEFINE_VOXEL_STRUCT(StructName, ...) \
    struct StructName { \
        __VA_ARGS__ \
        \
        static void registerAttributes(::VoxelData::AttributeRegistry* registry) { \
            REGISTER_ATTRIBUTES_IMPL(StructName, __VA_ARGS__) \
        } \
    };

// Helper to convert C++ type to AttributeType
namespace detail {
    template<typename T> struct TypeToAttributeType;
    template<> struct TypeToAttributeType<float> { static constexpr AttributeType value = AttributeType::Float; };
    template<> struct TypeToAttributeType<uint32_t> { static constexpr AttributeType value = AttributeType::Uint32; };
    template<> struct TypeToAttributeType<uint16_t> { static constexpr AttributeType value = AttributeType::Uint16; };
    template<> struct TypeToAttributeType<uint8_t> { static constexpr AttributeType value = AttributeType::Uint8; };
}

// Registration implementation (simplified - can be expanded)
#define REGISTER_ATTRIBUTES_IMPL(StructName, ...) \
    /* For now, user calls registry->registerKey() / addAttribute() manually */ \
    /* Full implementation would parse __VA_ARGS__ and auto-register */

} // namespace VoxelData
