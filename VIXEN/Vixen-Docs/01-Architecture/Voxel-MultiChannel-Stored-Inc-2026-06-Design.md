---
title: Generic Multi-Channel Stored Provider — Design (pre-materialization)
status: Design — approved 2026-06-23
date: 2026-06-23
tags: [architecture, voxel, sdf, stored-provider, soa-bricks, multi-channel, content-format]
aliases: [Multi-Channel Stored, Generic Channel Pool, SoA Channel Pool, Multi-Channel SDF]
related:
  - "[[Voxel-Content-Format-Contract-Design-2026-06]]"
  - "[[Voxel-Stored-SDF-Provider-Inc2-Design-2026-06]]"
  - "[[Stored-SDF-Provider-Inc2-Plan-2026-06]]"
  - "libraries/VoxelData/VOXELCONFIG.md"
---

# Generic Multi-Channel Stored Provider — Design

## 1. Context & goal

Inc2 (Stored SDF, M6 `d03ceca2`) renders Stored bodies solid by reusing the ESVO
traversal with a bounded trilinear leaf march. But it carries only **one per-voxel
channel** (`sdf`, a dedicated `sdfData[]` SSBO) plus a legacy per-voxel **material id**
(→ 64-entry palette); per-voxel `color`/`normal`/PBR are declared in the schema
(`VoxelComponents.h`, `RichVoxel`) but never serialized — color is palette-only.

**Goal:** generalize the Stored path to a **schema-derived multi-channel SoA pack** that
the shader reads **by semantic**, so the next increment (Materialization,
`[[Voxel-Content-Format-Contract-Design-2026-06]]` §6) bakes Procedural→Stored into ONE
generic format from the start, not a SDF-only one that needs retrofitting.

This realizes the contract design's §3.2 Provider + §4 SoA-pool model for the Stored
provider. It sequences **before** Materialization deliberately (user decision 2026-06-23).

## 1.1 Format ownership — the format is VIXEN's; a game is a consumer, not an owner

VIXEN **owns the rendering pipeline** — it **declares** the schema and **renders** it — so the
content format is a **VIXEN engine abstraction**, game-agnostic. A game (e.g. UNDERSET) is a
**consumer of the VIXEN pipeline, not an owner of it**: it **conforms** to VIXEN's format, it does
not define or shape it. The format must therefore represent VIXEN concepts, never consumer-specific
ones (no "planet"/"star"/game-entity semantics in the format — only generic voxel channels).
Concretely:

- The **channel-semantic vocabulary** (`SEM_*`) and **field-kind** (`FK_*`) enums are owned by
  VIXEN and defined **once in a VIXEN engine header** (CPU, e.g. alongside `VoxelData`/SVO),
  **mirrored 1:1 in GLSL** (the same single-source-of-truth pattern Inc1 used for
  `SdfRecipes.h`↔`SdfRecipes.glsl`). They are the engine's reusable vocabulary, not a content
  pack's.
- The **descriptor format** (the `OctreeConfig` channel table) is VIXEN's GPU contract; a
  conforming pack supplies channel *values* via the declared `VoxelConfig`/`AttributeRegistry`
  schema — VIXEN derives the GPU layout. A pack never writes raw GPU offsets.
- Adding a *new semantic* is a VIXEN engine decision (it extends the vocabulary + shader);
  a pack only chooses *which existing semantics* its content carries.

Net: any VIXEN-based game gets the same content format; UNDERSET is merely one consumer of it.

## 2. Scope

**Wired end-to-end this increment** (proves the generic machinery across two types):
- `sdf` — Float, declared `FK_DISTANCE` (the key channel; migrates from the dedicated
  `sdfData[]` into the pool).
- `color` — Vec3 (per-voxel, replaces the palette lookup for Stored bodies).
- `roughness` — Float (per-voxel, feeds roughness-aware lighting).

**Designed-for but deferred** (the framework handles them with no new serialize/addressing
code; just add the channel + a shader semantic): `normal`, `metallic`, `emission`, and
**`FK_DENSITY` volumetric fields** (the descriptor declares field kind now — §4 — but only
`FK_DISTANCE` is rendered this increment).

**Out of scope:** materialization; binary-path / material-palette changes (untouched);
real content authoring (the bake synthesizes spatially-varying channels to test the path).

## 3. The generic SoA channel pool (storage)

One SSBO (reuse **binding 11**) holds every channel of every brick. Per brick, channels
are stored **SoA, back-to-back**:

```
elemCount(Float)=1, elemCount(Vec3)=3            // components per voxel
voxelsPerBrick = 512                              // 8^3, no apron (Inc2 as-built)
brickStrideFloats = Σ_c (elemCount_c · 512)
channelBaseFloats_c = Σ_{c'<c} (elemCount_c' · 512)
// localBrick = brick index within THIS octree (from brickLookup[]);
// poolBrickBase = this octree's float base in the concatenated pool (§4 descriptor).
value(channel c, localBrick, voxel v, comp k)
    = pool[ poolBrickBase + localBrick·brickStrideFloats + channelBaseFloats_c + k·512 + v ]
```

