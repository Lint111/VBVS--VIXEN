# FSR 3 run 3 (R604, sn-fsr3c): whole-frame cost, test witness, exception-free fallback

Lane `fsr3`, branch `lane-fsr3`, started at `c9775cef`. Nothing pushed. Run 2 content (selection fallback and native witness) is in `reports/` history and in `native-run2/` evidence; the run 2 table is superseded by the tables below.

## Task 1: unit test witness (MEASURED)

Counts are identical before (`c9775cef`) and after (`adb8864c`). WSL builds and native builds: 0 warnings.

| Target | Result before | Result after |
|---|---|---|
| `test_upscaler_selection` (direct) | 7/7 | 7/7 |
| `test_vulkanresources_basic` (direct) | 14 run, 13 passed, 1 skipped | same |
| `test_hud_view` | 1/1 | 1/1 |
| `test_app_run_tick` | 6/6 | 6/6 |
| ctest `-R 'ViewBlobEquiv\|Application.UpscalerSelection\|AppRunTick\|HudView\|VulkanResources_CapabilityGraph'` | 25 passed, 0 failed, 1 skipped | same |

- The skipped test is `VulkanResources_CapabilityGraph.ConfiguredDeviceIdentityRejectsChangedDeviceOrDriver` (needs a configured device identity).
- `test_view_blob_equiv` run directly from `build/wsl` fails 2 of 3 on the working directory. It passes under ctest, before and after. Not a code change.
- The test target does not link `VixenApp`, so the Prepare-level fallback has no unit test. It is covered by the native forced-failure run below.

## Task 2: where the whole-frame cost goes

### What was added

- `Fsr3Upscaler.cpp`: CPU timing around `Record` and around `ffxFsr3UpscalerContextDispatch`, printed at cleanup as `[FSR3_CPU_TIMING] record|context_dispatch samples= median_ms= max_ms=` (commit `43066907`).
- Per-pass GPU columns in `perf.csv` already populate on this build after the first frames (`esvo_traverse_shade_ms`, `shadow_visibility_wave_ms`, `reconstruction_depth_ms`, `fsr3_temporal_ms`, `whole_frame_gpu_span_ms`). **Correction:** run 2 said they were 0; that was reading the first rows. The bug is in the reading, not the build. `reconstruction_history_seed_ms` was always 0 (64 of 64 rows); fixed in run 4 below.

### Method (MEASURED)

Native Windows Vulkan, `AMD Radeon(TM) 8060S Graphics`, profile `amd-8060s-windows`, MSVC Release, clone `C:\vixen-native\fsr3` at `adb8864c`. Test `LookdevCapture.RendersFourStatesAtFourAnglesDeterministically`, state midday, angle 0, 64 frames, fixed exposure, HUD suppressed. Each run is its own queue job (`--resource gpu`, label contains `native-timing`, no `--exclusive`). Medians are over the second half of the frames (32). Variants:

- `native`: `--render-scale`, default upscaler.
- `fsr3`: `--upscaler=fsr3`, qualification capture, GPU timing.
- `fsr3nopick`: as `fsr3`, with the shader's `TraceWorld` native pick replaced by a miss (diagnostic copy in `.tmp/diag`, original restored after every run).
- `rd`: `VIXEN_RECONSTRUCTION_DEPTH=1` only. `jp`: jitter probe. `jpnopick`: jitter probe plus no pick.

### Host-noise caveat (MEASURED)

Other lanes were building on this machine while these ran. The 8060S is an APU that shares memory bandwidth and CPU, so identical native runs moved between 74 and 196 ms at 1080p. Only same-series paired ratios are used. The `f1080-23` series (A/B/A/B/A/B/A) shows the effect: native 85, 136, 196, 146 ms in one sequence. Its `fsr3nopick` run has an FSR3 GPU median of 3.2 ms against 1.5 ms elsewhere, so it is contaminated and not used as evidence.

### A/B/A table: whole-frame CPU p50 (ms) and march pass p50 (ms)

