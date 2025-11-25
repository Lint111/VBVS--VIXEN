#pragma once

#include <gaia.h>
#include <glm/glm.hpp>
#include <functional>
#include <vector>
#include <unordered_map>
#include <span>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <optional>

namespace GaiaArchetype {

// Forward declarations
class ArchetypeBuilder;
class ArchetypeRegistry;
class RelationshipManager;

// ============================================================================
// Relationship Types - Define semantic relationships between archetypes
// ============================================================================

/**
 * RelationshipType - Represents a semantic relationship (e.g., "partof", "contains").
 *
 * These are tags that define the nature of entity relationships.
 * Gaia ECS uses Pair(Relation, Target) internally.
 */
struct RelationshipType {
    gaia::ecs::Entity tag;          // Gaia entity used as relationship tag
    std::string name;               // Human-readable name (e.g., "partof")
    bool isExclusive = false;       // If true, entity can only have one of this relationship

    bool operator==(const RelationshipType& other) const {
        return tag == other.tag;
    }
};

// Hash for RelationshipType
struct RelationshipTypeHash {
    size_t operator()(const RelationshipType& rt) const {
        return std::hash<uint64_t>{}(rt.tag.id());
    }
};

// ============================================================================
// Relationship Hook Signatures
// ============================================================================

/**
 * Hook called when a single relationship is added.
 *
 * @param world      ECS world reference
 * @param source     Entity being added to the relationship (e.g., voxel)
 * @param target     Entity receiving the relationship (e.g., volume)
 * @param relation   The relationship type tag
 */
using OnRelationshipAddedFn = std::function<void(
    gaia::ecs::World& world,
    gaia::ecs::Entity source,
    gaia::ecs::Entity target,
    const RelationshipType& relation
)>;

/**
 * Hook called when a relationship is removed.
 */
using OnRelationshipRemovedFn = std::function<void(
    gaia::ecs::World& world,
    gaia::ecs::Entity source,
    gaia::ecs::Entity target,
    const RelationshipType& relation
)>;

/**
 * Bundle hook - called when multiple entities are added to a relationship at once.
 * More efficient than individual hooks for batch operations.
 *
 * @param world      ECS world reference
 * @param sources    Span of source entities being added
 * @param target     Target entity receiving relationships
 * @param relation   The relationship type tag
 */
using OnBundleAddedFn = std::function<void(
    gaia::ecs::World& world,
    std::span<const gaia::ecs::Entity> sources,
    gaia::ecs::Entity target,
    const RelationshipType& relation
)>;

// ============================================================================
// Relationship Hook Container
// ============================================================================

/**
 * RelationshipHooks - Contains all hooks for a specific relationship type.
 */
struct RelationshipHooks {
    OnRelationshipAddedFn onAdded;
    OnRelationshipRemovedFn onRemoved;
    OnBundleAddedFn onBundleAdded;

    // Optional: batch threshold - if more than N entities added, use bundle hook
    size_t bundleThreshold = 16;
};

// ============================================================================
// Archetype Definition
// ============================================================================

/**
 * ArchetypeDefinition - Defines what components and relationships an archetype supports.
 *
 * Created by ArchetypeBuilder, stored in ArchetypeRegistry.
 */
class ArchetypeDefinition {
public:
    using ComponentAdder = std::function<void(gaia::ecs::World&, gaia::ecs::Entity)>;

    std::string name;

    // Component factories (add required components to entity)
    std::vector<ComponentAdder> requiredComponents;
    std::vector<ComponentAdder> optionalComponents;

    // Relationships this archetype can be a TARGET of
    // Key: RelationshipType, Value: Hooks for that relationship
    std::unordered_map<RelationshipType, RelationshipHooks, RelationshipTypeHash> acceptedRelationships;

    // Relationships this archetype can be a SOURCE of
    std::vector<RelationshipType> sourceRelationships;

