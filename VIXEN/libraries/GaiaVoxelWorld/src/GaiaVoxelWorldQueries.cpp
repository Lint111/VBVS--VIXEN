#include "pch.h"
#include "GaiaVoxelWorldInternal.h"
#include "MortonEncoding.h"

using namespace Vixen::GaiaVoxel::MortonKeyUtils;
using Vixen::Core::MortonCode64;

namespace Vixen::GaiaVoxel {

std::vector<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::queryRegion(
    const glm::vec3& min,
    const glm::vec3& max) const {

    std::vector<EntityID> results;
    auto query = m_impl->world.query().all<MortonKey>();
    query.each([&](gaia::ecs::Entity entity) {
        glm::vec3 pos = toWorldPos(m_impl->world.get<MortonKey>(entity));
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
    auto query = m_impl->world.query().all<MortonKey>();
    query.each([&](gaia::ecs::Entity entity) {
        if (m_impl->world.has<Density>(entity)) {
            if (m_impl->world.get<Density>(entity).value > 0.0f) {
                results.push_back(entity);
            }
        } else {
            results.push_back(entity);
        }
    });
    return results;
}

std::vector<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::queryOccupiedVoxels() const {
    std::vector<EntityID> results;
    auto query = m_impl->world.query().all<MortonKey>();
    query.each([&](gaia::ecs::Entity entity) {
        results.push_back(entity);
    });
    return results;
}

size_t GaiaVoxelWorld::countVoxelsInRegion(const glm::vec3& min, const glm::vec3& max) const {
    size_t count = 0;
    auto query = m_impl->world.query().all<MortonKey>();
    query.each([&](gaia::ecs::Entity entity) {
        glm::vec3 pos = toWorldPos(m_impl->world.get<MortonKey>(entity));
        if (pos.x >= min.x && pos.x <= max.x &&
            pos.y >= min.y && pos.y <= max.y &&
            pos.z >= min.z && pos.z <= max.z) {
            count++;
        }
    });
    return count;
}

std::span<const gaia::ecs::Entity> GaiaVoxelWorld::getEntityBlockRef(
    const glm::vec3& brickWorldMin,
    float brickWorldSize,
    uint8_t brickDepth) {

    BlockQueryKey key{brickWorldMin, brickDepth};
    auto it = m_blockCache.find(key);
    if (it != m_blockCache.end()) {
        return std::span<const gaia::ecs::Entity>(it->second);
    }

    std::vector<gaia::ecs::Entity> entities;
    glm::vec3 brickWorldMax = brickWorldMin + glm::vec3(brickWorldSize);
    glm::ivec3 brickGridMin{
        static_cast<int>(std::floor(brickWorldMin.x)),
        static_cast<int>(std::floor(brickWorldMin.y)),
        static_cast<int>(std::floor(brickWorldMin.z))
    };
    glm::ivec3 brickGridMax{
        static_cast<int>(std::floor(brickWorldMax.x)),
        static_cast<int>(std::floor(brickWorldMax.y)),
        static_cast<int>(std::floor(brickWorldMax.z))
    };

    auto query = m_impl->world.query().all<MortonKey>();
    query.each([&](gaia::ecs::Entity entity) {
        glm::ivec3 entityGrid = decode(m_impl->world.get<MortonKey>(entity));
        if (entityGrid.x >= brickGridMin.x && entityGrid.x < brickGridMax.x &&
            entityGrid.y >= brickGridMin.y && entityGrid.y < brickGridMax.y &&
            entityGrid.z >= brickGridMin.z && entityGrid.z < brickGridMax.z) {
            entities.push_back(entity);
        }
    });

    auto [insertIt, inserted] = m_blockCache.emplace(key, std::move(entities));
    (void)inserted;
    return std::span<const gaia::ecs::Entity>(insertIt->second);
}

void GaiaVoxelWorld::invalidateBlockCache() {
    m_blockCache.clear();
}

void GaiaVoxelWorld::invalidateBlockCacheAt(const glm::vec3& position) {
    uint64_t positionMorton = fromPosition(position).code;
    for (auto it = m_blockCache.begin(); it != m_blockCache.end(); ) {
        const auto& key = it->first;
        uint64_t blockMortonMin = fromPosition(key.worldMin).code;
        uint64_t blockMortonSpan = 1ULL << (3 * key.depth);
        uint64_t blockMortonMax = blockMortonMin + blockMortonSpan;
        bool containsPosition = (positionMorton >= blockMortonMin && positionMorton < blockMortonMax);
        if (containsPosition) {
            it = m_blockCache.erase(it);
        } else {
            ++it;
        }
    }
}

GaiaVoxelWorld::EntityID GaiaVoxelWorld::getEntityByWorldSpace(glm::vec3 worldPos) const {
    MortonKey mortonKey = fromPosition(worldPos);
    auto it = m_impl->mortonIndex.find(mortonKey.code);
    if (it != m_impl->mortonIndex.end()) {
        return it->second;
    }
    return EntityID{};
}

GaiaVoxelWorld::EntityID GaiaVoxelWorld::getEntityByMorton(uint64_t mortonCode) const {
    auto it = m_impl->mortonIndex.find(mortonCode);
    return (it != m_impl->mortonIndex.end()) ? it->second : EntityID{};
}

GaiaVoxelWorld::EntityID GaiaVoxelWorld::getEntityByMorton(const MortonCode64& morton) const {
    return getEntityByMorton(morton.code);
}

GaiaVoxelWorld::Stats GaiaVoxelWorld::getStats() const {
    Stats stats{};
    stats.totalEntities = 0;
    stats.solidVoxels = 0;

    auto allQuery = m_impl->world.query().all<MortonKey>();
    allQuery.each([&](gaia::ecs::Entity) {
        stats.totalEntities++;
    });

    auto solidQuery = m_impl->world.query().all<Density>();
    solidQuery.each([&](const Density& density) {
        if (density.value > 0.0f) {
            stats.solidVoxels++;
        }
    });

    stats.memoryUsageBytes = stats.totalEntities * 36;
    return stats;
}

} // namespace Vixen::GaiaVoxel
