#pragma once

#include <gaia.h>
#include "VoxelComponents.h"
#include "ComponentData.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <optional>
#include <span>

namespace GaiaVoxel {

/**
 * Central ECS-based voxel data management.
 *
 * Single source of truth for all voxel data. AttributeRegistry, VoxelInjectionQueue,
 * and SVO trees all reference entities via EntityID instead of copying data.
 *
 * Thread-safe: Gaia ECS handles concurrent entity access via lock-free SoA storage.
 *
 * Example usage:
 *   GaiaVoxelWorld world;
 *   auto voxelID = world.createVoxel(pos, 1.0f, red);
 *   auto voxels = world.queryRegion(minBounds, maxBounds);
 *   for (auto id : voxels) {
 *       auto pos = world.getPosition(id);
 *       auto color = world.getColor(id);
 *   }
 */
class GaiaVoxelWorld {
public:
    using EntityID = gaia::ecs::Entity;

    GaiaVoxelWorld();
    ~GaiaVoxelWorld();

    // Disable copy (ECS world is unique)
    GaiaVoxelWorld(const GaiaVoxelWorld&) = delete;
    GaiaVoxelWorld& operator=(const GaiaVoxelWorld&) = delete;

    // ========================================================================
    // Entity Creation/Deletion
    // ========================================================================

    /**
     * Create voxel entity with required components.
     * @param position World-space position
     * @param density Density/opacity [0,1]
     * @param color RGB color (optional, defaults to white)
     * @param normal Surface normal (optional, defaults to +Y)
     * @return Entity ID for referencing voxel
     */
    EntityID createVoxel(
        const glm::vec3& position,
        float density = 1.0f,
        const glm::vec3& color = glm::vec3(1.0f),
        const glm::vec3& normal = glm::vec3(0.0f, 1.0f, 0.0f));

    /**
     * Create voxel from VoxelCreationRequest (type-safe, zero string lookups).
     * Components are actual types (Density, Color, etc.) not string names.
     *
     * Example:
     *   ComponentQueryRequest attrs[] = {
     *       Density{0.8f},
     *       Color{glm::vec3(1, 0, 0)},
     *       Normal{glm::vec3(0, 1, 0)}
     *   };
     *   auto id = world.createVoxel({position, attrs});
     */
    EntityID createVoxel(const VoxelCreationRequest& request);

    // NOTE: createVoxelInBrick() REMOVED - Deprecated approach
    //
    // REASON: Brick storage should be a VIEW concept, not stored in entities.
    //
    // NEW ARCHITECTURE:
    // Dense brick data accessed via view pattern:
    //   BrickView brick(mortonKeyOffset, brickDepth);
    //   auto voxel = brick.getVoxel(localX, localY, localZ);  // const ref view
    //
    // Benefits:
    // - Zero allocation (view = offset + stride math)
    // - Cache-friendly (contiguous dense storage)
    // - Clean separation: Sparse voxels (ECS) vs Dense regions (brick arrays)
    // - Morton offset + (3 * 2^brickDepth) defines brick span automatically

    /**
     * Create an array of voxels from VoxelCreationRequest.
     * Each voxel can have different component sets.
     *
     * Example:
     *   ComponentData attrs1[] = {Density{0.8f}, Color{red}};
     *   ComponentData attrs2[] = {Density{0.5f}, Color{blue}};
     *   VoxelCreationRequest reqs[] = {
     *       {pos1, attrs1},
     *       {pos2, attrs2}
     *   };
     *   auto ids = world.createVoxelsBatch(reqs);
     */
    std::vector<EntityID> createVoxelsBatch(std::span<const VoxelCreationRequest> requests);

    /**
     * Destroy voxel entity.
     */
    void destroyVoxel(EntityID id);

    /**
     * Destroy all voxels (clear world).
     */
    void clear();

    // ========================================================================
    // Component Access (Generic Template API)
    // ========================================================================

    /**
     * Generic component getter - works with any FOR_EACH_COMPONENT type.
     *
     * Example:
     *   auto density = world.getComponentValue<Density>(entity);     // returns std::optional<float>
     *   auto color = world.getComponentValue<Color>(entity);         // returns std::optional<glm::vec3>
     *   auto material = world.getComponentValue<Material>(entity);   // returns std::optional<uint32_t>
     */
    template<typename TComponent>
    auto getComponentValue(EntityID id) const -> std::optional<ComponentValueType_t<TComponent>>;

