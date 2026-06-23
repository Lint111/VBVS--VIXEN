---
title: Voxel Content-Format Contract & Smooth SDF Body Rendering
status: Design approved 2026-06-22 — Inc1 (Procedural) + Inc2 (Stored SDF) BUILT & rendering 2026-06-23; Inc3 (Materialization) + multi-channel = resume here
date: 2026-06-22
updated: 2026-06-23
tags: [architecture, voxel, sdf, content-format, rendering, smoothing, body-octree]
aliases: [SDF Body Rendering, Smooth Voxel Bodies, Content Format Contract, Voxel Provider Contract]
related:
  - "[[RenderGraph-System]]"
  - "[[Auto-Sync-FrameGraph-Design-2026-06]]"
  - "[[Stored-SDF-Provider-Inc2-Plan-2026-06]]"
  - "libraries/VoxelData/VOXELCONFIG.md"
---

# Voxel Content-Format Contract & Smooth SDF Body Rendering

> **Status (2026-06-23).** The SDF channel of the contract is **functional end-to-end**:
> **Inc1 (Procedural)** merged to `main` (`6c8b3cef`); **Inc2 (Stored SDF)** done on branch
> `feat/stored-sdf-provider-impl` (M6 `d03ceca2`) — Stored-SDF bodies render SOLID (smooth +
> displaced spheres, no holes) verified on the real shader (lavapipe offscreen + live VIXEN.exe
> path: ShaderManagement compiles, bindings 11/12 dispatch). **Two design points changed in the
> build — see the ⚠️ callouts in §3.2, §4, §7:** Stored rendering reuses the ESVO octree
> traversal (not a standalone march), and the Stored storage model is **8³ bricks + cross-brick
> fetch + a 1-brick dilation margin** (NOT the 10³ apron originally proposed). **Resume point:**
> Inc3 (Materialization) and extending the contract beyond the `sdf` channel to multi-channel
> (`material`/`color`/`normal`/PBR) for arbitrary content packs (§3.1, §8).

## 1. Context & Problem

The live render path (`shaders/BodyInstanceRayMarch.comp`, dispatched by
`BodyOctreeSceneNode`) renders body instances as **hollow shell octrees of binary
voxels**. The default standalone scene is three bodies that are *literally unit
spheres* voxelized into shells. They look blocky and speckled, not spherical. Two
independent root causes, both confirmed in code:

1. **Faceted shading (dominant cause).** `BodyInstanceRayMarch.comp:289-299` derives
   the surface normal from whichever **cube face** the ray hit — only ±X/±Y/±Z, six
   possible normals. Lighting is therefore computed as if every surface point were a
   flat axis-aligned cube face. Even an arbitrarily dense sphere shades like a disco
   ball under this. This is ~80% of the "not spherical" perception.

2. **Blocky silhouette + surface holes/speckle.** `ShellVoxelizer.h::ShellVoxels()`
   builds a ~1-voxel-thin binary-occupancy shell (cells whose centre is within
   √3⁄2·cellsize of radius 1). Thin shells leak — a ray can pass between two voxels
   that touch only at an edge/corner — and the brick-DDA boundary rejection
   (`marchBrickInstanced`, `BodyInstanceRayMarch.comp:242-249`) drops additional
   single pixels at brick seams. The silhouette also stair-steps because the surface
   is literally cubes.

### Goal

Bodies (planets, stars, and **arbitrary voxel models**) should render smoothly —
spherical things look spherical — and the smoothness data should arrive **from the
content/sim package (UNDERTOW)** in a **type-declared format the renderer dispatches
on**, supporting multiple formats over time (SDF first; scalar/RGB/PBR channels
later) per the needs of each content pack.

## 2. Key Discovery — the contract already exists CPU-side

The "config struct that declares attached data types and naming, so the renderer
reads the declared layout and handles processes/variations accordingly" is already
implemented as the **`VoxelConfig` / `AttributeRegistry` system**
(`libraries/VoxelData/`, `VOXELCONFIG.md` — status: Implemented ✅):

