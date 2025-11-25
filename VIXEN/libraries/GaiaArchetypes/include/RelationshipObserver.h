#pragma once

#include <gaia.h>
#include <functional>
#include <unordered_map>
#include <vector>
#include <span>
#include <memory>
#include <string>
#include <string_view>
#include <mutex>
#include <optional>

namespace GaiaArchetype {

// Forward declarations
struct RelationshipType;

// ============================================================================
// Relationship Observer - Wraps Gaia ECS to provide relationship hooks
// ============================================================================

/**
 * RelationshipObserver - Intercepts and tracks relationship operations.
 *
 * Since Gaia ECS doesn't provide native hooks for Pair (relationship) operations,
 * this class wraps the World and provides callback functionality.
 *
 * ARCHITECTURE:
 * - All relationship operations go through this class
 * - Maintains a registry of callbacks per relationship type
 * - Supports both individual and batch operations
 * - Thread-safe callback invocation
 *
 * Usage:
 *   RelationshipObserver observer(world);
 *
 *   // Register callback for when voxels are added to volumes
 *   observer.onRelationshipAdded(PartOf, [](auto& ctx) {
 *       std::cout << "Voxel " << ctx.source << " added to volume " << ctx.target << "\n";
 *   });
 *
 *   // Add relationship (triggers callback)
 *   observer.addRelationship(voxelEntity, volumeEntity, PartOf);
 */
class RelationshipObserver {
public:
    // ========================================================================
    // Callback Context Structures
    // ========================================================================

    /**
     * Context passed to relationship callbacks.
     */
    struct RelationshipContext {
        gaia::ecs::World& world;
        gaia::ecs::Entity source;      // Entity with the relationship
        gaia::ecs::Entity target;      // Target of the relationship
        gaia::ecs::Entity relationTag; // The relationship type tag

        // Helper to get component from source
        template<typename T>
        T* getSourceComponent() {
            if (world.valid(source) && world.has<T>(source)) {
                return &world.set<T>(source);
            }
            return nullptr;
        }

        // Helper to get component from target
        template<typename T>
        T* getTargetComponent() {
            if (world.valid(target) && world.has<T>(target)) {
                return &world.set<T>(target);
            }
            return nullptr;
        }

        // Helper to get const component from source
        template<typename T>
        const T* getSourceComponentConst() const {
            if (world.valid(source) && world.has<T>(source)) {
                return &world.get<T>(source);
            }
            return nullptr;
        }

        // Helper to get const component from target
        template<typename T>
        const T* getTargetComponentConst() const {
            if (world.valid(target) && world.has<T>(target)) {
                return &world.get<T>(target);
            }
            return nullptr;
        }
    };

    /**
     * Context passed to batch relationship callbacks.
     */
    struct BatchRelationshipContext {
        gaia::ecs::World& world;
        std::span<const gaia::ecs::Entity> sources;  // All source entities
        gaia::ecs::Entity target;                     // Common target
        gaia::ecs::Entity relationTag;                // The relationship type tag

        // Helper to iterate sources with component access
        template<typename T, typename Fn>
        void forEachSourceWithComponent(Fn&& fn) {
            for (auto source : sources) {
                if (world.valid(source) && world.has<T>(source)) {
                    fn(source, world.get<T>(source));
                }
            }
        }
    };

    // ========================================================================
    // Callback Types
    // ========================================================================

    using OnAddedCallback = std::function<void(const RelationshipContext&)>;
    using OnRemovedCallback = std::function<void(const RelationshipContext&)>;
    using OnBatchAddedCallback = std::function<void(const BatchRelationshipContext&)>;
    using OnBatchRemovedCallback = std::function<void(const BatchRelationshipContext&)>;

    // ========================================================================
    // Construction
    // ========================================================================

    explicit RelationshipObserver(gaia::ecs::World& world);

    // Non-copyable, non-movable (references world)
    RelationshipObserver(const RelationshipObserver&) = delete;
    RelationshipObserver& operator=(const RelationshipObserver&) = delete;

    // ========================================================================
    // Callback Registration
    // ========================================================================

    /**
     * Register callback for when a relationship is added.
     *
     * @param relationTag  Entity tag representing the relationship type
     * @param callback     Function to call when relationship is added
     * @return Handle that can be used to unregister the callback
     */
    size_t onRelationshipAdded(gaia::ecs::Entity relationTag, OnAddedCallback callback);