For `{sdf, color, roughness}`: `brickStride = (1+3+1)·512 = 2560` floats;
`channelBase` = sdf 0, color 512, roughness 2048. **SDF is channel 0** — the M6
`_sampleSdfVoxel` changes from `sdfData[sdfBase + brick·512 + v]` to
`pool[brick·2560 + 0 + v]`. Sparsity stays at brick granularity (the M6 brick-level
narrow band + 1-brick dilation + full-brick population invariant applies **per channel** —
every active brick stores all channels for all 512 voxels).

The grid→brick lookup (`brickLookup[]`, binding 12) is unchanged — it maps a brick-grid
coord to a brick index; the pool addressing above turns that index into a per-channel
float offset.

## 4. Descriptor table (in the OctreeConfig tail)

Replace the single `sdfBrickArrayBase`@208 with a compact per-channel table in the 432-B
`OctreeConfig` std140 tail (≈232 B / ~14 uvec4 free). Layout (uvec4-packed to satisfy
std140 array stride):

```
formatId            @200  uint   (= STORED_SDF, unchanged)
bricksPerAxis(Sdf)  @204  uint   (unchanged)
poolBrickBase       @208  uint   // float-element base of THIS octree's bricks in the pool
channelCount        @212  uint
brickStrideFloats   @216  uint
// then channelCount entries, each {semanticId:uint, elemCount:uint, channelBaseFloats:uint, fieldKind:uint}
channels[0..N]      @224… uvec4 each   // N ≤ ~12 within the tail
```

`semanticId` is a small enum mirroring the known channel vocabulary
(`SEM_SDF=0, SEM_COLOR=1, SEM_ROUGHNESS=2, SEM_NORMAL=3, SEM_METALLIC=4, SEM_EMISSION=5,
SEM_DENSITY=6`). The shader scans the table for the semantic it needs and reads its
`channelBaseFloats`. A C++ `static_assert` keeps the table within the 432-B struct;
offset asserts as in M6.

**`fieldKind` — how a SCALAR field is interpreted (the data declares it, not the renderer).**
A scalar channel can mean different things, and that choice selects the rendering algorithm:
```
FK_NONE     = 0   // not a field (color, roughness, …) — no integration semantics
FK_DISTANCE = 1   // signed-distance field → sphere-trace to the zero iso-surface (M6 path)
FK_DENSITY  = 2   // density field → integrate/accumulate along the ray (volumetric; future)
```
This resolves the current latent confusion that the `density` component literally stores a
*distance* today — interpretation now comes from `fieldKind` in the descriptor, not the
component's name. **This increment emits only `SEM_SDF` with `FK_DISTANCE`** (the surface
path). `FK_DENSITY` is reserved: a future volumetric provider declares a density channel and
the shader dispatches an accumulation march instead of (or in addition to) the iso-surface
march. **A body may carry BOTH** — e.g. an `FK_DISTANCE` surface channel + an `FK_DENSITY`
halo channel — since the table lists each channel with its own kind; the renderer runs the
surface march and/or the volumetric integration per the kinds present. Cost now: one extra
uint per channel entry (already the 4th uvec4 lane), zero shader work beyond reading it.

## 5. CPU pack — `SerializeSdf` generalized (schema-driven)

Drive the pack off the body's `VoxelConfig`/`AttributeRegistry` (the contract):
1. Determine the channel set + order from the registry (declared attributes mapped to
   known semantics). SDF is the `density` key channel.
2. Allocate the pool: `numActiveBricks · brickStrideFloats`.
3. For each active brick, for each voxel in `z·64+y·8+x` order, for each channel: read the
   voxel's component (`getComponentValue<Density/Color/...>`) and write it at
   `channelBaseFloats_c + k·512 + v`. Missing component → the attribute's default.
4. Emit the descriptor table (channel semantics/elemCount/base) + `brickStrideFloats`.

`ConcatenateSdf` concatenates pools across octrees with per-octree `poolBrickBase`.
Adding a future channel needs **no** serialize edit — it falls out of the registry scan.
The M6 brick-level full-population + dilation is applied to the whole pool (all channels).

**Bake (`SdfBake.h`) for the test:** synthesize spatially-varying channels so per-voxel
variation is observable — e.g. `color = f(gridPos)` (a smooth gradient / banding) and
`roughness = g(gridPos)`. (Real content authoring is later; this proves the pipeline.)

## 6. Shader — generic, known-vocabulary reads

