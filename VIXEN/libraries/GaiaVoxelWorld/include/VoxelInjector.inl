#pragma once

#include "VoxelInjector.h"
#include "GaiaVoxelWorld.h"
#include <iostream>

namespace Vixen::GaiaVoxel {

// ============================================================================
// Template Method Implementations
// ============================================================================

template<typename SVOType>
size_t VoxelInjector::insertEntities(
    const std::vector<gaia::ecs::Entity>& entities,
    SVOType& svo,
    int brickResolution) {

    m_lastStats = Stats{};
    size_t inserted = 0;

    for (const auto& entity : entities) {
        // Get position from entity
        auto posOpt = m_world.getPosition(entity);
        if (!posOpt) {
            m_lastStats.failedInsertions++;
            continue;
        }

        // For SVO insertion, we need to pass entity reference instead of copying data
        // The SVO will store the entity ID, not duplicate the voxel data
        // TODO: Update LaineKarrasOctree to accept entity IDs instead of DynamicVoxelScalar

        // For now, this is a placeholder that shows the intended flow
        // Once LaineKarrasOctree supports entity storage (Phase 3), this will work:
        // if (svo.insertEntity(*posOpt, entity)) {
        //     inserted++;
        // }

        inserted++;
    }

    m_lastStats.totalInserted = inserted;
    return inserted;
}

template<typename SVOType>
size_t VoxelInjector::insertEntitiesBatched(
    const std::vector<gaia::ecs::Entity>& entities,
    SVOType& svo,
    int brickResolution) {

    m_lastStats = Stats{};

    // Group entities by brick coordinate
    auto brickMap = groupByBrick(entities, brickResolution);
    m_lastStats.brickCount = brickMap.size();

    size_t inserted = 0;

    // Process each brick
    for (const auto& [brickCoord, brickEntities] : brickMap) {
        // Insert all entities in this brick
        for (const auto& entity : brickEntities) {
            auto posOpt = m_world.getPosition(entity);
            if (!posOpt) {
                m_lastStats.failedInsertions++;
                continue;
            }

            // Placeholder for entity-based SVO insertion
            // Once LaineKarrasOctree supports entity storage (Phase 3):
            // if (svo.insertEntity(*posOpt, entity)) {
            //     inserted++;
            // }

            inserted++;
        }
    }

    m_lastStats.totalInserted = inserted;
    return inserted;
}

template<typename SVOType>
void VoxelInjector::compactOctree(SVOType& svo) {
    // Trigger ESVO compaction on the octree
    // This assumes SVOType has a compactToESVOFormat() method
    // The old VoxelInjector in SVO library has this:
    // compactToESVOFormat(svo);

    // For now, placeholder that will be implemented when we update
    // LaineKarrasOctree API in Phase 3
    std::cout << "[VoxelInjector] Compacting octree (placeholder - implement in Phase 3)\n";
}

} // namespace Vixen::GaiaVoxel
