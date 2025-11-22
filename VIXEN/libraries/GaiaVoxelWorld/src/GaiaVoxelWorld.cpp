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
	const ::VoxelData::DynamicVoxelScalar& data) {

	gaia::ecs::World& wrd = getWorld();

    gaia::ecs::Entity entity = wrd.add();


    gaia::ecs::EntityBuilder builder = wrd.build(entity);

    builder.add<MortonKey>();           

    for (const auto& attr : data) {
        builder.add<attr.getType()>();
    }
    builder.name("VoxelEntity");

    builder.commit();

    wrd.set<MortonKey>(entity) = MortonKey::fromPosition(position);

    for (const auto& attr : data) {
        wrd.set<attr.getType()>(entity) = attr.get<attr.getType()>();
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
    entity.add<BrickReference>(BrickReference{brickID, localX, localY, localZ});

    return entity;
}

void GaiaVoxelWorld::destroyVoxel(EntityID id) {
    if (id.valid()) {
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
    if (!id.valid() || !id.has<MortonKey>()) return std::nullopt;
    return id.get<MortonKey>().toWorldPos();
}

std::optional<float> GaiaVoxelWorld::getDensity(EntityID id) const {
    if (!id.valid() || !id.has<Density>()) return std::nullopt;
    return id.get<Density>().value;
}

std::optional<glm::vec3> GaiaVoxelWorld::getColor(EntityID id) const {
    if (!id.valid()) return std::nullopt;
    if (!id.has<Color_R>() || !id.has<Color_G>() || !id.has<Color_B>()) return std::nullopt;
    return glm::vec3(
        id.get<Color_R>().value,
        id.get<Color_G>().value,
        id.get<Color_B>().value
    );
}

std::optional<glm::vec3> GaiaVoxelWorld::getNormal(EntityID id) const {
    if (!id.valid()) return std::nullopt;
    if (!id.has<Normal_X>() || !id.has<Normal_Y>() || !id.has<Normal_Z>()) return std::nullopt;
    return glm::vec3(
        id.get<Normal_X>().value,
        id.get<Normal_Y>().value,
        id.get<Normal_Z>().value
    );
}

std::optional<uint32_t> GaiaVoxelWorld::getBrickID(EntityID id) const {
    if (!id.valid() || !id.has<BrickReference>()) return std::nullopt;
    return id.get<BrickReference>().brickID;
}

// Setters
void GaiaVoxelWorld::setPosition(EntityID id, const glm::vec3& position) {
    if (id.valid() && id.has<MortonKey>()) {
        id.set<MortonKey>(MortonKey::fromPosition(position));
    }
}

void GaiaVoxelWorld::setDensity(EntityID id, float density) {
    if (id.valid() && id.has<Density>()) {
        id.set<Density>(Density{density});
    }
}

void GaiaVoxelWorld::setColor(EntityID id, const glm::vec3& color) {
    if (id.valid()) {
        if (id.has<Color_R>()) id.set<Color_R>(Color_R{color.x});
        if (id.has<Color_G>()) id.set<Color_G>(Color_G{color.y});
        if (id.has<Color_B>()) id.set<Color_B>(Color_B{color.z});
    }
}

void GaiaVoxelWorld::setNormal(EntityID id, const glm::vec3& normal) {
    if (id.valid()) {
        if (id.has<Normal_X>()) id.set<Normal_X>(Normal_X{normal.x});
        if (id.has<Normal_Y>()) id.set<Normal_Y>(Normal_Y{normal.y});
        if (id.has<Normal_Z>()) id.set<Normal_Z>(Normal_Z{normal.z});
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

    auto query = m_impl->world.query().all<MortonKey>();
    query.each([&](gaia::ecs::Entity entity) {
        glm::vec3 pos = entity.get<MortonKey>().toWorldPos();
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

    auto query = m_impl->world.query().all<MortonKey>();
    query.each([&](gaia::ecs::Entity entity) {
        glm::vec3 pos = entity.get<MortonKey>().toWorldPos();
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
