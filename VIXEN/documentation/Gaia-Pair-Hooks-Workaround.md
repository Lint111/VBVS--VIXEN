# Gaia-ECS Runtime Pair Hooks Workaround

## Problem: Gaia Doesn't Support ComponentCache Hooks on Runtime Pairs

### The Issue

**Gaia-ECS provides two different Pair APIs:**

1. **Compile-Time Pairs** (Full Support):
```cpp
// Template-based pairs - get ComponentCacheItem
w.add<ecs::pair<Start, Position>>(entity, {10, 15});
const ComponentCacheItem& cci = w.add<ecs::pair<Start, Position>>();
ComponentCache::hooks(cci).func_add = [](...)  { ... };  // ✅ Works!
```

2. **Runtime Pairs** (Limited Support):
```cpp
// Runtime entity pairs - NO ComponentCacheItem returned
Entity relation = w.add();
Entity target = w.add();
w.add(source, Pair(relation, target));  // ❌ Returns void, not ComponentCacheItem
```

**The Problem:**
- `world.add(entity, Pair(rel, tgt))` returns **void**, not `ComponentCacheItem`
- Cannot register hooks via `ComponentCache::hooks()` because we don't have a `ComponentCacheItem`
- Runtime pairs don't create component cache entries that support automatic hook invocation

**From Gaia Docs:**
```cpp
// ✅ This works for component registration:
const ecs::ComponentCacheItem& cci = w.add<Position>();
ecs::ComponentCache::hooks(cci).func_add = [](...)  { ... };

// ❌ This doesn't work for runtime pairs:
w.add(entity, ecs::Pair(relation, target));  // Returns void
// No ComponentCacheItem → No hook registration possible
```

---

## Solution: Manual Hook Invocation Pattern

Since Gaia doesn't provide automatic hooks for runtime pairs, we implement **manual hook invocation** in `RelationshipRegistry`.

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ ArchetypeBuilder                                            │
│                                                             │
│ Entity volume = ArchetypeBuilder(world, registry)          │
│     .add<AABB>()                                            │
│     .onRelationshipAdded(VolumeContains, hookFunction)     │
│     .build();                                               │
│                                                             │
│ ↓ Registers hook in RelationshipRegistry                   │
└─────────────────────────────────────────────────────────────┘
                          │
                          ↓
┌─────────────────────────────────────────────────────────────┐
│ RelationshipRegistry                                        │
│                                                             │
│ Per-Entity Hooks Storage:                                  │
│ m_perEntityAddHooks[{VolumeContains, volumeEntity}]        │
│   = hookFunction                                            │
│                                                             │
│ addRelationship(VolumeContains, voxel, volume):            │
│   1. world.add(voxel, Pair(rel, volume))  // Gaia call     │
│   2. Lookup hook for (VolumeContains, volume)              │
│   3. If found → invoke hookFunction(voxel, volume, world)  │
└─────────────────────────────────────────────────────────────┘
```

### Implementation Details

#### 1. ArchetypeBuilder Hook Registration

**File:** [ArchetypeBuilder.h](../libraries/GaiaVoxelWorld/Arcetypes/ArchetypeBuilder.h:124-167)

```cpp
void registerPairHook(
    RelationshipRegistry::RelationType relationType,
    RelationshipHook onAdded,
    RelationshipHook onRemoved
) {
    Entity relationEntity = m_registry.getRelationship(relationType);
    Entity thisEntity = m_entity;  // Target entity

    // Store hook in registry for manual invocation
    // Can't use Gaia ComponentCache because runtime pairs return void
    m_registry.registerPerEntityHook(
        relationType,
        thisEntity,
        onAdded,
        onRemoved
    );
}
```

#### 2. RelationshipRegistry Hook Storage

**File:** [RelationshipRegistry.h](../libraries/GaiaVoxelWorld/Arcetypes/RelationshipRegistry.h:256-277)

```cpp
// Per-entity hooks map
using HookKey = std::pair<RelationType, Entity>;

struct HookKeyHash {
    std::size_t operator()(const HookKey& key) const {
        return std::hash<int>()(static_cast<int>(key.first)) ^
               std::hash<uint32_t>()(key.second.value());
    }
};

// Maps {RelationType, targetEntity} → Hook function
std::unordered_map<HookKey, std::function<void(Entity, Entity, World&)>, HookKeyHash>
    m_perEntityAddHooks;
std::unordered_map<HookKey, std::function<void(Entity, Entity, World&)>, HookKeyHash>
    m_perEntityRemoveHooks;
