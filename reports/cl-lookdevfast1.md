# cl-lookdevfast1: look-dev capture speed (R838.16, R812.14)

Branch `lane-lookdevfast1` (worktree `VBVS--VIXEN/.claude-worktrees/lookdevfast1`), from `origin/wave/authoring-convergence` (be4dcfaa). One commit: `0f37c418`. Nothing pushed.
Evidence: `/home/liory/codeman-cases/undertow/reports/cl-lookdevfast1-evidence/` (logs, per-run scripts, PNG hashes). All timings are Dozen (dzn) and non-representative for performance; they are correctness-lane wall times.

## SUMMARY

- **The slow captures are GPU-bound, not setup-bound.** One `State.midday` process is 117 s: about 6 s startup, then 32 frames at 3.3 s each. One frame is one `test_dispatch` (ray-march) pass: 1.73 s of GPU in a 1.87 s whole-frame GPU span. Process start, device creation, graph build, readback and compare together are under 10 s.
- **There is no cheap 76% cut.** What I tried and measured:
  - Running captures concurrently: little gain on the long tests (below), and it broke a timing assertion.
  - Cutting frames: changes pixels. At 4 frames per angle instead of 8, angle 0 matches but angles 1, 2 and 3 differ in all 4 states (12 of 16 files). The frame count is load-bearing; I did not change it.
  - The pipeline cache already exists and is reused (`SharedPipelineCache`, warm after the first run). It is cold only in the two tests that give themselves a private cache dir.
- **Two selection facts explain much of the 1 h 45 min:**
  - The glmdepth1 run was a single `ctest -L gpu` job. It ran the `native` duplicates that `tools/run-tests-split.sh` excludes by default: `RendersFourStatesAtFourAnglesDeterministically` (522 s) and `SinglePass.*` (254 s), 776 s that repeat work `State.*` already does.
  - A single job also serialises on `RESOURCE_LOCK vixen_gpu_device`. But the lock is only per ctest process, and the queue already shards (`split-gpu-N`, shared-device admission).
- **Landed (small, safe):**
  - Cost-weighted GPU shard planner. Round-robin gives a 6-shard makespan of 2957 s against a 1486 s ideal on the measured times.
  - `COST` on the 34 slowest look-dev captures.
  - Family labels `lookdev-*`.
  - A source→family manifest for affected-subset selection.
- **Not proven:**
  - A before/after for the whole `-L gpu` wall time. The full tree is not built, and both sharded lookdev runs I attempted were stopped (see MEASURED and REDS).
  - A per-test speedup. There is none yet.
- **Best unmeasured lead:** per-frame wall (about 3.0 s) is well above GPU time (about 1.75 s). Each frame is split into 81 yield-tile submissions at a 2 ms budget, while measured tiles take 21-25 ms. A tile-edge experiment (`VIXEN_GPUYIELD_EDGE`, queued as `cl-lookdevfast1:tile-edge`) was still waiting on queue admission when this report was written; see STOPs.

## WHERE THE TIME GOES

All from `[CAPTURE_TIMING]` lines and `VIXEN_PHASE_TIMING`.

**`State.midday`** (queue job `base-state-midday`, `ctest -V`, evidence `base-state/midday.log`): 234 s = 2 fresh processes (first and repeat) of 117 s each.

| Phase | First process | Repeat process |
|---|---|---|
| process start to `BuildRenderGraph` | 46 ms | 33 ms |
| graph build | 598 ms | 541 ms |
| build end to first frame (device, pipelines from warm cache) | 6.75 s | 4.95 s |
| frames (4 states × 8 frames = 32) | 97.7 s, p50 3.28 s | 99.3 s, p50 3.35 s |
| PNG capture and readback, total | 29 ms | 69 ms |
| SHA-256 compare of 4 PNG pairs | negligible | |

- Frame time is stable (p50 3.3 s, p95 4.1 s, max 4.4 s). That is real work, not a stall.
- Scene build and voxelisation are not visible in this scene (graph build is 0.6 s).

**One-angle profile** (`prof2/`, `perf.csv`): whole-frame GPU span 1875 ms, of which `esvo_traverse_shade` is 1734 ms. Wall per frame was 2.4-2.6 s.

**Tiling overhead** (log of `Presentation.night-service`, `after2/`):
- `[GPUYIELD] submissions=81 chunks=81 total_gpu_ms≈1750 max_submission_ms≈24 budget_ms=2.0 device_class=translated`.
- Wall per frame is about 3.0 s against about 1.75 s of GPU time. Roughly 40% of a frame is host-side submit and sync across 81 submissions (inferred from the gap, not isolated).
- The tile edge sits at the cold floor of 7 workgroups (`GpuChunkCost::Edge`) because the 2 ms budget is unreachable for this shader.

