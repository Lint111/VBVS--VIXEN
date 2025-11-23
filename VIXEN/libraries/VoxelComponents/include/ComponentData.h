#pragma once

#include "VoxelComponents.h"
#include <glm/glm.hpp>
#include <variant>
#include <span>
#include <cstdint>

namespace GaiaVoxel {

/**
 * Single component query/creation request with compile-time type safety.
 *
 * Wraps a ComponentVariant for use in voxel creation and query APIs.
 *
 * Benefits:
 * - Zero string lookups (component type is known at compile time)
 * - Type-safe (impossible to assign wrong value type)
 * - Component name accessible via Component::Name static member
 * - visitByName() uses compile-time dispatch via std::visit
 * - Automatically includes all components from FOR_EACH_COMPONENT macro
 *
 * Usage:
 *   ComponentQueryRequest req{Density{0.8f}};
 *   std::visit([](auto&& component) {
 *       using T = std::decay_t<decltype(component)>;
 *       std::cout << T::Name << std::endl;  // "density"
 *   }, req.component);
 */
struct ComponentQueryRequest {
    ComponentVariant component;

    // Implicit conversion from any registered component type
    template<typename T>
    ComponentQueryRequest(const T& comp) : component(comp) {}

    ComponentQueryRequest() = default;
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
 *   ComponentQueryRequest attrs[] = {
 *       Density{0.8f},
 *       Color{glm::vec3(1, 0, 0)},
 *       Normal{glm::vec3(0, 1, 0)},
 *       Material{42}
 *   };
 *   VoxelCreationRequest req{position, attrs};
 *
 *   // Process components:
 *   for (const auto& compReq : req.components) {
 *       std::visit([&](auto&& component) {
 *           using T = std::decay_t<decltype(component)>;
 *           world.add<T>(entity, component);  // Type-safe add
 *       }, compReq.component);
 *   }
 */
struct VoxelCreationRequest {
    glm::vec3 position;
    std::span<const ComponentQueryRequest> components;

    VoxelCreationRequest() = default;
    VoxelCreationRequest(const glm::vec3& pos, std::span<const ComponentQueryRequest> comps)
        : position(pos), components(comps) {}
};

/**
 * Batch voxel creation request.
 * Allows multiple voxels to share component definitions.
 */
struct VoxelCreationBatch {
    std::span<const glm::vec3> positions;             // N positions
    std::span<const ComponentQueryRequest> components;   // Shared components

    VoxelCreationBatch() = default;
    VoxelCreationBatch(
        std::span<const glm::vec3> pos,
        std::span<const ComponentQueryRequest> comps)
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
