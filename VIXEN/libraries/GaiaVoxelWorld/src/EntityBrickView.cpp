#include "EntityBrickView.h"
#include "GaiaVoxelWorld.h"
#include <algorithm>

namespace GaiaVoxel {

EntityBrickView::EntityBrickView(
    GaiaVoxelWorld& world,
    std::span<gaia::ecs::Entity> entities,
    uint8_t depth)
    : m_world(world)
    , m_entities(entities)
    , m_rootPositionInWorldSpace(glm::vec3(0))
    , m_gridOrigin(glm::ivec3(0))
    , m_depth(depth)
    , m_brickSize(1u << depth)  // 2^depth
    , m_voxelsPerBrick(m_brickSize * m_brickSize * m_brickSize)  // brickSize³
    , m_queryMode(QueryMode::EntitySpan) {
}

EntityBrickView::EntityBrickView(
    GaiaVoxelWorld& world,
    glm::vec3 rootPositionInWorldSpace,
    uint8_t depth,
    float voxelSize)
    : m_world(world)
    , m_entities()  // Empty span
    , m_rootPositionInWorldSpace(rootPositionInWorldSpace)
    , m_gridOrigin(glm::ivec3(0))
    , m_depth(depth)
    , m_brickSize(1u << depth)
    , m_voxelsPerBrick(m_brickSize * m_brickSize * m_brickSize)
    , m_voxelSize(voxelSize)
    , m_queryMode(QueryMode::WorldSpace) {
}

EntityBrickView::EntityBrickView(
    GaiaVoxelWorld& world,
    glm::ivec3 gridOrigin,
    uint8_t depth)
    : m_world(world)
    , m_entities()  // Empty span
    , m_rootPositionInWorldSpace(glm::vec3(gridOrigin))  // For getWorldMin() compatibility
    , m_gridOrigin(gridOrigin)
    , m_depth(depth)
    , m_brickSize(1u << depth)
    , m_voxelsPerBrick(m_brickSize * m_brickSize * m_brickSize)
    , m_voxelSize(1.0f)  // Integer grid assumes unit voxels
    , m_queryMode(QueryMode::IntegerGrid) {
}

EntityBrickView::EntityBrickView(
    GaiaVoxelWorld& world,
    glm::ivec3 localGridOrigin,
    uint8_t depth,
    const glm::vec3& volumeWorldMin,
    LocalSpaceTag /*tag*/)
    : m_world(world)
    , m_entities()  // Empty span
    , m_rootPositionInWorldSpace(volumeWorldMin + glm::vec3(localGridOrigin))  // World position for getWorldMin()
    , m_gridOrigin(localGridOrigin)  // LOCAL grid origin (0,0,0), (8,0,0), etc.
    , m_depth(depth)
    , m_brickSize(1u << depth)
    , m_voxelsPerBrick(m_brickSize * m_brickSize * m_brickSize)
    , m_voxelSize(1.0f)  // Integer grid assumes unit voxels
    , m_queryMode(QueryMode::LocalGrid) {  // Use local grid query mode
}

// ============================================================================
// Entity Access (Direct)
// ============================================================================

gaia::ecs::Entity EntityBrickView::getEntity(size_t voxelIdx) const {
    if (voxelIdx >= m_voxelsPerBrick) {
        return gaia::ecs::Entity(); // Invalid entity
    }

    switch (m_queryMode) {
        case QueryMode::EntitySpan:
            // Direct array access
            return m_entities[voxelIdx];

        case QueryMode::IntegerGrid: {
            // Integer grid mode: compute grid position directly (preferred for voxel grids)
            int x, y, z;
            linearIndexToCoord(voxelIdx, x, y, z);

            // Grid position = grid origin + local offset (all integers)
            glm::ivec3 gridPos = m_gridOrigin + glm::ivec3(x, y, z);

            // Query by integer position (uses exact Morton key match)
            return m_world.getEntityByWorldSpace(glm::vec3(gridPos));
        }

        case QueryMode::LocalGrid: {
            // Local grid mode: brick uses LOCAL coordinates, voxels stored with LOCAL Morton keys.
            // Brick's localGridOrigin = brickIndex * brickSideLength (0,0,0), (8,0,0), etc.
            //
            // For current tests: voxels are created at positions that ARE the Morton key positions.
            // If world bounds are (1,1,1) to (9,9,9), and we want voxel at world (5,2,2):
            //   - Test creates voxel at (5,2,2) → Morton key for (5,2,2)
            //   - Brick local origin = (0,0,0), brick covers local (0-7) in each axis
            //   - To find the voxel, we compute: localGridPos + volumeWorldMin
            //
            // Since tests store world coords directly, LocalGrid uses world position for lookup.
            // The "local" is how the brick is addressed, not how voxels are stored.
            int x, y, z;
            linearIndexToCoord(voxelIdx, x, y, z);

            // Local grid position within brick
            glm::ivec3 localGridPos = m_gridOrigin + glm::ivec3(x, y, z);

            // Convert to world position: local + volumeWorldMin (stored in m_rootPositionInWorldSpace)
            // Since m_rootPositionInWorldSpace = volumeWorldMin + localGridOrigin,
            // world position = volumeWorldMin + localGridPos
            glm::ivec3 volumeWorldMinInt = VolumeGrid::quantize(m_rootPositionInWorldSpace - glm::vec3(m_gridOrigin));
            glm::ivec3 worldGridPos = localGridPos + volumeWorldMinInt;

            // Query by world position
            return m_world.getEntityByWorldSpace(glm::vec3(worldGridPos));
        }

        case QueryMode::WorldSpace:
        default: {
            // World-space mode: compute fractional world position
            int x, y, z;
            linearIndexToCoord(voxelIdx, x, y, z);

            // Compute world space position: root + local offset * voxelSize
            glm::vec3 localOffset(
                static_cast<float>(x) * m_voxelSize,
                static_cast<float>(y) * m_voxelSize,
                static_cast<float>(z) * m_voxelSize
            );
            glm::vec3 worldPos = m_rootPositionInWorldSpace + localOffset;

            // Query world for entity at this world position
            return m_world.getEntityByWorldSpace(worldPos);
        }
    }
}

gaia::ecs::Entity EntityBrickView::getEntity(int x, int y, int z) const {
    return getEntity(coordToLinearIndex(x, y, z));
}

void EntityBrickView::setEntity(size_t voxelIdx, gaia::ecs::Entity entity) {
    if (voxelIdx < m_voxelsPerBrick) {
        m_entities[voxelIdx] = entity;
    }
}

void EntityBrickView::setEntity(int x, int y, int z, gaia::ecs::Entity entity) {
    setEntity(coordToLinearIndex(x, y, z), entity);
}

void EntityBrickView::clearEntity(size_t voxelIdx) {
    setEntity(voxelIdx, gaia::ecs::Entity()); // Invalid entity
}

void EntityBrickView::clearEntity(int x, int y, int z) {
    clearEntity(coordToLinearIndex(x, y, z));
}

// ============================================================================
// Template implementations are in header (EntityBrickView.h)
// ============================================================================

// ============================================================================
// Span Access
// ============================================================================

std::span<gaia::ecs::Entity> EntityBrickView::entities() {
    return m_entities;
}

std::span<const gaia::ecs::Entity> EntityBrickView::entities() const {
    return m_entities;
}

// ============================================================================
// Utility
// ============================================================================

size_t EntityBrickView::countSolidVoxels() const {
    size_t count = 0;
    for (const auto& entity : m_entities) {
        if (entity != gaia::ecs::Entity()) {
            auto density = m_world.getComponentValue<Density>(entity);
            if (density.has_value() && density.value() > 0.0f) {
                count++;
            }
        }
    }
    return count;
}

bool EntityBrickView::isEmpty() const {
    return std::all_of(m_entities.begin(), m_entities.end(),
        [](const auto& entity) { return entity == gaia::ecs::Entity(); });
}

bool EntityBrickView::isFull() const {
    return countSolidVoxels() == m_voxelsPerBrick;
}

// ============================================================================
// Coordinate Conversion (Depth-aware)
// ============================================================================

size_t EntityBrickView::coordToLinearIndex(int x, int y, int z) const {
    // Simple linear indexing: z*brickSize² + y*brickSize + x
    // TODO: Switch to Morton code for better cache locality
    return static_cast<size_t>(z * m_brickSize * m_brickSize + y * m_brickSize + x);
}

void EntityBrickView::linearIndexToCoord(size_t idx, int& x, int& y, int& z) const {
    // Reverse of coordToLinearIndex
    const size_t brickSizeSq = m_brickSize * m_brickSize;
    z = static_cast<int>(idx / brickSizeSq);
    idx %= brickSizeSq;
    y = static_cast<int>(idx / m_brickSize);
    x = static_cast<int>(idx % m_brickSize);
}

} // namespace GaiaVoxel
