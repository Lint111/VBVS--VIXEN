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
  reused from the GPU per-ray fix) — this milestone is "build what was already designed," not new design. ·
  **✅ DONE 2026-07-06** — commits `2a6dc9aa`, `68569f9b`, `2380f808` (the last fixes a
  blocking self-occlusion bug found by independent Opus validation); see M3 Progress Log below.
- **M4 — GigaVoxels GPU-LRU: evaluate, decide, document** · gate: a written go/no-build recommendation
  backed by evidence — at minimum, (a) confirm current/near-term resident-tree counts against the
  "tens to a few hundred bodies" threshold Inc1's own CPU-vs-GPU note used to justify staying CPU-side,
  (b) check whether nested-tree work ([[Tiered-ESVO-Observer-Addressing-Design-2026-07]]) has actually
  been scheduled, (c) if either condition now favors building it, produce a scoped sub-plan (request
  buffer + usage buffer sizing, stream-compaction primitive — check for an existing VIXEN library
  primitive before hand-rolling per Inc1's own note, GPU-side LRU list maintenance, `CapabilityGraph`
  gating tier). If neither condition has changed, this milestone's gate is the documented "not yet"
  decision itself, with the specific numbers that would flip it — not a build. ·
  **✅ DONE 2026-07-06** — commit `463ca2a8` (this milestone's own doc commit); decision: **not yet,
  documented "no-build" with specific flip triggers** — see M4 Progress Log below.

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

## M3 Progress Log

**M3 DONE (2026-07-06)**, on branch `feat/sparse-mip-esvo-inc2`. Built the CPU-side residency
occlusion gate exactly as Inc1 §M4b specified (see that plan's "DEFERRED TO INC2" bullet) — a
coarse, per-candidate ray-vs-sphere test against already brick-resident trees, not a new render
pass or a full per-pixel occlusion query.

- **New pure, dependency-free header** `libraries/SVO/include/OcclusionGate.h`, matching
  `FrustumCull.h`/`ResolvableLevel.h`/`ResidencyTrigger.h`'s own no-node/no-GPU-type idiom (glm-only,
  freestanding functions, unit-testable without a device/graph):
  - `RaySphereNearestHit(rayOrigin, rayDir, sphereCentre, sphereRadius, outT)` — the coarse geometric
    primitive: nearest non-negative ray-vs-sphere hit distance, or false on a miss/behind-origin sphere.
  - `ResidentOccluder{ centre, radius }` — a minimal already-resident-tree record, deliberately NOT
    `BodyInstanceGpu` itself (keeps the gate dependency-free the same way `ResidencyTrigger.h`
    intentionally doesn't take node/device types).
  - `IsOccludedByResidentTrees(cameraPos, candidateCentre, candidateDistance, residentOccluders)` — for
    each resident occluder, casts the ONE representative ray `cameraPos -> candidateCentre` and checks
    whether that occluder's bounding sphere is hit strictly closer than the candidate's own distance
    (a small epsilon guards a resident tree's own sphere from self-occluding on float noise). Empty
    `residentOccluders` (nothing resident yet) always returns false — the same graceful-degradation
    path `ResidencyTrigger.h` documents for its own missing occlusion parameter.
- **Wired into the live decision path**: `VulkanGraphApplication::UpdateBodySceneResidency()`
  (`application/main/source/VulkanGraphApplication.cpp`) now builds a `residentOccluders` list from
  `bodyScene->GetInstances()` (using the same `kResidencyBoundingRadius` constant `ResidencyTrigger.h`'s
  call already uses) whenever the NEW member `lastResidencyGranted_` (declared in
  `application/main/include/VulkanGraphApplication.h`) is true — i.e. the occlusion gate's "already
  resident" input is the pool's state as of the LAST re-check, not whatever this frame is about to
  decide (matches Inc1 M4b's "coarse depth estimate built from already brick-resident trees only," not
  a self-referential same-frame set). The per-instance loop now requires BOTH
  `InstanceWantsBrickResidency` (frustum+resolvability, unchanged) AND `!IsOccludedByResidentTrees`
  before granting; `lastResidencyGranted_` is updated at the end of the function from the frame's own
  decision, becoming next re-check's occlusion input. `SortInstancesFrontToBack`'s existing call site
  is untouched — this milestone deliberately reuses that ordering's existence as a design precedent
  (per Inc1 M4b's own note) but does NOT feed the sorted array positionally into the gate; the gate
  tests actual world positions/radii, independent of array order.
- **CPU-vs-GPU scale assumption re-confirmed, not just cited**: re-read Inc1 M4b's own reasoning
  (`Sparse-Mip-ESVO-LOD-Inc1-Plan-2026-07.md` §M4b) and confirmed it still holds rather than taking it
  on faith — `BodyOctreeSceneNode.cpp`'s existing instance cap and the undertow 60-300-body target are
  unchanged since Inc1, and this milestone's own mechanism is O(candidates × residentTrees) ray-vs-sphere
  tests, strictly cheaper per-pair than Inc1's own frustum+resolvability formula (no trig/log2, just a
  dot-product quadratic). At tens-to-hundreds of trees this is trivially sub-millisecond single-threaded;
  no GPU move warranted. Unchanged from Inc1's own conclusion — revisit only if the nested-tree epic
  pushes resident-tree counts up by orders of magnitude, per that section's own caveat.
- **New test** `libraries/SVO/tests/test_occlusion_gate.cpp` (11 tests, pure CPU, no device/graph —
  mirrors `test_residency_trigger.cpp`'s own no-GPU convention rather than
  `test_body_instance_occlusion_reject.cpp`'s GPU-device harness, since the gate under test here is
  pure CPU logic, not a shader change):
  - 3 isolated `RaySphereNearestHit` primitive checks (direct hit, off-axis miss, behind-origin miss).
  - 4 isolated `IsOccludedByResidentTrees` checks (empty resident set never occludes; a blocking
    resident sphere on-axis occludes; an off-axis resident sphere does not; a resident sphere FARTHER
    than the candidate along the same ray does not).
  - **The decisive test** (`OccludedTargetResidencyNotRequestedDespitePassingFrustumAndResolvability`):
    three-body line-up (camera at origin → occluder at 50m → target at 200m, collinear), occluder
    already brick-resident. First asserts (precondition) the target WOULD be granted residency by
    frustum+resolvability alone — proving the subsequent rejection is actually the occlusion gate's
    doing, not some other reason — then asserts the combined `WantsResidency` helper (mirroring
    `UpdateBodySceneResidency`'s own AND-of-both-gates decision) returns false with the occluder present.
  - **Emerge/move-aside symmetry** (the plan's explicit ask): `TargetResidencyRequestedOnceOccluderMovesAside`
    (occluder relocated off the camera→target ray) and `TargetResidencyRequestedOnceItEmergesPastTheOccluder`
    (target repositioned nearer than the occluder along the same ray) both assert residency is granted
    again via the identical combined gate.
  - Sanity check (`LoneCandidateGrantedWhenNothingResidentYet`): an empty resident set never itself
    rejects an otherwise-qualifying candidate — the gate degrades gracefully, doesn't accidentally
    reject everything.
  - Registered in `libraries/SVO/tests/CMakeLists.txt` mirroring `test_residency_trigger`'s own block
    (same `GTest::gtest_main SVO` link, same TBB-DLL-copy custom command, same
    `Tests/SVO Tests` folder, `gtest_discover_tests`).
- **Pre-existing, unrelated build breakage found and fixed while confirming the regression gate**:
  `shaders/BodyInstanceRayMarch.comp`'s call to `handleLeafHitInstancedSdf` (line 634, Inc3 M3's
  Stored-SDF dispatch branch) was missing the `stack` argument the function's own declared signature
  requires (`inout StackEntry stack[STACK_SIZE]`, used internally by `marchBrickSdf`) — a genuine
  arity-mismatch shader compile error, confirmed present BEFORE any of this milestone's own changes
  (reproduced standalone via `glslc` after `git stash`-ing every M3 change, restored after confirming).
  **Attribution correction (independent Opus validation, 2026-07-06):** originally attributed to Inc1
  M4b (`f634d549`) since that commit last touched this shader on this branch's own history; the
  validator traced the TRUE origin to merge commit `ae12ba78` (main ← `feat/sparse-mip-esvo-inc1`),
  which combined a signature WITH `stack` from main's parallel `47eccd64` with a call site WITHOUT
  `stack` from the Inc1 branch — a merge-conflict-resolution gap, not something either branch alone
  introduced. The substance of the original claim is unchanged: pre-existing, not self-introduced by
  this milestone, not introduced by Inc2 M1/M2. This blocked `test_body_instance_occlusion_reject` and
  the whole `VIXEN`/`VixenApp` targets from building at all, which would have made this milestone's own
  regression-check gate impossible to satisfy honestly. Fixed with the minimal one-line correction
  (added the missing `stack` argument at the call site, matching the sibling `handleLeafHitInstanced`
  call two lines below it, which already passes `stack` correctly) — not touched or extended beyond
  that.

### BLOCKING fix (post-Opus-validation, 2026-07-06): self-occlusion thrash

Independent Opus validation of the first M3 pass found a real bug in the live wiring, caught before
any of this milestone's other findings shipped as final: `residentOccluders` in
`VulkanGraphApplication::UpdateBodySceneResidency` was built from ALL current instances (whenever
`lastResidencyGranted_` was true), then EACH candidate was tested against that SAME full set —
including the candidate's own bounding sphere. There was no identity exclusion. The ray from the
camera toward a candidate's own centre always "hits" that candidate's own sphere at
`candidateDistance - radius`, comfortably closer than `candidateDistance` itself (not float noise —
the validator's own reproduction: with `kResidencyBoundingRadius = 24`, the self-hit lands 24 units
short of the true distance, and the original `kSelfHitEpsilon = 1e-3` was ~24 units too small to mask
it). Every candidate therefore self-occluded every time residency was already granted, producing:
frame N grants residency → frame N+1 every candidate self-occludes → `RequestBrickResidency(false)` →
bricks evicted → `lastResidencyGranted_=false` → frame N+2 occluder set empty → grants again →
repeat. This would have defeated the milestone's entire point (worse than no occlusion gate at all —
constant re-upload thrash instead of a stable decision). Confirmed NOT caught by the original
`test_occlusion_gate.cpp`: every occluder set in every original test was a genuinely separate body
from the candidate, never reproducing the live wiring's candidate-included-in-its-own-occluder-set
construction.

**Fix — identity exclusion, not a bigger epsilon.** `OcclusionGate.h`'s `ResidentOccluder` gained an
`id` field (`kNoOccluderId = -1` default) and `IsOccludedByResidentTrees` gained a `candidateId`
parameter: an occluder whose `id` matches `candidateId` is skipped unconditionally, regardless of
geometry — "a tree/pool cannot occlude its own pending residency decision" is a correctness statement,
not a numerical tie-break an epsilon could paper over. `UpdateBodySceneResidency` now builds
`residentOccluders` with each occluder's `id` set to its own index in `GetInstances()`, and passes the
candidate's own index as `candidateId` when testing — both loops switched from range-based to
indexed iteration for this. The prior epsilon (`kSelfHitEpsilon = 1e-3`) is retained only as a genuine
floating-point-noise guard (renamed `kNumericNoiseEpsilon`), not as the self-occlusion fix itself.

**New tests** (`test_occlusion_gate.cpp`, +3 tests, 14 total): `RaySphereNearestHit_ConfirmsTheSelfHitMechanismIsReal`
isolates the geometric root cause directly; `CandidateDoesNotSelfOccludeWhenPresentInItsOwnOccluderSet`
directly reproduces the validator's 3-body probe (a full occluder set built from every instance, each
tested against itself via matching id) and proves no self-occlusion after the fix — bodies placed with
wide lateral spread (verified numerically via an independent Python check first, matching this file's
own established practice) so none of the three genuinely occludes another, isolating the assertion to
self-occlusion only; `CandidateStillCorrectlyOccludedByADifferentResidentTreeWhenIdsDiffer` guards
against an over-broad fix, confirming id-exclusion skips only the matching id and still lets a
genuinely different resident tree occlude correctly. All 14/14 pass.

**Live-run gate (previously missing — the plan's own "Live-run gate is authoritative... (M1, M3)"
note, not satisfied by the first M3 pass, which only ran the 3 isolated unit-test suites):** ran the
real `VIXEN.exe` on real Mesa-Dozen GPU (WSL2, `vixen-wsl` preset) with
`VIXEN_STORED_SDF_DEMO=1 VIXEN_RESIDENCY_GATE_DEMO=1 VIXEN_EXIT_AFTER_FRAMES=650` — the identical
scripted camera sweep Inc1 M4c's own live gate used (far→near→yaw-sweep→near→far over the same
3-body Stored-SDF scene, X=14/64/114). Result: clean exit at 650 frames ("frame limit reached"), 0
crashes/exceptions/VUIDs/SYNC-HAZARD lines. Exactly **9** `RequestBrickResidency` transitions logged
across the whole run (`false→true→false→true→false→true→false→true→false`), each corresponding to one
of the 3 separately-placed bodies individually crossing its own resolvability threshold during the
distance sweep — matching Inc1 M4c's own documented pattern almost exactly, and critically NOT a
thrash storm: residency settles and holds stable for long stretches (0 further toggles from frame
~430 through the run's end at frame 650, despite the camera continuing to move through the yaw-sweep
and near→far-again phases) rather than flipping every frame, which is exactly what the pre-fix bug
would have produced once any residency was ever granted. This is the check that would have caught the
original bug — it now passes cleanly.

- **Not done in this milestone** (out of scope per the plan): M4 (GPU-LRU evaluation) remains open.
  No changes to `ResidencyTrigger.h`, `FrustumCull.h`, or `InstanceSort.h` — the occlusion gate is a
  new, independent gate layered on top via `UpdateBodySceneResidency`, not a rewrite of the existing
  three mechanisms.

**Opus validator: first pass ISSUES (2026-07-06), re-validated APPROVED after fix.** First pass found
a genuine BLOCKING bug via a direct probe (candidate included in its own occluder set → self-occlusion
on all 3 test bodies), correctly root-caused as identity, not epsilon (self-hit lands at
`dist - radius`, ~24 units off, far outside any reasonable numeric-noise tolerance). Fix re-dispatched
to the same implementer; re-validated independently: confirmed the `id`/`candidateId` exclusion is
unconditional and geometry-independent (not a bigger epsilon), confirmed both wiring loops share one
index scheme (no desync), re-derived the new tests' geometry in Python (self-occlusion test isolates
the bug precisely; the over-broad-exclusion guard test proves a genuinely different resident tree
still occludes correctly), re-ran all 14 occlusion-gate tests + 8 residency-trigger tests + 2 GPU
occlusion-reject tests fresh, and — the decisive check — re-ran the 650-frame live gate independently
and mapped all 9 transitions to frame numbers/camera distances by hand, confirming a 184-frame fully
stable tail under continued camera motion (the settle-and-hold signature the fix should produce,
not the every-frame thrash the bug would have produced). **APPROVED**, no remaining caveats.

