#include "pch.h"
#include "GaiaVoxelWorldInternal.h"
#include "MortonEncoding.h"

using namespace Vixen::GaiaVoxel::MortonKeyUtils;

namespace Vixen::GaiaVoxel {

void GaiaVoxelWorld::Impl::Init() {
    // Command buffers cannot register component types while a Gaia query
    // holds the structural lock. Register the complete variant up front.
    ComponentRegistry::visitAll([&](auto component) {
        using T = std::decay_t<decltype(component)>;
        (void)component;
        (void)world.add<T>();
    });
}

void GaiaVoxelWorld::Impl::rebuildMortonIndex() {
    mortonIndex.clear();
    auto query = world.query().all<MortonKey>();
    query.each([&](gaia::ecs::Entity entity, const MortonKey& key) {
        mortonIndex[key.code] = entity;
    });
}

GaiaVoxelWorld::GaiaVoxelWorld()
    : m_impl(std::make_unique<Impl>()) {
    m_impl->Init();
    std::cout << "[GaiaVoxelWorld] ECS world initialized\n";
}

GaiaVoxelWorld::~GaiaVoxelWorld() {
    std::cout << "[GaiaVoxelWorld] ECS world destroyed\n";
}

std::optional<glm::vec3> GaiaVoxelWorld::getPosition(EntityID id) const {
    // MortonKey is special - needs conversion to world position.
    if (!m_impl->world.valid(id) || !m_impl->world.has<MortonKey>(id)) return std::nullopt;
    return toWorldPos(m_impl->world.get<MortonKey>(id));
}

void GaiaVoxelWorld::setPosition(EntityID id, const glm::vec3& position) {
    // MortonKey is special - needs conversion from world position.
    if (m_impl->world.valid(id) && m_impl->world.has<MortonKey>(id)) {
        m_impl->world.set<MortonKey>(id) = fromPosition(position);
    }
}

bool GaiaVoxelWorld::hasComponent(EntityID id, const char* componentName) const {
    if (!m_impl->world.valid(id)) return false;

    bool found = false;
    ComponentRegistry::visitByName(componentName, [&](auto componentType) {
        using T = std::decay_t<decltype(componentType)>;
        found = m_impl->world.has<T>(id);
    });
    return found;
}

bool GaiaVoxelWorld::exists(EntityID id) const {
    return m_impl->world.valid(id);
}

gaia::ecs::World& GaiaVoxelWorld::getWorld() {
    return m_impl->world;
}

const gaia::ecs::World& GaiaVoxelWorld::getWorld() const {
    return m_impl->world;
}

uint64_t GaiaVoxelWorld::computeSpatialHash(const glm::vec3& position) const {
    return fromPosition(position).code;
}

} // namespace Vixen::GaiaVoxel
