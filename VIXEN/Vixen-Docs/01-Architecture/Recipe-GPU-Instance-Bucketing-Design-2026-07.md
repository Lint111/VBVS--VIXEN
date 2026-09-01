---
title: GPU Recipe-Instance Bucketing — Design
status: IMPLEMENTED in follow-on increments (Inc2–Inc4, 2026-07-16/17; merges `7a27357f`,
`71c6eeca`, `915fc5f6`); this document remains the design record for the shipped mechanism.
created: 2026-07-15
---

# GPU Recipe-Instance Bucketing — Design

> **This is the design record, not the implementation plan.** Its follow-on milestone-mapped
> plans delivered Inc2–Inc4; the async/background-compile half remains outside those increments.

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

### 3.1 Bucketing pass — granularity RESOLVED (2026-07-15)

A new compute pre-pass, run once per frame before the main shading dispatch, that reads
`bodyInstances[]` (already bound, binding 10) and produces, per bucket, a compacted list of member
instance indices (an atomic-counter-driven append, mirroring the `atomicAdd` precedent) plus a
per-bucket screen-space coverage estimate (see §3.2).

**Decision: bucket by exact `recipeId`, not by content-hash family, for this design.** The epic
doc (§7) already frames family-normalization as a SEPARATE, later increment (Increment 4:
"shape/literal normalization → parameterized family pipelines," explicitly depending on
[[Recipe-Parameterization-Plan-2026-07]] §5, shipped) — coupling this design's bucketing
granularity to the family concept now would prematurely merge two increments' scope and make this
design depend on Increment 4's normalization logic (which doesn't exist yet) before it's even
built. Bucketing by exact `recipeId` is: (a) simpler — `BodyInstanceGpu.recipeId` is already a
direct per-instance field, no extra indirection through
[[Recipe-Pipeline-Cache-Inc1-Plan-2026-07]]'s `RecipeContentCacher` lookup needed in the hot
bucketing-pass loop; (b) strictly correct today — every `recipeId` already gets its own emitted
`sdfRecipe_<id>` GLSL function (`UberShaderSplice.h`), so per-recipeId bucketing naturally lines up
with what a specialized pipeline would compile; (c) NOT a dead end — Increment 4's family
normalization is a natural, additive upgrade on top of working recipeId-level bucketing later
(swap the bucketing key from `recipeId` to a family hash once Increment 1's `RecipeContentCacher`
data is threaded into the bucketing pass, without needing to touch the dispatch/compositing
machinery this design builds). Building family-aware bucketing FIRST, before Increment 4's own
normalization design exists, would be scope creep into a not-yet-designed increment.

### 3.2 Screen-space coverage per bucket
For each bucket, need a conservative screen-space region (tile range, bounding rect, or similar) so
the eventual per-bucket dispatch can be shaped/sized to just that region instead of the whole
framebuffer. Requires projecting each member instance's `getRecipeBoundSphere` world-space sphere
to screen space (needs a view-proj matrix — resolve the §2 gap first) and taking the union across a
bucket's members. **Open question**: per-instance projection unioned per-bucket (more accurate,
more per-thread work) vs. some coarser per-bucket approximation (e.g. union of member bound spheres
computed once, cheaper but coarser)? Needs real design, not a guess.

### 3.3 Indirect dispatch sizing — RESOLVED (2026-07-15)

Once a bucket's screen-space coverage is known, write a `VkDispatchIndirectCommand`-shaped entry
into an indirect buffer (the plumbing for this — `IndirectBuffer` usage + `IndirectRead` barrier —
already exists, see §2) sized to that bucket's coverage, then `vkCmdDispatchIndirect` a specialized
per-recipe pipeline against just that region.

**Cross-bucket overlap resolution — the single most important design question, now resolved via a
codebase survey (2026-07-15):**

**Decision: sequential per-bucket dispatches with a plain non-atomic read-compare-conditionally-
overwrite against the shared `HitRecord` SSBO. No new atomic pattern needed.** This works because
of an existing, already-correct property of the RenderGraph barrier system, confirmed by direct
inspection:

