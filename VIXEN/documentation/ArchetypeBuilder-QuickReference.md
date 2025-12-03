# ArchetypeBuilder Quick Reference

## Gaia-ECS Pair Access Methods

### Direct Pair Extraction (O(1) - Fast)

```cpp
// Get first target from Pair(relation, *)
Entity target = world.target(sourceEntity, relationEntity);

// Get all targets from Pair(relation, *)
world.targets(sourceEntity, relationEntity, [](Entity target) {
    // Process each target
});

// Get first relation from Pair(*, target)
Entity relation = world.relation(sourceEntity, targetEntity);

// Get all relations from Pair(*, target)
world.relations(sourceEntity, targetEntity, [](Entity relation) {
    // Process each relation
});

// Check if pair exists
bool exists = world.has(sourceEntity, Pair(relationEntity, targetEntity));
```

### Query-Based Access (O(N) - Slow)

```cpp
// Find all entities with Pair(relation, target)
auto query = world.query().all(Pair(relationEntity, targetEntity));
query.each([](Entity e) {
    // Process entity e that has this pair
});

// Find all entities with Pair(relation, *)
auto query = world.query().all(Pair(relationEntity, All));

// Find all entities with Pair(*, target)
auto query = world.query().all(Pair(All, targetEntity));
```

---

## RelationshipRegistry Usage

### Setup
```cpp
World world;
RelationshipRegistry registry(world);
registry.initialize();  // Creates relationship entities
```

### Add/Remove Relationships
```cpp
// Add relationship: voxel is contained in volume
registry.addRelationship(VolumeContains, voxel, volume);
// → Calls: world.add(voxel, Pair(volumeContainsEntity, volume))

// Remove relationship
registry.removeRelationship(VolumeContains, voxel, volume);
// → Calls: world.del(voxel, Pair(volumeContainsEntity, volume))
```

### Query Relationships (Fast)
```cpp
// Get volume containing voxel (O(1))
Entity volume = registry.getRelationshipTarget(VolumeContains, voxel);

// Get all volumes containing voxel (O(1) per result)
std::vector<Entity> volumes = registry.getRelationshipTargets(VolumeContains, voxel);

// Check if voxel is in volume (O(1))
bool contained = registry.hasRelationship(VolumeContains, voxel, volume);
```

### Query Relationships (Slow - Inverse)
```cpp
// Get all voxels in volume (O(N) - iterates all entities)
std::vector<Entity> voxels = registry.findAllWithRelationship(VolumeContains, volume);
```

---

## ArchetypeBuilder Usage

### Basic Pattern
```cpp
Entity volume = ArchetypeBuilder(world, registry)
    .add<AABB>()                    // Add component (default)
    .add<Volume>(Volume{0.1f})      // Add component (with value)
    .onCreate([](Entity e, World& w) {
        std::cout << "Created: " << e.value() << "\n";
    })
    .build();
```

### With Per-Entity Hooks
```cpp
Entity volume = ArchetypeBuilder(world, registry)
    .add<AABB>()
    .onRelationshipAdded(VolumeContains, [](Entity voxel, Entity vol, World& w) {
        // Hook fires when ANY voxel added to THIS volume
        auto* aabb = w.get_mut<AABB>(vol);
        auto* mortonKey = w.get<MortonKey>(voxel);
        if (aabb && mortonKey) {
            glm::vec3 pos = MortonKeyUtils::toWorldPos(*mortonKey);
            aabb->expandToContain(pos);
        }
    })
    .build();
```

### With Deferred Relationships
```cpp
Entity voxel = ArchetypeBuilder(world, registry)
    .add<MortonKey>(key)
    .add<Density>(Density{0.8f})
    .relateTo(VolumeContains, volumeEntity)  // Relationship added during build()
    .onCreate([](Entity e, World& w) {
        std::cout << "Voxel created and linked\n";
    })
    .build();
// → Relationship added after commit, hooks fire automatically
```

