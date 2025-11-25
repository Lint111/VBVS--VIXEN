# ArchetypeBuilder Architecture - Complete Gaia-ECS Integration

## Overview

The ArchetypeBuilder system provides a fluent API for creating complex entities with components, relationships, and lifecycle hooks in Gaia-ECS. It handles the complete process from entity creation to relationship management and hook registration.

## Core Components

### 1. RelationshipRegistry
**Location:** `libraries/GaiaVoxelWorld/Arcetypes/RelationshipRegistry.h`

**Purpose:** Centralized management of relationship types as singleton entities.

**Key Features:**
- Defines standard relationship types (PartOf, Contains, ChildOf, VolumeContains, Uses, References)
- Creates named relationship entities on initialization
- Provides helper methods for adding/removing relationships
- Supports querying relationships (find all entities with relationship to target)

**Architecture Pattern:**
```cpp
// Singleton relationship entities
enum class RelationType {
    PartOf,          // Entity is part of another (composition)
    Contains,        // Entity contains another (inverse)
    ChildOf,         // Parent-child hierarchy
    VolumeContains,  // Volume contains voxel data
    Uses,            // Entity uses another (dependency)
    References,      // Weak reference
};

// Maps enum → Entity (created once at init)
std::unordered_map<RelationType, Entity> m_relationships;
```

**Usage:**
```cpp
World world;
RelationshipRegistry registry(world);
registry.initialize();

// Get relationship entity
Entity volumeContains = registry.getRelationship(RelationType::VolumeContains);

// Add relationship between entities
registry.addRelationship(RelationType::VolumeContains, voxelEntity, volumeEntity);

// Query all voxels in a volume
auto voxels = registry.findAllWithRelationship(RelationType::VolumeContains, volumeEntity);
```

---

### 2. ArchetypeBuilder
**Location:** `libraries/GaiaVoxelWorld/Arcetypes/ArchetypeBuilder.h`

**Purpose:** Fluent API for creating entities with components, relationships, and per-entity hooks.

**Key Features:**
- Chainable component addition (`.add<T>()`)
- Deferred relationship establishment (`.relateTo()`)
- Per-entity relationship hooks (`.onRelationshipAdded()`)
- Lifecycle callbacks (`.onCreate()`)
- Automatic commit and hook registration

**Architecture Pattern:**
```cpp
class ArchetypeBuilder {
    World& m_world;
    RelationshipRegistry& m_registry;
    Entity m_entity;                // Entity being built
    EntityBuilder m_builder;        // Gaia's batch modification API

    std::vector<HookData> m_relationshipHooks;       // Hooks to register
    std::vector<DeferredRelation> m_deferredRelations; // Relationships to add
    std::vector<std::function<void(Entity, World&)>> m_onCreateCallbacks;
};
```

**Hook Registration Mechanism (Gaia Pattern):**

The critical insight for Gaia-ECS hook registration:

```cpp
// 1. Create ComponentCacheItem for specific Pair(relation, target)
ComponentCacheItem pairItem = world.add(Pair(relationEntity, targetEntity));

// 2. Register hook on that specific cache item
ComponentCache::hooks(pairItem).func_add = [targetEntity](
    const World& world,
    const ComponentCacheItem& cci,
    Entity sourceEntity
) {
    // Hook fires when: world.add(sourceEntity, Pair(relationEntity, targetEntity))
    // sourceEntity = entity receiving the relationship
    // targetEntity = captured from builder
};

// 3. When relationship added, hook fires
world.add(voxelEntity, Pair(relationEntity, volumeEntity));
// → Gaia looks up ComponentCacheItem for this exact pair
// → Finds registered hook
// → Executes callback with sourceEntity=voxelEntity
```

**Why This Works:**
- Each `(relation, target)` pair gets its own `ComponentCacheItem`
- Hooks are registered per cache item, NOT globally
- Volume A and Volume B have INDEPENDENT hooks (different cache items)
- Clean separation: adding voxel to Volume A doesn't trigger Volume B's hooks