---

## M4 Progress Log

**M4 DONE (2026-07-06)**, on branch `feat/sparse-mip-esvo-inc2`. This is a decision-only milestone
per its own gate definition — no request-buffer/GPU-LRU code was written. Both of the plan's own
go/no-build conditions were re-checked against the actual current codebase and vault state (not
assumed unchanged from Inc1), and the evidence-based conclusion is **not yet — document "no-build"
with concrete flip triggers**, matching Inc1's own instruction not to build this speculatively.

### (a) Resident-tree-count threshold — re-checked, one correction found

Re-verified Inc1's own citation (`Sparse-Mip-ESVO-LOD-Inc1-Plan-2026-07.md:811-812`, "tens to a few
hundred bodies, per `BodyOctreeSceneNode.cpp:666`'s existing `3*64` instance cap") directly against
current code rather than taking it on faith, since Inc1 was written 2026-07-05 and this is now
2026-07-06 with several intervening commits:

- **The "`3*64` instance cap" citation does not describe any cap that ever existed.** The `3` was a
  conflation of `ConcatenatedOctrees::kMaxOctrees = 3` — a cap on distinct **octree kinds/configs**
  (introduced `81c916cc`, 2026-06-19), not on instance/body count — which was itself **removed** in
  `89b71001` "refactor(svo): octree pool is dynamic-N — drop kMaxOctrees=3 cap (I3.1)" (2026-06-29,
  i.e. *before* Inc1's plan doc was even written) and the shader-side `configs[3]` clamp dropped the
  same day in `efe225b3`. A stale test still asserting the old cap was caught later as KI-002
  (`75cca4b2`, 2026-07-02). `BodyOctreeSceneNode.cpp:655` today (901 lines, inside `SetShellThickness`)
  is a shell-dilation clamp (`dilation > 3u ? 3u : dilation`) — unrelated to instance count entirely
  (line 666 is inside the unrelated `DeriveShellCache`; corrected per Opus validator nit below).
  Inc1's citation was already
  stale at the moment it was written; this milestone corrects it rather than propagating it further.
- **Actual current behavior: no instance-count cap exists, hard or soft.** The instance ring
  (`BodyOctreeSceneNode::EnsureRingAllocated`, `BodyOctreeSceneNode.cpp:599-629`) is dynamic
  grow-only — `instanceRingCapacity_` (`BodyOctreeSceneNode.h:303`) reallocates whenever
  `neededCapacity` (derived directly from `Vixen::SVO::PackInstances(instances_).size()`) exceeds the
  current capacity; no upper bound is checked or enforced anywhere in the call chain. Commit `3d964cfd`
  (2026-07-03, "instance ring grows on SetInstances overflow") hardened this further by fixing a narrow
  OOB-read window on overflow, confirming growth is a deliberately-supported, safe path, not an
  incidental one. `test_body_octree_lifetime.cpp` (`kLargeCount = kSmallCount*8 = 32`) exists
  specifically to exercise this grow-on-overflow path and passes.
- **Actual current demo/test scene sizes remain small.** `VIXEN_STORED_SDF_DEMO` and the default
  no-flag fallback scene both seed **3** body instances (`BuildRenderGraph.cpp:641-648` and
  `:675-682`); `VIXEN_RESIDENCY_GATE_DEMO` reuses that same 3-body scene, only sweeping the camera.
  The largest N used anywhere in this epic's own tests is Inc2 M1's `test_bandwidth_ab_measurement.cpp`
  at **16** trees (`kNumTrees = 16`, confirmed still current) — chosen for direct comparability with
  Inc1's own endpoint measurement, not because it reflects a ceiling.
  `test_body_octree_lifetime.cpp`'s `kLargeCount = 32` is a ring-growth stress case, not a rendered
  scene. None of these are within an order of magnitude of "a few hundred."
- **The undertow target figure is unchanged and still the only number in the vault.** "60-300 bodies"
  appears verbatim in 5 docs (`Sparse-Mip-ESVO-LOD-Direction-2026-07.md:9`,
  `Sparse-Mip-ESVO-LOD-Inc1-Plan-2026-07.md:812`, this doc `:268`, `Widescreen-Perf-Fix-Plan-2026-07.md:318`,
  `Widescreen-Perf-Sweep-Findings-2026-07.md:108,158`) — no doc anywhere in `Vixen-Docs/` updates or
  supersedes this figure, and this milestone's own search found no newer number. The
  `undertow-vixen-integration-map` memory note (last touched 2026-06-29) cites the same "60-300"
  figure for "Path A (instances)."
- **Net effect on the threshold check**: the specific citation Inc1 used (`3*64` cap) turns out never
  to have existed, but the substantive conclusion it was defending — current/near-term scene sizes are
  in the tens-to-low-hundreds, not hundreds-to-thousands — is **still correct** on the actual evidence
  (3-16 in current tests/demos, 60-300 as the only documented target ceiling). The correction is to the
  citation, not to the underlying number-order conclusion.

### (b) Nested-tree epic scheduling status — checked, still purely a design doc

Read `Tiered-ESVO-Observer-Addressing-Design-2026-07.md`'s own status header directly (not inferred):
`status: Design (promoted from direction 2026-07-05) — NOT built, no increment started`, and its body
text repeats this explicitly: *"Nothing here is built. No increment has been scoped or started. This
is the doc a Plan would be written against, once the base mip-sampling epic in the parent direction
doc has shipped (§9, sequencing)."* Checked for any movement past that status since 2026-07-05:

- **No Inc1-style implementation plan doc exists for this epic.** Only one file matches
  `*Tiered*`/`*Nested-ESVO*`/`*Tree-of-Trees*` anywhere in the repo — the design doc itself. Compare to
  this epic's own sibling pattern: the mip-sampling epic has a Direction doc, then an Inc1 Plan doc,
  then an Inc2 Plan doc (this file) — the nested-tree epic has only step one of that three-step
  pattern.
- **No branch, worktree, or commit activity toward it.** `git branch -a | grep -i "tier|nested|observ"`
  returns nothing (checked against all local branches, all remote-tracking branches, and worktree
  names — none reference tiers/nested-trees/observer-addressing). `git log --all --oneline --grep`
  across "tier", "nested-tree", "tree-of-tree", "observer.address" (case-insensitive) surfaces only
  this doc's own authoring commits (`0051949e`, `5e668c2b`) and unrelated false-positive matches from
  other epics' commit messages that happen to contain the substring "tier" (e.g. AppFlow's
  "Tier-2 home decision," RenderGraph's "Tier-1 barrier replay" — different meaning of "tier," nothing
  to do with this design doc). One older, unrelated hit,
  `423122ca "feat(proposal): Integrate GigaVoxels GPU-driven usage-based caching"` (2025-12-31,
  6+ months before this design doc or even Inc1 existed, a `GaiaVoxelWorld` backend-expansion proposal
  doc, different codebase area) — not connected to this epic's scheduling in any way.