    // Type information for validation
    std::vector<std::type_index> requiredComponentTypes;
    std::vector<std::type_index> optionalComponentTypes;

    /**
     * Check if this archetype accepts a specific relationship type as target.
     */
    bool acceptsRelationship(const RelationshipType& rel) const {
        return acceptedRelationships.contains(rel);
    }

    /**
     * Get hooks for a relationship type.
     */
    const RelationshipHooks* getHooks(const RelationshipType& rel) const {
        auto it = acceptedRelationships.find(rel);
        return it != acceptedRelationships.end() ? &it->second : nullptr;
    }
};

// ============================================================================
// ArchetypeBuilder - Fluent API for defining archetypes
// ============================================================================

/**
 * ArchetypeBuilder - Fluent builder for creating archetype definitions.
 *
 * Example usage:
 *   auto volumeArchetype = ArchetypeBuilder("VoxelVolume")
 *       .withComponent<ChunkOrigin>()
 *       .withComponent<ChunkMetadata>()
 *       .acceptsRelationship(PartOf)
 *           .onAdded([](auto& world, auto voxel, auto volume, auto& rel) {
 *               // Handle single voxel added to volume
 *           })
 *           .onBundleAdded([](auto& world, auto voxels, auto volume, auto& rel) {
 *               // Handle batch of voxels added to volume (more efficient)
 *           })
 *       .build();
 */
class ArchetypeBuilder {
public:
    explicit ArchetypeBuilder(std::string_view name);

    // Move-only
    ArchetypeBuilder(ArchetypeBuilder&&) = default;
    ArchetypeBuilder& operator=(ArchetypeBuilder&&) = default;
    ArchetypeBuilder(const ArchetypeBuilder&) = delete;
    ArchetypeBuilder& operator=(const ArchetypeBuilder&) = delete;

    // ========================================================================
    // Component Registration
    // ========================================================================

    /**
     * Add a required component with default value.
     */
    template<typename TComponent>
    ArchetypeBuilder& withComponent() {
        m_definition.requiredComponentTypes.push_back(std::type_index(typeid(TComponent)));
        m_definition.requiredComponents.push_back([](gaia::ecs::World& world, gaia::ecs::Entity entity) {
            world.add<TComponent>(entity);
        });
        return *this;
    }

    /**
     * Add a required component with specific initial value.
     */
    template<typename TComponent>
    ArchetypeBuilder& withComponent(const TComponent& initialValue) {
        m_definition.requiredComponentTypes.push_back(std::type_index(typeid(TComponent)));
        m_definition.requiredComponents.push_back([initialValue](gaia::ecs::World& world, gaia::ecs::Entity entity) {
            world.add<TComponent>(entity, initialValue);
        });
        return *this;
    }

    /**
     * Add an optional component (added only if explicitly requested).
     */
    template<typename TComponent>
    ArchetypeBuilder& withOptionalComponent() {
        m_definition.optionalComponentTypes.push_back(std::type_index(typeid(TComponent)));
        m_definition.optionalComponents.push_back([](gaia::ecs::World& world, gaia::ecs::Entity entity) {
            world.add<TComponent>(entity);
        });
        return *this;
    }

    // ========================================================================
    // Relationship Registration (Fluent Sub-Builder)
    // ========================================================================

    /**
     * Declare that this archetype accepts a specific relationship type as TARGET.
     * Returns a sub-builder for configuring hooks.
     *
     * Example:
     *   builder.acceptsRelationship(PartOf)
     *          .onAdded(myCallback)
     *          .onBundleAdded(myBatchCallback);
     */
    class RelationshipConfigBuilder {
    public:
        RelationshipConfigBuilder(ArchetypeBuilder& parent, const RelationshipType& rel)
            : m_parent(parent), m_relation(rel) {}

        /**
         * Set hook for when a single entity is added to this relationship.
         */
        RelationshipConfigBuilder& onAdded(OnRelationshipAddedFn fn) {
            m_hooks.onAdded = std::move(fn);
            return *this;
        }