**`Presentation.*`** (4 × 433-526 s): same engine as `State.*`, with `DefaultFramesPerScenario` = max(8, 2 × jitter cycle 16) = 32 frames per angle, so 128 frames per process, two processes. The time is the same per-frame cost × 4× the frames (inferred from the code and the per-frame measurement; I did not time a Presentation process alone).

**`GlyphSdf.latin-carved`** (1920×1080, 1 frame; `glyph/run.log`): 103.6 s.
- 38.7 s to first frame, of which 33.5 s is four cold `vkCreateComputePipelines` (13.3 + 9.0 + 2.8 + 8.4 s, `warm_cache=0`). This test, and `RtlProof.Pair`, set a private `VIXEN_CACHE_DIR` and `VIXEN_SHARED_CACHE_DIR` under their capture dir, so they always start cold. That is deliberate for `ColdBound`.
- 17.8 s per 1080p frame × 3 frames, because `exitAfterFrames = frames + 2` (one capture frame plus two drain frames that render in full). About 53 s.
- About 10 s teardown and invariant checks.

**Concurrency** (`conc/`, one job, `midday` angle 0, 4 frames; other lanes were using the GPU, load average about 50, so noisy):

| Processes at once | Frame p50 per process | Wall for the batch | Throughput vs one |
|---|---|---|---|
| 1 | 2.62 s | 27.0 s | 1.0× |
| 2 | 4.86 s / 7.63 s | 45.0 s | 1.2× |
| 4 | 5.5-8.1 s | 51.9 s | 2.1× (includes startup overlap) |

- PNG SHA-256 identical in every process: parallel runs do not change bytes.
- The 4-frame batch flatters parallelism through startup overlap. Real long captures did worse:
  - 2-way, `Presentation.night-service`: one process took about 800 s against about 260 s alone. The test failed at 937.9 s.
  - 4-way: the `ctest-gpu-guard` stopped every shard after 900 s without ctest output.

## MEASURED

**Before** is the `cl-glmdepth1` `ctest -L gpu` log (`cl-glmdepth1-evidence/ctest-gpu.log`), serial, one job.

Discrepancy with the brief, counted from the same log:
- 1358 completed lines, 1337 parsed, sum 9692 s, with "Total Test time (real) = 9831.95 sec".
- 41 tests over 60 s, summing to 7869 s.
- The brief's 6239 s / 27 tests / 4717 s do not match this log. I use the log's numbers.

Top 27 by time, before:

| # | Test | Before (s) | After (s) |
|---|---|---|---|
| 1 | Presentation.night-service | 525.8 | 937.9 **Failed** (2-way, budget 900 s) |
| 2 | RendersFourStatesAtFourAnglesDeterministically (native duplicate) | 522.2 | not run by the split runner |
| 3 | Presentation.late-afternoon | 443.5 | not completed |
| 4 | Presentation.overcast | 434.0 | not completed |
| 5 | Presentation.midday | 433.8 | not completed |
| 6 | GlyphSdf.RtlProof.Pair | 220.0 | not rerun |
| 7 | ExposureRoom.walk | 180.6 | not rerun |
| 8 | GlyphSdf.AnalyticVsVoxel256.Yaw30 | 170.9 | not rerun |
| 9 | GlyphSdf.AnalyticVsVoxel | 158.2 | not rerun |
| 10 | State.night-service | 140.7 | not rerun |
| 11 | State.overcast | 124.2 | not rerun |
| 12 | State.late-afternoon | 118.3 | not rerun |
| 13 | State.midday | 117.3 | 234.5 (queued job `base-state-midday`, same code; box load differs) |
| 14 | GlyphSdf.qps-rtl-embossed | 111.6 | not rerun |
| 15 | GlyphSdf.qps-rtl-carved | 111.5 | not rerun |
| 16 | GlyphSdf.latin-embossed | 109.0 | not rerun |
| 17 | GlyphSdf.latin-carved | 101.3 | 103.6 (profile run) |
| 18 | GlyphSdf.AnalyticVsVoxel128 | 84.2 | not rerun |
| 19 | GlyphSdf.AnalyticVsVoxel128.Yaw30 | 80.1 | not rerun |
| 20 | GlyphSdf.latin-carved.ColdBound | 76.1 (Failed in the log) | not rerun |
| 21 | SinglePass.night-service (native duplicate) | 74.0 | not run by the split runner |
| 22 | StoredSdf.Brickmap | 67.6 | not rerun |
| 23 | HeadlessCornellGraph.Production...WallSeams | 66.6 | not rerun |
| 24 | SdfFilter.NonText256.Yaw30 | 62.6 | not rerun |
| 25 | SinglePass.midday (native duplicate) | 61.5 | not run by the split runner |
| 26 | SdfFilter.NonText128.Yaw30 | 60.9 | not rerun |
| 27 | SdfFilter.NonText256.Yaw0 | 60.8 | not rerun |

