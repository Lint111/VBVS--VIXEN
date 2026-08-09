---
title: Deep-Field Policy Stencil & Grouped Handling — Two-Axis Per-Pixel Classification
status: Design (user direction 2026-08-09) — not scheduled; slice 1 scoped to replace the parked C3 cull patch
date: 2026-08-09
tags: [architecture, stencil, grouping, dispatch, regime, virtual, materialized, composite, traversal, deep-field]
aliases: [Policy Stencil, Regime Stencil, Group Dispatch, Source-Axis Stencil]
related:
  - "[[Deep-Field-Residency-Unification-2026-08]]"
  - "[[Deep-Field-Mip-Accessor-Policy-2026-08]]"
  - "[[Lazy-Procedural-Delta-Baseline-Design-2026-07]]"
  - "docs/plans/2026-08-04-wavefront-recipe-shading.md (undertow)"
  - "perf/batch50-secondbody/report.md (undertow, B50-T1-FU)"
  - "shaders/TraceWorld.glsl"
  - "shaders/HitRecord.glsl"
---

# Deep-Field Policy Stencil & Grouped Handling

> **User direction (2026-08-09, verbatim):** *"We can add in the pre pass a stencil information on
> which pixel relates to which policy then handle them in a grouped way — this stencil value is an
> escaping ray that goes into accumulative mode; this pixel hit a mip level < n or a brick node
> declaring it in the scale of opaque handling. This way we can also later add volumetric and
> translucent or transparent groupings easily."*
>
> **Scope extension (same session):** the stencil carries a **second, orthogonal axis — source
> policy** — as **two overlapping bit flags**, not an enum: VIRTUAL and MATERIALIZED, hybrid = both
> set. Each downstream pipeline portion tests **its own bit** independently. Purpose is
> pipeline-portion skipping: *"in the case an area is pure virtual there is no reason to run the
> materialized portion of the pipeline and vice versa."*

**Grounding.** This revision was checked against the WSL canonical engine at committed baseline
`86fd2ebf`, **including the batch-50 uncommitted working-tree diff** (the
`[CompositeBlend]` probe family and overlap scene). Line numbers are pinned to the *working tree*,
not to HEAD's committed blobs, wherever the two differ — the C3 probe sites in particular exist only
in the uncommitted diff. Measured numbers come from `perf/batch50-secondbody/report.md`.

---

## 0. What this document is, and the one thing it is not

This is a **materialization-and-dispatch design**. The classifier is not defined here:
[[Deep-Field-Residency-Unification-2026-08]] §B, "Unification — one footprint→regime function,
two consumers," already defines the single `FootprintRegime(worldDist, cellWorldSize,
raySizeCoef, raySizeBias, cosmicK)` function and its Surface/MipHit/Cosmic results. This design
adds the stencil as that function's **third consumer**: evaluate the same function, materialize its
terminal result per pixel, and let downstream consumers read the byte rather than re-derive policy.

It is **not** a proposal to change what any regime *does*. Regimes 1/2/3 keep the semantics
`Deep-Field-Mip-Accessor-Policy-2026-08.md` already ratified. The only behavioural change this
design proposes is §D — and that change is a *group-scoped relaxation of one cull*, which is
exactly the fix direction the C3 follow-up already identified and parked for a user ruling
(`perf/batch50-secondbody/report.md:484`).

The invariant is: **one classifier, three consumers** — entry dispatch, residency, and stencil.
Any future threshold or result change is an amendment to `FootprintRegime` in the residency design
and its CPU/GLSL twins, never a stencil-local classifier.

---

## A. Stencil vocabulary — two orthogonal axes in one byte

### A.1 The regime axis (3 bits) — materializing `FootprintRegime`

The authoritative classification is [[Deep-Field-Residency-Unification-2026-08]] §B. Its
`FootprintRegime` formula is adopted unchanged:

```text
footprint = worldDist * raySizeCoef + raySizeBias
if raySizeCoef <= 0 || footprint < cellWorldSize/8: return Surface
if footprint < cosmicK * cellWorldSize:             return MipHit
return Cosmic
```

Those three results use the semantics already ratified and shipped in
`Vixen-Docs/01-Architecture/Deep-Field-Mip-Accessor-Policy-2026-08.md`:

1. **SURFACE** — footprint < voxel size — exact fine DDA march, binary hit.
2. **MIP HIT** — voxel ≤ footprint < K·cell — sample the mip ladder at the matched level, commit
   as a hit, no march.
3. **COSMIC** — footprint ≥ K·cell — transmittance accumulation over coarse cells.

The user's three named cases map onto these **exactly**, which is the reason this vocabulary is the
right one and a fresh one would be a mistake:

| User's phrasing | Regime | Where the engine already decides it |
|---|---|---|
| *"a brick node declaring it in the scale of opaque handling"* | SURFACE (opaque-brick) | the DDA march loop, `shaders/SceneBindings.glsl:2554` onward — "DDA owns voxel-brick traversal — level 0 by definition" (mip-accessor policy doc) |
| *"this pixel hit a mip level < n"* | MIP HIT (opaque-mip) | entry dispatch, `shaders/SceneBindings.glsl:2478-2548`, the `#ifdef VIXEN_MIP_POLICY` block — "resolves the mip sample and **returns before the DDA march loop begins**" |
| *"an escaping ray that goes into accumulative mode"* | COSMIC (accumulative/escaping) | the regime-3 accumulation walk; its residual transmittance surfaces as `WorldHit.residualT` (`shaders/TraceWorld.glsl:94-102`), consumed at `shaders/BodyInstanceRayMarch.comp:255` |

The stencil encodes those results as a small **enum in 3 bits**. `REGIME_MISS` is a terminal
materialization sentinel for "no winning hit"; it is not a fourth `FootprintRegime` result.
The other named values are explicitly reservations for design work that has not shipped:

```glsl
// shaders/PolicyStencil.glsl (NEW)
#define STENCIL_REGIME_MASK      0x07u   // bits 0..2
#define STENCIL_REGIME_SHIFT     0u

#define REGIME_MISS              0u   // no hit: sky / escaped the scene entirely
#define REGIME_SURFACE           1u   // opaque-brick, exact DDA march (footprint < voxel)
#define REGIME_MIP               2u   // opaque-mip<n, entry-dispatch mip resolve, no march
#define REGIME_COSMIC            3u   // accumulative/escaping, partial coverage, residualT < 1
// --- RESERVED / DESIGN ONLY: not shipped vocabulary or behavior (§F) ---
#define REGIME_VOLUMETRIC        4u   // DESIGN: participating media accumulation
#define REGIME_TRANSLUCENT       5u   // DESIGN: scattering surface, second-ray budget
#define REGIME_TRANSPARENT       6u   // DESIGN: refractive/alpha, ordered composite
// 7u free
```

