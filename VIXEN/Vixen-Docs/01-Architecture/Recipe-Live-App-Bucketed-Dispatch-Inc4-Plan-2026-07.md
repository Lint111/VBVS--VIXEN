# Recipe Live-App Bucketed-Dispatch Integration — Increment 4 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: use the post-brainstorm-context-manager pipeline to
> implement this plan milestone-by-milestone (fresh implementer + Opus validator per milestone,
> worktree-isolated, progress persisted in this doc; pre-bless the in-tree destructive/git tier at
> setup). **Live-run gates are authoritative for EVERY milestone** — every milestone here touches the
> REAL production shader (`BodyInstanceRayMarch.comp`) or the REAL render graph
> (`BuildRenderGraph.cpp`), not a standalone test-harness stand-in like Increments 2/3 used. Real
> discrete GPU, validation layers on, throughout. Never overlap two builds of one target. Watch long
> builds with a foreground polling loop, not a blind wait.

**Goal:** Give the specialized-pipeline-per-recipe mechanism Increments 2/3 built and proved correct
(in standalone GTest harnesses only) an actual, live consumer in `VixenApp`'s real render graph. This
is a **prerequisite-building increment, not a "ship the win" increment** — Increment 3's own honest
conclusion is that bucketed dispatch is still measurably slower than the tier-0 switch at every tested
N (0.30-0.33x/0.22-0.24x/0.04-0.05x, see [[Recipe-Bucketed-Dispatch-Overhead-Inc3-Plan-2026-07]]), so
this increment does NOT make the new path the unconditional default. It builds the plumbing needed to
even ATTEMPT live integration, gated behind an opt-in flag, so the mechanism can eventually be measured
and iterated on inside a real running app rather than only inside isolated test harnesses forever.

**Why this is bigger than "wire it up"** (grounding research, 2026-07-16/17): a safe, gated,
coexisting-with-tier-0 live integration is NOT a lightweight wrapper around Increments 2/3's existing
work. Three substantial gaps exist, confirmed by direct code inspection, none solvable cheaply:

1. **No per-instance skip mechanism exists anywhere in the shader chain.** `TraceWorld.glsl`'s
   instance loop (called from `BodyInstanceRayMarch.comp`) marches every index `0..pc.instanceCount`
   unconditionally, every frame (`TraceWorld.glsl:76-77`, duplicated in `TraceWorldShadow` at `:412`).
   `BodyInstance`/`BodyInstanceGpu` has no flag/bitmask/skip field. `pc.instanceCount` is a single
   scalar upper bound, not a filterable index list — there is no "exclude these specific indices,
   march the rest" primitive today.
2. **The real tier-0 shader writes `HitRecord` as a plain, unconditional overwrite**
   (`BodyInstanceRayMarch.comp:257`: `hitRecords[hitRecordIdx] = rec;`), NOT the conditional
   nearest-hit-wins compositing Increment 2 M3 already built and proved
   (`test_recipe_multi_bucket_compositing.cpp` Task 8) — but that proof was against a **hand-rolled
   GPU stand-in that reimplements `TraceWorld.glsl`'s algorithm**, never against the actual production
   shader. Making tier-0 and bucketed dispatch coexist correctly on any screen-space overlap requires
   retrofitting the SAME compositing scheme onto the real shader for the first time.
