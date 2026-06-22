---
title: Stored SDF Provider — Increment 2 Design
status: Design — approved 2026-06-22
date: 2026-06-22
tags: [architecture, voxel, sdf, stored-provider, soa-bricks, cacher, increment-2]
aliases: [Stored SDF, SoA bricks, Increment 2, Voxel SDF baking]
related:
  - "[[Voxel-Content-Format-Contract-Design-2026-06]]"
  - "[[SDF-Body-Rendering-Inc1-Plan-2026-06]]"
  - "libraries/VoxelData/VOXELCONFIG.md"
---

# Stored SDF Provider — Increment 2 Design

## 1. Context

Increment 1 shipped the **Procedural** provider (analytic SDF recipes, no storage) —
smooth spheres, merged to main (`6c8b3cef`). This increment adds the **Stored** provider:
arbitrary voxel content carrying a per-voxel **signed-distance field**, rendered to a
smooth trilinear iso-surface. It is the path that serves real baked/editable content from
the UNDERTOW package; Inc1's recipes only cover parametric primitives.

Overarching architecture (the per-channel Provider contract, the `VoxelConfig`/
`AttributeRegistry` schema, the shared `evalSDF`/gradient/lighting) is the design of
record: `[[Voxel-Content-Format-Contract-Design-2026-06]]`. This doc refines the **Stored**
provider's tactical design and the decisions deferred there.

## 2. Resolved decisions

- **No apron — cross-brick sampling instead.** The contract doc floated a 1-voxel apron
  (duplicated halo) for seamless trilinear/gradient. Rejected: an apron duplicates border
  voxels into neighbors, so every edit near a brick face must re-sync the halo copies —
  bookkeeping that bites Inc3 (materialization) and live UNDERTOW edits. **Cross-brick
  sampling** keeps each voxel in exactly one brick (single source of truth): when a
  trilinear corner or gradient neighbor falls outside the current 8³ brick, resolve the
  neighbor brick via the existing **`VoxelSceneCacher::BuildBrickGridLookup`** grid→brick
  index (O(1), no octree re-descent) and read its SoA SDF voxel. "Forgiving SVO" becomes a
  **residency margin** (keep neighbor bricks resident even just out of frustum), not
  duplicated data — automatic for today's fully-resident small-body scenes, a policy only
  when streaming large scenes later. A sample landing in an unallocated (empty-exterior)
  brick reads a **far-positive sentinel**; the narrow-band SDF guarantees the iso-surface's
  own stencil is always resident, so SDF=0 is never affected.
- **Gate content = bake the Inc1 recipe → Stored bricks.** No real Stored content exists
  yet (UNDERTOW feeds it later). At setup, sample the Inc1 Procedural recipe (sphere /
  displaced-sphere) into the SoA-SDF Stored brick set and render via the new Stored path.
  This gives a **built-in correctness oracle** (Procedural vs Stored-baked-from-the-same-
  recipe should render identically) and pulls Inc3's bake core forward (de-risks
  materialization). File-based content-pack I/O is **deferred** (the cacher already has
  serialize/deserialize to build on when UNDERTOW integration lands).

## 3. Architecture (4 units)

### 3.1 Schema-driven SoA cacher
`VoxelSceneCacher` today binarizes per-voxel `Density` to a material byte
(`VoxelSceneCacher.cpp:568–598`). Change: when the declared schema has an `sdf` (signed-
distance Float) channel, pack the **actual** per-voxel signed distance into a **per-brick
SoA SDF buffer** — a new GPU buffer alongside the existing `brickData`, following the
established `compressedColorsBuffer` / `compressedNormalsBuffer` precedent
(`VoxelSceneCacher.cpp:796–801`). The binary `brickData`/material path is untouched for
non-SDF content. Emit a compact **layout descriptor** (channel present + element type +
intra-brick stride/offset + `formatId = STORED_SDF`) into the free `OctreeConfig._padding4[16]`
tail (bytes 192–255), which the shader reads to dispatch.

### 3.2 Bake path (setup-time, also Inc3's core)
A small helper samples a recipe (the Inc1 `SdfRecipes` library) over a body's brick grid,
writing the narrow-band signed distance into the SoA-SDF Stored brick set + the brick-grid
lookup. Narrow band = store SDF in voxels within ±N of the surface (N≈2), leaving exterior
bricks unallocated (sentinel). This is the function Inc3 will trigger on edit.