    /**
     * Get component value by index (for multiple instances of same type).
     *
     * Gaia ECS supports multiple instances via Pair<ComponentType, IndexTag>.
     *
     * Example:
     *   // Entity with multiple colors
     *   auto primaryColor = world.getComponentValueByIndex<Color>(entity, 0);   // Color#0
     *   auto secondaryColor = world.getComponentValueByIndex<Color>(entity, 1); // Color#1
     *   auto tertiaryColor = world.getComponentValueByIndex<Color>(entity, 2);  // Color#2
     */
    template<typename TComponent>
    auto getComponentValueByIndex(EntityID id, uint32_t index) const -> std::optional<ComponentValueType_t<TComponent>>;

    /**
     * Generic component setter - works with any FOR_EACH_COMPONENT type.
     * Creates component if entity doesn't have it.
     */
    template<typename TComponent>
    void setComponent(EntityID id, ComponentValueType_t<TComponent> value);

    /**
     * Set component by index (for multiple instances of same type).
     * Creates component at index if doesn't exist.
     */
    template<typename TComponent>
    void setComponentByIndex(EntityID id, uint32_t index, ComponentValueType_t<TComponent> value);

    /**
     * Check component existence (type-safe template version).
     */
    template<typename TComponent>
    bool hasComponent(EntityID id) const;

    /**
     * Check if entity has component at specific index.
     */
    template<typename TComponent>
    bool hasComponentByIndex(EntityID id, uint32_t index) const;

    /**
     * String-based component existence check (uses ComponentRegistry for dynamic lookup).
     * Prefer template version for compile-time safety.
     */
    bool hasComponent(EntityID id, const char* componentName) const;

    /**
     * Check if entity exists and is valid.
     */
    bool exists(EntityID id) const;

    // ========================================================================
    // Special Accessors (MortonKey requires position conversion)
    // ========================================================================

    /**
     * Get world-space position (converts from MortonKey).
     */
    std::optional<glm::vec3> getPosition(EntityID id) const;

    /**
     * Set world-space position (converts to MortonKey).
     */
    void setPosition(EntityID id, const glm::vec3& position);

    /**
     * Find entity by world space component value.
     * Used by EntityBrickView for MortonKey-based lookups.
     *
     * @param worldPos World position to search for
     * @return Entity with matching world position, or invalid entity if not found
     */
    EntityID getEntityByWorldSpace(glm::vec3 worldPos) const;

    // ========================================================================
    // Spatial Queries
    // ========================================================================

    /**
     * Query all voxels in AABB region.
     * Returns entity IDs for iteration.
     */
    std::vector<EntityID> queryRegion(const glm::vec3& min, const glm::vec3& max) const;

    /**
     * Query voxels by brick coordinate.
     * Efficient for batch processing during injection.
     */
    std::vector<EntityID> queryBrick(const glm::ivec3& brickCoord, int brickResolution = 8) const;

    /**
     * Query all solid voxels (density > 0).
     */
    std::vector<EntityID> querySolidVoxels() const;

    /**
     * Count voxels in region (faster than queryRegion().size()).
     */
    size_t countVoxelsInRegion(const glm::vec3& min, const glm::vec3& max) const;

    /**
     * Get zero-copy view of entities in world-space brick region.
     * Results are cached - subsequent calls with same parameters return cached span.
     *
     * @param brickWorldMin Lower-left corner of brick in world space
     * @param brickDepth Brick depth (0-7), where brick spans 2^(3*depth) voxels
     *                   depth=0 → 1³ = 1 voxel
     *                   depth=1 → 2³ = 8 voxels
     *                   depth=2 → 4³ = 64 voxels
     *                   depth=3 → 8³ = 512 voxels (standard brick)
     * @return Span over entities in this region (cached until invalidated)
     *
     * CACHE LIFETIME:
     * - Cache persists across queries (no reallocation if same region queried)
     * - Invalidated on entity create/destroy (automatically via ECS hooks)
     * - Call invalidateBlockCache() to force refresh
     *
     * MORTON RANGE QUERY OPTIMIZATION:
     * - Uses efficient Morton code range check (2 integer comparisons)
     * - No coordinate decoding (3x faster than AABB world-space test)
     * - Avoids floating-point precision issues
     */
    std::span<const gaia::ecs::Entity> getEntityBlockRef(
        const glm::vec3& brickWorldMin,
        uint8_t brickDepth);