**Usage:**
```cpp
Entity volume = ArchetypeBuilder(world, registry)
    .add<AABB>()
    .add<Volume>(Volume{0.1f})
    .add<VolumeTransform>()
    .onRelationshipAdded(
        RelationshipRegistry::RelationType::VolumeContains,
        [](Entity voxel, Entity volume, World& w) {
            // Expand volume AABB when voxel added
            auto* mortonKey = w.get<MortonKey>(voxel);
            auto* aabb = w.get_mut<AABB>(volume);
            if (mortonKey && aabb) {
                glm::vec3 worldPos = MortonKeyUtils::toWorldPos(*mortonKey);
                aabb->expandToContain(worldPos);
            }
        }
    )
    .onCreate([](Entity e, World& w) {
        std::cout << "Volume created: " << e.value() << "\n";
    })
    .build();

// Later: add voxel to volume - hook fires!
Entity voxel = world.add();
world.add<MortonKey>(voxel, key);
registry.addRelationship(RelationType::VolumeContains, voxel, volume);
// → Hook executes: AABB expands
```

---

### 3. WorldContext
**Location:** `libraries/GaiaVoxelWorld/Arcetypes/WorldContext.h`

**Purpose:** High-level facade for ECS world with relationship management and archetype factories.

**Key Features:**
- Initializes RelationshipRegistry
- Provides factory methods for common archetypes (volumes, voxels)
- Registers global hooks for system-wide behavior
- Creates complex multi-entity structures (octrees, hierarchies)

**Architecture Pattern:**
```cpp
class WorldContext {
    World& m_world;
    RelationshipRegistry m_registry;

public:
    void initialize() {
        m_registry.initialize();
        registerHooks();  // Global system hooks
    }

    // Factory: simple volume
    Entity createVolume(float voxelSize);

    // Factory: voxel with relationship
    Entity createVoxel(Entity volumeEntity, const MortonKey& key);

    // Factory: complex structure
    VoxelOctreeStructure createVoxelOctree(float voxelSize, const std::vector<MortonKey>& keys);

    // Custom archetype builder
    ArchetypeBuilder build() {
        return ArchetypeBuilder(m_world, m_registry);
    }
};
```

**Global vs Per-Entity Hooks:**

**Per-Entity Hooks (ArchetypeBuilder):**
- Registered for specific target entity
- Volume A and Volume B have independent hooks
- Example: Volume A's AABB expansion logic differs from Volume B's

**Global Hooks (WorldContext):**
- Registered via RelationshipRegistry
- Trigger for ALL pairs of relationship type
- Example: Log every voxel addition across all volumes

**Usage:**
```cpp
WorldContext ctx(world);
ctx.initialize();

// Simple factory usage
Entity volume = ctx.createVolume(0.1f);
Entity voxel = ctx.createVoxel(volume, mortonKey);

// Complex structure
auto octree = ctx.createVoxelOctree(0.1f, voxelKeys);
// → Creates volume + multiple voxels + relationships

// Custom archetype
Entity custom = ctx.build()
    .add<MortonKey>(key)
    .add<Color>(Color{glm::vec3(1, 0, 0)})
    .relateTo(RelationType::VolumeContains, volume)
    .build();
```

---

## Component Hook Types (Gaia-ECS)

### Add Hook (`func_add`)
**Triggers:** When component/pair added to entity

**Signature:**
```cpp
void func_add(const World& world,
              const ComponentCacheItem& cci,
              Entity sourceEntity);
```

**Use Case:** Initialize state, update relationships, expand AABB

### Delete Hook (`func_del`)
**Triggers:** When component/pair removed from entity

**Signature:**
```cpp
void func_del(const World& world,
              const ComponentCacheItem& cci,
              Entity sourceEntity);
```

**Use Case:** Cleanup, shrink AABB, remove from spatial index

### Set Hook (`func_set`)
**Triggers:** When write access requested (NOT on silent sets)

**Signature:**
```cpp
void func_set(const World& world,
              const ComponentRecord& rec,
              Chunk& chunk);
```

**Use Case:** Dirty flags, change tracking, invalidation

**Note:** Set hooks operate at **chunk level**, not individual entities

---

## Workflow: Creating Entity with Hooks

