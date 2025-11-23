#include "GaiaVoxelWorld.h"
#include <algorithm>
#include <iostream>

using namespace GaiaVoxel::MortonKeyUtils; // For fromPosition(), toWorldPos(), etc.

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
    MortonKey key = fromPosition(position);
    world.add<MortonKey>(entity, key);
    world.add<Density>(entity, Density{density});
    world.add<Color>(entity, Color(color));
    world.add<Normal>(entity, Normal(normal));

    return entity;
}

// ComponentQueryRequest array overload - type-safe, zero string lookups
GaiaVoxelWorld::EntityID GaiaVoxelWorld::createVoxel(const VoxelCreationRequest& request) {

    auto& world = m_impl->world;
    gaia::ecs::Entity entity = world.add();

    // Always add MortonKey first
    MortonKey key = fromPosition(request.position);
    world.add<MortonKey>(entity, key);

    // Add components using std::visit (compile-time dispatch)
    for (const auto& compReq : request.components) {
        std::visit([&](auto&& component) {
            using T = std::decay_t<decltype(component)>;

            // Skip MortonKey (already added) and monostate (empty variant)
            if constexpr (!std::is_same_v<T, MortonKey> && !std::is_same_v<T, std::monostate>) {
                world.add<T>(entity, component);
            }
        }, compReq.component);
    }

    // Auto-parent to existing chunk if position falls within chunk bounds
    tryAutoParentToChunk(entity, request.position);

    // Invalidate only cached blocks containing this position (efficient partial invalidation)
    invalidateBlockCacheAt(request.position);

    return entity;
}

// Helper: Find chunk that contains given position and add ChildOf relation
void GaiaVoxelWorld::tryAutoParentToChunk(EntityID voxelEntity, const glm::vec3& position) {
    auto& world = m_impl->world;

    // Get voxel's Morton key
    if (!world.has<MortonKey>(voxelEntity)) return;
    MortonKey voxelKey = world.get<MortonKey>(voxelEntity);

    // Find matching chunk (can't modify world during query, so collect first)
    std::optional<EntityID> matchingChunk;

    // Query all chunks (entities with ChunkOrigin component)
    auto query = world.query().all<ChunkOrigin>();

    query.each([&](gaia::ecs::Entity chunkEntity, const ChunkOrigin& origin) {
        if (matchingChunk.has_value()) return; // Already found a match

        // Get chunk metadata to determine span
        if (!world.has<ChunkMetadata>(chunkEntity)) return;
        const auto& metadata = world.get<ChunkMetadata>(chunkEntity);

        // Calculate chunk Morton key span
        // Chunk root key = Morton key of chunk origin
        MortonKey chunkRootKey = fromPosition(glm::vec3(origin.x, origin.y, origin.z));

        // Chunk span = depth³ voxels (e.g., 8³ = 512)
        // Morton keys are ordered spatially, so we can use range check
        // For a chunk of size D, the span is approximately D³ Morton keys
        uint64_t chunkSpan = static_cast<uint64_t>(metadata.chunkDepth) *
                              static_cast<uint64_t>(metadata.chunkDepth) *
                              static_cast<uint64_t>(metadata.chunkDepth);

        // Check if voxel's Morton key is within chunk range
        // NOTE: Morton keys aren't perfectly contiguous for cubes, but this is a good approximation
        // for spatial locality (voxels near each other have similar Morton keys)
        if (voxelKey.code >= chunkRootKey.code &&
            voxelKey.code < (chunkRootKey.code + chunkSpan)) {
            matchingChunk = chunkEntity;
        }
    });

    // Add ChildOf relation outside of query (world is no longer locked)
    if (matchingChunk.has_value()) {
        world.add(voxelEntity, gaia::ecs::Pair(gaia::ecs::ChildOf, *matchingChunk));
    }
}

// createVoxelInBrick() REMOVED - See header comment
// Brick storage is now a VIEW pattern, not entity-stored data

void GaiaVoxelWorld::destroyVoxel(EntityID id) {
    if (m_impl->world.valid(id)) {
        // Get position before deletion for cache invalidation
        std::optional<glm::vec3> pos = getPosition(id);

        m_impl->world.del(id);

        // Invalidate only cached blocks containing this position (efficient partial invalidation)
        if (pos.has_value()) {
            invalidateBlockCacheAt(pos.value());
        }
    }
}

