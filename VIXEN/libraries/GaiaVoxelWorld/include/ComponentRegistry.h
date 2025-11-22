#pragma once

#include "VoxelComponents.h"
#include <gaia.h>
#include <string_view>
#include <type_traits>

namespace GaiaVoxel {

/**
 * ComponentRegistry - Compile-time component type registry.
 *
 * Provides type-safe, zero-cost component access via compile-time constants.
 * Eliminates runtime string lookups and typo errors.
 *
 * Benefits:
 * - Zero runtime overhead (compile-time string_view constants)
 * - Type safety (ComponentTag<T> enforces valid component types)
 * - Autocomplete friendly (IDE shows all registered components)
 * - No string typos (compilation error instead of runtime lookup failure)
 *
 * Usage:
 *   // OLD: String-based (runtime lookup, typo-prone)
 *   entity.get<float>("density");
 *
 *   // NEW: Type-safe (compile-time, zero-cost)
 *   entity.get<ComponentRegistry::Density>();
 *
 *   // Or use tags directly
 *   using CR = ComponentRegistry;
 *   entity.get<CR::Density>();
 */
namespace ComponentRegistry {

// ============================================================================
// Component Type Tags (Compile-Time Constants)
// ============================================================================

/**
 * ComponentTag - Type-safe component wrapper.
 *
 * Wraps Gaia component type with compile-time name constant.
 * Enables both type-safe access AND human-readable names.
 */
template<typename TComponent>
struct ComponentTag {
    using Type = TComponent;
    static constexpr std::string_view name = TComponent::Name;

    // Get component ID (cached after first call)
    static uint32_t id(gaia::ecs::World& world) {
        return gaia::ecs::Component<TComponent>::id(world);
    }
};

// ============================================================================
// Spatial Components
// ============================================================================

/**
 * MortonKey - Position encoded as Morton code (8 bytes).
 * Range: [-1,048,576 to +1,048,575] per axis.
 */
using Position = ComponentTag<MortonKey>;

// ============================================================================
// Key Attribute (Determines Octree Structure)
// ============================================================================

/**
 * Density - Voxel opacity/solidity [0,1].
 * This is typically the KEY attribute for octree structure.
 */
using Density = ComponentTag<GaiaVoxel::Density>;

// ============================================================================
// Color Components (Split RGB for SoA optimization)
// ============================================================================

using ColorR = ComponentTag<Color_R>;
using ColorG = ComponentTag<Color_G>;
using ColorB = ComponentTag<Color_B>;

/**
 * ColorRGB - Convenience aggregate for full color access.
 *
 * Usage:
 *   auto [r, g, b] = ColorRGB::get(entity);
 *   ColorRGB::set(entity, 1.0f, 0.0f, 0.0f);  // Red
 */
struct ColorRGB {
    static glm::vec3 get(gaia::ecs::Entity entity) {
        if (!entity.has<Color_R>() || !entity.has<Color_G>() || !entity.has<Color_B>()) {
            return glm::vec3(1.0f);  // Default white
        }
        return glm::vec3(
            entity.get<Color_R>().value,
            entity.get<Color_G>().value,
            entity.get<Color_B>().value
        );
    }

    static void set(gaia::ecs::Entity entity, float r, float g, float b) {
        if ( world.exists(entity)) {
            entity.set<Color_R>(Color_R{r});
            entity.set<Color_G>(Color_G{g});
            entity.set<Color_B>(Color_B{b});
        }
    }

    static void set(gaia::ecs::Entity entity, const glm::vec3& color) {
        set(entity, color.x, color.y, color.z);
    }
};

// ============================================================================
// Normal Components (Split XYZ for SoA optimization)
// ============================================================================

using NormalX = ComponentTag<Normal_X>;
using NormalY = ComponentTag<Normal_Y>;
using NormalZ = ComponentTag<Normal_Z>;

/**
 * NormalXYZ - Convenience aggregate for full normal access.
 */
struct NormalXYZ {
    static glm::vec3 get(gaia::ecs::Entity entity) {
        if (!entity.has<Normal_X>() || !entity.has<Normal_Y>() || !entity.has<Normal_Z>()) {
            return glm::vec3(0.0f, 1.0f, 0.0f);  // Default +Y
        }
        return glm::vec3(
            entity.get<Normal_X>().value,
            entity.get<Normal_Y>().value,
            entity.get<Normal_Z>().value
        );
    }

    static void set(gaia::ecs::Entity entity, float x, float y, float z) {
        if ( world.exists(entity)) {
            entity.set<Normal_X>(Normal_X{x});
            entity.set<Normal_Y>(Normal_Y{y});
            entity.set<Normal_Z>(Normal_Z{z});
        }
    }

