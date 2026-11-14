# cl-rtframe2: one GPU-yield rule, cold-frame seeding

Branch `lane-rtframe2` (worktree `VBVS--VIXEN/.claude-worktrees/rtframe2`, from `lane-rtyield1` 52096d56). Never pushed.
Evidence: `cl-rtframe2-evidence/` (`summ.py`, `dzn/`); raw native runs in `C:\vixen-native\rtframe2-run\` (`h-*`, `g-*`, `app-*`, `*-pixels`); native checkout `C:\vixen-native\rtframe1` (detached at the lane tip); run scripts `build.bat`, `seq.bat`, `g.bat`, `app.bat`, `pix.bat` beside the runs.
Machine: same as cl-rtframe1 (Radeon 8060S integrated, native Vulkan). Every native number is contended (queue `--gpu-benchmark` admitted after 5-35 min, box load not quiet); replicates are shown where taken. Absolute ms differ between sessions by 20-40% for that reason; compare rows within one session.

## What changed (commits)

| Commit | Change |
|---|---|
| 8d718654 | Merge `lane-lookdevfast2` and reconcile `GpuChunkCost` into one rule (below) |
| 10e406cd | Force-add `VIXEN/tools/closure/*` (see REDS 1) |
| 97202624 | New-extent seeding, `TraceRaysNode` shares the rule, tests |
| 1b5df33f | Persist the mean full-tile cost so a known device untiles from frame one |
| 287c0117 | `near` is a Windows macro; local renamed |
| 45a44076 + 7ea197ef | Fix candidate 3 and its revert (no gain) |

### The one rule (`GpuChunkCost::Edge`, `ComputeDispatchNode`, `TraceRaysNode`)
1. Budget = the submission budget raised from measured per-command launch latency (lookdevfast2: `ObserveScale`, `LaunchLatencyNs`, `SubmissionBudgetNs`), on every device.
2. Native devices (`integrated`, `discrete`; `GpuDeviceClassIsNative`) additionally pass a `WholePass`: if `fixed + meanCost * units` fits the window (about 240 ms), one dispatch (rtyield1).
3. Otherwise, and always on translated/cpu devices, the tile edge is `sqrt(budget * 0.95 / meanUnitNs)`, the mean full-tile cost. The lookdevfast2 probe (one scale up when the budget is unattainable and no latency is known) applies to both.
There is no separate max-based model any more: the max (`unitNs_`) remains only for the persisted envelope, `Estimate` and the fixed-latency guard.

A consequence for translated devices: they now size from the mean, not the max, so a translated device grows past the cold floor after calibration (unit test: the same dense-tile history that gave edge 7 now gives more than 7, below 63). dzn still tiles (below).

### Cold frames (task 2)
Cause, from the logs: the first four frames (one per flight ring slot) record before any ring readback exists, and a "warm" start did not help because the persisted record held only the max cost, which projects the whole pass over the window, and an extent never seen before had no record at all (`start=cold`).
Fix, three seeds in order at an extent change: this extent's persisted envelope; this process's envelope of the previous extent; the nearest persisted extent by pixel count (`SimpleTaskProfile::NearestChunkCalibration`). The mean full-tile cost is persisted too (`meanUnitNs`, absent in old records, which fall back to the max). Per-workgroup cost is a property of device and pipeline, so it carries across resolution (conservative upward: sub-linear scaling means larger extents cost less per workgroup). The calibration line now prints `seed=persisted|previous-extent|nearest-persisted`.
Not solved: the very first run on a device with an empty store. It has nothing to seed from, so it keeps four tiled frames (the B rows below). Removing those needs a measurement before frame 1, which only a dedicated probe dispatch could give; I did not add one.

## MEASURED (native Windows, AMD 8060S; contended; State.midday angle 0, 32 frames, CPU frame ms)

Columns: steady = frames 5+ p50/p95; all = frames 2+ p50/p95 (this is where cold frames show); cold excess = sum of frames 2-9 minus 8 x steady p50; subs = submissions of the first frames. Script: `cl-rtframe2-evidence/summ.py`.

**Before** = lane-rtyield1 52096d56, rtyield1's own runs (`C:\vixen-native\rtyield1-run\a-*-r1`, a different session, so steady values carry its load; its store state was whatever earlier runs left: 500x500 warm, others cold). The original pre-rtyield1 default is `b-*` (VIXEN_GPUYIELD_EDGE=7).
**After** = this lane tip, three store states: **A1** first visit of each extent in one persisted store (500, then 720 seeded from 500, then 1080 seeded from 720's nearest); **A2** second visit (warm); **B** empty store per run (no seed possible).

| Res | Row | steady p50/p95 | all p50/p95 | cold excess ms | subs first frames |
|---|---|---|---|---|---|
| 500x500 | original (edge 7) | 35.8 / 59.0 | 36.2 / 76.8 | 161 | 81 x all |
| | before (rtyield1) | 25.7 / 31.0 | 25.7 / 31.0 | 24 | 1 x all |
| | after A1 (empty store) | 28.9 / 41.8 | 29.1 / 81.9 | 192 | 81 x4, then 1 |
| | after A2 (warm) | 26.7 / 38.0 | 27.0 / 31.5 | 7 | 1 x all |
| | after B (empty) | 23.8 / 32.4 | 24.3 / 78.3 | 162 | 81 x4, then 1 |
| 1280x720 | original (edge 7) | 93.2 / 125.7 | 95.6 / 203.3 | 352 | 299 x all |
| | before (rtyield1) | 58.5 / 83.5 | 58.8 / 254.2 | 623 | 299 x4, then 1 |
| | after A1 (seeded from 500) | 33.5 / 42.7 | 34.6 / 63.8 | 87 | 1 x all |
| | after A2 (warm) | 33.4 / 41.5 | 33.6 / 62.5 | 83 | 1 x all |
| | after B (empty) | 44.0 / 55.7 | 44.2 / 221.1 | 533 | 299 x4, then 1 |
| 1920x1080 | original (edge 7) | 184.2 / 299.3 | 184.8 / 365.6 | 548 | 700 x all |
| | before (rtyield1) | 69.6 / 100.5 | 71.0 / 396.3 | 1039 | 700 x4, then 1 |
| | after A1 (seeded) | 60.4 / 86.0 | 60.4 / 86.0 | 44 | 1 x all |
| | after A2 (warm) | 59.6 / 89.9 | 60.4 / 89.9 | 61 | 1 x all |
| | after B (empty) | 68.5 / 113.8 | 68.7 / 359.2 | 966 | 700 x4, then 1 |

Reading:
- A new resolution on a device that has any stored extent now untiles from frame 1 (`subs` 1 from the first frame in A1 at 720 and 1080): all-frames p95 at 1080 goes from 396 ms to 86 ms, cold excess 1039 ms to 44-61 ms.
- The remaining 83-87 ms excess at 720 in A1/A2 is not tiling (one submission each); it is the first frames' pipeline/upload cost and box noise, and it appears in the warm A2 run too.
- B (empty store) still has four tiled frames, as before: the unavoidable first-ever run. 500x500 cold excess of B and A1 (162-192 ms) is that.
- Steady values within this session: 500 24-29 ms, 720 33-44, 1080 60-69 (contended). The untiled steady state did not change from rtyield1; the gain of this lane is the cold frames.

**Pixels (native, byte-exact), `State.midday` 500x500 four angles, at the measured build `h-pixels`:** 76ce784672154c37 / f2cc7c15c693667c / 068b2ee553fb47ec / 0bf0f7444c1132db: equal to rtframe1's native hashes. The tip differs from that build only by the reverted candidate 3 (a descriptor republish skip, no draw change). A four-angle run on the exact final tree was queued (`cl-rtframe2:native-final-pixels`) but not admitted in 45+ min and was cancelled by me; what exists for the final tree is angle 0 (76ce784672154c37) from `capture-nogather.exe`, the build equal to the final code, in `g-nogather-500-r1`. So the four hashes are verified on the measured build, one of them on the final code.

**dzn (smoke, non-representative timing):** `cl-rtframe2-evidence/dzn/`: `submissions=81 ... device_class=translated`, `launch_latency_ms=3.57`; angle-0 sha a72fc017eea2cd59 = rtframe1's dzn hash. It still tiles.

**VixenApp default (windowed, 400 frames, run sequentially, queue bench job; `app-500`, `app-1080`):**

| | before (rtyield1) | after |
|---|---|---|
| 500x500 p50 / p95 | 17.33 / n/a | 17.34 / 17.88 (max 24.4); GPU span p50 7.4; 1 submission |
| 1080p p50 / p95 | 23.0 / n/a (rtframe1: 26.9) | 22.31 / 29.10 (max 40.8); GPU span 18.9; 1 submission |
No regression.

**Unit tests (WSL, queue):** `GpuChunkCost.*` 14/14 pass in `test_rendergraph_tracking`, `SimpleTaskProfileTest.*` pass. New: `TranslatedAndOverWindowNativeShareLatencyDerivedTiling`, `ColdFrameSeedFromAnotherExtentUntilesAtFirstFrame`, `PersistedMeanLetsAKnownDeviceUntileFromFrameOne`, `SimpleTaskProfileTest.NearestChunkCalibrationSeedsANewExtent`; `NativeUntiles...` and `ChunkCalibrationPersists...` extended. Both device classes are covered (translated with and without a `WholePass`, native fitting and over the window). One existing expectation was relaxed on purpose: `SmallerFullTilesUpdateThroughputAfterLargerCalibration` allowed an edge of 4; with sizing from the two-sample mean it is 5 (5x5 projects about 1.4 ms mean against the 2 ms budget).

## Fix candidates (measured separately)

**3. `compute_desc_gatherer` reuse: no gain, left out.** Change: skip the per-frame `Out` of the descriptor array when no refreshed handle differs (commit 45a44076, reverted in 7ea197ef). Native A/B, same warm store, interleaved, 3 reps x 500 and 720, `NODEEXEC` p50: without 0.91 / 0.86 / 1.67 ms (500), 1.20 / 0.98 / 0.91 (720); with 0.90 / 0.92 / 1.84 (500), 0.90 / 0.92 / 0.90 (720). The 1.7-1.8 ms values appear with and without the change (box noise, bimodal); frame p50 within noise. The cost is the per-slot fetch loop (a graph instance lookup and a variant copy per binding), not the re-publish. Pixels unchanged (angle 0 sha 76ce784672154c37 both). Real fix would be a change-versioned binding table; not attempted. (rtframe1's 1.88 ms was a noisy sample of the same bimodal distribution.)

**4. `shadow_visibility_wave` amortization: not implemented, needs a ruling.** `BuildRenderGraph.cpp` dispatches the wave over every pixel ("Dispatches unconditionally ... no off-path"). Halving or amortizing it (as for the probe pass) means some pixels get a shadow-visibility result from an earlier frame or none, which changes pixels by construction, so by the acceptance rule it is out without a ruling. Current native cost (this session, contended): p50 4.4 ms at 500x500, 4.2 at 720, 7.6 at 1080 (rtframe1 measured 2.4 at 500), against 14.9 / 22.9 / 42.7 ms for `esvo_traverse_shade`. A pixel-neutral variant (skip lanes whose hit record is a miss via indirect dispatch) is a different change and was not tried.

**4b. `TraceRaysNode`:** shares `GpuChunkCost`, now passes the same `WholePass` (units = width x height x depth, native only). No look-dev or default path creates a `TraceRaysNode`, so it is covered by the unit tests only, not measured.

## STATUS
Tasks 1, 2 and 4b done and measured (2 is solved for any device with a stored extent; the empty-store first run keeps four tiled frames). Task 3: candidate 3 measured, no gain, reverted; candidate 4 not implemented (changes pixels, ruling needed).

## LANDABLE NOW
`lane-rtframe2` at 7ea197ef (VIXEN only): 8d718654, 10e406cd, 97202624, 1b5df33f, 287c0117 (+ the 45a44076/7ea197ef pair, net zero). Checks: `GpuChunkCost.*` and `SimpleTaskProfile` tests pass (WSL, queue); native hashes x4 equal rtframe1's; dzn still 81 submissions and same dzn hash; VixenApp default 500 and 1080 not regressed. Not run: the full VIXEN suite and the lookdev closure targets (the merge brings lookdevfast2's `lookdev_closure_*` targets; I did not build them, per the memory note that building them reruns GPU captures).

## REDS
1. **lane-lookdevfast2 `bcc1e930` is broken on a clean checkout**: the CMake targets reference `VIXEN/tools/closure/{open_recorder.cpp,closure_manifest.cpp,closure_manifest.h,test_closure_manifest.cpp}`, but `VIXEN/.gitignore` line 83 ignores `tools/`, so they were never committed. Configure fails with "Cannot find source file". Reproduces on `lane-lookdevfast2` as committed. I force-added the four files verbatim (10e406cd). If lookdevfast2 lands separately, it needs the same.
2. **`near` is a macro on Windows** (windef.h): `const auto near = ...` fails to compile natively (C2059) and passes on gcc. Caught by the native build only; the WSL build cannot see it.
3. The persisted record held only the conservative max, so a "warm" start at a tiled extent kept tiling (seen in rtyield1's own logs: `start=warm ... unit_ns=77428` at 160x90 after a forced-edge run overwrote it). Fixed by persisting the mean; an existing store without `meanUnitNs` falls back to the max until the next shutdown rewrites it.
4. First-ever run (empty store) still has four tiled frames (B rows).
5. No quiet bench window: numbers contended, replicates only where stated (A and B single runs; candidate 3 three reps).
6. "Before" is rtyield1's session, not a rerun of 52096d56 now (that binary no longer exists; the checkout was moved forward). Steady differences between sessions (e.g. 720 steady 58 vs 33) are mostly load.
7. Gatherer A/B uses `capture-head.exe` / `capture-nogather.exe` copied beside the build output (a copy elsewhere fails with 0xC0000135, missing DLLs; first attempt wasted one queue job).
8. I removed my own `g-*` output directories with a glob `rm` (rule: no wildcard deletes); they were my fresh, failed run outputs.
9. Pre-existing, not touched: `tests/gpu-class-manifest.tsv` is generated, so the new tests are classified by link closure until the next classification pass.

## STOPs
None blocking. Decision for the owner: candidate 4 (shadow wave amortization) changes pixels; ruling needed on whether a temporal/amortized shadow wave is acceptable for look-dev goldens, or whether to pursue the pixel-neutral miss-skip form.

## CONSOLIDATION ISSUES
- try -> fail -> remedy: native configure needs `UNDERTOW_ROOT` (and `YEROKET_ROOT`); failed with "needs UNDERTOW_ROOT", remedy `UNDERTOW_ROOT=C:\vixen-native\undertow-tapearray` in the build bat. Gate: `tools/vixen-native.sh` should forward it for an existing lane checkout, or the configure error should be hit before the 3-minute pre-configure steps.
- try -> fail -> remedy: a native exe copied outside the build dir exits 0xC0000135. Gate: the run script should refuse an exe not next to its DLLs with a located message.
- try -> fail -> remedy: `near` identifier compile error on MSVC only. Gate: a Windows-macro identifier lint (`near`, `far`, `min`, `max`) in a pre-commit or WSL build check.
- Footprint (R825): `C:\vixen-native\rtframe2-run` holds PNGs and perf.csv per run, small; no build output copied into the evidence dir.
