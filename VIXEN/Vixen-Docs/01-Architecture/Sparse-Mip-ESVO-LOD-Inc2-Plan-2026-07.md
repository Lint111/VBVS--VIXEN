# Sparse-Mip ESVO LOD — Inc2 Plan (2026-07)

**Status:** PLANNED, not started. Sequenced directly after [[Sparse-Mip-ESVO-LOD-Inc1-Plan-2026-07]]
(★ INC1 COMPLETE ★, merged to `main` 2026-07-06). This doc collects and scopes the Inc2 backlog that
Inc1's own validators flagged rather than blocked on — nothing here is new invention; every milestone
below traces to a specific caveat, deferred decision, or candidate note already recorded in the Inc1
plan or its parent direction doc.

## Goal

Close the gaps Inc1 explicitly left open: (1) replace the "extreme endpoint" bandwidth claim with a
realistic mixed-scene measurement, (2) harden a known-flaky test assertion, (3) build the CPU-side
occlusion gate Inc1 deferred, and (4) evaluate whether GigaVoxels' per-brick GPU-driven request/LRU
mechanism is warranted yet, or should itself wait for the nested-tree epic.

## Where each milestone comes from

- **M1 (mixed-scene bandwidth measurement)** — Inc1 Plan §M5 Progress Log, "Caveat A": the measured
  170-220x number is real but is the mechanism's all-or-nothing extreme endpoint (16 trees all-resident
  vs. all-mip-only), not the realistic mixed near/far operating point the parent direction doc's
  original framing described. Validator's explicit recommendation: "measure one mixed scene through the
  actual live trigger before quoting 170-220x as the realistic-scene number."
- **M2 (flaky-assertion hardening)** — Inc1 Plan §M5 Progress Log, "Caveat B": `test_partial_brick_upload`'s
  `EXPECT_EQ(totalUploads, +1)` assertion is genuinely timing-sensitive against the real async
  brick-upload/config-reupload state machine (0/38 failures in isolated re-testing, but a real rare
  flake under heavy concurrent-sweep contention). Tracked as "harden the assertion or accept the rare
  flake," not a blocker.
- **M3 (CPU-side residency occlusion gate)** — Inc1 Plan §M4b, explicitly deferred: "for each
  frustum-passing, resolvable candidate, test its bounding-volume center against a coarse depth estimate
  built from already brick-resident trees only... Decide at implementation time whether this ships in
  Inc1: if M5's bandwidth measurement shows frustum+resolvability alone deliver the claimed win... defer
  to Inc2." M5's measurement scenes had no heavily-occluding geometry, so this was deferred as planned —
  now in scope.
- **M4 (GigaVoxels request-buffer/GPU-LRU evaluation)** — Inc1 Plan's "Inc2 candidate" note (its own
  §"Prior art"): adopt the flat request-buffer + timestamp self-dedup + GPU-side LRU mechanism from
  Crassin's PhD thesis (Ch. 7), replacing Inc1's per-tree CPU formula with per-brick, ray-observed
  demand. Inc1's own note is explicit that this is motivated by **large partially-occluded/partially-visible
  single bodies** (a planet-scale tree), i.e. a natural companion to the nested-tree epic
  ([[Tiered-ESVO-Observer-Addressing-Design-2026-07]]), and instructs: "Decide whether to build this once
  M5's bandwidth measurement is in and nested-tree work is actually scheduled, not before." M5 is in;
  nested-tree work is not yet scheduled. M4 below is therefore an **evaluation milestone**, not a
  build-it milestone — its gate is a go/no-build decision with evidence, matching the Inc1 note's own
  instruction not to build this speculatively.

## Non-goals (this increment)

- Nested trees / tree-of-trees / `TierRef` / cross-tree traversal restart — still
  [[Tiered-ESVO-Observer-Addressing-Design-2026-07]]'s scope, still downstream of this.