`REGIME_MISS` is deliberately value 0 so a zero-cleared stencil image is a valid "nothing here"
state — no separate clear-value convention to remember, and a `memset`/`vkCmdClearColorImage` to 0
is the correct initialisation.

If volumetric, translucent, or transparent behavior is designed, it must extend
`FootprintRegime` (or explicitly compose a second authoritatively named input into that function)
in [[Deep-Field-Residency-Unification-2026-08]] §B first. Adding a stencil enum arm alone would
create the forbidden twin classifier.

### A.2 The source axis (2 bits) — a MASK, not an enum

The source axis is **two overlapping bit flags**, because a screen area (and even a single ray,
which may traverse both a virtual and a materialized body) can be **both**. An enum cannot express
"both", and a three-valued enum `{VIRTUAL, MATERIALIZED, HYBRID}` would force every consumer to
write `if (src == VIRTUAL || src == HYBRID)` — a two-term compare where a one-term bit test is
correct and cheaper. The bitmask makes hybrid a *consequence* of the representation rather than a
third case anyone has to remember.

**The terms are the docs' own.** `Lazy-Procedural-Delta-Baseline-Design-2026-07.md:143-144` gives a
two-row table whose row labels are literally **"Virtual (instructions-only)"** and **"Materialized
(cached)"**:

> | **Virtual (instructions-only)** | recipe ref + transform/params + derived bounds | direct GPU
> field evaluation (generalized `PROVIDER_PROCEDURAL`) … | the DEFAULT for all unedited content |
> | **Materialized (cached)** | produced subtree/bricks in the pool | ordinary ESVO march (today's
> path, unchanged shader semantics) | when a delta forces it, or when the perf policy elects it |

**The coordinator's proposed mapping is CONFIRMED, with one correction.** The doc states the render
mechanism per row directly: virtual ⇒ *"direct GPU field evaluation (generalized
`PROVIDER_PROCEDURAL`)"*; materialized ⇒ *"ordinary ESVO march"* over *"produced subtree/bricks in
the pool"*. So "procedural/recipe evaluation = virtual, baked brick/mip pool sampling =
materialized" is the doc's mapping, and it is already **live in the shader today** as a real runtime
discriminator:

- `shaders/SceneBindings.glsl:865` — `uint providerKind;  // 32  (0 = Stored/ESVO, 1 = Procedural)`
- `shaders/SceneBindings.glsl:990` — `#define PROVIDER_PROCEDURAL 1u`
- `shaders/TraceWorld.glsl:256` and `:772` — `if (inst.providerKind == PROVIDER_PROCEDURAL)`, the
  two live branch points in the instance loop.
- `shaders/BodyInstanceRayMarch.comp:284` — `if (winner.providerKind == PROVIDER_STORED)`.
- `shaders/InstanceOcclusionCull.comp:91` — `if (inst.providerKind != 0u) return false;` — an
  existing, shipped **pipeline-portion skip keyed on exactly this axis**. Prior art for §C.

**The correction to the coordinator's framing:** `providerKind` is a *per-instance*, **two-valued**
field — there is no hybrid instance today. The lazy-procedural doc's own state machine
(`Lazy-Procedural-Delta-Baseline-Design-2026-07.md:204-205`) is
`virtual (recipe ref) → materialized (configs[] slot) → evicted (back to recipe ref)` — a per-region
transition, still one state at a time. **Hybrid is a property of the PIXEL, not of the body**: a
pixel whose ray traverses a procedural body and a stored body has both bits set. That is precisely
why the mask belongs on the *stencil* (per-pixel/per-area) and not on `BodyInstance` (per-instance,
where `providerKind` already lives and is correctly an enum). This design does **not** propose
changing `providerKind`.

There is a third, doc-named state the mask should anticipate but **not** encode yet:
`Lazy-Procedural-Delta-Baseline-Design-2026-07.md:155-166` distinguishes a **recipe-delta** (region
stays virtual — "re-render from the amended instruction stream, zero materialization") from a
**materialized delta** ("the edit is not (efficiently) expressible in instruction space"). Both are
already covered by the two bits as defined: a recipe-delta'd region is VIRTUAL, a
materialized-delta'd one sets MATERIALIZED too. No third bit is needed for deltas. §F.2 shows what a
genuinely new source distinction (e.g. *streaming*) would cost.

```glsl
#define STENCIL_SRC_MASK         0x18u   // bits 3..4
#define STENCIL_SRC_VIRTUAL      0x08u   // bit 3 — PROVIDER_PROCEDURAL contributed to this pixel
#define STENCIL_SRC_MATERIALIZED 0x10u   // bit 4 — PROVIDER_STORED / brick+mip pool contributed
// hybrid == (STENCIL_SRC_VIRTUAL | STENCIL_SRC_MATERIALIZED), no distinct value needed
// bits 5..7 free (§F.2 reserves them for future source distinctions)
```

### A.3 The exact byte layout

```
 bit  7   6   5   4   3   2   1   0
     [ free  ] [MAT][VIR] [ regime 0..7 ]
      \__ §F.2 __/  \_ source mask _/  \_ regime enum _/
```

**The key property the user's framing buys**, and the reason source is a mask while regime is an
enum: a pipeline-portion skip is a **single bitwise test**, no decode, no switch, no compare chain:

```glsl
if ((stencil & STENCIL_SRC_MATERIALIZED) == 0u) return;   // pure-virtual pixel: skip this portion
if ((stencil & STENCIL_SRC_VIRTUAL)      == 0u) return;   // pure-materialized: skip the other one
```

Whereas a regime consumer wants exactly one branch target, which is what an enum + a group dispatch
gives it (§C). Right representation for each axis, and they cohabit one byte.

### A.4 Who writes it, and when it is final vs provisional

**One writer, one point.** The stencil is written by **the primary trace pass**
(`shaders/BodyInstanceRayMarch.comp`), at the **same site** it already writes the terminal
`HitRecord` — `:344` (`rec.flags = anyHit ? HITRECORD_FLAG_HIT : 0u`) through `:362`
(`rec._pad0 = uint[3](worldHit.instIdx, floatBitsToUint(worldHit.emission), 0u)`).

That site is chosen because it is the **first point where the classification is final**, and this is
load-bearing. Consider the state machine the user's directive implies — *"a ray can start opaque and
become escaping"*:

```
                    per-instance, INSIDE TraceWorld's loop  (PROVISIONAL)
   FootprintRegime ─┬─▶ MipHit, entry resolve returns pre-march ....... candidate = MIP
  (SceneBindings    ├─▶ DDA march loop, brick hit ..................... candidate = SURFACE
   :2478-2548)      └─▶ regime-3 accumulation walk, residualT < 1 ..... candidate = COSMIC
                              │
                              │  isCloserHit  (TraceWorld.glsl:639)
                              ▼
                    bestT/bestColor/bestResidualT OVERWRITTEN by any nearer instance
                    (TraceWorld.glsl:643 — `bestResidualT = instResidualT;`)
                              │
                              ▼
                    hit.residualT = bestResidualT   (TraceWorld.glsl:687)   ← FINAL
                              │
                              ▼
                    BodyInstanceRayMarch.comp:344-362 — write HitRecord + STENCIL
```