### With Add + Remove Hooks
```cpp
Entity volume = ArchetypeBuilder(world, registry)
    .add<AABB>()
    .onRelationship(
        VolumeContains,
        // Add hook
        [](Entity voxel, Entity vol, World& w) {
            std::cout << "Voxel added\n";
        },
        // Remove hook
        [](Entity voxel, Entity vol, World& w) {
            std::cout << "Voxel removed\n";
        }
    )
    .build();
```

---

## WorldContext Usage

### Setup
```cpp
WorldContext ctx(world);
ctx.initialize();  // Initializes registry + registers global hooks
```

### Factory Methods
```cpp
// Simple volume
Entity volume = ctx.createVolume(0.1f);

// Voxel with link
Entity voxel = ctx.createVoxel(volume, mortonKey);

// Voxel with attributes
Entity voxel = ctx.createVoxelWithAttributes(volume, key, 0.8f, glm::vec3(1, 0, 0));
```

### Complex Structures
```cpp
// Octree: volume + multiple voxels
auto octree = ctx.createVoxelOctree(0.1f, voxelKeys);
// octree.volumeEntity
// octree.voxelEntities

// Hierarchical octree with LOD
auto lodOctree = ctx.createHierarchicalOctree(0.1f, 4);
// lodOctree.rootVolume
// lodOctree.childVolumes (3 LOD levels)
```

### Custom Archetype
```cpp
Entity custom = ctx.build()
    .add<MortonKey>(key)
    .add<Color>(Color{glm::vec3(1, 0, 0)})
    .add<Emission>(Emission{glm::vec3(5, 1, 0)})
    .relateTo(VolumeContains, volume)
    .build();
```

---

## Hook Execution Flow

### Per-Entity Hook (ArchetypeBuilder)
```cpp
// 1. Create volume with hook
Entity volumeA = ArchetypeBuilder(world, registry)
    .add<AABB>()
    .onRelationshipAdded(VolumeContains, hookA)  // Hook for volumeA
    .build();

Entity volumeB = ArchetypeBuilder(world, registry)
    .add<AABB>()
    .onRelationshipAdded(VolumeContains, hookB)  // Hook for volumeB
    .build();

// 2. Behind the scenes
// - ComponentCacheItem pairA = world.add(Pair(VolumeContains, volumeA))
// - ComponentCache::hooks(pairA).func_add = hookA
// - ComponentCacheItem pairB = world.add(Pair(VolumeContains, volumeB))
// - ComponentCache::hooks(pairB).func_add = hookB

// 3. Add relationships
registry.addRelationship(VolumeContains, voxel1, volumeA);  // → hookA fires (NOT hookB)
registry.addRelationship(VolumeContains, voxel2, volumeB);  // → hookB fires (NOT hookA)
```

**Key Insight:** Each volume has its own `ComponentCacheItem`, so hooks are isolated.

### Global Hook (RelationshipRegistry)
```cpp
// 1. Register global hook
registry.onRelationshipAdded(VolumeContains, [](World& w, Entity voxel, Entity volume) {
    std::cout << "Any voxel added to any volume\n";
});

// 2. Add relationships
registry.addRelationship(VolumeContains, voxel1, volumeA);  // → Global hook fires
registry.addRelationship(VolumeContains, voxel2, volumeB);  // → Global hook fires
```

**Note:** Global hooks currently store callbacks but don't automatically register with Gaia (Gaia doesn't support wildcard hooks). You'd need to manually invoke them in `addRelationship()`.

---

## Performance Tips

### Use Direct Queries When Possible
```cpp
// ✅ FAST: O(1) - direct hash lookup
Entity volume = registry.getRelationshipTarget(VolumeContains, voxel);

// ❌ SLOW: O(N) - iterates all entities
auto voxels = registry.findAllWithRelationship(VolumeContains, volume);
```

