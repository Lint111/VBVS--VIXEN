#pragma once

#include "VoxelComponents.h"
#include <gaia.h>
#include <glm/glm.hpp>
#include <array>
#include <span>
#include <optional>

namespace GaiaVoxel {

// Forward declaration
class GaiaVoxelWorld;

/**
 * EntityBrickView - Lightweight span over brick entity array.
 *
 * Does NOT own entity data - just provides convenient access to
 * entity array representing a depth³ brick (e.g., 8³ = 512 entities).
 *
 * Architecture:
 * - Read/write entity references (8 bytes each)
 * - Component access via Gaia ECS (no data duplication)
 * - Zero-copy iteration via std::span
 * - Depth-driven sizing (depth → brickSize → voxelsPerBrick)
 *
 * Memory Layout (depth = 8):
 * - 512 entities × 8 bytes = 4 KB per brick
 * - vs OLD: 512 voxels × 140 bytes = 70 KB per brick (17.5× reduction!)
 *
 * Example (depth 3 → 2³ = 8 → 8³ = 512 voxels):
 *   std::array<gaia::ecs::Entity, 512> brickEntities;
 *   EntityBrickView brick(world, brickEntities, 3);
 *
 *   // Derived values: brickSize = 2^3 = 8, voxelsPerBrick = 8³ = 512
 *   // For SVO: depth typically 3-5 → brickSize 8-32 → voxels 512-32768
 *
 *   // Set entity at voxel (4, 2, 1)
 *   auto entity = world.createVoxel(pos, 1.0f, red, normal);
 *   brick.setEntity(4, 2, 1, entity);
 *
 *   // Read density at voxel (4, 2, 1)
 *   float density = brick.getDensity(4, 2, 1).value_or(0.0f);
 *
 *   // Iterate all entities
 *   for (auto entity : brick.entities()) {
 *       if ( world.exists(entity)) {
 *           // Process entity
 *       }
 *   }
 */
class EntityBrickView {
public:
    /**
     * Create brick view over entity array.
     * @param world GaiaVoxelWorld for component access
     * @param entities Reference to brick's entity array
     * @param depth Brick depth in octree (depth → 2^depth = brickSize)
     *              Example: depth=3 → brickSize=8 → 8³=512 voxels
     *                       depth=4 → brickSize=16 → 16³=4096 voxels
     *                       depth=5 → brickSize=32 → 32³=32768 voxels
     */
    EntityBrickView(GaiaVoxelWorld& world, std::span<gaia::ecs::Entity> entities, uint8_t depth);

    // Depth-derived properties (set in constructor)
    [[nodiscard]] size_t getBrickSize() const { return m_brickSize; }
    [[nodiscard]] size_t getVoxelsPerBrick() const { return m_voxelsPerBrick; }
    [[nodiscard]] uint8_t getDepth() const { return m_depth; }

    // ========================================================================
    // Entity Access (Direct)
    // ========================================================================

    /**
     * Get entity at linear voxel index [0, 511].
     */
    gaia::ecs::Entity getEntity(size_t voxelIdx) const;

    /**
     * Get entity at 3D coordinate [0, 7] per axis.
     */
    gaia::ecs::Entity getEntity(int x, int y, int z) const;

    /**
     * Set entity at linear voxel index.
     */
    void setEntity(size_t voxelIdx, gaia::ecs::Entity entity);

    /**
     * Set entity at 3D coordinate.
     */
    void setEntity(int x, int y, int z, gaia::ecs::Entity entity);

    /**
     * Clear entity at voxel (set to invalid entity).
     */
    void clearEntity(size_t voxelIdx);
    void clearEntity(int x, int y, int z);

    // ========================================================================
    // Component Access (Generic Template API)
    // ========================================================================

