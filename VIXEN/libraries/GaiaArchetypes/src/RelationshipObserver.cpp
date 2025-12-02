#include "pch.h"
#include "RelationshipObserver.h"
#include <algorithm>
#include <iostream>

namespace GaiaArchetype {

// ============================================================================
// RelationshipObserver Implementation
// ============================================================================

RelationshipObserver::RelationshipObserver(gaia::ecs::World& world)
    : m_world(world) {
}

size_t RelationshipObserver::onRelationshipAdded(
    gaia::ecs::Entity relationTag,
    OnAddedCallback callback) {

    std::lock_guard<std::mutex> lock(m_mutex);

    CallbackKey key = relationTag.id();
    auto& entries = m_callbacks[key];

    size_t handle = m_nextHandle++;
    entries.push_back(CallbackEntry{
        .handle = handle,
        .onAdded = std::move(callback),
        .onRemoved = nullptr,
        .onBatchAdded = nullptr,
        .onBatchRemoved = nullptr
    });

    return handle;
}

size_t RelationshipObserver::onRelationshipRemoved(
    gaia::ecs::Entity relationTag,
    OnRemovedCallback callback) {

    std::lock_guard<std::mutex> lock(m_mutex);

    CallbackKey key = relationTag.id();
    auto& entries = m_callbacks[key];

    size_t handle = m_nextHandle++;
    entries.push_back(CallbackEntry{
        .handle = handle,
        .onAdded = nullptr,
        .onRemoved = std::move(callback),
        .onBatchAdded = nullptr,
        .onBatchRemoved = nullptr
    });

    return handle;
}

size_t RelationshipObserver::onBatchAdded(
    gaia::ecs::Entity relationTag,
    OnBatchAddedCallback callback) {

    std::lock_guard<std::mutex> lock(m_mutex);

    CallbackKey key = relationTag.id();
    auto& entries = m_callbacks[key];

    size_t handle = m_nextHandle++;
    entries.push_back(CallbackEntry{
        .handle = handle,
        .onAdded = nullptr,
        .onRemoved = nullptr,
        .onBatchAdded = std::move(callback),
        .onBatchRemoved = nullptr
    });

    return handle;
}

size_t RelationshipObserver::onBatchRemoved(
    gaia::ecs::Entity relationTag,
    OnBatchRemovedCallback callback) {

    std::lock_guard<std::mutex> lock(m_mutex);

    CallbackKey key = relationTag.id();
    auto& entries = m_callbacks[key];

    size_t handle = m_nextHandle++;
    entries.push_back(CallbackEntry{
        .handle = handle,
        .onAdded = nullptr,
        .onRemoved = nullptr,
        .onBatchAdded = nullptr,
        .onBatchRemoved = std::move(callback)
    });

    return handle;
}

void RelationshipObserver::unregisterCallback(size_t handle) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& [key, entries] : m_callbacks) {
        entries.erase(
            std::remove_if(entries.begin(), entries.end(),
                [handle](const CallbackEntry& e) { return e.handle == handle; }),
            entries.end()
        );
    }
}

bool RelationshipObserver::addRelationship(
    gaia::ecs::Entity source,
    gaia::ecs::Entity target,
    gaia::ecs::Entity relationTag) {

    if (!m_world.valid(source) || !m_world.valid(target)) {
        return false;
    }

    // Add the relationship in Gaia ECS
    m_world.add(source, gaia::ecs::Pair(relationTag, target));

    // Handle callbacks
    if (m_deferredMode) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deferredOps.push_back(DeferredOp{
            .type = DeferredOp::Type::Add,
            .source = source,
            .target = target,
            .relationTag = relationTag
        });
    } else {
        invokeAddedCallbacks(source, target, relationTag);
    }

    return true;
}

size_t RelationshipObserver::addRelationshipBatch(
    std::span<const gaia::ecs::Entity> sources,
    gaia::ecs::Entity target,
    gaia::ecs::Entity relationTag) {

    if (!m_world.valid(target) || sources.empty()) {
        return 0;
    }

    // Add all relationships
    std::vector<gaia::ecs::Entity> validSources;
    validSources.reserve(sources.size());

    for (auto source : sources) {
        if (m_world.valid(source)) {
            m_world.add(source, gaia::ecs::Pair(relationTag, target));
            validSources.push_back(source);
        }
    }

    if (validSources.empty()) {
        return 0;
    }

    // Invoke callbacks
    if (m_deferredMode) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto source : validSources) {
            m_deferredOps.push_back(DeferredOp{
                .type = DeferredOp::Type::Add,
                .source = source,
                .target = target,
                .relationTag = relationTag
            });
        }
    } else {
        // Use batch callback if available and threshold met
        invokeBatchAddedCallbacks(validSources, target, relationTag);
    }

    return validSources.size();
}

