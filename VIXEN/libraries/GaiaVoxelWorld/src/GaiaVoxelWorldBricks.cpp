#include "pch.h"
#include "GaiaVoxelWorldInternal.h"
#include "MortonEncoding.h"

using Vixen::Core::MortonCode64;

namespace Vixen::GaiaVoxel {

void GaiaVoxelWorld::getBrickEntitiesInto(
    const MortonCode64& brickBaseMorton,
    std::span<EntityID> outEntities,
    uint32_t& outCount,
    uint32_t brickSize) const {

    outCount = 0;
    const size_t volume = static_cast<size_t>(brickSize) * brickSize * brickSize;

    for (size_t i = 0; i < volume && i < outEntities.size(); ++i) {
        outEntities[i] = EntityID{};
    }

    for (uint32_t z = 0; z < brickSize; ++z) {
        for (uint32_t y = 0; y < brickSize; ++y) {
            for (uint32_t x = 0; x < brickSize; ++x) {
                MortonCode64 voxelMorton = brickBaseMorton.addLocalOffset(x, y, z);
                auto it = m_impl->mortonIndex.find(voxelMorton.code);
                if (it != m_impl->mortonIndex.end()) {
                    size_t linearIndex = z * brickSize * brickSize + y * brickSize + x;
                    if (linearIndex < outEntities.size()) {
                        outEntities[linearIndex] = it->second;
                        outCount++;
                    }
                }
            }
        }
    }
}

void GaiaVoxelWorld::getBrickEntitiesByWorldPosInto(
    const glm::ivec3& brickWorldMin,
    std::span<EntityID> outEntities,
    uint32_t& outCount,
    uint32_t brickSize) const {
    MortonCode64 brickBaseMorton = MortonCode64::fromWorldPos(brickWorldMin);
    getBrickEntitiesInto(brickBaseMorton, outEntities, outCount, brickSize);
}

uint32_t GaiaVoxelWorld::countBrickEntities(
    const MortonCode64& brickBaseMorton,
    uint32_t brickSize) const {

    uint32_t count = 0;
    for (uint32_t z = 0; z < brickSize; ++z) {
        for (uint32_t y = 0; y < brickSize; ++y) {
            for (uint32_t x = 0; x < brickSize; ++x) {
                MortonCode64 voxelMorton = brickBaseMorton.addLocalOffset(x, y, z);
                if (m_impl->mortonIndex.contains(voxelMorton.code)) {
                    count++;
                }
            }
        }
    }
    return count;
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

GaiaVoxelWorld::BrickEntities GaiaVoxelWorld::getBrickEntities(
    const MortonCode64& brickBaseMorton,
    uint32_t brickSize) const {
    BrickEntities result;
    getBrickEntitiesInto(brickBaseMorton, result.entities, result.count, brickSize);
    return result;
}

GaiaVoxelWorld::BrickEntities GaiaVoxelWorld::getBrickEntitiesByWorldPos(
    const glm::ivec3& brickWorldMin,
    uint32_t brickSize) const {
    MortonCode64 brickBaseMorton = MortonCode64::fromWorldPos(brickWorldMin);
    return getBrickEntities(brickBaseMorton, brickSize);
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

std::vector<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::getVoxelsInChunk(EntityID chunkEntity) const {
    std::vector<EntityID> results;
    if (!m_impl->world.valid(chunkEntity))
        return results;

    auto query = m_impl->world.query().all<MortonKey>();
    query.each([&](gaia::ecs::Entity entity) {
        if (m_impl->world.has(entity, gaia::ecs::Pair(gaia::ecs::ChildOf, chunkEntity))) {
            results.push_back(entity);
        }
    });
    return results;
}

std::optional<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::findChunkByOrigin(
    const glm::ivec3& chunkOrigin) const {

    std::optional<EntityID> result;
    auto query = m_impl->world.query().all<ChunkOrigin>();
    query.each([&](gaia::ecs::Entity entity, const ChunkOrigin& origin) {
        if (result.has_value())
            return;
        glm::ivec3 entityOrigin = origin;
        if (entityOrigin == chunkOrigin) {
            result = entity;
        }
    });
    return result;
}

std::optional<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::findVoxelEntity(const MortonKey& key) const {
    std::optional<EntityID> result;
    auto query = m_impl->world.query().all<MortonKey>();
    query.each([&](EntityID entity, const MortonKey& mk) {
        if (result.has_value())
            return;
        if (mk.code == key.code) {
            result = entity;
        }
    });
    return result;
}

std::optional<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::findVoxelEntity(const glm::vec3& position) const {
    MortonKey key = MortonKeyUtils::fromPosition(position);
    return findVoxelEntity(key);
}

} // namespace Vixen::GaiaVoxel