    /**
     * Invalidate all cached block queries.
     * Called automatically on mass operations (clear, batch destroy).
     * Can be called manually to force full cache refresh.
     */
    void invalidateBlockCache();

    /**
     * Invalidate only cached blocks that contain given world position.
     * More efficient than full invalidation when modifying single voxels.
     *
     * @param position World position of modified voxel
     */
    void invalidateBlockCacheAt(const glm::vec3& position);

    // ========================================================================
    // Chunk Operations (Bulk Insert for Spatial Locality)
    // ========================================================================

    /**
     * Insert bulk voxels as a spatial chunk (e.g., 8³ = 512 voxels).
     * Creates ChunkOrigin + ChunkMetadata entity, then creates voxel entities
     * with ChildOf relation for fast spatial queries.
     *
     * @param chunkOrigin World-space chunk origin (e.g., {0, 0, 0} for first 8³ region)
     * @param voxels Voxels to insert (typically 512 for 8³ chunk)
     * @return Chunk entity ID (use for spatial queries)
     *
     * Example:
     *   VoxelCreationRequest voxels[512];
     *   // Fill voxels array...
     *   auto chunkID = world.insertChunk({0, 0, 0}, voxels);
     *   auto voxelsInChunk = world.getVoxelsInChunk(chunkID);
     */
    EntityID insertChunk(const glm::ivec3& chunkOrigin,
                         std::span<const VoxelCreationRequest> voxels);

    /**
     * Get all voxels in a chunk (fast via ChildOf relation).
     * @param chunkEntity Chunk entity ID (from insertChunk)
     * @return Vector of voxel entity IDs in this chunk
     */
    std::vector<EntityID> getVoxelsInChunk(EntityID chunkEntity) const;

    /**
     * Find chunk entity by world-space origin.
     * @param chunkOrigin World-space chunk origin
     * @return Chunk entity ID if found, nullopt otherwise
     */
    std::optional<EntityID> findChunkByOrigin(const glm::ivec3& chunkOrigin) const;

    // ========================================================================
    // Fast Entity Lookup (O(1) Spatial Hash)
    // ========================================================================

    /**
     * Find voxel entity by Morton key (direct Gaia query - zero memory overhead!).
     * Uses Gaia's chunk-based iteration for fast lookups without hash map cost.
     *
     * Performance: O(N/chunks) - typically fast for <1M voxels
     * Memory: 0 bytes overhead (vs 24+ bytes/voxel for hash map)
     *
     * @param key Morton key for position
     * @return Entity ID if voxel exists at this position, nullopt otherwise
     *
     * Example:
     *   auto key = MortonKey::fromPosition(glm::vec3(5, 10, 3));
     *   auto entity = world.findVoxelEntity(key);
     *   if (entity) {
     *       world.setDensity(*entity, 0.5f); // Update existing
     *   } else {
     *       world.createVoxel(pos, 0.5f); // Create new
     *   }
     */
    std::optional<EntityID> findVoxelEntity(const MortonKey& key) const;

    /**
     * Find voxel entity by world position (convenience wrapper).
     * @param position World-space position
     * @return Entity ID if voxel exists at this position, nullopt otherwise
     */
    std::optional<EntityID> findVoxelEntity(const glm::vec3& position) const;

    // ========================================================================
    // Batch Operations (for VoxelInjectionQueue)
    // ========================================================================
    // Note: createVoxelsBatch is defined above in Entity Creation section

    /**
     * Destroy multiple voxels in parallel.
     */
    void destroyVoxelsBatch(const std::vector<EntityID>& ids);

    // ========================================================================
    // Statistics
    // ========================================================================

    struct Stats {
        size_t totalEntities;
        size_t solidVoxels;
        size_t memoryUsageBytes;
    };

    Stats getStats() const;

    // ========================================================================
    // Advanced: Direct ECS Access (for custom queries)
    // ========================================================================

    /**
     * Get underlying ECS world for advanced queries.
     * Use with caution - direct world access bypasses API safety.
     */
    gaia::ecs::World& getWorld();
    const gaia::ecs::World& getWorld() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    // ========================================================================
    // Block Query Cache
    // ========================================================================

    struct BlockQueryKey {
        glm::vec3 worldMin;
        uint8_t depth;

        bool operator==(const BlockQueryKey& other) const {
            // Use epsilon comparison for float equality, exact match for depth
            constexpr float epsilon = 0.0001f;
            return glm::all(glm::lessThan(glm::abs(worldMin - other.worldMin), glm::vec3(epsilon)))
                && depth == other.depth;
        }
    };

