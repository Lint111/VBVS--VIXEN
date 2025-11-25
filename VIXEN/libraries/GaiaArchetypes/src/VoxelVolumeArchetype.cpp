#include "VoxelVolumeArchetype.h"
#include <iostream>
#include <chrono>

namespace GaiaArchetype {

// ============================================================================
// VoxelVolumeArchetype Implementation
// ============================================================================

VoxelVolumeArchetype::VoxelVolumeArchetype(
    gaia::ecs::World& world,
    RelationshipObserver& observer,
    RelationshipTypeRegistry& types)
    : m_world(world)
    , m_observer(observer)
    , m_types(types) {

    // Get or create the "partof" relationship tag
    m_partOfTag = m_types.partOf();

    // Register our internal hooks with the RelationshipObserver
    m_addedCallbackHandle = m_observer.onRelationshipAdded(m_partOfTag,
        [this](const RelationshipObserver::RelationshipContext& ctx) {
            handleVoxelAdded(ctx);
        });

    m_batchAddedCallbackHandle = m_observer.onBatchAdded(m_partOfTag,
        [this](const RelationshipObserver::BatchRelationshipContext& ctx) {
            handleVoxelBatchAdded(ctx);
        });

    m_removedCallbackHandle = m_observer.onRelationshipRemoved(m_partOfTag,
        [this](const RelationshipObserver::RelationshipContext& ctx) {
            handleVoxelRemoved(ctx);
        });

    std::cout << "[VoxelVolumeArchetype] Initialized with PartOf relationship hooks\n";
}

VoxelVolumeArchetype::~VoxelVolumeArchetype() {
    // Unregister our callbacks
    m_observer.unregisterCallback(m_addedCallbackHandle);
    m_observer.unregisterCallback(m_batchAddedCallbackHandle);
    m_observer.unregisterCallback(m_removedCallbackHandle);
}

gaia::ecs::Entity VoxelVolumeArchetype::createVolume(const glm::ivec3& origin) {
    return createVolume(origin, glm::ivec3(64, 64, 64));
}

gaia::ecs::Entity VoxelVolumeArchetype::createVolume(
    const glm::ivec3& origin,
    const glm::ivec3& size) {

    auto entity = m_world.add();

    // Add volume components
    m_world.add<VolumeOrigin>(entity, VolumeOrigin(origin));
    m_world.add<VolumeSize>(entity, VolumeSize{size.x, size.y, size.z});
    m_world.add<VolumeStats>(entity, VolumeStats{});
    m_world.add<VolumeBounds>(entity, VolumeBounds{});

    std::cout << "[VoxelVolumeArchetype] Created volume at ("
              << origin.x << ", " << origin.y << ", " << origin.z << ")\n";

    return entity;
}

bool VoxelVolumeArchetype::addVoxelToVolume(
    gaia::ecs::Entity voxel,
    gaia::ecs::Entity volume) {

    return m_observer.addRelationship(voxel, volume, m_partOfTag);
}

size_t VoxelVolumeArchetype::addVoxelsToVolume(
    std::span<const gaia::ecs::Entity> voxels,
    gaia::ecs::Entity volume) {

    return m_observer.addRelationshipBatch(voxels, volume, m_partOfTag);
}

bool VoxelVolumeArchetype::removeVoxelFromVolume(
    gaia::ecs::Entity voxel,
    gaia::ecs::Entity volume) {

    return m_observer.removeRelationship(voxel, volume, m_partOfTag);
}

size_t VoxelVolumeArchetype::clearVolume(gaia::ecs::Entity volume) {
    auto voxels = getVoxelsInVolume(volume);
    return m_observer.removeRelationshipBatch(voxels, volume, m_partOfTag);
}

std::vector<gaia::ecs::Entity> VoxelVolumeArchetype::getVoxelsInVolume(
    gaia::ecs::Entity volume) const {

    return m_observer.getSourcesFor(volume, m_partOfTag);
}

const VolumeStats* VoxelVolumeArchetype::getVolumeStats(gaia::ecs::Entity volume) const {
    if (!m_world.valid(volume) || !m_world.has<VolumeStats>(volume)) {
        return nullptr;
    }
    return &m_world.get<VolumeStats>(volume);
}

const VolumeBounds* VoxelVolumeArchetype::getVolumeBounds(gaia::ecs::Entity volume) const {
    if (!m_world.valid(volume) || !m_world.has<VolumeBounds>(volume)) {
        return nullptr;
    }
    return &m_world.get<VolumeBounds>(volume);
}

bool VoxelVolumeArchetype::isVoxelInVolume(
    gaia::ecs::Entity voxel,
    gaia::ecs::Entity volume) const {

    return m_observer.hasRelationship(voxel, volume, m_partOfTag);
}

void VoxelVolumeArchetype::setOnVoxelAdded(OnVoxelAddedCallback callback) {
    m_onVoxelAdded = std::move(callback);
}

void VoxelVolumeArchetype::setOnVoxelBatchAdded(OnVoxelBatchAddedCallback callback) {
    m_onVoxelBatchAdded = std::move(callback);
}

void VoxelVolumeArchetype::setOnVoxelRemoved(OnVoxelRemovedCallback callback) {
    m_onVoxelRemoved = std::move(callback);
}

void VoxelVolumeArchetype::setBatchThreshold(size_t threshold) {
    m_observer.setBatchThreshold(threshold);
}

// ============================================================================
// Internal Hook Handlers
// ============================================================================

void VoxelVolumeArchetype::handleVoxelAdded(
    const RelationshipObserver::RelationshipContext& ctx) {

    // Update volume statistics
    updateVolumeStats(ctx.target, +1);

    // Update bounds if voxel has position
    // Note: In real implementation, we'd get position from MortonKey component
    // updateVolumeBounds(ctx.target, voxelPosition);

    // Invoke user callback
    if (m_onVoxelAdded) {
        m_onVoxelAdded(ctx.world, ctx.source, ctx.target);
    }
}

void VoxelVolumeArchetype::handleVoxelBatchAdded(
    const RelationshipObserver::BatchRelationshipContext& ctx) {

    // Update volume statistics with batch count
    updateVolumeStats(ctx.target, static_cast<int>(ctx.sources.size()));

    // Update bounds for all voxels
    // for (auto voxel : ctx.sources) {
    //     updateVolumeBounds(ctx.target, getVoxelPosition(voxel));
    // }

    // Invoke user callback
    if (m_onVoxelBatchAdded) {
        m_onVoxelBatchAdded(ctx.world, ctx.sources, ctx.target);
    }
}

void VoxelVolumeArchetype::handleVoxelRemoved(
    const RelationshipObserver::RelationshipContext& ctx) {

    // Update volume statistics
    updateVolumeStats(ctx.target, -1);

    // Note: Bounds recalculation would be expensive here
    // Better to mark dirty and recalc lazily

    // Invoke user callback
    if (m_onVoxelRemoved) {
        m_onVoxelRemoved(ctx.world, ctx.source, ctx.target);
    }
}

void VoxelVolumeArchetype::updateVolumeStats(gaia::ecs::Entity volume, int voxelDelta) {
    if (!m_world.valid(volume) || !m_world.has<VolumeStats>(volume)) {
        return;
    }

    auto& stats = m_world.set<VolumeStats>(volume);
    stats.voxelCount = static_cast<uint32_t>(
        std::max(0, static_cast<int>(stats.voxelCount) + voxelDelta));
    stats.isDirty = true;
    stats.lastModified = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

void VoxelVolumeArchetype::updateVolumeBounds(
    gaia::ecs::Entity volume,
    const glm::vec3& voxelPos) {

    if (!m_world.valid(volume) || !m_world.has<VolumeBounds>(volume)) {
        return;
    }

    auto& bounds = m_world.set<VolumeBounds>(volume);
    bounds.expand(voxelPos);
}

// ============================================================================
// VoxelVolumeSystem Implementation
// ============================================================================

VoxelVolumeSystem::VoxelVolumeSystem(gaia::ecs::World& world)
    : m_world(world) {
}

void VoxelVolumeSystem::processDirtyVolumes() {
    // Query all volumes that are dirty
    auto query = m_world.query().all<VolumeStats>().all<VolumeOrigin>();

    query.each([this](gaia::ecs::Entity volume, VolumeStats& stats, const VolumeOrigin&) {
        if (stats.isDirty) {
            // Get voxels for this volume
            // Note: This requires the relationship query functionality

            if (m_processCallback) {
                // Would need to query voxels with partof relationship to this volume
                // For now, pass empty span
                std::vector<gaia::ecs::Entity> voxels;
                m_processCallback(m_world, volume, voxels);
            }

            // Mark as clean
            stats.isDirty = false;
        }
    });
}

void VoxelVolumeSystem::setProcessCallback(ProcessVolumeCallback callback) {
    m_processCallback = std::move(callback);
}

} // namespace GaiaArchetype