- **Conclusion: nested-tree work has not moved from "design doc" to "actively planned/in-progress."**
  Condition (b) is unchanged from how Inc1's own note described it.

### Decision: not yet — no-build, documented with concrete flip triggers

Following the Inc1 note's own stated criterion precisely (`Sparse-Mip-ESVO-LOD-Inc1-Plan-2026-07.md:156-162`):
this mechanism is motivated specifically by **large, partially-occluded/partially-visible single
bodies** (a planet-scale tree) as **a natural companion to the nested-tree epic**, and Inc1's own
instruction was "decide whether to build this once M5's bandwidth measurement is in and nested-tree
work is actually scheduled, not before." M5 (Inc2 M1, done) is in. Nested-tree work is **not**
scheduled — still a pure design doc, zero branches/commits/plan docs toward it (§b above). Resident
tree counts remain in the low tens in every current test/demo, with 60-300 as the only documented
future ceiling, still an order of magnitude below where a per-brick GPU request/LRU mechanism would
be justified over the existing sub-millisecond CPU formula (§a above; M3's Progress Log already
re-confirmed the CPU-side cost model independently for the occlusion gate at this same scale).
**Neither condition has changed. The decision is no-build, matching the plan's own expected outcome —
this was verified against current evidence, not assumed.**

This is not a "let's not bother" default — both conditions were independently re-derived in this
milestone (finding and correcting the stale `3*64` citation in the process) rather than copy-pasted
from Inc1's note, and both point the same direction Inc1 already predicted.