bool RelationshipObserver::removeRelationship(
    gaia::ecs::Entity source,
    gaia::ecs::Entity target,
    gaia::ecs::Entity relationTag) {

    if (!m_world.valid(source) || !m_world.valid(target)) {
        return false;
    }

    // Check if relationship exists
    if (!m_world.has(source, gaia::ecs::Pair(relationTag, target))) {
        return false;
    }

    // Invoke callbacks before removal
    if (m_deferredMode) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deferredOps.push_back(DeferredOp{
            .type = DeferredOp::Type::Remove,
            .source = source,
            .target = target,
            .relationTag = relationTag
        });
    } else {
        invokeRemovedCallbacks(source, target, relationTag);
    }

    // Remove the relationship
    m_world.del(source, gaia::ecs::Pair(relationTag, target));

    return true;
}

size_t RelationshipObserver::removeRelationshipBatch(
    std::span<const gaia::ecs::Entity> sources,
    gaia::ecs::Entity target,
    gaia::ecs::Entity relationTag) {

    if (!m_world.valid(target) || sources.empty()) {
        return 0;
    }

    size_t removed = 0;
    for (auto source : sources) {
        if (removeRelationship(source, target, relationTag)) {
            removed++;
        }
    }

    return removed;
}

bool RelationshipObserver::hasRelationship(
    gaia::ecs::Entity source,
    gaia::ecs::Entity target,
    gaia::ecs::Entity relationTag) const {

    if (!m_world.valid(source) || !m_world.valid(target)) {
        return false;
    }

    return m_world.has(source, gaia::ecs::Pair(relationTag, target));
}

std::vector<gaia::ecs::Entity> RelationshipObserver::getSourcesFor(
    gaia::ecs::Entity target,
    gaia::ecs::Entity relationTag) const {

    std::vector<gaia::ecs::Entity> results;

    if (!m_world.valid(target)) {
        return results;
    }

    // Query entities with Pair(relationTag, target)
    // Note: Gaia wildcard queries - (relationTag, target)
    auto query = m_world.query().all(gaia::ecs::Pair(relationTag, target));

    query.each([&results](gaia::ecs::Entity entity) {
        results.push_back(entity);
    });

    return results;
}

std::vector<gaia::ecs::Entity> RelationshipObserver::getTargetsFor(
    gaia::ecs::Entity source,
    gaia::ecs::Entity relationTag) const {

    std::vector<gaia::ecs::Entity> results;

    if (!m_world.valid(source)) {
        return results;
    }

    // For getting targets, we need to iterate the entity's pairs
    // Gaia provides pair iteration via targets() or similar
    // This is a simplified version - full implementation would use Gaia's pair API

    // Note: This is a limitation - Gaia's API for querying "all targets of source's relationships"
    // would require iterating all possible targets or maintaining our own index

    return results;
}

size_t RelationshipObserver::countRelationships(
    gaia::ecs::Entity source,
    gaia::ecs::Entity relationTag) const {

    if (!m_world.valid(source)) {
        return 0;
    }

    // Count relationships of this type from source
    // This would need Gaia's pair iteration API
    return 0; // Placeholder
}

void RelationshipObserver::flush() {
    std::vector<DeferredOp> ops;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ops = std::move(m_deferredOps);
        m_deferredOps.clear();
    }

    // Group operations by type and target for batch processing
    std::unordered_map<uint64_t, std::vector<gaia::ecs::Entity>> addsByTarget;
    std::unordered_map<uint64_t, std::vector<gaia::ecs::Entity>> removesByTarget;

    for (const auto& op : ops) {
        uint64_t key = (static_cast<uint64_t>(op.target.id()) << 32) | op.relationTag.id();

        if (op.type == DeferredOp::Type::Add) {
            addsByTarget[key].push_back(op.source);
        } else {
            removesByTarget[key].push_back(op.source);
        }
    }

    // Process batched adds
    for (auto& [key, sources] : addsByTarget) {
        if (sources.empty()) continue;

        // Extract target and relationTag from key
        // Find the original op to get target/relationTag
        for (const auto& op : ops) {
            uint64_t opKey = (static_cast<uint64_t>(op.target.id()) << 32) | op.relationTag.id();
            if (opKey == key && op.type == DeferredOp::Type::Add) {
                invokeBatchAddedCallbacks(sources, op.target, op.relationTag);
                break;
            }
        }
    }

    // Process batched removes (individual callbacks for now)
    for (const auto& op : ops) {
        if (op.type == DeferredOp::Type::Remove) {
            invokeRemovedCallbacks(op.source, op.target, op.relationTag);
        }
    }
}