```cpp
VOXEL_CONFIG(StandardVoxel, 3) {
    VOXEL_ATTRIBUTE(DENSITY,  float,     0, true);   // key → octree structure
    VOXEL_ATTRIBUTE(MATERIAL, uint32_t,  1, false);
    VOXEL_ATTRIBUTE(COLOR,    glm::vec3, 2, false);
}
```

- `AttributeDescriptor { name, AttributeType type, defaultValue, index, isKey }` —
  named + typed channel declaration, with byte sizes/component counts (so GPU packing
  offsets derive automatically).
- Enumerable/reflective: `getAttributeNames()`, `getAttributeIndex()`,
  `hasAttribute()`, `getStorage()`.
- Observable: `IAttributeRegistryObserver` (`onAttributeAdded/Removed/KeyChanged`) —
  "read **and update** handling when the layout changes" is first-class.
- Pre-built configs already include `BasicVoxel` ("for simple **SDF** scenes"),
  `RichVoxel` (+`NORMAL`/`METALLIC`/`ROUGHNESS`), `CompactVoxel` (material-only).
- Ingestion (`VoxelInjector`) is already **data-driven** over
  `brick.getAttributeNames()`.

**The gap is narrow and specific:** this schema is honored CPU-side but **collapsed at
the GPU boundary** — `VoxelSceneCacher` packs only a material byte into `brickData[]`
and ignores the declared layout, and `BodyInstanceRayMarch.comp` hardcodes "material
byte + cube-face normal." The pure fix is to **propagate the existing `VoxelConfig`
layout across the GPU boundary**, not to invent a parallel format registry (which
would duplicate `VoxelConfig`).

## 3. The Contract

### 3.1 Declared channel schema (reused)

`VoxelConfig`/`AttributeRegistry` *is* the contract. A content pack declares its
voxel channels (names + `AttributeType`). The renderer reads this declared layout and
dispatches handling by the **presence and semantics of named channels** (`sdf`,
`material`, `color`, `normal`, …). New *layouts of known semantics* are data-driven;
new *semantics* require shader support (a known-channel vocabulary).

### 3.2 Per-channel Provider (the new axis)

A channel's declaration says *what* it is; a **Provider** says *where its values come
from*. This is the central new abstraction and it unifies baked and analytic content
behind one renderer:

| Provider | Storage | Shader field eval | Sparse-tree role |
|---|---|---|---|
| **Stored** (baked) | SoA bricks (+1-voxel apron) | fetch + trilinear | octree gives empty-space skipping; march existing bricks |
| **Procedural** (recipe) | none — recipe id + small param block | evaluate `f(p)` analytically (sphere-trace) | no bricks; distance itself skips empty space; octree optional, bounds-only |
| **Materialized** | starts Procedural → baked to Stored on demand | switches to Stored once baked | bake populates bricks, then behaves as Stored |

**Shared downstream shading.** Both providers expose one `evalSDF(p)` (and its
gradient). The iso-surface root-find, gradient normal, and lighting are **identical**
regardless of source — only field *evaluation* branches by provider. One SDF
renderer, two field sources, not two pipelines.

### 3.3 GPU layout descriptor

Derived from the declared `VoxelConfig`, a compact per-body/per-octree descriptor
carries: channel semantic + `AttributeType` + provider kind + provider data
(brick/channel offsets for Stored; recipe id + params for Procedural). It lives in the
free tail of the 256-byte `OctreeConfig` UBO (bytes ≥200, currently padding) and/or a
small parallel recipe SSBO. The shader reads it to select the field-eval path.

## 4. Storage model for the Stored provider — SoA bricks

The sparse octree-of-bricks keeps **sparsity at brick granularity**; how a voxel's
channels are laid out *inside* a brick is independent. We choose **per-brick SoA**:
channels stored as separate contiguous sub-arrays within each brick, bricks
concatenated in a pool.