**Provisional inside the loop, final after it.** This is the same distinction the codebase already
paid for once and wrote down: `WorldHit.wasFarField`'s own comment at `shaders/TraceWorld.glsl:88-92`
warns that a per-candidate far-field win is **not** the same thing as the pixel's terminal answer —
*"NOT the same as 'some far-field candidate won its own per-instance `isCloserHit` compare' (the
round-8-located `[FarFieldWon]` conflation) — this rides `bestInstIdx`'s actual selection, so a
later non-far-field instance overwriting the winner also overwrites/clears this."* The
`[FarFieldWon]` conflation is a **paid-for bug in exactly this shape**. The stencil must not repeat
it.

So the rule, stated once:

> **The regime axis rides the winner.** Each candidate's value is the result of the shared
> `FootprintRegime` evaluation, not a stencil-local threshold test. It is set/overwritten wherever `bestResidualT` is
> (`TraceWorld.glsl:643`), cleared whenever a later instance wins, and read only after the loop
> closes at `:675`. It is a `WorldHit` field, exactly like `wasFarField` and `residualT` already are
> — never a sticky shader global.
>
> **The source axis accumulates.** It is an OR across every instance that *contributed* to the
> pixel, not just the winner — because "did any virtual work happen for this pixel" is the question
> a pipeline-portion skip actually asks. It is set at the two live `providerKind` branch points
> (`TraceWorld.glsl:256`, `:772`) and never cleared.

That asymmetry — enum rides the winner, mask accumulates — is the *only* subtle thing in the write
path, and it falls directly out of what each axis is for.

**Concretely, five edits in `TraceWorld.glsl`:**

| Site | Edit |
|---|---|
| `TraceWorld.glsl:68-103` (`struct WorldHit`) | add `uint stencil;` — one more field alongside `residualT`/`behindColor`/`wasFarField` |
| `TraceWorld.glsl:256`, `:772` (the `providerKind` branches) | `srcMask \|= STENCIL_SRC_VIRTUAL;` / `\|= STENCIL_SRC_MATERIALIZED;` on the else side |
| the existing entry-policy block | call/materialize the shared GLSL `FootprintRegime` result as `instRegime`; do not repeat its comparisons in stencil code |
| `TraceWorld.glsl:643` (inside the `isCloserHit` winner branch, beside `bestResidualT = instResidualT;`) | `bestRegime = instRegime;` so the shared result rides the winner |
| `TraceWorld.glsl:686-692` (the `#ifdef`'d tail where `hit.residualT`/`hit.behindColor` are stamped) | `hit.stencil = (bestRegime << STENCIL_REGIME_SHIFT) \| srcMask;` |

Note the tail already has a flag-off `#else` arm (`:689-692`) that writes inert defaults. The
stencil follows the same discipline: on a flag-off build it writes `REGIME_SURFACE | srcMask`, which
is the correct classification for a build with no regime-3 walk compiled in.

---

## B. Storage — per-pixel value and per-tile aggregate

### B.1 Reuse-first: the candidate homes, evaluated

The brief proposed *"HitRecord has spare `_pad0` lanes"*. **Verified against the actual layout —
that is not true as stated, and the correction matters.** `shaders/HitRecord.glsl:46-57` carries an
explicit MASTER ledger, and **all three lanes are occupied**:

- `_pad0[0]` = winning instance index (M3 round 3; also how W2's recipe-bucketed shade resolves
  pixel→instance→recipe — `SpatialReuseShade.comp:252`).
- `_pad0[1]` = `floatBitsToUint(WorldHit.emission)` (M11.2) — read at `SpatialReuseShade.comp:470`.
- `_pad0[2]` = ShadowVisibilityWave's visibility answers (W1b).

`_pad0[2]` currently assigns only bits 0..4, but free ownership is not sufficient: the two shipped
`ShadowVisibilityWave.comp` stores overwrite the whole word. The analytic store writes
`visBits & 0xFu`, clearing bits 4..31; the reservoir store writes `(old & 0xFu) | 0x10u`, clearing
bits 5..31. Therefore a march-written byte in bits 8..15 is destroyed before later consumers unless
both stores become mask-preserving.

| Home | Byte cost | Verdict |
|---|---|---|
| **`HitRecord._pad0[2]` bits 8..15 + mask-preserving shadow RMW** | **0 B storage.** `HitRecord` remains 60 B declared size with a 64 B SSBO array stride. | ✅ **Slice-1 choice.** The analytic shadow writer gains one SSBO read and mask/OR operations per launched entry; the reservoir writer already reads for RMW and becomes a simpler preserve-all-except-owned-bit operation. Zero new binding, descriptor, or barrier. |
| A dedicated **R8_UINT image** | **250,000 B = 250 kB decimal = 244.14 KiB** at 500×500. | ⏸ **Fallback for a named consumer.** It avoids shared-word RMW and enables raster/HW-stencil or image-mip reduction, but adds an image, descriptor, barrier, and layout transition per frame. No current compute-only consumer justifies that fixed allocation. |
| A different `HitRecord` pad home | No safe zero-growth byte exists. | ⛔ `_pad0[0]` is instance id, `_pad0[1]` is emission, and `_pad0[2]` is the only lane with unassigned bits. Growing the record would move both declared size and stride and is worse than the R8 fallback. |
| **`TraceBufferHeader` debug path** | 0 B (repurposes padding) | ⛔ **Wrong for production, categorically.** It is a *single global header*, not per-pixel: the `[CompositeBlend]`/`[WalkCov]` family are frame scalars. The composite fields and `recordCompositeBlend` helper compile unconditionally; only the call is `#ifdef VIXEN_REGIME3_COMPOSITE`-gated, so the off variant is behavior-inert, not compile-absent. The header is also host-read instrumentation with a stall and a pinned 528 B size. |

**Decision: retain `_pad0[2]` bits 8..15 and pay the preservation cost.** The two required stores
are specified as:

```glsl
// Analytic phase runs first: rebaseline owned bits 0..4, preserve bits 5..31.
uint old = hitRecords[slot]._pad0[2];
hitRecords[slot]._pad0[2] = (old & ~0x1Fu) | (visBits & 0x0Fu);

// Reservoir phase runs later and writes only owned bit 4 when visible.
hitRecords[slot]._pad0[2] |= 0x10u;
```

The analytic phase deliberately clears bit 4 each frame while preserving the stencil. The reservoir
phase preserves analytic, stencil, and future bits. Dispatch ordering already separates the phases,
so this does not introduce an inter-phase atomic requirement. Slice verification must measure the
added analytic read; if it is material, the R8 image is the documented fallback.

**Bit placement inside `_pad0[2]`** remains non-adjacent to the shadow bits:

```
_pad0[2]:  bits 0..3  analytic-light visibility bitmask   (ShadowVisibilityWave, EXISTING)
           bit  4     reserved ReSTIR reservoir answer    (EXISTING)
           bits 5..7  reserved for the shadow ledger's own growth
           bits 8..15 ◀── THE POLICY STENCIL BYTE  (this design)
           bits 16..31 free
#define HITRECORD_STENCIL_SHIFT 8u
#define HITRECORD_STENCIL_MASK  0xFF00u
```

**Size vocabulary.** SPIR-V reflection and `HitRecordCpu` assert a **60 B declared element size**;
std430 tail alignment makes the real runtime-array **stride 64 B**. Reusing existing bits changes
neither number. Do not write `sizeof(HitRecord)==64`: the parity test intentionally distinguishes
declared size from buffer stride.

**Lifetime and readers.** Written by the march, preserved by both shadow stores, valid for **this
frame only**, and read within the same frame by post-march consumers: `ShadowVisibilityWave.comp` (skip shadow rays for
`REGIME_COSMIC`/`REGIME_MISS`), `SpatialReuseShade.comp`, `DirectLighting.comp`, and the composite.
There is deliberately **no cross-frame contract** — the march rewrites it every frame, so no history
buffer, no reprojection, no staleness question. If a later slice wants temporal reuse it can add
one; it does not get one for free here, and does not need one.

### B.2 The tile aggregate — "area", not just pixel

The user said **area**. The aggregate is the mechanism that turns per-pixel classification into a
*dispatch-level* saving, and with bit flags it is a **pure OR-reduction**:

```
tileStencilSrc[t] = OR over all pixels p in tile t of (stencil[p] & STENCIL_SRC_MASK)
```

Then, per pipeline portion, per tile:

| `tileStencilSrc[t] & STENCIL_SRC_MASK` | Meaning | Dispatch decision |
|---|---|---|
| `0x08` (VIRTUAL only) | no pixel in the tile touched materialized content | **zero the materialized portion's indirect dispatch for this tile** |
| `0x10` (MATERIALIZED only) | no pixel touched procedural content | **zero the virtual portion's indirect dispatch** |
| `0x18` (both) | mixed / hybrid tile | run both portions |
| `0x00` | pure miss/sky tile | run neither |

The OR-reduction is the *whole* reason the source axis is a mask. An enum would need a
"does-any-pixel-in-this-tile-have-property-X" reduction per property; a mask gets all properties in
one `atomicOr`, or one `subgroupOr` + one `atomicOr` per workgroup.

The regime axis can ride the same reduction as a **presence mask** —
`tileRegimeMask[t] |= (1u << regime)` — an 8-bit "which regimes appear in this tile", which is what
§C's per-group dispatch consumes. Same one atomic.

**What "tile" means concretely here.** Three candidate granularities already exist in this tree; the
design picks the one that costs nothing:

- **`8×8` — the primary march workgroup.** `shaders/BodyInstanceRayMarch.comp:30` uses an 8×8
  local size. The shipped graph currently floor-divides (`500/8 = 62`), so it launches 62×62
  groups, covers only 496×496 pixels, and leaves the valid right/bottom border (`x=496..499` or
  `y=496..499`) untraced and unclassified. **The stencil slice amends the primary dispatch to
  ceiling division:** `(500+7)/8 = 63` in each dimension. `BodyInstanceRayMarch.comp:221` already
  rejects invocations outside `imageSize`, so the 504×504 launch classifies all 500×500 valid
  pixels while the 4 out-of-range lanes on each edge return without contributing. A partial edge
  tile ORs only its valid invocations; there is no default or stale-border classification.
  The tile buffer is therefore **63×63 = 3,969 uints = 15,876 B (15.50 KiB)**. The old floor grid
  was 62×62 = 3,844 uints = 15,376 B (15.02 KiB), but is not an acceptable stencil extent.
- **`32×32` — the B1 HiZ tile pyramid.** *"B1 occlusion probe enabled: depth 500×500 ping-pong, HiZ
  tiles 32×32"* (`control-composite-1.log:19`; constructed at
  `application/main/source/graph/BuildRenderGraph.cpp:1440`). The wavefront doc calls this pyramid
  out as reusable prior art — *"B1 depth proxy + HiZ tile pyramid | shipped, default-on |
  primary-termination certainty seed; **the screen-anchored pyramid pattern**"*
  (`docs/plans/2026-08-04-wavefront-recipe-shading.md:49`). A 32×32 tile is a coarser, cheaper
  dispatch unit but a **weaker filter** (16× more pixels per tile ⇒ far more tiles come back
  "mixed"). Correct upgrade if 8×8 tile-buffer traffic ever shows up in a profile; wrong starting
  point.
