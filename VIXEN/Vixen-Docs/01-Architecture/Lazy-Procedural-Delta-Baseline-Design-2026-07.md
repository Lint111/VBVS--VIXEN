---
title: Lazy-Procedural + Delta Baseline — Instructions-First Rendering, Materialization as Delta-Cache
status: Design (user direction 2026-07-10) — not scheduled; Inc0/Inc1 candidates identified
date: 2026-07-10
tags: [architecture, svo, esvo, procedural, recipes, delta, streaming, lazy, baseline]
aliases: [Instructions-First Rendering, Lazy Procedural Baseline, Delta Rendering Baseline]
related:
  - "[[Sparse-Mip-ESVO-LOD-Direction-2026-07]]"
  - "[[Tiered-ESVO-Observer-Addressing-Design-2026-07]]"
  - "[[Tiered-ESVO-Inc2-Plan-2026-07]]"
  - "[[Runtime-Kernel-Pipeline-Direction-2026-06]]"
  - "[[Voxel-Content-Format-Contract-Design-2026-06]]"
  - "[[GigaVoxels-Streaming]]"
  - "libraries/SVO/include/SdfBake.h"
  - "libraries/SVO/include/Recipe/RecipeBaker.h"
  - "libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp"
---

# Lazy-Procedural + Delta Baseline — Instructions-First Rendering, Materialization as Delta-Cache

> **Status (2026-07-10).** User-set program direction: *"transition VIXEN from bake-based rendering
> to lazy procedural + delta rendering as baseline, to avoid having to bake the whole dataset before
> rendering a scene."* Refined same session (user, verbatim intent): **if we generate everything
> lazily from the instruction set we push, we don't need to materialize anything until there is a
> delta to account for** — generation data stays out of the CPU↔GPU boundary; the traffic becomes
> *recipes + deltas only*. Deltas come in kinds: a **recipe-delta** (e.g. cutting part of a
> predefined shape with an appended CSG op) needs no materialization at all — it renders straight
> from the amended instruction stream; only edits that instruction space cannot express become
> **materialized deltas**; and a future **compaction layer** may convert materialized delta data
> back into lazy recipe data to reclaim memory. Producer decision (user, same session): **CPU-first
> behind a placement-agnostic contract**. Second refinement (user, same session):
> **instructions-first is a bandwidth economy, not an exclusivity rule** — proper voxel deltas and
> stored voxel *assets* remain fully renderable, first-class content; the principle is that the
> more of a scene renders from recipes/instructions, the less data bandwidth the CPU↔GPU boundary
> has to carry, so traffic scales with the scene's *irreducibly stored* fraction, not its size.
>
> **Grounding.** This doc is grounded in a 6-area code sweep run 2026-07-10 against commit
> `c1c87fb6` (eager bake path, shipped residency machinery, recipe pipeline, edit/delta state,
> Tiered-ESVO Inc2, GigaVoxels prior art), followed by a 3-lens adversarial review (code accuracy,
> vault consistency, feasibility) whose confirmed findings are folded in below. **File:line
> citations are pinned to `c1c87fb6` unless marked otherwise** — the Tiered-ESVO Inc2 merge
> (`2d67840e`, landed on `main` the same morning, 2026-07-10 06:13) shifts many line numbers in
> `BodyOctreeSceneNode.*`, `BodyInstanceRayMarch.comp`, and `BuildRenderGraph.cpp`; post-merge
> positions are given inline where load-bearing.

## 1. Context & problem

### 1.1 What "bake-based" concretely means today

Every octree scene pays a whole-dataset CPU bake at graph **Compile**, before the first frame:

1. `BodyOctreeSceneNode::CompileImpl` → `EnsureOctreesBuilt()` → `CreateOctreeBuffers()`
   (`BodyOctreeSceneNode.cpp:181-205` @c1c87fb6; :192-216 post-merge). Three content paths, all
   whole-dataset: default 3 **binary** shell octrees from a full 64³ lattice scan (`kShellDepth=6`,
   `BodyOctreeSceneNode.h:199-200`); `VIXEN_STORED_SDF_DEMO` bakes 3 Stored-SDF bodies at n=64;
   or a pre-baked pool arrives via `SetRecipePool` — the editor path.
2. The recipe bake (`BakeSdfWorld`, `SdfBake.h:74-172`) **scans every cell of the full [0,n)³ grid
   twice**: pass 1 evaluates the field at every cell (occupancy, `:107-114`); pass 2 re-iterates
   the full grid but evaluates only active-brick cells (`if (!activeBrick…) continue;` at
   `:148-149`), creating **one ECS entity per populated voxel** (`:167`).
3. `LaineKarrasOctree::rebuild` (`SVORebuild.cpp:228-736`) discards the old tree and reconstructs
   everything from a full ECS-world query (`:291-292`), then runs per-brick DXT + normal derivation
   for every populated brick (Phase 4, `:613-735`).
4. `SerializeSdf` re-reads all 512 voxels of every brick via per-voxel world-space hash lookups
   (`ShellOctreeGpu.h:497-611`), and `ConcatenateSdf` glues all octrees into monolithic byte blobs
   with absolute per-octree base offsets stamped at concat time (`:760-812`).
5. Any content change goes through `Rematerialize`: `vkDeviceWaitIdle` + re-bake of **every
   registered recipe** (`RecipeBaker.h:52-107` loops all ids) + destroy/recreate of all octree GPU
   buffers (`BodyOctreeSceneNode.cpp:631-648` @c1c87fb6). The editor does this **on every layer
   toggle** (`EditorApplication.cpp:191-233`).

### 1.2 The laziness we think we have is thinner than documented

Three findings from the 2026-07-10 sweep that change the starting picture:

- **The mip fallback is not fed on any default production path.** `ConcatenateSdfWithMips`
  (`MipBake.h:322-372`) has no non-test caller — both standing production paths call plain
  `ConcatenateSdf` (`BodyOctreeSceneNode.cpp:463` @c1c87fb6, `RecipeBaker.h:88`), so the default
  app's mip pool is a 1-byte placeholder (`BodyOctreeSceneNode.cpp:572-581` @c1c87fb6). The one
  exception is new: the flag-gated `VIXEN_TIER_CROSSING_DEMO` path merged with Tiered-ESVO Inc2
  bakes real mip pools via `BakeAndAttachMipPool` + a manual `ConcatenatedOctrees` assembly
  (`BuildRenderGraph.cpp:619-624, 690-717` post-merge) — a **third** pool-construction path Inc0
  must count. **Failure-mode correction (from review):** with an empty mip pool, a
  `brickResident==0` **leaf** is a *miss* — the body renders **invisible**, not grey
  (`MipFallback.glsl:66-68` returns false on zero coverage → traversal advances past the leaf);
  neutral grey appears only at the non-leaf LOD-cutoff and tier-crossing fallbacks. This raises
  the stakes of flipping anything to lazy before mips are fed (§6 Inc0).