Rationale (vs AoS / global-SoA):
- The dominant smooth-render access pattern samples **one channel many times** — a
  trilinear iso-surface hit + gradient reads the `sdf` channel ~14× per hit, while
  `material`/`color` are read once at the final hit. SoA makes those samples
  contiguous; AoS would drag the whole voxel record per neighbour fetch, wasting
  bandwidth.
- The **brick stays the unit of sparsity, allocation, streaming, residency, and
  apron** — what a content-pack-fed sparse renderer wants.
- Mirrors the CPU `AttributeStorage` (already SoA; Vec3 = 3 separate float arrays), so
  CPU→GPU packing is a near-direct copy.

**Apron (halo).** Store each channel at 10³ instead of 8³ (a 1-voxel border duplicated
from neighbour bricks at bake time) so trilinear filtering and gradients never cross a
brick boundary — no octree re-descent at seams, no seam artifacts. Cost: ~2× voxels
per brick (1000 vs 512). Alternative (deferred): 8³ + cross-brick fetch at faces
(less memory, more shader complexity).

> **⚠️ As-built (Inc2, 2026-06-23): the deferred alternative was chosen — 8³ bricks +
> cross-brick fetch, no 10³ apron.** Bricks stay 8³ (512 floats/channel); the GPU
> trilinear stencil reads neighbour bricks at faces via the grid→brick lookup table
> (`brickLookup[]`, binding 12), resolving each corner to its owning brick (or a `1e9`
> sentinel for unallocated neighbours, which the march probes through). To keep those
> face stencils reading honest data, the **bake dilates the active brick set by one brick**
> and **fully populates every active brick with true SDF** — i.e. seam-correctness is
> achieved by a *brick-level* margin in the data, not a *voxel-level* apron in storage.
> Net memory ≈ band-shell + 1-brick margin, no per-brick 10³ inflation. If multi-channel
> Stored content later shows seam artifacts on `color`/`normal`, revisit the 10³ apron
> for those channels. The §4 addressing formula still applies with `apron = 0`.

Addressing (all schema-derived → fully data-driven):
```
voxelsPerBrick = (8 + 2·apron)³                      // 10³ with apron=1
brickStride    = Σ_channels (elemSize_c · voxelsPerBrick)
channelBase_c  = Σ_{c'<c} (elemSize_c' · voxelsPerBrick)
value(c, brick b, voxel v) = brickPool[ b·brickStride + channelBase_c + v·elemSize_c ]
```

## 5. Procedural provider

A body carries a small **recipe** (analytic SDF program) + parameter block instead of
voxel storage. The shader evaluates `f(p)` directly and sphere-traces within the
body's bounds (AABB from `worldPos` + `renderScale`); the distance field provides
empty-space skipping inherently, so no octree/brick storage is needed.

- **Recipe library (bounded, first cut):** sphere, displaced-sphere
  (`length(p−c) − r + amp·fbm(p·freq)`), box, and a couple of CSG combinators
  (union / smooth-union / subtract). Most celestial content fits: a planet *is* a
  displaced sphere; a star is a sphere + corona.
- **Normal:** gradient of `f` via central differences (or analytic) → smooth at any
  zoom, **zero holes by construction**.
- Result for the live 3-body scene: true smooth spheres at near-zero memory — fixes
  both root causes completely because there is no voxelization to be blocky or leak.

## 6. Materialization (Procedural → Stored)

When content must become **editable at playtime** (destruction, deformation, carving),
bake the recipe into SoA bricks and flip the provider to Stored. The trigger machinery
already exists: `AttributeRegistry`'s `IAttributeRegistryObserver` +
`VoxelInjector::onKeyChanged()` rebuild hook is precisely the "field changed →
re-materialize" path.

## 7. Seam map (files that change)

