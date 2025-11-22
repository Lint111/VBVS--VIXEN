#pragma once

#include <gaia.h>
#include <glm/glm.hpp>
#include <cstdint>

namespace GaiaVoxel {

/**
 * ECS Components for voxel data using Gaia's native multi-member support.
 *
 * Design:
 * - Vec3 types use {x,y,z} or {r,g,b} members (Gaia handles SoA internally)
 * - Macro generates component + trait metadata
 * - Natural glm::vec3 conversion via helper functions
 */

// ============================================================================
// Component Definition Macros (Simplified - Gaia Handles Layout)
// ============================================================================

// Scalar component (single value)
#define VOXEL_COMPONENT_SCALAR(ComponentName, LogicalName, Type, DefaultVal) \
    struct ComponentName { \
        static constexpr const char* Name = LogicalName; \
        Type value = DefaultVal; \
    };

// Vec3 component (float only, Gaia controls layout)
// Layout: AoS or SoA (passed directly to GAIA_LAYOUT macro)
#define VOXEL_COMPONENT_VEC3(ComponentName, LogicalName, S0, S1, S2, Layout, D0, D1, D2) \
    struct ComponentName { \
        static constexpr const char* Name = LogicalName; \
        static constexpr const char* Suffixes[3] = {#S0, #S1, #S2}; \
        GAIA_LAYOUT(Layout); \
        \
        float S0 = D0; \
        float S1 = D1; \
        float S2 = D2; \
        \
        /* glm::vec3 conversion */ \
        ComponentName() = default; \
        ComponentName(const glm::vec3& v) : S0(v[0]), S1(v[1]), S2(v[2]) {} \
        operator glm::vec3() const { return glm::vec3(S0, S1, S2); } \
        glm::vec3 toVec3() const { return glm::vec3(S0, S1, S2); } \
    };

// ============================================================================
// Spatial Indexing
// ============================================================================

/**
 * Morton code - encodes 3D position in single uint64.
 * 8 bytes vs 12 bytes for glm::vec3.
 */
VOXEL_COMPONENT_SCALAR(MortonKey, "position", uint64_t, 0)

// Helper functions for MortonKey
namespace MortonKeyUtils {
    glm::ivec3 decode(uint64_t code);
    glm::vec3 toWorldPos(uint64_t code);
    uint64_t encode(const glm::vec3& pos);
    uint64_t encode(const glm::ivec3& pos);
}

// ============================================================================
// Core Voxel Attributes
// ============================================================================

// Scalar attributes
VOXEL_COMPONENT_SCALAR(Density, "density", float, 1.0f)
VOXEL_COMPONENT_SCALAR(Material, "material", uint32_t, 0)
VOXEL_COMPONENT_SCALAR(EmissionIntensity, "emission_intensity", float, 0.0f)

// Vec3 attributes with Gaia layout control
VOXEL_COMPONENT_VEC3(Color, "color", r, g, b, AoS, 1.0f, 1.0f, 1.0f)
VOXEL_COMPONENT_VEC3(Normal, "normal", x, y, z, AoS, 0.0f, 1.0f, 0.0f)
VOXEL_COMPONENT_VEC3(Emission, "emission", r, g, b, AoS, 0.0f, 0.0f, 0.0f)

// ============================================================================
// Metadata Components
// ============================================================================

/**
 * Brick reference - links voxel to dense brick storage.
 */
struct BrickReference {
    static constexpr const char* Name = "brick_reference";
    uint32_t brickID;
    uint8_t localX;
    uint8_t localY;
    uint8_t localZ;
};

/**
 * Chunk ID - spatial grouping.
 */
VOXEL_COMPONENT_SCALAR(ChunkID, "chunk_id", uint32_t, 0)

/**
 * Tag component - marks voxels that should be in octree.
 * Empty struct = zero memory overhead.
 */
struct Solid {};

// ============================================================================
// Component Traits (MINIMAL - Just Name!)
// ============================================================================

template<typename T>
struct ComponentTraits;

// Single macro for all components - no type differentiation
#define DEFINE_COMPONENT_TRAITS(Component) \
    template<> \
    struct ComponentTraits<Component> { \
        static constexpr const char* Name = Component::Name; \
    };

// Register all components (same macro for all!)
DEFINE_COMPONENT_TRAITS(MortonKey)
DEFINE_COMPONENT_TRAITS(Density)
DEFINE_COMPONENT_TRAITS(Material)
DEFINE_COMPONENT_TRAITS(EmissionIntensity)
DEFINE_COMPONENT_TRAITS(ChunkID)
DEFINE_COMPONENT_TRAITS(Color)
DEFINE_COMPONENT_TRAITS(Normal)
DEFINE_COMPONENT_TRAITS(Emission)

#undef DEFINE_COMPONENT_TRAITS

// ============================================================================
// Type Detection (Automatic via C++20 Concepts)
// ============================================================================

// Detect scalar component (has .value member)
template<typename T>
concept HasValueMember = requires(T t) {
    { t.value };
};

// Detect vec3 component (has toVec3() method)
template<typename T>
concept HasToVec3Method = requires(const T t) {
    { t.toVec3() } -> std::convertible_to<glm::vec3>;
};

// ============================================================================
// Component Concepts (Type-Based Detection - No Bools!)
// ============================================================================

template<typename T>
concept VoxelComponent = requires {
    { ComponentTraits<T>::Name } -> std::convertible_to<const char*>;
};

// Vec3 component = has toVec3() method
template<typename T>
concept Vec3Component = VoxelComponent<T> && HasToVec3Method<T>;

// Scalar component = has .value member
template<typename T>
concept ScalarComponent = VoxelComponent<T> && HasValueMember<T>;

// Get component value (works for both scalar and vec3)
template<VoxelComponent T>
auto getValue(const T& component) {
    if constexpr (Vec3Component<T>) {
        return component.toVec3();
    } else {
        return component.value;
    }
}

// Set component value (works for both scalar and vec3)
template<ScalarComponent T, typename ValueType>
void setValue(T& component, const ValueType& value) {
    component.value = value;
}

template<Vec3Component T>
void setValue(T& component, const glm::vec3& value) {
    component = T(value);  // Uses conversion constructor
}

} // namespace GaiaVoxel
