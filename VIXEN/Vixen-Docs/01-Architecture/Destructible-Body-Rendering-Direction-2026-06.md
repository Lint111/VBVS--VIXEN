---
title: Destructible / Deformable Body Rendering — Direction
aliases: [Materialized Provider, Incremental Voxel Edit, Provider-LOD, Destructible Bodies, Path B]
tags: [architecture, rendering, voxel, sdf, provider, materialization, future, game-renderer, AR41, AR48]
created: 2026-06-29
status: FUTURE
---

# Destructible / Deformable Body Rendering — Direction

> [!summary] 💡 FUTURE — direction captured, **not scheduled** (post-MVP)
> The substrate already exists; this doc records *how the pieces compose* into a runtime
> destructible/deformable render capability, so nothing in the MVP forecloses it. Nothing here
> is on a schedule. Engine-side only — VIXEN owns the capability; a consumer supplies the content
> and the edits (see [[vixen-owns-content-format-not-consumer]] in spirit: keep this game-agnostic).

## The capability

A VIXEN-rendered body should be able to **change shape at runtime** under consumer/player action —
carved, deformed, ablated — and have that change **persist and keep accumulating**, without a full
per-edit rebuild or a GPU stall. This is the engine enabler for any consumer that wants destructible,
deformable, or mineable geometry (combat damage, terrain deformation, excavation).

## This is one axis of the existing provider model — not a new pipeline

[[Voxel-Content-Format-Contract-Design-2026-06]] already defines three per-channel **providers** that
share one downstream `evalSDF(p)` + gradient:

- **Procedural** — recipe evaluated in-shader, no storage. (Inc1, DONE)
- **Stored** — baked SoA voxel bricks, trilinear iso-surface fetch. (Inc2/Inc3, DONE)
- **Materialized** — Procedural→Stored bake on edit. (named; partially built)

Destructible rendering is the **Materialized** axis taken to its real-time conclusion: a body starts
**Procedural** (cheap, just a recipe), is **materialized into a Stored voxel volume on first edit**,
then **incrementally mutated** thereafter.

## Provider-LOD lifecycle (the synthesis)

| Range / state | Provider | Per-frame cost |
|---|---|---|
| far / map | instance blob | trivial |
| approached, intact | **Procedural** recipe (composable — see [[SDF-Recipe-Kernel-Codegen-Inc4-2026-06-Design]]) | shader-only |
| under active edit | **Materialized → Stored**, then **incrementally carved** | per-edit delta |

A body is **promoted on demand**. You never choose "voxels" globally — only the elements actually being
reshaped pay the Stored/voxel cost; everything else stays a Procedural instance.

## What exists vs. the gap

- ✅ **Procedural + Stored** providers; **recipe composition** (CSG union/subtract/smooth + transforms +
  primitives) via the kernel-codegen catalogue (Inc4 P2.4, on `main`).
- ✅ **Materialize-on-edit as a full re-bake** — `BodyOctreeSceneNode::SetBakeRecipe` → `Rematerialize`
  (`vkDeviceWaitIdle` + whole-octree re-upload). Correct for an *editor* edit; **too coarse for
  real-time destruction** (a stall per shot).
- ❌ **The real-time leg** — incremental dirty-region edit + GPU **delta** upload — is unbuilt. It is
  precisely the two open P4 items in [[Maturation-Backlog-2026-06]]:
  - **[AR#41]** SVO incremental update — `LaineKarrasOctree::updateBlock()` is implemented but unwired
    and never patches `ChildDescriptors`, so mutations are invisible until a full `O(world)` `rebuild()`.
  - **[AR#48]** sim→render change bridge — `GaiaVoxelWorld` emits no change events; `VoxelInjectionQueue`
    exists but is unwired; there is no GPU delta-upload leg.

## The build, when scheduled (P4)

1. **Dirty journal** on the field source — Morton-keyed change set + `GaiaVoxelWorld` change events. (AR#48)
2. **Incremental octree update** — wire `updateBlock` / dirty-subtree `ChildDescriptor` patching +
   per-brick recompression + double-buffered swap. (AR#41)
3. **GPU delta-upload** — push only changed bricks (the multi-channel `channelPool` is already per-brick
   addressable), never a full re-upload.
4. **Materialize trigger** — promote a Procedural body to Stored on first edit (reuse the existing bake;
   the new part is *incremental* thereafter).

## Consumer pattern (composition, not enumeration)

Consumers compose bodies from the **recipe vocabulary** (primitives + CSG + transforms) — linear in part
*types*, never in part *combinations*. Destruction is then either (a) **recipe-level** (swap or subtract
a part — cheap, stays Procedural) or (b) **volume-level** (carve the Materialized voxels — the AR#41/48
leg above). First motivating consumer: UNDERTOW's **modular ships** (a ship = CSG-union of module
sub-recipes; a destroyed module → capability loss in the sim + a geometry change in the render). See
undertow `docs/superpowers/specs/2026-06-29-modular-ship-destructible-representation-design.md`.

## Related

- [[Voxel-Content-Format-Contract-Design-2026-06]] — the three-provider model (the §-ownership + provider axes)
- [[SDF-Recipe-Kernel-Codegen-Inc4-2026-06-Design]] — the recipe-composition vocabulary
- [[Maturation-Backlog-2026-06]] — **AR#41** / **AR#48**, the P4 mechanics this direction depends on