    static void set(gaia::ecs::Entity entity, const glm::vec3& normal) {
        set(entity, normal.x, normal.y, normal.z);
    }
};

// ============================================================================
// Material Component
// ============================================================================

using Material = ComponentTag<GaiaVoxel::Material>;

// ============================================================================
// Emission Components (Split RGBI for SoA optimization)
// ============================================================================

using EmissionR = ComponentTag<Emission_R>;
using EmissionG = ComponentTag<Emission_G>;
using EmissionB = ComponentTag<Emission_B>;
using EmissionIntensity = ComponentTag<Emission_Intensity>;

/**
 * EmissionRGBI - Convenience aggregate for full emission access.
 */
struct EmissionRGBI {
    static glm::vec4 get(gaia::ecs::Entity entity) {
        if (!entity.has<Emission_R>() || !entity.has<Emission_G>() ||
            !entity.has<Emission_B>() || !entity.has<Emission_Intensity>()) {
            return glm::vec4(0.0f);  // No emission
        }
        return glm::vec4(
            entity.get<Emission_R>().value,
            entity.get<Emission_G>().value,
            entity.get<Emission_B>().value,
            entity.get<Emission_Intensity>().value
        );
    }

    static void set(gaia::ecs::Entity entity, float r, float g, float b, float intensity) {
        if ( world.exists(entity)) {
            entity.set<Emission_R>(Emission_R{r});
            entity.set<Emission_G>(Emission_G{g});
            entity.set<Emission_B>(Emission_B{b});
            entity.set<Emission_Intensity>(Emission_Intensity{intensity});
        }
    }

    static void set(gaia::ecs::Entity entity, const glm::vec3& color, float intensity) {
        set(entity, color.x, color.y, color.z, intensity);
    }
};

// ============================================================================
// Brick Metadata Components
// ============================================================================

using BrickRef = ComponentTag<BrickReference>;
using Chunk = ComponentTag<ChunkID>;

// ============================================================================
// Component Iteration Helpers
// ============================================================================

/**
 * ComponentList - Compile-time list of all registered components.
 *
 * Used for automatic registration, serialization, reflection.
 */
template<typename... TComponents>
struct ComponentList {
    static constexpr size_t count = sizeof...(TComponents);

    // Apply function to each component type
    template<typename Func>
    static void forEach(Func&& func) {
        (func.template operator()<TComponents>(), ...);
    }

    // Register all components with ECS world
    static void registerAll(gaia::ecs::World& world) {
        (gaia::ecs::Component<TComponents>::id(world), ...);
    }
};

/**
 * AllComponents - Complete list of all voxel components.
 */
using AllComponents = ComponentList<
    // Spatial
    MortonKey,

    // Key attribute
    GaiaVoxel::Density,

    // Color (split)
    Color_R, Color_G, Color_B,

    // Normal (split)
    Normal_X, Normal_Y, Normal_Z,

    // Material
    GaiaVoxel::Material,

    // Emission (split)
    Emission_R, Emission_G, Emission_B, Emission_Intensity,

    // Metadata
    BrickReference, ChunkID
>;

/**
 * CoreComponents - Minimal set for basic voxel rendering.
 */
using CoreComponents = ComponentList<
    MortonKey,
    GaiaVoxel::Density,
    Color_R, Color_G, Color_B
>;

// ============================================================================
// Type Traits for Component Validation
// ============================================================================

/**
 * is_valid_component - Check if type is a registered component.
 */
template<typename T>
struct is_valid_component : std::false_type {};

// Specialize for all registered components
template<> struct is_valid_component<MortonKey> : std::true_type {};
template<> struct is_valid_component<GaiaVoxel::Density> : std::true_type {};
template<> struct is_valid_component<Color_R> : std::true_type {};
template<> struct is_valid_component<Color_G> : std::true_type {};
template<> struct is_valid_component<Color_B> : std::true_type {};
template<> struct is_valid_component<Normal_X> : std::true_type {};
template<> struct is_valid_component<Normal_Y> : std::true_type {};
template<> struct is_valid_component<Normal_Z> : std::true_type {};
template<> struct is_valid_component<GaiaVoxel::Material> : std::true_type {};
template<> struct is_valid_component<Emission_R> : std::true_type {};
template<> struct is_valid_component<Emission_G> : std::true_type {};
template<> struct is_valid_component<Emission_B> : std::true_type {};
template<> struct is_valid_component<Emission_Intensity> : std::true_type {};
template<> struct is_valid_component<BrickReference> : std::true_type {};
template<> struct is_valid_component<ChunkID> : std::true_type {};

template<typename T>
inline constexpr bool is_valid_component_v = is_valid_component<T>::value;

/**
 * Compile-time validation for component types.
 *
 * Usage:
 *   static_assert(is_valid_component_v<Density>, "Invalid component");
 */
template<typename T>
concept ValidComponent = is_valid_component_v<T>;

} // namespace ComponentRegistry

// ============================================================================
// Convenience Aliases (Global Scope)
// ============================================================================

/**
 * Short alias for ComponentRegistry.
 *
 * Usage:
 *   using CR = GaiaVoxel::CR;
 *   entity.get<CR::Density>();
 */
namespace CR = ComponentRegistry;

} // namespace GaiaVoxel
