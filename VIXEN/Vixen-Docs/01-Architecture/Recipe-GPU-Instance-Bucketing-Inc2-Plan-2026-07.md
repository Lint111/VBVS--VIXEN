# Recipe GPU Instance Bucketing — Increment 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: use the post-brainstorm-context-manager pipeline to
> implement this plan milestone-by-milestone (fresh implementer + Opus validator per milestone,
> worktree-isolated, progress persisted in this doc; pre-bless the in-tree destructive/git tier at
> setup). **Live-run gates are authoritative for EVERY milestone in this plan** — unlike
> [[Recipe-Pipeline-Cache-Inc1-Plan-2026-07]] (pure CPU infra), nothing here is CPU-only; every
> milestone touches shaders, dispatch, or GPU synchronization and must end in an actual `VIXEN.exe`
> run with validation layers explicitly enabled (Windows-native, real GPU — this project's standing
> policy, `vixen-build-policy` skill). Never overlap two builds of one target. Watch long builds
> with a foreground polling loop, not a blind wait.

**Goal:** Ship the mechanism that lets a hot SDF recipe render through its own specialized compute
pipeline instead of the shared tier-0 `switch(recipeId)` uber-shader, at a scale the tier-0 switch
cannot reach (measured collapse at N≈100, hard hang at N≈500 — see
[[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] §8). This is **Increment 2** of that epic
— the design doc [[Recipe-GPU-Instance-Bucketing-Design-2026-07]] resolved all 4 open architecture
questions (2026-07-15); this plan turns that resolved design into working code. **We are not
re-litigating whether the current tier-0-only setup is viable at scale — it isn't, that's already
measured — this plan proceeds directly to building the alternative.**

**What "done" looks like for this increment:** instances get bucketed by `recipeId` in a GPU
pre-pass; each bucket's screen-space coverage is estimated from its members' bound spheres; hot
buckets get their own specialized single-recipe pipeline dispatched via `vkCmdDispatchIndirect`
sized to their coverage; cold buckets fall back to the existing tier-0 switch path unchanged;
cross-bucket pixel ownership resolves correctly via sequential dispatch + plain read-compare-write
against the existing `HitRecord` SSBO. **What this increment does NOT ship**: the async
background-compile half of the original "tier-1 promotion" sketch (a hot recipe's specialized
pipeline is compiled SYNCHRONOUSLY when it's promoted in this increment — async compile-and-swap is
a follow-on, layered on top of working bucketed dispatch, once this increment proves the routing
mechanism itself is sound and worth the async investment); GPU-LRU eviction (Increment 3); family
normalization (Increment 4).

**Depends on (shipped):** [[Recipe-Pipeline-Cache-Inc1-Plan-2026-07]] (Increment 1, content-hash
cache, merged main `bf8dfbf5`) — not a hard technical dependency for THIS increment's `recipeId`-
level bucketing (deliberately decoupled from family-hash, see design doc §3.1), but the natural
upgrade path once Increment 4 exists. [[Recipe-Parameterization-Plan-2026-07]] (P4, shipped, merged
`fb3a577c`) — a specialized pipeline still needs to read `recipeParams[]` correctly.

**Design of record:** [[Recipe-GPU-Instance-Bucketing-Design-2026-07]] — read it in full before
starting; every task below cites the specific §3.x section it implements. All 4 architecture
questions (view-proj gap, cross-bucket compositing, bucketing granularity, hotness-gating shape)
are RESOLVED there; this plan does not re-derive them, it implements the resolved decisions.

**Tech Stack:** C++23, GLSL compute (runtime-compiled via glslang), Vulkan 1.3 (indirect dispatch,
`vkCmdDispatchIndirect` — new to this codebase, confirmed zero prior usage), GoogleTest, CMake
ninja/wsl presets + Windows `.bat` builds, real GPU (Windows-native) for every milestone.

---

## §0. Scope

**In scope:**
- `CameraNode`'s new `CURRENT_VIEW_PROJ` output (design §4 item 1) — the view-proj matrix gap fix.
- A GPU compute pre-pass that buckets `bodyInstances[]` by `recipeId`, producing per-bucket
  compacted instance-index lists (design §3.1).
- Per-bucket screen-space coverage estimation from projected bound spheres (design §3.2).
- `DispatchPass`/`MultiDispatchNode` extension to accept an indirect dispatch buffer instead of
  fixed CPU-known dimensions (design §3.3's prerequisite; the buffer/barrier plumbing this needs —
  `ResourceUsage::IndirectBuffer`, `AccessKind::IndirectRead` — already exists per the design doc's
  §2 survey, this is the local per-call-site wiring).
- Per-recipe specialized single-recipe shader emission (NOT the switch — reuse
  `EmitProceduralFieldFunctionGlsl` per-recipe, synchronous compile via the existing
  `ComputePipelineCacher`/`ComputePipelineNode` pattern, no async yet per §0's explicit non-goal).
- Sequential bucket dispatch with the plain read-compare-write `HitRecord` compositing scheme
  (design §3.3, resolved — relies on `MultiDispatchNode`'s existing default `autoBarriers_`
  behavior, no new synchronization primitive).
- A minimal, hard-coded-for-now hotness signal sufficient to prove the mechanism (e.g. "always
  promote every distinct recipeId with ≥1 instance this frame" is an acceptable placeholder for
  THIS increment — a real hot-mark/usage-history tracker is legitimately part of the deferred async
  layer, not required to prove bucketed dispatch itself works; but see Task 6/M3 for the
  minimum-viable hotness gate this increment DOES need to avoid promoting every single-instance
  recipe, which would defeat the fixed-overhead-per-bucket concern the design doc flagged).
- A measured performance comparison (bucketed-dispatch path vs. tier-0 switch) at increasing N, on
  real GPU — this IS the empirical validation the design doc's §4 item 6 called for, folded into
  this plan's own milestones rather than a separate pre-plan spike, per explicit instruction.

**Out of scope:** async background compile-and-swap (a genuine follow-on increment, not this one);
GPU-LRU eviction; family-hash bucketing (Increment 4's job, `recipeId`-level only here per the
design's resolved decision); any change to `SdfRecipeCodegenGlsl.h`'s ReadParam/ReadParamFloat3
emit logic (Recipe-Parameterization already shipped this, reused as-is); any change to
`RecipeContentCacher` (Increment 1, untouched — this increment doesn't consume its family data, per
the granularity decision).

---

## Milestone Map

- **M1 — View-proj plumbing + bucketing pre-pass (bucket, don't dispatch yet)** (Tasks 1-3) ·
  **live-run gate, validation layers mandatory** · `CURRENT_VIEW_PROJ` output exists and is
  correct; a compute pre-pass buckets `bodyInstances[]` by `recipeId` into per-bucket compacted
  instance-index SSBOs; per-bucket screen-space coverage is computed and readable back for
  verification. Nothing routes to a specialized pipeline yet — this milestone proves bucketing
  alone is correct, isolated from dispatch/compositing risk.
  - [ ] Not started.
- **M2 — Indirect dispatch plumbing + specialized pipeline compile** (Tasks 4-5) · **live-run
  gate** · `DispatchPass` accepts an indirect buffer; a per-recipe specialized shader (no switch)
  emits and compiles synchronously through the existing `ComputePipelineCacher`; a SINGLE hot
  recipe's bucket dispatches via `vkCmdDispatchIndirect` against its own pipeline, with everything
  else still on the tier-0 switch. Proves the indirect-dispatch + specialized-pipeline mechanism in
  isolation, one recipe at a time, before enabling it for many.
  - [ ] Not started.
- **M3 — Cross-bucket compositing + multi-recipe routing + minimal hotness gate** (Tasks 6-8) ·
  **live-run gate** · MULTIPLE hot recipes each get their own bucket+pipeline+indirect-dispatch,
  sequential dispatch order via `MultiDispatchNode`'s default barrier behavior, `HitRecord`
  read-compare-write compositing proven correct under real screen-space overlap (two recipes whose
  bound spheres visibly overlap on screen, confirm nearest-hit wins); a minimal hotness gate
  (placeholder policy, not a real usage-history tracker) prevents promoting every single-instance
  recipe.
  - [x] DONE — commit `9770c211`. Opus validator: **APPROVED**.
- **M4 — Performance validation + no-regression sweep + doc closure** (Tasks 9-10) · **live-run
  gate, DISCRETE GPU mandatory (see scoping note)** · measured FPS/frame-time comparison
  (bucketed-dispatch path vs. tier-0-switch-only) at increasing hot-recipe counts on real GPU,
  recorded in [[Perf-Ledger]] alongside the existing switch-scaling table; full no-regression sweep;
  JIT direction doc's Increment 2 status flipped to shipped (or, if the measurement shows the
  mechanism ISN'T actually a win yet, documented honestly as a real, load-bearing finding — see Task
  9's explicit instruction not to force a positive result).
  - [ ] Not started. Scoped 2026-07-16: standalone perf-harness extension of M1-M3's proven test
    pattern (no live-app integration this increment); must fix the inherited GPU-selection gap
    (force discrete NVIDIA GPU) before capturing any numbers.

### Progress Log

(populated as milestones complete — one entry per milestone: commit hash, gate evidence, Opus
validator verdict; follow the Recipe-Parameterization / Recipe-Pipeline-Cache plans' convention.)

- Milestone 1 (Tasks 1-3): DONE · commit `1530b5a2` · Opus validator APPROVED · 2026-07-16.
- Milestone 2 (Tasks 4-5): DONE · commits `b2d45afc`..`0a1ff8fe` (fix: reject-sphere center must use
  registered `boundCenter`, not `worldPos`) · Opus validator APPROVED after one fix round · 2026-07-16.
- Milestone 3 (Tasks 6-8): DONE · commit `9770c211` · Opus validator APPROVED · 2026-07-16. Task 8
  overlap gate independently re-derived by the validator from bound-sphere geometry in Python
  (1131 overlap px, A wins all, 2984 total hits) — exact match to the GPU result, not taken on
  faith. Order-independence empirically proven (0/65536 px differ between dispatch orderings). Two
  real bugs found+fixed in the test harness's own device/pipeline setup (wrong `VkPipelineLayout`
  from `ComputePipelineCacher`'s fallback path; null `fpCmdPipelineBarrier2` from a hand-built
  `VkDevice` bypassing `VulkanDevice::CreateDevice()`'s function-pointer resolution) — both scoped to
  the test harness, not production code. **Scoped deviation, not a blocker**: M3 proves the
  mechanism in a standalone GPU harness; bucketed dispatch is NOT wired into the live `VIXEN.exe`
  render graph yet (the real tier-0 shader `BodyInstanceRayMarch.comp` has no mechanism to exclude
  hot/bucketed instances and writes `HitRecord` unconditionally) — live-app integration is deferred
  to a future increment, documented in the test file header + commit message. **Also newly noted**:
  this milestone's test hand-selects its physical device (`PickPhysicalDevice()`) with no
  discrete-vs-integrated preference — same bug class as the already-shipped `DeviceNode` fix (main
  `0ee32428`) but never applied to this test's own device-selection code. Validator ran on the AMD
  integrated GPU as a result; not a correctness blocker for M3 (mechanism/compositing proof is
  GPU-vendor-agnostic), but flagged for M4 since M4 IS a performance measurement and needs to run on
  the discrete NVIDIA GPU to be representative.

---

## Tasks

### M1 — View-proj plumbing + bucketing pre-pass

**Task 1 — `CURRENT_VIEW_PROJ` output on `CameraNode`.**
Mirror `PREV_VIEW_PROJ`'s exact existing pattern in `libraries/RenderGraph/include/Data/Nodes/
CameraNodeConfig.h`: `CameraNodeCounts::OUTPUTS` 2→3; a new `OUTPUT_SLOT(CURRENT_VIEW_PROJ, const
glm::mat4&, 2, ...)` (line ~74's sibling); a new `INIT_OUTPUT_DESC(CURRENT_VIEW_PROJ, ...)` (line
~141's sibling); update the two `static_assert`s that pin `PREV_VIEW_PROJ_Slot::index`/`Type` with
matching ones for the new slot. In `CameraNode.cpp`, at both existing `prevViewProj = projection *
view;` sites (`:193`, `:377`), also emit the CURRENT frame's `projection*view` (the multiply
already happens — this is exposing the value under the new slot, not new computation) via
`ctx.Out(CameraNodeConfig::CURRENT_VIEW_PROJ, ...)`. Confirm no existing consumer of `CameraNode`'s
output count/slot indices breaks (grep every `ctx.In(CameraNodeConfig::` call site before changing
`OUTPUTS`).

**Task 2 — Bucketing compute pre-pass.**
New shader (e.g. `shaders/RecipeInstanceBucketing.comp`) + a new RenderGraph node (or extend an
existing dispatch node — decide based on where `bodyInstances[]`/binding-10 and the new
`CURRENT_VIEW_PROJ` are both naturally in scope; likely a new dedicated node given this is a
distinct pre-pass, not a modification of `ComputeDispatchNode`'s existing responsibility). One
thread per instance (NOT per pixel — this pass is instance-shaped, unlike the existing march). Per
design §3.1: bucket by exact `recipeId` (read `BodyInstanceGpu.recipeId`, binding 10). Output:
per-recipeId compacted instance-index lists via an atomic-counter-driven append (mirror the
`atomicAdd` pattern from `shaders/ShaderCounters.glsl` — a per-bucket atomic counter + indexed
write, the classic GPU compaction idiom). Bucket count/capacity: decide a reasonable upper bound
for distinct live recipeIds per frame (the epic's own target is 1000+ instances, NOT necessarily
1000+ DISTINCT recipeIds simultaneously hot — confirm this distinction explicitly and size buffers
for a sane distinct-recipe ceiling, not the instance ceiling; if unsure, 256 distinct buckets is a
reasonable starting cap, document the choice).

**Task 3 — Per-bucket screen-space coverage.**
Per design §3.2: for each bucket, project member instances' `getRecipeBoundSphere` world-space
spheres to screen space using the new `CURRENT_VIEW_PROJ` matrix, union across the bucket's
members into a conservative screen-space bounding rect (or tile range — decide the exact
representation; a pixel-space AABB is the simplest starting point, tile-based can be a later
refinement if M4's perf data shows AABB granularity is too coarse). Write this per-bucket coverage
to a small SSBO. **Gate for this milestone**: a CPU-side readback test (or debug capture) proving
bucket membership and coverage are computed correctly for a known synthetic scene (e.g. 3 distinct
recipeIds, N instances each, verify bucket contents and a hand-computed expected screen coverage
match) — this milestone does NOT yet dispatch anything using this data, it only proves the data is
right, isolating correctness risk from the dispatch/compositing risk M2/M3 add.

### M2 — Indirect dispatch plumbing + specialized pipeline compile

**Task 4 — `DispatchPass` indirect-buffer support.**
`libraries/RenderGraph/include/Data/DispatchPass.h`: add an `std::optional<VkBuffer>
indirectBuffer` (+ offset if needed) alongside the existing fixed `workGroupCount`. Update
`MultiDispatchNode`'s dispatch-recording call sites (`MultiDispatchNode.cpp:518-521,614-616` and
similar) to branch: if `indirectBuffer` is set, call `vkCmdDispatchIndirect` (the FIRST use of this
API in the codebase — confirm the `VkDispatchIndirectCommand`-shaped buffer content contract
exactly, per the Vulkan spec: 3×uint32 x/y/z workgroup counts, no header); else the existing fixed
`vkCmdDispatch` path, byte-identical to today (this must be a strictly additive change — every
existing `MultiDispatchNode` caller with no `indirectBuffer` set must be unaffected, confirm via
the existing `MultiDispatchNode` test suite staying green). Wire M1's per-bucket coverage SSBO into
a `VkDispatchIndirectCommand`-shaped buffer (a small transform pass, or compute the indirect
command directly in the bucketing pass's own output format if simpler — decide).

**Task 5 — Per-recipe specialized pipeline (synchronous compile).**
For exactly ONE designated "hot" recipe (hardcode/pick one for this milestone — the minimal
hotness gate is M3's job, not this one): emit a specialized single-recipe GLSL shader (reuse
`EmitProceduralFieldFunctionGlsl` for just that one recipe's bytecode — NOT the multi-recipe
switch `SpliceProceduralRecipesIntoSource` path; this is a smaller, simpler compile, one recipe's
field function + a `main()` that calls it directly, no switch dispatch overhead), compile it
SYNCHRONOUSLY through the existing `ComputePipelineCacher`/`ComputePipelineNode` pattern (mirror
`ComputePipelineNode.cpp:261-282`'s hash-the-SPIR-V cache-key convention). Dispatch this recipe's
bucket via `vkCmdDispatchIndirect` (Task 4) against its own pipeline, sized to its M1/M3 coverage.
**Gate**: this one recipe renders correctly via its OWN pipeline (visually/via hit-buffer readback
matching what the tier-0 switch would have produced for the same recipe+instances), while every
OTHER recipe still renders via the unmodified tier-0 switch in the same frame. This is the
"proves the mechanism once, in isolation" milestone — no compositing-under-overlap risk yet since
only one recipe is bucketed-dispatched.

### M3 — Cross-bucket compositing + multi-recipe routing + minimal hotness gate

**Task 6 — Minimal hotness gate.**
A placeholder policy sufficient to prevent promoting every single-instance recipe to its own
pipeline (which would multiply per-bucket fixed overhead pointlessly) without building the real
async usage-history tracker (deferred, out of scope). Simplest defensible policy: promote any
`recipeId` whose bucket (from M1's pre-pass) has ≥ some instance-count threshold this frame (e.g.
≥4 instances — pick and document a number, it doesn't need to be tuned yet, just non-trivial).
Document explicitly that this is a placeholder, not the real hot-mark mechanism, and will be
replaced when the async layer is built.

**Task 7 — Multi-recipe sequential bucketed dispatch.**
Extend M2's single-recipe proof to N hot recipes simultaneously (per Task 6's gate) — each gets its
own specialized pipeline (Task 5's compile path, now looped) and its own indirect dispatch (Task
4's plumbing), all issued through `MultiDispatchNode` with its DEFAULT `autoBarriers_` behavior
(confirm this is NOT disabled anywhere in this milestone's wiring — per the design doc, this
default is exactly what makes the next task's compositing correct with no atomics). Cold recipes
(below Task 6's threshold) continue through the existing tier-0 switch dispatch, unchanged,
alongside the new bucketed dispatches in the same frame.

**Task 8 — Cross-bucket `HitRecord` compositing.**
Per design §3.3 (resolved): each specialized-pipeline bucket's shader does a plain (non-atomic)
`if (myHitT < hitRecords[idx].hitT) hitRecords[idx] = myRecord;` against the existing `HitRecord`
SSBO (binding 18) instead of the current unconditional overwrite the tier-0 switch path uses (the
tier-0 path's own write may need the SAME conditional-write treatment now that OTHER passes might
have already written a closer hit for some pixels this frame — check ordering: does the tier-0
switch dispatch run BEFORE or AFTER the bucketed dispatches, and does that ordering matter for
correctness given the read-compare-write scheme is symmetric regardless of order, since "nearest
wins" is order-independent by construction — confirm this reasoning holds, don't assume). **Gate
(the most important test in this whole plan)**: construct a synthetic scene with 2+ hot recipes
whose bound spheres VISIBLY overlap on screen from the test camera angle; confirm the correct
nearest-hit recipe wins per-pixel in the overlap region (compare against a reference: what the
UNMODIFIED tier-0 switch would produce for the identical scene — byte-identical or
tolerance-bounded pixel comparison). This is the design doc's flagged "single most important
remaining design question," now built — do not skip or weaken this gate.

### M4 — Performance validation + no-regression sweep + doc closure

**Scoping note (added post-M3, 2026-07-16):** M1-M3 deliberately proved the bucketing/dispatch/
compositing mechanism in a standalone GTest harness (hand-built `VkInstance`/`VkDevice`, mirroring
`test_hitrecord_readback.cpp`'s precedent), NOT wired into `VIXEN.exe`'s live render graph — see
M3's Progress Log entry. Wiring live-app integration (`BuildRenderGraph.cpp`, replacing/augmenting
the real tier-0 `BodyInstanceRayMarch.comp` dispatch) is out of proportion for this milestone and is
deferred to a future increment; M4 stays consistent with M1-M3's own approach and extends the SAME
standalone-harness pattern into a perf-measurement harness, instrumented the same way the existing
switch-scaling table was captured (steady FPS, `cpu_frame_time_ms`, boot/steady bytes via the
`PerfCsvWriter` convention — see [[Perf-Ledger]] "Switch-scaling measurement" table, M5, 2026-07-10).
The existing tier-0-switch-only baseline (N=3/10/100/500, already captured in that table on real GPU)
does NOT need to be re-captured — Task 9 only needs to capture THIS increment's bucketed-dispatch
numbers at the same N values for a direct side-by-side.

**Also carried forward from M3 (real, non-blocking finding — fix as part of M4, not deferred
further):** M1-M3's test harnesses hand-select their Vulkan physical device
(`PickPhysicalDevice()`/equivalent) with NO discrete-vs-integrated preference — same bug class as
the already-shipped `DeviceNode::SelectPhysicalDevice()` fix (main `0ee32428`, prefers the first
`VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU`) but never applied to this hand-rolled test pattern. M1-M3
ran on whichever GPU enumerated first (confirmed AMD integrated on this machine) — harmless for
correctness/mechanism proofs, but M4 is a PERFORMANCE measurement and must run on the discrete
NVIDIA GPU to be representative and comparable against the existing switch-scaling table (which was
captured on `AMD Radeon` per its own header — note this discrepancy explicitly when reporting M4's
numbers: if the original switch-scaling baseline was ALSO captured on an integrated/different GPU
than M4's bucketed-dispatch run, the comparison needs the SAME GPU for both sides, re-capturing the
tier-0 baseline on this milestone's chosen GPU if the vendor/class differs from the original
capture). **Task 9's harness setup must fix `PickPhysicalDevice()` (or add an explicit override) to
force discrete-GPU selection before capturing any numbers**, and record which physical device
(`vkGetPhysicalDeviceProperties.deviceName`) was actually used for every number reported.

**Task 9 — Performance measurement.**
Real GPU (Windows-native), validation layers on for correctness confirmation, then a SEPARATE
release/perf-focused run for timing (matching the existing switch-scaling measurement's own
methodology, see [[Perf-Ledger]] "Switch-scaling measurement" table format). Fix the GPU-selection
gap above FIRST, before capturing any numbers. Measure steady-state FPS/frame-time for: (a) the
EXISTING tier-0-switch-only path at increasing distinct-recipe counts — reuse the existing
N=3/10/100 data ONLY IF it was captured on the same GPU class (discrete) as this milestone's run;
otherwise re-capture on the same discrete GPU to keep the comparison apples-to-apples (this is a
new consideration this milestone must resolve, not assume away); (b) THIS increment's
bucketed-dispatch path at the same N values, with a realistic mix of hot/cold recipes per Task 6's
threshold, using the standalone-harness pattern (not live-app integration, per the scoping note
above). **Record the result honestly** — if bucketed dispatch is NOT actually faster at the tested N
(e.g. the sequential-dispatch barrier overhead per bucket dominates at low N, and the win only
appears at higher N, or doesn't appear at all in this synchronous-compile-only increment), document
that as a real, load-bearing finding, not something to paper over. The epic's own justification (§8)
was for N≥100; if this increment's mechanism doesn't show a win until async compile removes the
promotion-latency cost, say so explicitly — that's valuable information for sequencing the async
follow-on, not a failure of this increment (which was scoped to prove the ROUTING mechanism, not to
be the fully-optimized end state).

**Task 10 — No-regression sweep + doc closure.**
Full recipe/SVO/RenderGraph test suite, confirm zero regressions beyond whatever baseline is
established by this point (check the most recent merge's confirmed-pre-existing failure list before
assuming any new failure is unrelated — do not skip this verification; KI-034's push-constant-
mismatch class and the pre-existing view/hud codegen drift-guard failures the M3 validator confirmed
are both expected background noise, not something to re-investigate). Update
[[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] §7 Increment 2's status to shipped (with
the honest performance finding from Task 9, whatever it turns out to be) and this plan doc's own
Milestone Map/Progress Log.

---

## Risks / decision points

- **Distinct-recipe bucket capacity (Task 2).** Confirm the 1000+ instance target doesn't imply
  1000+ SIMULTANEOUSLY-DISTINCT recipeIds — size bucket-count structures for a realistic distinct-
  recipe ceiling, not the raw instance ceiling, and document the chosen cap.
- **Synchronous compile latency (M2/M3, explicitly deferred).** Every specialized pipeline in this
  increment compiles ON THE CRITICAL PATH (no async yet) — this WILL cause a visible hitch the
  first time a recipe is promoted. This is a KNOWN, ACCEPTED limitation of this increment (the async
  layer is explicitly out of scope, follows once routing is proven) — do not let a validator flag
  this as a defect; it's a scoped, documented gap, not a bug.
- **Task 8's ordering-independence claim.** The plan asserts nearest-hit-wins compositing is
  order-independent given the read-compare-write scheme — this needs to be CONFIRMED, not assumed,
  during implementation (a subtle bug here would silently produce wrong pixels only under specific
  dispatch orderings, which could pass a single-order test and fail in practice if ordering isn't
  actually guaranteed the way assumed).
- **Task 9's honesty requirement.** This plan explicitly anticipates the possibility that this
  increment alone (without async compile) may not show a clear win yet — this is a legitimate,
  useful outcome to report, not something to route around by cherry-picking favorable N values or
  skipping the comparison.