3. **Indirect dispatch has never touched production code.** `ComputeDispatchNode` — the only dispatch
   node actually wired into `BuildRenderGraph.cpp` — has zero `vkCmdDispatchIndirect` capability
   (confirmed: zero occurrences of "indirect" in that node/header). `MultiDispatchNode`'s indirect
   support (Increment 2 M2's Task 4) exists ONLY inside the standalone GTest harnesses, which
   hand-construct their own `VkInstance`/`VkDevice` outside `VixenApp` entirely. Wiring bucketed
   dispatch live means introducing indirect dispatch into the production render graph for the first
   time, not reusing something already proven there.

**What "done" looks like for this increment:** the specialized-pipeline mechanism runs inside a REAL
`VixenApp` instance, gated behind an opt-in `VIXEN_*` env-var flag (mirroring the existing
`VIXEN_PROCEDURAL_UBER_DEMO`-style convention, confirmed reusable — `BuildRenderGraph.cpp` has 10+
precedents for this exact gating pattern), producing IDENTICAL visual output to tier-0-only rendering
for the same scene (the correctness bar, proven via real screen-capture comparison, not a synthetic
harness), with a final honest measurement of live-app FPS/frame-time (bucketed-gated-on vs. gated-off)
recorded in [[Perf-Ledger]]. **This increment does NOT**: make the new path the default (tier-0 stays
default, per Inc3's own finding that bucketed dispatch isn't yet a win); implement GPU-LRU eviction
(the ORIGINAL Increment 4 scope, now pushed — eviction needs a live population to evict from, which
this increment creates the possibility of but does not itself accumulate over app-lifetime usage
patterns; that's a follow-on once this increment's promotion-on-first-use logic exists and has run
long enough in a real session to accumulate cold pipelines worth evicting); solve the
single-dispatch-no-switch architecture ([[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]]
remains a separate, larger, not-yet-started direction).

**Depends on (shipped):** [[Recipe-GPU-Instance-Bucketing-Inc2-Plan-2026-07]] (M1-M4, the specialized-
pipeline/bucketing mechanism itself, proven correct in isolation) and
[[Recipe-Bucketed-Dispatch-Overhead-Inc3-Plan-2026-07]] (M0-M3, the per-bucket-overhead investigation
and its honest "still not a win" conclusion) — this increment does not redesign either, it gives their
already-proven mechanism a real place to run.

**Tech Stack:** C++23, GLSL compute, Vulkan 1.3 (`vkCmdDispatchIndirect` in PRODUCTION for the first
time, `VkMemoryBarrier2`), GoogleTest + real `VixenApp` integration tests, CMake ninja/wsl presets +
Windows `.bat` builds, real discrete GPU (Windows-native) for every milestone.

---

## §0. Scope

**In scope:**
- A new per-instance exclusion mechanism sufficient to let tier-0's march skip a caller-specified set
  of instance indices (the ones a given frame promoted to bucketed dispatch), without breaking
  existing tier-0-only rendering (every existing test/scene with the new mechanism unused/empty must
  render byte-identically to today).
- Retrofitting the conditional nearest-hit-wins `HitRecord` compositing scheme (proven in Inc2 M3's
  test harness) onto the REAL `BodyInstanceRayMarch.comp`/`TraceWorldShadow` write paths — applied
  UNCONDITIONALLY (this is a correctness fix regardless of whether bucketed dispatch is active this
  frame — an unconditional overwrite is only safe today because nothing else writes `HitRecord` in the
  same pass; once a second writer can exist, the guard must always be present, gated flag or not).
- Introducing `MultiDispatchNode`/indirect-dispatch support into `BuildRenderGraph.cpp`'s real graph
  construction, gated behind a new opt-in env var (name TBD by the M3 implementer, follow the
  `VIXEN_*` convention).
- A live, real-`VixenApp` correctness gate: same scene, same camera, bucketed-dispatch-gate ON vs. OFF,
  screen-capture or `HitRecord`-buffer comparison proving IDENTICAL results.
- A final live-app performance measurement (gate ON vs. OFF), honestly recorded regardless of outcome
  (this increment is not expected to show bucketed dispatch winning — Inc3 already found it doesn't at
  the dispatch-mechanism level in isolation; a live measurement might reveal something different due to
  real-scene mixed hot/cold recipe populations, or might simply confirm the isolated finding — report
  whichever is true).

**Out of scope (explicitly, do not let scope creep here):**
- GPU-LRU eviction itself (the original Increment 4 scope) — this increment creates the PRECONDITION
  (a live population of specialized pipelines that can go cold) but does not implement eviction policy.
  A future increment (4b, or renumber again once this lands) owns actually wiring `TypedCacher::Erase`
  (confirmed dead code today, zero callers anywhere) into an age/LRU policy.
- Making bucketed dispatch the default path — stays opt-in-flagged given Inc3's own finding.
- Any change to the fundamental bucketing/compositing ALGORITHM Inc2/Inc3 already proved — this
  increment is about GIVING that proven mechanism a real home, not re-deriving it.
- The single-dispatch-no-switch architecture — a separate, larger, unscoped direction.
- Fixing KI-037 (`ProjectToPixel`'s near-plane coverage-rect bug) — unrelated to this increment's own
  scope, remains open for whoever eventually revisits barrier-coalescing or spatial-culling correctness.

---

## Milestone Map

- **M1 — Per-instance skip mechanism** (Task 1) · **live-run gate** · a new mechanism (exact shape —
  a skip-bitmask SSBO, an excluded-index-range convention, or similar — decided by the implementer,
  documented and justified) lets tier-0's march exclude a caller-specified instance subset, with zero
  behavior change when unused.
  - [x] DONE 2026-07-17. Shape chosen: (a) skip-bitmask SSBO (binding 35,
    `InstanceSkipMaskBuffer`/`isInstanceSkipped()` in `shaders/SceneBindings.glsl`), read once per
    instance-loop iteration in both `TraceWorld` and `TraceWorldShadow` (`shaders/TraceWorld.glsl`).
    Chose (a) over (b) (CPU-side instance-array re-partitioning) because (a) needs no new ordering
    dependency between the bucketing pre-pass and tier-0's dispatch sizing (the plan doc's own
    concern with (b)), is a pure additive SSBO binding matching the exact "1-byte/256-byte
    placeholder when the owning feature is inactive" convention already proven safe by this same
    shader's bindings 13/15/16 (MipPoolBuffer/TierRefTableBuffer/OccupancyGridBuffer), and needs zero
    CPU-side re-ordering logic to be a no-op — the CPU side still only needs to know WHICH indices to
    mark, which M3's bucketing pre-pass output will supply later.
- **M2 — Real cross-pass HitRecord compositing on the production shader** (Task 2) · **live-run gate**
  · retrofit the proven nearest-hit-wins conditional write onto `BodyInstanceRayMarch.comp`/
  `TraceWorldShadow`'s actual write paths, proven correct via the SAME rigor Inc2 M3 used (real
  overlap scene, both dispatch orderings, byte-identical `HitRecord` comparison) but against the REAL
  shader this time, not a stand-in.
  - [ ] Not started.
