# Feature Request: Relationship (Pair) Hooks

Hey! I've been using Gaia ECS for a voxel engine project and really enjoying the architecture. I ran into a use case that I think could benefit from a small addition to the hook system.

## The Problem

I'm building spatial containers where voxel entities can be "part of" volume entities using pairs:

```cpp
world.add(voxel, Pair(PartOf, volume));
```

When this relationship is established, I need to:
- Update the volume's statistics (voxel count)
- Mark the volume's octree as dirty for rebuild
- Expand bounding boxes

Currently, component hooks (`func_add`/`func_del`) work great, but pairs are explicitly filtered out:

```cpp
// world.h:814-815
if (entity.comp())
    tl_new_comps.push_back(entity);
// ^ pairs don't get queued for hooks
```

So I built a wrapper that intercepts `world.add(entity, Pair(...))` calls and maintains its own callback registry. It works, but it feels like something that could be handled natively with minimal changes.

## Proposed Solution

Extend the existing hook system to support pairs. The implementation would follow the exact same patterns:

- **Queuing**: Add `tl_new_pairs`/`tl_del_pairs` alongside existing component arrays
- **Invocation**: Call `trigger_pair_add_hooks()` in `commit()` alongside `trigger_add_hooks()`
- **Storage**: Simple `cnt::map<uint32_t, PairHooks>` keyed by relation entity id
- **Config**: `GAIA_ENABLE_PAIR_HOOKS` flag (defaults to `GAIA_ENABLE_HOOKS`)

**Estimated changes**: ~60 lines in `world.h`, 1 line in `config.h`

## Usage Would Look Like

```cpp
auto PartOf = world.add();

// Register hook for when any Pair(PartOf, X) is added
world.on_pair_add(PartOf, [](World& w, Entity source, Pair pair) {
    auto target = pair.second();
    // Update target's statistics, mark dirty, etc.
});

// Normal usage - hooks fire automatically
world.add(voxel, Pair(PartOf, volume));  // → triggers hook
```

## Why Not Just Use the Wrapper?

The wrapper approach works, but:
1. Adds overhead (extra indirection, separate data structures)
2. Can be bypassed if someone calls `world.add()` directly
3. Doesn't integrate with existing constraint handling (OnDelete, etc.)

## Questions

Before diving into implementation details, I wanted to check:

1. Is this something you'd consider adding to core Gaia?
2. Any concerns with the approach?
3. Should pair hooks fire for built-in relations (ChildOf, Is, DependsOn)?

Happy to share the full implementation sketch or submit a PR if there's interest. Also totally understand if this isn't a direction you want to take the library - the wrapper works fine for my use case.

---

**Environment**: Gaia ECS v0.9.3
**Project**: Real-time voxel engine with ECS-based spatial data management