        /**
         * Set hook for when an entity is removed from this relationship.
         */
        RelationshipConfigBuilder& onRemoved(OnRelationshipRemovedFn fn) {
            m_hooks.onRemoved = std::move(fn);
            return *this;
        }

        /**
         * Set hook for batch additions (more efficient than per-entity).
         */
        RelationshipConfigBuilder& onBundleAdded(OnBundleAddedFn fn) {
            m_hooks.onBundleAdded = std::move(fn);
            return *this;
        }

        /**
         * Set threshold for when to use bundle hook vs individual hooks.
         * Default is 16 entities.
         */
        RelationshipConfigBuilder& bundleThreshold(size_t threshold) {
            m_hooks.bundleThreshold = threshold;
            return *this;
        }

        /**
         * Finish configuring this relationship and return to parent builder.
         */
        ArchetypeBuilder& done() {
            m_parent.m_definition.acceptedRelationships[m_relation] = std::move(m_hooks);
            return m_parent;
        }

        // Allow chaining directly without done() by implicit conversion
        operator ArchetypeBuilder&() {
            return done();
        }

    private:
        ArchetypeBuilder& m_parent;
        RelationshipType m_relation;
        RelationshipHooks m_hooks;
    };

    RelationshipConfigBuilder acceptsRelationship(const RelationshipType& rel) {
        return RelationshipConfigBuilder(*this, rel);
    }

    /**
     * Declare that entities of this archetype can be SOURCES of a relationship.
     * (e.g., Voxels can be "partof" something)
     */
    ArchetypeBuilder& canRelate(const RelationshipType& rel) {
        m_definition.sourceRelationships.push_back(rel);
        return *this;
    }

    // ========================================================================
    // Build
    // ========================================================================

    /**
     * Finalize and return the archetype definition.
     */
    ArchetypeDefinition build() {
        return std::move(m_definition);
    }

private:
    ArchetypeDefinition m_definition;
};

// ============================================================================
// ArchetypeRegistry - Central registry for all archetype definitions
// ============================================================================

/**
 * ArchetypeRegistry - Stores and manages archetype definitions.
 *
 * Provides lookup and validation for archetypes.
 */
class ArchetypeRegistry {
public:
    /**
     * Register an archetype definition.
     */
    void registerArchetype(ArchetypeDefinition definition) {
        m_archetypes[definition.name] = std::move(definition);
    }

    /**
     * Get archetype by name.
     */
    const ArchetypeDefinition* getArchetype(std::string_view name) const {
        auto it = m_archetypes.find(std::string(name));
        return it != m_archetypes.end() ? &it->second : nullptr;
    }

    /**
     * Check if archetype exists.
     */
    bool hasArchetype(std::string_view name) const {
        return m_archetypes.contains(std::string(name));
    }

    /**
     * Get all registered archetypes.
     */
    const auto& archetypes() const { return m_archetypes; }

private:
    std::unordered_map<std::string, ArchetypeDefinition> m_archetypes;
};

// ============================================================================
// RelationshipManager - Manages relationship creation with hook invocation
// ============================================================================

/**
 * RelationshipManager - Creates relationships between entities and invokes hooks.
 *
 * This is the core class that connects Gaia ECS relationships with archetype hooks.
 *
 * Usage:
 *   RelationshipManager rm(world, registry);
 *   rm.createRelationship(voxelEntity, volumeEntity, PartOf);  // Invokes hooks
 *   rm.createRelationshipBatch(voxels, volumeEntity, PartOf);  // Batch with bundle hook
 */
class RelationshipManager {
public:
    RelationshipManager(gaia::ecs::World& world, const ArchetypeRegistry& registry)
        : m_world(world), m_registry(registry) {}