void GaiaVoxelWorld::clear() {
    // Query all entities with MortonKey and delete them
    auto query = m_impl->world.query().all<MortonKey>();
    query.each([this](gaia::ecs::Entity entity) {
        m_impl->world.del(entity);
    });
    // Invalidate block cache (structural change)
    invalidateBlockCache();
}

// ============================================================================
// Component Access (Thread-Safe via Gaia's Lock-Free SoA)
// ============================================================================

// ============================================================================
// Special Accessors (MortonKey position conversion)
// ============================================================================

std::optional<glm::vec3> GaiaVoxelWorld::getPosition(EntityID id) const {
    // MortonKey is special - needs conversion to world position
    if (!m_impl->world.valid(id) || !m_impl->world.has<MortonKey>(id)) return std::nullopt;
    return toWorldPos(m_impl->world.get<MortonKey>(id));
}

void GaiaVoxelWorld::setPosition(EntityID id, const glm::vec3& position) {
    // MortonKey is special - needs conversion from world position
    if (m_impl->world.valid(id) && m_impl->world.has<MortonKey>(id)) {
        m_impl->world.set<MortonKey>(id) = fromPosition(position);
    }
}

bool GaiaVoxelWorld::hasComponent(EntityID id, const char* componentName) const {
    if (!m_impl->world.valid(id)) return false;

    // Use ComponentRegistry to check component by name
    bool found = false;
    GaiaVoxel::ComponentRegistry::visitByName(componentName, [&](auto component_type) {
        using T = std::decay_t<decltype(component_type)>;
        found = m_impl->world.has<T>(id);
    });

    return found;
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
    float brickSize) {

    BlockQueryKey key{brickWorldMin, brickSize};

    // Check cache first
    auto it = m_blockCache.find(key);
    if (it != m_blockCache.end()) {
        // Cache hit - return span to cached vector
        return std::span<const gaia::ecs::Entity>(it->second);
    }

    // Cache miss - perform query and populate cache
    std::vector<gaia::ecs::Entity> entities;
    glm::vec3 brickWorldMax = brickWorldMin + glm::vec3(brickSize);

    auto query = m_impl->world.query().all<MortonKey>();
    query.each([&](gaia::ecs::Entity entity) {
        glm::vec3 pos = toWorldPos(m_impl->world.get<MortonKey>(entity));
        if (pos.x >= brickWorldMin.x && pos.x < brickWorldMax.x &&
            pos.y >= brickWorldMin.y && pos.y < brickWorldMax.y &&
            pos.z >= brickWorldMin.z && pos.z < brickWorldMax.z) {
            entities.push_back(entity);
        }
    });

    // Insert into cache and return span
    auto [insertIt, inserted] = m_blockCache.emplace(key, std::move(entities));
    return std::span<const gaia::ecs::Entity>(insertIt->second);
}

void GaiaVoxelWorld::invalidateBlockCache() {
    m_blockCache.clear();
}

void GaiaVoxelWorld::invalidateBlockCacheAt(const glm::vec3& position) {
    // Remove all cached blocks that contain this position
    // Iterate through cache and remove blocks where position falls within bounds
    for (auto it = m_blockCache.begin(); it != m_blockCache.end(); ) {
        const auto& key = it->first;
        glm::vec3 blockMax = key.worldMin + glm::vec3(key.size);

        bool containsPosition =
            position.x >= key.worldMin.x && position.x < blockMax.x &&
            position.y >= key.worldMin.y && position.y < blockMax.y &&
            position.z >= key.worldMin.z && position.z < blockMax.z;

        if (containsPosition) {
            it = m_blockCache.erase(it);  // Invalidate this block
        } else {
            ++it;
        }
    }
}

GaiaVoxelWorld::EntityID GaiaVoxelWorld::getEntityByMortonKey(uint64_t mortonKey) const {
    // Query all entities with MortonKey component
    auto query = m_impl->world.query().all<MortonKey>();

    EntityID result;
    query.each([&](gaia::ecs::Entity entity) {
        const auto& key = m_impl->world.get<MortonKey>(entity);
        if (key.code == mortonKey) {
            result = entity;
            // Note: Could optimize with spatial index/hash map in future
        }
    });

    return result;  // Returns invalid entity if not found
}

// ============================================================================
// Batch Operations
// ============================================================================

std::vector<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::createVoxelsBatch(
    std::span<const VoxelCreationRequest> requests) {

    std::vector<EntityID> ids;
    ids.reserve(requests.size());

    // Type-safe batch creation - zero string lookups
    for (const auto& req : requests) {
        ids.push_back(createVoxel(req));
    }

    return ids;
}

