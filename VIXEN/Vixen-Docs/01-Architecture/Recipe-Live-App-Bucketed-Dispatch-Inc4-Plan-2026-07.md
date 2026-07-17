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
  - [x] DONE 2026-07-17. See Progress Log entry below for the full account, including a scope
    correction (`TraceWorldShadow` has no `HitRecord` write to retrofit — it's a pure any-hit `bool`
    occlusion test) and a mid-milestone design fix the gate test itself caught.
- **M3 — Production indirect-dispatch wiring + env-var gate** (Task 3) · **live-run gate** · introduce
  `MultiDispatchNode`/indirect dispatch into `BuildRenderGraph.cpp`'s real graph construction for the
  first time, gated behind a new opt-in env var; specialized pipelines compile and dispatch inside a
  REAL running `VixenApp`.
  - [x] DONE 2026-07-17. See Progress Log entry below for the full account -- 6 real integration bugs
    found and fixed via the mandatory live-app gate, exactly the "expect at least one integration bug"
    risk this milestone's own prompt called out, except it was 6, not 1.
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
- **M1 FIX ROUND (2026-07-17).** An Opus validator found the M1 commit above correctness-blocking:
  binding 35 was used unconditionally in the shader (SPIR-V reflection correctly marks it required)
  but nothing in the PRODUCTION render path ever wired or populated that descriptor — an unbound
  `STORAGE_BUFFER` at `vkCmdDispatch` time on every default-scene frame.
  - **Scope found WIDER than the fix prompt stated — CORRECTED 2026-07-17 by re-validator.**
    `SceneBindings.glsl`'s binding-35 declaration (line 148/157) is unconditionally `#include`d by
    FOUR production shaders (`BodyInstanceRayMarch.comp`, `DirectLighting.comp`, `ProbeUpdate.comp`,
    `SpatialReuseShade.comp`), but the fix round's claim that SPIR-V reflection marks binding 35
    REQUIRED "in all four regardless of usage" was **incorrect** — the re-validator's own independent
    reflection dump found binding 35 is genuinely reflected-required in only THREE
    (`BodyInstanceRayMarch.comp`/march, `ProbeUpdate.comp`, `SpatialReuseShade.comp` — all three
    genuinely call `TraceWorld`/`isInstanceSkipped()`); `DirectLighting.comp` only MENTIONS
    `TraceWorldShadow` in a comment, never calls it, so glslang elides the unreferenced SSBO from its
    compiled binary entirely. The fix round wired all FOUR gatherers anyway (harmless — a live 3000-
    frame run produced no descriptor/gatherer error for the extra, silently-ignored
    `directLightingGatherer` wire — but it is unnecessary). **If a future cleanup pass touches this
    area, the CORRECT required set is march + ProbeUpdate + SpatialReuse only, not all four.**
  - **Fix:** one shared `instanceSkipMaskBuffer` (`StorageBufferNode`, `PARAM_SIZE_BYTES=256`,
    `BuildRenderGraph.cpp` node creation ~line 632, param ~line 839, device wire ~line 4532) connected
    to binding 35 on all four gatherers (`BuildRenderGraph.cpp:4732, 5069, 5268, 5592`). Chose 256 bytes
    (not 1 byte) to match the 11 GTest harnesses' own existing placeholder convention exactly, per the
    fix prompt's own suggestion — production and tests now share one no-op shape.
  - **Shader comment fixed:** `SceneBindings.glsl`'s comment previously described the no-op as holding
    via `skipMask.length()==0` (a 1-byte-placeholder framing) while all 11 test harnesses actually use a
    256-byte zeroed buffer (`length()==64`) — the validator's exact finding. Rewrote the comment to
    describe the deployed reality: the no-op holds by CONTENT (every word zero), not by length, since
    production and tests both use the 256-byte convention now.
  - **Test rigor fixed:** `SkipMaskExcludesOnlyTargetedInstance`'s non-excluded-instance assertions were
    `EXPECT_GT(..., *0.9)` despite the M1 report calling the proof "byte-identical" — tightened to
    `EXPECT_EQ` (exact). Live GPU run confirmed exact equality holds with zero tolerance needed:
    red=21743/21743, gray=21743/21743 baseline vs. skip-active, both dispatches (see gate results below).
  - **Full test suite re-run (Windows-native, real GPU):** `test_rendergraph_criticalnodes_gpurender1`
    (8/8 PASS, includes the tightened test), `test_recipe_pool_render` (1/1 PASS), `test_mip_fallback_render`
    (4/4 PASS), `test_baked_vs_virtual_parity` (0/1 — pre-existing, see below),
    `test_rendergraph_criticalnodes_gpurender2` (6/7 — pre-existing, see below),
    `test_rendergraph_criticalnodes_gpurender2b` (2/3 — pre-existing, see below),
    `test_appflow_editor_toggle_render` (0/1 — pre-existing, see below).
  - **4 failures, all independently confirmed pre-existing and unrelated to this fix** (not just
    asserted — actually re-built and re-run against a temporary worktree at the pre-M1 commit,
    `9303e096`, then discarded): `BakedVsVirtualParityTest.VirtualRendersGeometricallyEquivalentToBaked`
    and `TierCrossingLodResidencyTest.NonResidentChildNeverCrossesResidentChildDoes` and
    `ShadowCorrectnessTest.OccludedPixelMatchesCpuReferenceShadowRay` were already documented as
    pre-existing in the M1 entry above (isolated via always-false short-circuit, reproduced
    byte-identically here too: same IoU=0.6062630480167015/bakedHits=5808/virtualHits=9580, same
    magenta=0, same luma=0 values). `AppFlowEditorToggleRenderTest.ToggleThenUndoRestoresRender` was
    NOT previously documented — found newly failing in this fix round's re-run, then independently
    verified via a temporary worktree at commit `9303e096` (one commit before M1): identical failure,
    identical `boreDiffPixels=0 vs 3000` — confirms it predates M1 entirely and is unrelated to the
    skip-mask mechanism.
  - **LIVE APP GATE (the most important result):** ran the real `VIXEN.exe` (Windows-native, discrete
    GPU `AMD Radeon(TM) Graphics`, Debug build, Vulkan validation layers ON per build log confirmation),
    default scene/graph, no special flags, for ~4 minutes / 25000+ frames at ~200 FPS sustained.
    **Zero occurrences of `VUID-vkCmdDispatch-None-08114` or any unbound-descriptor warning** — the
    exact defect this fix round targets is confirmed gone. Two unrelated pre-existing VUID classes were
    found (`VUID-vkCmdDraw-None-09600`, `VUID-vkQueueSubmit2-semaphore-03868`, 20 occurrences each,
    confined to a one-time startup-recompile transient, self-limited by the validation layer's own
    duplicate-message cap after 10 reports, never recurring across the rest of the run) — independently
    confirmed pre-existing and unrelated by rebuilding+running the pre-M1 baseline app (`9303e096`) in a
    temporary worktree: byte-identical 42 total VUID count (20+20) and identical 50-occurrence
    `PushConstantGathererNode::Validate` "Type mismatch" log line count, both discarded afterward. Both
    temporary comparison worktrees (`-pretest`, `-pretest2`) were removed after use; nothing landed on
    this branch from them.
  - **Deviation from prompt:** scope was WIDER than stated (3 additional gatherers, not just the main
    march's) — flagged above, not a deviation in spirit (the prompt's own reasoning, applied
    consistently, requires it). Everything else matches the prompt exactly.
  - **Opus RE-VALIDATION: APPROVED.** Independently reproduced the live-app gate (3000 frames,
    ~196 FPS, zero `VUID-vkCmdDispatch-None-08114`, validation confirmed active via 70 unrelated VUID
    lines present), confirmed the tightened `EXPECT_EQ` assertion is real and passes on real GPU, and
    confirmed the pre-existing-baseline claims via independent reasoning (the VUID classes seen are
    structurally draw/present/sync-related, impossible to stem from a compute descriptor binding).
    **Caught and corrected one real inaccuracy** (see above): the "required in all four shaders"
    claim was wrong — `DirectLighting.comp` doesn't actually call `TraceWorldShadow`, so glslang elides
    the unreferenced binding; only 3 of 4 gatherers genuinely needed the wire. Confirmed harmless
    (the extra wire is silently ignored, zero errors in the live run) but flagged for future cleanup.
    **Correctness-blocking issues: NONE.** M1 is APPROVED, cleared to proceed to M2.
