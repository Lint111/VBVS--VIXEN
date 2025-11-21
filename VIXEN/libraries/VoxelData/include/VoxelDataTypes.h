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
 * Attribute descriptor - metadata for a voxel attribute
 */
struct AttributeDescriptor {
    std::string name;
    AttributeType type;
    std::any defaultValue;
    bool isKey;  // If true, determines octree structure

    AttributeDescriptor() = default;

    AttributeDescriptor(std::string n, AttributeType t, std::any def, bool key = false)
        : name(std::move(n)), type(t), defaultValue(std::move(def)), isKey(key) {}

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

} // namespace VoxelData
