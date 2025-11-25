#pragma once

#include <gaia.h>
#include <unordered_map>
#include <string>
#include <functional>

using namespace gaia::ecs;

namespace GaiaVoxel {

/**
 * RelationshipRegistry - centralized management of relationship tag entities and hooks.
 *
 * Solves:
 * 1. Singleton relationship entities (PartOf, Contains, etc.)
 * 2. Hook registration for relationship events
 * 3. Coherent relationship lifecycle management
 *
 * Architecture:
 * - Relationship types defined once, reused everywhere
 * - Hooks registered centrally, not scattered across archetypes
 * - Clear ownership: registry owns relationship entities
 */
class RelationshipRegistry {
public:
    // Standard relationship types
    enum class RelationType {
        PartOf,          // Entity is part of another (composition)
        Contains,        // Entity contains another (inverse of PartOf)
        ChildOf,         // Parent-child hierarchy
        VolumeContains,  // Volume contains voxel data
        Uses,            // Entity uses another (dependency)
        References,      // Weak reference
    };

    RelationshipRegistry(World& world) : m_world(world) {}

    // Initialize all relationship entities and hooks
    void initialize() {
        registerRelationshipType(RelationType::PartOf, "PartOf");
        registerRelationshipType(RelationType::Contains, "Contains");
        registerRelationshipType(RelationType::ChildOf, "ChildOf");
        registerRelationshipType(RelationType::VolumeContains, "VolumeContains");
        registerRelationshipType(RelationType::Uses, "Uses");
        registerRelationshipType(RelationType::References, "References");
    }

    // Get relationship entity for a type
    Entity getRelationship(RelationType type) const {
        auto it = m_relationships.find(type);
        if (it != m_relationships.end()) {
            return it->second;
        }
        return EntityBad;
    }

    /**
     * Register global hook for ALL instances of a relationship type.
     *
     * Example: onRelationshipAdded(VolumeContains, [](World& w, Entity voxel, Entity volume) {
     *              // Expand volume AABB when ANY voxel added to ANY volume
     *          });
     *
     * This is different from ArchetypeBuilder hooks, which are per-target-entity.
     * Global hooks trigger for ALL pairs of this type, useful for system-wide logic.
     */
    void onRelationshipAdded(RelationType type, std::function<void(World&, Entity source, Entity target)> callback) {
        Entity relationEntity = getRelationship(type);
        if (relationEntity == EntityBad) return;

        // Register hook that triggers for ALL Pair(relationEntity, *) additions
        // This is a global hook, not tied to specific target entity
        auto hookIndex = m_addHooks.size();
        m_addHooks.push_back({type, callback});

        // Register via Gaia ComponentCache (using wildcard pattern for ALL targets)
        // Note: Gaia doesn't support wildcard hooks directly, so we'd need to register
        // per-target hooks via ArchetypeBuilder instead for specific entity pairs.

        #ifndef NDEBUG
        std::cout << "[RelationshipRegistry] Registered global add hook for relationship type: "
                  << static_cast<int>(type) << "\n";
        #endif
    }

    /**
     * Register global hook for when specific relationship removed.
     */
    void onRelationshipRemoved(RelationType type, std::function<void(World&, Entity source, Entity target)> callback) {
        auto hookIndex = m_removeHooks.size();
        m_removeHooks.push_back({type, callback});

        #ifndef NDEBUG
        std::cout << "[RelationshipRegistry] Registered global remove hook for relationship type: "
                  << static_cast<int>(type) << "\n";
        #endif
    }

    /**
     * Register per-entity hook (called by ArchetypeBuilder).
     * Stores hook for specific (relation, target) pair for manual invocation.
     */
    void registerPerEntityHook(
        RelationType type,
        Entity targetEntity,
        std::function<void(Entity source, Entity target, World&)> onAdded,
        std::function<void(Entity source, Entity target, World&)> onRemoved
    ) {
        if (onAdded) {
            m_perEntityAddHooks[{type, targetEntity}] = onAdded;
        }
        if (onRemoved) {
            m_perEntityRemoveHooks[{type, targetEntity}] = onRemoved;
        }
    }

    // Helper: create relationship between two entities
    void addRelationship(RelationType type, Entity source, Entity target) {
        Entity relationEntity = getRelationship(type);
        if (relationEntity == EntityBad) return;

        m_world.add(source, Pair(relationEntity, target));

        // MANUAL HOOK INVOCATION: Check for per-entity hooks
        auto perEntityKey = std::make_pair(type, target);
        auto it = m_perEntityAddHooks.find(perEntityKey);
        if (it != m_perEntityAddHooks.end()) {
            it->second(source, target, m_world);
        }

        // Also invoke global hooks
        for (auto& hook : m_addHooks) {
            if (hook.first == type) {
                hook.second(m_world, source, target);
            }
        }
    }