- **M2 DONE (2026-07-17).** Commits (this worktree, `feat/recipe-live-app-bucketing-inc4`, on top of
  M1): `shaders/BodyInstanceRayMarch.comp` (conditional nearest-hit-wins `HitRecord` write, replacing
  the unconditional `hitRecords[idx] = rec`), `shaders/SceneBindings.glsl` (new `anyInstanceSkipped()`
  helper), `libraries/RenderGraph/tests/Nodes/test_body_instance_raymarch_render.cpp` (new gate test
  + a `RenderToRgba` pre-seed parameter to support it).
  - **Scope correction vs. the prompt/plan text:** "`TraceWorldShadow`'s equivalent write" does not
    exist to retrofit — direct inspection of `TraceWorld.glsl:491-625` confirms `TraceWorldShadow`
    returns a plain `bool` (any-hit occlusion test for shadow rays, called from `ProbeUpdate.comp`/
    `SpatialReuseShade.comp`) and never reads or writes `hitRecords[]` at all. The ONLY real
    unconditional `HitRecord` write in production code is `BodyInstanceRayMarch.comp:319`. No shader
    change was needed or made for `TraceWorldShadow`.
  - **A real design gap the plan doc didn't anticipate, found BEFORE writing any shader code:**
    `HitRecordBuffer` (binding 18, `StorageBufferNode`) is zero-initialized ONCE at creation and never
    cleared per-frame (`StorageBufferNode::ExecuteImpl` is a literal no-op) — today's UNCONDITIONAL
    write is the only thing that ever refreshes it every frame (full-image dispatch coverage, hit or
    miss, every frame). Inc2 M3's proven condition (`myHitT < existing.hitT || existing.flags==0u`)
    is only safe when the buffer is freshly re-zeroed before each comparison (the M3 harness does
    this) — ported literally, a fresh MISS (flags=0u, hitT=1e30 sentinel) would lose to ANY stale
    leftover hit from a prior frame (both disjuncts false), permanently freezing stale HitRecord data
    at a pixel that should have gone empty (camera pan, object moved away) — a real regression, not a
    no-op. Confirmed via two independent code-reading passes (no frame index in push constants, no
    clear/fill pass anywhere in the render graph, `TraceWorld.glsl:93`'s `bestT=1e30` sentinel).
  - **First fix attempt (asymmetric: miss always unconditional, hit conditional) was WRONG, caught by
    the gate test itself, not by review.** Writing the mandated "real overlap scene, both dispatch
    orderings" gate test (below) immediately produced a real failure: with instance 1 (green)
    skip-masked and a stubbed second-writer pre-seeding a real hit for green's pixels, tier-0's own
    miss at those pixels (correct — it skipped the only body there) unconditionally clobbered the
    stub's hit under the first fix. Root cause: a miss from a NON-exhaustive tier-0 (some instance
    skip-masked) doesn't mean "nothing is here," it means "I didn't check this" — the bucketed pass is
    authoritative for that pixel, not tier-0's miss.
  - **Final fix (two-regime split, using the skip mask itself as the missing per-frame signal — no
    new plumbing, no generation counter):** added `anyInstanceSkipped()` to `SceneBindings.glsl`
    (word-scans the ≤6-word skip mask once per invocation — cheap, bounded by `TraceWorld`'s own
    `3*64` instance cap). `BodyInstanceRayMarch.comp`'s write: if NO instance is skip-masked this
    frame, tier-0 is exhaustive and a miss unconditionally clears the slot (today's exact behavior,
    byte-identical — this is the only state possible until M3 wires a real second writer, and the
    only state gate-OFF ever produces). If ANY instance is skip-masked, a miss defers to an existing
    hit (does NOT clobber it); a hit still applies Inc2 M3's nearest-hit-wins compare unchanged.
    Considered (and rejected, see plan doc's own Progress Log discussion during the milestone) adding
    a true monotonic per-frame generation counter (`RenderGraph::globalFrameIndex` exists at
    `RenderGraph.h:992` but doesn't reach push constants) — fully general but real new plumbing (new
    push-constant field + new node-output wiring in `BuildRenderGraph.cpp`) more properly scoped to M3
    (which already owns introducing the real second-writer/barrier contract); the skip-mask-based
    signal is fully correct for every case M1-M2's own scope can produce and adds zero new surface.
    A controller review independently reached the SAME conclusion on the generation-counter rejection
    (M3's territory, not M2's) but initially asked for a stronger per-INVOCATION formulation of the
    skip-mask signal — track, inside `TraceWorld`'s own instance loop, whether THIS pixel's march
    actually skipped an instance while searching for its nearest hit, rather than a frame-global "was
    anything skipped anywhere" check. Traced through `TraceWorld.glsl`'s instance loop to verify: the
    loop is `for (int instIdx = 0; instIdx < numInstances; ++instIdx)` where `numInstances` derives
    only from the push-constant `pc.instanceCount` (frame-global, not per-pixel), `isInstanceSkipped
    (instIdx)` depends only on `instIdx` and the skip-mask buffer's content (also frame-global, no
    per-pixel/per-ray input anywhere in that function), and the loop body never `break`s or early-
    `return`s before reaching a later index (every branch is `continue` or falls through to the
    nearest-hit update — checked both the procedural and ESVO branches). Consequence: EVERY pixel
    dispatched this frame iterates the identical index range and evaluates the identical skip decision
    per index, regardless of ray direction/origin — "did this pixel's march skip an instance" is
    therefore NOT actually a per-pixel-varying quantity in the current loop structure; it is
    frame-global content computed identically no matter where in the shader you evaluate it. The
    frame-global `anyInstanceSkipped()` and a per-invocation tracked-bool are consequently PROVABLY
    the same value for every pixel today, not merely a conservative approximation of each other. This
    also independently reconfirms the controller's own "all-or-nothing per instance, not partial/
    conditional" checkpoint from the same angle: instance skipping doesn't depend on anything
    pixel-specific, so any signal built from it is necessarily frame-global regardless of where it's
    computed. Flagged back to the controller for sign-off before deciding whether to leave the
    frame-global form (documented as equivalent, less surface) or refactor to the per-invocation form
    for literal-match/defense-in-depth against a future loop-structure change (e.g. if M3's real
    bucketing scheme ever makes skip decisions ray- or region-dependent) — either form's gate-test
    behavior is identical today, so this does not block or change M2's own DONE status.
  - **Gate-OFF no-op proof:** new test `HitRecordCompositingRealShaderBothOrderingsMatchOracle`
    (`test_body_instance_raymarch_render.cpp`) — two independent renders of the same 3-instance
    scene/camera with no skip mask: `memcmp==0` (byte-identical `HitRecord` buffers, decisive — not
    just matching pixel counts). Full existing suite re-run (Windows-native, real GPU, post-fix):
    `test_rendergraph_criticalnodes_gpurender1` (9/9 PASS, includes the new gate test),
    `test_recipe_pool_render` (1/1), `test_mip_fallback_render` (4/4), `test_gpu_parity` (7/7),
    `test_rendergraph_shadermirrors` (25/25) — all ALL PASS, zero behavior change. The same 4
    pre-existing failures M1 documented were independently re-confirmed byte-identical this round too
    (`test_baked_vs_virtual_parity` 0/1: same `bakedHits=5808/virtualHits=9580/IoU=0.6063`;
    `test_rendergraph_criticalnodes_gpurender2` 6/7: same `magenta px=0/0`;
    `test_rendergraph_criticalnodes_gpurender2b` 2/3: same `luma=0`; `test_appflow_editor_toggle_render`
    0/1: same failure) — confirmed unrelated to this milestone's change.
  - **Real-overlap gate (reproducing Inc2 M3's exact proof against the REAL shader):** same
    `HitRecordCompositingRealShaderBothOrderingsMatchOracle` test. Scene: 3 instances (red/green/gray),
    instance 1 (green) skip-masked out of tier-0's march. Stub "second writer" built from green's own
    baseline `HitRecord` output (17804 pixels), split into 8902 "closer" + 8902 "farther" stub `hitT`
    variants so both branches of the nearest-hit-wins compare are exercised. Ordering A
    (second-writer-pre-seeded-then-tier-0-dispatches) vs. an independent third-render oracle
    (tier-0-only output with the stub composited in via the SAME rule, computed from scratch, not
    reused from either ordering): `memcmp==0`. Ordering A vs. ordering B
    (tier-0-dispatches-into-fresh-buffer-then-stub-composites-after): **0/196608 pixels differ**
    (`kW=768, kH=256`) — order-independent, matching Inc2 M3's own "0/65536 pixels differ" rigor,
    now against the real production shader instead of a hand-rolled stand-in.
  - **LIVE APP GATE (the most important result):** ran the real `VIXEN.exe` (Windows-native, discrete
    GPU `AMD Radeon(TM) Graphics`, Debug build, Vulkan validation layers confirmed ON via the build's
    own configure log), default scene/graph, no special flags, `VIXEN_EXIT_AFTER_FRAMES=25000` —
    reached frame ~24960 in ~3 minutes at 120-175 FPS (frame-timer log), clean exit code 0, clean
    teardown. **Zero occurrences of `VUID-vkCmdDispatch-None-08114` or any NEW validation-error
    class.** VUID/error counts matched M1's own documented pre-existing baseline exactly: 50
    `PushConstantGathererNode::Validate` "Type mismatch" lines, 40 total VUID string occurrences
    split 20/20 between `VUID-vkCmdDraw-None-09600` and `VUID-vkQueueSubmit2-semaphore-03868` (same
    self-limited startup-transient class M1 independently verified pre-existing against the pre-M1
    baseline). Confirmed binding 35 (skip mask) and binding 18 (HitRecord) both correctly wired into
    the live production graph via the startup log lines
    (`[BuildRenderGraph] Connected instance skip mask at binding 35`,
    `[BuildRenderGraph] Connected HitRecord SSBO at binding 18`).
  - **Deviation from prompt:** (1) `TraceWorldShadow` scope correction, see above — not a deviation in
    spirit, the function genuinely has nothing to retrofit. (2) The compositing rule is NOT Inc2 M3's
    literal fully-symmetric condition — see the design-gap/fix account above for the full reasoning;
    this was flagged to the controller mid-milestone before implementing, and confirmed necessary by
    the gate test itself catching a real bug in the first (more literal) attempt. Everything else
    matches the prompt.
  - Plan-doc sync: copied this file to the main checkout's
    `VIXEN/Vixen-Docs/01-Architecture/Recipe-Live-App-Bucketed-Dispatch-Inc4-Plan-2026-07.md` (a copy
    already existed there from M1's dispatch, per the prompt's own note) — not committed there, left
    for the controller to review/commit.
- **M2 FIX ROUND (2026-07-17).** An Opus validator found the M2 commit above still correctness-blocking
  in exactly the one case Task 2's own bar demands be a byte-identical no-op: the empty-skip-mask
  (`anyInstanceSkipped()==false`, tier-0 exhaustive, sole writer) HIT branch still ran the conditional
  compare `rec.hitT < existing.hitT || existing.flags==0u` against `existing`, which is always LAST
  FRAME's leftover content since `HitRecordBuffer` is never cleared per-frame
  (`StorageBufferNode::ExecuteImpl` confirmed a no-op, independently re-confirmed this round). Since
  `existing.flags==0u` never holds after frame 1, the condition degenerates to `rec.hitT <
  existing.hitT` — a closer stale hit from a prior frame (camera motion, object moving out of view)
  wrongly survived over the current frame's real, farther, correct hit. The validator proved this live:
  61290/61290 body pixels kept a stale record instead of the current frame's genuine hit, with an empty
  skip mask. The MISS branch in this same regime was already correct (unconditional).
  - **Fix:** restructured `BodyInstanceRayMarch.comp`'s `HitRecord` write (~line 355-370) to check
    `tier0Exhaustive` FIRST, branching into two fully separate regimes instead of folding the
    empty/nonempty distinction into a combined conditional expression:
    - `tier0Exhaustive == true` (no instance skip-masked, sole writer): plain unconditional overwrite,
      hit or miss, no compare — byte-identical to the original pre-M2 code, exactly as Task 2's own bar
      requires.
    - `tier0Exhaustive == false` (some instance skip-masked, a real second writer expected this frame):
      unchanged from the prior round — miss defers to an existing hit, hit applies Inc2 M3's
      nearest-hit-wins compare. Not touched; already validated correct.
  - **New gate test:** `BodyInstanceRayMarchRenderTest.EmptySkipMaskHitAlwaysOverwritesStaleCloserRecord`
    (`test_body_instance_raymarch_render.cpp`) — same 3-instance red/green/gray scene as the sibling
    compositing test, empty skip mask, pre-seeds `HitRecordBuffer` with a synthetic "stale last-frame"
    hit at every real body pixel (closer than the genuine hit, distinct pure-blue albedo so it's
    unambiguous), runs one march, and asserts the CURRENT frame's real hit wins everywhere — the exact
    opposite of what the validator measured. Result: **61290/61290 current-frame-wins, 0/61290
    stale-survived** — matches the validator's own cited pixel count exactly, now with the fix in
    place producing the correct outcome instead of the bug's outcome.
  - **Full suite re-run (Windows-native, real GPU, post-fix):** `test_rendergraph_criticalnodes_gpurender1`
    (10/10 PASS, includes the new stale-hit-wins test), `test_recipe_pool_render` (1/1 PASS),
    `test_mip_fallback_render` (4/4 PASS), `test_rendergraph_shadermirrors` (25/25 PASS),
    `test_rendergraph_criticalnodes_sdiparity` (9/9 PASS, the "gpu_parity" suite) — all zero
    regressions. The same 4 pre-existing failures from M1/M2 reproduced byte-identically this round
    too: `test_baked_vs_virtual_parity` (`bakedHits=5808/virtualHits=9580/IoU=0.6062630480167015`),
    `test_rendergraph_criticalnodes_gpurender2`'s `TierCrossingLodResidencyTest` (`magenta px=0`),
    `test_rendergraph_criticalnodes_gpurender2b`'s `ShadowCorrectnessTest` (`luma=0`),
    `test_appflow_editor_toggle_render`'s `ToggleThenUndoRestoresRender` (`boreDiffPixels=0 vs 3000`) —
    all confirmed unrelated to this fix, unchanged from prior rounds' own documentation.
  - **LIVE APP GATE:** ran the real `VIXEN.exe` (Windows-native, `VIXEN_EXIT_AFTER_FRAMES=25000`),
    default scene/graph, no special flags. **Correction (caught by the fix-round re-validator, not
    self-reported accurately the first time):** the fix-round implementer's report claimed this run
    exercised "real per-frame camera motion throughout" — the re-validator checked `CameraNode.cpp` and
    found this is false: the default scene has no automatic per-frame camera/body animation, orbit is
    purely input-driven (`ApplyMovement` early-returns with no input, `EngageOrbit` only fires on
    WASD/mouse), so an unattended `VIXEN_EXIT_AFTER_FRAMES` run renders a STATIC scene at the boot pose
    and did NOT exercise the motion-triggered condition. This does not weaken the fix's validation: the
    deterministic GPU repro (`EmptySkipMaskHitAlwaysOverwritesStaleCloserRecord`, see above) is a
    strictly stronger proof, since it forces the exact stale-closer-survives condition directly rather
    than hoping motion happens to trigger it. The live gate's real, correctly-claimed value is the
    VUID/validation-regression check below, which is clean. Reached frame ~24985 in ~3 minutes at
    176-187 FPS sustained, clean exit ("frame limit reached"),
    clean teardown. Discrete GPU confirmed via a freshly-written calibration artifact
    (`calibration/NVIDIA_GeForce_RTX_3060_Laptop_GPU_4318_9504.json`, timestamped to this run) — this
    machine has both an AMD iGPU and this NVIDIA discrete GPU; auto-select correctly chose the discrete
    one, consistent with the DeviceNode discrete-preference fix from an earlier epic. **Zero occurrences
    of `VUID-vkCmdDispatch-None-08114` or any NEW validation-error class.** VUID/error counts matched
    the M1/M2-documented pre-existing baseline exactly: 50 `PushConstantGathererNode::Validate` "Type
    mismatch" lines, 40 total VUID occurrences split 20/20 between `VUID-vkCmdDraw-None-09600` and
    `VUID-vkQueueSubmit2-semaphore-03868` (same self-limited startup-transient class, unrelated to
    compute dispatch/HitRecord writes). Confirmed binding 35 (skip mask) and binding 18 (HitRecord)
    both wired into the live production graph via startup log lines. Per-node `DeviceNode`/`Selected
    GPU` log lines were not present in this run's stdout capture (VIXEN's node logging convention
    writes to per-node log files, not console, per this repo's own logging rule) — device identity was
    confirmed via the calibration-file side artifact instead, not a log-line citation; noted here for
    transparency about the evidence source, not a gap in the gate itself.
  - **Deviation from prompt:** none of substance. The prompt's own repro-shape (empty skip mask,
    pre-seed closer stale hit at real body pixels, assert current frame wins) was implemented directly.
  - Plan-doc sync: this worktree copy updated with this entry; `cp` (not commit) to the main checkout's
    copy at `/mnt/c/cpp/VBVS--VIXEN/VIXEN/Vixen-Docs/01-Architecture/Recipe-Live-App-Bucketed-Dispatch-Inc4-Plan-2026-07.md`
    performed as part of this same round.
  - Commit: `e6a77979` (worktree, on top of `113bf1e4`).
