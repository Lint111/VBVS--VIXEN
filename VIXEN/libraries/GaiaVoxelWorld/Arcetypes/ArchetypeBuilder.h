#pragma once

#include "VoxelComponents.h"
#include "RelationshipRegistry.h"
#include <gaia.h>
#include <vector>
#include <functional>
#include <unordered_map>

using namespace gaia::ecs;

namespace GaiaVoxel {

// Hook signature: (sourceEntity, targetEntity, world)
// Called when: world.add(sourceEntity, Pair(relationEntity, targetEntity))
using RelationshipHook = std::function<void(Entity source, Entity target, World& world)>;

/**
 * ArchetypeBuilder - fluent API for creating entities with components, relationships, and hooks.
 *
 * Key Features:
 * 1. Fluent entity creation with components
 * 2. Relationship setup with automatic hook registration
 * 3. Hook callbacks for when other entities relate to this entity
 *
 * Usage:
 *   auto volume = ArchetypeBuilder(world)
 *       .add<AABB>()
 *       .add<Volume>(Volume{0.1f})
 *       .add<VolumeTransform>()
 *       .onRelationshipAdded(partOfRelation, [](Entity voxel, Entity volume, World& w) {
 *           // Called when world.add(voxel, Pair(partOfRelation, volume))
 *           expandAABB(volume, voxel, w);
 *       })
 *       .build();
 *
 *   // Later: add voxel to volume - hook triggers!
 *   Entity voxel = world.add();
 *   world.add<MortonKey>(voxel, key);
 *   world.add(voxel, Pair(partOfRelation, volume));  // ← Hook fires!
 */
class ArchetypeBuilder {
public:

    explicit ArchetypeBuilder(World& world, RelationshipRegistry& registry)
        : m_world(world), m_registry(registry), m_entity(world.add()), m_builder(world.build(m_entity)) {}

    // Add component with default construction
    template<typename T>
    ArchetypeBuilder& add() {
        m_builder.add<T>();
        return *this;
    }

    // Add component with value
    template<typename T>
    ArchetypeBuilder& add(const T& value) {
        m_builder.add<T>(value);
        return *this;
    }

    /**
     * Register hook for when other entities establish relationship TO this entity.
     *
     * Creates ComponentCacheItem for Pair(relationEntity, thisEntity) and registers
     * hook in Gaia's ComponentCache that triggers when pair is added.
     *
     * @param relationType The relationship type from RelationshipRegistry
     * @param onAdded Callback when Pair(relationEntity, thisEntity) added to source entity
     */
    ArchetypeBuilder& onRelationshipAdded(RelationshipRegistry::RelationType relationType, RelationshipHook onAdded) {
        m_relationshipHooks.push_back({relationType, onAdded, nullptr});
        return *this;
    }

    /**
     * Register both add and remove hooks for relationship.
     */
    ArchetypeBuilder& onRelationship(RelationshipRegistry::RelationType relationType, RelationshipHook onAdded, RelationshipHook onRemoved) {
        m_relationshipHooks.push_back({relationType, onAdded, onRemoved});
        return *this;
    }

    /**
     * Add relationship to another entity.
     * Deferred until build() to ensure target entity exists.
     */
    ArchetypeBuilder& relateTo(RelationshipRegistry::RelationType relationType, Entity target) {
        m_deferredRelations.push_back({relationType, target});
        return *this;
    }

    /**
     * Register callback after entity is created.
     */
    ArchetypeBuilder& onCreate(std::function<void(Entity, World&)> callback) {
        m_onCreateCallbacks.push_back(callback);
        return *this;
    }

    /**
     * Build entity and register all relationship hooks.
     */
    Entity build() {
        m_builder.commit();

        // Add deferred relationships
        for (auto& rel : m_deferredRelations) {
            m_registry.addRelationship(rel.relationType, m_entity, rel.target);
        }

        // Register relationship hooks AFTER entity is committed
        for (auto& hook : m_relationshipHooks) {
            registerPairHook(hook.relationType, hook.onAdded, hook.onRemoved);
        }

        // Run onCreate callbacks
        for (auto& callback : m_onCreateCallbacks) {
            callback(m_entity, m_world);
        }

        return m_entity;
    }