### Step 1: Initialize Registry
```cpp
World world;
RelationshipRegistry registry(world);
registry.initialize();  // Creates relationship entities
```

### Step 2: Create Target Entity with Hooks
```cpp
Entity volume = ArchetypeBuilder(world, registry)
    .add<AABB>()
    .add<Volume>(Volume{0.1f})
    .onRelationshipAdded(
        RelationshipRegistry::RelationType::VolumeContains,
        [](Entity voxel, Entity volume, World& w) {
            // Hook implementation
        }
    )
    .build();
```

**Behind the scenes:**
1. `ArchetypeBuilder` creates entity via `world.add()`
2. Adds components via `EntityBuilder` (batch operation)
3. Commits entity with `builder.commit()`
4. Registers hooks via `ComponentCache::hooks(pairItem).func_add = ...`
5. Executes `onCreate` callbacks

### Step 3: Create Source Entity and Add Relationship
```cpp
Entity voxel = world.add();
world.add<MortonKey>(voxel, key);

// THIS TRIGGERS THE HOOK
registry.addRelationship(RelationType::VolumeContains, voxel, volume);
// → world.add(voxel, Pair(relationEntity, volume))
// → Gaia finds ComponentCacheItem for Pair(relationEntity, volume)
// → Executes registered hook
```

---

## Advanced Patterns

### Pattern 1: Multi-Hook Entity
```cpp
Entity volume = ArchetypeBuilder(world, registry)
    .add<AABB>()
    .onRelationship(
        RelationType::VolumeContains,
        // Add hook
        [](Entity voxel, Entity vol, World& w) {
            std::cout << "Voxel added\n";
        },
        // Remove hook
        [](Entity voxel, Entity vol, World& w) {
            std::cout << "Voxel removed\n";
        }
    )
    .onRelationshipAdded(
        RelationType::Uses,  // Different relationship
        [](Entity user, Entity vol, World& w) {
            std::cout << "Volume now used by: " << user.value() << "\n";
        }
    )
    .build();
```

### Pattern 2: Deferred Relationship
```cpp
// Create voxel that's immediately linked to volume
Entity voxel = ArchetypeBuilder(world, registry)
    .add<MortonKey>(key)
    .relateTo(RelationType::VolumeContains, volumeEntity)
    .build();
// → Relationship added during build(), hooks fire automatically
```

### Pattern 3: Complex Structure Creation
```cpp
auto octree = ctx.createHierarchicalOctree(0.1f, 4);
// Creates:
// - Root volume (finest resolution)
// - 3 LOD volumes (coarser resolutions)
// - ChildOf relationships between LOD levels
```

### Pattern 4: Querying Relationships

**Direct Pair Access (Fast - O(1)):**
```cpp
// Get target entity from pair on source entity
// Uses Gaia's world.target(entity, relation) API
Entity volume = registry.getRelationshipTarget(RelationType::VolumeContains, voxel);
// Returns first volume that voxel is contained in

// Get all targets (if entity has multiple relationships of same type)
std::vector<Entity> volumes = registry.getRelationshipTargets(RelationType::VolumeContains, voxel);
// Returns all volumes containing this voxel

// Check if specific relationship exists
bool isContained = registry.hasRelationship(RelationType::VolumeContains, voxel, volume);

// Find what relationship connects two entities
Entity relation = registry.getRelationBetween(voxel, volume);
// Returns relationship entity (e.g., volumeContainsEntity)
```

**Inverse Query (Slower - O(N) iteration):**
```cpp
// Find all entities with relationship TO target
auto voxels = registry.findAllWithRelationship(RelationType::VolumeContains, volumeEntity);
// Returns all voxels contained in this volume
// Note: Iterates all entities with Pair(VolumeContains, volumeEntity)
```

**Performance Comparison:**
- `getRelationshipTarget()`: O(1) - direct hash lookup on source entity
- `findAllWithRelationship()`: O(N) - query iteration over all entities
- Use `target()` when you know the source, use `findAll()` for inverse queries

---

## Complete API Reference

### RelationshipRegistry API

