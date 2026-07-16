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
  - [x] **DONE 2026-07-16.** Finding: MIXED m_i+k_i correlation, N (switch-case count) ruled OUT
    as an independent driver — see Perf-Ledger.md "Switch-cost isolation (Inc3 M0...)" for full
    swept data + axis-decoupling proof. Decision: M1/M2 as currently scoped (per-bucket dispatch-
    COUNT overhead) do NOT address what this measurement found actually drives tier-0's own N=100
    knee (m_i/k_i-shaped, not switch-dispatch-shaped) — flagged to controller/user for a scope
    decision before M1/M2 proceed, NOT unilaterally started or skipped by this milestone.
- **M1 — Reduce per-bucket descriptor/push-constant overhead** (Task 2) · **live-run gate** · shared
  SSBO + per-dispatch offset/index replaces per-bucket descriptor-set aliasing where data doesn't
  genuinely require a distinct set; correctness proven against the existing M2/M3 oracle pattern.
  - [ ] Not started. **Conditional on M0** — do not start until M0's finding is in and confirms this
    class of fix is worth pursuing (see M0's Task 1 decision gate).
- **M2 — Barrier-coalescing correctness analysis + (if safe) reduction** (Task 3) · **live-run gate**
  · prove whether N−1 barriers can become fewer without weakening M3's write-after-write ordering
  proof; implement ONLY if a safe reduction is actually established, not assumed.
  - [ ] Not started.
- **M3 — Re-measurement + honest doc closure** (Task 4) · **live-run gate, discrete GPU mandatory** ·
  full M4-equivalent perf comparison re-run after M1/M2's changes, honestly reported (including a
  "still slower" outcome if that's what the data shows), plan/epic doc closure.
  - [ ] Not started.

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
  (Inc3 M0...)" section. Per the plan doc's explicit scope, M1/M2 were NOT started — this finding
  is handed to the controller/user: M1/M2 as currently framed (per-bucket dispatch-COUNT overhead)
  target a switch-case-count-shaped cost this measurement found is NOT what's driving tier-0's own
  N=100 knee; the knee is m_i/k_i-shaped, pointing more toward
  [[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]]'s single-dispatch-no-switch
  territory than toward reducing bucket-count overhead.

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
