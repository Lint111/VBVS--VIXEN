# Lazy-Procedural + Delta Baseline — Perf Ledger

> **Purpose.** A standing, milestone-over-milestone perf table so the *direct effect of each
> change* is visible: render-cycle segment timings, CPU↔GPU bandwidth, and FPS. One row per
> milestone. Grows as [[Lazy-Procedural-Delta-Baseline-Inc0-Inc1-Plan-2026-07]] advances (and
> beyond, into Inc2+). Committed to git so rows diff over time.
>
> **How rows are filled.**
> - **CPU columns** (bake ms, bytes generated, boot-upload bytes) — measured in this WSL session
>   by the pure-CPU harness (`test_generation_cost_benchmark` and successors); filled at each
>   milestone's validation.
> - **GPU columns** (fps, per-pass GPU time, steady-state bandwidth) — this WSL install has **no
>   GPU-backed Vulkan ICD** (see the plan's ENVIRONMENT NOTE), so they come from a **Windows-native
>   run**. The app writes a perf-log **CSV on exit** (frame time, per-pass GPU timestamps, bytes
>   uploaded); the user runs Windows, hands over the CSV, and its numbers are transcribed into the
>   row here. GPU cells read `TODO(win)` until a Windows run backfills them. **lavapipe is a
>   forbidden pattern — GPU rows are real-GPU only, never a software rasterizer.**
>
> **Standard scene / bake** (hold constant so rows compare): single-sphere recipe,
> n=64 / band=2.5 / depth=3 (the M3 benchmark scene) unless a row states otherwise. FPS rows name
> their resolution.

## Render-cycle segments (column meanings)

| Segment | Where | What it measures |
|---|---|---|
| **generate (bake)** | CPU | whole-grid recipe eval → voxels → octree → serialize (`BakeSdfWorld`+`rebuild`+`SerializeSdf`); the eager path. `0` once a body renders GPU-direct (Inc1). |
| **mip bake** | CPU | `BakeAndAttachMipPool` (M1-wired). |
| **boot upload bytes** | CPU↔GPU | bytes uploaded before first frame (the binary bricks blob is what M2 made lazy; channelPool/nodes/mips/lookup/shell still upload whole). |
| **steady bandwidth** | CPU↔GPU | bytes/s streamed during a representative camera move (brick residency traffic). GPU-run. |
| **traverse (GPU)** | GPU | ESVO traversal pass time (per-frame, GPU timestamp). GPU-run. |
| **shade / recipe-eval (GPU)** | GPU | shading + (Inc1) per-ray `sdfRecipe(p)` evaluation pass time. GPU-run. |
| **fps** | GPU | frames/s at stated resolution, steady state, real GPU. |

## Ledger

| Milestone | generate (bake) ms | mip bake ms | boot upload bytes | steady bandwidth | traverse GPU ms | shade/eval GPU ms | fps | notes |
|---|---|---|---|---|---|---|---|---|
| **M0 baseline** (pre-Inc0, eager) | 1472.8 (median; 3-run 1472.8/1535.2/1444.2) | 6.05 | whole pool (nodes+bricks+channelPool+mipPool = 6,260,896 B; **bricks blob uploaded eager**) | TODO(win) | TODO(win) | TODO(win) | TODO(win) | M3 commit `c59211fd`, WSL2 Release. Bricks upload whole at boot. |
| **M1** (production mip wiring) | 1472.8 (unchanged) | 6.05 (now REAL mips, non-placeholder) | same as M0 (still eager) | TODO(win) | TODO(win) | TODO(win) | TODO(win) | Mips real but residency still eager → boot upload unchanged. Commits `4a25a0c2..ae218a41`. |
| **M2** (boot-lazy residency) | 1472.8 (unchanged — bake NOT lazified) | 6.05 | **bricks blob = 0 at boot** (streams on residency trigger); non-brick pool still whole | TODO(win) | TODO(win) | TODO(win) | TODO(win) | The lazy win is on UPLOAD, not generation. Boot-upload-bytes line to be captured in the live gate. Commits `b61f5dd6..0cecdc4e`. |
| **M3** (measurement only) | 1472.8 | 6.05 | — | — | — | — | — | No production change; established the M0 baseline numbers above. Commit `c59211fd`. |
| **M4** (GLSL emitter + parity + perf writer) | 1472.8 (bake path untouched) | 6.05 | — | — | — | — | — | Emitter + numerical parity + perf-CSV writer (Task 6b) only; no render-cycle change. Commits `46837742..024fb297`. **GPU columns from M5 on are populated from the perf CSV this milestone added.** |
| **M5** (uber-shader splice, zero-bake direct) | **0** ✅ (validator-proven — no bake call path) | **0** | **0** ✅ (perf CSVs confirm boot bytes = 0) | see switch-scaling table ↓ | 0 (timer unwired — see note) | measured as frame-time ↓ | **N=3: ~420 · N=10: ~396 · N=100: ~51** | **`generate→0` CONFIRMED LIVE on real GPU (AMD Radeon, D3D12/dzn).** Zero bake, zero upload for virtual bodies — verified by CSV boot/steady bytes = 0. Cost moved to per-frame GPU eval (shows as frame time). Commits `e69affd5..6af6f8f3` + `32518611` (arbitrary-N demo). |

### Switch-scaling measurement (M5, user-requested 2026-07-10) — the switch-vs-rolled-out fork

**Real GPU (AMD Radeon, Windows-native, D3D12/dzn, validation on). N distinct STRUCTURAL recipes
(different opcode programs, not param clones) spliced into one `switch(recipeId)` uber-shader.**

| N | steady FPS | cpu_frame_time_ms (steady) | boot/steady bytes | outcome |
|---|---|---|---|---|
| 3 | ~420 | ~2.5–3.0 | 0 / 0 | clean |
| 10 | ~396 | ~2–5 | 0 / 0 | clean (≈flat vs N=3) |
| 100 | ~51 | ~15–70 | 0 / 0 | **runtime knee — ~8× FPS collapse vs N=10** |
| 500 | — | — | — | **HARD CEILING: native driver `vkCreateComputePipelines` hangs ~14 min, RAM oscillates to ~28 GB, process vanishes — no CSV, no dump. glslang GLSL→SPIR-V itself finished fine (~22s); the wall is the D3D12/dzn driver's shader optimizer on one giant switch + hundreds of spliced fns.** |

**FINDING (decisive, answers the fork):** the uber-switch is **fine through ~10 recipes, degrades hard by 100** (register spill / cache pressure / branch divergence across a much larger switch — a knee between 10 and 100), and is **unusable at 500** (driver-level pipeline-compile hang). **→ A rolled-out per-recipe-pipeline architecture is worth pursuing for anything approaching order-100+ live recipes** — this is the evidence [[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]]'s decision gate required; the JIT epic is now DATA-JUSTIFIED for high recipe counts (and tier-0 switch remains correct for small N).

**Note — `esvo_traverse_shade_ms` was 0 in the N=3/10/100 capture above, now FIXED** (commit
`c0045dcd`). Root cause was NOT a missing wire (the raymarch dispatch's GPU timing was fully wired:
`ComputeDispatchNode` RecordDispatchStart/End → `PerfCsvWriter.GetLastDispatchMs()`), but a shared-infra
bug: `VoxelGridNode`'s memory-only `GPUPerformanceLogger` allocated a `GPUQueryManager` slot it never
reset, and `GPUQueryManager` requires ALL allocated slots reset before it reads back ANY — so one dead
slot silently blocked GPU-timing readback for the whole shared pool (incl. the raymarch node's). Fix =
pass `nullptr` to that memory-logger's ctor (memory-tracking doesn't need the query manager).
**Post-fix (N=10, real GPU): `esvo_traverse_shade_ms` ≈ 1.8–2.4 ms** GPU compute vs ~4–8 ms CPU
frame — a plausible fraction. **AVAILABLE FOLLOW-UP:** re-capture N=3/100/(retry 500) with the now-working
timestamp to attribute the N=100 knee to GPU-vs-CPU precisely (the scaling *conclusion* already holds
from FPS; this would just sharpen the attribution). The switch-scaling table above predates the fix
(its GPU column was 0); its FPS numbers stand.
| **M6** (coarse occupancy + parity gate) | 0 (virtual) | 0 (virtual) | ~0 (virtual) + occupancy grid (16³ f16, small; binding 16) | — | see M5 | occupancy empty-space skip added (should reduce eval for sparse recipes) | — | **Inc1 CLOSED.** Baked-vs-virtual parity PROVEN on real GPU: sphere IoU 0.84, CSG IoU 0.87. Domain-modifier recipes (Twist/Mirror) rendered nothing GPU-direct — **KI-LPD-001 RESOLVED post-M6 (`9f6d82df`): 8×-step-inflation for ungridded recipes; twist_sphere virtual 0→9183.** Residual twist_sphere IoU 0.585 is a corpus authoring bug (KI-LPD-003), not the marcher. Commits `c5025cf7..9f6d82df`. |

## Notes & method

- **Generation is the dominant term, and the win is avoidance** (M3, Opus-validated): CPU generation
  runs ~3–4 MB/s — 2–3 orders of magnitude slower than transferring the same bytes. So the
  "generate" column going to **0** for virtual bodies at M5 is the headline effect this ledger
  exists to show; watch it against the (small) per-frame GPU eval cost that replaces it.
- **Compare rows, not absolutes**: WSL2 CPU timings are machine/load-dependent (M3 validator's box
  ran ~27% slower with the same shape). The *shape* and the *deltas between milestones* are the
  signal; note the machine on any row whose absolute numbers matter.
- **GPU rows come from the in-app CSV** (`PerfCsvWriter`, added M4 Task 6b, commit `46837742`):
  env-knob **`VIXEN_PERF_CSV=<path>`** (no-op when unset), dumped on app exit from a Windows-native
  run. **Schema:** `frame,cpu_frame_time_ms,steady_state_fps,boot_bytes_uploaded,steady_state_bytes_uploaded,esvo_traverse_shade_ms`.
  (`esvo_traverse_shade_ms` is the compute-pass GPU timestamp via the existing `GPUPerformanceLogger`;
  boot vs steady byte counters latch in `BodyOctreeSceneNode`.) To backfill a row: run the app
  Windows-native with `VIXEN_PERF_CSV` set, hand over the CSV, transcribe.

### Bucketed-dispatch measurement (Recipe GPU Instance Bucketing Inc2 M4, 2026-07-16)

**GPU-selection prerequisite (Task 9):** the original switch-scaling table above (M5,
2026-07-10) predates the `DeviceNode::SelectPhysicalDevice()` discrete-GPU-preference fix (main
`0ee32428`, merged 2026-07-15) — its "AMD Radeon" header does not confirm which of this machine's
3 enumerated GPUs (`vulkaninfo`: GPU0=AMD integrated, GPU1=NVIDIA RTX 3060 Laptop discrete,
GPU2=AMD integrated again) was actually selected at capture time, since device selection back
then had no discrete-vs-integrated preference at all (first-enumerated-wins). **Decision: the
old table is NOT reused — the tier-0-switch baseline is RE-CAPTURED below**, on the same
confirmed discrete GPU used for this milestone's bucketed-dispatch numbers, so the comparison is
apples-to-apples. (M1-M3's own standalone test harnesses had the identical gap — no
discrete-preference in their hand-rolled `PickPhysicalDevice()` — fixed in this milestone's new
perf harness, `test_recipe_bucketing_perf.cpp`, by mirroring `DeviceNode`'s exact selection logic.)

**Physical device confirmed for EVERY number below:** `NVIDIA GeForce RTX 3060 Laptop GPU`
(discrete) — printed by both the live-app run (`DeviceNode::SelectPhysicalDevice()`, deterministic
first-discrete-wins, cross-checked against `vulkaninfo --summary`'s enumeration order) and the
standalone perf harness (`PickPhysicalDevice()`, explicit discrete-first pass, logged per-test-case
as `[recipe-bucketing-perf] selected physical device: '...' (discrete=1)`).

**(a) Tier-0-switch-only path, RE-CAPTURED on the discrete NVIDIA GPU** (live `VIXEN.exe`,
`VIXEN_PROCEDURAL_UBER_DEMO=<N>`, `VIXEN_PERF_CSV`, `VIXEN_EXIT_AFTER_FRAMES=300`, validation
layers on, Windows-native Debug build — **this repo has no Windows Release CMake preset**
(`vixen-ninja` is `CMAKE_BUILD_TYPE: Debug` unconditionally; only the WSL presets have a Release
variant), so this Debug-build number is the only Windows-native number obtainable here and is
what both this table and the original 2026-07-10 table were necessarily captured under):

| N | steady FPS (avg, last 60 frames) | steady FPS range | cpu_frame_time_ms (avg) | boot/steady bytes |
|---|---|---|---|---|
| 3 | 165.5 | 161.3–168.4 | 6.10 | 0 / 0 |
| 10 | 171.6 | 165.5–177.7 | 5.47 | 0 / 0 |
| 100 | 85.7 | 82.7–87.8 | 10.30 | 0 / 0 |

**Qualitative shape matches the original 2026-07-10 table** (flat N=3→N=10, real drop by N=100 —
here ~2× rather than the original's ~8×, consistent with the discrete GPU's larger compute/cache
headroom than whatever device the un-fixed original selection landed on) — the tier-0 switch
knee is confirmed again on this GPU, just less severe in absolute terms. Absolute FPS values are
NOT directly comparable to the 2026-07-10 table (different, now-known-correct GPU); the *shape*
is the reusable finding.

**(b) THIS increment's bucketed-dispatch mechanism vs. a cold-path stand-in** (standalone GTest
harness, `test_recipe_bucketing_perf.cpp`, mirrors M1-M3's proven pattern — see that file's header
for why a stand-in shader, not the real `BodyInstanceRayMarch.comp`, is the correct-scoped
substitute; live-app integration is out of scope for this whole increment). N recipes, each with
exactly `kHotnessThreshold=4` instances (100% promoted — Task 6's gate), spread on a
non-overlapping world-space grid (isolates dispatch/routing overhead from compositing, which M3
already proved correct under real overlap). 30 steady-state iterations per N, 1 warm-up iteration
excluded, synchronous specialized-pipeline compile timed and reported SEPARATELY (excluded from
the steady-state figures — see below):

| N | bucketed ms/iter | bucketed fps | cold-stand-in ms/iter | cold-stand-in fps | speedup (cold/bucketed) | sync compile total (excluded above) |
|---|---|---|---|---|---|---|
| 3 | 1.063 | 941.0 | 0.330 | 3031.0 | **0.31x (bucketed SLOWER)** | 1117 ms (372 ms/recipe) |
| 10 | 1.651 | 605.6 | 0.417 | 2397.5 | **0.25x (bucketed SLOWER)** | 1676 ms (168 ms/recipe) |
| 100 | 12.899 | 77.5 | 0.599 | 1670.8 | **0.05x (bucketed SLOWER)** | 9883 ms (99 ms/recipe) |

**HONEST FINDING (Task 9's explicit requirement — not cherry-picked, not papered over): at every
tested N, this increment's bucketed-dispatch mechanism is SLOWER than a single fixed dispatch
covering the same instance/recipe load, and the gap WIDENS as N grows** (0.31x → 0.25x → 0.05x).
This is the anticipated risk from the plan doc's own Risks section, now measured, not assumed:
per-bucket fixed overhead (N separate `vkCmdDispatchIndirect` calls + N descriptor-set binds + N
`MultiDispatchNode` auto-barrier insertions, each against a SMALL per-bucket screen-space rect at
this harness's scene scale) dominates the per-bucket useful work, and that fixed cost scales
linearly with N while the single cold-path dispatch's cost stays roughly flat (it's one dispatch
covering the full screen regardless of how many recipes' instances it loops). **This increment's
own scope was to prove the ROUTING mechanism is correct (M1-M3, APPROVED), not to already be the
faster path** — that was explicitly flagged as a possible outcome (plan doc Risks: "Task 9's
honesty requirement"). The synchronous-compile cost (excluded above, but real and on the critical
path per M2/M3's own scoped limitation) makes first-promotion latency even worse — 99 ms/recipe
at N=100, ~10 seconds total — reinforcing rather than contradicting this finding.

**What this does NOT mean:** the epic's own justification (§8, [[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]])
was for the tier-0 SWITCH's own degradation at N≥100 (confirmed again in (a) above) — this
increment's bucketed-dispatch mechanism was never claimed to already be the fully-optimized
answer to that; it was scoped to prove routing correctness first. The per-bucket fixed-overhead
problem measured here is exactly what the deferred async-compile-and-swap follow-on (Increment
3+) and any future per-bucket-dispatch-batching work would need to address before bucketed
dispatch is competitive — this is the concrete, measured starting point for sequencing that work,
not a reason to abandon the mechanism. **Sequencing implication for Increment 3+:** async compile
alone does not fix the per-iteration 0.05x–0.31x gap measured here (that gap is steady-state,
compile already excluded) — the NEXT thing to measure/fix is per-bucket dispatch overhead
(barrier/bind cost per `vkCmdDispatchIndirect`), not just compile latency.