### 3.3 Shader Stored-SDF handler
A new brick handler (selected by `formatId`): instead of "first occupied voxel = hit"
(binary DDA), march the ray and at each step sample the **trilinearly-interpolated** SDF
(8 corners, cross-brick via the grid-lookup) until it crosses zero, then refine to the
iso-surface; the **gradient** of the SDF (central differences, also cross-brick) is the
smooth normal. Output feeds the shared `computeLighting` (same downstream as Procedural).
The binary ESVO path and the Procedural branch are untouched; the handler is a third
per-body branch keyed on the layout descriptor's `formatId`.

### 3.4 Gate
Bake the default 3-body recipe into Stored bricks behind a flag (e.g. `VIXEN_STORED_SDF_DEMO`),
render via the Stored path, and A/B against the Procedural render: the silhouettes/shading
should match within tolerance, 0 syncval. Keep Procedural as the default scene.

## 4. Data model

- **SoA SDF brick:** per brick, a contiguous array of the SDF channel for its 8³=512 voxels.
  Element type = **`float`** for Inc2 (exact + simplest; `unorm16` compression is deferred —
  §7). Bricks concatenated in one GPU buffer with a per-octree base offset (the
  `nodeArrayBase`/`brickArrayBase` pattern).
- **Layout descriptor (in `OctreeConfig._padding4`):** `{ formatId, sdfChannelBaseOffset,
  sdfElemType, voxelsPerBrick }` — enough for the shader to address `sdf[brick][voxel]` and
  pick the Stored-SDF handler. Exact field packing decided in the plan (fits in ≤64 B tail).
- **Neighbor resolution:** `BuildBrickGridLookup` (grid coord → brick index) is uploaded so
  the shader resolves cross-brick samples in O(1). Missing entry → far-positive sentinel.

## 5. Seam map

| File | Change |
|---|---|
| `libraries/CashSystem/src/VoxelSceneCacher.cpp` (`BuildOctree`/brick-pack ~568–598, `BuildBrickGridLookup` ~681, buffer upload ~781–806) | schema-driven SoA-SDF packing + upload the SDF buffer + grid-lookup; write the layout descriptor into `OctreeConfig` |
| `libraries/CashSystem/include/VoxelSceneCacher.h` (`OctreeConfig` ~100–133) | define the layout-descriptor fields in the `_padding4` tail |
| `libraries/SVO/include/` (new) `SdfBake.h` | bake a recipe → SoA-SDF Stored bricks (reuses `SdfRecipes.h`) |
| `shaders/BodyInstanceRayMarch.comp` | Stored-SDF handler (trilinear iso-surface + gradient, cross-brick fetch); `formatId` dispatch |
| `shaders/` (maybe new include) `StoredSdf.glsl` | the trilinear-fetch + iso-surface march helpers |
| `application/main/source/graph/BuildRenderGraph.cpp` | `VIXEN_STORED_SDF_DEMO` gate scene (bake the 3 bodies) |

**Unchanged (must not regress):** binary ESVO path, the Procedural provider, Inc1 `SdfRecipes`.

## 6. Testing

- **Oracle (live gate):** Procedural vs Stored-baked-from-same-recipe render → match within
  tolerance; 0 syncval. Authoritative (`[[live-verification-authoritative-for-gpu-work]]`).
- **Unit (CPU, gtest):**
  - bake round-trip: recipe → SoA-SDF bricks → sample back ≈ recipe value within quantization.
  - SoA pack/offset math (schema-derived stride/offset).
  - cross-brick sampling: a CPU mirror of the trilinear+neighbor-lookup at a brick boundary
    matches the analytic field (no seam discontinuity); missing-neighbor → sentinel.
- **No-regression:** binary ESVO tests (`test_gpu_parity`, brick traversal) still pass.

## 7. Deferred (not Inc2)

- File-based content-pack I/O (format header + loader) — cacher serialize/deserialize exists
  to build on; lands with real UNDERTOW integration.
- Streaming residency policy (the 1-brick margin) — only needed when scenes exceed resident
  capacity; today everything is resident.
- Inc3 materialization (Procedural→Stored on edit) — uses §3.2's bake + the observer/rebuild
  hook.
- SDF channel compression (unorm16 vs float) — a plan-level decision; start with whichever is
  simpler and measure.

## 8. Rejected alternatives

- **Apron / duplicated halo** — rejected for the edit-sync cost (see §2); cross-brick
  sampling is the single-source-of-truth choice.
- **Octree re-descent for neighbor bricks** — rejected; the existing `BuildBrickGridLookup`
  gives O(1) neighbor resolution, no per-sample descent.
- **Hand-authored test SDF for the gate** — rejected; the recipe→bake oracle is stronger and
  advances Inc3.