    /**
     * Register callback for when a relationship is removed.
     */
    size_t onRelationshipRemoved(gaia::ecs::Entity relationTag, OnRemovedCallback callback);

    /**
     * Register callback for batch relationship additions.
     * Called instead of individual callbacks when adding multiple relationships at once.
     */
    size_t onBatchAdded(gaia::ecs::Entity relationTag, OnBatchAddedCallback callback);

    /**
     * Register callback for batch relationship removals.
     */
    size_t onBatchRemoved(gaia::ecs::Entity relationTag, OnBatchRemovedCallback callback);

    /**
     * Unregister a callback by handle.
     */
    void unregisterCallback(size_t handle);

    // ========================================================================
    // Relationship Operations (Use these instead of direct Gaia calls)
    // ========================================================================

    /**
     * Add a relationship between source and target entities.
     * Triggers registered onAdded callbacks.
     *
     * @param source       Entity that will have the relationship
     * @param target       Target entity of the relationship
     * @param relationTag  Relationship type tag entity
     * @return true if relationship was added successfully
     */
    bool addRelationship(
        gaia::ecs::Entity source,
        gaia::ecs::Entity target,
        gaia::ecs::Entity relationTag);

    /**
     * Add relationships in batch - more efficient for many entities.
     * Triggers onBatchAdded callback if registered, otherwise individual callbacks.
     *
     * @param sources     Vector of source entities
     * @param target      Common target entity
     * @param relationTag Relationship type tag
     * @return Number of relationships successfully added
     */
    size_t addRelationshipBatch(
        std::span<const gaia::ecs::Entity> sources,
        gaia::ecs::Entity target,
        gaia::ecs::Entity relationTag);

    /**
     * Remove a relationship between entities.
     * Triggers registered onRemoved callbacks.
     */
    bool removeRelationship(
        gaia::ecs::Entity source,
        gaia::ecs::Entity target,
        gaia::ecs::Entity relationTag);

    /**
     * Remove relationships in batch.
     */
    size_t removeRelationshipBatch(
        std::span<const gaia::ecs::Entity> sources,
        gaia::ecs::Entity target,
        gaia::ecs::Entity relationTag);

    // ========================================================================
    // Query Operations
    // ========================================================================

    /**
     * Check if a relationship exists.
     */
    bool hasRelationship(
        gaia::ecs::Entity source,
        gaia::ecs::Entity target,
        gaia::ecs::Entity relationTag) const;

    /**
     * Get all entities that are sources of a relationship to target.
     * (i.e., all entities that have Pair(relationTag, target))
     */
    std::vector<gaia::ecs::Entity> getSourcesFor(
        gaia::ecs::Entity target,
        gaia::ecs::Entity relationTag) const;

    /**
     * Get all entities that are targets of relationships from source.
     * (i.e., for each Pair(relationTag, X) on source, return X)
     */
    std::vector<gaia::ecs::Entity> getTargetsFor(
        gaia::ecs::Entity source,
        gaia::ecs::Entity relationTag) const;

    /**
     * Count relationships of a type from source.
     */
    size_t countRelationships(
        gaia::ecs::Entity source,
        gaia::ecs::Entity relationTag) const;

    // ========================================================================
    // Batch Processing Utilities
    // ========================================================================

    /**
     * Set the threshold for when batch callbacks are used instead of individual.
     * Default is 16 entities.
     */
    void setBatchThreshold(size_t threshold) { m_batchThreshold = threshold; }
    size_t getBatchThreshold() const { return m_batchThreshold; }

    /**
     * Enable/disable deferred callback execution (for batching).
     * When enabled, callbacks are queued and executed in flush().
     */
    void setDeferredMode(bool enabled) { m_deferredMode = enabled; }
    bool isDeferredMode() const { return m_deferredMode; }

    /**
     * Execute all deferred callbacks.
     * Call this after a batch of operations when in deferred mode.
     */
    void flush();

    // ========================================================================
    // Direct World Access (for advanced use)
    // ========================================================================

    gaia::ecs::World& world() { return m_world; }
    const gaia::ecs::World& world() const { return m_world; }

private:
    // ========================================================================
    // Internal Types
    // ========================================================================

