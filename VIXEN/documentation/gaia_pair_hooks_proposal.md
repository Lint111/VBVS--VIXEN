# Feature Request: Relationship (Pair) Hooks

## Summary

Add `OnAdd`/`OnRemove` hooks for relationship pairs, following the existing component hook patterns.

## Motivation

When building systems with entity relationships (hierarchies, spatial containers, graphs), it's often necessary to react when relationships are established or broken. Currently, component hooks exist but pair hooks don't - pairs are explicitly filtered out in the queuing logic:

```cpp
// world.h:814-815 - only components get hooks
if (entity.comp())
    tl_new_comps.push_back(entity);
```

### Use Case Example

Voxel engine with spatial containers:
- Voxels can be "part of" a Volume
- When relationship established → update volume statistics, mark octree dirty
- When relationship broken → trigger cleanup

## Proposed Implementation

Following the existing hook architecture with minimal additions:

### 1. Add Pair Hook Storage (world.h)

```cpp
// Near existing hook types
using FuncOnPairAdd = void(World& world, Entity source, Pair pair);
using FuncOnPairDel = void(World& world, Entity source, Pair pair);

struct PairHooks {
    FuncOnPairAdd* func_add{};
    FuncOnPairDel* func_del{};
};

// In World class (private section)
#if GAIA_ENABLE_PAIR_HOOKS
    // Hooks indexed by relation entity id
    // Key = relation.id(), Value = hooks for that relation type
    cnt::map<uint32_t, PairHooks> m_pairHooks;
#endif
```

### 2. Add Pair Queuing Arrays (EntityBuilder)

```cpp
// Near existing tl_new_comps/tl_del_comps
#if GAIA_ENABLE_PAIR_HOOKS
    cnt::sarray_ext<Entity, 32> tl_new_pairs;
    cnt::sarray_ext<Entity, 32> tl_del_pairs;
#endif
```

### 3. Queue Pairs During Add/Del (EntityBuilder)

```cpp
// In handle_add<>(), after existing component hook queuing (line ~815)
if (entity.comp())
    tl_new_comps.push_back(entity);
#if GAIA_ENABLE_PAIR_HOOKS
else if (entity.pair())
    tl_new_pairs.push_back(entity);
#endif

// In handle_del(), after existing component hook queuing (line ~873)
if (entity.comp())
    tl_del_comps.push_back(entity);
#if GAIA_ENABLE_PAIR_HOOKS
else if (entity.pair())
    tl_del_pairs.push_back(entity);
#endif
```

### 4. Add Hook Invocation Functions (EntityBuilder)

```cpp
#if GAIA_ENABLE_PAIR_HOOKS
void trigger_pair_add_hooks() {
    if (tl_new_pairs.empty())
        return;

    m_world.lock();
    for (auto pairEntity : tl_new_pairs) {
        const auto rel = pairEntity.id();   // relation
        const auto it = m_world.m_pairHooks.find(rel);
        if (it != m_world.m_pairHooks.end() && it->second.func_add != nullptr) {
            it->second.func_add(m_world, m_entity, Pair(pairEntity));
        }
    }
    tl_new_pairs.clear();
    m_world.unlock();
}

void trigger_pair_del_hooks() {
    if (tl_del_pairs.empty())
        return;

    m_world.lock();
    for (auto pairEntity : tl_del_pairs) {
        const auto rel = pairEntity.id();
        const auto it = m_world.m_pairHooks.find(rel);
        if (it != m_world.m_pairHooks.end() && it->second.func_del != nullptr) {
            it->second.func_del(m_world, m_entity, Pair(pairEntity));
        }
    }
    tl_del_pairs.clear();
    m_world.unlock();
}
#endif
```

### 5. Call in commit() (EntityBuilder)

```cpp
void commit() {
    if (m_pArchetype == nullptr)
        return;

    if (m_pArchetypeSrc != m_pArchetype) {
        auto& ec = m_world.fetch(m_entity);

        trigger_del_hooks();
#if GAIA_ENABLE_PAIR_HOOKS
        trigger_pair_del_hooks();
#endif

        m_world.move_entity_raw(m_entity, ec, *m_pArchetype);

        trigger_add_hooks();
#if GAIA_ENABLE_PAIR_HOOKS
        trigger_pair_add_hooks();
#endif

        // ... rest unchanged
    }
}
```

### 6. Registration API (World)

```cpp
#if GAIA_ENABLE_PAIR_HOOKS
/// Register hook called when Pair(relation, *) is added to any entity
void on_pair_add(Entity relation, FuncOnPairAdd* callback) {
    m_pairHooks[relation.id()].func_add = callback;
}

/// Register hook called when Pair(relation, *) is removed from any entity
void on_pair_del(Entity relation, FuncOnPairDel* callback) {
    m_pairHooks[relation.id()].func_del = callback;
}

/// Get mutable access to pair hooks for a relation
PairHooks& pair_hooks(Entity relation) {
    return m_pairHooks[relation.id()];
}
#endif
```

### 7. Config Flag (config.h)

```cpp
// Add near GAIA_ENABLE_ADD_DEL_HOOKS
#ifndef GAIA_ENABLE_PAIR_HOOKS
    #define GAIA_ENABLE_PAIR_HOOKS GAIA_ENABLE_HOOKS
#endif
```

## Usage Example

```cpp
// Create relation type
auto PartOf = world.add();

// Register hooks
world.on_pair_add(PartOf, [](World& w, Entity source, Pair pair) {
    auto target = w.get(pair.second());
    std::cout << source.id() << " is now part of " << target.id() << "\n";
    // Update container statistics, mark dirty, etc.
});

world.on_pair_del(PartOf, [](World& w, Entity source, Pair pair) {
    std::cout << source.id() << " removed from " << pair.second().id() << "\n";
});

// Normal usage - hooks fire automatically
auto voxel = world.add();
auto volume = world.add();
world.add(voxel, Pair(PartOf, volume));  // → triggers on_pair_add
world.del(voxel, Pair(PartOf, volume));  // → triggers on_pair_del
```

## Design Decisions

1. **Hooks indexed by relation only** (not relation+target)
   - Simpler, lower memory overhead
   - Target is available in callback via `pair.second()`
   - Users can filter by target in callback if needed

2. **Same invocation timing as component hooks**
   - `del` hooks before archetype movement
   - `add` hooks after archetype movement
   - Consistent with existing behavior

3. **Compile-time flag for zero overhead**
   - `GAIA_ENABLE_PAIR_HOOKS` defaults to `GAIA_ENABLE_HOOKS`
   - No cost for users who don't use pair hooks

4. **No separate cache system**
   - Simple `cnt::map` instead of full PairCache
   - Pairs are dynamic (relation+target), don't need ComponentCacheItem complexity
   - Avoids code bloat

## Files Modified

| File | Changes |
|------|---------|
| `config.h` | Add `GAIA_ENABLE_PAIR_HOOKS` flag |
| `world.h` | Add ~60 lines: types, storage, queuing, invocation, API |

## Alternatives Considered

1. **Full PairCache/PairCacheItem** - Overkill for hooks-only use case
2. **Hooks per relation+target** - Memory heavy, most use cases only need relation
3. **Observer pattern** - More complex, doesn't integrate with existing hook system

## Questions

1. Should pair hooks fire for built-in relations (ChildOf, Is, DependsOn)?
2. Interest in batch hook variant (called once with all added pairs)?
3. Preferred location for `PairHooks` struct - world.h or new header?

---

**Environment**: Gaia ECS v0.9.3
**Use case**: Real-time voxel engine with ECS-based spatial containers