| Output / scale | A1 native | B fsr3 | A2 native | B / mean(A) | fsr3nopick |
|---|---:|---:|---:|---:|---:|
| 500x500 @ 2/3 | 25.50 (march 18.4) | 56.72 (46.6) | 26.40 (17.7) | 2.19x | 33.59 (24.7) |
| 500x500 @ 1/2 | 17.15 (7.1) | 53.01 (46.4) | 17.13 (8.1) | 3.09x | 28.85 (20.3) |
| 1080p @ 2/3 (series d1080) | 107.92 (91.8) | 241.92 (223.0) | 103.57 (88.0) | 2.29x | 97.53 (82.5) |
| 1080p @ 1/2 | 74.21 (61.9) | 245.66 (227.2) | 70.58 (61.0) | 3.39x | 134.50 (112.0) |

- 1080p @ 2/3 was also run as `f1080-23` (native/fsr3/native/nopick/native/fsr3/native). Its fsr3 frames are 235 and 283 ms against native 85, 136, 196, 146 ms. The ratio ranges 1.2x to 2.8x depending on which native neighbour is used, which is why the d1080 series, where neighbours agree within 4%, is the one tabulated.
- Bar (FSR3 frame at 1080p @ 2/3 no slower than native, paired A/B/A medians): **not met.** FSR3 is 2.3x native in the cleanest series.

### FSR3 GPU time against the 2.0 ms budget (MEASURED, `[FSR3_GPU_TIMING] summary`, 64 samples)

| Output / scale | GPU median | GPU max | Within 2.0 ms |
|---|---:|---:|---|
| 500x500 @ 2/3 | 0.296 ms | 0.528 ms | yes |
| 500x500 @ 1/2 | 0.239 ms | 0.475 ms | yes |
| 1080p @ 2/3 (d1080) | 1.579 ms | 3.278 ms | median yes, max no |
| 1080p @ 2/3 (f1080-23, two runs) | 1.492 / 1.763 ms | 3.275 / 3.294 ms | median yes, max no |
| 1080p @ 1/2 | 1.590 ms | 2.801 ms | median yes, max no |

Run 2 reported 1080p max values of 1.997 and 1.383 ms. In this run the 1080p max is 2.8-3.3 ms in every fsr3 run. The max is one or two outlier samples per 64 and the host was loaded (INFERRED: contention, since the `fsr3nopick` run with timing 1.3-1.9 ms and max 1.6-2.9 ms shows the same spread), but the budget is exceeded on the worst frame at 1080p and I am not claiming it passes there.

FSR3 CPU cost (MEASURED): `record` median 0.17-0.26 ms, `context_dispatch` median 0.16-0.25 ms, max 0.44 ms (one 6.2 ms max in the contaminated run).

### Where the extra time goes (MEASURED)

The FSR3 frame delta is in one pass: `esvo_traverse_shade` (the march). At 1080p @ 2/3 (series d1080), march p50 is 223.0 ms for fsr3 against 91.8 / 88.0 for native: +133 ms of a +134 ms frame delta. Shadow (4.3 vs 4.8), reconstruction depth (0.53), FSR3 temporal (1.55) and the CPU record/dispatch (0.35 together) are all under 2% of the delta.

Decomposition at 1080p @ 2/3 (march p50, same series):

| Variant | march p50 | frame CPU p50 |
|---|---:|---:|
| native | 91.8 / 88.0 | 107.9 / 103.6 |
| rd (reconstruction inputs only) | 113.0 | 128.0 |
| jpnopick (jitter, no pick) | 113.2 | 135.8 |
| jp (jitter + pick) | 217.6 | 235.4 |
| fsr3 | 223.0 | 241.9 |
| fsr3nopick | 82.5 | 97.5 |