#### Relationship Management
```cpp
// Add relationship between entities
void addRelationship(RelationType type, Entity source, Entity target);
// Example: registry.addRelationship(VolumeContains, voxel, volume);

// Remove relationship
void removeRelationship(RelationType type, Entity source, Entity target);
// Example: registry.removeRelationship(VolumeContains, voxel, volume);

// Check if relationship exists
bool hasRelationship(RelationType type, Entity source, Entity target) const;
// Example: if (registry.hasRelationship(VolumeContains, voxel, volume)) { ... }
```

#### Direct Pair Queries (Fast - O(1))
```cpp
// Get first target of relationship from source
Entity getRelationshipTarget(RelationType type, Entity source) const;
// Example: Entity volume = registry.getRelationshipTarget(VolumeContains, voxel);
// Returns: First volume entity that voxel is contained in
// Uses: world.target(source, relationEntity)

// Get all targets of relationship from source
std::vector<Entity> getRelationshipTargets(RelationType type, Entity source) const;
// Example: auto volumes = registry.getRelationshipTargets(VolumeContains, voxel);
// Returns: All volume entities containing this voxel
// Uses: world.targets(source, relationEntity, callback)

// Get relationship entity connecting two entities
Entity getRelationBetween(Entity source, Entity target) const;
// Example: Entity rel = registry.getRelationBetween(voxel, volume);
// Returns: Relationship entity (e.g., volumeContainsEntity)
// Uses: world.relation(source, target)
```

#### Inverse Queries (Slow - O(N) iteration)
```cpp
// Find all entities with relationship TO target
std::vector<Entity> findAllWithRelationship(RelationType type, Entity target) const;
// Example: auto voxels = registry.findAllWithRelationship(VolumeContains, volume);
// Returns: All voxel entities contained in this volume
// Uses: world.query().all(Pair(relationEntity, target))
```

#### Global Hooks (System-Wide)
```cpp
// Register hook for ALL instances of relationship type
void onRelationshipAdded(RelationType type, std::function<void(World&, Entity source, Entity target)> callback);
// Example: registry.onRelationshipAdded(VolumeContains, [](World& w, Entity voxel, Entity volume) {
//     std::cout << "Any voxel added to any volume\n";
// });

void onRelationshipRemoved(RelationType type, std::function<void(World&, Entity source, Entity target)> callback);
```

### ArchetypeBuilder API

#### Component Addition
```cpp
// Add component with default construction
ArchetypeBuilder& add<T>();
// Example: builder.add<AABB>();

// Add component with value
ArchetypeBuilder& add<T>(const T& value);
// Example: builder.add<Volume>(Volume{0.1f});
```

#### Relationship Configuration
```cpp
// Register hook for when entities relate TO this entity (per-entity hook)
ArchetypeBuilder& onRelationshipAdded(RelationType relationType, RelationshipHook onAdded);
// Example: builder.onRelationshipAdded(VolumeContains, [](Entity voxel, Entity volume, World& w) {
//     // Only fires when voxel added to THIS volume
// });

// Register both add and remove hooks
ArchetypeBuilder& onRelationship(RelationType relationType, RelationshipHook onAdded, RelationshipHook onRemoved);

// Add relationship to another entity (deferred until build)
ArchetypeBuilder& relateTo(RelationType relationType, Entity target);
// Example: builder.relateTo(VolumeContains, volumeEntity);
```

#### Lifecycle Callbacks
```cpp
// Register callback after entity created
ArchetypeBuilder& onCreate(std::function<void(Entity, World&)> callback);
// Example: builder.onCreate([](Entity e, World& w) {
//     std::cout << "Entity created: " << e.value() << "\n";
// });
```

#### Build
```cpp
// Commit entity and register all hooks
Entity build();
// Returns: Created entity with all components and hooks registered

// Get entity before building (advanced)
Entity entity() const;
```

### WorldContext API

#### Initialization
```cpp
void initialize();
// Initializes RelationshipRegistry and registers global hooks
```

#### Factory Methods
```cpp
// Create volume entity
Entity createVolume(float voxelSize = 1.0f);

// Create voxel linked to volume
Entity createVoxel(Entity volumeEntity, const MortonKey& key);

// Create voxel with attributes
Entity createVoxelWithAttributes(Entity volumeEntity, const MortonKey& key, float density, const glm::vec3& color);
```

