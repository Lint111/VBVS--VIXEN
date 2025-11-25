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
