#pragma once

#include "RelationshipRegistry.h"
#include "ArchetypeBuilder.h"
#include <gaia.h>

using namespace gaia::ecs;

namespace GaiaVoxel {

/**
 * WorldContext - high-level facade for ECS world with relationship management.
 *
 * Solves:
 * 1. Centralized relationship registry initialization
 * 2. Unified API for entity/relationship operations
 * 3. Hook registration in one place
 * 4. Complex multi-entity structure creation
 *
 * Usage:
 *   WorldContext ctx(world);
 *   ctx.initialize();  // Sets up relationships and hooks
 *
 *   // Simple entity creation
 *   Entity volume = ctx.createVolume(0.1f);
 *   Entity voxel = ctx.createVoxel(volume, mortonKey);
 *
 *   // Complex structure creation
 *   auto structure = ctx.createVoxelOctree(voxelSize, maxDepth);
 */
class WorldContext {
public:
    WorldContext(World& world)
        : m_world(world), m_registry(world) {}

    // Initialize relationships and register all hooks
    void initialize() {
        m_registry.initialize();
        registerHooks();
    }

    // Relationship registry accessor
    RelationshipRegistry& relationships() { return m_registry; }
    const RelationshipRegistry& relationships() const { return m_registry; }

    // Archetype factory methods
    Entity createVolume(float voxelSize = 1.0f) {
        return Archetypes::createVolume(m_world, m_registry, voxelSize);
    }

    Entity createVoxel(Entity volumeEntity, const MortonKey& key) {
        return Archetypes::createVoxel(m_world, m_registry, volumeEntity, key);
    }

    Entity createVoxelWithAttributes(Entity volumeEntity, const MortonKey& key, float density, const glm::vec3& color) {
        return Archetypes::createVoxelWithAttributes(m_world, m_registry, volumeEntity, key, density, color);
    }

    // Builder for custom archetypes
    ArchetypeBuilder build() {
        return ArchetypeBuilder(m_world, m_registry);
    }

    // Complex structure: Voxel Octree (volume + multiple voxels)
    struct VoxelOctreeStructure {
        Entity volumeEntity;
        std::vector<Entity> voxelEntities;
    };

    VoxelOctreeStructure createVoxelOctree(float voxelSize, const std::vector<MortonKey>& voxelKeys) {
        VoxelOctreeStructure structure;

        // Create volume
        structure.volumeEntity = createVolume(voxelSize);

        // Create all voxels and link to volume
        for (const auto& key : voxelKeys) {
            Entity voxelEntity = createVoxel(structure.volumeEntity, key);
            structure.voxelEntities.push_back(voxelEntity);
        }

        return structure;
    }

    // Complex structure: Octree with spatial hierarchy
    struct HierarchicalOctree {
        Entity rootVolume;
        std::vector<Entity> childVolumes;  // LOD levels
        std::unordered_map<Entity, std::vector<Entity>> volumeToVoxels;
    };

    HierarchicalOctree createHierarchicalOctree(float baseVoxelSize, int lodLevels) {
        HierarchicalOctree structure;

        // Create root volume (finest resolution)
        structure.rootVolume = createVolume(baseVoxelSize);

        // Create LOD levels (coarser resolutions)
        for (int lod = 1; lod < lodLevels; ++lod) {
            float lodVoxelSize = baseVoxelSize * (1 << lod);  // Double size per LOD
            Entity lodVolume = createVolume(lodVoxelSize);
            structure.childVolumes.push_back(lodVolume);

            // Establish parent-child relationship
            m_registry.addRelationship(RelationshipRegistry::RelationType::ChildOf, lodVolume, structure.rootVolume);
        }

        return structure;
    }

private:
    void registerHooks() {
        // Hook: when voxel with MortonKey linked to volume, expand volume's AABB
        m_registry.onRelationshipAdded(
            RelationshipRegistry::RelationType::VolumeContains,
            [](World& world, Entity voxelEntity, Entity volumeEntity) {
                // Get Morton key from voxel
                auto* mortonKey = world.get<MortonKey>(voxelEntity);
                if (!mortonKey) return;

                // Get volume's AABB
                auto* aabb = world.get_mut<AABB>(volumeEntity);
                if (!aabb) return;

                // Decode Morton key to world position
                glm::vec3 worldPos = MortonKeyUtils::toWorldPos(*mortonKey);

                // Expand AABB
                aabb->expandToContain(worldPos);

                // Update VolumeTransform
                auto* volume = world.get<Volume>(volumeEntity);
                if (volume && aabb->isInitialized()) {
                    VolumeTransform transform = VolumeTransform::fromWorldBounds(aabb->min, aabb->max);

                    if (world.has<VolumeTransform>(volumeEntity)) {
                        *world.get_mut<VolumeTransform>(volumeEntity) = transform;
                    } else {
                        world.add<VolumeTransform>(volumeEntity, transform);
                    }

                    #ifndef NDEBUG
                    std::cout << "[WorldContext Hook] Volume AABB expanded. Morton=" << mortonKey->code
                              << " Bounds: [" << aabb->min.x << "," << aabb->min.y << "," << aabb->min.z << "] → ["
                              << aabb->max.x << "," << aabb->max.y << "," << aabb->max.z << "]\n";
                    #endif
                }
            }
        );

        // Additional hooks for other relationships...
    }

    World& m_world;
    RelationshipRegistry m_registry;
};

} // namespace GaiaVoxel