- Cause named: the jitter path (`jitterEnabled`, which `--upscaler=fsr3` turns on) runs `writeNativePick`, a second full `TraceWorld` per pixel at the **presentation** extent with the native ray footprint, on top of the jittered primary trace. Removing it takes the fsr3 march from 223 to 82.5 ms and the frame from 242 to 97.5 ms at 1080p @ 2/3 (frame at native parity). Adding it back without FSR3 (`jp` vs `jpnopick`) costs +104 ms, so FSR3 itself is not the cause.
- `fsr3nopick` is not an apples-to-apples native comparison: native keeps its (cheap, native-resolution) pick, and the diagnostic removes the pick entirely.
- Removing the pick explains about 70% of the delta at 500x500 and 1080p @ 1/2 (500 @ 2/3: 23 of 31 ms; 500 @ 1/2: 24 of 36 ms; 1080p @ 1/2: 111 of 172 ms) and all of it at 1080p @ 2/3.
- Residual (MEASURED existence, INFERRED cause): `rd` alone adds about 23 ms of march at 1080p @ 2/3, so enabling the reconstruction inputs path costs something by itself. At 1/2 the residual is larger in relative terms (500x500: march 20 vs 7-8 ms). The march dispatch is sized from the presentation target when jitter is on, so the primary trace may also be running at output extent rather than render extent. Not measured separately.

### Why this is a STOP, not a fix

`writeNativePick` is a documented design choice (deterministic native-resolution picking independent of the upscaler). The costly part is its dispatch extent and per-frame execution, which is a design question outside the upscaler module and its graph wiring. Candidate directions, none implemented: pick on demand (only when a pick is requested), reuse the primary hit when scale is 1, or restrict the pick to a small region around the cursor. Filed.

## Task 3: exception-free fallback (R213)

- `UpscalerProviderInitializationError` is removed. `UpscalerProviderInitializationResult { providerId, initialized, message }` is returned by the FSR3 factory hook (`Fsr3ContextState::Initialize`), forwarded by the module's compile hook through `providerInitializationReported`, stored in `reportedUpscalerInitialization_`, and checked by `Prepare()`.
- Context setup now returns failure text instead of throwing (`InitializeContext`, `CreateSharedResources`, `CreateSharedImage`, `FfxFailure`). The per-frame `Record` path was converted in run 4 below.
- Same fallback order (FSR3, FSR1, native), at most 3 failures, same Warning text, same 7 tests (`test_upscaler_selection` now builds the result struct instead of throwing).
- Exhausted fallback no longer throws: `Prepare()` sets `lastError_`, logs an Error and returns with `isPrepared=false`.

### End-to-end witness (MEASURED, native 8060S)

A temporary clone-only edit made `InitializeContext` fail. `--upscaler=fsr3`, 500x500, 8 frames. The first attempt shows (evidence `forcedfail-fallback-run.log`):

- `Prepare Upscaler provider 'fsr3' failed context initialization: FSR 3.1.4 context initialization failed: forced FFX context creation failure (temporary validation); trying the next provider; suggest a device variant that includes 'Upscaler:Fsr3.1.4'`
- retry: `BuildRenderGraph Upscaler provider 'fsr3' rejected: not initialized; trying the next provider`, FSR1 selected, 1 test passed, exit 0, no exception.

Finding: unmodified, the retry aborts. `proceduralRecipes_` is an app member that survives the engine reset, and the look-dev harness registers recipes inside its `BuildRenderGraph` override, so the second attempt gets `DuplicateId` and `Prepare()` returns the error cleanly ("could not register look-dev material recipe 100 (status 1)"). The run above needed a second temporary harness edit that tolerates `DuplicateId`. This is a recipe lifecycle decision (clear vs repopulate, and the engine-lifetime recipe cacher is not repopulated either), so I did not change it. Both temporary edits were reverted by exact path from backups in `.tmp/diag`, the clone's tracked files match `adb8864c` (only the earlier generated-header changes remain), and it was rebuilt clean.

## Run 4 (sn-fsr3d): per-frame Record without throws, history-seed column

Commits `23bef8b7` (T-4635) and `44995604` (T-4637), on top of `b600a5ef`. `origin/main-wave` does not exist in this clone; `origin/main` is already an ancestor, so there was nothing to merge.

### T-4635: Record returns a status (R213)