    /**
     * Create a relationship between source and target entities.
     * Invokes onAdded hook if target's archetype accepts this relationship.
     *
     * @param source   Source entity (e.g., voxel)
     * @param target   Target entity (e.g., volume)
     * @param relation Relationship type
     * @return true if relationship was created and hooks invoked
     */
    bool createRelationship(
        gaia::ecs::Entity source,
        gaia::ecs::Entity target,
        const RelationshipType& relation);

    /**
     * Create relationships in batch - invokes bundle hook for efficiency.
     *
     * @param sources  Vector of source entities
     * @param target   Target entity
     * @param relation Relationship type
     * @return Number of relationships created
     */
    size_t createRelationshipBatch(
        std::span<const gaia::ecs::Entity> sources,
        gaia::ecs::Entity target,
        const RelationshipType& relation);

    /**
     * Remove a relationship between entities.
     * Invokes onRemoved hook if registered.
     */
    bool removeRelationship(
        gaia::ecs::Entity source,
        gaia::ecs::Entity target,
        const RelationshipType& relation);

    /**
     * Check if a relationship exists.
     */
    bool hasRelationship(
        gaia::ecs::Entity source,
        gaia::ecs::Entity target,
        const RelationshipType& relation) const;

    /**
     * Get all entities related to target via a specific relationship.
     */
    std::vector<gaia::ecs::Entity> getRelatedEntities(
        gaia::ecs::Entity target,
        const RelationshipType& relation) const;

private:
    gaia::ecs::World& m_world;
    const ArchetypeRegistry& m_registry;

    // Find archetype definition for an entity (by checking its components)
    const ArchetypeDefinition* findArchetypeForEntity(gaia::ecs::Entity entity) const;
};

// ============================================================================
// Entity Factory - Creates entities from archetype definitions
// ============================================================================

/**
 * EntityFactory - Creates entities conforming to archetype definitions.
 *
 * Usage:
 *   EntityFactory factory(world, registry);
 *   auto volume = factory.create("VoxelVolume");
 *   auto voxel = factory.create("Voxel", {position, density});
 */
class EntityFactory {
public:
    EntityFactory(gaia::ecs::World& world, const ArchetypeRegistry& registry)
        : m_world(world), m_registry(registry) {}

    /**
     * Create an entity from an archetype definition.
     * Adds all required components with default values.
     */
    gaia::ecs::Entity create(std::string_view archetypeName);

    /**
     * Create an entity with optional component overrides.
     *
     * @param archetypeName Name of archetype
     * @param componentSetters Functions to set component values after creation
     */
    template<typename... Setters>
    gaia::ecs::Entity create(std::string_view archetypeName, Setters&&... setters) {
        auto entity = create(archetypeName);
        if (m_world.valid(entity)) {
            (setters(m_world, entity), ...);
        }
        return entity;
    }

    /**
     * Create multiple entities in batch.
     */
    std::vector<gaia::ecs::Entity> createBatch(std::string_view archetypeName, size_t count);

private:
    gaia::ecs::World& m_world;
    const ArchetypeRegistry& m_registry;
};

// ============================================================================
// Common Relationship Types (Pre-defined)
// ============================================================================

namespace Relations {
    /**
     * Create a "PartOf" relationship type.
     * Semantics: source IS PART OF target (e.g., voxel partof volume)
     */
    RelationshipType createPartOf(gaia::ecs::World& world);

    /**
     * Create a "Contains" relationship type.
     * Semantics: source CONTAINS target (inverse of PartOf)
     */
    RelationshipType createContains(gaia::ecs::World& world);

    /**
     * Create a "ChildOf" relationship type (wraps Gaia's built-in).
     */
    RelationshipType createChildOf(gaia::ecs::World& world);

    /**
     * Create a custom relationship type.
     *
     * @param world ECS world
     * @param name Human-readable name
     * @param exclusive If true, entity can only have one relationship of this type
     */
    RelationshipType createCustom(gaia::ecs::World& world, std::string_view name, bool exclusive = false);
}

} // namespace GaiaArchetype
