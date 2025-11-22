#pragma once

#include "VoxelComponents.h"
#include <gaia.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>

namespace GaiaVoxel {

// Forward declarations
class GaiaVoxelWorld;

/**
 * VoxelInjector - Inserts GaiaVoxelWorld entities into SVO spatial index.
 *
 * Architecture:
 * - Reads entity positions from GaiaVoxelWorld
 * - Groups entities by brick coordinate (spatial locality optimization)
 * - Inserts grouped entities into LaineKarrasOctree
 * - Triggers octree compaction after batch insertions
 *
 * Responsibilities:
 * - SVO spatial indexing (NOT entity creation - that's GaiaVoxelWorld's job)
 * - Brick-level batching for optimal tree traversal
 * - Compaction coordination
 *
 * Example:
 *   GaiaVoxelWorld world;
 *   // LaineKarrasOctree octree(...);  // SVO spatial index
 *   VoxelInjector injector(world);
 *
 *   // Get entities from queue
 *   auto entities = queue.getCreatedEntities();
 *
 *   // Insert into SVO (batched, optimized)
 *   injector.insertEntitiesBatched(entities, octree);
 *   injector.compactOctree(octree);
 */
class VoxelInjector {
public:
    /**
     * Create injector for given world.
     * @param world GaiaVoxelWorld containing entities to index
     */
    explicit VoxelInjector(GaiaVoxelWorld& world);

    // ========================================================================
    // Entity Insertion API
    // ========================================================================

    /**
     * Insert entities into SVO spatial index.
     * Entities must already exist in GaiaVoxelWorld.
     * @param entities Entity IDs to insert
     * @param svo Target SVO structure (must support insertVoxel API)
     * @param brickResolution Brick size for grouping (default: 8)
     * @return Number of entities successfully inserted
     */
    template<typename SVOType>
    size_t insertEntities(
        const std::vector<gaia::ecs::Entity>& entities,
        SVOType& svo,
        int brickResolution = 8);

    /**
     * Insert entities with brick-level batching.
     * Groups entities by brick coordinate before insertion.
     * MUCH faster than individual insertion for large batches.
     * @param entities Entity IDs to insert
     * @param svo Target SVO structure
     * @param brickResolution Brick size for grouping (default: 8)
     * @return Number of entities successfully inserted
     */
    template<typename SVOType>
    size_t insertEntitiesBatched(
        const std::vector<gaia::ecs::Entity>& entities,
        SVOType& svo,
        int brickResolution = 8);

    /**
     * Compact SVO octree after batch insertions.
     * Reorganizes descriptors into ESVO format.
     * @param svo Target SVO structure
     */
    template<typename SVOType>
    void compactOctree(SVOType& svo);

    // ========================================================================
    // Brick Grouping Utilities
    // ========================================================================

    /**
     * Group entities by brick coordinate.
     * Used internally by insertEntitiesBatched().
     * Exposed for advanced usage (custom insertion logic).
     */
    struct BrickCoord {
        int x, y, z;

        bool operator==(const BrickCoord& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct BrickCoordHash {
        size_t operator()(const BrickCoord& coord) const {
            // Simple hash combining x, y, z
            return std::hash<int>()(coord.x) ^
                   (std::hash<int>()(coord.y) << 1) ^
                   (std::hash<int>()(coord.z) << 2);
        }
    };

    using BrickEntityMap = std::unordered_map<BrickCoord, std::vector<gaia::ecs::Entity>, BrickCoordHash>;

    /**
     * Group entities by brick coordinate.
     * @param entities Entity IDs to group
     * @param brickResolution Brick size (default: 8)
     * @return Map of brick coord → entity IDs
     */
    BrickEntityMap groupByBrick(
        const std::vector<gaia::ecs::Entity>& entities,
        int brickResolution = 8) const;

    // ========================================================================
    // Statistics
    // ========================================================================

    struct Stats {
        size_t totalInserted;      // Total entities inserted
        size_t failedInsertions;   // Out-of-bounds or errors
        size_t brickCount;         // Number of unique bricks touched
    };

    Stats getLastInsertionStats() const { return m_lastStats; }

private:
    GaiaVoxelWorld& m_world;
    Stats m_lastStats{};

    // Helper: Compute brick coordinate from position
    BrickCoord computeBrickCoord(const glm::vec3& position, int brickResolution) const;
};

} // namespace GaiaVoxel

// Template implementation (must be in header for template instantiation)
#include "VoxelInjector.inl"
