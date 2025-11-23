#pragma once

#include <gaia.h>
#include "VoxelComponents.h"
#include "ComponentData.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <optional>
#include <span>

namespace GaiaVoxel {

/**
 * Central ECS-based voxel data management.
 *
 * Single source of truth for all voxel data. AttributeRegistry, VoxelInjectionQueue,
 * and SVO trees all reference entities via EntityID instead of copying data.
 *
 * Thread-safe: Gaia ECS handles concurrent entity access via lock-free SoA storage.
 *
 * Example usage:
 *   GaiaVoxelWorld world;
 *   auto voxelID = world.createVoxel(pos, 1.0f, red);
 *   auto voxels = world.queryRegion(minBounds, maxBounds);
 *   for (auto id : voxels) {
 *       auto pos = world.getPosition(id);
 *       auto color = world.getColor(id);
 *   }
 */
class GaiaVoxelWorld {
public:
    using EntityID = gaia::ecs::Entity;

    GaiaVoxelWorld();
    ~GaiaVoxelWorld();

    // Disable copy (ECS world is unique)
    GaiaVoxelWorld(const GaiaVoxelWorld&) = delete;
    GaiaVoxelWorld& operator=(const GaiaVoxelWorld&) = delete;

    // ========================================================================
    // Entity Creation/Deletion
    // ========================================================================

    /**
     * Create voxel entity with required components.
     * @param position World-space position
     * @param density Density/opacity [0,1]
     * @param color RGB color (optional, defaults to white)
     * @param normal Surface normal (optional, defaults to +Y)
     * @return Entity ID for referencing voxel
     */
    EntityID createVoxel(
        const glm::vec3& position,
        float density = 1.0f,
        const glm::vec3& color = glm::vec3(1.0f),
        const glm::vec3& normal = glm::vec3(0.0f, 1.0f, 0.0f));

    /**
     * Create voxel from component array (type-safe, zero string lookups).
     * Components are actual types (Density, Color, etc.) not string names.
     *
     * Example:
     *   ComponentData attrs[] = {
     *       Density{0.8f},
     *       Color{glm::vec3(1, 0, 0)},
     *       Normal{glm::vec3(0, 1, 0)}
     *   };
     *   auto id = world.createVoxel(position, attrs);
     */
    EntityID createVoxel(
        const glm::vec3& position,
        std::span<const ComponentData> components);

    /**
     * Create voxel with brick reference (for dense brick storage).
     */
    EntityID createVoxelInBrick(
        const glm::vec3& position,
        float density,
        const glm::vec3& color,
        const glm::vec3& normal,
        uint32_t brickID,
        uint8_t localX,
        uint8_t localY,
        uint8_t localZ);

    /**
     * Create an array of voxels from VoxelCreationRequest.
     * Each voxel can have different component sets.
     *
     * Example:
     *   ComponentData attrs1[] = {Density{0.8f}, Color{red}};
     *   ComponentData attrs2[] = {Density{0.5f}, Color{blue}};
     *   VoxelCreationRequest reqs[] = {
     *       {pos1, attrs1},
     *       {pos2, attrs2}
     *   };
     *   auto ids = world.createVoxelsBatch(reqs);
     */
    std::vector<EntityID> createVoxelsBatch(std::span<const VoxelCreationRequest> requests);

    /**
     * Destroy voxel entity.
     */
    void destroyVoxel(EntityID id);

    /**
     * Destroy all voxels (clear world).
     */
    void clear();

    // ========================================================================
    // Component Access (Thread-Safe)
    // ========================================================================

    // Getters
    std::optional<glm::vec3> getPosition(EntityID id) const;
    std::optional<float> getDensity(EntityID id) const;
    std::optional<glm::vec3> getColor(EntityID id) const;
    std::optional<glm::vec3> getNormal(EntityID id) const;
    std::optional<uint32_t> getBrickID(EntityID id) const;

    // Setters
    void setPosition(EntityID id, const glm::vec3& position);
    void setDensity(EntityID id, float density);
    void setColor(EntityID id, const glm::vec3& color);
    void setNormal(EntityID id, const glm::vec3& normal);

    // Check component existence (type-safe template version preferred)
    template<typename Component>
    bool hasComponent(EntityID id) const;

    // String-based overload (uses ComponentRegistry for dynamic lookup)
    bool hasComponent(EntityID id, const char* componentName) const;

    bool exists(EntityID id) const;

    // ========================================================================
    // Spatial Queries
    // ========================================================================

    /**
     * Query all voxels in AABB region.
     * Returns entity IDs for iteration.
     */
    std::vector<EntityID> queryRegion(const glm::vec3& min, const glm::vec3& max) const;

    /**
     * Query voxels by brick coordinate.
     * Efficient for batch processing during injection.
     */
    std::vector<EntityID> queryBrick(const glm::ivec3& brickCoord, int brickResolution = 8) const;

    /**
     * Query all solid voxels (density > 0).
     */
    std::vector<EntityID> querySolidVoxels() const;

    /**
     * Count voxels in region (faster than queryRegion().size()).
     */
    size_t countVoxelsInRegion(const glm::vec3& min, const glm::vec3& max) const;

    // ========================================================================
    // Batch Operations (for VoxelInjectionQueue)
    // ========================================================================
    // Note: createVoxelsBatch is defined above in Entity Creation section

    /**
     * Destroy multiple voxels in parallel.
     */
    void destroyVoxelsBatch(const std::vector<EntityID>& ids);

    // ========================================================================
    // Statistics
    // ========================================================================

    struct Stats {
        size_t totalEntities;
        size_t solidVoxels;
        size_t memoryUsageBytes;
    };

    Stats getStats() const;

    // ========================================================================
    // Advanced: Direct ECS Access (for custom queries)
    // ========================================================================

    /**
     * Get underlying ECS world for advanced queries.
     * Use with caution - direct world access bypasses API safety.
     */
    gaia::ecs::World& getWorld();
    const gaia::ecs::World& getWorld() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    // Helper: Compute spatial hash for position
    uint64_t computeSpatialHash(const glm::vec3& position) const;
};

// ============================================================================
// Template Implementations
// ============================================================================

template<typename Component>
bool GaiaVoxelWorld::hasComponent(EntityID id) const {
    return getWorld().valid(id) && getWorld().has<Component>(id);
}

} // namespace GaiaVoxel