- **M3 — Production indirect-dispatch wiring + env-var gate** (Task 3) · **live-run gate** · introduce
  `MultiDispatchNode`/indirect dispatch into `BuildRenderGraph.cpp`'s real graph construction for the
  first time, gated behind a new opt-in env var; specialized pipelines compile and dispatch inside a
  REAL running `VixenApp`.
  - [ ] Not started.
- **M4 — Live correctness + performance measurement** (Task 4) · **live-run gate, discrete GPU
  mandatory** · gate-ON vs. gate-OFF correctness proof (identical rendering) + honest FPS/frame-time
  measurement in the real app, recorded in [[Perf-Ledger]] and this plan's doc closure.
  - [ ] Not started.

### Progress Log

(populated as milestones complete — one entry per milestone: commit hash, gate evidence, Opus
validator verdict.)

- **Grounding research (2026-07-16/17, pre-M1):** confirmed via direct code inspection that (a) no
  production code path creates specialized per-recipe `VkPipeline` objects today — the mechanism
  exists only in 3 standalone GTest harnesses (`test_recipe_bucketed_indirect_dispatch.cpp`,
  `test_recipe_bucketing_perf.cpp`, `test_recipe_multi_bucket_compositing.cpp`) that hand-construct
  their own `VkInstance`/`VkDevice`, confirmed via zero matches for `SpecializedRecipeShaderGlsl.h` or
  `RecipeInstanceBucketing.comp` in any `application/` or non-test `libraries/` source; (b)
  `TypedCacher`/`CacherBase`'s `Erase()` has zero callers anywhere in the codebase, `Clear()` only
  fires at full device teardown — the underlying cache infrastructure is purely additive today, no
  eviction of any kind exists; (c) confirmed the Sparse-Mip ESVO LOD epic's own GPU-LRU deferral had 3
  explicit flip-triggers (hundreds of resident trees in a real scene; the tiered-ESVO epic reaching an
  implementation-plan status; a measured CPU-cost regression), none of which are met today — the JIT
  epic's own Increment 4 (GPU-LRU) borrows a DIFFERENT justification (the switch-scaling knee) rather
  than satisfying those triggers, a pre-existing tension in the docs, not new. **This confirmed GPU-LRU
  eviction (the original Increment 4 scope) has no live target to operate on — re-scoped 2026-07-17 to
  this live-app-wiring increment instead, per explicit user decision, with eviction itself deferred to
  a future increment once a live population exists.**