### Cache Inverse Queries
```cpp
// Instead of querying every frame:
auto voxels = registry.findAllWithRelationship(VolumeContains, volume);  // O(N)

// Cache the result and update when relationships change:
class VolumeCache {
    std::unordered_map<Entity, std::vector<Entity>> volumeToVoxels;

    void onVoxelAdded(Entity voxel, Entity volume) {
        volumeToVoxels[volume].push_back(voxel);  // O(1)
    }

    const std::vector<Entity>& getVoxels(Entity volume) const {
        return volumeToVoxels.at(volume);  // O(1)
    }
};
```

### Batch Relationship Operations
```cpp
// Use EntityBuilder for multiple relationships
auto builder = world.build(entity);
builder.add(Pair(rel1, target1));
builder.add(Pair(rel2, target2));
builder.add(Pair(rel3, target3));
builder.commit();  // Single archetype migration
```

---

## Common Patterns

### Pattern: Volume-Voxel with Auto AABB Expansion
```cpp
Entity volume = ArchetypeBuilder(world, registry)
    .add<AABB>()
    .add<Volume>(Volume{0.1f})
    .onRelationshipAdded(VolumeContains, [](Entity voxel, Entity vol, World& w) {
        auto* aabb = w.get_mut<AABB>(vol);
        auto* key = w.get<MortonKey>(voxel);
        if (aabb && key) {
            aabb->expandToContain(MortonKeyUtils::toWorldPos(*key));
        }
    })
    .build();
```

### Pattern: Bidirectional Relationship
```cpp
// Create parent-child relationship with inverse
Entity parent = world.add();
Entity child = world.add();

registry.addRelationship(Contains, parent, child);  // Parent contains child
registry.addRelationship(ChildOf, child, parent);   // Child is child of parent

// Query both directions
Entity p = registry.getRelationshipTarget(ChildOf, child);      // → parent
Entity c = registry.getRelationshipTarget(Contains, parent);    // → child
```

### Pattern: Typed Relationships
```cpp
// Use different relationship types for different semantics
registry.addRelationship(VolumeContains, voxel, volume);  // Spatial containment
registry.addRelationship(Uses, shader, texture);          // Resource dependency
registry.addRelationship(References, pointer, target);    // Weak reference
```

---

## Debugging

### Enable Debug Logging
```cpp
#ifndef NDEBUG
registry.onRelationshipAdded(VolumeContains, [](World& w, Entity voxel, Entity volume) {
    std::cout << "[DEBUG] Voxel " << voxel.value()
              << " added to volume " << volume.value() << "\n";
});
#endif
```

### Validate Relationships
```cpp
void validateVolumeIntegrity(const RelationshipRegistry& registry, Entity volume) {
    auto voxels = registry.findAllWithRelationship(VolumeContains, volume);
    std::cout << "Volume " << volume.value() << " contains " << voxels.size() << " voxels\n";

    for (Entity voxel : voxels) {
        Entity parent = registry.getRelationshipTarget(VolumeContains, voxel);
        assert(parent == volume && "Voxel parent mismatch!");
    }
}
```

---

## Gaia-ECS Hook Signatures

### Add Hook
```cpp
ComponentCache::hooks(pairItem).func_add = [](
    const World& world,
    const ComponentCacheItem& cci,
    Entity sourceEntity
) {
    // sourceEntity = entity receiving the pair
};
```

### Delete Hook
```cpp
ComponentCache::hooks(pairItem).func_del = [](
    const World& world,
    const ComponentCacheItem& cci,
    Entity sourceEntity
) {
    // sourceEntity = entity losing the pair
};
```

### Set Hook (Chunk-Level)
```cpp
ComponentCache::hooks(item).func_set = [](
    const World& world,
    const ComponentRecord& rec,
    Chunk& chunk
) {
    // Fires when component value changed (NOT silent set)
    // Operates on entire chunk, not individual entity
};
```

---

**Document Version:** 1.0
**Last Updated:** 2025-11-24
**See Also:** [ArchetypeBuilder-Architecture.md](ArchetypeBuilder-Architecture.md)
