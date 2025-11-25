#include "ArchetypeBuilder.h"
#include <iostream>
#include <algorithm>

namespace GaiaArchetype {

// ============================================================================
// ArchetypeBuilder Implementation
// ============================================================================

ArchetypeBuilder::ArchetypeBuilder(std::string_view name) {
    m_definition.name = std::string(name);
}

// ============================================================================
// RelationshipManager Implementation
// ============================================================================

bool RelationshipManager::createRelationship(
    gaia::ecs::Entity source,
    gaia::ecs::Entity target,
    const RelationshipType& relation) {

    if (!m_world.valid(source) || !m_world.valid(target)) {
        return false;
    }

    // Create the Gaia ECS relationship: source has Pair(relation.tag, target)
    m_world.add(source, gaia::ecs::Pair(relation.tag, target));

    // Find target's archetype and invoke hooks if registered
    // We iterate through registered archetypes to find one that matches the target
    for (const auto& [name, archetype] : m_registry.archetypes()) {
        // Check if this archetype accepts the relationship
        const auto* hooks = archetype.getHooks(relation);
        if (hooks && hooks->onAdded) {
            // Validate target has required components for this archetype
            bool matches = true;
            for (const auto& componentType : archetype.requiredComponentTypes) {
                // Note: In a full implementation, we'd check actual components
                // For now, we invoke hook if archetype accepts this relationship
            }

            if (matches) {
                hooks->onAdded(m_world, source, target, relation);
                return true;
            }
        }
    }

    return true; // Relationship created even without hook
}

size_t RelationshipManager::createRelationshipBatch(
    std::span<const gaia::ecs::Entity> sources,
    gaia::ecs::Entity target,
    const RelationshipType& relation) {

    if (!m_world.valid(target) || sources.empty()) {
        return 0;
    }

    // Find hooks for target's archetype
    const RelationshipHooks* hooks = nullptr;
    for (const auto& [name, archetype] : m_registry.archetypes()) {
        hooks = archetype.getHooks(relation);
        if (hooks) break;
    }

    // Create all relationships first
    size_t created = 0;
    std::vector<gaia::ecs::Entity> validSources;
    validSources.reserve(sources.size());

    for (auto source : sources) {
        if (m_world.valid(source)) {
            m_world.add(source, gaia::ecs::Pair(relation.tag, target));
            validSources.push_back(source);
            created++;
        }
    }

    // Invoke hooks
    if (hooks && !validSources.empty()) {
        // Use bundle hook if available and threshold met
        if (hooks->onBundleAdded && validSources.size() >= hooks->bundleThreshold) {
            hooks->onBundleAdded(m_world, validSources, target, relation);
        }
        // Otherwise use individual hooks
        else if (hooks->onAdded) {
            for (auto source : validSources) {
                hooks->onAdded(m_world, source, target, relation);
            }
        }
    }

    return created;
}

bool RelationshipManager::removeRelationship(
    gaia::ecs::Entity source,
    gaia::ecs::Entity target,
    const RelationshipType& relation) {

    if (!m_world.valid(source) || !m_world.valid(target)) {
        return false;
    }

    // Check if relationship exists
    if (!m_world.has(source, gaia::ecs::Pair(relation.tag, target))) {
        return false;
    }

    // Find and invoke onRemoved hook
    for (const auto& [name, archetype] : m_registry.archetypes()) {
        const auto* hooks = archetype.getHooks(relation);
        if (hooks && hooks->onRemoved) {
            hooks->onRemoved(m_world, source, target, relation);
            break;
        }
    }

    // Remove the relationship
    m_world.del(source, gaia::ecs::Pair(relation.tag, target));

    return true;
}

bool RelationshipManager::hasRelationship(
    gaia::ecs::Entity source,
    gaia::ecs::Entity target,
    const RelationshipType& relation) const {

    if (!m_world.valid(source) || !m_world.valid(target)) {
        return false;
    }

    return m_world.has(source, gaia::ecs::Pair(relation.tag, target));
}

std::vector<gaia::ecs::Entity> RelationshipManager::getRelatedEntities(
    gaia::ecs::Entity target,
    const RelationshipType& relation) const {

    std::vector<gaia::ecs::Entity> results;

    if (!m_world.valid(target)) {
        return results;
    }

    // Query all entities that have a Pair(relation.tag, target) relationship
    // Note: Gaia ECS wildcard queries would be ideal here
    // For now, we iterate entities with the relationship tag
    auto query = m_world.query().all(gaia::ecs::Pair(relation.tag, target));

    query.each([&results](gaia::ecs::Entity entity) {
        results.push_back(entity);
    });

    return results;
}

const ArchetypeDefinition* RelationshipManager::findArchetypeForEntity(
    gaia::ecs::Entity entity) const {

    // This would check which archetype definition matches the entity's components
    // For now, return nullptr (would need component type checking)
    return nullptr;
}

// ============================================================================
// EntityFactory Implementation
// ============================================================================

gaia::ecs::Entity EntityFactory::create(std::string_view archetypeName) {
    const auto* archetype = m_registry.getArchetype(archetypeName);
    if (!archetype) {
        std::cerr << "[EntityFactory] Unknown archetype: " << archetypeName << "\n";
        return gaia::ecs::Entity();
    }

    // Create entity
    auto entity = m_world.add();

    // Add all required components
    for (const auto& adder : archetype->requiredComponents) {
        adder(m_world, entity);
    }

    return entity;
}

std::vector<gaia::ecs::Entity> EntityFactory::createBatch(
    std::string_view archetypeName, size_t count) {

    std::vector<gaia::ecs::Entity> entities;
    entities.reserve(count);

    const auto* archetype = m_registry.getArchetype(archetypeName);
    if (!archetype) {
        std::cerr << "[EntityFactory] Unknown archetype: " << archetypeName << "\n";
        return entities;
    }

    for (size_t i = 0; i < count; ++i) {
        auto entity = m_world.add();

        // Add all required components
        for (const auto& adder : archetype->requiredComponents) {
            adder(m_world, entity);
        }

        entities.push_back(entity);
    }

    return entities;
}

// ============================================================================
// Common Relationship Types Implementation
// ============================================================================

namespace Relations {

RelationshipType createPartOf(gaia::ecs::World& world) {
    // Create a tag entity for "PartOf" relationship
    auto tag = world.add();
    return RelationshipType{
        .tag = tag,
        .name = "partof",
        .isExclusive = false  // Entity can be part of multiple things
    };
}

RelationshipType createContains(gaia::ecs::World& world) {
    auto tag = world.add();
    return RelationshipType{
        .tag = tag,
        .name = "contains",
        .isExclusive = false
    };
}

RelationshipType createChildOf(gaia::ecs::World& world) {
    // Use Gaia's built-in ChildOf for hierarchical relationships
    return RelationshipType{
        .tag = gaia::ecs::ChildOf,
        .name = "childof",
        .isExclusive = true  // Entity can only have one parent
    };
}

RelationshipType createCustom(gaia::ecs::World& world, std::string_view name, bool exclusive) {
    auto tag = world.add();
    return RelationshipType{
        .tag = tag,
        .name = std::string(name),
        .isExclusive = exclusive
    };
}

} // namespace Relations

} // namespace GaiaArchetype