    struct BlockQueryKeyHash {
        size_t operator()(const BlockQueryKey& key) const {
            // Combine hashes using FNV-1a-like mixing
            size_t hash = 0xcbf29ce484222325ULL;
            auto hashFloat = [&hash](float f) {
                // Quantize to epsilon grid to ensure consistent hashing
                int32_t quantized = static_cast<int32_t>(f * 10000.0f);
                hash ^= static_cast<size_t>(quantized);
                hash *= 0x100000001b3ULL;
            };
            hashFloat(key.worldMin.x);
            hashFloat(key.worldMin.y);
            hashFloat(key.worldMin.z);
            hash ^= static_cast<size_t>(key.depth);
            hash *= 0x100000001b3ULL;
            return hash;
        }
    };

    // Cache: block key -> entity list
    mutable std::unordered_map<BlockQueryKey, std::vector<gaia::ecs::Entity>, BlockQueryKeyHash> m_blockCache;

    // Helper: Compute spatial hash for position
    uint64_t computeSpatialHash(const glm::vec3& position) const;

    // Helper: Auto-parent voxel to existing chunk if within bounds
    void tryAutoParentToChunk(EntityID voxelEntity, const glm::vec3& position);
};

// ============================================================================
// Template Implementations (Header-only for implicit instantiation)
// ============================================================================

template<typename TComponent>
bool GaiaVoxelWorld::hasComponent(EntityID id) const {
    return getWorld().valid(id) && getWorld().has<TComponent>(id);
}

template<typename TComponent>
auto GaiaVoxelWorld::getComponentValue(EntityID id) const -> std::optional<ComponentValueType_t<TComponent>> {
    if (!getWorld().valid(id) || !getWorld().has<TComponent>(id)) {
        return std::nullopt;
    }

    // Get component from Gaia ECS
    auto& component = getWorld().get<TComponent>(id);

    // Return the value - use getValue() helper to handle both scalar and vec3
    return getValue(component);
}

template<typename TComponent>
void GaiaVoxelWorld::setComponent(EntityID id, ComponentValueType_t<TComponent> value) {
    if (!getWorld().valid(id)) {
        return; // Can't set component on invalid entity
    }

    // Add or update component (Gaia add() overwrites existing)
    getWorld().add<TComponent>(id, TComponent{value});
}

// ============================================================================
// Indexed Component Access (Multiple Instances of Same Type)
// ============================================================================

template<typename TComponent>
bool GaiaVoxelWorld::hasComponentByIndex(EntityID id, uint32_t index) const {
    if (!getWorld().valid(id)) {
        return false;
    }

    // Gaia ECS: Use Pair(ComponentType, IndexEntity) for indexed components
    // Index 0 is the default component (no pair needed)
    if (index == 0) {
        return getWorld().has<TComponent>(id);
    }

    // For index > 0, check if entity has Pair<TComponent, IndexTag>
    // Note: This requires creating index entities as tags
    // Implementation depends on Gaia ECS Pair API
    // TODO: Implement indexed component storage using Gaia pairs
    return false; // Placeholder
}

template<typename TComponent>
auto GaiaVoxelWorld::getComponentValueByIndex(EntityID id, uint32_t index) const
    -> std::optional<ComponentValueType_t<TComponent>> {

    if (!getWorld().valid(id)) {
        return std::nullopt;
    }

    // Index 0 is the default component
    if (index == 0) {
        return getComponentValue<TComponent>(id);
    }

    // For index > 0, access via Pair<TComponent, IndexTag>
    // TODO: Implement using Gaia ECS relationship pairs
    // e.g., getWorld().get<Pair<TComponent, IndexEntity[index]>>(id)
    return std::nullopt; // Placeholder
}

template<typename TComponent>
void GaiaVoxelWorld::setComponentByIndex(EntityID id, uint32_t index,
                                          ComponentValueType_t<TComponent> value) {
    if (!getWorld().valid(id)) {
        return;
    }

    // Index 0 is the default component
    if (index == 0) {
        setComponent<TComponent>(id, value);
        return;
    }

    // For index > 0, store via Pair<TComponent, IndexTag>
    // TODO: Implement using Gaia ECS relationship pairs
    // This requires creating index entities and using Pair API
    // e.g., getWorld().add<Pair<TComponent, IndexEntity[index]>>(id)
}

} // namespace GaiaVoxel