- **Per-bucket screen AABB — the wavefront/recipe granularity.** `RecipeInstanceBucketing.comp`
  already computes a *"conservative pixel-space AABB per bucket via `atomicMin`/`atomicMax`"*
  (`:19-26`) and turns it into *"a `VkDispatchIndirectCommand` sized to just that"* bucket's rect
  (`:231-234`), with `local_size_x = 64` (`:54`). This is **not a tile grid** — it is a
  per-recipe-bucket rectangle. It is the right *shape* for §C's group dispatch and the wrong shape
  for the OR-reduction, which wants a fixed grid. Use both: the grid for the reduction, the AABB
  pattern for the dispatch.

**Vocabulary alignment.** Throughout, "group" is used the way the wavefront doc uses "bucket": a
compacted member list + a screen rect + a `VkDispatchIndirectCommand`
(`docs/plans/2026-08-04-wavefront-recipe-shading.md:512-516`). The distinction is the key: a
*bucket* is keyed by `recipeId`, a **group is keyed by stencil regime**. They are orthogonal and
compose — a pixel is in exactly one regime group and one recipe bucket.

---

## C. Grouped handling — how downstream consumes it

Three mechanisms, in ascending cost:

| Mechanism | What it costs | What it buys |
|---|---|---|
| **1. Branch-on-tile-uniform** | one buffer read + one branch per workgroup | Removes divergence *when the tile is uniform*, which is the common case (screen-space policy is highly spatially coherent — a body's silhouette is contiguous). Costs nothing when it isn't: the branch falls through to today's code. |
| **2. Indirect dispatch per group** | a compaction pass + per-group `VkDispatchIndirectCommand` + a barrier per group | True zero-cost skipping — the GPU never launches the workgroup. This is what the wavefront doc's bucketing does today (`:512-516`), and `MultiDispatchNode`'s `autoBarriers` already serialises WAW between bucket dispatches (`:515-516`). |
| **3. HW stencil test** | an R8 image + layout transitions + a raster path | Free rejection *in raster*. Irrelevant while every consumer is a compute shader. |

### C.1 Recommendation for slice 1: **(1) branch-on-tile-uniform**

Rationale, in order:

1. **It needs no new pass.** The tile-OR is computed by the workgroup that already has the data
   (§B.2), and consumed by a single `if` at the top of each downstream shader. Mechanism (2)
   requires a compaction pass, a per-group command buffer, and a barrier — real machinery, for a win
   that mechanism (1) already captures on uniform tiles.
2. **Uniform tiles are where the win is anyway.** Mechanism (2) beats (1) precisely on *mixed*
   tiles, where (1) must run both portions. But a mixed tile is one straddling a silhouette — a thin
   boundary set. Paying for (2) up front is paying for the boundary case before measuring it.
3. **The precedent is already shipped, in this exact shape.**
   `shaders/InstanceOcclusionCull.comp:91` is `if (inst.providerKind != 0u) return false;` — an
   early-out on exactly the source axis, done as a branch, in production, today. This design
   generalises that one line from per-instance to per-tile.
4. **(2) is a strict, non-breaking upgrade.** Nothing about the stencil write path or the tile buffer
   changes when the consumer flips from branch to indirect dispatch; only the consumer's prologue
   does. So (1) does not have to be undone to get (2).

**What (2) buys later, named so it isn't rediscovered:** it eliminates the launch cost of skipped
workgroups (branch-on-uniform still pays scheduling + the buffer read), and it *reorders* pixels so
a workgroup is regime-coherent even when screen space isn't — which is the actual prize for
`REGIME_VOLUMETRIC`/`REGIME_TRANSLUCENT` (§F), whose handlers will be expensive enough that
divergence dominates. The wavefront doc's own thesis is exactly this: *"a bucket's warp runs ONE
recipe straight-line"* (`:146`), against a measured **2.4× at N=192** regression from carrying the
whole library in an N-way `OpSwitch` (`:136-137`).

**What (3) buys later:** if any consumer ever becomes a raster pass, HW stencil rejects at
fixed-function cost. Speculative today — no such consumer exists.

---

## D. The C3 resolution via groups

### D.1 The problem, restated in stencil terms

Measured and closed by the B50-T1 follow-up (`perf/batch50-secondbody/report.md:315-484`): the
regime-3 composite blend executes **84,300 times** across 300 frames with `max(behindColor.rgb)`
**exactly 0**, on every single execution. The cause is *not* the bake and *not* the blend — it is the
front-to-back entry-cull:

```glsl
// shaders/TraceWorld.glsl:533-544  (working tree)
float entryTieBand = SEAM_TIE_EPS_REL * max(abs(bestT), 1.0);
if (entryTWorld > bestT + entryTieBand) {
    // This instance's nearest possible entry is already farther than
    // something already hit this ray — its full ESVO traversal
    // (below) cannot possibly produce the nearest hit. Skip it
    // entirely: zero traversal iterations, not just a discarded result.
    continue;                                        // ← line 543
}
```

`continue` skips the instance **entirely**, so `instHit` is never computed for it, so
`TraceWorld.glsl:663`'s `else if (instHit)` — the *only* path that can promote a behind-body to
`secondColor` (`:672`) — is unreachable for exactly the instances that would populate it.
`secondColor` keeps its `:163` initialiser, `hit.behindColor = vec3(0.0)` (`:688`), and the blend at
`BodyInstanceRayMarch.comp:260` adds `0.015625 * vec3(0)` in this scene. The exact-zero inference is
**scene-scoped**: the demonstrated rear body is white-tinted with white baked voxels, so a reached
lane-0 color path would be nonzero. Authored or baked black inputs can legally assign zero at the
same sites; zero is not universally reachable only from the initializer.

The `[CompositeBlend]` fields and helper compile unconditionally; only the call is gated by
`#ifdef VIXEN_REGIME3_COMPOSITE`. Composite-off is therefore behavior-inert, not compile-absent.

**In stencil vocabulary the conflict is one sentence:** the cull's premise — *"anything farther than
`bestT` cannot be the nearest hit"* — is a **`REGIME_SURFACE`/`REGIME_MIP` invariant**. It is true
for an opaque winner and **false for `REGIME_COSMIC`**, where a partial-coverage winner does not
fully occlude and a farther hit is exactly what it needs. The cull is not wrong; it is
**unconditionally applied to a group it was never derived for**.

That is the general shape this whole design exists to fix: *a policy hardcoded as global because
there was no vocabulary to scope it.*

### D.2 The change: group-scope the cull

The cull becomes **group-scoped on the current winner's materialized policy and exact blend
interval**. The interval is load-bearing: a `REGIME_COSMIC`-wide exception includes rays whose
residual transmittance is too small to blend and invalidates the 281/frame bound.

```glsl
// shaders/TraceWorld.glsl:533-544, group-scoped
float entryTieBand = SEAM_TIE_EPS_REL * max(abs(bestT), 1.0);
bool  relaxForComposite = false;
#ifdef VIXEN_REGIME3_COMPOSITE
    // Match BodyInstanceRayMarch.comp's actual blend gate exactly. Cosmic rays
    // outside this interval contribute no second layer and retain the full cull.
    relaxForComposite = (bestRegime == REGIME_COSMIC) &&
                        (bestResidualT > 1e-6) &&
                        (bestResidualT < 0.999999);
#endif
if (!relaxForComposite && entryTWorld > bestT + entryTieBand) { … continue; }
```

This uses both outputs already maintained with the winner: `bestRegime` scopes the policy and
`bestResidualT` scopes the behavior that will actually consume a second layer. Future non-occluding
design regimes do **not** inherit this exception merely by joining an enum; each must amend the
authoritative policy and define its own consumption interval/budget.

**What each group keeps:**

| Group | Cull behaviour | Rationale |
|---|---|---|
| `REGIME_SURFACE`, `REGIME_MIP` | **cull intact, unchanged** | Opaque winner fully occludes; the premise holds. This is the common case and a genuine, valuable optimisation ("zero traversal iterations", `:538-539`). **Zero perf change, byte-identical output.** |
| `REGIME_COSMIC` with `1e-6 < residualT < 0.999999` | **survive the cull** — the candidate proceeds to traversal, reaches `:663`, and may populate `secondColor` | This is exactly the later composite's blend population. |
| `REGIME_COSMIC` outside the blend interval | **cull intact** | The later composite consumes no second layer, so relaxing traversal cannot affect the demonstrated output. |
| `REGIME_VOLUMETRIC` / `TRANSLUCENT` / `TRANSPARENT` | **RESERVED / DESIGN; no cull behavior assigned** | These are not shipped regimes. Their eventual ordered-composition rule may require a different budget. |

### D.3 Predicted perf shape — quantified

**The denominator, with its source.** The `[CompositeBlend]` probe counts *blends*, not rays, so the
ray total must come from the frame geometry:

- Render target **500×500** — `perf/batch50-secondbody/control-composite-1.log:19`
  (*"depth 500×500 ping-pong"*) and `:24` (*"from window 500×500"*).
- **300 frames** per boot — the `[CompositeBlend]` reading in `report.md` divides 84,300 by 300 to
  get 281 blends/frame, so 300 is the run's own stated frame count.
- ⇒ **75,000,000 primary rays** across the run (250,000/frame × 300).

**Cross-check against an independent counter.** `[Regime3] entry=436200` over the same 300 frames
(`report.md:159`) = 1,454 regime-3 entries/frame. This is the count of rays that *entered* the
regime-3 walk — a much smaller set than all rays, as expected: the two bodies occupy a small
fraction of a 500×500 frame. The 84,300 blends (281/frame) is 19.3% of those 1,454 entries, i.e.
the subset whose `residualT` landed strictly inside `(1e-6, 0.999999)` — consistent, since
`BodyInstanceRayMarch.comp:255` gates on exactly that range.

**The prediction:**

| Quantity | Value | Basis |
|---|---|---|
| Rays where the chosen blend-gated cull relaxes | **≤ 84,300 / 300 = ≤ 281/frame** | The relaxation condition matches the measured `residualT > 1e-6 && residualT < 0.999999` blend interval. |
| Chosen relaxation as a fraction of primary rays | **≤ 0.1124%** (reported compactly as ≤0.112%) | 281 / 250,000. |
| Rejected `REGIME_COSMIC`-wide alternative | **≈1,435–1,454 rays/frame = ≈0.574–0.582%** | `FarFieldWon 430,500/300 = 1,435`; `Regime3 entry 436,200/300 = 1,454`. It is roughly five times the blend population and is not this design. |
| Extra instance traversals introduced | **≤ 281/frame**, bounded by (relaxed rays × overlapping instances behind) = ≤ 281 × 1 on this 2-body scene | Only the *behind* instance survives; a 2-body scene has exactly one. |
| Predicted `esvo_traverse_shade_ms` movement | **below measurement noise** — 0.11% more traversals on the *only* affected shader stage | The CSV column exists (`control-composite-1.csv` header) but the mip-accessor doc's own cost note warns this machine has shown *"up to 165× within-config spread"* under concurrent GPU load; a 0.1% signal is not separable. **Declare it unmeasurable, do not claim a win or a loss.** |
| Predicted `[CompositeBlend] behindMax` | **> 0** (currently exactly 0) | This is the decisive, *non-noisy* reference movement — a binary flip, not a percentage. |
| Predicted frame hash | **moves off `5e942652`** on the overlap-composite leg | If the hash does not move, the fix did not work; the follow-up already established `5e942652` is byte-reproducible across repeats. |
| Predicted flag-off / control legs | **unchanged** — `3951c2c5` on both control legs, `5e942652` on overlap-floor2 | The change is inside `#ifdef VIXEN_REGIME3_COMPOSITE` and gated on `REGIME_COSMIC`. |

**The honest cost statement** (which is why this was flagged for a user ruling in the first place):
this removes a traversal-skip for a bounded subset of rays. It is a real cost on composite builds. At
≤0.112% of rays on this scene it is expected to be invisible, but on a scene with heavy regime-3
coverage and deep instance stacks it scales with (cosmic pixels × instances behind). The group
scoping is what keeps that cost *confined to the group that needs it* — on the opaque path, which is
every other pixel, the cull is bit-for-bit what it is today.

**Composition ceiling.** One `secondColor` is sufficient for this measured two-layer
demonstration. It is not a general ordered multilayer, transparency, or volumetric solution.
Deeper stacks require an explicit termination/transmittance budget and storage or queueing for
more than one secondary candidate; the stencil does not remove that ceiling.

---

## E. Secondary waves

**Status: design-derived; every item in this section must be validated before implementation.**
The primary stencil gives secondary work a policy vocabulary without forcing a new stream or a
screen-space-only interpretation.

### E.1 Launch and destination classification

- **Launch policy:** a secondary wave inherits the origin pixel's stencil byte. The apply pass
  already fetches that pixel's `HitRecord`, so the byte rides `_pad0[2]` with **zero new streams**.
  Inheritance describes why the wave launched; it must not be reused as the destination result.
- **Destination policy:** each wave entry evaluates the **same authoritative `FootprintRegime`**
  from [[Deep-Field-Residency-Unification-2026-08]] §B using that entry's cone footprint. For a
  wave entry, the distance input is the traveled ray length and the footprint grows as
  `rayLength * raySizeCoef` (plus the same bias and cell thresholds). This is a new call site of
  the same function, not a wave-local classifier.
- Bounce paths therefore grow their footprint naturally with accumulated path distance and migrate
  toward accumulative/probe-grid policy groups. The far-field limit is the existing DDGI shape:
  sufficiently broad/distant work belongs in probe-grid accumulation rather than exact per-bounce
  materialized traversal.

### E.2 Queue and skip shape

Wave queues partition by **(policy-group × source-bits)**. Reuse the shipped
`RecipeInstanceBucketing` pattern: compact members by key, retain a conservative work extent, and
emit a `VkDispatchIndirectCommand` per non-empty partition. Policy group remains an enum;
VIRTUAL/MATERIALIZED remain independently testable mask bits, including the hybrid case.

Within each queue segment, OR-reduce source bits and regime presence. The segment aggregate gives
secondary waves the same virtual/materialized evaluator skip as primary tiles: if the OR lacks
`STENCIL_SRC_MATERIALIZED`, do not dispatch the materialized evaluator; if it lacks
`STENCIL_SRC_VIRTUAL`, do not dispatch the virtual evaluator. This is a dispatch decision, not a
per-entry reclassification.

### E.3 Cull scope and counters

Cull relaxation attaches to **WAVE TYPE × policy**, not policy alone:

- Shadow waves retain the full nearest-hit cull for every policy. They answer binary visibility and
  do not need `secondColor` or a second-nearest candidate.
- Only composite-relevant wave types may relax, and only under the same exact blend interval
  `residualT > 1e-6 && residualT < 0.999999` specified in §D. A cosmic label by itself is
  insufficient.
- Slice-0 counters record **per-wave-entry** group histograms in addition to per-pixel histograms.
  The dimensions are wave type, destination `FootprintRegime`, and source mask; launch-policy
  histograms may be retained separately to measure migration from origin policy to destination
  policy.

Required validation: prove the apply pass fetch is available at every launch site, define the exact
wave-entry cone coefficient/bias ABI, measure queue-compaction overhead and segment uniformity, and
confirm the DDGI handoff threshold before treating this section as scheduled work.

---

## F. Extensibility proof — both axes, on paper

The mechanical claim is narrow: after the authoritative classifier has been amended, materializing
another result does not change the byte width or generic OR-reduction. It does **not** claim that a
new regime is only an enum edit.

### F.1 Adding a regime group (e.g. `REGIME_VOLUMETRIC`)

Walk every file the stencil touches and show what a new group requires:

| Site | Change for a new group? | Why |
|---|---|---|
| `PolicyStencil.glsl` enum | **+1 line** (`#define REGIME_VOLUMETRIC 4u`) | 3 bits hold 8 values; 4 are used, 3 reserved, 1 free. **No bit-layout change, no `HitRecord` change, no struct-size movement.** |
| The write path — `TraceWorld.glsl:643`, `:686-692` | **none** | It writes `bestRegime`, an opaque `uint`. It has no knowledge of which values exist. |
| The tile OR-reduction (§B.2) | **none** | `tileRegimeMask \|= (1u << regime)` is value-agnostic; bit 4 lights up on its own. |
| Existing consumers (`ShadowVisibilityWave`, `SpatialReuseShade`, composite) | **none** | Each tests *its own* regime (`if (regime == REGIME_COSMIC)`, `if ((tileRegimeMask & (1<<REGIME_SURFACE)) != 0)`). A value they don't test is a value they ignore. |
| The §D cull scope | **No automatic inheritance** | Cull behavior is WAVE TYPE × policy and requires a defined consumption interval/budget. A non-occluding label alone does not justify the composite exception. |
| The new handler | **the actual work** | A new shader / a new group dispatch. Irreducible — it is the feature. |

**Net after the `FootprintRegime` amendment:** one stencil enum line + generic materialization + the
new handler; no layout growth. Before that amendment the value is RESERVED/DESIGN and cannot be
produced. ∎

### F.2 Adding a source distinction (e.g. `STENCIL_SRC_STREAMING`)

| Site | Change? |
|---|---|
| `PolicyStencil.glsl` mask | **+1 line** — `#define STENCIL_SRC_STREAMING 0x20u` (bit 5), `STENCIL_SRC_MASK` widens `0x18u` → `0x38u`. Bits 5–7 were reserved for exactly this (§A.3). **Still one byte, still zero struct growth.** |
| The OR-reduction | **none** — it ORs `stencil & STENCIL_SRC_MASK`; a widened mask carries the new bit automatically. |
| Existing bit tests | **none** — `(stencil & STENCIL_SRC_MATERIALIZED) == 0u` is unaffected by an unrelated bit being set. **This is the property an enum would not have**: a three-valued enum extended to four forces every `==` comparison to be re-audited. A mask does not. |
| The regime axis | **none** — orthogonal bits, orthogonal semantics. |

**Net:** one define + one mask constant + the new portion's own bit test. ∎

The lazy-procedural doc's own roadmap already names the candidates this reserves room for: the
**compaction layer** (`Lazy-Procedural-Delta-Baseline-Design-2026-07.md:176-181`) that converts
materialized data back to instructions, and the **eviction** path (`:458`) that reverts a region *"to
virtual/coarse"*. Both are source-axis transitions; neither needs a new bit under the current
definition (a compacted region is simply VIRTUAL again), which is itself evidence the two-bit split
is at the right granularity.

### F.3 The two axes do not interact

`REGIME_COSMIC` on a virtual body and on a materialized body are the same regime with different
source bits. `STENCIL_SRC_VIRTUAL` on a surface hit and a cosmic hit are the same source bit with
different regime values. There is no combination requiring a special case, and no consumer needs to
test both axes at once — which is why they can share a byte without sharing semantics.

### F.4 Classifier authority and amendment rule

`FootprintRegime` is the tracked authority at engine commit `86fd2ebf`, defined in
[[Deep-Field-Residency-Unification-2026-08]] §B and staged there as a CPU header plus hand-synced
GLSL twin. This stencil is its third consumer; §E's destination-wave evaluation is a fourth call
site of the same function.

The extensibility claim is therefore conditional: adding a byte value is mechanically cheap, but
adding classification behavior is never "one enum line." A new regime must first amend the
authoritative formula, inputs, CPU/GLSL parity contract, and residency meaning in the residency doc.
Only then may the stencil materialize the new result. This keeps vocabulary and thresholds from
drifting between residency, primary traversal, primary stencil, and secondary waves.

---

## G. Slice list — smallest first, each independently bootable

Every slice below is independently buildable, bootable, and verifiable. "Predicted movement" entries
are **declared references — testable predictions, not measured results.** Nothing in this document
has been built or booted.

### Slice 0 — scene-composition counter *(prerequisite for Slice 4, ~30 lines)*

**Why it exists:** the coordinator asked which fraction of the scene is virtual-only vs
materialized-only vs mixed. **No such counter exists.** Verified: the perf CSV columns are
per-stage timings only (`frame, cpu_frame_time_ms, …, esvo_traverse_shade_ms, …` —
`control-composite-1.csv` header); the GPU counter family is `[WalkCov]`, `[Regime3]`,
`[FarFieldWon]`, `[PolicyEntryDispatch]`, `[CompositeBlend]` (`report.md:153-160`, `:315-484`) — all
regime/coverage counters, **none source-axis**. And the batch-50 test scenes are 2–3 stored bodies;
`providerKind` is almost certainly uniformly `PROVIDER_STORED` on every leg, meaning the measured
virtual fraction today is likely **0%** and the skip win on *these* scenes is nil.

**Do:** counters in the `[CompositeBlend]` family's pattern, tallying pixels by tile-OR class:
pure-virtual / pure-materialized / mixed, plus **per-wave-entry** histograms keyed by wave type,
destination regime, and source mask (§E.3). Banner next to `[WalkCov]`.

**Predicted movement:** frame hash **unchanged** (instrumentation is image-inert — the C3 probe
already demonstrated this discipline, `report.md:F5`). `sizeof(TraceBufferHeader)` **unchanged** if it
repurposes padding; if not, the `== 528` static_assert moves and must be re-pinned.
**Predicted reading on current scenes: pure-materialized ≈ 100%, virtual ≈ 0%** — which, if
confirmed, means Slice 4 needs a mixed-provider scene before it can demonstrate anything.

**May be bundled into Slice 1** — the counters and the stencil write path touch adjacent code.

### Slice 1 — the stencil write path + the C3 divergence through the group mechanism ⭐

**This slice REPLACES the previously-parked one-branch cull patch.** The C3 follow-up's proposed fix
was a direct residual test at `TraceWorld.glsl:534` (`report.md`, "Cheapest correct shape"). That
patch is **superseded**: the corrected design uses the exact blend interval, expressed through the
winner's authoritative regime plus residual rather than an unscoped scalar. Do **not** land both.
The one-branch version is not
smaller — it is the same branch — but it lacks the policy vocabulary required to scope wave types.

**Do:**
1. `shaders/PolicyStencil.glsl` — new file, the §A defines only (~25 lines, no logic).
2. `WorldHit.stencil` + `bestRegime` (`TraceWorld.glsl:68-103`, `:643`, `:686-692`); source bits at
   `:256`/`:772`.
3. Stencil → `HitRecord._pad0[2]` bits 8–15 at `BodyInstanceRayMarch.comp:362`.
4. Convert both `ShadowVisibilityWave.comp` stores to the mask-preserving forms in §B.1.
5. Change the primary dispatch from floor 62×62 to ceiling 63×63; retain the existing shader bounds
   return so every valid border pixel is classified and out-of-range lanes are inert.
6. **The blend-interval-gated group cull** at `TraceWorld.glsl:533-544` (§D.2).

**Explicitly scoped to demonstrate the C3 divergence through the group mechanism** — the stencil is
not scaffolding here, it is the thing that *carries* the fix. No downstream stencil consumer or tile
aggregate is added yet. The bar is: composite diverges from floor2, and it diverges *because a
regime group said so*.

**Predicted movements:**
- `[CompositeBlend] behindMax` **0 → nonzero** ⭐ the decisive one.
- `[CompositeBlend] blends` — **unchanged at 84,300** (the residualT gate is untouched).
- Frame hash, `overlap-composite` — **`5e942652` → a new value.**
- Frame hash, `overlap-floor2` — **`5e942652`, unchanged.**
- Frame hash, both control legs — **`3951c2c5`, unchanged.**
- Flag-off identity `87473180f7b4e603` — **unchanged** (mip-accessor policy's standing bar).
- DDA census `414/420, mean 244.3071, max 332` — **unchanged.**
- `HitRecord` declared size **60 B** and SSBO stride **64 B** — both unchanged.
- Border coverage **496×496 → 500×500**; out-of-range invocations in the 504×504 launch return.
- `esvo_traverse_shade_ms` — **≤ +0.112% ⇒ unmeasurable** (§D.3). Do not claim a cost or a win.
- `shadow_visibility_wave_ms` — expected small movement from the analytic RMW read; measure rather
  than assume it is free.

### Slice 2 — the tile OR-reduction

**Do:** `subgroupOr`/shared-memory reduce the stencil within each 8×8 march workgroup
(`BodyInstanceRayMarch.comp:30`), one `atomicOr` per valid workgroup into the 63×63 tile buffer
(3,969 uints, 15,876 B / 15.50 KiB). Partial edge groups reduce only valid pixels. Write only;
**no consumer yet.**

**Predicted:** frame hash **unchanged on every leg** — this slice is image-inert by construction. If
any hash moves, the reduction is corrupting the stencil. That is the whole test.

### Slice 3 — first group consumer: branch-on-tile-uniform

**Do:** one prologue test in `ShadowVisibilityWave.comp` — skip the tile when its regime mask
contains no group that needs shadow rays (`REGIME_MISS` / `REGIME_COSMIC` only).

**Predicted:** frame hash **unchanged** (skipping shadow work for pixels that discard it must be
image-neutral — if a hash moves, the skip is wrong). `shadow_visibility_wave_ms` (an existing CSV
column) **decreases** by roughly the fraction of tiles that are pure-miss — on a 500×500 frame with
two small bodies, that is most of the frame, so this is the **first slice with a plausibly measurable
timing win**. Measure it in a quiet GPU window against a same-session baseline (the mip-accessor
doc's cost note is explicit about this machine's contention).

### Slice 4 — source-axis pipeline-portion skip *(the cheapest skip win; gated on Slice 0)*

**Do:** zero the materialized portion's dispatch for tiles whose OR lacks
`STENCIL_SRC_MATERIALIZED`, and vice versa (§B.2).

**Direction, and the honest caveat:** the coordinator asked which direction has measured support.
**Neither does yet.** Per Slice 0, no source-composition counter exists and the current test scenes
are almost certainly 100% materialized. **This slice cannot demonstrate a win on any scene in the
batch-50 corpus** and needs (a) Slice 0's counter and (b) a scene with real procedural bodies. The
existing precedent that the skip *works* — `InstanceOcclusionCull.comp:91` skipping non-stored
instances — is per-instance, not per-tile, so it demonstrates the pattern but not the payoff.

**Predicted:** frame hash **unchanged on every leg** (a correct skip is image-neutral, always).
Timing movement **unpredictable until Slice 0 reports the composition**; on a 100%-materialized
scene, predicted movement is **zero**, and that null result is the correct outcome, not a failure.

### Slice 5+ — indirect dispatch per group; new regime groups

Only once a group handler is expensive enough that divergence dominates (§C). The wavefront doc's
bucketing machinery (`:512-516`) is the vehicle; it is already shipped and does not need building.

---

## Open Questions

1. **What enforces CPU/GLSL parity for `FootprintRegime`?** The residency design leaves hand-sync
   versus a small parity script/test open. The stencil and secondary-wave consumers increase the
   cost of silent drift, but this revision does not select an implementation mechanism.

2. **What performance threshold moves storage to R8?** The selected `HitRecord` home adds one SSBO
   read plus masks to the analytic shadow store. Slice 1 must measure that cost; a raster consumer,
   hardware mip reduction, or material measured RMW regression would justify the 250,000 B R8
   fallback. No numeric switch threshold is established yet.

3. **Should the source axis be per-instance-contributed or per-winner?** §A.4 chose *accumulate
   across all contributing instances*, because a pipeline-portion skip asks "did any virtual work
   happen here". A per-winner mask would be a strictly stronger filter (more tiles come back pure)
   but wrong for any portion that cares about non-winning contributions. Revisit if Slice 0 shows
   mixed tiles dominating.

4. **Is 8×8 the right tile, or should it be 32×32?** §B.2 picked 8×8 because it is free (the march
   workgroup already has the pixels). 32×32 matches the shipped HiZ pyramid
   (`BuildRenderGraph.cpp:1440`) and would let a future consumer reuse that pyramid's mip chain for
   a hierarchical OR. Deferred until tile-buffer traffic shows in a profile.

5. **How do non-geometric future regimes amend `FootprintRegime`?** Volumetric/translucent/
   transparent values are RESERVED/DESIGN. Their producer may require an authored material/media
   input in addition to footprint; that input and its residency meaning must be specified in the
   authoritative function before any value ships.

6. **Can the secondary-wave design be validated without new ABI?** §E assumes every launch apply
   pass already fetches the origin `HitRecord`, and still needs the cone coefficient/bias ABI,
   queue-compaction cost, segment-uniformity data, and DDGI migration threshold validated.

7. **Does a mixed-provider test scene exist, or must one be authored?** Slice 4 is undemonstrable
   without one. The batch-50 overlap scene is stored-bodies-only as far as could be determined from
   the uncommitted diff.

---

## Verification note

This document is design only. **Nothing was built, booted, or newly measured for it.** Only this
canonical doc, its required mirror, and the B51 report are written; no commit or push is made. Every measured number
quoted is attributed to `perf/batch50-secondbody/report.md`; every predicted number is labelled as a
prediction. The batch-50 uncommitted working-tree diff was read as context and left untouched.

`B51: DONE — the stencil now materializes the tracked FootprintRegime classifier, survives shadow stores through costed RMW preservation, covers the full 500×500 extent, gates C3 relaxation to ≤281 blend rays/frame, and extends the same policy to secondary-wave design.`