**No sub-plan produced** (per the plan's own gate: a sub-plan is only warranted "if either condition
now favors building it"). For the record, in case a future revisit's evidence flips: no existing VIXEN
stream-compaction/prefix-sum primitive was found (`grep -rl "prefix sum|StreamCompact|PrefixSum|ScanCompute"`
across `libraries/` returns nothing) — a future build would need to either hand-roll one or bring in an
external primitive, which Inc1's own note already flagged as worth checking before assuming; this
milestone confirms the check comes back empty, information a future go-decision would need immediately.
The `CapabilityGraph` gating tier Inc1 sketched (`DeviceExtensionCapability`/`DeviceFeatureCapability`
composition, `VK_EXT_device_generated_commands` → persistent-thread/megakernel fallback →
CPU-formula floor) remains architecturally sound and unchanged; nothing in this milestone's
investigation invalidates it, it simply isn't needed yet.

### Concrete triggers that would flip this decision

A future reader re-evaluating this should treat **either** of the following as sufficient grounds to
re-open M4-style build work, without needing to redo this whole investigation:

1. **Resident-tree/instance counts actually reach the hundreds in a real scene** — not a synthetic
   stress test like `test_body_octree_lifetime.cpp`'s `kLargeCount=32` (which exists to test ring
   growth, not to represent a target scene), but an actual demo, benchmark, or undertow integration
   scene instantiating on the order of 100+ concurrently-resident `BodyOctreeSceneNode` trees. Watch
   specifically for the "60-300" figure (currently just a documented aspiration in 5 docs, never
   exercised in any current test) becoming a real, run scene — that would put this epic's own
   scale assumption ("tens to a few hundred... measurably sufficient") at its own stated edge.
