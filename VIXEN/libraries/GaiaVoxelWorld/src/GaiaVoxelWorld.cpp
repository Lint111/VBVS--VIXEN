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

    auto& world = m_impl->world;
    gaia::ecs::Entity entity = world.add();

    // Add all components
    world.add<MortonKey>(entity, MortonKey::fromPosition(position));
    world.add<Density>(entity, Density{density});
    world.add<Color>(entity, Color(color));
    world.add<Normal>(entity, Normal(normal));

    return entity;
}

// DynamicVoxelScalar overload - converts attribute names to components
GaiaVoxelWorld::EntityID GaiaVoxelWorld::createVoxel(
    const glm::vec3& position,
    const ::VoxelData::DynamicVoxelScalar& data) {

    auto& world = m_impl->world;
    gaia::ecs::Entity entity = world.add();

    // Always add MortonKey
    world.add<MortonKey>(entity, MortonKey::fromPosition(position));

    // Convert DynamicVoxelScalar attributes to ECS components
    for (const auto& attr : data) {
        if (attr.name == "density") {
            world.add<Density>(entity, Density{attr.get<float>()});
        }
        else if (attr.name == "color") {
            world.add<Color>(entity, Color(attr.get<glm::vec3>()));
        }
        else if (attr.name == "normal") {
            world.add<Normal>(entity, Normal(attr.get<glm::vec3>()));
        }
        else if (attr.name == "material") {
            world.add<Material>(entity, Material{attr.get<uint32_t>()});
        }
        else if (attr.name == "emission") {
            world.add<Emission>(entity, Emission(attr.get<glm::vec3>()));
        }
        // Add other known components as needed
    }

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
    m_impl->world.add<BrickReference>(entity, BrickReference{brickID, localX, localY, localZ});

    return entity;
}

void GaiaVoxelWorld::destroyVoxel(EntityID id) {
    if (m_impl->world.valid(id)) {
        m_impl->world.del(id);
    }
}

void GaiaVoxelWorld::clear() {
    // Query all entities with MortonKey and delete them
    auto query = m_impl->world.query().all<MortonKey>();
    query.each([this](gaia::ecs::Entity entity) {
        m_impl->world.del(entity);
    });
}

// ============================================================================
// Component Access (Thread-Safe via Gaia's Lock-Free SoA)
// ============================================================================

std::optional<glm::vec3> GaiaVoxelWorld::getPosition(EntityID id) const {
    if (!m_impl->world.valid(id) || !m_impl->world.has<MortonKey>(id)) return std::nullopt;
    return m_impl->world.get<MortonKey>(id).toWorldPos();
}

std::optional<float> GaiaVoxelWorld::getDensity(EntityID id) const {
    if (!m_impl->world.valid(id) || !m_impl->world.has<Density>(id)) return std::nullopt;
    return m_impl->world.get<Density>(id).value;
}

std::optional<glm::vec3> GaiaVoxelWorld::getColor(EntityID id) const {
    if (!m_impl->world.valid(id) || !m_impl->world.has<Color>(id)) return std::nullopt;
    return glm::vec3(m_impl->world.get<Color>(id));  // Automatic conversion
}

std::optional<glm::vec3> GaiaVoxelWorld::getNormal(EntityID id) const {
    if (!m_impl->world.valid(id) || !m_impl->world.has<Normal>(id)) return std::nullopt;
    return glm::vec3(m_impl->world.get<Normal>(id));  // Automatic conversion
}

std::optional<uint32_t> GaiaVoxelWorld::getBrickID(EntityID id) const {
    if (!m_impl->world.valid(id) || !m_impl->world.has<BrickReference>(id)) return std::nullopt;
    return m_impl->world.get<BrickReference>(id).brickID;
}

// Setters
void GaiaVoxelWorld::setPosition(EntityID id, const glm::vec3& position) {
    if (m_impl->world.valid(id) && m_impl->world.has<MortonKey>(id)) {
        m_impl->world.set<MortonKey>(id) = MortonKey::fromPosition(position);
    }
}

void GaiaVoxelWorld::setDensity(EntityID id, float density) {
    if (m_impl->world.valid(id) && m_impl->world.has<Density>(id)) {
        m_impl->world.set<Density>(id) = Density{density};
    }
}

void GaiaVoxelWorld::setColor(EntityID id, const glm::vec3& color) {
    if (m_impl->world.valid(id) && m_impl->world.has<Color>(id)) {
        m_impl->world.set<Color>(id) = Color(color);
    }
}

void GaiaVoxelWorld::setNormal(EntityID id, const glm::vec3& normal) {
    if (m_impl->world.valid(id) && m_impl->world.has<Normal>(id)) {
        m_impl->world.set<Normal>(id) = Normal(normal);
    }
}

bool GaiaVoxelWorld::exists(EntityID id) const {
    return  m_impl->world.valid(id);
}

// ============================================================================
// Spatial Queries
// ============================================================================

std::vector<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::queryRegion(
    const glm::vec3& min,
    const glm::vec3& max) const {

    std::vector<EntityID> results;

    auto query = m_impl->world.query().all<MortonKey>();
    query.each([&](gaia::ecs::Entity entity) {
        glm::vec3 pos = m_impl->world.get<MortonKey>(entity).toWorldPos();
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
        if (m_impl->world.get<Density>(entity).value > 0.0f) {
            results.push_back(entity);
        }
    });

    return results;
}

size_t GaiaVoxelWorld::countVoxelsInRegion(const glm::vec3& min, const glm::vec3& max) const {
    size_t count = 0;

    auto query = m_impl->world.query().all<MortonKey>();
    query.each([&](gaia::ecs::Entity entity) {
        glm::vec3 pos = m_impl->world.get<MortonKey>(entity).toWorldPos();
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
    const std::vector<::VoxelData::DynamicVoxelScalar>& voxels) {

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

std::vector<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::createVoxelsBatch(
    const std::vector<VoxelCreationEntry>& entries) {

    std::vector<EntityID> ids;
    ids.reserve(entries.size());

    for (const auto& entry : entries) {
        ids.push_back(createVoxel(
            entry.position,
            entry.request.density,
            entry.request.color,
            entry.request.normal));
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
    Stats stats{};
    stats.totalEntities = 0;
    stats.solidVoxels = 0;

    // Count total entities with MortonKey
    auto allQuery = m_impl->world.query().all<MortonKey>();
    allQuery.each([&](gaia::ecs::Entity) {
        stats.totalEntities++;
    });

    // Count solid voxels (entities with Density > 0)
    auto solidQuery = m_impl->world.query().all<Density>();
    solidQuery.each([&](const Density& density) {
        if (density.value > 0.0f) {
            stats.solidVoxels++;
        }
    });

    // Approximate memory usage
    // Each entity: ~36 bytes (MortonKey + Density + Color_RGB + Normal_XYZ + overhead)
    stats.memoryUsageBytes = stats.totalEntities * 36;

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
    // Use Morton code as spatial hash
    return MortonKey::fromPosition(position).code;
}

} // namespace GaiaVoxel