- **Even lazy upload is off in practice.** `residencyRequested_` defaults to `true`
  (`BodyOctreeSceneNode.h:225`), so boot still uploads the whole brick blob eagerly. Residency is
  **one bool for the entire concatenated pool** — `RequestBrickResidency(bool)`
  (`BodyOctreeSceneNode.cpp:166-174` @c1c87fb6; :177-185 post-merge) stashes it, `UploadBrickPool`'s
  gate consumes it (`:782-787` @c1c87fb6), and `PollBrickUploadCompletion` stamps `brickResident=1`
  into **every** config at once.
- **The incremental primitives are dead ends by design.** `insert`/`remove` were deliberately
  removed (`LaineKarrasOctree.h:138-144` @c1c87fb6; :151-157 post-merge); `updateBlock` patches only
  the CPU brick-view map, never hierarchy masks/serialization/mips, test-only callers
  (`SVORebuild.cpp:742-823`); `dirtyBricks_` has no producer (`BodyOctreeSceneNode.h:299` @c1c87fb6).

So today: generation is 100% eager and whole-dataset; upload laziness is shipped but dormant on
default paths; nothing incremental survives contact with production.

### 1.3 Why now

- The doctrine is already written down: [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] ("bedrock can be
  PROCEDURAL — bottom trees generated on demand via the recipe/kernel pipeline (recipe + edit
  overlay)") and [[Tiered-ESVO-Observer-Addressing-Design-2026-07]] §6 ("(TierAddress, seed) →
  base geometry … populated on demand"; delta/persistence/eviction deliberately unspecified).
  This doc is that deferred piece, promoted to the **engine baseline** for every scene — not a
  planetary-tier special case.
- **Tiered-ESVO Inc2 shipped and merged to `main` 2026-07-10** (merge `2d67840e`, M1-M5 complete;
  the parallel codex-worktree M3 line is an ancestor of the merge). The shipped code treats
  `TierRef::childOctreeIndex` as always a valid, dereferenceable `configs[]` slot (now stated as a
  comment in the merged shader, `BodyInstanceRayMarch.comp:742` post-merge; M2's round-trip test
  requires it, M4's gate peeks `configs[childOctreeIndex].brickResident` directly) — i.e. a tree
  that *hasn't been generated at all* has no representation. The baseline transition is what
  supplies that missing state, and the merged `TierRef`/leaf-hit code is the natural place to add it.
- Undertow's side of the split already exists on paper: `Reify(entity, seed, deltaLog, t) →
  ConcreteState` (undertow `2026-07-05-reification-design.md`, FUTURE) and the shipped
  deltas-only campaign save (seed-regen t₀ base + sparse dirty-set override) prove the pattern in
  the sim. VIXEN adopting the same base+delta shape render-side closes the loop.

## 2. The model (what "baseline" means after the transition)

**A scene is a set of content references, not a dataset.** Each body/region is identified by an
address and described by `(recipe program ref, param/seed block, delta chain)` — or, for
non-procedural content, by a **stored voxel asset ref**. Rendering a scene requires pushing
*instructions* (recipe bytecode / compiled recipe kernels — bytes to KBs), *deltas* (sparse — most
content has none, ever), and the voxel payloads of whatever genuinely stored content the scene
contains. No voxel payload crosses the CPU↔GPU boundary for unedited procedural content.

**Content sources form a spectrum, and all of it renders (user clarification 2026-07-10):**
*pure procedural* (recipe only — zero voxel traffic) → *procedural + delta* (recipe + sparse
overrides — traffic is the delta) → *voxel asset* (imported/authored voxel data with no procedural
base — traffic is the asset, paid once and paged). The baseline does not forbid stored voxels; it
ensures stored voxels are the only thing that ever *costs* voxel bandwidth. A voxel asset is born
materialized and simply skips the producer's base-evaluation step; it uses the same paged pool,
per-brick residency, mip ladder, and eviction machinery as materialized procedural regions.

Three representational states per region, forming a ladder the renderer moves along on demand:

| State | What exists | Renders via | When |
|---|---|---|---|
| **Virtual (instructions-only)** | recipe ref + transform/params + derived bounds | direct GPU field evaluation (generalized `PROVIDER_PROCEDURAL`), bounded by a per-program conservative bound + a **new** per-instance coarse occupancy structure (§4.1 — the shipped node-ordinal mip pool is *not* reachable here) | the DEFAULT for all unedited content |
| **Materialized (cached)** | produced subtree/bricks in the pool | ordinary ESVO march (today's path, unchanged shader semantics) | when a delta forces it, or when the perf policy elects it (deep programs where a baked march is cheaper than field eval) |
| **Coarse (mip-only)** | mip ladder samples only | existing `brickResident==0` → mip-sample fallback | far / produced-then-evicted content — the shipped Sparse-Mip mechanism, finally fed in production |

Note (from review): virtual renders a smooth analytic iso-surface; materialized renders the
voxelized iso-surface at bake resolution — the **virtual↔materialized transition is a visible
representation change**, and the perf policy elects it precisely for near/large bodies. Transition
quality (resolution-matched materialization, fade/dither) is a §4.1 policy concern, and gates must
compare geometry with stated tolerances, not expect pixel equality (§6 Inc1).

**Deltas are tiered by expressiveness, cheapest first:**

1. **Recipe-delta** — the edit is an instruction-space operation (CSG-subtract the cut shape,
   append/patch ops in the program). The region stays *virtual*: re-render from the amended
   instruction stream, zero materialization, zero voxel traffic. The `.vxd` layer stack is already
   exactly this shape (non-destructive, replayable, program-level — `EditorDocumentModel.h:104-134`);
   it generalizes from "toggle a layer" to "append an edit op." **Cost honesty (from review):** with
   codegen-per-recipe (§4.1), a *structural* recipe-delta implies a runtime shader-recompile hitch;
   *param-only* edits avoid it once `ReadParam` lands — which is why that dependency is pulled
   forward (§8.2).
2. **Materialized delta** — the edit is not (efficiently) expressible in instruction space
   (arbitrary per-voxel painting, imported scan data). Stored as sparse **brick-level absolute
   overrides keyed by region address**, applied by the producer over the freshly evaluated base
   when the region materializes. Absolute values (not diffs against base bytes) because the base is
   only per-build deterministic (unordered_map phases in `SVORebuild`, libm/GPU transcendental
   drift) — the delta must tolerate epsilon *value* drift. **Topology drift is the sharper problem**
   (from review): brick existence is a threshold function of the drifting field, and this program
   itself will change the occupancy algorithm (§4.2) — so delta records must be
   **topology-self-sufficient and producer-versioned** (§4.4), not assume the base always allocates
   the same bricks. For **sim-caused divergence** (damage, mining), undertow's delta log remains the
   authoritative persistent record ([[Tiered-ESVO-Observer-Addressing-Design-2026-07]] §6 + the
   reification spec); VIXEN's materialized record of the reified bake is evictable cache, never the
   persisted delta.
3. **Compaction (v2+)** — a background pass that fits materialized delta data back into instruction
   form (error-bounded inverse fitting: merge many carve ops, replace a materialized brick with a
   fitted primitive subprogram when fit error < ε), shrinking the materialized set over time.
   Related to (not the same as) the clone-aware dedup direction in
   [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] — dedup shares identical produced content; compaction
   *eliminates* produced content by re-proceduralizing it. Explicitly not v1.

**The producer** — `(recipe, region address, LOD, delta chain) → subtree topology + brick/channel
payloads` — is the one new load-bearing abstraction. It runs CPU-side in v1 (worker threads driving
the pure `evalRecipe` VM per region, direct-to-pool, no ECS round-trip) behind a
**placement-agnostic contract**, so a GPU compute producer can replace it in a later increment
without redesign. This is GigaVoxels' "production" concept ([[GigaVoxels-Streaming]], Crassin
thesis Ch.7) expressed through VIXEN's existing seams.

**GPU residency is always a cache over persistent artifacts.** Eviction is safe for
pure-procedural regions (regenerate from instructions), safe for delta'd regions as long as the
delta store is the persistent thing (regenerate base + re-apply), and safe for voxel assets
(reload from the stored artifact). This inverts today's ownership: the persistent artifacts are
*(recipes, deltas, voxel assets)*; GPU-resident octrees/bricks/mips become derived, budgeted,
reproducible state everywhere.

### 2.1 Region identity (decision: unify, both layers)

Per user decision 2026-07-10: **both** region layers, unified under one addressing scheme:

- **Subtree-scale laziness** rides the Tiered-ESVO mechanism: a lazily-known region is a
  `TierRef`-shaped edge. For a *virtual* region the edge resolves to a **recipe reference**
  (instructions + transform), not a resident child tree — the "not yet generated" state the merged
  Tiered-ESVO code cannot represent (§1.3) becomes first-class: `virtual (recipe ref) →
  materialized (configs[] slot) → evicted (back to recipe ref)`.
- **Brick-scale laziness** inside a materialized tree keeps the existing per-brick machinery
  (`brickGridLookup` sentinel, mip fallback) — payload paging below the subtree granularity.

The address vocabulary is `TierAddress` ([[Tiered-ESVO-Observer-Addressing-Design-2026-07]] §4) so
render residency, delta keys, and (eventually) undertow reification all speak one key. For v1
single-body scenes the address degenerates to (body id, brick coord) — the scheme must not *require*
multi-tier scenes to be useful.

## 3. What already exists (reuse inventory)

| Need | Existing primitive | State |
|---|---|---|
| Pure per-point base evaluator | `Recipe::evalRecipe` stack VM (~87 opcodes, closed constant programs) | live, CPU, bake-time only — region-scoping is a new *driver*, not a new evaluator |
| Zero-bake GPU render of procedural bodies | `PROVIDER_PROCEDURAL` in-shader field march (`BodyInstanceRayMarch.comp:1148` post-merge; `BuildRenderGraph.cpp` providerKind=1 bodies) | live, but hard-coded to 2 analytic recipes in `SdfRecipes.glsl`, with hand-derived per-recipe bounds (§4.1) |
| Arbitrary-recipe GPU evaluation | bytecode→HLSL codegen `EmitProceduralComputeShader` + glslang runtime compile ([[Runtime-Kernel-Pipeline-Direction-2026-06]]) | parity-tested, test-only, **standalone program shape** — not directly composable into the GLSL uber-shader (§4.1) |
| Graceful "render before content exists" | mip ladder + `brickResident==0` fallback + `brickGridLookup` 0xFFFFFFFF sentinel (Sparse-Mip Inc1/2) | shipped; fed only by the flag-gated Inc2 tier-crossing demo — dormant on default paths (§1.2); node-ordinal-addressed, ESVO-traversal-only (unreachable for virtual bodies) |
| Residency demand signal | `InstanceWantsBrickResidency` (frustum + resolvability; deliberately no occlusion param) composed with `IsOccludedByResidentTrees` in `UpdateBodySceneResidency` | live, per-instance evaluation, whole-pool grant |
| Async streamed upload | `BatchedUploader` (supports offset uploads — unexercised, every call site passes offset 0), `UploadBrickPool` poll state machine | live for whole-blob; offsets unused |
| Producer/eviction interface sketch | `ISVOStreamingManager` (Morton-keyed `requestLoad`/`evictBrick`, priorities) | header-only stub, no impl, no callers — shape matches, decide revive-vs-delete at plan time |
| Replayable program-level edit list | `.vxd` VoxelDocument layer stack | live (editor), program-level only |
| Cross-tree reference + traversal restart | `TierRef`/`farBit==1` + fresh-stack restart (Tiered-ESVO Inc2) | **SHIPPED — merged to main 2026-07-10 (`2d67840e`)** |
| Sim-side base+delta doctrine | undertow reification spec + shipped deltas-only campaign save | spec'd / shipped (sim-side) |

## 4. Architecture

### 4.1 Instruction-direct rendering (the new default path)

Generalize `PROVIDER_PROCEDURAL` from "2 hard-coded analytic recipes" to "any registered VRC
recipe program." **The honest composition shape (from review):** the parity-tested codegen artifact
(`EmitProceduralComputeShader`) emits a *self-contained standalone HLSL program* (own trace main),
while the render path is a single ~1350-line GLSL uber-shader whose provider dispatch is a
compile-time static call — GLSL has no function pointers and glslang cannot mix HLSL functions into
a GLSL translation unit. So Inc1's real shape is:

- a **new GLSL field-function emitter** (or a GLSL re-target of the existing HLSL AST visitor —
  the kernel framework already dual-targets, so this is emitter work, not new codegen
  architecture), producing `float sdfRecipe_<id>(vec3 p)` functions;
- **uber-shader regeneration**: registered recipes are textually merged with a `recipeId` switch
  into `BodyInstanceRayMarch.comp`, runtime-compiled via the existing glslang path
  (`BuildRenderGraph.cpp` already compiles this shader from source at startup) +
  `vkCreateComputePipeline`;
- consequence: **recipe registration and structural recipe-deltas cost a pipeline recompile**
  (measured budget required; mitigations: async compile with the old pipeline serving until swap,
  and `ReadParam` pulled forward so param-only edits are free — §8.2). The alternative
  (per-recipe standalone pipelines) requires a multi-dispatch depth-composition restructure and is
  rejected for v1 (§9).

**Safety bounds are per-program derivations, not free (from review).** The shipped procedural path
is safe only because both bounds are hand-derived for its 2 recipes: an in-shader bounding *sphere*
from recipe params (`boundR = params.x + maxDisp + 0.01`) and an analytic Lipschitz step relaxation
(`stepScale = 1/(1 + amp·freq·√3)`), `SdfRecipes.glsl:42-59`. `BodyInstanceGpu` carries **no AABB**
(worldPos + renderScale only; renderScale unused for procedural). An arbitrary 87-opcode program
(Twist/Bend/Repeat/displacement break the Lipschitz-1 assumption) therefore needs recipe metadata
to carry — or codegen to derive — a **conservative bounding volume and a step-relaxation factor**.
V1: authored/registry-stored bounds + a conservative global relaxation; interval-derived bounds are
the upgrade path (§8.1). The procedural branch also lacks the ESVO path's front-to-back
`entryT>bestT` early reject — N overlapping virtual bodies currently cost N full traces per pixel —
so extending that reject is in Inc1's scope.

**Acceleration + graceful degradation for virtual bodies is NEW machinery, not mip-pool reuse
(from review).** The shipped mip pool is addressed by ESVO node ordinal and read only inside
octree traversal (`MipFallback.glsl:10-14,52`); a virtual region has no node array, so no shader
path can reach it. Virtual bodies get a small **per-instance coarse occupancy/bounds structure**
(own binding, sampled in the procedural branch — e.g. a low-res occupancy grid or coarse
sample tree produced from the recipe) serving both empty-space skipping and far-LOD shading. Its
production has the same conservativeness question as §4.2 (see §8.1): point-sampling coarse cell
centers is **not** conservative (the narrow band is ~2.5 voxels; thin features vanish), and a
conservative interval evaluator over the full opcode catalogue does not exist today.

Perf policy, not correctness, then decides when a hot virtual region is *elected* for
materialization (deep program + near camera + large footprint → bake it; the baked march is the
fast path, the field eval is the always-available path), with transition quality handled per §2.

### 4.1a Resolvability-gated recipe evaluation ("mip for compute", user direction 2026-07-12)

**Distinct axis from residency/materialization.** §4.3's mip ladder and §2's Virtual→Materialized→
Coarse ladder both answer *what data is resident* — a storage/bandwidth question. This is a third,
orthogonal axis that answers *how much of the recipe graph is worth evaluating at all* — a
**compute** question, and it applies whether or not the region is ever materialized. User framing
(verbatim intent): looking at a far planet through a telescope, fine surface detail the sampling
resolution cannot resolve should never be paid for computationally — not "load a coarser mip
instead of the fine one" but "don't walk the part of the recipe tree that only affects detail
below the sample's resolving power." Mechanism is tree-pruning against a per-node feature-scale
bound, not array indexing — the mip pool's ordinal-addressed storage was already ruled out as
unreachable for virtual content (above); this reuses the mip *principle* (resolvability gates
detail), not the mip *pool*.

Sketch of what this needs, reusing existing hooks rather than new ones where possible:

- **A resolvability signal at the sample site.** `InstanceWantsBrickResidency`'s footprint/distance
  cone math (`ResidencyTrigger.h`, `raySizeCoef`) already computes "smallest feature this sample
  could distinguish" for the residency decision; the procedural branch needs the same number
  (world-space feature-scale-at-this-ray) available per field-evaluation call, not just per
  residency poll.
- **Per-node feature-scale metadata on the recipe program**, analogous to but distinct from the
  per-program bounding sphere + Lipschitz step-relaxation metadata already shipped for Inc1
  (`SdfRecipes.glsl:42-59`, registry metadata v1). Where the shipped metadata bounds *where* the
  surface can be, this would bound *how fine* a subtree's contribution is (e.g. a displacement/
  noise/detail node tagged with the smallest feature size it introduces).
- **A prune point in the codegen'd field-function / VM walk**: once footprint-at-sample exceeds a
  subtree's feature scale, skip evaluating that subtree (return its parent's value, or a cheap
  bound) instead of descending. This is a codegen-time or VM-time branch, not a new storage
  structure — cost is saved GPU cycles per sample, not saved bytes.
- **Interacts with, does not replace, Open Decision #1** (conservative recipe evaluation, §8): that
  decision is about occupancy correctness (does a thin feature get missed at build/produce time);
  this is about per-sample compute cost at *render* time for content that is already virtual and
  never gets materialized at all. An interval/feature-scale VM built for one can likely inform the
  other, but the two questions (is the tree topology I chose to materialize correct vs. how deep do
  I evaluate a tree I'm not materializing) are separate and should not be conflated into one
  open decision.
- **Relationship to §8 Open Decision #9** (virtual↔materialized transition quality): this axis is
  what makes staying virtual cheap enough, at distance, to be the right perf-policy choice more
  often — reducing pressure to materialize far content at all, rather than only handling the
  transition once perf policy has already decided to promote.

Not scoped to an increment yet — natural home is alongside Inc1's per-program bounds/relaxation
metadata (same registry, same codegen pass) or as an Inc4 policy item once the feature-scale
metadata question is answered. Recorded here as a named, distinct concern so it isn't lost inside
Open Decision #1's occupancy framing.

**Prior art (research pass, 2026-07-12).** Not a novel problem class; the closest published
mechanism is **Lipschitz Pruning** (Barbier et al., *CGF* 2025 / Eurographics Best Paper honorable
mention, Adobe Research) — prunes CSG/primitive SDF trees in real time on GPU using each node's
Lipschitz bound (`f/K` normalization) to determine when a subtree's contribution is negligible at
a given spatial scale; the closest thing to a general algorithm for "prune a tree of implicit-
surface ops by per-node feature scale" that exists today, though it triggers on region size for
offline tree simplification, not per-sample screen-space footprint. The footprint/resolvability
half of the problem is a much older, standard solved problem: Amanatides' cone tracing (SIGGRAPH
1984, the original "resolve the correct LOD for a procedural model from a cone's footprint")
and Igehy's ray differentials (SIGGRAPH 1999, the RenderMan/pbrt-standard mechanism); Quilez's
sphere-tracing articles note a footprint estimate falls out of the march itself at near-zero extra
cost. The noise/displacement special case (clamp fBm octave count by distance) is standard,
decades-old terrain-shader practice, not a citable paper — a strict subset of this idea already
shipped at scale elsewhere. Adaptively Sampled Distance Fields (Frisken et al., SIGGRAPH 2000) is
the classical ancestor linking octree depth to resolvable feature size, spiritual predecessor of
both the shipped mip system and Lipschitz Pruning. GigaVoxels' LOD/streaming machinery is confirmed
storage/streaming-side only (screen-space projected node size gates what to *voxelize*), not a
precedent for generation-time compute pruning. Net: the pieces are individually established and
adoptable as-is (footprint via ray differentials/cone estimate; octave clamping for noise-class
opcodes); per-opcode feature-scale bounds for the full ~87-opcode catalogue need adaptation from
Lipschitz Pruning's primitive/smooth-CSG formulas; wiring pruning as a **runtime per-sample gate**
inside the stack-VM/codegen (vs. Lipschitz Pruning's offline tree-simplification pass, which doesn't
fit VIXEN's live-editable recipes) has no direct precedent and is original design work.

**Working design (user-derived, 2026-07-12), building on the prior art above.** Two distinct
composition rules for the two opcode categories, not one:

- **Compositional/pass-through opcodes** (union, intersect, smooth-blend, material/color ops — the
  majority of the catalogue): introduce no new geometric frequency, so they need no per-opcode
  feature-scale logic at all — bottom-up feature scale is just the min (finest-dominates) of their
  children's.
- **Domain-transforming opcodes** (translate, rotate, scale, shear, Twist, Bend, and other
  position-stack warps): don't inject detail either, but they distort the mapping between a
  footprint measured in parent/world space and the same footprint measured in the child's local
  domain. Each such opcode exposes a **self-contained local Jacobian function**,
  `J_opcode(position, opcode_params) -> 3x3 (or 3x4 affine)`, evaluated at the current sample
  position — not a single precomposed constant matrix. Affine opcodes (translate/rotate/uniform or
  static non-uniform scale) naturally degenerate to a position-independent `J`; nonlinear opcodes
  (Twist, Bend, param/noise-driven scale) return a true function of position, since their local
  rate of domain distortion varies across the domain (e.g. Twist's effective rotation-per-unit-
  height is constant, but the resulting linear speed at a point scales with its distance from the
  twist axis). The opcode owns its own `J_opcode` — the pruning machinery never special-cases
  opcode types from outside, matching how each opcode already owns its own field-eval and
  HLSL/GLSL codegen emission. `J_opcode` is a natural sibling of the existing per-opcode
  position-warp function already evaluated during the position-stack walk (same input, computed
  alongside it, not a second pass): at each warp node, multiply `J_opcode(pos, params)` into the
  running accumulated transform threaded down the position-stack, then recurse with the warped
  position as today.
- **Scale-injecting opcodes** (Noise/fBm, Displace, Repeat/tile): these are the only nodes that
  need an actual feature-scale *bound*, as a scalar (e.g. noise's highest-octave wavelength,
  Repeat's tile period) — compared against the accumulated, Jacobian-transformed footprint at that
  point in the tree to decide whether to prune below this node.
- **Prune test**: at a scale-injecting node, transform the current sample footprint through the
  accumulated `J` chain from root to here, and skip evaluating the node's detail contribution if
  the transformed footprint exceeds the node's feature-scale bound.
- **Conservativeness caveat**: a per-sample Jacobian is a local-linearization of the warp at that
  point. It is only a valid footprint estimate where the warp is well-approximated as linear over
  the footprint's own spatial extent — a large footprint (far/coarse sample) combined with a
  high-curvature or fast-varying warp (tight Twist, sharp Bend) can make the linear estimate
  underestimate true distortion, risking an incorrect prune. Same failure shape as Open Decision
  #1's occupancy conservativeness problem (point/derivative sampling can miss what a full
  evaluation would catch), same three-way resolution menu: bound the curvature term (costs more,
  exact-er), accept dense/finer sampling near high-curvature warps, or accept non-conservative
  pruning under a documented miss policy.

### 4.2 The producer (materialization on demand)

```
ProduceRegion(recipeRef, regionAddress, lod, deltaChain, neighborBoundaryDeltas) ->
    { subtree topology, brick payloads (per channel, incl. apron), mip samples for the produced levels }
```

- **v1 placement: CPU worker threads** (bake is single-threaded today; the two n³ loops run on the
  calling thread — the producer introduces the job infrastructure). Placement-agnostic contract:
  inputs/outputs are owned pool-format bytes + a completion signal, never borrowed spans or ECS
  state, so a GPU compute producer can be substituted later. The scheduler must have one owner
  (Gaia background jobs or an adapter to the engine/TBB scheduler), and workers must not mutate the
  shared `GaiaVoxelWorld`; see
  [[../03-Research/Gaia-Bulk-Voxel-Mutation-and-Upload-Research-2026-07]].
- **Direct-to-pool**: the ECS per-voxel round-trip (create entity per voxel at `SdfBake.h:167`,
  re-read per voxel at `ShellOctreeGpu.h:556-608`) is pure overhead for procedural content and is
  bypassed — the producer writes node/brick/channel bytes directly. `GaiaVoxelWorld` remains the
  substrate for *authoring-time* edited voxels (per-voxel-mutable with Morton lookup), not a
  mandatory intermediate for base content.
- **Topology on demand is a correctness-bearing change, not an optimization (from review).**
  Today's topology is exact *because* pass 1 exhaustively evaluates every cell (`SdfBake.h:107-114`).
  Deriving occupancy from node-level predicates requires **conservative recipe evaluation** —
  point-sampling misses thin features, and that miss is permanent (the brick never exists). The
  three honest options are an open decision that gates this section and §4.1's coarse structure
  (§8.1): (a) an interval/Lipschitz-bound VM over the ~87-opcode catalogue (substantial new engine
  component: per-opcode interval rules incl. Twist/Bend/Repeat and discontinuous math ops); (b)
  dense fine-grid evaluation *per region* (keeps exactness, forfeits the few-evals claim — still
  region-scoped and parallel); (c) accept non-conservative occupancy with a specified miss policy.
- **Region-boundary apron (from review).** The GPU trilinear/gradient stencils gather corners
  across brick boundaries; an unallocated neighbor returns the 1e9 sentinel and corrupts the face
  sample/normal — a previously-shipped seam-artifact class the whole-grid bake avoids via its
  1-brick dilation skirt (`SdfBake.h:118-141`). Per-region production must preserve that
  invariant: produced pages carry an **overlapping apron evaluated as base ⊕ neighbor deltas**
  (hence `neighborBoundaryDeltas` in the contract), or region materialization force-includes
  neighbor boundary bricks. A two-adjacent-regions seam test (one delta'd, materialized in both
  orders) is a mandatory Inc2/Inc3 gate.
- **Delta application** happens inside the producer: evaluate base region → splat materialized-delta
  brick overrides → emit. Recipe-deltas never reach the producer (they amended the instruction
  stream upstream).
- **Determinism contract**: `(recipe bytes, region address, params/seed, lod) → payload` is
  reproducible per build. Bit-exactness across toolchains/devices is explicitly NOT promised —
  which is why materialized deltas are absolute overrides carrying their own topology (§4.4), and
  why content hashes (future dedup/compaction) must be computed over *produced* bytes, not
  re-derived on other machines.

### 4.3 Pool, residency, eviction

- **Paged pool with indirection** replaces monolithic-with-absolute-offsets concatenation: a
  slot/page allocator for node ranges and brick pages so a produced region can be inserted or
  evicted without re-concatenating and without `Rematerialize`'s device-wait + destroy/recreate.
  **Precondition (from review): fixed reserved capacity.** Pool buffers are created exactly-sized
  and published to the graph once at Compile; there is no post-compile buffer swap short of the
  Rematerialize recompile. So the paged pool avoids stalls only *within* a reserved budget chosen
  by admission control; exhaustion policy is evict-first, resize-as-last-resort (an accepted
  Rematerialize-shaped stall). 32-bit base offsets / `uint` element indexing cap each pool's
  addressable range — the budget ceiling is structural. This allocator is also the enabling
  condition for Tiered-ESVO's *dynamic* child trees (`ConcatenatedOctrees` is build-time-static;
  every per-tree base offset needs rebasing to append today).
- **Keyed residency**: `RequestBrickResidency(bool)` generalizes to keyed requests (region address →
  request queue → producer → paged insert → per-region GPU-visible residency), replacing the
  single whole-pool bool. The existing stash-then-service-in-ExecuteImpl pattern and the
  pendingHandle+Poll async state machine generalize directly (produce → upload → flip flag).
- **Demand signal**: v1 stays CPU-side camera analytics (extend the `UpdateBodySceneResidency`
  composition to per-region). A GPU ray-observed request buffer (flat, timestamp
  self-deduplicating, GigaVoxels thesis §7.3.3) is the recorded candidate for v2 — **the Sparse-Mip
  Inc2 M4 "no-build" decision is formally superseded by its own flip-trigger 2** (nested-tree work
  has now shipped) and must be re-taken with evidence when this program schedules the GPU-feedback
  increment, not inherited. Note VIXEN has no stream-compaction/prefix-sum primitive (verified in
  the M4 log) — a prerequisite if that path is taken.
- **Eviction** exists for the first time, and is trivially safe by construction (§2): evict =
  free pages + revert the region's state to virtual/coarse; the mip ladder keeps produced-then-
  evicted regions renderable. Budgets: the recipe pool's post-hoc `byteBudget` bake gate
  (`RecipeBaker.h:92-103`) becomes real admission control on the paged pool.

### 4.4 Delta store

- **Keying**: region address (§2.1) → ordered list of materialized-delta brick records
  (channel-tagged absolute payloads). Recipe-deltas live in the document/program layer
  (`.vxd` generalization), not here.
- **Topology self-sufficiency + producer versioning (from review).** Each delta record stores the
  override region's brick-existence set (or subtree topology snapshot) alongside payloads, and the
  journal stamps the **producer-algorithm version** it was recorded against, with a defined
  migration path. Rationale: brick existence is a threshold function of a base field that is only
  per-build deterministic, and this program itself changes the occupancy algorithm (§4.2) — an
  un-versioned journal keyed to "whatever bricks the base allocates" is invalidated wholesale by
  either. Inc3's replay gate must include a cross-build (ideally cross-machine) case — same-build
  replay structurally cannot expose this failure.
- **Persistence**: a new VDC-container chunk (kernel-C# canonical schema, same single-source
  codegen discipline as VRC1/ViewBlob — [[kernel-codegen-framework-direction]]) so the delta
  journal is a first-class, versioned, replayable artifact. The in-memory `ActionStack` (lambdas,
  `ActionStack.h:57-66`) gains a serializable entry form at the same time — undo history and the
  persisted journal should share one representation (KI-016, the broken live editor undo, gets
  fixed or absorbed by this work — it gates any editor-facing milestone).
- **Ownership boundary — extended on the VIXEN side, undertow side unchanged.**
  [[Tiered-ESVO-Observer-Addressing-Design-2026-07]] §6 deferred the delta format without granting
  it to VIXEN; this doc *extends* that boundary by giving VIXEN a persisted store for
  **authored/render-side** deltas. For **sim** divergence the reification split is untouched:
  undertow's delta log is the authoritative persistent record; reified concrete state arrives and
  is baked like any edited region, and VIXEN's materialized result stays evictable cache — VIXEN
  never becomes a second source of truth for sim history. The shared vocabulary is the address.

### 4.5 Incremental update (edits without Rematerialize)

An edit (either kind) invalidates exactly its region: recipe-delta → amend + re-push instructions
(param-only edits are free once `ReadParam` lands; structural edits pay the §4.1 pipeline recompile,
served async); materialized delta → re-produce affected bricks + **bottom-up dirty-path mip refresh
(O(depth) per edit)** — the refresh the Sparse-Mip direction doc assumed but which was never built
(`BakeMipPool` is whole-tree only, `MipBake.h:155-287`). Per-region offset uploads via
`BatchedUploader` replace whole-pool re-upload. `Rematerialize` survives only as the fallback for
structural scene changes and pool resize.

The detailed publication contract is
[[../03-Research/Gaia-Bulk-Voxel-Mutation-and-Upload-Research-2026-07]]:
parallel immutable page assembly → generation-checked single-owner metadata commit → coalesced
offset uploads → timeline-complete atomic page-table flip → deferred retirement. It also records the
legacy grouped Gaia `copy_n` candidate for authoring-time per-voxel storage and rejects the current
multi-worker `VoxelInjectionQueue` path.

## 5. Seam map

| File / area | Role today | Change |
|---|---|---|
| `SdfBake.h` (`BakeSdfWorld`, `BakeRecipeInstructionsToSdfWorld`) | whole-grid two-pass bake driver | gains region-scoped producer driver (per-region eval, direct-to-pool, no ECS, apron rule); whole-grid path retained for tests/tools |
| `SVORebuild.cpp` (`rebuild`) | from-scratch whole-world build | producer emits subtree topology per region; occupancy per §8.1's conservative-evaluation decision. The eager whole-rebuild remains for non-procedural sources |
| `ShellOctreeGpu.h` (`SerializeSdf`, `ConcatenateSdf*`, `ConcatenatedOctrees`) | monolithic serialize/concat, absolute offsets | paged pool + indirection (fixed reserved capacity); incremental region insert/evict; `ConcatenateSdfWithMips` becomes the production path |
| `MipBake.h` | whole-tree mip bake; wired only into the Inc2 demo (`BakeAndAttachMipPool`) | production wiring on all pool paths; dirty-path O(depth) refresh |
| `BodyOctreeSceneNode.*` | eager Compile bake; whole-pool residency; Rematerialize | keyed residency service; paged buffer ownership; region-scoped upload; Rematerialize demoted to fallback |
| `ResidencyTrigger.h` / `VulkanGraphApplication` | per-instance camera trigger (+ separate occlusion gate), whole-pool grant | per-region requests; later: GPU request-buffer feedback (M4 re-open) |
| `BodyInstanceRayMarch.comp` + `SdfRecipes.glsl` | 2 hard-coded procedural recipes; leaf-hit brick dispatch (now incl. merged Inc2 tier-crossing branch) | generalized instruction-direct provider (regenerated uber-shader + recipeId switch); per-instance coarse-occupancy sampling; bestT early-reject in the procedural branch; virtual-region branch coordinated with the merged tier-crossing code |
| new: GLSL field-function emitter (kernel framework) | HLSL standalone emitter exists (test-only) | GLSL re-target emitting composable `sdfRecipe_<id>` functions |
| `Recipe/` (`RecipeRegistry`, `RecipeBaker`) | whole-registry eager bake, post-hoc budget | recipe refs as first-class render content (+ per-program bounds/relaxation metadata); producer entry; admission-control budget |
| `EditorApplication` / AppFlow | re-flatten + full re-bake per toggle | recipe-delta path (program patch); materialized-delta authoring; serializable ActionStack (KI-016) |
| new: `RegionProducer` (lib TBD) | — | the placement-agnostic producer contract + CPU v1 impl + job infrastructure + apron policy |
| new: delta store (VDC chunk) | — | address-keyed, topology-self-sufficient, producer-versioned delta journal (kernel-C# canonical schema) |
| (merged) `TierRef`/`TierRefTable` | always-valid child slot (shipped invariant) | gains virtual state: recipe-ref payload for not-yet-materialized children |

## 6. Increment sequencing (proposal — plans to be written per increment)

- **Inc0 — activate the shipped laziness, scoped to mip-capable content (small, immediate).**
  Wire `ConcatenateSdfWithMips` into the Stored-SDF and recipe-pool production paths (the Inc2
  demo's `BakeAndAttachMipPool` is the working precedent); flip the residency default to lazy
  **only for trees with real mip content**. The default boot scene is 3 *binary* shell octrees
  (`channelCount==0` — mip samples are structurally impossible, and a mip-less lazy leaf renders
  **invisible**, §1.2), and the residency bool is whole-pool — so either binary shells stay eager,
  or the default scene converts to a mip-capable format in this increment; a per-content-format
  flip needs at least a per-tree grant, which is Inc2 territory. Also: benchmark per-region
  generation cost (the number the vault asserts but never measured — "min(generation, transfer)").
  **MEASURED (Inc0 M3, commit `c59211fd`, Opus-validated): the "min(generation, transfer)" framing
  is WRONG — generation is the DOMINANT term.** Standard single-sphere n=64/band=2.5/depth=3 bake:
  ~6.26 MB generated in ~1.5–1.9 s median (WSL2 CPU, Release) = **~3–4 MB/s**, i.e. **2–3 orders
  of magnitude slower than transferring the same bytes** (1 GB/s floor → ~6.3 ms; ~200–300×). Cost
  split: fused pass-2 eval+ECS-createVoxel ~68%, `rebuild()`+DXT ~22%, `SerializeSdf` ~8%; dense
  eval / mip bake / concat negligible. **So the bandwidth win is from AVOIDANCE — never generating
  far/occluded/lazy regions at all — NOT from generation racing transfer for materialized regions.**
  Consequence: the conservative-evaluation / occupancy machinery that decides *which regions to skip*
  (§8.1) is the load-bearing part of the instructions-first economy, not generator throughput.
- **Inc1 — instruction-direct rendering. DONE (M1-M6, 2026-07-10/11).** Arbitrary registered
  recipes render with ZERO bake: GLSL field-function emitter → uber-shader splice +
  `recipeId` switch → runtime compile via `ShaderBundleBuilder`; per-program conservative
  bounds + step relaxation (registry metadata v1, M5 Task 10); per-instance coarse occupancy
  grid for empty-space skip + far shading (M6 Task 13 — 64³ dense-eval → 16³ conservative
  min-|sd| downsample, half-cell-diagonal Lipschitz margin, random-probe-verified); bestT
  early-reject in the procedural branch (M5 Task 12). **Recompile is SYNCHRONOUS, registered
  before Compile()** — not the "async, measured recompile budget" this section originally
  proposed; async re-splice was descoped (no production caller needs a live-edit recompile
  mid-frame yet — registration happens at scene-build time, not as a per-frame edit path).
  Gate (M6 Task 14): same recipe rendered baked vs virtual matches on geometry (silhouette IoU
  at stated resolution) on a scene that never baked (bake-call counter proof) — **real-GPU
  result: 2 of 3 corpus recipes pass (plain sphere IoU=0.84, sphere+box SmoothUnion CSG
  IoU=0.87); the third (Twist-modified sphere, exercising a position-stack opcode with no
  occupancy grid) fails with virtualHits=0 — a real, reproducible, non-flaky failure isolated
  to "any recipe using a position-stack push/RestorePos pair" (Twist and MirrorX both
  reproduce it identically) but not root-caused past that; see test_baked_vs_virtual_parity.cpp's
  file-header KNOWN ISSUE note for the full investigation trail and next-step pointers.** This
  is the user's named most-impactful axis; the core zero-bake claim is proven, the
  domain-modifier corner is open.
- **Inc1b — resolvability-gated recipe evaluation. PLANNED, not started** (§4.1a; plan doc
  [[Lazy-Procedural-Delta-Baseline-Inc1b-Plan-2026-07]]). Compute-reduction axis, orthogonal to
  Inc2 (no ordering dependency): prunes recipe-tree detail evaluation in the already-shipped
  virtual/GPU-direct render path once a sample's footprint is coarser than a subtree's own feature
  scale. Named "Inc1b" (not "Inc2") specifically to avoid colliding with this section's existing
  Inc2 number — it follows on Inc1's shipped uber-shader/bounds-metadata work, not the producer.
- **Inc2 — the CPU region producer.** Placement-agnostic contract; region-scoped direct-to-pool
  production (topology + bricks + mips + apron) on worker threads; keyed residency requests;
  paged-pool v1 (reserved capacity, insert without Rematerialize). Requires the §8.1
  conservative-evaluation decision. Gates: a region materializes on approach with coarse/mip
  fallback covering the latency; the two-adjacent-regions seam test; measured production
  latency/budget.
- **Inc3 — deltas.** Recipe-delta path through the editor (program patch; param edits free
  post-`ReadParam`, structural edits async-recompile; kills per-toggle Rematerialize);
  materialized-delta store (VDC chunk, topology-self-sufficient + producer-versioned) +
  producer-side application + dirty-path mip refresh; serializable ActionStack / KI-016. Gates:
  edit → only affected regions re-produce; delta'd-neighbor seam test; save/load replays deltas
  over a fresh procedural base — including a **cross-build replay** case.
- **Inc4 — eviction + budgets + scale.** Page reclaim, admission control, virtual↔materialized
  policy (incl. transition quality); Tiered-ESVO convergence (virtual TierRef children). Decision
  point: re-open the GPU request-buffer/LRU question with evidence (trigger 2 has fired).
- **v2+ — GPU producer** (compute-fill via kernel codegen), **compaction** (materialized→recipe
  fitting), content-addressed dedup (per the Sparse-Mip direction's clone-aware section).

Ordering rationale: Inc0/Inc1 are independently shippable and de-risk the two mechanisms everything
else composes (mip ladder in production, instruction-direct eval) before any format surgery (paged
pool) starts. Inc2 before Inc3 because materialized deltas need the producer to apply onto;
recipe-deltas could land earlier if the editor needs them sooner.

## 7. Coordination & sequencing constraints

- **Tiered-ESVO Inc2 is merged** (`2d67840e`, 2026-07-10 — M1-M5, codex M3 line included as an
  ancestor). No merge-order gate remains; this program's shader work now means *extending the
  merged* `TierRef`/leaf-hit code (which owns the last spare `ChildDescriptor` sentinel, `farBit`)
  with the virtual state rather than inventing a second sentinel. Residual hygiene item: the codex
  worktree (`/mnt/c/tmp/vixen-codex-tiered-esvo-inc2-m3-resume-20260709`) still holds *uncommitted*
  M3-alternative artifacts (offscreen render test + CPU-mirror crossing parity test) — reconcile or
  discard against merged main before this program's Inc1 touches the same shader.
- **Undertow**: no dependency for v1 (authored deltas are VIXEN-side); the address vocabulary and
  the delta-record shape should be shared with the integration map before Inc3 freezes formats.
- **Prior-art debt**: Aokana (open-world voxel streaming; flagged CRITICAL/urgent-read in the vault,
  never ingested) should be read before Inc2 freezes the addressing/page-table scheme.

## 8. Open decisions

1. **Conservative recipe evaluation** (gates §4.1's coarse structure and §4.2's topology-on-demand):
   (a) interval/Lipschitz-bound VM over the ~87-opcode catalogue — per-opcode interval rules incl.
   position-stack modifiers (Twist/Bend/Repeat*) and discontinuous math (Frac/Step/Sign/Pow) — a
   substantial new engine component; (b) dense fine-grid evaluation per region (exact, parallel,
   forfeits the few-evals economy); (c) non-conservative occupancy + explicit miss policy. V1 may
   ship (b) for producer topology while (a) matures for bounds/step-relaxation derivation.
2. **Recipe parameterization / `ReadParam`.** Recipes are closed constant programs today
   (`paramMask==0` enforced; P4 `ReadParam` deferred). Pulled forward by this program: (i)
   instructions-first at scale wants (recipe, params/seed) instances; (ii) param-only recipe-deltas
   avoid the structural-edit pipeline recompile (§4.1). What is the param-block ABI?
3. **GPU recipe evaluation shape.** Working assumption: GLSL field-function emitter + uber-shader
   regeneration + async runtime recompile (§4.1). Must measure: recompile latency (glslang +
   pipeline creation on Mesa-Dozen/WSL2 and lavapipe), shader size/register pressure as the merged
   recipe count grows, and the crossover where per-recipe pipelines + multi-dispatch composition
   would win despite the restructure.
4. **Multi-channel recipe outputs.** Color/roughness are hard-coded position functions inside the
   bake (`SdfBake.h:154-168`), not recipe outputs — and the virtual path shades instance tint, so
   baked-vs-virtual can only be geometry-compared until this lands (§6 Inc1 gate). Fold into the
   kernel-codegen channel direction, before or during Inc2?
5. **Region key concretely** — `TierAddress` hop-chain vs (octree id, Morton brick key) for the
   v1 single-body degenerate case, and the dense `brickGridLookup` table's fate at sparse scale.
6. **`ISVOStreamingManager`** — revive as the producer/eviction contract or delete as dead prior
   art; its shape matches but it has never had an implementation.
7. **Shell-cache interaction** — the surface-shell ESVO cache derives from the full pool at
   Compile; under partial materialization it must derive per-region or be subsumed by the mip
   ladder. Owner increment TBD (likely Inc2).
8. **Instance-count scaling** — the per-pixel all-instances loop and the 3×64=192-instance shader
   cap: if regions become instance-like entries, what replaces the linear loop (BVH over
   instances / tier-structured culling)? Sharpened by §4.1: the virtual path currently lacks even
   the bestT early-reject.
9. **Virtual↔materialized transition quality** — resolution-matched materialization, fade/dither,
   or accept the pop; policy owner is Inc4 but the gate methodology (geometry tolerance) starts
   at Inc1. Related but distinct: §4.1a's resolvability-gated recipe evaluation (reduces the
   frequency/urgency of promotion in the first place, by making distant virtual content cheap).
10. **Resolvability-gated recipe evaluation feature-scale metadata** (§4.1a) — per-node/per-subtree
    feature-scale bounds are not derived by anything today; open whether they're authored (like v1
    bounds/relaxation), derived from the same interval VM as Open Decision #1(a) if that's built,
    or a coarser per-recipe-family cutoff. **Scoped 2026-07-12**: see
    [[Lazy-Procedural-Delta-Baseline-Inc1b-Plan-2026-07]] M3 (closed-form per-opcode where
    derivable, dense-eval-derived-margin fallback otherwise, deny-by-default elsewhere) — plan
    written, not yet implemented.

## 9. Rejected alternatives

- **Eager bake with bigger budgets / more caching** — rejected: the cost is structural
  (whole-domain evaluation + whole-tree rebuild + monolithic upload), not a constant factor;
  planetary/tiered scale is unreachable by construction.
- **Per-brick paging only, no instruction-direct path** (classic GigaVoxels without procedural
  producers) — rejected as the *baseline*: it still requires materializing everything once to page
  it, keeps voxel payload as the CPU↔GPU currency, and forfeits the user-named win (traffic =
  instructions + deltas only). It survives as one layer of the unified model (§2.1).
- **Byte-diff deltas against regenerated base** — rejected: base is per-build deterministic only;
  cross-toolchain/device bit-exactness is not promised and pinning it (fixed-point field eval,
  no-libm) is a much deeper cut than absolute-override deltas (which must still be
  topology-self-sufficient, §4.4).
- **In-shader bytecode interpreter as the v1 GPU path** — not selected (see §8.3): no interpreter
  exists, divergence/register-pressure risks, while codegen is parity-tested; revisit only if
  uber-shader regeneration measurably fails (recompile latency or shader-size ceiling).
- **Per-recipe standalone pipelines for v1** — rejected: cannot participate in the single-dispatch
  per-pixel nearest-hit instance loop without a multi-dispatch depth-composition restructure;
  revisit at §8.3's crossover.
- **HistoPyramid request compaction, CPU-mirrored LRU clones, CUDA-DP-style scheduling, disk
  out-of-core tier, mean-filtered SDF mips** — all previously rejected in the vault
  (Sparse-Mip Inc1 plan prior-art + Inc2 M4); this program inherits those rejections.

## 10. Consequences for existing docs

- [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] — unchanged; this program is the "bedrock can be
  PROCEDURAL (recipe + edit overlay)" line made baseline, and finally feeds its mip machinery on
  default production paths (Inc0).
- [[Tiered-ESVO-Observer-Addressing-Design-2026-07]] §6 — its "populated on demand by evaluating
  (TierAddress, seed) → base geometry" expectation gets its concrete producer here; its hard scope
  line (no delta format in that doc) is honored — the delta format lives in THIS doc's §4.4, which
  *extends* the VIXEN side of the boundary for authored deltas while leaving sim-delta ownership
  with undertow. The shipped code's always-valid-`configs[]`-slot assumption is superseded by the
  virtual state (§2.1) once this program lands.
- [[Runtime-Kernel-Pipeline-Direction-2026-06]] — Inc1 here is a concrete consumer of its
  runtime-compile capability; that direction doc stops being FUTURE-only.
- [[Voxel-Content-Format-Contract-Design-2026-06]] — provider kinds gain the virtual/materialized
  distinction as an orthogonal residency axis, not a new provider kind.

## Related

- [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] — mip ladder, residency, clone-aware dedup direction
- [[Tiered-ESVO-Observer-Addressing-Design-2026-07]] + [[Tiered-ESVO-Inc2-Plan-2026-07]] — address
  vocabulary, cross-tree references, the merged traversal work this program extends
- [[Runtime-Kernel-Pipeline-Direction-2026-06]] — runtime recipe→kernel compilation (Inc1 enabler)
- [[GigaVoxels-Streaming]] — production/request/LRU prior art (thesis-depth notes)
- undertow `2026-07-05-reification-design.md`, `2026-06-13-deltas-only-geometry-save-design.md` —
  sim-side base+delta doctrine and shipped precedent
- [[kernel-codegen-framework-direction]] — single-source schema discipline for the delta chunk +
  recipe kernels
