---
title: GPU Recipe-Instance Bucketing — Design
status: design (not yet plan-ready)
created: 2026-07-15
---

# GPU Recipe-Instance Bucketing — Design

> **This is a design doc, not an implementation plan.** It exists to work out the architecture
> before committing to milestones. Do not start implementation from this doc alone — it needs a
> review pass and a follow-on milestone-mapped plan doc (mirroring
> [[Recipe-Pipeline-Cache-Inc1-Plan-2026-07]]'s structure) before any code is written.

## 1. Why this exists — Increment 2 turned out to need real architecture, not a quick step

[[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] §7 sketches "Increment 2: async tier-1
promotion on usage — hot-mark → background emit+compile → swap when ready → universal fallback."
Scoping that increment (2026-07-15) surfaced a hard architectural blocker the direction doc didn't
anticipate: **today's renderer is ONE full-screen compute dispatch where every pixel-thread marches
every instance and selects each one's recipe via an in-shader `switch(recipeId)`**
(`ComputeDispatchNode.cpp:478` dispatches `(width+7)/8 × (height+7)/8`;
`shaders/TraceWorld.glsl`'s instance loop calls `evalRecipeField`'s generated switch,
`libraries/SVO/include/Recipe/UberShaderSplice.h`). "Give recipe N its own specialized pipeline"
has NO existing per-draw-call or per-dispatch seam to hook into — recipe selection happens per
pixel-thread, deep inside the one shared dispatch, not at a level a second pipeline could
intercept.

**Two decompositions were considered and rejected/accepted:**
- **Option A — per-recipe full-screen re-dispatch + cross-pass compositing.** For each hot recipe,
  run a SEPARATE full-screen dispatch using that recipe's specialized (switch-free) shader, then
  composite nearest-hit against the existing `HitRecord` SSBO (`shaders/HitRecord.glsl`, already a
  per-pixel `hitT` G-buffer record every pass could read-compare-conditionally-overwrite — the one
  reusable piece of infra this option has). **Rejected** (user, 2026-07-15): at the actual target
  scale (1000+ live recipes), this is N full-framebuffer dispatches per frame for N hot recipes —
  it does not merely fail to help at scale, it actively inverts the tier-1 promotion's entire
  purpose (making hot recipes cheaper), and there is zero existing tile/indirect-dispatch culling
  to mitigate the cost (confirmed: no `vkCmdDispatchIndirect` call anywhere in the codebase, no
  screen-tile structure of any kind).
- **Option B — GPU instance bucketing.** Partition instances by recipe/pipeline BEFORE the
  per-pixel shading dispatch, so each recipe's dispatch only ever touches the screen region its
  actual instances could cover, via GPU-computed indirect dispatch sizing. **Selected** (user,
  2026-07-15) as the only approach that scales toward 1000+ live recipes — this doc designs it.

## 2. What this doc must resolve before a plan doc can exist

Per the scoping research (2026-07-15), the raw building blocks are a mix of "already there" and
"genuinely missing":

**Already there (reuse, don't rebuild):**
- **Indirect-dispatch buffer/barrier plumbing exists**, just unused. `ResourceUsage::IndirectBuffer`
  already maps to `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT` (`Resource.cpp:166-167`);
  `AccessKind::IndirectRead` already emits the correct `VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT`/
  `VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT` barrier (`BarrierTypes.h:36,78-79,120`). Swapping
  `ComputeDispatchNode`/`MultiDispatchNode`'s `vkCmdDispatch` call for `vkCmdDispatchIndirect` is a
  **local per-call-site change** (add an `optional<VkBuffer> indirectBuffer` to `DispatchPass`), not
  a deep node-architecture rewrite — the barrier/resource layer already models this correctly.
- **A dispatch-by-pipeline primitive already exists and is dormant**: `MultiDispatchNode`/
  `DispatchPass` (`libraries/RenderGraph/src/Nodes/MultiDispatchNode.cpp`,
  `include/Data/DispatchPass.h`) binds a distinct `VkPipeline` per queued pass, with an unused
  `optional<uint32_t> groupId` field clearly intended for exactly this kind of partitioned
  dispatch — never wired to anything live.
- **Compute-shader atomics are proven on this engine's target hardware** (`shaders/
  ShaderCounters.glsl`'s `atomicAdd` usage) — a bucketing pre-pass's atomic append-to-bucket
  pattern has a working precedent to build from.
- **Recipe identity is already first-class per-instance data**: `BodyInstanceGpu.recipeId`
  (binding 10) is already there; a bucketing pre-pass reads data that already exists, no new
  per-instance field needed for the recipeId axis itself.
- **Instance SSBO capacity is not a blocker.** `BodyOctreeSceneNode`'s ring buffer is fully
  grow-only (`EnsureRingAllocated`, `.cpp:752-782`) — 1000+ instances (64 KB/ring-slot) is trivial
  for the existing allocation design; no capacity ceiling needs raising first.
- **A real world-space bound sphere per recipe already exists**: `getRecipeBoundSphere(recipeId,
  center, radius, relaxation)` (generated switch, `UberShaderSplice.h:105-116`) — geometrically
  sufficient input to project to a screen-space region, IF projected (see gap below). Note it's
  deliberately conservative/loose (`SdfRecipes.glsl:113,122`), not tight — screen-space coverage
  estimates from it will over-cover, not under-cover, which is the safe direction to be wrong in.

**Genuinely missing — this design must invent these, nothing to mirror:**
- **No view-projection matrix reaches any shader in the trace path.** The trace push-constant block
  (`SceneBindings.glsl:212-232`) carries only camera basis vectors (`cameraPos/Dir/Up/Right/fov/
  aspect`) — rays are reconstructed from these, there's no `mat4 viewProj`. The one existing GPU
  matrix, `prevViewProj` (`CameraNode.cpp:193,377`), is scoped to temporal reprojection and is not
  bound into `TraceWorld.glsl`/`SceneBindings.glsl` at all. **A bucketing pre-pass needs either a
  real view-proj matrix newly plumbed into a binding, or an on-GPU projection derived from the
  existing basis vectors** — resolve which in this doc (see §4).
- **No tile/clustered/screen-space-region structure of ANY kind exists** — not for lighting, not
  for shadows, not experimentally. This is the single biggest genuine gap: bucketing by recipe
  alone ("these 40 instances are recipe 6") doesn't answer "which pixels does bucket 6 need to
  shade" without SOME screen-space coverage structure to bound the dispatch.
- **No screen-space bounding-rect/AABB projection code exists anywhere** — the existing
  screen-space LOD cutoff (`TraceWorld.glsl:132-145`) is a scalar cone-footprint distance test, not
  a 2D projection; it answers "is this surface small enough to stop marching," not "what pixel
  range does this sphere cover."
- **No GPU-side instance-bucketing/partitioning pass exists** — the only existing "sort by X" is
  `SortInstancesFrontToBack` (`InstanceSort.h:26`), CPU-side, single-threaded, distance-keyed, not
  recipe-keyed, and doesn't partition (just reorders one array).

## 3. Shape of the design (to be worked out, not yet decided)

This section is the actual design work — sketched here as the open questions a review pass needs
to close, not as decided architecture. Do not treat anything below as final.

### 3.1 Bucketing pass
A new compute pre-pass, run once per frame before the main shading dispatch, that reads
`bodyInstances[]` (already bound, binding 10) and produces, per distinct `recipeId` (or per
recipe-content-hash "family" from [[Recipe-Pipeline-Cache-Inc1-Plan-2026-07]] — **open question**:
bucket by exact `recipeId` or by content-hash family, which would let structurally-identical
recipes under different `recipeId`s share a bucket/pipeline, closer to the epic's Increment 4
"family" goal? Deciding this now vs. deferring to Increment 4 needs a call), a compacted list of
member instance indices (an atomic-counter-driven append, mirroring the `atomicAdd` precedent) plus
a per-bucket screen-space coverage estimate (see §3.2).

### 3.2 Screen-space coverage per bucket
For each bucket, need a conservative screen-space region (tile range, bounding rect, or similar) so
the eventual per-bucket dispatch can be shaped/sized to just that region instead of the whole
framebuffer. Requires projecting each member instance's `getRecipeBoundSphere` world-space sphere
to screen space (needs a view-proj matrix — resolve the §2 gap first) and taking the union across a
bucket's members. **Open question**: per-instance projection unioned per-bucket (more accurate,
more per-thread work) vs. some coarser per-bucket approximation (e.g. union of member bound spheres
computed once, cheaper but coarser)? Needs real design, not a guess.

### 3.3 Indirect dispatch sizing
Once a bucket's screen-space coverage is known, write a `VkDispatchIndirectCommand`-shaped entry
into an indirect buffer (the plumbing for this — `IndirectBuffer` usage + `IndirectRead` barrier —
already exists per §2) sized to that bucket's coverage, then `vkCmdDispatchIndirect` a specialized
per-recipe pipeline against just that region. **Open question**: how does a workgroup that only
covers "recipe 6's screen region" know it should ONLY shade recipe-6 instances and not accidentally
re-shade instances another bucket already owns, given screen regions from different buckets will
overlap in practice (two different recipes' bound spheres can easily overlap on screen)? This needs
either (a) each bucket's pass writing into the shared `HitRecord` SSBO with a nearest-hit
compare-and-conditionally-overwrite (mirroring Option A's one salvageable idea, but now bounded to
real coverage instead of the whole screen, so the cost concern from Option A doesn't apply at the
same magnitude — still needs careful ordering/synchronization design), or (b) some other resolution
scheme. **This is probably the single most important remaining design question** — get the
cross-bucket depth/hit resolution right or this whole design produces wrong pixels under overlap.

### 3.4 Fallback path
Per the epic's own tier-0/tier-1 model: NOT every recipe should get bucketed-dispatch treatment —
only genuinely hot ones (Increment 2's original "hot-mark" concept, still needed). Cold/rare
recipes should stay on the existing tier-0 switch path (correctly, since a bucket+specialized-
pipeline+indirect-dispatch machine has real fixed overhead per bucket that isn't worth paying for a
recipe rendering 1 instance). **Open question**: does the bucketing pass itself need to know
hotness (skip bucketing cold recipes, leave them for the existing switch-based instance loop to
handle inline as today), or does bucketing run for everything and hotness only gates WHICH buckets
get their own specialized pipeline vs. falling back to a "residual tier-0 bucket"? The latter is
probably cleaner (uniform bucketing, heterogeneous pipeline assignment) but needs to be decided.

## 4. Immediate next steps (before a plan doc)

1. **Resolve the view-proj-matrix gap (§2).** Decide: plumb a real `mat4 viewProj` into a new
   binding (simplest, matches how `prevViewProj` already exists elsewhere for a different purpose),
   or derive an on-GPU projection from the existing basis vectors (avoids a new binding, more GPU
   math per bucketing thread — probably not worth it given a matrix binding is cheap and simple).
   Recommend: new binding, mirror `prevViewProj`'s existing CPU-side computation
   (`CameraNode.cpp:193,377`) for the CURRENT frame instead of previous.
2. **Resolve §3.3's cross-bucket overlap/depth-resolution question** — this is the load-bearing
   correctness question the whole design hinges on. Consider prototyping the `HitRecord`
   compare-and-conditionally-overwrite scheme in isolation (e.g. two overlapping buckets, confirm
   nearest-hit resolves correctly) before committing to the full design.
3. **Decide the recipeId-vs-content-hash-family bucketing granularity (§3.1)** relative to
   [[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] §7 Increment 4's own family-normalization
   goal — avoid building bucketing twice (once naive by-recipeId, once later reworked to
   by-family) if the family concept can be adopted now cheaply.
4. **Prototype/measure before committing to full design.** Given the real complexity surfaced
   here, consider a small measurement spike (e.g. hand-rolled bucketing + indirect dispatch for a
   SYNTHETIC 2-3-recipe scene, no hotness/async yet) to validate the core mechanism (bucket →
   indirect dispatch → correct compositing) works and is actually cheaper than the tier-0 switch at
   realistic instance counts, before investing in the full milestone-mapped implementation plan.
5. **Once §4.1-4.4 are resolved**, write the actual milestone-mapped implementation plan doc
   (mirroring [[Recipe-Pipeline-Cache-Inc1-Plan-2026-07]]'s structure: Milestone Map, per-task
   detail, live-run gates given this is fundamentally GPU/render work throughout — unlike
   Increment 1, NOTHING in this design is pure-CPU infra, every milestone here needs a real
   Windows-native GPU live-run gate).

## 5. Scope notes

This design does NOT yet cover: the async/background-compile half of the original Increment 2
sketch (hot-mark → background emit+compile → swap) — that layers on TOP of working bucketed
dispatch, not before it, since there's no point async-compiling a specialized pipeline if nothing
can route instances to it yet. GPU-LRU eviction (Increment 3) and full family normalization
(Increment 4) are also out of scope here, per the epic's own sequencing — this doc is scoped
strictly to "can instances be bucketed and dispatched per-recipe correctly and affordably," the
prerequisite question everything else in the epic depends on.

See [[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] (the epic this de-risks),
[[Recipe-Pipeline-Cache-Inc1-Plan-2026-07]] (Increment 1, shipped, the content-hash cache this
design's §3.1 open question references), [[Recipe-Parameterization-Plan-2026-07]] (the keystone
both increments build on).