```

**Hook Registration API:**
```cpp
void registerPerEntityHook(
    RelationType type,
    Entity targetEntity,
    std::function<void(Entity, Entity, World&)> onAdded,
    std::function<void(Entity, Entity, World&)> onRemoved
) {
    if (onAdded) {
        m_perEntityAddHooks[{type, targetEntity}] = onAdded;
    }
    if (onRemoved) {
        m_perEntityRemoveHooks[{type, targetEntity}] = onRemoved;
    }
}
```

#### 3. Manual Hook Invocation

**File:** [RelationshipRegistry.h](../libraries/GaiaVoxelWorld/Arcetypes/RelationshipRegistry.h:119-138)

```cpp
void addRelationship(RelationType type, Entity source, Entity target) {
    Entity relationEntity = getRelationship(type);
    if (relationEntity == EntityBad) return;

    // 1. Add Gaia pair
    m_world.add(source, Pair(relationEntity, target));

    // 2. MANUAL HOOK INVOCATION: Check for per-entity hooks
    auto perEntityKey = std::make_pair(type, target);
    auto it = m_perEntityAddHooks.find(perEntityKey);
    if (it != m_perEntityAddHooks.end()) {
        it->second(source, target, m_world);  // ← Hook fires here
    }

    // 3. Also invoke global hooks
    for (auto& hook : m_addHooks) {
        if (hook.first == type) {
            hook.second(m_world, source, target);
        }
    }
}
```

---

## Usage Example

### Creating Volume with Per-Entity Hook

```cpp
World world;
RelationshipRegistry registry(world);
registry.initialize();

// Create volume with hook that expands AABB when voxels added
Entity volume = ArchetypeBuilder(world, registry)
    .add<AABB>()
    .add<Volume>(Volume{0.1f})
    .onRelationshipAdded(
        RelationshipRegistry::RelationType::VolumeContains,
        [](Entity voxel, Entity vol, World& w) {
            // This hook fires when voxel added to THIS volume
            auto* aabb = w.get_mut<AABB>(vol);
            auto* key = w.get<MortonKey>(voxel);
            if (aabb && key) {
                glm::vec3 pos = MortonKeyUtils::toWorldPos(*key);
                aabb->expandToContain(pos);
                std::cout << "[Hook] Expanded AABB for volume "
                          << vol.value() << "\n";
            }
        }
    )
    .build();

// Add voxel to volume - hook triggers!
Entity voxel = world.add();
world.add<MortonKey>(voxel, MortonKey{1234});

registry.addRelationship(
    RelationshipRegistry::RelationType::VolumeContains,
    voxel,
    volume
);
// → Hook fires: "[Hook] Expanded AABB for volume X"
```

### Hook Isolation

```cpp
// Create two volumes with different hooks
Entity volumeA = ArchetypeBuilder(world, registry)
    .add<AABB>()
    .onRelationshipAdded(VolumeContains, [](Entity v, Entity vol, World& w) {
        std::cout << "[Volume A Hook] Voxel added\n";
    })
    .build();

Entity volumeB = ArchetypeBuilder(world, registry)
    .add<AABB>()
    .onRelationshipAdded(VolumeContains, [](Entity v, Entity vol, World& w) {
        std::cout << "[Volume B Hook] Voxel added\n";
    })
    .build();

// Add voxel to volumeA - only volumeA's hook fires
registry.addRelationship(VolumeContains, voxel1, volumeA);
// Output: "[Volume A Hook] Voxel added"

// Add voxel to volumeB - only volumeB's hook fires
registry.addRelationship(VolumeContains, voxel2, volumeB);
// Output: "[Volume B Hook] Voxel added"
```

**Hook isolation works because:**
- Each volume has its own entry in `m_perEntityAddHooks[{VolumeContains, volumeA}]`
- Lookup uses `{RelationType, targetEntity}` key
- Only matching target entity's hook fires

---

## Performance Characteristics

### Hook Lookup Cost

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Hook registration | O(1) | `unordered_map` insert |
| Hook lookup | O(1) | `unordered_map` find with hash |
| Hook invocation | O(1) per hook | Direct function call |
| Global hooks | O(N) | Linear scan of global hooks (typically small N) |

### Memory Overhead

```cpp
// Per-entity hook storage
sizeof(HookKey) = 8 bytes  // enum (4) + Entity (4)
sizeof(std::function) ≈ 32 bytes  // Function object + capture

// Total per hook: ~40 bytes
// For 1000 volumes with hooks: ~40 KB
```

### Optimization Opportunities

1. **Cache locality:** Hooks stored in hash table (not cache-friendly)
2. **Function call overhead:** `std::function` has virtual call cost
3. **Hash computation:** XOR hash on every lookup

**If performance critical:**
- Use flat map (sorted vector) for better cache locality
- Store function pointers instead of `std::function` to avoid virtual calls
- Batch relationship additions to amortize lookup cost

---

## Alternative Approaches Considered

### 1. Compile-Time Pair Components

**Approach:** Generate compile-time `pair<Relation, Target>` types for each relationship.

```cpp
// Define compile-time pair type
struct VolumeContainsTag {};
struct VolumeEntity {};

// Register hooks on pair type
const auto& cci = w.add<ecs::pair<VolumeContainsTag, VolumeEntity>>();
ComponentCache::hooks(cci).func_add = [](...)  { ... };  // ✅ Works!

