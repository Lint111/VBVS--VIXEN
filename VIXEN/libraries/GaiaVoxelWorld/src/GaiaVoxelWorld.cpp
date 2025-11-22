#include "GaiaVoxelWorld.h"
#include <algorithm>
#include <iostream>

namespace GaiaVoxel {

// ============================================================================
// GaiaVoxelWorld::Impl - Pimpl for ECS world encapsulation
// ============================================================================

struct GaiaVoxelWorld::Impl {
    gaia::ecs::World world;

    // Component registration (done once at startup)
    void registerComponents() {
        // Components are auto-registered by Gaia on first use
        // No explicit registration needed
    }
};

// ============================================================================
// GaiaVoxelWorld Implementation
// ============================================================================

GaiaVoxelWorld::GaiaVoxelWorld()
    : m_impl(std::make_unique<Impl>()) {
    m_impl->registerComponents();
    std::cout << "[GaiaVoxelWorld] ECS world initialized\n";
}

GaiaVoxelWorld::~GaiaVoxelWorld() {
    std::cout << "[GaiaVoxelWorld] ECS world destroyed\n";
}

// ============================================================================
// Entity Creation/Deletion
// ============================================================================

GaiaVoxelWorld::EntityID GaiaVoxelWorld::createVoxel(
    const glm::vec3& position,
    float density,
    const glm::vec3& color,
    const glm::vec3& normal) {

    auto entity = m_impl->world.add();

    // Add components using POD initialization
    entity.add<Position>(Position{position.x, position.y, position.z});
    entity.add<Density>(Density{density});
    entity.add<Color>(Color{color.x, color.y, color.z});
    entity.add<Normal>(Normal{normal.x, normal.y, normal.z});
    entity.add<SpatialHash>(SpatialHash{computeSpatialHash(position)});

    return entity;
}

GaiaVoxelWorld::EntityID GaiaVoxelWorld::createVoxelInBrick(
    const glm::vec3& position,
    float density,
    const glm::vec3& color,
    const glm::vec3& normal,
    uint32_t brickID,
    uint8_t localX,
    uint8_t localY,
    uint8_t localZ) {

    auto entity = createVoxel(position, density, color, normal);
    entity.add<BrickReference>(BrickReference{brickID, localX, localY, localZ});

    return entity;
}

void GaiaVoxelWorld::destroyVoxel(EntityID id) {
    if (id.valid()) {
        m_impl->world.del(id);
    }
}

void GaiaVoxelWorld::clear() {
    // Query all entities and delete them
    auto query = m_impl->world.query().all<Position>();
    query.each([this](gaia::ecs::Entity entity) {
        m_impl->world.del(entity);
    });
}

// ============================================================================
// Component Access (Thread-Safe via Gaia's Lock-Free SoA)
// ============================================================================

std::optional<glm::vec3> GaiaVoxelWorld::getPosition(EntityID id) const {
    if (!id.valid() || !id.has<Position>()) return std::nullopt;
    auto pos = id.get<Position>();
    return pos.toVec3();
}

std::optional<float> GaiaVoxelWorld::getDensity(EntityID id) const {
    if (!id.valid() || !id.has<Density>()) return std::nullopt;
    return id.get<Density>().value;
}

std::optional<glm::vec3> GaiaVoxelWorld::getColor(EntityID id) const {
    if (!id.valid() || !id.has<Color>()) return std::nullopt;
    auto col = id.get<Color>();
    return col.toVec3();
}

std::optional<glm::vec3> GaiaVoxelWorld::getNormal(EntityID id) const {
    if (!id.valid() || !id.has<Normal>()) return std::nullopt;
    auto nrm = id.get<Normal>();
    return nrm.toVec3();
}

std::optional<uint32_t> GaiaVoxelWorld::getBrickID(EntityID id) const {
    if (!id.valid() || !id.has<BrickReference>()) return std::nullopt;
    return id.get<BrickReference>().brickID;
}

// Setters
void GaiaVoxelWorld::setPosition(EntityID id, const glm::vec3& position) {
    if (id.valid() && id.has<Position>()) {
        id.set<Position>(Position{position.x, position.y, position.z});
        id.set<SpatialHash>(SpatialHash{computeSpatialHash(position)});
    }
}

void GaiaVoxelWorld::setDensity(EntityID id, float density) {
    if (id.valid() && id.has<Density>()) {
        id.set<Density>(Density{density});
    }
}

void GaiaVoxelWorld::setColor(EntityID id, const glm::vec3& color) {
    if (id.valid() && id.has<Color>()) {
        id.set<Color>(Color{color.x, color.y, color.z});
    }
}

void GaiaVoxelWorld::setNormal(EntityID id, const glm::vec3& normal) {
    if (id.valid() && id.has<Normal>()) {
        id.set<Normal>(Normal{normal.x, normal.y, normal.z});
    }
}

bool GaiaVoxelWorld::exists(EntityID id) const {
    return id.valid();
}

// ============================================================================
// Spatial Queries
// ============================================================================

std::vector<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::queryRegion(
    const glm::vec3& min,
    const glm::vec3& max) const {

    std::vector<EntityID> results;

    auto query = m_impl->world.query().all<Position>();
    query.each([&](gaia::ecs::Entity entity) {
        auto pos = entity.get<Position>().toVec3();
        if (pos.x >= min.x && pos.x <= max.x &&
            pos.y >= min.y && pos.y <= max.y &&
            pos.z >= min.z && pos.z <= max.z) {
            results.push_back(entity);
        }
    });

    return results;
}

std::vector<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::queryBrick(
    const glm::ivec3& brickCoord,
    int brickResolution) const {

    glm::vec3 brickMin = glm::vec3(brickCoord) * float(brickResolution);
    glm::vec3 brickMax = brickMin + glm::vec3(brickResolution);

    return queryRegion(brickMin, brickMax);
}

std::vector<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::querySolidVoxels() const {
    std::vector<EntityID> results;

    auto query = m_impl->world.query().all<Density>();
    query.each([&](gaia::ecs::Entity entity) {
        if (entity.get<Density>().isSolid()) {
            results.push_back(entity);
        }
    });

    return results;
}

size_t GaiaVoxelWorld::countVoxelsInRegion(const glm::vec3& min, const glm::vec3& max) const {
    size_t count = 0;

    auto query = m_impl->world.query().all<Position>();
    query.each([&](gaia::ecs::Entity entity) {
        auto pos = entity.get<Position>().toVec3();
        if (pos.x >= min.x && pos.x <= max.x &&
            pos.y >= min.y && pos.y <= max.y &&
            pos.z >= min.z && pos.z <= max.z) {
            count++;
        }
    });

    return count;
}

// ============================================================================
// Batch Operations
// ============================================================================

std::vector<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::createVoxelsBatch(
    const std::vector<VoxelData>& voxels) {

    std::vector<EntityID> ids;
    ids.reserve(voxels.size());

    for (const auto& voxel : voxels) {
        ids.push_back(createVoxel(
            voxel.position,
            voxel.density,
            voxel.color,
            voxel.normal));
    }

    return ids;
}

void GaiaVoxelWorld::destroyVoxelsBatch(const std::vector<EntityID>& ids) {
    for (auto id : ids) {
        destroyVoxel(id);
    }
}

// ============================================================================
// Statistics
// ============================================================================

GaiaVoxelWorld::Stats GaiaVoxelWorld::getStats() const {
    Stats stats;

    // Count total entities
    auto allQuery = m_impl->world.query().all<Position>();
    allQuery.each([&](gaia::ecs::Entity) {
        stats.totalEntities++;
    });

    // Count solid voxels
    auto solidQuery = m_impl->world.query().all<Density>();
    solidQuery.each([&](gaia::ecs::Entity entity) {
        if (entity.get<Density>().isSolid()) {
            stats.solidVoxels++;
        }
    });

    // Approximate memory usage
    // Each entity: ~64 bytes (Position + Density + Color + Normal + SpatialHash + overhead)
    stats.memoryUsageBytes = stats.totalEntities * 64;

    return stats;
}

// ============================================================================
// Advanced: Direct ECS Access
// ============================================================================

gaia::ecs::World& GaiaVoxelWorld::getWorld() {
    return m_impl->world;
}

const gaia::ecs::World& GaiaVoxelWorld::getWorld() const {
    return m_impl->world;
}

// ============================================================================
// Helper Functions
// ============================================================================

uint64_t GaiaVoxelWorld::computeSpatialHash(const glm::vec3& position) const {
    // Convert float position to integer grid (assuming unit voxels)
    glm::ivec3 gridPos(
        static_cast<int>(std::floor(position.x)),
        static_cast<int>(std::floor(position.y)),
        static_cast<int>(std::floor(position.z))
    );

    return SpatialHash::compute(gridPos);
}

} // namespace GaiaVoxel