| File | Role | Change |
|---|---|---|
| `libraries/VoxelData/` (`VoxelConfig`, `AttributeRegistry`) | declared schema | reuse as-is; it is the contract |
| `libraries/SVO/include/ShellOctreeGpu.h`, `BodyInstanceGpu` | per-body GPU record | add provider kind + provider data (recipe id/params, or brick/channel offsets) |
| ~~`libraries/CashSystem/VoxelSceneCacher`~~ → **`libraries/SVO/include/ShellOctreeGpu.h` (`SerializeSdf`/`ConcatenateSdf`)** | CPU→GPU brick build (BODY path) | **As-built (Inc2):** bodies serialize via `ShellOctreeGpu`, not `VoxelSceneCacher` (that is the VoxelGridNode/world path). `SerializeSdf` emits the SoA-SDF brick pool + grid→brick lookup + descriptor. Bake (`SdfBake.h`) fully populates active+dilated bricks with true SDF. |
| `OctreeConfig` UBO (`ShellOctreeGpu.h`) | per-octree GPU config | free tail carries the descriptor: `formatId`@200 / `bricksPerAxis`@204 / `sdfBrickArrayBase`@208 (432-B std140, ArrayStride 432) |
| `shaders/BodyInstanceRayMarch.comp` | render | provider branch → `evalSDF(p)`; shared iso-surface root-find + gradient normal; replaces cube-face normal |
| `libraries/RenderGraph/.../BodyOctreeSceneNode` | scene node | accept provider-tagged bodies; route Procedural vs Stored |
| `application/main/source/graph/BuildRenderGraph.cpp` | 3-body default scene | seed Procedural sphere/star bodies for the live gate |

## 8. Increment plan

The contract is **designed** for all three providers up front; only Increment 1 is
**built** first. This matches the project's established Design-doc + per-increment-Plan
pattern (cf. Auto-Sync FrameGraph) and the live-gate-authoritative lesson.

1. **Increment 1 — Procedural SDF provider. ✅ DONE — merged to `main` (`6c8b3cef`, 2026-06-22).**
   Contract scaffolding (provider tag in the per-body record + shader dispatch) + a small recipe
   library + shared iso-surface/gradient shading. The 3-body scene's planets/stars are true smooth
   spheres. **No storage work.** Fast, decisive on-screen win.
2. **Increment 2 — Stored SDF provider. ✅ DONE — branch `feat/stored-sdf-provider-impl` (M1–M6,
   M6 `d03ceca2`, 2026-06-23; not yet merged).** Plan: [[Stored-SDF-Provider-Inc2-Plan-2026-06]].
   Serves baked SDF content. **⚠️ As-built differs from this design's first sketch (deliberately):**
   - **Storage = 8³ bricks + cross-brick fetch + a 1-brick DILATION margin, NOT a 10³ apron** (§4).
   - **The body serializer is `ShellOctreeGpu::SerializeSdf`, NOT `VoxelSceneCacher`** (§7) — bodies
     are serialized per the `ShellOctreeGpu` path, with the GPU layout descriptor in the
     `OctreeConfig` tail (`formatId`@200, `bricksPerAxis`@204, `sdfBrickArrayBase`@208; 432-B std140).
   - **Rendering reuses the ESVO octree traversal** (`traverseOctreeInstanced`), swapping only the
     leaf hit-test to a bounded trilinear march (`handleLeafHitInstancedSdf`→`marchBrickSdf`) — it does
     NOT run a standalone sphere-trace. The original flat `marchStoredSdf` was built first, produced
     POV-dependent brick holes, and was retired in M6 (§3.2).
   - **Bake invariant:** narrow-band sparsity is at the BRICK level — active bricks are FULLY
     populated with true SDF (storing only band voxels left the rest 0.0 → false iso-surfaces).
3. **Increment 3 — Materialization. ▶ RESUME HERE.** Procedural→Stored bake on edit, via the
   `AttributeRegistry` observer/rebuild hook (§6). Plus the still-open contract breadth below.

