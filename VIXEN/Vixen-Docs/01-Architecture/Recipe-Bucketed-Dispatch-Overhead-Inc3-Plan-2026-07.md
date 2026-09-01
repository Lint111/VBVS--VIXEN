# Recipe Bucketed-Dispatch Overhead — Increment 3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: use the post-brainstorm-context-manager pipeline to
> implement this plan milestone-by-milestone (fresh implementer + Opus validator per milestone,
> worktree-isolated, progress persisted in this doc; pre-bless the in-tree destructive/git tier at
> setup). **Live-run gates are authoritative for EVERY milestone** — every milestone here touches
> shaders, dispatch, or GPU synchronization and must end in a real GPU run with validation layers
> explicitly enabled (Windows-native, discrete GPU — see M0/M2's explicit GPU-selection requirement
> below, this is not optional given Increment 2's own GPU-selection gotcha). Never overlap two builds
> of one target. Watch long builds with a foreground polling loop, not a blind wait.

**Goal:** Close the gap [[Recipe-GPU-Instance-Bucketing-Inc2-Plan-2026-07]]'s M4 honestly measured —
bucketed dispatch is CONSISTENTLY SLOWER than the tier-0 switch at every tested N (0.31x at N=3 down
to 0.05x at N=100), root-caused to N separate `vkCmdDispatchIndirect` calls + descriptor-set binds +
`MultiDispatchNode` auto-barrier insertions, confirmed to total **5N−1 Vulkan API calls for N
buckets** (grounding research, 2026-07-16 — see Progress Log). This increment reduces that per-bucket
fixed cost where it's genuinely reducible, and treats what genuinely isn't reducible (per-pipeline
`vkCmdBindPipeline`) as a hard architectural ceiling this increment does NOT attempt to break — that
ceiling is [[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]]'s territory, a different,
larger increment, not this one's.

**What "done" looks like for this increment:** either (a) a measured, honest reduction in bucketed
dispatch's per-bucket overhead, re-validated against the SAME tier-0 baseline methodology M4 used, at
the SAME N values, on the SAME (discrete) GPU class — reporting the new speedup/slowdown ratios
honestly, whether or not they now favor bucketed dispatch; or (b) if M0's spike reveals tier-0's own
N=100 knee is NOT switch-dispatch-cost-shaped, an honest scope pivot documented and handed back to the
epic doc rather than continuing to "fix" a problem whose target turned out to be mischaracterized.
**This increment does NOT ship**: a single-dispatch-with-per-thread-selection redesign (that's the
separate, not-yet-scoped direction doc above, gated on this increment's own M0 finding); async
compile (already-established non-fix per M4's own finding — compile cost is excluded from the
steady-state numbers this increment targets); any change to the tier-0 switch shader itself.

**Depends on (shipped):** [[Recipe-GPU-Instance-Bucketing-Inc2-Plan-2026-07]] (Increment 2, M1-M4,
merged main `7a27357f`) — this increment operates entirely within Inc2's shipped mechanism
(`MultiDispatchNode`, `SpecializedRecipeShaderGlsl.h`, `RecipeInstanceBucketing.comp`), it does not
redesign it.

**Grounding of record:** see this plan's own Progress Log below for the 2026-07-16 research pass that
scoped this increment — exact API-call counts, the linear-scaling arithmetic check, confirmed absence
of any native Vulkan dispatch-indirect-count mechanism, and the explicit open question (tier-0's own
knee root cause) this plan's M0 resolves before the rest proceeds.

**Tech Stack:** C++23, GLSL compute, Vulkan 1.3 (`vkCmdDispatchIndirect`, `VkMemoryBarrier2` /
`VK_KHR_synchronization2`), GoogleTest, CMake ninja/wsl presets + Windows `.bat` builds, real discrete
GPU (Windows-native) for every milestone.

---

## §0. Scope

**In scope:**
- **M0 — decisive, cheap spike**: isolate whether the ORIGINAL tier-0 switch's N=100 knee (the
  2026-07-10 switch-scaling measurement, ~8x FPS collapse) is caused by switch-dispatch/branch cost,
  or by register-pressure/instruction-cache thrash from having many recipes' differing code resident
  in one compiled shader. This is a **gating** milestone — its result determines whether M1+ proceeds
  as planned or the increment pivots.
- Reducing per-bucket `vkCmdBindDescriptorSets` + `vkCmdPushConstants` calls where the underlying data
  genuinely doesn't require a distinct descriptor set per bucket (a shared SSBO + per-dispatch offset
  or index, instead of per-bucket descriptor-set aliasing) — IF M0 confirms this is worth pursuing.
- A careful, correctness-first analysis of whether `MultiDispatchNode`'s N−1 `InsertAutoBarrier` calls
  can be coalesced into fewer barrier calls WITHOUT weakening the ordering guarantee M3 proved
  (write-after-write hazard between every sequential pair touching `HitRecord`) — this is explicitly
  a "prove correctness first, optimize second" task, not an assumed-safe optimization.
- Re-measuring the FULL M4 comparison (same N values, same discrete-GPU requirement, same honest-
  reporting standard) after any change, so any claimed improvement is verified the same rigorous way
  M4's original (negative) finding was.

