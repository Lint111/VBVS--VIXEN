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
    , m_depth(depth)
    , m_brickSize(1u << depth)  // 2^depth
    , m_voxelsPerBrick(m_brickSize * m_brickSize * m_brickSize)  // brickSize³
    , m_usesEntitySpan(true) {
}

EntityBrickView::EntityBrickView(
    GaiaVoxelWorld& world,
    glm::vec3 rootPositionInWorldSpace,
    uint8_t depth,
    float voxelSize)
    : m_world(world)
    , m_entities()  // Empty span
    , m_rootPositionInWorldSpace(rootPositionInWorldSpace)
    , m_depth(depth)
    , m_brickSize(1u << depth)
    , m_voxelsPerBrick(m_brickSize * m_brickSize * m_brickSize)
    , m_voxelSize(voxelSize)
    , m_usesEntitySpan(false) {
}

// ============================================================================
// Entity Access (Direct)
// ============================================================================

gaia::ecs::Entity EntityBrickView::getEntity(size_t voxelIdx) const {
    if (voxelIdx >= m_voxelsPerBrick) {
        return gaia::ecs::Entity(); // Invalid entity
    }

    if (m_usesEntitySpan) {
        // Span-based mode: direct array access
        return m_entities[voxelIdx];
    } else {
        // MortonKey-based mode: query ECS
        int x, y, z;
        linearIndexToCoord(voxelIdx, x, y, z);

        // Compute world space position: root + local offset * voxelSize
        // Voxel coords (x,y,z) are in [0, brickSize), multiply by voxelSize to get world offset
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
