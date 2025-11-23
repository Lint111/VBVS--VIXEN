#pragma once

#include "VoxelComponents.h"
#include <glm/glm.hpp>
#include <variant>
#include <span>
#include <cstdint>

namespace GaiaVoxel {

/**
 * Type-safe component variant containing actual component types.
 *
 * Stores any component from ComponentRegistry as a variant.
 * No string lookups - component type IS the identifier.
 *
 * Example:
 *   ComponentData{Density{0.8f}}
 *   ComponentData{Color{glm::vec3(1, 0, 0)}}
 *   ComponentData{Material{42}}
 */
using ComponentVariant = std::variant<
    Density,
    Material,
    EmissionIntensity,
    Color,
    Normal,
    Emission,
    MortonKey
>;

/**
 * Single component instance with compile-time type.
 *
 * Benefits:
 * - Zero string lookups (component type is known at compile time)
 * - Type-safe (impossible to assign wrong value type)
 * - Component name accessible via Component::Name static member
 * - visitByName() uses compile-time dispatch via std::visit
 *
 * Usage:
 *   ComponentData data{Density{0.8f}};
 *   std::visit([](auto&& component) {
 *       using T = std::decay_t<decltype(component)>;
 *       std::cout << T::Name << std::endl;  // "density"
 *   }, data.component);
 */
struct ComponentData {
    ComponentVariant component;

    // Implicit conversion from any component type
    template<typename T>
    ComponentData(const T& comp) : component(comp) {}

    ComponentData() = default;
};

/**
 * Voxel creation request with type-safe component list.
 *
 * Benefits over DynamicVoxelScalar:
 * - No VoxelData dependency (uses VoxelComponents only)
 * - Compile-time type safety (impossible to pass wrong value type)
 * - Zero string lookups (component types known at compile time)
 * - Zero allocation (stack-based component array)
 * - std::span avoids copies
 *
 * Usage:
 *   ComponentData attrs[] = {
 *       Density{0.8f},
 *       Color{glm::vec3(1, 0, 0)},
 *       Normal{glm::vec3(0, 1, 0)},
 *       Material{42}
 *   };
 *   VoxelCreationRequest req{position, attrs};
 *
 *   // Process components:
 *   for (const auto& data : req.components) {
 *       std::visit([&](auto&& component) {
 *           using T = std::decay_t<decltype(component)>;
 *           world.add<T>(entity, component);  // Type-safe add
 *       }, data.component);
 *   }
 */
struct VoxelCreationRequest {
    glm::vec3 position;
    std::span<const ComponentData> components;

    VoxelCreationRequest() = default;
    VoxelCreationRequest(const glm::vec3& pos, std::span<const ComponentData> comps)
        : position(pos), components(comps) {}
};

/**
 * Batch voxel creation request.
 * Allows multiple voxels to share component definitions.
 */
struct VoxelCreationBatch {
    std::span<const glm::vec3> positions;        // N positions
    std::span<const ComponentData> components;   // Shared components

    VoxelCreationBatch() = default;
    VoxelCreationBatch(
        std::span<const glm::vec3> pos,
        std::span<const ComponentData> comps)
        : positions(pos), components(comps) {}
};

/**
 * Query result for voxel lookup.
 * Returns entity handle + component accessor.
 */
struct VoxelQueryResult {
    uint64_t entityID = 0;  // Gaia entity ID (or 0 if not found)
    bool exists = false;

    // Component access via ComponentRegistry::visitByName()
    // Example: world.get<Density>(Entity::from_id(result.entityID))
};

} // namespace GaiaVoxel