- A generic `int channelBase(uint semanticId)` scans the descriptor table; returns the
  channel's `channelBaseFloats` or `-1` (absent → caller uses a default).
- `float sampleChannelTrilinear(uint sem, vec3 gridPos)` and a vec3 variant: same 8-corner
  trilinear gather as M6's `sampleSdfTrilinear`, but addressing
  `pool[brick·brickStride + base + comp·512 + v]`. `_sampleSdfVoxel` is refactored to call
  the generic reader with `SEM_SDF` (SDF march logic unchanged).
- At the leaf hit (`handleLeafHitInstancedSdf`): after the iso-surface hit point, sample
  `SEM_COLOR` (default white) and `SEM_ROUGHNESS` (default ~0.5) trilinearly; output them.
  Normal stays the SDF gradient.
- Absent channels resolve to defaults, so a `{sdf}`-only or `{sdf,color}` body still works.

## 7. Lighting — roughness-aware

`computeLighting` gains a `roughness` parameter; base color is the per-voxel `color`
channel (Stored bodies no longer tint a palette grey). Roughness modulates the specular
term (a minimal GGX-ish or Blinn-Phong exponent map) — enough to make the channel visibly
affect shading. Binary/Procedural lighting paths keep their current behavior (roughness
defaulted).

## 8. Seam map (files that change)

| File | Change |
|---|---|
| `libraries/SVO/include/ShellOctreeGpu.h` | `OctreeConfig` tail → per-channel descriptor table; `SerializeSdf`/`ConcatenateSdf` → schema-driven SoA pool pack (replaces dedicated `sdfBricks`); `static_assert`s |
| `libraries/SVO/include/SdfBake.h` | bake synthesizes varying `color`/`roughness` (test content); full-brick population now writes all channels |
| `shaders/StoredSdf.glsl` | generic pool addressing; `channelBase`/`sampleChannelTrilinear`; `_sampleSdfVoxel` via the generic reader |
| `shaders/BodyInstanceRayMarch.comp` | pool binding-11 declaration (was `sdfData[]`); `handleLeafHitInstancedSdf` samples color+roughness; descriptor-table fields in GLSL `OctreeConfig` |
| `shaders/Lighting.glsl` | `computeLighting` roughness param |
| `libraries/RenderGraph/.../BodyOctreeSceneNode*` | rename the SDF buffer output to the channel-pool (binding 11) — same wiring, generalized name/contents |
| tests | extend `test_soa_sdf_serialize` (schema-derived offsets/round-trip for sdf+color+roughness); extend `test_body_instance_raymarch_render` (`RenderStoredSdfBodiesNoHoles` → also assert per-voxel color gradient + roughness shading) |

## 9. Testing

- **Live/offscreen gate (authoritative):** the lavapipe render test bakes a sphere with a
  spatial color gradient + varying roughness; assert (a) SDF still solid (fillRatio 1.0,
  no regression), (b) the color **gradient is visible** (not a flat tint — e.g. sample
  pixels across the disk and confirm the channel varies), (c) roughness changes shading.
  Inspect the PNG.
- **CPU pack round-trip:** `test_soa_sdf_serialize` — for sdf+color+roughness, assert
  `brickStrideFloats`, each `channelBaseFloats`, and a known voxel's per-channel values
  match the schema + baked components.
- **No-regression:** binary `RenderRealShaderNearView` + `RenderMultiKindBodies`; single-
  channel `{sdf}` body still renders (defaults for absent channels).

## 10. Why this shape

- **One pool + descriptor table** = materialization bakes one uniform format; the shader
  is decoupled from the channel count (scan-by-semantic), mirroring the contract §4.
- **Schema-driven pack** = new channels are data, not code (the contract's whole point).
- **SDF as channel 0** (not a special buffer) = no two-format split for materialization.
- **Known-vocabulary semantics** = the shader must know what to *do* with a channel
  (color→albedo, roughness→specular), so fully-arbitrary channels aren't useful; the table
  declares *presence + offset*, the shader supplies *meaning*.

## 11. Out of scope / deferred

- `normal` (SDF gradient suffices for SDF bodies; per-voxel normal is for non-SDF content),
  `metallic`, `emission` — framework-ready, not wired.
- **Volumetric / density fields (`FK_DENSITY`)** — the descriptor now *declares* field kind
  (§4), but only `FK_DISTANCE` is rendered this increment. A future volumetric provider adds
  a density channel + a ray-accumulation march (and the surface+volume combination, e.g. a
  star's body + corona, a planet + atmosphere). Designed-for via `fieldKind`; not wired.
- Materialization (next increment) — bakes Procedural→Stored into this pool via the
  `AttributeRegistry` observer hook.
- Per-channel 10³ apron (only if multi-channel seams appear; cross-brick fetch is the
  Inc2 as-built default).
- Removing the legacy material-id/palette from the binary path (untouched here).