    // Helper: remove relationship
    void removeRelationship(RelationType type, Entity source, Entity target) {
        Entity relationEntity = getRelationship(type);
        if (relationEntity == EntityBad) return;

        m_world.del(source, Pair(relationEntity, target));

        // MANUAL HOOK INVOCATION: Check for per-entity hooks
        auto perEntityKey = std::make_pair(type, target);
        auto it = m_perEntityRemoveHooks.find(perEntityKey);
        if (it != m_perEntityRemoveHooks.end()) {
            it->second(source, target, m_world);
        }

        // Also invoke global hooks
        for (auto& hook : m_removeHooks) {
            if (hook.first == type) {
                hook.second(m_world, source, target);
            }
        }
    }

    // Query: find all entities with specific relationship to target
    // Example: findAllWithRelationship(VolumeContains, volumeEntity) -> all voxels in volume
    std::vector<Entity> findAllWithRelationship(RelationType type, Entity target) const {
        Entity relationEntity = getRelationship(type);
        std::vector<Entity> results;

        if (relationEntity == EntityBad) return results;

        // Query all entities with Pair(relationEntity, target)
        auto query = m_world.query().all(Pair(relationEntity, target));
        query.each([&](Entity e) {
            results.push_back(e);
        });

        return results;
    }

    /**
     * Get target of relationship from source entity.
     *
     * Uses Gaia's world.target() API to extract the target entity from a pair.
     *
     * Example:
     *   world.add(voxel, Pair(volumeContainsEntity, volumeEntity));
     *   Entity volume = getRelationshipTarget(VolumeContains, voxel);
     *   // Returns: volumeEntity
     */
    Entity getRelationshipTarget(RelationType type, Entity source) const {
        Entity relationEntity = getRelationship(type);
        if (relationEntity == EntityBad) return EntityBad;

        // Gaia API: world.target(entity, relation) returns first target of (relation, *)
        // For Pair(relationEntity, targetEntity) on source, this extracts targetEntity
        return m_world.target(source, relationEntity);
    }

    /**
     * Get all targets of relationship from source entity.
     *
     * Returns all entities that source has relationship with.
     *
     * Example:
     *   world.add(voxel, Pair(volumeContains, volume1));
     *   world.add(voxel, Pair(volumeContains, volume2));
     *   auto volumes = getRelationshipTargets(VolumeContains, voxel);
     *   // Returns: [volume1, volume2]
     */
    std::vector<Entity> getRelationshipTargets(RelationType type, Entity source) const {
        Entity relationEntity = getRelationship(type);
        std::vector<Entity> targets;
        if (relationEntity == EntityBad) return targets;

        // Gaia API: world.targets(entity, relation, callback)
        // Iterates all targets of (relation, *) on entity
        m_world.targets(source, relationEntity, [&targets](Entity target) {
            targets.push_back(target);
        });

        return targets;
    }

    /**
     * Get relation entity from source to target.
     *
     * Uses Gaia's world.relation() API to find which relationship connects entities.
     *
     * Example:
     *   world.add(voxel, Pair(volumeContains, volume));
     *   Entity rel = getRelationBetween(voxel, volume);
     *   // Returns: volumeContainsEntity
     */
    Entity getRelationBetween(Entity source, Entity target) const {
        // Gaia API: world.relation(entity, target) returns first relation of (*, target)
        return m_world.relation(source, target);
    }

    /**
     * Check if relationship exists between entities.
     */
    bool hasRelationship(RelationType type, Entity source, Entity target) const {
        Entity relationEntity = getRelationship(type);
        if (relationEntity == EntityBad) return false;

        // Gaia API: world.has(entity, Pair(relation, target))
        return m_world.has(source, Pair(relationEntity, target));
    }

private:
    void registerRelationshipType(RelationType type, const std::string& name) {
        Entity relationEntity = m_world.add();
        m_world.name(relationEntity, name.c_str());
        m_relationships[type] = relationEntity;
    }

    World& m_world;
    std::unordered_map<RelationType, Entity> m_relationships;

    // Global hooks: trigger for ALL instances of relationship type
    std::vector<std::pair<RelationType, std::function<void(World&, Entity, Entity)>>> m_addHooks;
    std::vector<std::pair<RelationType, std::function<void(World&, Entity, Entity)>>> m_removeHooks;

    // Per-entity hooks: trigger only for specific (relation, target) pair
    // Key: {RelationType, targetEntity} → Hook function
    // Workaround for Gaia's lack of ComponentCache hooks on runtime Pair(entity, entity)
    using HookKey = std::pair<RelationType, Entity>;

    struct HookKeyHash {
        std::size_t operator()(const HookKey& key) const {
            return std::hash<int>()(static_cast<int>(key.first)) ^
                   std::hash<uint32_t>()(key.second.value());
        }
    };

    std::unordered_map<HookKey, std::function<void(Entity, Entity, World&)>, HookKeyHash> m_perEntityAddHooks;
    std::unordered_map<HookKey, std::function<void(Entity, Entity, World&)>, HookKeyHash> m_perEntityRemoveHooks;
};

} // namespace GaiaVoxel
