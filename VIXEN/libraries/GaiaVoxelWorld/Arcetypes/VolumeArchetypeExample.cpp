#include "pch.h"

/**
 * Complete Example: VolumeArchetype with ArchetypeBuilder
 *
 * Demonstrates the Gaia pattern for relationship hooks:
 * 1. Create relationship entity (singleton)
 * 2. Create target entity (volume)
 * 3. Register ComponentCacheItem for Pair(relation, target)
 * 4. Register hook on that cache item
 * 5. Add relationship to source entity → hook fires!
 */

#include "ArchetypeBuilder.h"
#include "VoxelComponents.h"
#include <gaia.h>

using namespace gaia::ecs;
using namespace GaiaVoxel;

void completeExample() {
    World world;

    // ========================================================================
    // Step 1: Create Relationship Entity (Singleton)
    // ========================================================================
    Entity partOfRelation = world.add();
    world.name(partOfRelation, "PartOf");

    // ========================================================================
    // Step 2: Create Volume with ArchetypeBuilder + Hooks
    // ========================================================================
    Entity volumeEntity = ArchetypeBuilder(world)
        .add<AABB>()
        .add<Volume>(Volume{0.1f})  // 10cm voxels
        .add<VolumeTransform>()
        .onRelationshipAdded(partOfRelation, [](Entity voxel, Entity volume, World& w) {
            // This callback fires when: world.add(voxel, Pair(partOfRelation, volume))

            std::cout << "[Hook] Voxel " << voxel.value() << " added to volume " << volume.value() << "\n";

            // Get Morton key from voxel
            auto* mortonKey = w.get<MortonKey>(voxel);
            if (!mortonKey) {
                std::cout << "[Hook] No MortonKey on voxel, skipping AABB expansion\n";
                return;
            }

            // Expand volume's AABB
            auto* aabb = w.get_mut<AABB>(volume);
            if (!aabb) {
                std::cout << "[Hook] No AABB on volume!\n";
                return;
            }

            glm::vec3 worldPos = MortonKeyUtils::toWorldPos(*mortonKey);
            aabb->expandToContain(worldPos);

            std::cout << "[Hook] AABB expanded to contain " << worldPos.x << "," << worldPos.y << "," << worldPos.z << "\n";

            // Update VolumeTransform
            auto* volume_comp = w.get<Volume>(volume);
            if (volume_comp && aabb->isInitialized()) {
                VolumeTransform transform = VolumeTransform::fromWorldBounds(aabb->min, aabb->max);

                if (w.has<VolumeTransform>(volume)) {
                    *w.get_mut<VolumeTransform>(volume) = transform;
                } else {
                    w.add<VolumeTransform>(volume, transform);
                }

                std::cout << "[Hook] VolumeTransform updated. Bounds: ["
                          << aabb->min.x << "," << aabb->min.y << "," << aabb->min.z << "] → ["
                          << aabb->max.x << "," << aabb->max.y << "," << aabb->max.z << "]\n";
            }
        })
        .onCreate([](Entity e, World& w) {
            std::cout << "[Archetype] Volume created: " << e.value() << "\n";
        })
        .build();

    // At this point:
    // - volumeEntity exists with AABB, Volume, VolumeTransform components
    // - ComponentCacheItem created for Pair(partOfRelation, volumeEntity)
    // - Hook registered in ComponentCache

    // ========================================================================
    // Step 3: Create Voxel and Add Relationship → Hook Fires!
    // ========================================================================
    std::cout << "\n=== Adding voxel to volume ===\n";

    Entity voxel1 = world.add();
    world.add<MortonKey>(voxel1, MortonKey{1234});

    // THIS TRIGGERS THE HOOK!
    world.add(voxel1, Pair(partOfRelation, volumeEntity));
    // → ComponentCache looks up Pair(partOfRelation, volumeEntity)
    // → Finds our registered hook
    // → Fires callback with (voxel1, volumeEntity, world)
    // → AABB expands, VolumeTransform updates

    // ========================================================================
    // Step 4: Add More Voxels - Hooks Keep Firing
    // ========================================================================
    std::cout << "\n=== Adding 5 more voxels ===\n";

    for (uint64_t i = 100; i < 105; ++i) {
        Entity voxel = world.add();
        world.add<MortonKey>(voxel, MortonKey{i});
        world.add(voxel, Pair(partOfRelation, volumeEntity));  // Hook fires each time!
    }

    // ========================================================================
    // Step 5: Query Results
    // ========================================================================
    std::cout << "\n=== Final Volume State ===\n";

    auto* finalAABB = world.get<AABB>(volumeEntity);
    if (finalAABB && finalAABB->isInitialized()) {
        std::cout << "AABB: [" << finalAABB->min.x << "," << finalAABB->min.y << "," << finalAABB->min.z << "] → ["
                  << finalAABB->max.x << "," << finalAABB->max.y << "," << finalAABB->max.z << "]\n";
    }

    auto* finalVolume = world.get<Volume>(volumeEntity);
    if (finalVolume && finalAABB) {
        int requiredDepth = finalVolume->getRequiredDepth(*finalAABB);
        std::cout << "Required octree depth: " << requiredDepth << " (voxelSize=" << finalVolume->voxelSize << ")\n";
    }

    // ========================================================================
    // Step 6: Create Second Volume - Independent Hooks
    // ========================================================================
    std::cout << "\n=== Creating second volume ===\n";

    Entity volumeEntity2 = ArchetypeBuilder(world)
        .add<AABB>()
        .add<Volume>(Volume{0.05f})  // 5cm voxels (finer resolution)
        .add<VolumeTransform>()
        .onRelationshipAdded(partOfRelation, [](Entity voxel, Entity volume, World& w) {
            std::cout << "[Hook Volume2] Voxel " << voxel.value() << " added to second volume!\n";
            // Same logic as volume1, but operates independently
        })
        .build();

    // Add voxel to volume2 - only volume2's hook fires
    Entity voxel2 = world.add();
    world.add<MortonKey>(voxel2, MortonKey{9999});
    world.add(voxel2, Pair(partOfRelation, volumeEntity2));  // Only volume2's hook fires!

    std::cout << "\n=== Example Complete ===\n";
}

/**
 * Key Insight: Gaia ComponentCache Pattern
 *
 * The magic happens here:
 *
 * 1. const ComponentCacheItem& pairItem = world.add(Pair(relation, target));
 *    - Creates cache entry for this SPECIFIC pair
 *    - Each (relation, target) combination gets its own cache item
 *
 * 2. ComponentCache::hooks(pairItem).func_add = [target](World& w, ..., Entity source) { ... };
 *    - Registers hook on THAT SPECIFIC pair cache item
 *    - Captures 'target' entity in closure
 *
 * 3. world.add(source, Pair(relation, target));
 *    - Gaia looks up ComponentCacheItem for Pair(relation, target)
 *    - Finds our hook and fires it
 *    - Passes 'source' entity as parameter
 *
 * Result:
 * - Each volume has independent hooks
 * - Voxel added to volumeA → only volumeA's hook fires
 * - Voxel added to volumeB → only volumeB's hook fires
 * - Clean, scalable, follows Gaia's design perfectly
 */