    struct CallbackEntry {
        size_t handle;
        OnAddedCallback onAdded;
        OnRemovedCallback onRemoved;
        OnBatchAddedCallback onBatchAdded;
        OnBatchRemovedCallback onBatchRemoved;
    };

    struct DeferredOp {
        enum class Type { Add, Remove };
        Type type;
        gaia::ecs::Entity source;
        gaia::ecs::Entity target;
        gaia::ecs::Entity relationTag;
    };

    // Key for callback lookup: relationship tag entity ID
    using CallbackKey = uint64_t;

    // ========================================================================
    // Internal Methods
    // ========================================================================

    void invokeAddedCallbacks(
        gaia::ecs::Entity source,
        gaia::ecs::Entity target,
        gaia::ecs::Entity relationTag);

    void invokeRemovedCallbacks(
        gaia::ecs::Entity source,
        gaia::ecs::Entity target,
        gaia::ecs::Entity relationTag);

    void invokeBatchAddedCallbacks(
        std::span<const gaia::ecs::Entity> sources,
        gaia::ecs::Entity target,
        gaia::ecs::Entity relationTag);

    // ========================================================================
    // Member Data
    // ========================================================================

    gaia::ecs::World& m_world;

    // Callbacks indexed by relationship tag ID
    std::unordered_map<CallbackKey, std::vector<CallbackEntry>> m_callbacks;

    // Handle counter for callback registration
    size_t m_nextHandle = 1;

    // Batch processing settings
    size_t m_batchThreshold = 16;
    bool m_deferredMode = false;

    // Deferred operations queue
    std::vector<DeferredOp> m_deferredOps;

    // Thread safety
    mutable std::mutex m_mutex;
};

// ============================================================================
// RelationshipTypeRegistry - Creates and caches relationship type tags
// ============================================================================

/**
 * RelationshipTypeRegistry - Manages relationship type tag entities.
 *
 * Ensures each relationship type name maps to a unique tag entity.
 * Tags are cached for efficient lookup.
 */
class RelationshipTypeRegistry {
public:
    explicit RelationshipTypeRegistry(gaia::ecs::World& world);

    /**
     * Get or create a relationship type tag.
     *
     * @param name Unique name for the relationship type
     * @return Tag entity for this relationship type
     */
    gaia::ecs::Entity getOrCreate(std::string_view name);

    /**
     * Get a relationship type tag by name (returns invalid if not found).
     */
    std::optional<gaia::ecs::Entity> get(std::string_view name) const;

    /**
     * Check if a relationship type exists.
     */
    bool exists(std::string_view name) const;

    /**
     * Get the name of a relationship type tag.
     */
    std::optional<std::string_view> getName(gaia::ecs::Entity tag) const;

    // Common relationship types (created on first access)
    gaia::ecs::Entity partOf();     // "partof" - source is part of target
    gaia::ecs::Entity contains();   // "contains" - source contains target
    gaia::ecs::Entity childOf();    // Uses Gaia's built-in ChildOf

private:
    gaia::ecs::World& m_world;
    std::unordered_map<std::string, gaia::ecs::Entity> m_nameToTag;
    std::unordered_map<uint64_t, std::string> m_tagToName;
};

// ============================================================================
// Helper Macros for Common Patterns
// ============================================================================

/**
 * Macro to define a typed relationship accessor on a class.
 *
 * Usage:
 *   class VoxelVolume {
 *       DEFINE_RELATIONSHIP_ACCESSOR(Voxels, PartOf)
 *       // Creates: addVoxel(), removeVoxel(), getVoxels(), hasVoxel()
 *   };
 */
#define DEFINE_RELATIONSHIP_ACCESSOR(Name, RelationType) \
    bool add##Name(gaia::ecs::Entity source) { \
        return m_observer.addRelationship(source, m_entity, m_types.RelationType()); \
    } \
    bool remove##Name(gaia::ecs::Entity source) { \
        return m_observer.removeRelationship(source, m_entity, m_types.RelationType()); \
    } \
    std::vector<gaia::ecs::Entity> get##Name##s() const { \
        return m_observer.getSourcesFor(m_entity, m_types.RelationType()); \
    } \
    bool has##Name(gaia::ecs::Entity source) const { \
        return m_observer.hasRelationship(source, m_entity, m_types.RelationType()); \
    }

} // namespace GaiaArchetype