- `ComputeStageNode::ExternalRecorder` now returns `std::string` (empty = OK) (`ComputeStageNode.h:42`).
- `Fsr3ContextState::Record` returns failure text for the uninitialized context, bad inputs, oversized extent and the FidelityFX dispatch failure; `CheckFfx` is removed (`Fsr3Upscaler.cpp:43-126`). The hooks' record lambda forwards the text.
- `ComputeStageNode::RecordComputeCommands` external branch reports a recorder failure, a missing output image, or a `vkEndCommandBuffer` failure through `RenderGraph::ReportGpuFault` (`ComputeStageNode.cpp:324-353`). The command buffer is always ended first, so the submit that follows is valid. The frame then fails with the latched typed `VkResult` (`VK_ERROR_UNKNOWN` for recorder text), exactly like the existing submit-failure path, with one logged error and no exception. Six `throw` sites are gone; none added.
- Not done (STOP below): handing the latched fault back to `Prepare()`'s provider fallback loop mid-run. `Render()` still maps a failed `RenderFrame` to `return false`, which is what the old exception reached too.
- Not touched: `vkBeginCommandBuffer` throw at `ComputeStageNode.cpp:307` is shared by every compute stage, not the FSR3 path.
- Test: `test_fsr3_record_status` (links `VixenApp`, CPU only) builds the real FSR3 hooks and calls `record` before initialization on a real `ComputeStageNode::Context`. It asserts no exception and the returned failure text. It does not cover the dispatch-failure and extent branches (they need a live FidelityFX context) or the `ComputeStageNode` to `ReportGpuFault` hop (needs a Vulkan command buffer); the latter reuses the already-tested `ReportGpuFault`/`FrameFault` (`test_gpu_fault.cpp`).

### T-4637: why the seed column was 0 (cause INFERRED, fix MEASURED)

- The seed stage is real on the FSR3 path: `ReconstructionHistory.cpp:75` registers it, the node is built and runs every frame (`ReconstructionHistory.comp` writes one history image per frame), and the column exists in `perf.csv`. So the file's absent-stage convention (omit the column, as native runs do) does not apply. Every other registered stage had a value; only this one was 0, 0/32 frames, in every earlier fsr3 run.
- Cause (INFERRED): `GPUQueryManager` was created with 16 consumer slots (`DeviceNode.cpp:731`). `AllocateQuerySlot` returns `INVALID_SLOT` when full and `GPUPerformanceLogger::CollectResults` then returns early, so a stage compiled after the pool filled reads 0. The seed node has no downstream consumer in the dependency graph, so it compiles late.
- Fix: pool raised to 32. No other behaviour change.
- Native witness (MEASURED, 8060S, `amd-8060s-windows`, clone `C:\vixen-native\fsr3` at `44995604`, MSVC Release, 0 warnings, 1080p @ 2/3, 64 frames, median of last 32; each run its own `--resource gpu` queue job, evidence `native-run4/`):

| Run | seed ms | fsr3_temporal ms | FSR3 GPU median ms | whole frame ms | march ms |
|---|---|---|---|---|---|
| before, d1080 02-fsr3 (`adb8864c`) | 0.000 (0/32) | 1.555 | 1.579 | 243.2 | 223.0 |
| before, f1080-23 02-fsr3 / 06-fsr3 | 0.000 | 1.467 / 1.685 | 1.492 / 1.763 | 211.7 / 276.9 | 191.7 / 253.6 |
| after, seed 03-fsr3 | **0.088 (32/32)** | 1.457 | 1.536 | 234.1 | 214.5 |
| after, seed 01-fsr3 (loaded host) | 0.171 | 3.192 | 3.185 | 725.7 | 698.5 |
| after, seed 02-fsr3 (loaded host) | 0.171 | 3.192 | 3.184 | 716.6 | 688.6 |

- Seed column populated in all three after runs. FSR3 GPU median in the clean run is 1.536 ms, inside the 1.49-1.76 ms before range: no regression. Runs 01 and 02 ran in the slow state this APU showed before (`f1080-23` 04-fsr3nopick: FSR3 3.24 ms, whole-frame passes all about 2.5-3x); every pass scaled together, including ones this change cannot touch (shadow 9.0 vs 3.8 ms), so they are contention, not the change (INFERRED from the uniform scaling). They are kept, not discarded.
- The GPU max stays at 3.2-3.3 ms against the 2.0 ms budget, as before.
- Mechanism not directly observed: no log line proves the pool was full. Evidence is 0/32 before and 32/32 after the pool change alone.

### Witness: unit tests (MEASURED, WSL)

