#include "VoxelInjector.h"
#include "GaiaVoxelWorld.h"
#include <cmath>

namespace GaiaVoxel {

VoxelInjector::VoxelInjector(GaiaVoxelWorld& world)
    : m_world(world) {
}

VoxelInjector::BrickCoord VoxelInjector::computeBrickCoord(
    const glm::vec3& position,
    int brickResolution) const {

    BrickCoord coord;
    coord.x = static_cast<int>(std::floor(position.x / brickResolution));
    coord.y = static_cast<int>(std::floor(position.y / brickResolution));
    coord.z = static_cast<int>(std::floor(position.z / brickResolution));
    return coord;
}

VoxelInjector::BrickEntityMap VoxelInjector::groupByBrick(
    const std::vector<gaia::ecs::Entity>& entities,
    int brickResolution) const {

    BrickEntityMap brickMap;

    for (const auto& entity : entities) {
        // Get position from world
        auto posOpt = m_world.getPosition(entity);
        if (!posOpt) {
            continue; // Skip entities without position
        }

        // Compute brick coordinate
        BrickCoord coord = computeBrickCoord(*posOpt, brickResolution);

        // Add entity to brick group
        brickMap[coord].push_back(entity);
    }

    return brickMap;
}

} // namespace GaiaVoxel