(The State.midday "after" of 234.5 s is the whole two-process test, so the comparable "before" figure is 117.3 s per the log.)

- **There is no per-test speedup to report.** The only behavioural gain available without changing pixels or weakening assertions is the shard-planning change, and it shows up as wall clock, not per-test time. Per-test times under concurrency get worse (see above).
- **Whole `-L gpu` wall time:**
  - Before: 9832 s (log). 2 h 44 min of test time; the brief's 1 h 45 min is the queue wall, not reconcilable from this log.
  - After: **not measured.** The full 1378-test tree is not built in this worktree (only `vixen_lookdev_capture`, 669 targets).
  - Look-dev subset, planned, 4 shards, cost-weighted: tests per shard 7 / 11 / 7 / 11, cost sums 1037 / 1034 / 1037 / 1033 s against a 4141 s serial sum. The ideal is 1035 s per shard.
  - Executed: 4-way stopped by `ctest-gpu-guard` at 1531 s (evidence `after-4way-guard-stopped/`); 2-way killed by me at about 1040 s after the first Presentation test failed its 900 s suite budget (938 s) (evidence `after2/`). Neither completed.
- **Estimated, not measured:** 6-shard round-robin makespan 2957 s against 1486 s for the cost-weighted plan, computed offline from the log's per-test times with no contention. The real gain is capped by GPU saturation.

Frame-count study (`frames/`, all 4 states, 4 angles, default 8 frames vs 4): angle 0 identical, angles 1-3 DIFFER in every state. Per-file hashes are in `frames/run.log`.

## LANDABLE NOW

Branch `lane-lookdevfast1`, commit `0f37c418` (VIXEN only):

1. `tools/shard-ctest-tests.py`: independent tests are placed longest-first on the least-loaded shard by their `COST` property (unset counts as 1 s). Fixture or dependent tests still go to shard 0, which now counts toward its load. `tools/tests/test_shard_ctest_tests.py` updated, with a new test that costly cases spread one per shard. `python3 -I tools/tests/test_shard_ctest_tests.py`: 2 tests OK.
2. `VIXEN/application/main/tests/test_lookdev_capture.cmake`:
   - `COST` (serial seconds measured on Dozen) on the 34 slowest look-dev captures.
   - Appends family labels `lookdev-states`, `-presentation`, `-tonemap`, `-exposure`, `-glyphsdf`, `-sdf-bake`, `-reconstruction`.
   - Checked after reconfigure: label counts 9 / 4 / 34 / 3 / 10 / 10 / 8; `COST` and `LABELS` visible in `ctest --show-only=json-v1`.
   - No capture command, environment, frame count or assertion changed; bytes are untouched.
3. `VIXEN/application/main/tests/lookdev-affected-by.tsv`: source prefix to families.
   - This is the answer to task 4 (below). The mapping is **hand-declared and unverified** (inferred from file names); it should be generated from each capture process's loaded shader and library closure.

**Task 4: can ctest select only the affected captures?** Yes, now at family granularity: `ctest -L '^gpu$' -L '^lookdev-glyphsdf$'` and so on (a `-L` regex ANDs repeated labels). Serial cost per family from the glmdepth1 log:

| Family | Tests | Serial s | Share of the 9692 s |
|---|---|---|---|
| lookdev-presentation | 4 | 1837 | 19.0% |
| lookdev-states | 9 | 1277 | 13.2% |
| lookdev-glyphsdf | 9 | 1147 | 11.8% |
| lookdev-sdf-bake | 10 | 583 | 6.0% |
| lookdev-tonemap | 32 | 458 | 4.7% |
| lookdev-exposure | 3 | 275 | 2.8% |
| lookdev-reconstruction | 8 | 130 | 1.3% |
| all look-dev | | 5707 | 58.9% |

A change to `libraries/Text` runs about 1147 s instead of 9692 s. Changes to the shared renderer (`BodyInstanceRayMarch.comp`, `BuildRenderGraph.cpp`, ...) map to `*`, the whole set. The selector itself (diff to labels) is **not** written: R812/R821 forbid new script decision logic, so it belongs in managed code (see CONSOLIDATION ISSUES). Planet/other captures have no families yet.