- The 2024 paper's CUDA-Dynamic-Parallelism scheduling fix — genuine Vulkan-portability problem, only
  worth evaluating if VIXEN's own render/production structure shows the same tail-regime stalling that
  fix targets, which nothing so far has shown.
- Fractional-LOD lerp between adjacent mip levels, clone-aware/content-hash dedup, undertow-side
  reification — all still out of scope per Inc1 §0, unchanged here.
- Building M4's GPU-LRU mechanism outright — see M4's framing above; this increment evaluates, a later
  one (gated on the nested-tree epic actually being scheduled) would build it.

---

## Milestone Map

- **M1 — Mixed-scene bandwidth measurement** · gate: a new or extended `test_bandwidth_ab_measurement`
  scenario with a realistic near/far body mix (not all-16-resident vs. all-16-mip-only), driven through
  the actual live `ResidencyTrigger` (not the harness's manual `RequestBrickResidency` toggle) so the
  measurement reflects the real per-frame decision path, not an idealized bound. Update the direction
  doc's status banner to report both numbers (extreme-endpoint 170-220x, realistic-mix N%) so future
  readers don't over-read the endpoint as typical. ·
  **✅ DONE 2026-07-06** — commit `84d1731f`, Opus-validated APPROVED.
- **M2 — Harden `test_partial_brick_upload`'s flaky assertion** · gate: root-cause the exact race between
  the brick-upload completion poll and the config-reupload completion poll (Inc1's Progress Log already
  narrows it to `PollBrickUploadCompletion`'s phase-2 count), then either fix the assertion to tolerate
  the legitimate interleaving or fix a genuine state-machine bug if one is found — proven via a stress
  run (≥50 iterations under concurrent load, matching the validator's own reproduction methodology) with
  zero failures after the fix, vs. a captured pre-fix failure for comparison. ·
  **✅ DONE 2026-07-06** — test-only fix, production code untouched; 134/134 stress runs green
  post-fix (see M2 Progress Log below).
- **M3 — CPU-side residency occlusion gate** · gate: `test_body_instance_occlusion_reject`-style unit
  test (three-body line-up: camera → occluder → occluded target, occluder already brick-resident) proving
  the occluded target's residency is NOT requested despite passing frustum + resolvability, and IS
  requested once the occluder moves aside or the target emerges past it. Implements the exact mechanism
  Inc1 §M4b already specified (coarse depth estimate from already-resident trees, front-to-back order
  reused from the GPU per-ray fix) — this milestone is "build what was already designed," not new design.
- **M4 — GigaVoxels GPU-LRU: evaluate, decide, document** · gate: a written go/no-build recommendation
  backed by evidence — at minimum, (a) confirm current/near-term resident-tree counts against the
  "tens to a few hundred bodies" threshold Inc1's own CPU-vs-GPU note used to justify staying CPU-side,
  (b) check whether nested-tree work ([[Tiered-ESVO-Observer-Addressing-Design-2026-07]]) has actually
  been scheduled, (c) if either condition now favors building it, produce a scoped sub-plan (request
  buffer + usage buffer sizing, stream-compaction primitive — check for an existing VIXEN library
  primitive before hand-rolling per Inc1's own note, GPU-side LRU list maintenance, `CapabilityGraph`
  gating tier). If neither condition has changed, this milestone's gate is the documented "not yet"
  decision itself, with the specific numbers that would flip it — not a build.

---

## M1 Progress Log

**M1 DONE (2026-07-06)**, on branch `feat/sparse-mip-esvo-inc2`. Added a new `TEST_F` to the
existing `test_bandwidth_ab_measurement.cpp` (`libraries/RenderGraph/tests/Nodes/`) rather than a
new file — same fixture (`BandwidthAbMeasurementTest`), same device/`DirectAllocator`/
`DeviceBudgetManager`/`BatchedUploader` wiring already proven there, so no new build target.

- **Scene**: 16 `BodyOctreeSceneNode` trees (same N as Inc1's endpoint measurement, for direct
  comparability) — 5 positioned 8-40m straight ahead (near/in-frustum/resolvable under a 60°
  FOV) and 11 positioned 1500-2500m straight ahead (far/below the brick-tier resolvability
  threshold at that FOV, per `test_residency_trigger.cpp`'s own 2000m/60° reference point). Not
  a 50/50 split chosen to prove a point — closer to "a few bodies the player is next to, many
  more scattered across the rest of a large explorable region."
- **Driven through the actual live trigger**: every tree starts mip-only (`RequestBrickResidency
  (false)` before `Compile`, per the file's own documented INITIAL-vs-on-demand upload-path
  caveat), then each tree's position is evaluated through `Vixen::SVO::
  InstanceWantsBrickResidency` (`ResidencyTrigger.h`) — the same pure function
  `VulkanGraphApplication::UpdateBodySceneResidency` calls per-instance every camera-changed
  frame in the live app. Only trees the trigger actually grants get
  `RequestBrickResidency(true)` called afterward; the rest are never touched, staying mip-only
  exactly as the real trigger would leave them. This is NOT the existing A/B test's pattern
  (which manually forces `RequestBrickResidency(true)` on every tree in one condition) — it is
  the actual per-frame decision path.
- **Measured (BatchedUploaderStats, real Mesa-Dozen GPU, WSL2, `vixen-wsl` preset)**: live
  trigger granted residency to exactly 5 of 16 trees (matching the intended near/far
  placement, asserted in the test). Mixed scene uploaded **8,362,320 bytes** vs. **26,759,424
  bytes** for the same 16 trees all-resident (the existing test's own baseline, re-run inside
  this test for self-containment) — **31.2% of baseline, i.e. a 68.8% reduction / 3.2x less
  data moved**, reproduced byte-identical across repeated runs (no flake observed). Tracks the
  5/16 = 31.25% near-body fraction almost exactly, as expected: these fixture trees have
  uniform per-tree brick cost, so bytes scale with the resident fraction, not the tree count.
- **Direction doc status banner updated** ([[Sparse-Mip-ESVO-LOD-Direction-2026-07]]): the
  170-220x/100%-reduction number is now explicitly labeled the all-or-nothing extreme endpoint;
  the new 3.2x/68.8%-reduction number is added alongside it as the realistic-mix data point for
  this scene's specific 31%-resident ratio, with an explicit note that a different near/far
  ratio would land at a different point between the two endpoints (scaling with resident
  fraction, not fixed at either number) — matching this milestone's own instruction not to
  let future readers over-read the extreme endpoint as typical.
- **Live-run gate satisfied**: both `test_bandwidth_ab_measurement` tests (the original A/B
  test and this new mixed-scene test) run and pass on the real Mesa-Dozen GPU (`vixen-wsl`
  preset, `VK_ICD_FILENAMES` pointed at the WSL-provisioned `dzn_icd.json`) — 2/2 tests,
  confirmed via direct re-execution (not just a single pass).
- **Not done in this milestone** (out of scope per the plan): no changes to
  `ResidencyTrigger.h`, `UpdateBodySceneResidency`, or any production residency-decision code —
  M1 is purely a new measurement against the mechanism Inc1 already shipped. M2 (flaky-assertion
  hardening), M3 (occlusion gate), M4 (GPU-LRU evaluation) remain open.

**Opus validator: APPROVED (2026-07-06)** — independently re-ran the test 4x on real Mesa-Dozen GPU
(3x mixed-only + 1x full binary), byte-identical every time; confirmed the trigger call is genuine
(residency-request set is *derived from* `InstanceWantsBrickResidency`'s boolean return, not a
hardcoded set dressed up as a trigger call — the crux of this milestone); confirmed zero production
code touched (diff = 1 test file + 2 docs only); independently checked the resolvability arithmetic
and the per-tree byte math (exact to the byte: 26,759,424/16 × 5 = 8,362,320); verified tree
integrity (clean, coherent, no stray artifacts from the implementer's `setsid nohup` build-detachment
workaround); confirmed doc updates accurately distinguish the two numbers and don't overstate 3.2x as
"the" realistic number. **One non-blocking nit for a future doc pass**: the far trees are actually
denied by the frustum far-plane (`kFarPlane=500m`) short-circuiting before the resolvability formula
runs, not by the resolvability threshold as currently described in the docs/test comments — both
gates would deny regardless (resolvability crossover is 660m, still < 1500m), so the 5/16 decision
and bandwidth numbers are unaffected; just an imprecise stated reason, not a re-dispatch.

---

## M2 Progress Log

**M2 DONE (2026-07-06)**, on branch `feat/sparse-mip-esvo-inc2`. Root-caused and fixed the exact
assertion Inc1's M5 validator flagged (`test_partial_brick_upload.cpp`, the `EXPECT_EQ(...totalUploads,
...+1)` check for the phase-2 config-reupload count) — production code
(`BodyOctreeSceneNode.cpp`) was **not touched**; this was a test-assertion timing bug (option (a) in
the milestone's own framing), verified independently rather than assumed.

- **Root cause, precisely**: `ExecuteImpl` calls `UploadBrickPool()` (queues the brick upload) and
  then, **unconditionally, in the same call, with no yield in between**, `PollBrickUploadCompletion()`
  (polls for completion of whatever is pending). The test assumed a fixed 2-tick cadence — frame 1
  queues the brick upload, frame 2 observes its completion and queues the phase-2 config re-upload.
  In reality, `PollBrickUploadCompletion()`'s completion check (`vkGetFenceStatus` via
  `IsUploadComplete`/`ProcessCompletions`) can and does sometimes succeed **within frame 1's own
  `ExecuteImpl` call** — dzn/Dozen signals a tiny buffer copy's fence within microseconds of
  `vkQueueSubmit`, well inside the same function call, when the GPU is otherwise idle (i.e. this
  race is *more* likely on an uncontended system, not less — the opposite of what "flaky under
  concurrent load" suggested). When that happens, the config re-upload is queued on frame 1, not
  frame 2; frame 2's poll then finds nothing pending (Phase 1 already cleared,
  `pendingConfigUploadHandle_` already resolved via the test's own `WaitAllUploads()`), so the
  fixed-tick assertion at old line 353 saw `totalUploads` unchanged instead of `+1` and failed.
  Confirmed via an instrumented debug trace added temporarily to `BodyOctreeSceneNode.cpp` (removed
  before commit — `git diff` shows zero production changes) printing the frame index and each
  Phase-1/Phase-2 branch taken: captured runs where the trace showed `PollBrickUploadCompletion:
  brick IS complete -> queuing config re-upload` firing during frame 1's own `ExecuteImpl`, and
  other runs where the same check reported "not complete yet" and correctly deferred to frame 2 —
  both are legitimate, correct outcomes of the real async state machine, not a state-machine bug.
  No missed transition, no cross-handle race, no production defect found.
- **Fix**: rewrote the test's post-brick-upload assertions (`test_partial_brick_upload.cpp`, the
  block after the frame-1 `Execute()`+`WaitAllUploads()`) to poll — the same non-blocking idiom
  `PollBrickUploadCompletion()` itself uses, no fixed sleep, no assumption about which tick — for
  `totalUploads` to reach a baseline-relative **settled count** (`statsRightAfterRequest.totalUploads
  + 1 brick-upload + 1 config-reupload`), bounded by `kMaxPollTicks = 32` (generous; production
  settles in 1-2 ticks) so a genuine hang still fails loudly (`ASSERT_TRUE(reachedSettledCount)`).
  Also added `ASSERT_LE(..., kExpectedAfterBothPhases)` inside the poll loop so an actual
  over-upload regression (more than brick+config) would still fail as a correctness bug, not be
  silently tolerated by the loosened polling. The baseline is taken from
  `statsRightAfterRequest.totalUploads` (always 0, captured before `RequestBrickResidency(true)`),
  not from a post-frame-1 snapshot — the first fix attempt used a post-frame-1 baseline and still
  failed under stress (12/50), because if the config re-upload already happened on frame 1, that
  baseline already included it, so the loop waited forever for a 3rd upload that would never come;
  switching the baseline to before frame 1 fixed this.
- **Proof (stress-tested on real Mesa-Dozen GPU, WSL2, `vixen-wsl` preset)**:
  - **Pre-fix, isolated** (20 runs, standalone, matching Inc1's own methodology): **4/20 failures**
    (iterations 7, 8, 9, 19), all at the same assertion line, all showing `totalUploads` stuck one
    short of expected — a clean, reproducible repro of the exact Inc1-flagged flake, contradicting
    Inc1's 0/38 controlled-retest finding but consistent with Inc1's "rare flake in the wild" note
    (this run's higher rate is plausibly system-load-dependent, matching the "more likely when the
    GPU is idle/fast" mechanism above).
  - **Post-fix, isolated**: 50/50 passed, then a further 30/30 passed (80/80 total, 0 failures).
  - **Post-fix, 6-way concurrent load** (matching the Inc1 validator's own concurrent-batch
    methodology): 54/54 passed (9 batches of 6 simultaneous processes), 0 failures.
  - **Combined post-fix total: 134/134 passed, 0 failures** — comfortably exceeds the milestone's
    "≥50 iterations under concurrent load" gate.
- **Not done in this milestone** (out of scope per the plan): no production code changes (confirmed
  via `git diff` — only the test file is modified); M3 (occlusion gate) and M4 (GPU-LRU evaluation)
  remain open.

**Opus validator: APPROVED (2026-07-06)** — independently confirmed byte-identical production code
(zero changes to `BodyOctreeSceneNode.cpp/.h`/`VulkanDevice.cpp` vs. parent `4795a36b`), read
`ExecuteImpl`/`PollBrickUploadCompletion` directly and confirmed the no-yield same-tick
config-reupload race is architecturally real (not just asserted), and verified the fix polls the
real completion mechanism (bounded by `kMaxPollTicks=32`, `ASSERT_TRUE` on hang, `ASSERT_LE` still
catching an over-upload regression) rather than loosening the assertion or adding a blind sleep.
Re-ran independently on real Mesa-Dozen GPU: **110/110 post-fix** (55 isolated + 54 6-way concurrent
+ 1 single run), 0 failures; independently reproduced the pre-fix flake at **7/20** (own separate
run, same ballpark as the implementer's 4/20, all at the exact flagged assertion line,
`totalUploads` one short) by temporarily restoring the parent commit's test file, then correctly
restored HEAD, rebuilt, and confirmed a clean tree matching `c8ea0534` before finishing.

---

## Notes for implementers

- M1-M3 are Sonnet-medium implementable against Inc1's existing test fixtures and mechanisms (same
  device/`DirectAllocator`/`BatchedUploader` wiring already proven in `test_partial_brick_upload.cpp`
  and `test_body_instance_occlusion_reject.cpp`) — no new architecture, just filling in what Inc1 already
  specified or measured incompletely.
- M4 is evaluation-first by design (per the Inc1 note's own instruction) — do not let an implementer
  jump straight to building the request-buffer/GPU-LRU mechanism without the go/no-build check landing
  first; the Inc1 note is explicit that at current tree counts the existing CPU formula is "measurably
  sufficient."
- Live-run gate is authoritative for all GPU-touching work here (M1, M3) — per this project's standing
  rule, run and observe on real hardware or lavapipe/Dozen, not just static review.