- `FrameSyncScheduler::NeedsSync` (`FrameSyncScheduler.cpp:12-17`) treats **write-after-write as a
  hazard**, not just the usual RAW/WAR — `AccessWrites(prev) || AccessWrites(cur)` triggers a
  `SyncEdge`/`GroupBarrier` ordered by the nodes' graph-declared execution order
  (`FrameSyncScheduler.cpp:26-49`). So if bucket A's dispatch and bucket B's dispatch are both
  RenderGraph nodes declaring a write access to the `HitRecord` resource, the graph AUTOMATICALLY
  serializes them with a real memory-visibility barrier (`PassRecorder.cpp:24-48`, a global
  `VkMemoryBarrier2` — not region-scoped, which is exactly what's needed since it doesn't assume
  disjoint writes, it correctly flushes/serializes ALL shader-storage writes before the next
  dispatch reads/writes).
- `MultiDispatchNode` already does this as its DEFAULT behavior: with `autoBarriers_` on (the
  default), it inserts a compute→compute `VkMemoryBarrier2` (`srcAccess=SHADER_WRITE`,
  `dstAccess=SHADER_READ|SHADER_WRITE`) between every dispatch pass in a group
  (`MultiDispatchNode.cpp:462-468,480-484,653-668`) — the exact UAV-hazard serialization this
  design needs, already built and already the default.
- **Given that serialization, a plain (non-atomic) `if (myHitT < hitRecords[idx].hitT) hitRecords[idx] = myRecord;`
  in each bucket's shader is CORRECT** — no two threads within one dispatch write the same pixel
  (one thread per covered pixel), and the barrier guarantees bucket A's writes are complete and
  visible before bucket B's dispatch begins reading/writing. Atomics (a float-bitcast
  `atomicMin`-on-`hitT`-as-bits trick, proven viable via the existing `atomicMax`-on-`floatBitsToUint`
  precedent at `SpatialReuseShade.comp:503-515`, though only for a single scalar reduction, not a
  full-struct winner-write) would ONLY become necessary if a future revision wants buckets to run
  CONCURRENTLY (no barrier between them) for performance — an explicit, deliberate future
  optimization to consider only if sequential bucket dispatch proves too slow, not a day-one
  requirement.

**Consequence for §4's "prototype before full design" recommendation**: the prototype should use
`MultiDispatchNode`'s existing per-pass-barrier default (do NOT disable `autoBarriers_`) and a
plain read-compare-write shader — this is now a low-risk, well-precedented mechanism, not an
open research question. The only remaining uncertainty is PERFORMANCE (how much serialization
cost N sequential bucket dispatches add vs. the tier-0 switch, and vs. Option A's rejected
per-recipe-full-screen-redispatch cost) — that's an empirical question for the prototype/spike,
not a correctness question.

