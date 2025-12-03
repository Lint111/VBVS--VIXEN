#pragma once

#include "VoxelComponents.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gaia.h>
#include "GaiaVoxelWorld.h"

using namespace gaia::ecs;

namespace GaiaVoxel {


/**
 * VolumeArc - factory for creating volume entities.
 *
 * Creates entity with:
 * - AABB (bounds, initially uninitialized)
 * - Volume (voxelSize metadata)
 * - VolumeTransform (auto-updated when AABB expands)
 *
 * Usage:
 *   Entity volumeEntity = VolumeArc::create(world, 0.1f);  // 10cm voxels
 *
 *   // Add voxel to volume
 *   Entity voxelEntity = world.add();
 *   world.add<MortonKey>(voxelEntity, MortonKey{mortonCode});
 *   world.add(voxelEntity, Pair(VolumeContains::entity(world), volumeEntity));
 *   // → Hook auto-expands AABB and updates transform
 */
class VolumeArc {
public:
    static Entity Create(GaiaVoxelWorld& w) {
        Entity volumeEntity = w.getWorld().add();

        EntityBuilder builder = w.getWorld().build(volumeEntity);
        builder.add<AABB>()          // Spatial bounds (initially empty)
               .add<Volume>()  // Resolution metadata
               .add<VolumeTransform>();  // Transform (will be updated by hook)
        builder.commit();

        return volumeEntity;
    }

    // Register hook for when MortonKey is added to entity with VolumeContains relationship
    static void registerHooks(GaiaVoxelWorld& w) {


        const ComponentCacheItem& partof_relatioship = w.getWorld().add<Pair(w.Relationshions[Relation::PartOf],Create(w)>());
        ComponentCache::hooks(volume_contains_entity_item).func_add = [](const World& world, const ComponentCacheItem& item, Entity voxelEntity) {
            // This function is called when a MortonKey component is added to an entity

            // Check if entity has VolumeContains relationship
            // We need to find if there's a Pair(volumeContainsEntity, volumeEntity) on this voxelEntity

            // Iterate through all volume entities and check if this voxel has relationship to them
           if(!world.has<MortonKey>(voxelEntity)) return;


        };
    }

    // Helper: add voxel entity to volume (establishes relationship)
    static void addVoxel(World& world, Entity volumeEntity, Entity voxelEntity) {
        // Add VolumeContains relationship: voxelEntity → volumeEntity
        // Triggers registered hook which expands AABB and updates transform
        world.add(voxelEntity, Pair(VolumeContains::entity(world), volumeEntity));
    }

    // Helper: create voxel and add to volume in one call
    static Entity createVoxel(World& world, Entity volumeEntity, const MortonKey& key) {
        Entity voxelEntity = world.add();
        world.add<MortonKey>(voxelEntity, key);
        addVoxel(world, volumeEntity, voxelEntity);
        return voxelEntity;
    }
};

} // namespace GaiaVoxel