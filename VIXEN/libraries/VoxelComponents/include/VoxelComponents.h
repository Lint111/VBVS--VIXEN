#pragma once

#include <gaia.h>
#include <glm/glm.hpp>
#include <variant>
#include <tuple>
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
struct MortonKey {
    static constexpr const char* Name = "position";
    uint64_t code = 0;

    // Decode position from Morton code
    glm::ivec3 toGridPos() const;
    glm::vec3 toWorldPos() const {
        glm::ivec3 grid = toGridPos();
        return glm::vec3(grid);
    }

    // Static factory methods
    static MortonKey fromPosition(const glm::vec3& pos);
    static MortonKey fromPosition(const glm::ivec3& pos);
};

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
// Component Registry - Macro-Based Automatic Registration
// ============================================================================

/**
 * Central component registry using X-macro pattern.
 *
 * To register a new component:
 * 1. Add REGISTER_COMPONENT(ComponentName) to VOXEL_COMPONENT_LIST
 * 2. ComponentVariant, AllComponents tuple, and traits are auto-updated
 *
 * Benefits:
 * - Single source of truth (no manual variant updates)
 * - Compile-time iteration via visitAll()
 * - Type-safe component lookup
 */

// ============================================================================
// SINGLE SOURCE OF TRUTH - Component Registry
// ============================================================================
/**
 * To register a new component:
 * 1. Define the component (VOXEL_COMPONENT_SCALAR or VOXEL_COMPONENT_VEC3)
 * 2. Add APPLY_MACRO(macro, ComponentName) to FOR_EACH_COMPONENT below
 * 3. ComponentVariant, AllComponents tuple, and ComponentTraits auto-update
 *
 * This is the ONLY place you need to add component names!
 */
#define APPLY_MACRO(macro, ...) macro(__VA_ARGS__)
#define FOR_EACH_COMPONENT(macro) \
    APPLY_MACRO(macro, Density) \
    APPLY_MACRO(macro, Material) \
    APPLY_MACRO(macro, EmissionIntensity) \
    APPLY_MACRO(macro, Color) \
    APPLY_MACRO(macro, Normal) \
    APPLY_MACRO(macro, Emission) \
    APPLY_MACRO(macro, MortonKey)

namespace ComponentRegistry {
    // Generate AllComponents tuple from macro registry
    // NOTE: Tuple is only instantiated temporarily in visitAll() - not stored
    // Size when instantiated: ~56 bytes (7 components × ~8 bytes default-constructed)

    #define AS_TYPE(Component) Component,
    using AllComponents = std::tuple<FOR_EACH_COMPONENT(AS_TYPE) std::monostate>;
    #undef AS_TYPE

    // Visit all components with a lambda
    template<typename Visitor>
    constexpr void visitAll(Visitor&& visitor) {
        // Directly instantiate each component and call visitor
        #define VISIT_COMPONENT(Component) visitor(Component{});
        FOR_EACH_COMPONENT(VISIT_COMPONENT)
        #undef VISIT_COMPONENT
    }

    // Find component by name (compile-time)
    template<typename Visitor>
    constexpr bool visitByName(std::string_view name, Visitor&& visitor) {
        bool found = false;
        visitAll([&](auto component) {
            using Component = std::decay_t<decltype(component)>;
            if (Component::Name == name) {
                visitor(component);
                found = true;
            }
        });
        return found;
    }

} // namespace ComponentRegistry

// ============================================================================
// Component Variant Type
// ============================================================================

/**
 * Type-safe variant containing any registered component type.
 *
 * Automatically generated from FOR_EACH_COMPONENT macro registry.
 * Benefits:
 * - Zero string lookups - component type IS the identifier
 * - Type-safe - impossible to assign wrong value type
 * - Memory efficient - stores only ONE component at a time
 *
 * Memory layout:
 * - Size = sizeof(largest component) + sizeof(discriminator)
 * - Typical size: 16 bytes (12 for vec3 + 4 for discriminator)
 * - NOT a tuple (no allocation of all types simultaneously)
 *
 * Example:
 *   ComponentVariant v1 = Density{0.8f};       // holds Density only
 *   ComponentVariant v2 = Color{glm::vec3(1, 0, 0)};  // holds Color only
 */
#define AS_VARIANT_TYPE(Component) Component,
using ComponentVariant = std::variant<FOR_EACH_COMPONENT(AS_VARIANT_TYPE) std::monostate>;
#undef AS_VARIANT_TYPE

// ============================================================================
// Metadata Components
// ============================================================================

// NOTE: BrickReference REMOVED - Deprecated
// Brick storage is now a VIEW pattern (BrickView), not entity-stored.
// Dense brick data lives in contiguous arrays accessed via Morton offset + stride.
// See GaiaVoxelWorld.h for BrickView architecture.

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
// Component Traits (Auto-Generated from Single Source of Truth!)
// ============================================================================

template<typename T>
struct ComponentTraits;

// Reuse FOR_EACH_COMPONENT to generate traits
#define GENERATE_TRAIT(Component) \
    template<> \
    struct ComponentTraits<Component> { \
        static constexpr const char* Name = Component::Name; \
    };

FOR_EACH_COMPONENT(GENERATE_TRAIT)

#undef GENERATE_TRAIT

// ChunkID is metadata, not in main registry but needs traits
template<>
struct ComponentTraits<ChunkID> {
    static constexpr const char* Name = ChunkID::Name;
};

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