void GaiaVoxelWorld::destroyVoxelsBatch(const std::vector<EntityID>& ids) {
    for (auto id : ids) {
        if (m_impl->world.valid(id)) {
            m_impl->world.del(id);
        }
    }
    // Invalidate block cache once after batch (not per-entity)
    invalidateBlockCache();
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
// Chunk Operations (Bulk Insert for Spatial Locality)
// ============================================================================

GaiaVoxelWorld::EntityID GaiaVoxelWorld::insertChunk(
    const glm::ivec3& chunkOrigin,
    std::span<const VoxelCreationRequest> voxels) {

    auto& world = m_impl->world;

    // 1. Create chunk metadata entity FIRST (for ChildOf relation)
    gaia::ecs::Entity chunkEntity = world.add();
    world.add<ChunkOrigin>(chunkEntity, ChunkOrigin(chunkOrigin));

    // 2. Create voxel entities and link them to chunk via ChildOf relation
    std::vector<EntityID> voxelEntities;
    voxelEntities.reserve(voxels.size());

    for (const auto& voxelReq : voxels) {
        auto voxelEntity = createVoxel(voxelReq);
        // Add ChildOf relation to link voxel to chunk
        world.add(voxelEntity, gaia::ecs::Pair(gaia::ecs::ChildOf, chunkEntity));
        voxelEntities.push_back(voxelEntity);
    }

    // 3. Add chunk metadata (voxel span reference)
    uint8_t chunkDepth = static_cast<uint8_t>(std::cbrt(voxels.size())); // e.g., 8 for 512 voxels
    ChunkMetadata metadata;
    metadata.entityOffset = voxelEntities[0].id();
    metadata.chunkDepth = chunkDepth;
    metadata.flags = 0x01; // isDirty = true
    world.add<ChunkMetadata>(chunkEntity, metadata);

    return chunkEntity;
}

std::vector<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::getVoxelsInChunk(EntityID chunkEntity) const {
    std::vector<EntityID> results;

    if (!m_impl->world.valid(chunkEntity))
        return results;

    // Query all entities with ChildOf(chunkEntity) relation
    // NOTE: Gaia queries are built at compile-time, so we iterate all voxels and filter
    auto query = m_impl->world.query().all<MortonKey>();

    query.each([&](gaia::ecs::Entity entity) {
        // Check if this voxel has ChildOf relation to chunkEntity
        if (m_impl->world.has(entity, gaia::ecs::Pair(gaia::ecs::ChildOf, chunkEntity))) {
            results.push_back(entity);
        }
    });

    return results;
}

std::optional<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::findChunkByOrigin(
    const glm::ivec3& chunkOrigin) const {

    std::optional<EntityID> result;

    // Query all entities with ChunkOrigin component
    auto query = m_impl->world.query().all<ChunkOrigin>();

    query.each([&](gaia::ecs::Entity entity, const ChunkOrigin& origin) {
        if (result.has_value())
            return; // Already found, skip rest

        glm::ivec3 entityOrigin = origin; // Automatic conversion via operator glm::ivec3()
        if (entityOrigin == chunkOrigin) {
            result = entity;
        }
    });

    return result;
}

// ============================================================================
// Fast Entity Lookup (Direct Gaia Query - Zero Memory Overhead!)
// ============================================================================

std::optional<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::findVoxelEntity(const MortonKey& key) const {
    std::optional<EntityID> result;

    // Direct query - Gaia's chunk-based iteration is already fast
    // No hash map overhead (saves 24+ bytes per voxel!)
    auto query = m_impl->world.query().all<MortonKey>();

    query.each([&](EntityID entity, const MortonKey& mk) {
        if (result.has_value())
            return; // Already found, skip rest

        if (mk.code == key.code) {
            result = entity;
        }
    });

    return result;
}

std::optional<GaiaVoxelWorld::EntityID> GaiaVoxelWorld::findVoxelEntity(const glm::vec3& position) const {
    MortonKey key = fromPosition(position);
    return findVoxelEntity(key);
}

// ============================================================================
// Helper Functions
// ============================================================================

uint64_t GaiaVoxelWorld::computeSpatialHash(const glm::vec3& position) const {
    // Use Morton code as spatial hash
    return fromPosition(position).code;
}

} // namespace GaiaVoxel
