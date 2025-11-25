#pragma once

#include "ArchetypeBuilder.h"
#include "RelationshipObserver.h"
#include <gaia.h>
#include <glm/glm.hpp>
#include <vector>
#include <span>
#include <functional>

namespace GaiaArchetype {

// ============================================================================
// Voxel Volume Archetype - Example of relationship hooks in action
// ============================================================================

/**
 * VoxelVolumeArchetype - Demonstrates the relationship hook system.
 *
 * A VoxelVolume is a spatial container that accepts voxels via the "partof" relationship.
 * When voxels are added (individually or in batches), hooks are triggered to:
 * - Update spatial indices
 * - Rebuild octree structures
 * - Notify rendering systems
 *
 * USAGE:
 *
 *   // Setup
 *   gaia::ecs::World world;
 *   RelationshipObserver observer(world);
 *   RelationshipTypeRegistry types(world);
 *
 *   VoxelVolumeArchetype volumeArchetype(world, observer, types);
 *
 *   // Create a volume
 *   auto volume = volumeArchetype.createVolume(glm::ivec3(0, 0, 0));
 *
 *   // Create voxels
 *   auto voxel1 = world.add();
 *   world.add<MortonKey>(voxel1, MortonKey{...});
 *   world.add<Density>(voxel1, Density{1.0f});
 *
 *   // Add voxel to volume - triggers hook!
 *   volumeArchetype.addVoxelToVolume(voxel1, volume);
 *
 *   // Or batch add - triggers bundle hook for efficiency!
 *   std::vector<gaia::ecs::Entity> voxels = {...};
 *   volumeArchetype.addVoxelsToVolume(voxels, volume);
 */

// ============================================================================
// Volume-Specific Components
// ============================================================================

/**
 * VolumeOrigin - World-space origin of the volume.
 */
struct VolumeOrigin {
    static constexpr const char* Name = "volume_origin";
    int x = 0;
    int y = 0;
    int z = 0;

    VolumeOrigin() = default;
    VolumeOrigin(const glm::ivec3& v) : x(v.x), y(v.y), z(v.z) {}
    operator glm::ivec3() const { return glm::ivec3(x, y, z); }
};

/**
 * VolumeSize - Dimensions of the volume in voxels.
 */
struct VolumeSize {
    static constexpr const char* Name = "volume_size";
    int width = 64;
    int height = 64;
    int depth = 64;
};

/**
 * VolumeStats - Statistics about the volume's contents.
 * Updated by relationship hooks when voxels are added/removed.
 */
struct VolumeStats {
    static constexpr const char* Name = "volume_stats";
    uint32_t voxelCount = 0;
    uint32_t solidCount = 0;
    bool isDirty = false;       // True if octree needs rebuild
    uint64_t lastModified = 0;  // Timestamp of last modification
};

/**
 * VolumeBounds - Computed AABB of volume contents.
 */
struct VolumeBounds {
    static constexpr const char* Name = "volume_bounds";
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 0, maxY = 0, maxZ = 0;

    bool isValid() const { return maxX > minX || maxY > minY || maxZ > minZ; }

    void expand(const glm::vec3& point) {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        minZ = std::min(minZ, point.z);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
        maxZ = std::max(maxZ, point.z);
    }
};

// ============================================================================
// VoxelVolumeArchetype Class
// ============================================================================

class VoxelVolumeArchetype {
public:
    // ========================================================================
    // User-definable callbacks for volume events
    // ========================================================================

    /**
     * Callback when a single voxel is added to a volume.
     * Called AFTER the relationship is established.
     */
    using OnVoxelAddedCallback = std::function<void(
        gaia::ecs::World& world,
        gaia::ecs::Entity voxel,
        gaia::ecs::Entity volume
    )>;

    /**
     * Callback when voxels are added in batch.
     * More efficient than per-voxel callback for large additions.
     */
    using OnVoxelBatchAddedCallback = std::function<void(
        gaia::ecs::World& world,
        std::span<const gaia::ecs::Entity> voxels,
        gaia::ecs::Entity volume
    )>;

    /**
     * Callback when a voxel is removed from a volume.
     */
    using OnVoxelRemovedCallback = std::function<void(
        gaia::ecs::World& world,
        gaia::ecs::Entity voxel,
        gaia::ecs::Entity volume
    )>;

    // ========================================================================
    // Construction
    // ========================================================================

    VoxelVolumeArchetype(
        gaia::ecs::World& world,
        RelationshipObserver& observer,
        RelationshipTypeRegistry& types);

    ~VoxelVolumeArchetype();

    // ========================================================================
    // Volume Creation
    // ========================================================================

    /**
     * Create a new volume entity at the given origin.
     */
    gaia::ecs::Entity createVolume(const glm::ivec3& origin);

    /**
     * Create a new volume with specific size.
     */
    gaia::ecs::Entity createVolume(const glm::ivec3& origin, const glm::ivec3& size);