Caveats on what landed:
- (a) The shard-planner edit is Python policy in an existing tool, so it is R812-borderline.
- (b) The `COST` values are static numbers from one Dozen run: a relative weight for ordering, not a timing claim. They will drift.
- (c) GPU shards were not exercised to completion.

## REDS

- Mine, from running the code on this lane:
  - `Application.LookdevCapture.Presentation.night-service` **Failed** at 937.9 s under 2-way sharding: "Look-dev capture exceeded its runtime budget: 938 s > 900 s". The `LOOKDEV_CAPTURE_BUDGET_S=900` assertion in `run_lookdev_capture.cmake` assumes serial speed. I did not change it (not weakening).
  - 4-way: `ctest-gpu-guard` stopped all 4 shards ("no ctest output for 900s") during `Presentation.*`. Default `VIXEN_GPU_GUARD_IDLE_SECONDS` is 900.
  - Neither is a bug in the change; they say that more than 1-2 concurrent lookdev shards on this box exceed the existing budgets.
- Pre-existing in the glmdepth1 log (9 failures; I did not recheck them on the base):
  - `EditorToggleUndoCapture` ×5
  - `HeadlessUiGraph.RendersAndReadsBackAnOffscreenFrame`
  - `LookdevCapture.SaturationChannel.system` and `.reference`
  - `GlyphSdf.latin-carved.ColdBound`

## STOPs

1. **`velocity-1` merge conflicts with `origin/wave/authoring-convergence`** in `VIXEN/codegen/CMakeLists.txt` (the kernel pin line). The rule says report, do not resolve. I aborted the merge; this branch is **not** on the velocity tip.
2. **Owner or controller decisions, none made here:**
   - (a) Runtime budgets. 900 s per look-dev suite run and the 900 s guard idle limit assume serial speed. Parallel shards need either a per-shard-width budget or a budget based on frames rather than wall seconds. Weakening them is not mine to do.
   - (b) GPU-yield tiling in captures. Each 500×500 frame is 81 tiles at a 2 ms budget; measured tiles are 21-25 ms; wall per frame exceeds GPU time by about 1.2 s. `VIXEN_GPUYIELD_EDGE` pins the tile edge ("a diagnostic, not a production control"). Whether captures may pin a larger edge, and whether that is byte-identical, is **unmeasured**: job `cl-lookdevfast1:tile-edge` (script `edge/run.sh`: edge default / 32 / 16 / default, same PNG hash check) was queued behind CPU pressure the whole time I could wait. If it landed, its output is in `edge/run.log` and not in this report.
   - (c) Drain frames. `exitAfterFrames = frames + 2` renders two full extra 1080p frames (about 36 s of 103 s in the 1-frame glyph tests, about 5 s in a State process). Making them cheap would change the readback handoff the comment says is deliberately exercised.
   - (d) The 776 s `native` duplicates and the 1096 s `PlanetCapture.RendersStatesDeterministically` aggregate: lanes should use `tools/run-tests-split.sh`, which excludes `native`, not a bare `ctest -L gpu`. The lane-rules wording ("ctest ... through the queue") does not say this.
3. **Queue wait dominated my session**: every GPU job waited 15-45 min for admission (CPU pressure, load average 45-50 on 32 cores), which is why several experiments are incomplete.

## CONSOLIDATION ISSUES

- Action: sharded run `run-tests-split.sh --gpu-jobs 4`. Failure: guard stopped at 900 s idle on `Presentation.*`. Remedy tried: `VIXEN_GPU_GUARD_IDLE_SECONDS=2400` and `--gpu-jobs 2`. Gate that should exist: a per-test expected-duration (from `COST`) feeding the guard idle limit and the suite budget, so parallel width does not turn into false reds.
- Selection for "which captures does this diff affect" needs a managed-code selector fed by a generated manifest (shader and library closure of each capture process), not the hand-declared `lookdev-affected-by.tsv`.
- Lane rules should point VIXEN GPU runs at `tools/run-tests-split.sh` explicitly.
- No SPT proposals were filed (queue waits used the session); the three items above are the proposals to file.

## BENCH REQUEST

On native Windows (ws), where timing counts: the per-frame cost split (GPU span vs wall) and the `VIXEN_GPUYIELD_EDGE` experiment, to see whether the 81-tile overhead exists off Dozen. Exact command is `edge/run.sh` in the evidence directory. Dozen numbers above are correctness-only and non-representative.