**Out of scope (explicitly, do not let scope creep here):**
- `vkCmdBindPipeline` reduction/elimination — confirmed architecturally unavoidable under the current
  one-specialized-pipeline-per-bucket design (no Vulkan mechanism, core or extension, batches
  multi-pipeline dispatch; grounding research 2026-07-16 confirmed zero relevant extensions are even
  enabled). Do NOT attempt a workaround here — that's the single-dispatch direction doc's territory,
  a different architecture, a different (much larger) increment.
- A native "dispatch-indirect-count" mechanism — confirmed NOT to exist in core Vulkan 1.3 or any
  extension (unlike `vkCmdDrawIndirectCount`'s draw-side equivalent). Do not spend time searching for
  one; the grounding research already exhausted this.
- Async compile / promotion-latency work — M4 already established this is orthogonal (compile cost is
  excluded from the steady-state numbers this increment targets).
- Any change to the tier-0 switch shader (`BodyInstanceRayMarch.comp`, `UberShaderSplice.h`) — M0 is a
  STANDALONE measurement using a synthetic shader, not a production change to the real tier-0 path.

---

## Milestone Map

- **M0 — Isolate tier-0's N=100 knee root cause via randomized N/m_i/k_i stress test (GATING
  milestone)** (Task 1) · **live-run gate, discrete GPU mandatory** · a randomized-recipe-corpus
  stress measurement (N distinct recipes of varying step-count `m_i`, `k_i` instances of each
  rendered simultaneously) determines whether the ORIGINAL tier-0 baseline's own scaling problem is
  switch/branch-shaped or code-size/register-pressure-shaped — this result gates whether M1+
  proceeds as planned. Supersedes an earlier "identical-trivial vs. real-differing" binary design
  (2-point comparison) per user correction 2026-07-16: a single combined, randomized, swept
  experiment is a strictly better isolation of the same question than two hand-picked extremes.
  - [x] **DONE 2026-07-16, commit `2e6a4f51`, Opus validator APPROVED (independently re-derived the
    same conclusion, found an even cleaner single-axis isolation than the implementer's own
    headline evidence).** Finding: MIXED m_i+k_i correlation, N (switch-case count) ruled OUT as an
    independent driver — see Perf-Ledger.md "Switch-cost isolation (Inc3 M0...)" for full swept
    data + axis-decoupling proof. **USER SCOPE DECISION 2026-07-16: proceed with M1/M2 as
    originally scoped.** M1/M2 target Increment 2's own separately-confirmed regression (bucketed
    dispatch is measurably slower than tier-0 at every N, M4's finding) — that is a real, distinct
    problem worth fixing on its own merits regardless of what drives tier-0's SEPARATE m_i/k_i-
    shaped scaling problem. The m_i/k_i finding does NOT block M1/M2; it is carried forward as
    grounding for a FUTURE increment (informing
    [[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]], not this one's scope).
- **M1 — Reduce per-bucket descriptor/push-constant overhead** (Task 2) · **live-run gate** · shared
  SSBO + per-dispatch offset/index replaces per-bucket descriptor-set aliasing where data doesn't
  genuinely require a distinct set; correctness proven against the existing M2/M3 oracle pattern.
  - [x] **DONE 2026-07-16.** `BucketMembersBuffer` (binding 1) now binds the FULL shared
    `bucketIndices[]` output from the M1-bucketing pre-pass directly (row-major, indexed by
    `recipeId * maxMembersPerBucket + m`) instead of a per-bucket CPU-readback+reupload slice. A
    NEW `BucketMetaBuffer` (binding 3) — one shared SSBO, one 32B entry per recipeId
    (`memberCount`/`rectMinX`/`rectMinY`/`boundRadius`/`stepRelaxation`) — replaces those 5 fields
    in the push-constant block; the push-constant struct shrinks from 92B to 80B (camera/screen
    fields + a single `recipeId` selector). All N specialized pipelines share ONE
    `VkPipelineLayout`, so ONE shared `VkDescriptorSet` is bound on the FIRST bucket's
    `DispatchPass` only — subsequent buckets leave `descriptorSets` empty, which
    `MultiDispatchNode::RecordDispatches`' existing `if (!pass.descriptorSets.empty())` guard
    skips entirely (Vulkan spec-confirmed: descriptor-set bindings persist across
    `vkCmdBindPipeline`/`vkCmdDispatchIndirect` when pipeline layouts stay compatible — "Pipeline
    Layout Compatibility"). **Measured API-call-count reduction** (`test_recipe_bucketing_perf`,
    real discrete NVIDIA RTX 3060 Laptop GPU): `vkCmdBindDescriptorSets` N→1 at every tested N
    (3→1, 10→1, 100→1); `vkCmdPushConstants` stays N calls (recipeId still varies per bucket) but
    each call's payload shrinks 92B→80B. **Correctness**: `test_recipe_bucketed_indirect_dispatch`
    (M2, single-bucket) — GPU vs. independent CPU oracle, `matchedHits=1664`,
    `maxHitTDelta=0.00000`; `test_recipe_multi_bucket_compositing` (M3, overlap-compositing) — GPU
    vs. oracle `matchedHits=2984` both orderings, **0/65536 pixels differ between hot-first and
    cold-first HitRecord buffers** (the strongest available proof this refactor didn't disturb M3's
    order-independence proof). **Full regression**: `test_recipe_instance_bucketing` (1/1),
    `test_recipe_bucketed_indirect_dispatch` (4/4), `test_recipe_multi_bucket_compositing` (2/2),
    `test_recipe_bucketing_perf` (3/3), `test_switch_cost_isolation` (6/6, M0's own suite,
    unaffected as expected) — **16/16 passing**, all on the confirmed discrete NVIDIA GPU. No
    change to `vkCmdBindPipeline` count (out of scope, unchanged) or barrier count (M2's job).
    Deviation from prompt: none — mechanism matches the prompt's suggested design ("single shared
    descriptor-set bind instead of N," offset/index via push-constant) exactly.
    **Opus validator: APPROVED.** Independently confirmed the load-bearing spec claim, not taken
    on faith — traced `PipelineLayoutCacher::ComputeKey` to prove all N specialized pipelines
    resolve to the IDENTICAL `VkPipelineLayout` handle (cache hit, not merely "compatible"), and
    confirmed the implementer correctly re-binds the descriptor set in M3's cold-first ordering
    (after an incompatible cold pipeline runs) rather than naively binding once — the 0/65536
    order-independence re-run is empirical proof this is right. Zero validation-layer errors across
    every reproduced run (had the persistence assumption been wrong, `VK_LAYER_KHRONOS_validation`
    would have flagged a descriptor-set VUID immediately). Independently re-ran all 16 tests
    fresh, confirmed freshly-relinked binaries (no stale-pass risk). Cleared to proceed to M2.
- **M2 — Barrier-coalescing correctness analysis + (if safe) reduction** (Task 3) · **live-run gate**
  · prove whether N−1 barriers can become fewer without weakening M3's write-after-write ordering
  proof; implement ONLY if a safe reduction is actually established, not assumed.
  - [x] **DONE (analysis-only) 2026-07-16, commit `5394c889`.** **Verdict: a safe reduction is
    theoretically sound in principle but its soundness precondition is currently VIOLATED by an
    unrelated, real gap — NOT implemented this milestone.** No code change. Filed as **KI-037**
    (`ProjectToPixel` silently drops camera-straddling points, shrinking coverage rects below their
    true footprint — the wrong direction for the scheme's "always over-cover" requirement; latent,
    no current test constructs a camera-straddling hot instance, but blocks a rect-disjointness-
    based barrier skip). **Opus validator: APPROVED.** Independently re-derived the Vulkan
    memory-model reasoning from scratch (confirmed accurate: hazards are per-memory-location, no
    implicit ordering between un-barriered dispatches), traced the actual shader code and
    numerically confirmed the near-plane bug is real, and specifically checked whether the
    all-or-nothing refusal was overly conservative — confirmed a narrower "skip only for
    non-straddling pairs" scheme would STILL require plumbing spatial extent into
    `MultiDispatchNode` (which has zero concept of dispatch spatial extent today) plus a sound
    straddling-detection path, both genuinely beyond this analysis milestone's scope. One advisory
    refinement caught for a future implementer: the write-set-disjointness basis should compare
    8px-workgroup-rounded footprints, not raw coverage rects, since the dispatch grid rounds up to
    workgroup multiples (reinforces, doesn't weaken, the milestone's conclusion). 16/16 regression
    suite reproduced independently, zero new regressions. Cleared to proceed to M3.
- **M3 — Re-measurement + honest doc closure** (Task 4) · **live-run gate, discrete GPU mandatory** ·
  full M4-equivalent perf comparison re-run after M1/M2's changes, honestly reported (including a
  "still slower" outcome if that's what the data shows), plan/epic doc closure.
  - [x] **DONE 2026-07-16.** Full re-run of Inc2 M4's exact comparison
    (`test_recipe_bucketing_perf`, same 3 GTest cases/N values/scene shape) on the confirmed
    discrete `NVIDIA GeForce RTX 3060 Laptop GPU`, twice (reproducibility check). **Result: NO
    MEANINGFUL IMPROVEMENT.** Speedup ratios are flat within run-to-run noise vs. M4's original
    baseline: N=3 0.31x→0.30-0.33x, N=10 0.25x→0.22-0.24x, N=100 0.05x→0.04-0.05x. M1's real,
    verified `vkCmdBindDescriptorSets` N→1 reduction (14.3%/18.4%/19.8% of total 5N−1 API calls at
    N=3/10/100) did not translate into any detectable steady-state speedup — the arithmetic
    sanity-check (required by this milestone) shows a ~15-20% call-count reduction producing
    essentially 0% measured improvement, meaning descriptor-set binds were not a disproportionate
    share of the actual per-bucket bottleneck on this GPU/driver; the dominant cost is more likely
    `vkCmdDispatchIndirect` and/or the architecturally-unavoidable `vkCmdBindPipeline`, neither
    addressed by this increment. Full numbers, methodology, and the arithmetic check: see
    Perf-Ledger.md "Bucketed-dispatch re-measurement (Inc3 M3...)" section. **No-regression sweep:**
    131 GTest binaries run Windows-native on the same GPU, validation layers on — 1769 individual
    test cases passed, 0 assertion failures; this increment's own 4 target tests
    (`test_recipe_instance_bucketing`, `test_recipe_bucketed_indirect_dispatch`,
    `test_recipe_multi_bucket_compositing`, `test_recipe_bucketing_perf`) all pass cleanly (1+4+2+3
    = matches M1's 16/16 baseline). 16 binaries reported a non-zero exit/timeout; every one
    individually cross-checked against `git diff main` (zero overlap — this branch touches only
    recipe-bucketing dispatch files) and against Known-Issues.md: all are pre-existing/unrelated
    (KI-034's stale-push-constant-mirror files, KI-032's empty-colorbuffer-readback files, already-
    documented multi-minute SDF-bake binaries, or unrelated environment issues — a Windows temp-file
    handle race, a missing EOS-overlay JSON, HUD/RML rendering). **Zero new regressions.** Deviation
    from prompt: none.
    **Increment 3 overall verdict, honestly stated:** the story across M0-M3 IS coherent, even
    though the outcome is not a win. M0 correctly gated the increment's premise and found tier-0's
    OWN knee is m_i/k_i-shaped (a separate problem, carried forward, not fixed here) but correctly
    did NOT block M1/M2 (a real, distinct, already-confirmed regression worth investigating on its
    own merits). M1 shipped a real, correctness-verified improvement to ONE specific cost
    (descriptor-set bind count) — this was genuine, measured engineering, not wasted work. M2
    honestly found a theoretically-sound but currently-blocked optimization path (real bug filed,
    KI-037) rather than forcing an unsafe reduction. M3 (this milestone) honestly reports that the
    one change which DID ship doesn't move the needle on the metric this whole increment was
    scoped around. **This increment does not close the bucketed-dispatch-vs-tier-0 gap** — the
    real next step, per M0's own finding, is more likely
    [[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]]'s single-dispatch-no-switch
    territory (addressing m_i/k_i-shaped cost directly) than further per-bucket-call-count
    reduction, since this increment's entire premise (per-bucket API call count is the dominant
    cost) is now measured, not just theorized, to be false or at least not the dominant factor.
    **Opus validator: APPROVED — full M0-M3 increment ready for final review + merge.**
    Independently re-ran the perf harness twice (own results: 0.280x/0.253x/0.054x and
    0.311x/0.253x/0.042x at N=3/10/100 — squarely in the reported band; one own-run number at
    N=3 landed BELOW the reported figure, direct evidence against positive spin). Verified the
    5N−1/N−1 arithmetic exactly. Directly opened `Known-Issues.md` and confirmed every one of the
    16 non-clean binaries is literally named in KI-034/KI-032, with zero overlap against this
    branch's diff (confirmed the diff touches only `SpecializedRecipeShaderGlsl.h` + 4 test files +
    cmake + docs). Confirmed all three doc-closure files (this plan, JIT epic §7, Perf-Ledger) carry
    identical numbers with no drift. Confirmed `git diff M1..HEAD` on non-test production code is
    empty — M1 remains the increment's ONE and ONLY shipped code change, exactly as claimed.

### Progress Log

(populated as milestones complete — one entry per milestone: commit hash, gate evidence, Opus
validator verdict.)

- **Grounding research (2026-07-16, pre-M0):** confirmed via direct code inspection
  (`MultiDispatchNode.cpp:561-687`, `SpecializedRecipeShaderGlsl.h`, `RecipeInstanceBucketing.comp`)
  that the legacy `taskQueue_`-based dispatch path (what M4's harness uses) issues **5N−1 Vulkan API
  calls for N buckets**: 1× `vkCmdBindPipeline` + 1× `vkCmdBindDescriptorSets` + 1×
  `vkCmdPushConstants` + 1× `vkCmdDispatchIndirect` per bucket (4N), plus N−1
  `InsertAutoBarrier`/`fpCmdPipelineBarrier2` calls (barriers fire for every bucket EXCEPT the first,
  confirmed at `MultiDispatchNode.cpp:586-590`). At N=100 that's 499 API calls. Confirmed the M4
  Perf-Ledger's "linear scaling" claim via a coarse per-bucket-excess-cost check: (bucketed −
  cold-stand-in) / N ≈ 0.123 ms/bucket at BOTH N=10 and N=100 (0.244 ms/bucket at N=3, plausibly
  small-N/warm-up noise given only 30 steady iterations) — consistent with linear per-bucket
  overhead, though only a 3-point check, not a rigorous fit. Confirmed `vkCmdBindPipeline` is
  architecturally unavoidable per-bucket (distinct compiled pipeline per specialized shader, no
  Vulkan batching mechanism exists for this). Confirmed NO native Vulkan
  dispatch-indirect-count mechanism exists anywhere in core 1.3 or any extension VIXEN currently
  enables (`VulkanDevice.cpp` extension list checked directly — RTX/accel-structure/sync2/maintenance
  extensions only, nothing dispatch-batching-related). Confirmed the barrier-coalescing question
  (whether N−1 could become fewer) is a genuine, unanalyzed gap — the current code proves N−1 is
  SUFFICIENT for correctness, never establishes whether fewer would ALSO be correct. **Most
  important finding**: confirmed the open question flagged in
  [[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]] (is tier-0's OWN N=100 knee
  switch-cost or register-pressure/icache-shaped) remains completely unmeasured anywhere in the
  repo — this directly motivated adding M0 as a gating milestone before committing to M1/M2's
  batching-oriented fixes, per explicit user decision 2026-07-16 ("cheap measurement spike first,
  then scope the plan").

- **M0 (2026-07-16):** randomized N/m_i/k_i stress harness (`test_switch_cost_isolation.cpp`,
  new standalone GTest target) built and run on confirmed discrete NVIDIA RTX 3060 Laptop GPU.
  Main N=3/10/100 sweep (random m_i in [3,50], k_i in [1,20] per recipe) showed a real/control
  ratio growing monotonically with N: ~1.0x at N=3, ~1.8x at N=10, ~2-4x at N=100 (multiple
  repeated trials of the same seeded draw at each N). Three axis-decoupling cases (pinning m_i/k_i
  narrow while N varies) then isolated the actual driver: **N=100 with m_i pinned to [3,5] and
  k_i pinned to [1,3] collapsed the ratio to 1.19x** (statistically indistinguishable from N=3's
  baseline) despite the switch still having 100 cases — ruling OUT switch-case-count/branch-
  dispatch cost as the driver. **N=10 with m_i pinned to [45,50] and k_i pinned to [15,20] raised
  the ratio to 2.66x** — higher than N=100's own large-m_i/low-k_i case (1.30x) — with switch-case
  count held at only 10. **Verdict: MIXED m_i (code-size/register-pressure) + k_i (instance-count/
  re-evaluation-count) correlation, N ruled out as an independent factor.** Full swept data,
  seed/methodology, and the decision-gate writeup are in Perf-Ledger.md's "Switch-cost isolation
  (Inc3 M0...)" section. **At the M0 decision point, per the plan doc's explicit scope, M1/M2 were
  not yet started; both were subsequently executed and recorded below.** This finding
  was handed to the controller/user: M1/M2 as then framed (per-bucket dispatch-COUNT overhead)
  target a switch-case-count-shaped cost this measurement found is NOT what's driving tier-0's own
  N=100 knee; the knee is m_i/k_i-shaped, pointing more toward
  [[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]]'s single-dispatch-no-switch
  territory than toward reducing bucket-count overhead.

- **M2 (2026-07-16):** barrier-coalescing correctness analysis (Task 3), analysis-only, no code
  change. **Central finding, in two parts:**
  (1) **The Vulkan memory-model fact that settles the general question**: a hazard (and therefore
  the spec's requirement for an explicit dependency) exists between two commands only when they
  access *overlapping memory* and at least one access is a write — this is defined per memory
  location, not per buffer object. `VkMemoryBarrier2`'s access/stage masks are global in scope
  (unlike `VkBufferMemoryBarrier2`/image-subresource barriers, which can scope to a byte range or
  mip/array slice), but that only describes what a barrier, if present, synchronizes — it does NOT
  mean the spec requires a barrier between two dispatches whose actual write sets are PROVABLY
  disjoint index ranges of the same buffer. Separately, and just as load-bearing: Vulkan gives NO
  implicit ordering between two compute dispatches recorded back-to-back in one command buffer with
  no barrier between them — their invocations may execute concurrently on the device. So the
  barrier's real job here is not "publish writes to a later reader," it's "prevent two dispatches
  whose invocations can touch the same address from running concurrently." Given
  `SpecializedRecipeShaderGlsl.h`'s write pattern (`hitIdx = pixelCoords.y * screenWidth +
  pixelCoords.x`, and `pixelCoords` is hard-bounded to `[rectMinX, rectMinX+rectWidth) x [rectMinY,
  rectMinY+rectHeight)` both by an explicit early-return check AND by the fact that the dispatch's
  OWN workgroup count, written by `RecipeInstanceBucketing.comp`'s mode==2 finalize pass, is sized
  exactly to that rect — the thread grid cannot even launch outside it): **if two buckets' coverage
  rects are disjoint in screen space, their `HitRecord` write sets are structurally, provably
  disjoint, and the spec does not require a barrier between those two dispatches.** This is the
  precise mechanism M2's Task 3 asked to establish, and it checks out.
  (2) **Why it is NOT implemented this milestone**: applying (1) requires the coverage rects
  themselves to be a SOUND conservative superset of each bucket's true write footprint (rect ⊇
  footprint must hold in every case, or "rects disjoint" stops implying "footprints disjoint").
  Traced this precondition directly and found it is currently VIOLATED:
  `RecipeInstanceBucketing.comp`'s `ProjectToPixel` (lines 134-144) computes the coverage rect by
  projecting 7 world-space points (bound-sphere center + 6 axis extrema) and silently DROPPING any
  point with `clip.w <= 0` (behind-camera) from the union (`if (clip.w <= 0.0) return false;`, no
  contribution to minX/minY/maxX/maxY). For an instance whose bound sphere straddles the camera's
  near/W=0 plane (center behind camera, some extremal points in front, or vice versa), this SHRINKS
  the computed rect below the sphere's true on-screen footprint rather than growing it — the
  opposite of the conservative direction the scheme needs. Confirmed via `grep` that no frustum/
  near-plane culling exists anywhere upstream of this pass (`RecipeInstanceBucketing.comp` and
  `MultiDispatchNode.cpp` both searched) — camera-straddling instances are a real, reachable,
  currently-unguarded case in this pipeline, not a hypothetical. Implementing the barrier reduction
  on top of this would mean: two buckets whose TRUE footprints overlap (because one has a
  camera-straddling instance with an under-computed rect) could be classified "disjoint" by the
  rect check and have their barrier incorrectly skipped — silently reintroducing the exact
  write-after-write race M3 proved doesn't happen today. This is precisely the failure mode M2's
  prompt and the plan's Risks section warned against forcing past. **Per the plan's own "if no safe
  reduction is established" clause: this is a legitimate, honest outcome, not a forced optimization.
  A safe reduction is NOT ruled out in general** (part 1's reasoning is sound and reusable) **— it
  is blocked on a separate, currently-unfixed soundness gap in M1's coverage-rect computation.**
  Fixing `ProjectToPixel`'s near-plane handling (e.g. clip the sphere against the near plane instead
  of dropping w<=0 points, or fall back to full-screen coverage for any instance with a mixed-sign
  W extremum) would be the prerequisite for a future increment to revisit this — that, plus the
  separate (non-trivial) work of plumbing per-bucket screen-rect data into `MultiDispatchNode`
  itself (which today has no concept of a dispatch's spatial extent — it sees an opaque
  `DispatchPass` queue) so it could decide barrier-skip pairs, is out of THIS milestone's scope.
  **No code changed; M1's improvement stands as this increment's real contribution, per the plan's
  own explicit fallback.** Full regression suite not re-run for M2 specifically (no code change to
  regress) — M1's 16/16 passing baseline stands unchanged; M3 will re-run the full suite alongside
  its re-measurement pass regardless.

- **M3 (2026-07-16):** full re-measurement of Inc2 M4's exact comparison
  (`test_recipe_bucketing_perf`, N=3/10/100), run twice for reproducibility on the confirmed
  discrete `NVIDIA GeForce RTX 3060 Laptop GPU`. **No meaningful improvement**: speedup ratios flat
  within noise vs. M4's baseline at every N (0.31x→0.30-0.33x, 0.25x→0.22-0.24x,
  0.05x→0.04-0.05x). Arithmetic sanity-check: M1 removed 14.3%/18.4%/19.8% of total 5N−1 API calls
  at N=3/10/100 but this produced ~0% measured speedup change — descriptor-set binds were not a
  disproportionate share of the real bottleneck on this GPU/driver. Full regression: 131 GTest
  binaries, 1769 cases passed, 0 assertion failures; this increment's 4 target tests all pass
  (1+4+2+3, matches M1's 16/16); 16 binaries with a non-zero exit, all individually confirmed
  pre-existing/unrelated (KI-034, KI-032, documented slow SDF-bake binaries, unrelated environment
  issues) via `git diff main` (zero file overlap) and Known-Issues.md cross-check — zero new
  regressions. Full numbers/methodology: Perf-Ledger.md "Bucketed-dispatch re-measurement (Inc3
  M3...)". **Increment 3 overall: coherent story, real-but-insufficient result** — M0's gating
  finding stands (tier-0's own knee is m_i/k_i-shaped, not switch-count-shaped), M1's descriptor-
  bind reduction is real and correctness-verified but M3 shows it doesn't move the bucketed-vs-
  cold-path ratio, M2's honest non-reduction stands. This increment does NOT close the gap M4
  found; the next real step is more likely
  [[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]]'s territory than further
  per-bucket-call-count work. See Milestone Map entry above for full detail.

- **M1 (2026-07-16):** shared-SSBO + push-constant-shrink refactor, implemented against
  `SpecializedRecipeShaderGlsl.h` (shader emission) and all 3 dispatch-phase test harnesses
  (`test_recipe_bucketed_indirect_dispatch.cpp` M2, `test_recipe_multi_bucket_compositing.cpp` M3,
  `test_recipe_bucketing_perf.cpp` M4 — all 3 construct/consume the specialized shader's
  binding/push-constant contract directly, so all 3 needed updating in lockstep). Mechanism: (1)
  `BucketMembersBuffer` binds the bucketing pre-pass's OWN shared `bucketIndices[]` output
  directly (it was already one row-major buffer; the per-bucket CPU readback+slice+reupload loop
  was pure overhead, now eliminated). (2) new `BucketMetaBuffer` SSBO (32B/entry, indexed by
  recipeId) replaces `memberCount`/`rectMinX`/`rectMinY`/`boundRadius`/`stepRelaxation` in the
  push-constant block. (3) ONE shared `VkDescriptorSet` (not N) is bound on the first bucket's
  `DispatchPass` only; verified via Vulkan spec research ("Pipeline Layout Compatibility") that
  descriptor-set bindings persist across `vkCmdBindPipeline`/`vkCmdDispatchIndirect` as long as
  pipeline layouts stay compatible (true here — all N specialized pipelines share one
  `VkPipelineLayout`), and confirmed `MultiDispatchNode::RecordDispatches` already has an
  `if (!pass.descriptorSets.empty())` guard that skips the bind call when left empty — no
  `MultiDispatchNode` changes needed. **Measured, not assumed**: `vkCmdBindDescriptorSets` dropped
  N→1 at N=3/10/100 (real discrete RTX 3060 Laptop GPU); push-constant payload 92B→80B/call.
  **Correctness**: M2's oracle match (`matchedHits=1664`, `maxHitTDelta=0.00000`) and M3's
  overlap-compositing proof (0/65536 pixels differ between orderings) both hold unchanged under
  the refactor. Full regression suite (M0's `test_switch_cost_isolation` +
  `test_recipe_instance_bucketing`/`test_recipe_bucketed_indirect_dispatch`/
  `test_recipe_multi_bucket_compositing`/`test_recipe_bucketing_perf`) 16/16 passing. See
  Milestone Map entry above for full detail; M2 (barrier-coalescing) and M3 (re-measurement) are
  next.

---

## Tasks

### M0 — Isolate tier-0's N=100 knee root cause via randomized N/m_i/k_i stress test

**Task 1 — Randomized-recipe-corpus stress measurement.**
Build a standalone GPU test harness (mirror the existing switch-scaling measurement's methodology —
real discrete GPU, `PerfCsvWriter`-style steady FPS/frame-time capture) that generates a randomized
recipe corpus and instance population, sweeping THREE axes at once instead of two hand-picked
extremes:

- **N** — number of DISTINCT randomly-generated recipes (drives switch case-count / bucket count).
  Use N=3, N=10, N=100 to stay comparable with the existing 2026-07-10 switch-scaling table.
- **`m_i`** — each recipe `i`'s opcode STEP COUNT, randomized per-recipe within a documented range
  (e.g. 3-50 steps, pick and justify a range covering the corpus's real authored-recipe complexity —
  check `Vixen-Docs`/existing recipe corpus files for a realistic step-count distribution rather than
  guessing). This is the axis that actually stresses register pressure / instruction-cache pressure
  per switch-case, which the ORIGINAL M0 design (a clean binary "identical trivial vs. real differing"
  comparison) collapsed into a single flat choice — sweeping `m_i` randomly gives a much more
  realistic, continuous signal of whether code-size-per-case correlates with the knee.
- **`k_i`** — number of INSTANCES of recipe `i` rendered simultaneously in the same frame, randomized
  per-recipe (e.g. document a range/distribution — this need not be uniform; real scenes have skewed
  instance counts per recipe type). Total instance count = Σk_i across all N recipes. This is the
  bucketing/dispatch-count axis Increment 2/3 actually care about, deliberately included here so this
  measurement reflects a REALISTIC mixed scene, not an idealized one-instance-per-recipe setup.

Randomize the actual opcode content of each recipe's `m_i` steps too (not just the count) — pull from
the real, valid `SdfOpCode` set (see `RecipeStack.h`'s arity table for what's structurally valid to
chain), respecting real stack-arity rules so generated recipes are valid, renderable programs, not
just syntactically-plausible garbage. Document your randomization seed/methodology so the run is
reproducible.

**Render the FULL randomized scene through today's REAL tier-0 switch** (`UberShaderSplice.h`-shaped,
N cases, each case's body reflecting recipe `i`'s actual `m_i`-step content) and measure steady FPS/
frame-time — this is the primary signal. **Also render the same corpus's instance population with
EVERY recipe given IDENTICAL trivial bodies** (same `m_i`-per-recipe SHAPE preserved for fairness,
but every case's actual computation forced to the same trivial expression) as a control — this
isolates whether the knee tracks WITH real code-size/complexity or persists even when complexity is
flattened out, which is the actual thing this milestone needs to distinguish.

**The decision gate**: if the control (flattened-complexity) run ALSO knees similarly to the real
corpus at high N: root cause is switch/branch-dispatch-shaped (or Σk_i/instance-count-shaped,
consider both — this design's inclusion of `k_i` means you may find the knee tracks instance count
more than distinct-recipe count, which would be an important, separate finding from the original
question — report this explicitly if you see it) — proceed to M1/M2 as planned. If the control stays
flat/near-flat while the real-complexity corpus knees (and this tracks with `m_i`, not `k_i`): root
cause is code-size/register-pressure/icache-shaped, NOT switch-dispatch cost — **STOP here, do not
proceed to M1/M2 as planned**. Document this finding honestly in this plan's Progress Log and in
[[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]] (which explicitly gates its own
"single dispatch, no switch" premise on this exact result), then escalate to the user. If the knee
correlates with `k_i` (instance count / bucket count) rather than `N` or `m_i`: this is a DIFFERENT,
also-important finding — report it explicitly, since it would mean the real bottleneck is
INSTANCE-COUNT-shaped, which has direct implications for this increment's own dispatch-overhead
framing (M1/M2 target dispatch-count-per-bucket, which IS instance/bucket-count-shaped — a
confirmation of this correlation would directly support proceeding, while a pure-N or pure-`m_i`
correlation with no `k_i` sensitivity would argue against it).

**Gate**: FPS/frame-time captured across the swept (N, m_i-range, k_i-range) space, on the confirmed
discrete GPU, recorded in [[Perf-Ledger]] as a new "Switch-cost isolation (Inc3 M0, randomized N/m_i/
k_i stress)" section — include enough of the raw swept data (not just a summary conclusion) that a
future reader can see the actual correlation pattern, not just your interpretation of it. Real GPU,
validation layers on for correctness, separate timing run for the actual numbers (matching Inc2 M4's
own build-config discipline — note if this repo still has no Release CMake preset, per M4's finding,
both tables are necessarily Debug-build numbers, document this explicitly same as M4 did).

### M1 — Reduce per-bucket descriptor/push-constant overhead (conditional on M0)

**Task 2 — Shared SSBO instead of per-bucket descriptor-set aliasing.**
Only proceed if M0's Task 1 gate confirms this class of fix is worth pursuing (i.e., M0 didn't reveal
a disqualifying finding that makes this increment's whole framing moot). Today,
`SpecializedRecipeShaderGlsl.h:95-98`'s `BucketMembersBuffer` (binding 1) is aliased per-bucket,
requiring a distinct `vkCmdBindDescriptorSets` call per bucket. Investigate replacing this with ONE
shared SSBO covering all buckets' member-index data, with each dispatch's specific bucket-offset
passed via a cheaper mechanism (a single push-constant offset/index, or — if M2 makes this viable —
folded into the same per-dispatch data M2 already needs to touch). Similarly for the per-bucket
scalar state currently in `vkCmdPushConstants` (`rectMinX`/`rectMinY`/`memberCount`/`boundRadius`/
`stepRelaxation`, `SpecializedRecipeShaderGlsl.h:115-131`) — investigate whether this can be hoisted
into the same shared-buffer-plus-offset scheme rather than a distinct push-constant call per bucket.

**Gate**: correctness proven against the EXISTING M2/M3 oracle pattern (CPU-independent oracle,
whole-screen `HitRecord` comparison) — confirm the refactored data-passing scheme produces
byte-identical results to the pre-M1 mechanism for the same synthetic scenes M2/M3 already used, not
just "it renders something." Measure the API-call-count reduction directly (log/count actual Vulkan
calls issued per bucket before/after) before claiming a reduction, don't just assume the refactor
worked.

### M2 — Barrier-coalescing correctness analysis + (if safe) reduction

**Task 3 — Prove (not assume) whether fewer barriers preserve M3's ordering guarantee.**
`MultiDispatchNode::InsertAutoBarrier` (`MultiDispatchNode.cpp:671-687`) issues one `VkMemoryBarrier2`
(SHADER_WRITE→SHADER_READ|WRITE) between every adjacent bucket pair — N−1 total for N buckets. M3's
correctness proof (order-independence, 0/65536 px differ across dispatch orderings) depends on a
write-after-write hazard being enforced between EVERY sequential pair of dispatches touching
`HitRecord`. Before touching this: **first establish, via careful reasoning about Vulkan's memory
model (not assumption), whether the barrier count can be reduced while preserving that guarantee** —
e.g., is a barrier only actually REQUIRED before dispatches whose bound-sphere regions overlap in
screen space (M1's coverage data could identify non-overlapping bucket pairs, which plain
read-compare-write correctness doesn't need serialized against each other at all), or does Vulkan's
memory model require the full N−1 chain regardless of spatial overlap because of some other hazard
class (e.g. cache coherency across the whole `HitRecord` buffer, not just the specific pixels a bucket
touches)? Document the reasoning explicitly — this is exactly the kind of "confirm, don't assume"
gate the design doc's Risks section already established a precedent for in M3's own ordering-proof.

**If a safe reduction is established**: implement it, with a gate that reproduces M3's EXACT
order-independence proof (both dispatch orderings, byte-for-byte `HitRecord` comparison) on the
reduced-barrier scheme, not a weaker check.

**If no safe reduction is established** (i.e., the N−1 barriers turn out to be genuinely required):
document this explicitly as a real finding — this task's "gate" in that case is a clear, honest
explanation of WHY, not a forced optimization. Move to M3 with M1's improvement (if any) as this
increment's actual contribution.

### M3 — Re-measurement + honest doc closure

**Task 4 — Full re-measurement against the M4 baseline.**
Real discrete GPU (confirmed via the same device-name-logging discipline M4 established), re-run the
EXACT same comparison M4 did (bucketed-dispatch vs. tier-0-switch-only, same N=3/10/100 values) after
M1/M2's changes. **Record honestly** — if the changes improved the ratio but bucketed dispatch is
still slower than tier-0, say so plainly; if M0's gate stopped M1/M2 from proceeding, this milestone's
job becomes purely closing out M0's finding rather than a fix. Update
[[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] and this plan's own Milestone Map/Progress
Log with the final, honest state — including an explicit statement of what (if anything) remains
open for a hypothetical Increment 4, informed by M0's finding (if code-size/register-pressure-shaped,
the real next step is likely closer to
[[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]]'s territory than more per-bucket-call
reduction).

---

## Risks / decision points

- **M0 is a genuine go/no-go gate, not a formality.** Do not let implementation momentum carry M1/M2
  forward if M0's result disqualifies this increment's framing — the whole point of scoping M0 first
  (per explicit user decision) was to avoid investing in a fix for the wrong root cause.
- **`vkCmdBindPipeline` is a hard, out-of-scope ceiling for this increment.** Any milestone that finds
  itself trying to work around per-pipeline binding is scope-creeping into the single-dispatch
  direction doc's territory — stop and flag it rather than continuing.
- **Barrier-coalescing (M2) is correctness-critical, not a pure perf task.** A wrong reduction here
  would silently reintroduce the exact race-condition risk M3 spent real effort proving didn't exist
  — treat any barrier-count change with M3-level scrutiny (independent validator re-derivation of the
  ordering proof), not a lighter touch just because it's "only removing something."
- **Honesty requirement carries forward from Inc2 M4.** If M1/M2 don't actually improve the ratio, or
  M0 kills the increment's premise, report that plainly — this plan's own Progress Log already
  models the discipline of writing down a real, unflattering grounding fact (the still-open switch-
  cost question) rather than assuming it away.