void RelationshipObserver::invokeAddedCallbacks(
    gaia::ecs::Entity source,
    gaia::ecs::Entity target,
    gaia::ecs::Entity relationTag) {

    std::lock_guard<std::mutex> lock(m_mutex);

    CallbackKey key = relationTag.id();
    auto it = m_callbacks.find(key);
    if (it == m_callbacks.end()) return;

    RelationshipContext ctx{
        .world = m_world,
        .source = source,
        .target = target,
        .relationTag = relationTag
    };

    for (const auto& entry : it->second) {
        if (entry.onAdded) {
            entry.onAdded(ctx);
        }
    }
}

void RelationshipObserver::invokeRemovedCallbacks(
    gaia::ecs::Entity source,
    gaia::ecs::Entity target,
    gaia::ecs::Entity relationTag) {

    std::lock_guard<std::mutex> lock(m_mutex);

    CallbackKey key = relationTag.id();
    auto it = m_callbacks.find(key);
    if (it == m_callbacks.end()) return;

    RelationshipContext ctx{
        .world = m_world,
        .source = source,
        .target = target,
        .relationTag = relationTag
    };

    for (const auto& entry : it->second) {
        if (entry.onRemoved) {
            entry.onRemoved(ctx);
        }
    }
}

void RelationshipObserver::invokeBatchAddedCallbacks(
    std::span<const gaia::ecs::Entity> sources,
    gaia::ecs::Entity target,
    gaia::ecs::Entity relationTag) {

    std::lock_guard<std::mutex> lock(m_mutex);

    CallbackKey key = relationTag.id();
    auto it = m_callbacks.find(key);
    if (it == m_callbacks.end()) return;

    // Check if batch callback exists and threshold is met
    bool useBatch = sources.size() >= m_batchThreshold;

    if (useBatch) {
        BatchRelationshipContext ctx{
            .world = m_world,
            .sources = sources,
            .target = target,
            .relationTag = relationTag
        };

        bool batchHandled = false;
        for (const auto& entry : it->second) {
            if (entry.onBatchAdded) {
                entry.onBatchAdded(ctx);
                batchHandled = true;
            }
        }

        // Fall back to individual callbacks if no batch handler
        if (!batchHandled) {
            useBatch = false;
        }
    }

    // Use individual callbacks
    if (!useBatch) {
        for (auto source : sources) {
            RelationshipContext ctx{
                .world = m_world,
                .source = source,
                .target = target,
                .relationTag = relationTag
            };

            for (const auto& entry : it->second) {
                if (entry.onAdded) {
                    entry.onAdded(ctx);
                }
            }
        }
    }
}

// ============================================================================
// RelationshipTypeRegistry Implementation
// ============================================================================

RelationshipTypeRegistry::RelationshipTypeRegistry(gaia::ecs::World& world)
    : m_world(world) {
}

gaia::ecs::Entity RelationshipTypeRegistry::getOrCreate(std::string_view name) {
    std::string nameStr(name);

    auto it = m_nameToTag.find(nameStr);
    if (it != m_nameToTag.end()) {
        return it->second;
    }

    // Create new tag entity
    auto tag = m_world.add();

    m_nameToTag[nameStr] = tag;
    m_tagToName[tag.id()] = nameStr;

    return tag;
}

std::optional<gaia::ecs::Entity> RelationshipTypeRegistry::get(std::string_view name) const {
    auto it = m_nameToTag.find(std::string(name));
    if (it != m_nameToTag.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool RelationshipTypeRegistry::exists(std::string_view name) const {
    return m_nameToTag.contains(std::string(name));
}

std::optional<std::string_view> RelationshipTypeRegistry::getName(gaia::ecs::Entity tag) const {
    auto it = m_tagToName.find(tag.id());
    if (it != m_tagToName.end()) {
        return it->second;
    }
    return std::nullopt;
}

gaia::ecs::Entity RelationshipTypeRegistry::partOf() {
    return getOrCreate("partof");
}

gaia::ecs::Entity RelationshipTypeRegistry::contains() {
    return getOrCreate("contains");
}

gaia::ecs::Entity RelationshipTypeRegistry::childOf() {
    // Use Gaia's built-in ChildOf for hierarchical relationships
    return gaia::ecs::ChildOf;
}

} // namespace GaiaArchetype