#### Complex Structures
```cpp
// Create volume + multiple voxels
VoxelOctreeStructure createVoxelOctree(float voxelSize, const std::vector<MortonKey>& keys);
// Returns: { Entity volumeEntity, std::vector<Entity> voxelEntities }

// Create hierarchical octree with LOD levels
HierarchicalOctree createHierarchicalOctree(float baseVoxelSize, int lodLevels);
// Returns: { Entity rootVolume, std::vector<Entity> childVolumes, ... }
```

#### Custom Archetype Builder
```cpp
// Get builder for custom entity
ArchetypeBuilder build();
// Example: ctx.build().add<MortonKey>(key).add<Color>(color).build();
```

---

## Memory and Performance Considerations

### Hook Registration Cost
- **Per-entity hooks:** One `ComponentCacheItem` per `(relation, target)` pair
- **Memory:** ~32 bytes per hook (lambda capture + function pointer)
- **Performance:** O(1) lookup when relationship added (hash table in ComponentCache)

### Relationship Storage
- **Gaia Pair:** Stored as entity component `Pair(relation, target)`
- **Memory:** ~16 bytes per relationship (2 entity IDs)
- **Query cost:** O(N) scan for `findAllWithRelationship` (no index)

### Optimization Opportunities
1. **Batch relationship addition:** Use `EntityBuilder` for multiple relationships
2. **Cache query results:** Store `std::vector<Entity>` of voxels instead of repeated queries
3. **Disable hooks when not needed:** Use `GAIA_ENABLE_HOOKS=0` compile flag

---

## Configuration Flags

```cpp
// Disable all hooks (compile-time)
#define GAIA_ENABLE_HOOKS 0

// Disable only add/delete hooks
#define GAIA_ENABLE_ADD_DEL_HOOKS 0

// Disable only set hooks
#define GAIA_ENABLE_SET_HOOKS 0
```

---

## Testing Strategy

### Unit Tests
- `test_archetype_builder.cpp`: ArchetypeBuilder API
- `test_relationship_registry.cpp`: Relationship management
- `test_component_hooks.cpp`: Hook execution and order

### Integration Tests
- `test_volume_voxel_integration.cpp`: Volume-voxel workflow
- `test_hook_isolation.cpp`: Independent hooks per entity
- `test_complex_structures.cpp`: Hierarchical octrees

### Example Files
- `VolumeArchetypeExample.cpp`: Complete working example with all patterns

---

## Key Takeaways

1. **Gaia Hook Pattern:** Hooks are registered per `ComponentCacheItem`, not globally
2. **Per-Entity Isolation:** Each target entity gets its own hook (via ArchetypeBuilder)
3. **Relationship as Pair:** `Pair(relation, target)` is a component on source entity
4. **Fluent API:** Chainable builder pattern for ergonomic entity creation
5. **Deferred Operations:** Relationships and hooks registered after entity commit
6. **Query Support:** RelationshipRegistry provides bidirectional relationship queries

---

## References

- **Gaia-ECS GitHub:** https://github.com/richardbiely/gaia-ecs
- **Component Hooks Docs:** README.md#component-hooks
- **Pair Components:** README.md#relationships
- **EntityBuilder Pattern:** README.md#bulk-operations

---

## Future Enhancements

### Planned Features
1. **Wildcard hooks:** Trigger for ALL instances of relationship type (global pattern)
2. **Hook priorities:** Control execution order for multiple hooks
3. **Async hooks:** Deferred execution for expensive operations
4. **Hook composition:** Combine multiple hooks into reusable hook chains

### Research Questions
1. Can we implement relationship indexing for O(1) `findAllWithRelationship`?
2. Should we cache `ComponentCacheItem` lookups to avoid repeated `world.add(Pair(...))`?
3. Can hook registration be deferred until first relationship added (lazy initialization)?

---

**Document Version:** 1.0
**Last Updated:** 2025-11-24
**Author:** Claude Code (Architecture Review)