2. **`Tiered-ESVO-Observer-Addressing-Design-2026-07.md` gets an actual implementation plan doc** —
   i.e. a sibling `Tiered-ESVO-*-Inc1-Plan-2026-0X.md` appears (matching this epic's own Direction →
   Inc1-Plan → Inc2-Plan document lineage), or a branch/worktree is created specifically to build
   against it, or its status header changes from "NOT built, no increment started" to anything
   indicating active scoping. Either of these directly satisfies Inc1's own stated pre-condition
   ("nested-tree work is actually scheduled").
3. **A future increment finds the CPU-side occlusion gate (Inc2 M3) or residency formula
   measurably failing to keep up** at real scene sizes actually being tested (not hypothesized) —
   i.e. a profiler-measured per-frame CPU cost that stops being sub-millisecond. This would be direct
   empirical evidence independent of tree-count guesses, and would justify revisiting even before
   condition 1 or 2 individually crosses its own threshold.

None of the three has occurred as of this milestone (2026-07-06). Re-check this section, not just the
Inc1 note, on the next pass — it supersedes Inc1's `3*64` citation with the corrected reasoning above.

**Opus validator: APPROVED (2026-07-07)** — independently re-verified all claims above against the
actual codebase/git history/vault rather than trusting the report: confirmed the stale-citation
correction is itself accurate (`kMaxOctrees=3` traced to `81c916cc`→removed `89b71001`, six days
before Inc1's `3abcac0c` citation was written); confirmed no instance cap exists by reading
`EnsureRingAllocated` directly; confirmed the 3/32/16 tree-count figures and the "60-300" ceiling by
direct file reads and a broad vault search; confirmed the nested-tree epic is genuinely design-only
(no plan doc, branch, or worktree references it; `git log --all --grep` fully accounted for); and
confirmed the "no-build" decision follows from Inc1's own stated AND-criterion rather than being
predetermined. Judged the document itself well-organized and reusable (specific hashes/lines/searchable
terms). One non-blocking nit (a `:666`→`:655` line-reference typo in this doc, unrelated to any
conclusion) found and corrected above.

---

## Notes for implementers

- M1-M3 are Sonnet-medium implementable against Inc1's existing test fixtures and mechanisms (same
  device/`DirectAllocator`/`BatchedUploader` wiring already proven in `test_partial_brick_upload.cpp`
  and `test_body_instance_occlusion_reject.cpp`) — no new architecture, just filling in what Inc1 already
  specified or measured incompletely.
- M4 is evaluation-first by design (per the Inc1 note's own instruction) — do not let an implementer
  jump straight to building the request-buffer/GPU-LRU mechanism without the go/no-build check landing
  first; the Inc1 note is explicit that at current tree counts the existing CPU formula is "measurably
  sufficient." **DONE 2026-07-06**: the check landed "not yet" — see M4 Progress Log's "Concrete
  triggers" for what would flip it before building anything here.
- Live-run gate is authoritative for all GPU-touching work here (M1, M3) — per this project's standing
  rule, run and observe on real hardware or lavapipe/Dozen, not just static review.