    // ========================================================================
    // Voxel-Volume Relationship Management
    // ========================================================================

    /**
     * Add a single voxel to a volume.
     * Triggers onVoxelAdded callback.
     */
    bool addVoxelToVolume(gaia::ecs::Entity voxel, gaia::ecs::Entity volume);

    /**
     * Add multiple voxels to a volume in batch.
     * Triggers onVoxelBatchAdded callback if count >= batchThreshold.
     */
    size_t addVoxelsToVolume(
        std::span<const gaia::ecs::Entity> voxels,
        gaia::ecs::Entity volume);

    /**
     * Remove a voxel from a volume.
     * Triggers onVoxelRemoved callback.
     */
    bool removeVoxelFromVolume(gaia::ecs::Entity voxel, gaia::ecs::Entity volume);

    /**
     * Remove all voxels from a volume.
     */
    size_t clearVolume(gaia::ecs::Entity volume);

    // ========================================================================
    // Volume Queries
    // ========================================================================

    /**
     * Get all voxels in a volume.
     */
    std::vector<gaia::ecs::Entity> getVoxelsInVolume(gaia::ecs::Entity volume) const;

    /**
     * Get volume statistics.
     */
    const VolumeStats* getVolumeStats(gaia::ecs::Entity volume) const;

    /**
     * Get volume bounds.
     */
    const VolumeBounds* getVolumeBounds(gaia::ecs::Entity volume) const;

    /**
     * Check if a voxel is in a volume.
     */
    bool isVoxelInVolume(gaia::ecs::Entity voxel, gaia::ecs::Entity volume) const;

    // ========================================================================
    // Callback Registration
    // ========================================================================

    /**
     * Set callback for when individual voxels are added.
     */
    void setOnVoxelAdded(OnVoxelAddedCallback callback);

    /**
     * Set callback for when voxels are added in batch.
     */
    void setOnVoxelBatchAdded(OnVoxelBatchAddedCallback callback);

    /**
     * Set callback for when voxels are removed.
     */
    void setOnVoxelRemoved(OnVoxelRemovedCallback callback);

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * Set the threshold for batch callback usage.
     * When adding >= threshold voxels, batch callback is used.
     */
    void setBatchThreshold(size_t threshold);

    /**
     * Get the "partof" relationship tag.
     */
    gaia::ecs::Entity getPartOfTag() const { return m_partOfTag; }

private:
    // ========================================================================
    // Internal Hooks (connected to RelationshipObserver)
    // ========================================================================

    void handleVoxelAdded(const RelationshipObserver::RelationshipContext& ctx);
    void handleVoxelBatchAdded(const RelationshipObserver::BatchRelationshipContext& ctx);
    void handleVoxelRemoved(const RelationshipObserver::RelationshipContext& ctx);

    /**
     * Update volume statistics after voxel changes.
     */
    void updateVolumeStats(gaia::ecs::Entity volume, int voxelDelta);

    /**
     * Update volume bounds with a new voxel position.
     */
    void updateVolumeBounds(gaia::ecs::Entity volume, const glm::vec3& voxelPos);

    // ========================================================================
    // Member Data
    // ========================================================================

    gaia::ecs::World& m_world;
    RelationshipObserver& m_observer;
    RelationshipTypeRegistry& m_types;

    // Relationship tag for "partof"
    gaia::ecs::Entity m_partOfTag;

    // Callback handles (for cleanup)
    size_t m_addedCallbackHandle = 0;
    size_t m_batchAddedCallbackHandle = 0;
    size_t m_removedCallbackHandle = 0;

    // User callbacks
    OnVoxelAddedCallback m_onVoxelAdded;
    OnVoxelBatchAddedCallback m_onVoxelBatchAdded;
    OnVoxelRemovedCallback m_onVoxelRemoved;
};

// ============================================================================
// Helper: VoxelVolumeSystem - ECS System for processing volumes
// ============================================================================

/**
 * VoxelVolumeSystem - Example ECS system that processes dirty volumes.
 *
 * This demonstrates how relationship hooks can mark volumes dirty,
 * and a system can process them efficiently.
 */
class VoxelVolumeSystem {
public:
    explicit VoxelVolumeSystem(gaia::ecs::World& world);

    /**
     * Process all dirty volumes (e.g., rebuild octrees).
     * Call this in your main loop after voxel modifications.
     */
    void processDirtyVolumes();

    /**
     * Set callback for when a volume needs processing.
     */
    using ProcessVolumeCallback = std::function<void(
        gaia::ecs::World& world,
        gaia::ecs::Entity volume,
        std::span<const gaia::ecs::Entity> voxels
    )>;

    void setProcessCallback(ProcessVolumeCallback callback);

private:
    gaia::ecs::World& m_world;
    ProcessVolumeCallback m_processCallback;
};

} // namespace GaiaArchetype