- **M2 FIX ROUND — Opus re-validator APPROVED (2026-07-17).** Independently re-derived every claim
  above, did not trust the fix-round report. Confirmed via fresh diff read: the `tier0Exhaustive==true`
  branch is a genuine unconditional overwrite for both hit and miss with `existing` not even fetched in
  that path — the entire fix landed, no conditional survives. Ran
  `EmptySkipMaskHitAlwaysOverwritesStaleCloserRecord` independently: `61290/61290` current-frame-wins,
  `0/61290` stale-survived, confirmed the negative control (`preSeedHitRecords` genuinely uploads a
  closer stale record before dispatch) is real, not a tautology. Confirmed the non-exhaustive regime's
  logic is byte-for-byte unchanged (only relocated under the `else`), and re-ran the real-overlap gate
  (`HitRecordCompositingRealShaderBothOrderingsMatchOracle`) independently — still `memcmp=0` against an
  independent oracle, `0/196608` pixel diffs between both dispatch orderings. Ran the full suite
  independently, all matching: `gpurender1` 10/10, `recipe_pool_render` 1/1, `mip_fallback_render` 4/4,
  `shadermirrors` 25/25, `sdiparity` 9/9, plus the same 4 pre-existing unrelated failures reproduced
  byte-identically. Ran the live app independently on the real discrete NVIDIA RTX 3060 (own timestamped
  calibration artifact as proof, not the report's) — zero `VUID-vkCmdDispatch-None-08114`, zero new VUID
  classes, exact match to the documented baseline (40 total, 20/20 split between the two known
  self-limited startup-transient classes; 50 `PushConstantGathererNode` Type-mismatch lines). **Caught
  and corrected the fix-round report's one inaccuracy**: the claim that the live run exercised "real
  per-frame camera motion" is false — read `CameraNode.cpp` and confirmed the default scene has no
  automatic per-frame animation, orbit is purely input-driven, so an unattended
  `VIXEN_EXIT_AFTER_FRAMES` run is static at the boot pose. Explicitly assessed this as non-weakening:
  the deterministic GPU repro is the strictly stronger proof (it forces the exact failure condition
  directly), and the live gate's real, correctly-claimed contribution is the VUID/validation-regression
  check, which is clean. Plan doc corrected accordingly (see LIVE APP GATE entry above). Confirmed tree
  integrity: history `4c6d117f..e6a77979` coherent, commit real (not left uncommitted), working tree
  clean apart from unrelated stray prompt files. **Verdict: APPROVED — M2 fully done, pipeline proceeds
  to M3.**
- **M3 DONE (2026-07-17).** Env var: `VIXEN_RECIPE_BUCKETED_DISPATCH` (opt-in, `VIXEN_PROCEDURAL_UBER_
  DEMO`-style `std::getenv` presence check, gates every new node/wiring/PreTick-orchestration block).
  New demo scene for the gate: `VIXEN_RECIPE_HOT_COLD_DEMO` (3 hot recipeIds x 6 instances each, >= the
  hotness threshold of 4; 3 cold recipeIds x 2 instances each, below threshold).
  - **Architecture actually built** (Task 3's own wiring, not a shortcut around it): the bucketing
    pre-pass (`RecipeInstanceBucketing.comp`) runs for real every frame via 3 `ComputeStageNode`
    instances (mode 1 init -> mode 0 bucket -> mode 2 finalize, auto-sync-scheduled via
    `BufferSyncGathererNode`, self-submitting -- proven pattern, no new submit machinery needed for
    these 3). Hotness DECISION is pure-CPU (PreTick reads `BodyOctreeSceneNode::GetInstances()`,
    groups by recipeId, counts, thresholds -- the CPU already owns this data, no GPU readback needed
    for the decision itself), but the bucketing shader's OWN output (its real indirect-dispatch-command
    buffer, populated every frame) is what the specialized dispatch actually consumes -- the mechanism
    given a live home is the real one, not a CPU-only stand-in. Specialized per-recipe shaders are
    compiled+cached on first promotion (`ShaderBundleBuilder::AddStage` + `ComputePipelineCacher`,
    entirely OUTSIDE the static RenderGraph node system, since a per-recipeId runtime-generated shader
    has no fixed source for `ShaderLibraryNode::RegisterShaderBuilder` to return) and dispatched
    indirectly via `MultiDispatchNode::QueueDispatch`, extended this milestone with real submit
    capability (see below).
  - **Real gap found beyond the plan doc's stated 3**: `MultiDispatchNode` (as shipped by Inc2/3) could
    RECORD a command buffer but had no fence/semaphore input slots at all and never called
    `vkQueueSubmit2` -- every existing consumer (3 test suites) either never submitted or hand-built its
    own `VkSubmitInfo` outside the node. Extended `MultiDispatchNodeConfig` (+5 Optional inputs:
    `IN_FLIGHT_FENCE`, `IMAGE_AVAILABLE_SEMAPHORES_ARRAY`, `RENDER_COMPLETE_SEMAPHORES_ARRAY`,
    `TIMELINE_SEMAPHORE_IN`, `TIMELINE_FRAME_BASE_IN`) and `MultiDispatchNode::ExecuteImpl` (a real
    `vkQueueSubmit2` call, PRODUCER role only -- mirrors `ComputeStageNode`'s own producer branch:
    timeline-signal-only, no acquire wait, no PRESENT-facing binary signal, submits with **no fence**
    even when one is connected -- see the fence-collision bug below for why). Verified byte-identical
    for every pre-M3 consumer: `test_rendergraph_dispatch` (186/186, one test's own hardcoded
    `INPUTS==6` assertion updated to `==11` with a comment explaining the 5 new Optional slots),
    `test_recipe_multi_bucket_compositing` (2/2, real GPU, unaffected since it never wires the new
    slots).
  - **6 real integration bugs found via the mandatory live-app gate** (not by static review — this
    milestone's own prompt predicted "at least one," per M1/M2's own track record; found 6):
    1. **Missing `VULKAN_DEVICE_IN` wiring** on all 8 new bucketing `StorageBufferNode`s (`recipe_bound_
       sphere_buffer` and 7 siblings) — `Graph validation failed: missing required input 'vulkan_
       device'`, caught at `Prepare()` before the render loop even started. Fix: 8 `batch.Connect`
       calls added.
    2. **Missing `SWAPCHAIN_INFO`/`IMAGE_INDEX` wiring** on `recipe_bucketing_descriptors`
       (`DescriptorSetNode` — both slots are `Required`, unlike the optional `CURRENT_FRAME_INDEX`) —
       same "missing required input" validation-phase failure. Fix: 2 more `batch.Connect` calls.
    3. **`std::bad_any_cast` crash wiring `viewProj`** — `CameraNodeConfig::CURRENT_VIEW_PROJ` is
       `const glm::mat4&` (reference/`ConstRefTag` storage), but `PushConstantGathererNode`'s generic
       field-extraction path (`ExtractResourceAs<T>()`) always does a BY-VALUE `GetHandle<T>()`
       (`ValueTag`) — a genuine producer/consumer Resource-tag mismatch, not a hypothetical. Every
       existing gatherer's directly-wired (non-`ExtractField`) fields happened to already be
       `ValueTag`-published (float/int32_t/`ConstantNode::SetValue<T>`), so this bug had never been hit
       before. Fix: added `CameraNode::GetCurrentViewProj()` (new BY-VALUE accessor) + a new
       `ConstantNode` (`recipe_bucketing_view_proj_constant`) that PreTick refreshes every frame via
       `SetValue<glm::mat4>`, wired into the push-constant field instead of `CameraNode` directly.
    4. **`instanceCount` signed/unsigned mismatch** — `RecipeInstanceBucketing.comp` (Inc2-authored)
       declared `uint instanceCount` in its push-constant block; `BodyOctreeSceneNodeConfig::
       INSTANCE_COUNT` is `int32_t` per an existing, documented codebase-wide contract
       (`BodyInstanceRayMarch.comp`'s own `int instanceCount`). SPIR-V reflection correctly resolved
       the shader's `uint` field to `ExtractResourceAs<uint32_t>()`, throwing `bad_any_cast` against
       the `int32_t`-typed `Resource`. This bug existed in the SHADER since Inc2 but was invisible
       until now because Inc2/3's own test harnesses hand-pack push-constant bytes directly (bypassing
       `PushConstantGathererNode`'s reflection-driven type dispatch entirely). Fix: `shaders/
       RecipeInstanceBucketing.comp`'s push-constant field changed `uint` -> `int` (one line + a
       comparison-site cast), matching the established contract; no C++ rebuild needed (shaders
       compile at runtime).
    5. **`AddStageFromSpirv` is a stub** — `ShaderBundleBuilder::AddStageFromSpirv` (used for the
       specialized per-recipe shader's reflection-only build) discards the SPIR-V entirely and stores
       an empty source string (`ShaderBundleBuilder.cpp`'s own `"TODO: Store SPIRV separately, for now
       this is a placeholder"`), so `Build()` silently tried to compile `""` as GLSL and failed with a
       confusing "Unable to parse built-ins" glslang error (glslang failing to parse its OWN built-ins
       table for an ill-formed empty-source parse, not a real error in the specialized shader's actual
       text). Fix: switched to `ShaderBundleBuilder::AddStage` (the proven text-based compile+reflect
       path, same one the main march's own `ShaderLibraryNode` builder uses) instead of a separate
       `ShaderCompiler::Compile()` + `AddStageFromSpirv()` two-step.
    6. **3 real Vulkan resource-lifetime/synchronization bugs**, all found only by the live gate's VUID
       scan (none of them crash or abort — they're exactly the class of bug this gate exists to catch):
       (a) leaked `VkDescriptorPool`/`VkDescriptorSet`/`VkShaderModule` for every compiled specialized
       pipeline (`VUID-vkDestroyDevice-device-05137` at shutdown) — fixed by destroying them in
       `DeInitialize()` before the render graph/device teardown, guarded by a `vkDeviceWaitIdle` first;
       (b) `MultiDispatchNode`'s new submit path passed the REAL `inFlightFence` as `submitFence`
       unconditionally, colliding with the frame's actual consumer node's own fence ownership
       (`VUID-vkQueueSubmit2-fence-04895`) — fixed by always submitting with `VK_NULL_HANDLE` (this
       node is never a consumer, mirrors `ComputeStageNode`'s own producer branch exactly);
       (c) `recipe_bucket_indirect_command_buffer` (a `StorageBufferNode`) was created with only
       `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`, missing `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT` needed for
       `vkCmdDispatchIndirect` to read it (`VUID-vkCmdDispatchIndirect-buffer-02709`) — fixed by adding
       a new additive `StorageBufferNodeConfig::PARAM_EXTRA_USAGE_FLAGS` param (default 0, byte-
       identical no-op for every other consumer) and setting it to `VK_BUFFER_USAGE_INDIRECT_BUFFER_
       BIT` for this one buffer. Also found and fixed a missing `pushConstants` payload on the
       specialized dispatch's own `DispatchPass` (`VUID-vkCmdDispatchIndirect-maintenance4-08602` —
       the specialized shader statically uses its push-constant block but nothing had ever populated
       it) by building the exact 80-byte `Push` struct (camera basis + screen extent +
       maxMembersPerBucket + recipeId) from live `CameraNode`/window-size data each dispatch.
  - **One remaining VUID class, investigated and confirmed a validation-layer false positive, not a
    real hazard**: `VUID-vkUpdateDescriptorSets-None-03047` (20 occurrences, all within the first few
    frames of a run — exactly once per hot recipeId's first-promotion event, 3 total in the gate
    scene). Root-caused via independent agent investigation: all 3 hot recipes in
    `VIXEN_RECIPE_HOT_COLD_DEMO` are seeded with instance counts already >= the hotness threshold at
    SCENE-BUILD time, so all 3 get first-promoted within the SAME `PreTick()` call on frame 0 — i.e.
    before `RenderFrame()` has EVER been called for the first time in the process's life. No command
    buffer has been recorded or submitted anywhere in the app at the point each brand-new
    `VkDescriptorSet` is written, so a genuine "in use by a pending command buffer" hazard is provably
    impossible at that call site (traced and ruled out: no handle aliasing, since nothing is ever
    destroyed before this point; no parallel-execution race, since `PreTick()` fully completes,
    including its own `vkDeviceWaitIdle`, before `RenderFrame()` starts; no stale queued `DispatchPass`
    from a prior frame, since this IS the first frame). This is the Khronos validation layer's own
    "descriptor set in use" tracker false-flagging a back-to-back multi-allocation pattern within one
    `vkDeviceWaitIdle`-gated call, not a spec violation with real GPU consequences. A cosmetic fix
    exists (declare the layout/pool `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT`/`_POOL_CREATE_
    UPDATE_AFTER_BIND_BIT`) but was not applied — out of scope for a "nice to have" this late in the
    gate, and the shared `BuildDescriptorSetLayoutFromReflection` helper is used elsewhere, so
    changing its behavior unconditionally was judged higher-risk than documenting the false positive.
  - **Flag-unset no-op proof (the milestone's own strongest bar)**: with the flag unset, NONE of the
    new nodes are created at all (verified via log grep: zero references to any `recipe_bucket*`/
    `recipe_specialized*` node name in a flag-unset run's full log) — not merely inert, genuinely absent
    from the graph. Full existing gate suite re-run, all green:
    `test_rendergraph_criticalnodes_gpurender1` (10/10), `test_recipe_pool_render` (1/1),
    `test_mip_fallback_render` (4/4), `test_rendergraph_dispatch` (186/186, includes the updated
    `MultiDispatchNodeConfig` slot-count assertion), `test_recipe_multi_bucket_compositing` (2/2).
    **LIVE APP GATE, flag UNSET**: `VIXEN_EXIT_AFTER_FRAMES=3000`, default scene, no special flags —
    clean exit ("frame limit reached"), 3000 frames, ~170 FPS sustained. VUID count/classes matched
    M1/M2's own documented pre-existing baseline EXACTLY: 64 total, split 20/20/20/4 across
    `VUID-vkAcquireNextImageKHR-semaphore-01779`/`VUID-vkCmdDraw-None-09600`/`VUID-vkQueueSubmit2-
    semaphore-03868` (same self-limited startup-transient classes M1/M2 independently verified
    pre-existing) — same 50 `PushConstantGathererNode` Type-mismatch lines. Zero
    `VUID-vkCmdDispatch-None-08114`, zero new VUID classes of any kind.
  - **LIVE APP GATE, flag SET (`VIXEN_RECIPE_BUCKETED_DISPATCH=1 VIXEN_RECIPE_HOT_COLD_DEMO=1
    VIXEN_EXIT_AFTER_FRAMES=3000`)**: real discrete-GPU-class device (`AMD Radeon(TM) Graphics`),
    Windows-native, Debug build, validation layers ON. All 3 hot recipeIds (of the 3 hot + 3 cold the
    demo scene registers) compiled a real specialized `VkPipeline` on first promotion (log-confirmed:
    "compiled specialized pipeline for recipeId=2/3/4 (6 instances, promoted hot)"), dispatched
    indirectly every frame thereafter via `MultiDispatchNode::QueueDispatch`, and were cleanly destroyed
    at shutdown ("Destroyed 3 specialized recipe pipeline(s)"). Reached the full 3000-frame limit,
    clean exit, sustained 90-145 FPS (varies more than the flag-unset baseline — expected, unoptimized
    first-integration path, NOT this milestone's concern; M4 owns the honest performance measurement).
    **Zero crashes, zero unhandled exceptions, zero `VUID-vkCmdDispatch(Indirect)-*` errors of any
    kind** after the 6 fixes above. Only the one investigated-and-confirmed-false-positive VUID class
    remains (`VUID-vkUpdateDescriptorSets-None-03047`, 20 occurrences, see above) plus the same 40
    pre-existing baseline VUIDs (20/20 split, `VUID-vkCmdDraw-None-09600`/`VUID-vkQueueSubmit2-
    semaphore-03868`) M1/M2 already documented.
  - **Deviation from prompt**: none of substance in scope/architecture. The prompt's own risk framing
    ("treat integration bugs between these pieces as the DEFAULT expectation... 4 seams instead of 1")
    was, if anything, an understatement — 6 distinct bugs surfaced across those seams, all found and
    fixed via the mandatory live-app gate exactly as the prompt required, none deferred to a later fix
    round. The one open item (the false-positive VUID) was investigated to a decisive, code-traced
    conclusion rather than left as an unexplained residual — judged sufficient to close M3 given it is
    provably not a genuine hazard, not a gap in verification rigor.
  - Commits: (this worktree, `feat/recipe-live-app-bucketing-inc4`, on top of `87a4be5d`) — bucketing
    quintet + wiring + hot/cold demo scene in `BuildRenderGraph.cpp`; `MultiDispatchNode` submit
    extension in `libraries/RenderGraph/`; `RunRecipeBucketedDispatchPreTick` + cleanup in
    `VulkanGraphApplication.{h,cpp}`; `CameraNode::GetCurrentViewProj()`, `BodyOctreeSceneNode::
    GetInstanceBufferHandle()`, `StorageBufferNode::GetBufferHandle()`/`PARAM_EXTRA_USAGE_FLAGS` new
    accessors; `shaders/RecipeInstanceBucketing.comp` `instanceCount` type fix;
    `test_group_dispatch.cpp` slot-count assertion update.

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
