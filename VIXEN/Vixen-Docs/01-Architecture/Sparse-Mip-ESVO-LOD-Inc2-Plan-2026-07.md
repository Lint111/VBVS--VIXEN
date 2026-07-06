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
  readers don't over-read the endpoint as typical.
- **M2 — Harden `test_partial_brick_upload`'s flaky assertion** · gate: root-cause the exact race between
  the brick-upload completion poll and the config-reupload completion poll (Inc1's Progress Log already
  narrows it to `PollBrickUploadCompletion`'s phase-2 count), then either fix the assertion to tolerate
  the legitimate interleaving or fix a genuine state-machine bug if one is found — proven via a stress
  run (≥50 iterations under concurrent load, matching the validator's own reproduction methodology) with
  zero failures after the fix, vs. a captured pre-fix failure for comparison.
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