// Use compile-time pairs
w.add<ecs::pair<VolumeContainsTag, VolumeEntity>>(voxel);
```

**Pros:**
- Uses native Gaia ComponentCache hooks
- Automatic invocation (no manual calls)
- Type-safe

**Cons:**
- Requires code generation for each relationship type
- Less flexible (can't create relationships dynamically)
- More complex build system

### 2. Gaia Observer System

**Approach:** Use Gaia's observer/reactive system (if available).

```cpp
// Hypothetical API (not confirmed in Gaia docs)
world.observe<Pair>()
    .onAdd([](Entity source, Pair pair) {
        // React to pair addition
    });
```

**Pros:**
- Native Gaia feature
- Potentially more efficient

**Cons:**
- Not documented in Gaia README
- May not exist or may not work with runtime pairs
- Unclear if supports per-entity hooks

### 3. Custom Event System

**Approach:** Build separate event bus for relationship changes.

```cpp
class RelationshipEventBus {
    void subscribe(RelationType type, Entity target, Callback cb);
    void publish(RelationType type, Entity source, Entity target);
};
```

**Pros:**
- Full control over event dispatch
- Can add features (priorities, async, etc.)

**Cons:**
- Duplicates Gaia's hook system
- More code to maintain
- Performance overhead (double dispatch)

---

## Chosen Solution: Manual Hook Invocation

**Why this approach?**

1. **Simplicity:** Minimal changes to existing API
2. **Compatibility:** Works with Gaia's runtime pairs
3. **Performance:** O(1) hash lookup, no virtual dispatch
4. **Flexibility:** Supports both per-entity and global hooks
5. **No code generation:** Works with dynamic relationships

**Trade-offs:**
- Manual invocation required (not automatic like ComponentCache hooks)
- Must remember to call via `registry.addRelationship()`, not `world.add()` directly
- Hook lookup overhead on every relationship add/remove

---

## Best Practices

### 1. Always Use RelationshipRegistry

**❌ DON'T:**
```cpp
// Bypasses hooks!
world.add(voxel, Pair(relationEntity, volumeEntity));
```

**✅ DO:**
```cpp
// Invokes hooks automatically
registry.addRelationship(VolumeContains, voxel, volumeEntity);
```

### 2. Hooks Should Be Fast

```cpp
// ❌ BAD: Expensive operation in hook
.onRelationshipAdded(VolumeContains, [](Entity v, Entity vol, World& w) {
    // Don't do O(N) operations here!
    auto allVoxels = registry.findAllWithRelationship(VolumeContains, vol);
    recalculateVolume(allVoxels);
});

// ✅ GOOD: Incremental update
.onRelationshipAdded(VolumeContains, [](Entity v, Entity vol, World& w) {
    // Fast O(1) AABB expansion
    auto* aabb = w.get_mut<AABB>(vol);
    auto* key = w.get<MortonKey>(v);
    if (aabb && key) {
        aabb->expandToContain(MortonKeyUtils::toWorldPos(*key));
    }
});
```

### 3. Avoid Direct world.add() with Pairs

**Enforcement:** Consider making `world` private and exposing only through `RelationshipRegistry` API.

```cpp
class WorldContext {
public:
    RelationshipRegistry& relationships() { return m_registry; }
    // Don't expose raw world to prevent bypassing hooks
private:
    World& m_world;
};
```

---

## Future Work

### If Gaia Adds Runtime Pair Hook Support

If future Gaia versions add `ComponentCacheItem` support for runtime pairs:

```cpp
// Hypothetical future API
ComponentCacheItem pairItem = world.add(Pair(rel, target));
ComponentCache::hooks(pairItem).func_add = [](...)  { ... };
```

**Migration Path:**
1. Check Gaia version at runtime
2. Use native hooks if available
3. Fall back to manual invocation on older versions
4. Gradually migrate to native hooks

### Alternative: Wrapper Library

Create thin wrapper around Gaia that adds runtime pair hook support:

```cpp
class GaiaWrapper {
public:
    ComponentCacheItem addPair(Entity source, Pair pair) {
        world.add(source, pair);
        return createCacheItemProxy(pair);  // Custom wrapper
    }
};
```

---

## Conclusion

**Current State:**
- Gaia-ECS runtime pairs don't support ComponentCache hooks
- Manual hook invocation pattern provides equivalent functionality
- Per-entity hooks work correctly with O(1) lookup

**Recommendation:**
- Use this workaround for runtime relationship hooks
- Performance is acceptable for typical voxel use cases
- Consider compile-time pairs if performance becomes critical

**Open Questions:**
1. Does Gaia have observer/reactive system for pairs?
2. Will future versions support runtime pair hooks?
3. Should we implement compile-time pair wrapper for critical paths?

---

**Document Version:** 1.0
**Last Updated:** 2025-11-24
**See Also:**
- [ArchetypeBuilder-Architecture.md](ArchetypeBuilder-Architecture.md)
- [ArchetypeBuilder-QuickReference.md](ArchetypeBuilder-QuickReference.md)