    /**
     * Get component value at linear voxel index.
     * Uses VoxelComponents API - works with any registered component.
     *
     * Example:
     *   auto density = brick.getComponentValue<Density>(42);      // returns std::optional<float>
     *   auto color = brick.getComponentValue<Color>(4, 2, 1);     // returns std::optional<glm::vec3>
     */
    template<typename TComponent>
    auto getComponentValue(size_t voxelIdx) const -> std::optional<ComponentValueType_t<TComponent>>;

    template<typename TComponent>
    auto getComponentValue(int x, int y, int z) const -> std::optional<ComponentValueType_t<TComponent>> {
        return getComponentValue<TComponent>(coordToLinearIndex(x, y, z));
    }

    /**
     * Set component value at voxel index.
     * Creates component if entity doesn't have it.
     */
    template<typename TComponent>
    void setComponent(size_t voxelIdx, ComponentValueType_t<TComponent> value);

    template<typename TComponent>
    void setComponent(int x, int y, int z, ComponentValueType_t<TComponent> value) {
        setComponent<TComponent>(coordToLinearIndex(x, y, z), value);
    }

    /**
     * Check if entity has component.
     */
    template<typename TComponent>
    bool hasComponent(size_t voxelIdx) const;

    template<typename TComponent>
    bool hasComponent(int x, int y, int z) const {
        return hasComponent<TComponent>(coordToLinearIndex(x, y, z));
    }

    // ========================================================================
    // Span Access (Zero-Copy Iteration)
    // ========================================================================

    /**
     * Get span over all 512 entities.
     * Zero-copy iteration.
     */
    std::span<gaia::ecs::Entity> entities();
    std::span<const gaia::ecs::Entity> entities() const;

    // ========================================================================
    // Utility
    // ========================================================================

    /**
     * Count solid voxels (entities with density > 0).
     */
    size_t countSolidVoxels() const;

    /**
     * Check if brick is completely empty.
     */
    bool isEmpty() const;

    /**
     * Check if brick is completely solid.
     */
    bool isFull() const;

private:
    /**
     * Convert 3D coordinate to linear index.
     * Uses Morton code ordering for cache locality.
     */
    size_t coordToLinearIndex(int x, int y, int z) const;

    /**
     * Convert linear index to 3D coordinate.
     */
    void linearIndexToCoord(size_t idx, int& x, int& y, int& z) const;
    GaiaVoxelWorld& m_world;
    std::span<gaia::ecs::Entity> m_entities;

    // Depth-derived sizing
    uint8_t m_depth;                // Brick depth (3-8 typical for SVO)
    size_t m_brickSize;             // 2^depth (8, 16, 32, ... 256)
    size_t m_voxelsPerBrick;        // brickSize³ (512, 4096, 32768, ...)
};

// ============================================================================
// Template Implementation (Header-only for implicit instantiation)
// ============================================================================

// Forward declare GaiaVoxelWorld methods we need
// (actual implementation requires GaiaVoxelWorld.h included in .cpp files that use this)

template<typename TComponent>
auto EntityBrickView::getComponentValue(size_t voxelIdx) const -> std::optional<ComponentValueType_t<TComponent>> {
    auto entity = getEntity(voxelIdx);
    if (entity == gaia::ecs::Entity()) {
        return std::nullopt;
    }

    // Delegate to GaiaVoxelWorld's generic template API
    return m_world.getComponentValue<TComponent>(entity);
}

template<typename TComponent>
void EntityBrickView::setComponent(size_t voxelIdx, ComponentValueType_t<TComponent> value) {
    auto entity = getEntity(voxelIdx);
    if (entity == gaia::ecs::Entity()) {
        return; // Can't set component on invalid entity
    }

    // Delegate to GaiaVoxelWorld's generic template API
    m_world.setComponent<TComponent>(entity, value);
}

template<typename TComponent>
bool EntityBrickView::hasComponent(size_t voxelIdx) const {
    auto entity = getEntity(voxelIdx);
    if (entity == gaia::ecs::Entity()) {
        return false;
    }
    return m_world.hasComponent<TComponent>(entity);
}

} // namespace GaiaVoxel
