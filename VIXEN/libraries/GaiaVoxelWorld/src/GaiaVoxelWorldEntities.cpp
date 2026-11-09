#include "pch.h"
#include "GaiaVoxelWorldInternal.h"
#include "MortonEncoding.h"

#include <unordered_set>

using namespace Vixen::GaiaVoxel::MortonKeyUtils;

namespace Vixen::GaiaVoxel {

GaiaVoxelWorld::EntityID GaiaVoxelWorld::createVoxel(
    const glm::vec3& position,
    float density,
    const glm::vec3& color,
    const glm::vec3& normal) {

    auto& world = m_impl->world;
    gaia::ecs::Entity entity = world.add();

    MortonKey key = fromPosition(position);
    world.add<MortonKey>(entity, key);
    world.add<Density>(entity, Density{density});
    world.add<Color>(entity, Color(color));
    world.add<Normal>(entity, Normal(normal));

    m_impl->mortonIndex[key.code] = entity;
    return entity;
}

GaiaVoxelWorld::EntityID GaiaVoxelWorld::createVoxel(const VoxelCreationRequest& request) {
    auto& world = m_impl->world;
    gaia::ecs::Entity entity = world.add();

    MortonKey key = fromPosition(request.position);
    world.add<MortonKey>(entity, key);

    for (const auto& compReq : request.components) {
        std::visit([&](auto&& component) {
            using T = std::decay_t<decltype(component)>;
            if constexpr (!std::is_same_v<T, MortonKey> && !std::is_same_v<T, std::monostate>) {
                world.add<T>(entity, component);
            }
        }, compReq.component);
    }

    m_impl->mortonIndex[key.code] = entity;
    tryAutoParentToChunk(entity, request.position);
    invalidateBlockCacheAt(request.position);
    return entity;
}

void GaiaVoxelWorld::tryAutoParentToChunk(EntityID voxelEntity, const glm::vec3& position) {
    auto& world = m_impl->world;

    if (!world.has<MortonKey>(voxelEntity)) return;
    MortonKey voxelKey = world.get<MortonKey>(voxelEntity);
    std::optional<EntityID> matchingChunk;
    auto query = world.query().all<ChunkOrigin>();

    query.each([&](gaia::ecs::Entity chunkEntity, const ChunkOrigin& origin) {
        if (matchingChunk.has_value()) return;
        if (!world.has<ChunkMetadata>(chunkEntity)) return;
        const auto& metadata = world.get<ChunkMetadata>(chunkEntity);
        MortonKey chunkRootKey = fromPosition(glm::vec3(origin.x, origin.y, origin.z));
        uint64_t chunkSpan = static_cast<uint64_t>(metadata.chunkDepth) *
                              static_cast<uint64_t>(metadata.chunkDepth) *
                              static_cast<uint64_t>(metadata.chunkDepth);
        if (voxelKey.code >= chunkRootKey.code &&
            voxelKey.code < (chunkRootKey.code + chunkSpan)) {
            matchingChunk = chunkEntity;
        }
    });

    if (matchingChunk.has_value()) {
        world.add(voxelEntity, gaia::ecs::Pair(gaia::ecs::ChildOf, *matchingChunk));
    }
}

void GaiaVoxelWorld::destroyVoxel(EntityID id) {
    if (m_impl->world.valid(id)) {
        if (m_impl->world.has<MortonKey>(id)) {
            MortonKey key = m_impl->world.get<MortonKey>(id);
            m_impl->mortonIndex.erase(key.code);
        }

        std::optional<glm::vec3> pos = getPosition(id);
        m_impl->world.del(id);
        if (pos.has_value()) {
            invalidateBlockCacheAt(pos.value());
        }
    }
}

void GaiaVoxelWorld::clear() {
    std::vector<gaia::ecs::Entity> toDelete;
    auto query = m_impl->world.query().all<MortonKey>();
    query.each([&toDelete](gaia::ecs::Entity entity) {
        toDelete.push_back(entity);
    });

    for (auto entity : toDelete) {
        m_impl->world.del(entity);
    }

    m_impl->mortonIndex.clear();
    invalidateBlockCache();
}

std::vector<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::createVoxelsBatch(
    std::span<const VoxelCreationRequest> requests,
    BatchCreateStrategy strategy) {

    auto& world = m_impl->world;
    if (world.locked()) {
        throw std::logic_error(
            "GaiaVoxelWorld::createVoxelsBatch cannot make structural changes during a query; "
            "use deferVoxelsBatch with the query command buffer");
    }

    std::vector<EntityID> ids(requests.size());
    if (requests.empty()) {
        return ids;
    }

    auto addComponents = [&](EntityID entity, const VoxelCreationRequest& request) {
        for (const auto& compReq : request.components) {
            std::visit([&](auto&& component) {
                using T = std::decay_t<decltype(component)>;
                if constexpr (!std::is_same_v<T, MortonKey> && !std::is_same_v<T, std::monostate>) {
                    world.add<T>(entity, component);
                }
            }, compReq.component);
        }
    };

    auto overwriteComponents = [&](EntityID entity, const VoxelCreationRequest& request) {
        for (const auto& compReq : request.components) {
            std::visit([&](auto&& component) {
                using T = std::decay_t<decltype(component)>;
                if constexpr (!std::is_same_v<T, MortonKey> && !std::is_same_v<T, std::monostate>) {
                    world.set<T>(entity) = component;
                }
            }, compReq.component);
        }
    };

    auto createOne = [&](size_t requestIndex) {
        const auto& request = requests[requestIndex];
        const auto entity = world.add();
        const MortonKey key = fromPosition(request.position);
        world.add<MortonKey>(entity, key);
        addComponents(entity, request);
        ids[requestIndex] = entity;
        m_impl->mortonIndex[key.code] = entity;
        return entity;
    };

    if (strategy == BatchCreateStrategy::Serial) {
        for (size_t i = 0; i < requests.size(); ++i) {
            createOne(i);
        }
    } else {
        static_assert(std::variant_size_v<ComponentVariant> <= 64);
        std::unordered_map<uint64_t, std::vector<size_t>> groups;
        groups.reserve(requests.size());
        for (size_t i = 0; i < requests.size(); ++i) {
            uint64_t signature = 0;
            for (const auto& component : requests[i].components) {
                signature |= uint64_t{1} << component.component.index();
            }
            groups[signature].push_back(i);
        }

        for (const auto& [signature, indices] : groups) {
            (void)signature;
            const EntityID seed = createOne(indices.front());
            size_t next = 1;
            while (next < indices.size()) {
                const size_t remaining = indices.size() - next;
                const uint32_t copyCount = static_cast<uint32_t>(std::min<size_t>(
                    remaining, std::numeric_limits<uint32_t>::max()));
                world.copy_n(seed, copyCount, [&](EntityID entity) {
                    const size_t requestIndex = indices[next++];
                    const auto& request = requests[requestIndex];
                    const MortonKey key = fromPosition(request.position);
                    world.set<MortonKey>(entity) = key;
                    overwriteComponents(entity, request);
                    ids[requestIndex] = entity;
                    m_impl->mortonIndex[key.code] = entity;
                });
            }
        }
    }

    invalidateBlockCache();
    return ids;
}

namespace {

template <typename CommandBuffer>
GaiaVoxelWorld::DeferredVoxelBatch deferVoxelsBatchImpl(
    const GaiaVoxelWorld* owner,
    CommandBuffer& commands,
    std::span<const OwnedVoxelCreationRequest> requests) {
    GaiaVoxelWorld::DeferredVoxelBatch batch;
    batch.owner = owner;
    batch.keys.reserve(requests.size());
    std::unordered_set<uint64_t> uniqueKeys;
    uniqueKeys.reserve(requests.size());
    for (const auto& request : requests) {
        const MortonKey key = fromPosition(request.position);
        if (!uniqueKeys.insert(key.code).second) {
            throw std::invalid_argument("deferred voxel batch contains duplicate Morton cells");
        }
        batch.keys.push_back(key);
    }

    for (size_t i = 0; i < requests.size(); ++i) {
        const auto& request = requests[i];
        const MortonKey key = batch.keys[i];
        const gaia::ecs::Entity entity = commands.add();
        commands.template add<MortonKey>(entity, MortonKey{key});
        for (const auto& compReq : request.components) {
            std::visit([&](auto&& component) {
                using T = std::decay_t<decltype(component)>;
                if constexpr (!std::is_same_v<T, MortonKey> && !std::is_same_v<T, std::monostate>) {
                    commands.template add<T>(entity, T{component});
                }
            }, compReq.component);
        }
    }
    return batch;
}

} // namespace

GaiaVoxelWorld::DeferredVoxelBatch GaiaVoxelWorld::deferVoxelsBatch(
    gaia::ecs::CommandBufferST& commands,
    std::span<const OwnedVoxelCreationRequest> requests) {
    return deferVoxelsBatchImpl(this, commands, requests);
}

GaiaVoxelWorld::DeferredVoxelBatch GaiaVoxelWorld::deferVoxelsBatch(
    gaia::ecs::CommandBufferMT& commands,
    std::span<const OwnedVoxelCreationRequest> requests) {
    return deferVoxelsBatchImpl(this, commands, requests);
}

std::vector<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::finalizeDeferredVoxelsBatch(
    const DeferredVoxelBatch& batch) {
    if (m_impl->world.locked()) {
        throw std::logic_error(
            "GaiaVoxelWorld::finalizeDeferredVoxelsBatch must run after the query lock is released");
    }
    if (batch.keys.empty()) {
        return {};
    }
    if (batch.owner != this) {
        throw std::invalid_argument("deferred voxel batch belongs to another GaiaVoxelWorld");
    }

    m_impl->rebuildMortonIndex();
    std::vector<EntityID> ids;
    ids.reserve(batch.keys.size());
    for (const MortonKey& key : batch.keys) {
        const auto it = m_impl->mortonIndex.find(key.code);
        if (it == m_impl->mortonIndex.end()) {
            throw std::runtime_error("deferred voxel batch was not committed before finalization");
        }
        ids.push_back(it->second);
    }
    invalidateBlockCache();
    return ids;
}

void GaiaVoxelWorld::destroyVoxelsBatch(const std::vector<EntityID>& ids) {
    for (auto id : ids) {
        if (m_impl->world.valid(id)) {
            if (m_impl->world.has<MortonKey>(id)) {
                MortonKey key = m_impl->world.get<MortonKey>(id);
                m_impl->mortonIndex.erase(key.code);
            }
            m_impl->world.del(id);
        }
    }
    invalidateBlockCache();
}

GaiaVoxelWorld::EntityID GaiaVoxelWorld::insertChunk(
    const glm::ivec3& chunkOrigin,
    std::span<const VoxelCreationRequest> voxels) {

    auto& world = m_impl->world;
    gaia::ecs::Entity chunkEntity = world.add();
    world.add<ChunkOrigin>(chunkEntity, ChunkOrigin(chunkOrigin));

    std::vector<EntityID> voxelEntities;
    voxelEntities.reserve(voxels.size());
    for (const auto& voxelReq : voxels) {
        auto voxelEntity = createVoxel(voxelReq);
        world.add(voxelEntity, gaia::ecs::Pair(gaia::ecs::ChildOf, chunkEntity));
        voxelEntities.push_back(voxelEntity);
    }

    uint8_t chunkDepth = static_cast<uint8_t>(std::cbrt(voxels.size()));
    ChunkMetadata metadata;
    metadata.entityOffset = voxelEntities[0].id();
    metadata.chunkDepth = chunkDepth;
    metadata.flags = 0x01;
    world.add<ChunkMetadata>(chunkEntity, metadata);
    return chunkEntity;
}

} // namespace Vixen::GaiaVoxel