**Still-open contract work (resume alongside Inc3):** only the **`sdf` channel** is built end-to-end.
The full per-channel contract (§3.1) — declaring + GPU-propagating + shading **`material`/`color`/
`normal`/PBR** channels from arbitrary content packs (UNDERTOW) — is designed but not implemented.
The Stored path currently carries a material brick + palette alongside the SoA-SDF; extending it to
the general schema-derived multi-channel SoA pack (§4 addressing) is the next contract increment.

### Increment 1 scope (immediately actionable)

- Add a `providerKind` (+ `recipeId` + small param block) to the per-body GPU record
  (`BodyInstanceGpu` and/or `OctreeConfig` tail).
- Shader: in the per-instance loop, branch on `providerKind`. Procedural → sphere-trace
  the recipe within the body AABB; Stored → existing ESVO path (unchanged).
- Implement `evalSDF(p)` + central-difference gradient for the recipe library; feed the
  existing `computeLighting()` with the smooth gradient normal.
- Seed the default 3-body scene with Procedural bodies (displaced-sphere planets +
  a star) so the live gate shows the win.
- Keep the existing Stored/ESVO path intact and selectable (no regression to current
  shell bodies).

## 9. Testing strategy

- **Live gate is authoritative** for the shader/render result — run the standalone app
  and confirm smooth spheres + 0 syncval (the project's repeatedly-proven rule for GPU
  work). Capture before/after.
- **CPU/GPU parity** for recipe eval: unit-test the `evalSDF` recipe library against a
  CPU mirror (cf. existing `GpuTraversalMirror` pattern) so the analytic field matches
  on both sides.
- **No-regression:** the existing ESVO/shell bodies still render (Stored path
  untouched in Inc 1).
- Increment 2 adds: SoA-brick pack round-trip tests, apron-correctness (gradient
  continuity across brick seams), schema-driven stride/offset tests.

## 10. Rejected alternatives

- **New parallel format-enum registry** — duplicates the existing `VoxelConfig` schema
  system; rejected in favour of extending `VoxelConfig` across the GPU boundary.
- **Baked per-voxel normals as the interchange** — bloats the content format (a normal
  per voxel), awkward for a sim to emit, and fixes neither silhouette nor holes by
  itself. (Optional `normal` channel may exist later via the same contract, but it is
  not the smoothing mechanism.)
- **Stay binary + screen-space smoothing (depth-normal reconstruction / TAA)** —
  band-aid; shading stays fundamentally faceted and it fights the content-format goal.
  Violates the project no-band-aid rule.
- **AoS bricks** — compatible with sparsity but wastes bandwidth on the single-channel
  (SDF) neighbour sampling that dominates smooth rendering; rejected in favour of SoA
  bricks.

## 11. Open decisions

- ~~Apron-on from the start vs apron-off-first at Increment 2 (memory vs seam artifacts).~~
  **RESOLVED (Inc2, 2026-06-23): apron-off** — 8³ bricks + cross-brick fetch, seam-correctness via
  a 1-brick bake dilation + full-brick SDF (§4 as-built). Revisit per-channel 10³ apron only if
  multi-channel seams appear.
- ~~Exact home of the GPU layout descriptor (inline in `OctreeConfig` tail vs a parallel SSBO).~~
  **RESOLVED (Inc2): inline in the `OctreeConfig` tail** — `formatId`@200 / `bricksPerAxis`@204 /
  `sdfBrickArrayBase`@208, in the 432-byte std140 `Vixen::SVO::OctreeConfig` (SPIR-V ArrayStride 432).
- **(still open)** Recipe representation generality: fixed primitive+CSG library (first cut) vs a small
  SDF-program/bytecode interpreter (Dreams-like) if content demands arbitrary CSG.
- **(new, for the resume)** Multi-channel Stored pack: the SoA addressing (§4) is schema-derived but
  only `sdf` (+ a legacy material brick/palette) is wired today. Decide how `material`/`color`/`normal`
  channels ride the same SoA pool + descriptor when extending past the SDF channel.