- `test_upscaler_selection` 7/7. New `test_fsr3_record_status` 1/1.
- ctest `-R 'ViewBlobEquiv|Application.UpscalerSelection|AppRunTick|HudView|VulkanResources_CapabilityGraph|Fsr3RecordStatus'`: 26 selected, 0 failed, 1 skipped (`ConfiguredDeviceIdentityRejectsChangedDeviceOrDriver`), 25 passed. The same selection without the new test is 25 selected (24 passed, 1 skipped), so the "25 passed, 1 skipped" of run 3 counted the skip among passes. `GpuFault` ctest (12 tests) passes. 0 new warnings in WSL or native builds (the `DeviceNode.cpp:684` and `RenderGraph.cpp:868` deprecation warnings predate this change).
- Build note: `.tmp/fetch/glm-subbuild` had been reconfigured by a Windows-path build (`//wsl.localhost/...`), which broke WSL configure. I moved it aside to `.tmp/fetch/glm-subbuild.win-configured`; it is not tracked. WSL configure also needs `UNDERTOW_ROOT=/home/liory/projects/undertow`.

## R412 canonical-shape check on added lines

Run 3 (`c9775cef..adb8864c`): `throw` appeared twice in added lines (a re-indented `std::logic_error` in `Prepare`, and the `CheckFfx` `std::runtime_error`). Run 4 (`b600a5ef..HEAD`): no `throw` in added lines, six removed. No new attribute or declaration forms. 0 new warnings.

## LANDABLE NOW

Repo VBVS--VIXEN, branch `lane-fsr3`:

- `23bef8b7` fix: return FSR3 per-frame record failures as status instead of throwing (T-4635)
- `44995604` fix: raise GPU query consumer pool from 16 to 32 (T-4637)
- earlier on the branch: `43066907`, `adb8864c`
- the commit carrying this report (branch tip)

## STOPs

- **Mid-run provider demotion needs a design call.** A latched FSR3 record failure now fails the frame with a typed result and one logged error, with no exception, but the frame still ends `Render()` with `false`. Degrading to FSR1 or native without leaving the run means re-running `Prepare()`'s fallback loop from `Render()` (the loop is not reentrant today: it tears down `engine_`, and the recipe retry in the next bullet breaks it). I did not invent that mechanism.
- **Whole-frame bar not met.** FSR3 at 1080p @ 2/3 is 2.3x the native frame (242 vs 104-108 ms, paired), almost all of it the native-resolution pick trace in the jitter path (T-4633, untouched).
- 1080p FSR3 GPU max is 2.8-3.3 ms against the 2.0 ms budget; the median passes.
- Recipe registry is not retry-safe (T-4634, untouched).

## SPT DISPOSITION

Run 4 resolved two proposals filed in `.spt-proposals/fsr3.jsonl` in run 3: "Per-frame FSR3 Record path still throws (R213)" (entry `a2743fc4`, by `23bef8b7`) and "reconstruction_history_seed_ms is always 0 on the FSR3 path" (entry `71ac787f`, by `44995604`). The file is unchanged; both entries are done. No new proposals filed this run; the open items are in the next section.

## CONSOLIDATION ISSUES

- Mid-run provider demotion: `Render()` cannot re-run the `Prepare()` fallback after a latched provider fault (tier:sol, effort:M). Not filed as a proposal.
- `vkBeginCommandBuffer` throw in `ComputeStageNode::RecordComputeCommands` (`ComputeStageNode.cpp:307`) and the other generic-path throws remain on a per-frame path for every compute stage (tier:luna, effort:S). Not filed.
- GPU query pool size is a hard-coded constant with a silent early return at `GPUPerformanceLogger::CollectResults` when a slot is missing: a pass that fails to get a slot reports 0 instead of absent, which is what hid T-4637 (tier:luna, effort:S). Not filed.
- `.tmp/fetch` is shared by WSL and Windows builds, and a Windows-path configure breaks the WSL one (tier:haiku, effort:S). Not filed.
- Open from earlier runs: native pick trace dominates the FSR3 whole frame (tier:sol, effort:L); Prepare fallback retry not idempotent for recipes (tier:luna, effort:M); APU timing runs are contaminated by concurrent host builds (tier:luna, effort:M); 22 older proposals in the inbox.