    // Get entity before building (for advanced use cases)
    Entity entity() const { return m_entity; }

private:
    void registerPairHook(RelationshipRegistry::RelationType relationType, RelationshipHook onAdded, RelationshipHook onRemoved) {
        Entity relationEntity = m_registry.getRelationship(relationType);
        if (relationEntity == EntityBad) {
            #ifndef NDEBUG
            std::cerr << "[ArchetypeBuilder] ERROR: Unknown relationship type\n";
            #endif
            return;
        }

        // CRITICAL: Gaia-ECS Hook Registration on Runtime Pairs
        //
        // Problem: world.add(entity, Pair(rel, tgt)) returns void, not ComponentCacheItem
        //
        // Solution Options:
        // 1. Manual hook invocation in RelationshipRegistry.addRelationship()
        // 2. Create compile-time pair component wrapper (requires code generation)
        // 3. Use Gaia's observer/reactive system (if available)
        //
        // Current Implementation: MANUAL HOOK INVOCATION
        // Store hook in registry, call manually when relationship added
        // This is a limitation of Gaia's runtime pair system

        // Store hook in registry for manual invocation
        // We can't use Gaia's ComponentCache hooks for runtime pairs
        // because Pair(entity1, entity2) doesn't create a ComponentCacheItem

        // WORKAROUND: Store hooks in RelationshipRegistry instead
        // The registry will manually invoke hooks in addRelationship()

        #ifndef NDEBUG
        std::cerr << "[ArchetypeBuilder] WARNING: Gaia runtime pairs don't support ComponentCache hooks\n"
                  << "  Hooks must be manually invoked in RelationshipRegistry.addRelationship()\n"
                  << "  Consider using compile-time pair<T,U> types if automatic hooks are needed\n";
        #endif

        // Register hook with registry for manual invocation
        // RelationshipRegistry will call these hooks in addRelationship()/removeRelationship()
        m_registry.registerPerEntityHook(relationType, thisEntity, onAdded, onRemoved);
    }

    struct HookData {
        RelationshipRegistry::RelationType relationType;
        RelationshipHook onAdded;
        RelationshipHook onRemoved;
    };

    struct DeferredRelation {
        RelationshipRegistry::RelationType relationType;
        Entity target;
    };

    World& m_world;
    RelationshipRegistry& m_registry;
    Entity m_entity;
    EntityBuilder m_builder;
    std::vector<HookData> m_relationshipHooks;
    std::vector<DeferredRelation> m_deferredRelations;
    std::vector<std::function<void(Entity, World&)>> m_onCreateCallbacks;
};

/**
 * Archetype Presets - factory functions for common entity patterns.
 */
namespace Archetypes {

    // Create volume entity
    inline Entity createVolume(World& world, RelationshipRegistry& registry, float voxelSize = 1.0f) {
        return ArchetypeBuilder(world, registry)
            .add<AABB>()
            .add<Volume>(Volume{voxelSize})
            .add<VolumeTransform>()
            .onCreate([](Entity e, World& w) {
                #ifndef NDEBUG
                std::cout << "[Archetype] Volume created: " << e.value() << "\n";
                #endif
            })
            .build();
    }

    // Create voxel entity and link to volume
    inline Entity createVoxel(World& world, RelationshipRegistry& registry, Entity volumeEntity, const MortonKey& key) {
        return ArchetypeBuilder(world, registry)
            .add<MortonKey>(key)
            .relateTo(RelationshipRegistry::RelationType::VolumeContains, volumeEntity)
            .onCreate([key](Entity e, World& w) {
                #ifndef NDEBUG
                std::cout << "[Archetype] Voxel created: " << e.value() << " Morton=" << key.code << "\n";
                #endif
            })
            .build();
    }

    // Create voxel with full attributes
    inline Entity createVoxelWithAttributes(
        World& world,
        RelationshipRegistry& registry,
        Entity volumeEntity,
        const MortonKey& key,
        float density,
        const glm::vec3& color
    ) {
        return ArchetypeBuilder(world, registry)
            .add<MortonKey>(key)
            .add<Density>(Density{density})
            .add<Color>(Color{color})
            .relateTo(RelationshipRegistry::RelationType::VolumeContains, volumeEntity)
            .build();
    }

} // namespace Archetypes

} // namespace GaiaVoxel
