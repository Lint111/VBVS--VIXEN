#include "EntityBrickView.h"
#include "GaiaVoxelWorld.h"
#include <algorithm>

namespace GaiaVoxel {

EntityBrickView::EntityBrickView(
    GaiaVoxelWorld& world,
    std::array<gaia::ecs::Entity, 512>& entities)
    : m_world(world), m_entities(entities) {
}

// ============================================================================
// Entity Access (Direct)
// ============================================================================

gaia::ecs::Entity EntityBrickView::getEntity(size_t voxelIdx) const {
    if (voxelIdx >= VOXELS_PER_BRICK) {
        return gaia::ecs::Entity(); // Invalid entity
    }
    return m_entities[voxelIdx];
}

gaia::ecs::Entity EntityBrickView::getEntity(int x, int y, int z) const {
    return getEntity(coordToLinearIndex(x, y, z));
}

void EntityBrickView::setEntity(size_t voxelIdx, gaia::ecs::Entity entity) {
    if (voxelIdx < VOXELS_PER_BRICK) {
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
// Component Access (Convenience)
// ============================================================================

std::optional<float> EntityBrickView::getDensity(size_t voxelIdx) const {
    auto entity = getEntity(voxelIdx);
    if (!entity.valid()) {
        return std::nullopt;
    }
    return m_world.getDensity(entity);
}

std::optional<float> EntityBrickView::getDensity(int x, int y, int z) const {
    return getDensity(coordToLinearIndex(x, y, z));
}

std::optional<glm::vec3> EntityBrickView::getColor(size_t voxelIdx) const {
    auto entity = getEntity(voxelIdx);
    if (!entity.valid()) {
        return std::nullopt;
    }
    return m_world.getColor(entity);
}

std::optional<glm::vec3> EntityBrickView::getColor(int x, int y, int z) const {
    return getColor(coordToLinearIndex(x, y, z));
}

std::optional<glm::vec3> EntityBrickView::getNormal(size_t voxelIdx) const {
    auto entity = getEntity(voxelIdx);
    if (!entity.valid()) {
        return std::nullopt;
    }
    return m_world.getNormal(entity);
}

std::optional<glm::vec3> EntityBrickView::getNormal(int x, int y, int z) const {
    return getNormal(coordToLinearIndex(x, y, z));
}

std::optional<uint32_t> EntityBrickView::getMaterialID(size_t voxelIdx) const {
    auto entity = getEntity(voxelIdx);
    if (!entity.valid()) {
        return std::nullopt;
    }
    // Material component access (if exists)
    // For now, return nullopt (implement when Material component added)
    return std::nullopt;
}

std::optional<uint32_t> EntityBrickView::getMaterialID(int x, int y, int z) const {
    return getMaterialID(coordToLinearIndex(x, y, z));
}

// ============================================================================
// Span Access
// ============================================================================

std::span<gaia::ecs::Entity> EntityBrickView::entities() {
    return std::span<gaia::ecs::Entity>(m_entities.data(), m_entities.size());
}

std::span<const gaia::ecs::Entity> EntityBrickView::entities() const {
    return std::span<const gaia::ecs::Entity>(m_entities.data(), m_entities.size());
}

// ============================================================================
// Utility
// ============================================================================

size_t EntityBrickView::countSolidVoxels() const {
    size_t count = 0;
    for (const auto& entity : m_entities) {
        if (entity.valid()) {
            auto density = m_world.getDensity(entity);
            if (density.has_value() && density.value() > 0.0f) {
                count++;
            }
        }
    }
    return count;
}

bool EntityBrickView::isEmpty() const {
    return std::all_of(m_entities.begin(), m_entities.end(),
        [](const auto& entity) { return !entity.valid(); });
}

bool EntityBrickView::isFull() const {
    return countSolidVoxels() == VOXELS_PER_BRICK;
}

// ============================================================================
// Coordinate Conversion
// ============================================================================

size_t EntityBrickView::coordToLinearIndex(int x, int y, int z) {
    // Simple linear indexing: z*64 + y*8 + x
    // TODO: Switch to Morton code for better cache locality
    return static_cast<size_t>(z * 64 + y * 8 + x);
}

void EntityBrickView::linearIndexToCoord(size_t idx, int& x, int& y, int& z) {
    // Reverse of coordToLinearIndex
    z = static_cast<int>(idx / 64);
    idx %= 64;
    y = static_cast<int>(idx / 8);
    x = static_cast<int>(idx % 8);
}

} // namespace GaiaVoxel