### 3.4 Fallback path
Per the epic's own tier-0/tier-1 model: NOT every recipe should get bucketed-dispatch treatment —
only genuinely hot ones (Increment 2's original "hot-mark" concept, still needed). Cold/rare
recipes should stay on the existing tier-0 switch path (correctly, since a bucket+specialized-
pipeline+indirect-dispatch machine has real fixed overhead per bucket that isn't worth paying for a
recipe rendering 1 instance).

**Recommendation (2026-07-15, judgment call — revisit once the spike has real numbers): hotness
gates WHICH buckets get their own specialized pipeline, not whether bucketing happens at all.**
Bucket every recipeId uniformly in the pre-pass (cheap — it's one atomic-append per instance, no
per-bucket cost yet), then partition buckets AFTER bucketing into "hot" (gets a specialized
pipeline + its own indirect dispatch) vs. "cold" (its member instances fall back to the existing
tier-0 switch path, handled inline exactly as today — no new machinery needed for cold recipes at
all). This is cleaner than trying to skip bucketing cold recipes up front, because: (a) hotness is
inherently a THIS-FRAME-VS-HISTORY concept (a recipe is hot because it was rendered enough times
recently) that the bucketing pass itself has no way to know without extra state — simpler to let
bucketing be uniform and stateless, and apply the hotness gate as a separate, simpler decision
using data the hot-mark tracking (still TODO, §5) already maintains; (b) it keeps the bucketing
pass's job single-purpose (partition by recipeId) rather than also encoding a hotness policy
inside it, easier to test and reason about independently. **Caveat**: this recommendation is not
empirically validated — if the spike (§4 item 4) finds bucketing-everything's fixed cost is
non-trivial even for cold/rare recipes, revisit toward skip-cold-recipes-up-front instead.

## 4. Immediate next steps (before a plan doc)

1. **View-proj-matrix gap — RESOLVED (2026-07-15), low-risk.** Confirmed `CameraNode` already
   computes `projection * view` EVERY frame (`CameraNode.cpp:193,377`) but only exposes it as the
   deliberately-lagged `PREV_VIEW_PROJ` graph output (`CameraNodeConfig.h:74`, `OUTPUT_SLOT` macro
   pattern, `OUTPUTS=2`). **Decision: add a sibling `CURRENT_VIEW_PROJ` output** using the exact
   same `OUTPUT_SLOT`/`INIT_OUTPUT_DESC` pattern already established for `PREV_VIEW_PROJ`
   (`CameraNodeConfig.h:74,141,153,161` are the 4 touch points to mirror) — no new math (the
   multiply already happens), no new CPU computation, just exposing a value already computed under
   a new slot + wiring it into a new binding for the bucketing pre-pass. This is now a small,
   well-understood, low-risk task, not an open design question.
2. **§3.3's cross-bucket overlap/depth-resolution question — RESOLVED (2026-07-15).** See §3.3 —
   sequential `MultiDispatchNode`-style dispatches (its existing `autoBarriers_` default already
   provides correct write-after-write serialization) + a plain non-atomic read-compare-write is
   correct. No atomics, no new synchronization primitive needed. The only open item left here is
   empirical (performance), not design (correctness) — see item 4.
3. **Bucketing granularity (§3.1) — RESOLVED (2026-07-15).** Bucket by exact `recipeId`, not
   content-hash family — avoids prematurely coupling this design to Increment 4's not-yet-designed
   family-normalization work; recipeId-level bucketing upgrades cleanly to family-level later.
4. **Hotness-gating shape (§3.4) — RESOLVED as a recommendation (2026-07-15), not empirically
   validated.** Bucket uniformly, gate specialized-pipeline assignment by hotness after bucketing.
   Revisit if the spike below shows this is wrong.
5. **All four design questions above are now resolved or have a stated recommendation** — no
   remaining open CORRECTNESS questions block a plan doc. What remains is empirical validation
   (below) and the async/hot-mark tracking mechanism itself (§5, out of this design's scope,
   layers on top).
6. **Prototype/measure before committing to full design.** With items 1-4 resolved and no longer
   open research questions, the remaining uncertainty is PERFORMANCE: does sequential bucketed
   dispatch (N buckets × serialized barriers) actually beat the tier-0 switch at realistic hot-
   recipe counts, and by how much? A small measurement spike (hand-rolled bucketing + indirect
   dispatch for a SYNTHETIC 2-3-recipe scene, no hotness/async yet, using `MultiDispatchNode`'s
   existing default barrier behavior) would give a real number before investing in the full
   milestone-mapped implementation plan.
7. **Once the item-6 spike confirms the mechanism is worth building**, write the actual
   milestone-mapped implementation plan doc (mirroring
   [[Recipe-Pipeline-Cache-Inc1-Plan-2026-07]]'s structure: Milestone Map, per-task detail,
   live-run gates given this is fundamentally GPU/render work throughout — unlike Increment 1,
   NOTHING in this design is pure-CPU infra, every milestone here needs a real Windows-native GPU
   live-run gate).

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
