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
 * the 512-entity array representing an 8³ brick.
 *
 * Architecture:
 * - Read/write entity references (8 bytes each)
 * - Component access via Gaia ECS (no data duplication)
 * - Zero-copy iteration via std::span
 *
 * Memory Layout:
 * - 512 entities × 8 bytes = 4 KB per brick
 * - vs OLD: 512 voxels × 140 bytes = 70 KB per brick (17.5× reduction!)
 *
 * Example:
 *   std::array<gaia::ecs::Entity, 512> brickEntities;
 *   EntityBrickView brick(world, brickEntities);
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
 *       if (entity.valid()) {
 *           // Process entity
 *       }
 *   }
 */
class EntityBrickView {
public:
    static constexpr size_t BRICK_SIZE = 8;
    static constexpr size_t VOXELS_PER_BRICK = 512; // 8³

    /**
     * Create brick view over entity array.
     * @param world GaiaVoxelWorld for component access
     * @param entities Reference to brick's entity array (512 entities)
     */
    EntityBrickView(GaiaVoxelWorld& world, std::array<gaia::ecs::Entity, 512>& entities);

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
    // Component Access (Convenience)
    // ========================================================================

    // Read component values
    std::optional<float> getDensity(size_t voxelIdx) const;
    std::optional<float> getDensity(int x, int y, int z) const;

    std::optional<glm::vec3> getColor(size_t voxelIdx) const;
    std::optional<glm::vec3> getColor(int x, int y, int z) const;

    std::optional<glm::vec3> getNormal(size_t voxelIdx) const;
    std::optional<glm::vec3> getNormal(int x, int y, int z) const;

    std::optional<uint32_t> getMaterialID(size_t voxelIdx) const;
    std::optional<uint32_t> getMaterialID(int x, int y, int z) const;

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

    /**
     * Convert 3D coordinate to linear index.
     * Uses Morton code ordering for cache locality.
     */
    static size_t coordToLinearIndex(int x, int y, int z);

    /**
     * Convert linear index to 3D coordinate.
     */
    static void linearIndexToCoord(size_t idx, int& x, int& y, int& z);

private:
    GaiaVoxelWorld& m_world;
    std::array<gaia::ecs::Entity, 512>& m_entities;
};

} // namespace GaiaVoxel