- **Feasibility research (2026-07-17, pre-M1):** confirmed a SAFE, GATED, coexisting-with-tier-0 live
  integration requires the SAME substantial new plumbing as an unconditional-default integration would
  — the env-var gating mechanism itself is trivial and precedented (10+ existing `VIXEN_*` examples in
  `BuildRenderGraph.cpp`), but does not reduce the cost of the 3 prerequisites this plan's M1-M3 target
  (per-instance skip mechanism, real cross-pass compositing on the ACTUAL tier-0 shader, first-ever
  production indirect-dispatch wiring). Confirmed `pc.instanceCount` is a single scalar bound over one
  contiguous `bodyInstances[]` array — NOT a filterable index list — so "just exclude bucketed
  instances from the count" is not architecturally available; the real shape needed is either a new
  per-instance exclusion primitive in the shader, or CPU-side re-partitioning of the instance array
  every frame reacting to which recipes were promoted (itself new, fragile plumbing). M1 owns deciding
  between these approaches.
- **M1 DONE (2026-07-17).** Commit (this worktree, `feat/recipe-live-app-bucketing-inc4`):
  `shaders/SceneBindings.glsl` (+binding 35 `InstanceSkipMaskBuffer`/`isInstanceSkipped()`),
  `shaders/TraceWorld.glsl` (early-continue in both `TraceWorld`'s and `TraceWorldShadow`'s instance
  loops), plus 11 hand-rolled GTest Vulkan harnesses updated to declare the new binding in their own
  descriptor-set-layout arrays (`test_body_instance_raymarch_render.cpp` and 10 siblings under
  `libraries/RenderGraph/tests/Nodes/`) — these harnesses build their own `VkDescriptorSetLayoutBinding`
  arrays rather than going through the production reflection/gatherer path, so each needed its own
  256-byte placeholder + layout/write entry for the new binding, or validation correctly rejected the
  now-larger reflected descriptor set.
  - **Shape chosen:** (a) skip-bitmask SSBO, not (b) CPU-side re-partitioning — see the Milestone Map
    entry above for the full justification.
  - **Empty-skip-set no-op proof:** ran the full existing render/SVO/mirror suite with the mechanism
    wired but every scene's skip mask left at its zeroed/placeholder default.
    `test_rendergraph_criticalnodes_gpurender1` (8 tests, incl. the new positive-case test below): ALL
    PASS. `test_recipe_pool_render` (1/1), `test_mip_fallback_render` (4/4): ALL PASS.
    `test_rendergraph_shadermirrors` (25/25, pure-CPU `TraceWorldShadow` mirror, untouched baseline):
    ALL PASS. `test_gpu_parity` (7/7): ALL PASS. Three pre-existing failures were found
    (`test_rendergraph_criticalnodes_gpurender2`'s `TierCrossingLodResidencyTest.
    NonResidentChildNeverCrossesResidentChildDoes`, `test_rendergraph_criticalnodes_gpurender2b`'s
    `ShadowCorrectnessTest.OccludedPixelMatchesCpuReferenceShadowRay`, and
    `test_baked_vs_virtual_parity`'s `VirtualRendersGeometricallyEquivalentToBaked`) — each was
    isolated by temporarily short-circuiting `isInstanceSkipped()` to always-false (`if (false &&
    isInstanceSkipped(...))`) and re-running: all three reproduced byte-identically (same magenta=0/0,
    same luma=0 values, same IoU=0.6062630480167015/bakedHits=5808/virtualHits=9580) with the mechanism
    fully neutralized, proving they predate and are independent of this change — not investigated
    further as fixing them is out of this milestone's scope. `test_stored_sdf_march_mirror` is a
    pure-CPU mirror with zero reference to `SceneBindings.glsl`/`TraceWorld.glsl` (grep-confirmed) and
    was skipped for runtime reasons (dense marching, multi-minute regardless of this change), not a
    gate concern.
  - **Positive-case exclusion proof:** new test
    `BodyInstanceRayMarchRenderTest.SkipMaskExcludesOnlyTargetedInstance`
    (`test_body_instance_raymarch_render.cpp`) — 3 side-by-side body instances (red/green/gray
    material kinds at instance indices 0/1/2, reusing the sibling
    `RenderMultiKindBodiesProvesStrideFix` scene). Baseline (no skip mask): red=21743, green=17804,
    gray=21743 px. With instance index 1 (green) excluded via `skipMask={0x2u}` (bit 1 set): red=21743
    (byte-identical to baseline), green=0 (fully excluded), gray=21743 (byte-identical to baseline) —
    decisive proof the mechanism excludes exactly the targeted instance and leaves all others
    completely unaffected.
  - **Deviation from prompt:** none of substance. The prompt's plan-doc sync step (worktree → main
    checkout copy) is handled by the controller per the prompt's own instruction (implementer does not
    commit in the main checkout).

---

## Tasks

### M1 — Per-instance skip mechanism

**Task 1.** Design and implement a mechanism letting `TraceWorld.glsl`'s instance loop (called from
both `BodyInstanceRayMarch.comp` and its `TraceWorldShadow` sibling) skip a caller-specified subset of
instance indices, while marching all others normally. Two candidate shapes to evaluate (pick one,
document why):
- **(a) A skip-bitmask SSBO**: one bit per instance index, read once per instance at loop-iteration
  start, cheap early-continue if set. Simple, GPU-resident, doesn't require CPU-side array
  re-partitioning — the CPU side just needs to know WHICH indices to mark, which the bucketing
  pre-pass's output already identifies (per-recipe compacted instance-index lists).
- **(b) CPU-side instance-array re-partitioning**: reorder `bodyInstances[]` every frame so bucketed
  instances are a contiguous, excludable prefix/suffix, adjust `pc.instanceCount` down accordingly.
  Avoids a new GPU-side read but requires new CPU-side logic reacting to per-frame bucketing/hotness
  results BEFORE the tier-0 dispatch is sized — a real ordering dependency between the bucketing
  pre-pass and tier-0's own dispatch that doesn't exist today (currently independent, parallelizable
  passes).

**Gate**: EVERY existing test/scene with the new mechanism's skip-set empty (the default,
zero-instances-excluded case) renders BYTE-IDENTICALLY to pre-M1 code — this is the single most
important correctness bar for this milestone, since it's a change to the shared, universally-used
tier-0 shader. Run the full existing render-test suite, confirm zero behavior change when the feature
is unused, before even testing the exclusion mechanism's positive case.

### M2 — Real cross-pass HitRecord compositing

**Task 2.** Change `BodyInstanceRayMarch.comp`'s unconditional `hitRecords[hitRecordIdx] = rec;`
(and `TraceWorldShadow`'s equivalent write) to the conditional nearest-hit-wins scheme Inc2 M3 already
proved correct in its standalone harness: `if (myHitT < hitRecords[idx].hitT || <virgin-flag
condition>) hitRecords[idx] = myRecord;`. This must be applied UNCONDITIONALLY — not just when the
bucketed-dispatch gate is on — since once a SECOND writer to `HitRecord` can exist in some frames
(gated on), an unconditional-overwrite tier-0 write is unsafe in ANY frame where ordering isn't
guaranteed; the conditional write costs one extra read+compare and is always correct, gated on or off.

**Gate**: reproduce Inc2 M3's exact proof (real overlap scene, both dispatch orderings, byte-identical
`HitRecord` comparison) but THIS TIME against the actual production `BodyInstanceRayMarch.comp`, with
M1's skip mechanism actively excluding some instances and M3's future bucketed dispatch (stubbed or
real, implementer's choice for sequencing) writing those same pixels. Confirm gate-OFF (no exclusions,
tier-0 handles everything) still renders byte-identically to pre-M2 code — the conditional write must
be a no-op behavior change when there's genuinely only one writer.

### M3 — Production indirect-dispatch wiring + env-var gate

**Task 3.** Introduce `MultiDispatchNode` (or equivalent indirect-dispatch-capable node) into
`BuildRenderGraph.cpp`'s real graph construction, following the established `VIXEN_*` opt-in-flag
convention (10+ precedents: `VIXEN_PROCEDURAL_UBER_DEMO`, `VIXEN_UI_DEMO`, etc. — `std::getenv`-gated
alternate graph branches). When the new flag is unset, the graph builds EXACTLY as today (tier-0 only,
M1's skip-set always empty, M2's conditional write present but behaviorally a no-op with one writer).
When set, wire: the bucketing pre-pass (`RecipeInstanceBucketing.comp`, already shipped), specialized
pipeline compilation for promoted recipes (`SpecializedRecipeShaderGlsl.h`/`ComputePipelineCacher`,
already shipped), indirect dispatch of those pipelines via the new production node, M1's skip mechanism
excluding those same instances from tier-0's pass, and M2's compositing making the two passes' writes
safe regardless of screen-space overlap.

**Gate**: a real `VixenApp` instance builds and runs with the flag SET, without crashing, without
validation-layer errors, for a scene containing both hot (promoted) and cold recipes.

### M4 — Live correctness + performance measurement

**Task 4.** With the flag OFF vs. ON, render the SAME scene/camera and prove IDENTICAL output (screen
capture diff or `HitRecord` buffer comparison, whichever is more practical given the real app's actual
readback capabilities — investigate what's available, don't assume Inc2/3's test-harness readback
conventions transfer directly to the live app). Then measure honest FPS/frame-time for both
configurations at a few realistic hot/cold recipe population mixes. **Record the result honestly** —
Inc3 already found the isolated dispatch mechanism doesn't beat tier-0; a live measurement might match
that, might differ due to real-scene effects (e.g. real scenes' actual instance/recipe distribution
differing from the synthetic 100%-hot-promotion scenes Inc2/3 tested), or might reveal something else
entirely. Whatever the result, report it plainly in [[Perf-Ledger]] and this plan's Progress Log, and
update [[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] §7 with the increment's actual outcome.

---

## Risks / decision points

- **M1's correctness bar is the highest-stakes item in this whole increment.** `TraceWorld.glsl` is
  shared, universally-exercised code — a subtle regression here would affect every scene, not just the
  bucketed-dispatch-gated ones. Treat any change here with the scrutiny Inc2 M2's bound-sphere bug and
  Inc2 M3's compositing proof both required, not a lighter touch.
- **M2's "always apply the conditional write, gated or not" requirement is deliberate, not
  incidental.** Do not gate the compositing-scheme change itself behind the env var — only the SECOND
  writer (bucketed dispatch) should be gated; the conditional-write safety net should be unconditional,
  so a future increment enabling the gate doesn't ALSO need to remember to flip a compositing switch.
- **This increment may not close Inc2/Inc3's perf gap, and that's fine.** Its value is giving the
  proven mechanism a live home to be measured and iterated on in — not itself being the increment that
  makes bucketed dispatch win. Do not force a positive framing on M4's measurement if the honest result
  is "still doesn't help," consistent with this whole epic's established honesty discipline.
- **GPU-LRU eviction remains explicitly deferred**, now for a documented reason (no live population
  exists yet to evict from) rather than the epic's earlier, weaker "premature" framing. Once this
  increment ships and runs in real sessions long enough to accumulate cold specialized pipelines, THAT
  is the point to scope eviction — not before.
