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
- Per-pass GPU columns in `perf.csv` already populate on this build after the first frames (`esvo_traverse_shade_ms`, `shadow_visibility_wave_ms`, `reconstruction_depth_ms`, `fsr3_temporal_ms`, `whole_frame_gpu_span_ms`). **Correction:** run 2 said they were 0; that was reading the first rows. The bug is in the reading, not the build. `reconstruction_history_seed_ms` is always 0 (64 of 64 rows), which is filed.

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
- Context setup now returns failure text instead of throwing (`InitializeContext`, `CreateSharedResources`, `CreateSharedImage`, `FfxFailure`). `CheckFfx` still throws on the per-frame `Record` path (outside init scope; filed).
- Same fallback order (FSR3, FSR1, native), at most 3 failures, same Warning text, same 7 tests (`test_upscaler_selection` now builds the result struct instead of throwing).
- Exhausted fallback no longer throws: `Prepare()` sets `lastError_`, logs an Error and returns with `isPrepared=false`.

### End-to-end witness (MEASURED, native 8060S)

A temporary clone-only edit made `InitializeContext` fail. `--upscaler=fsr3`, 500x500, 8 frames. The first attempt shows (evidence `forcedfail-fallback-run.log`):

- `Prepare Upscaler provider 'fsr3' failed context initialization: FSR 3.1.4 context initialization failed: forced FFX context creation failure (temporary validation); trying the next provider; suggest a device variant that includes 'Upscaler:Fsr3.1.4'`
- retry: `BuildRenderGraph Upscaler provider 'fsr3' rejected: not initialized; trying the next provider`, FSR1 selected, 1 test passed, exit 0, no exception.

Finding: unmodified, the retry aborts. `proceduralRecipes_` is an app member that survives the engine reset, and the look-dev harness registers recipes inside its `BuildRenderGraph` override, so the second attempt gets `DuplicateId` and `Prepare()` returns the error cleanly ("could not register look-dev material recipe 100 (status 1)"). The run above needed a second temporary harness edit that tolerates `DuplicateId`. This is a recipe lifecycle decision (clear vs repopulate, and the engine-lifetime recipe cacher is not repopulated either), so I did not change it. Both temporary edits were reverted by exact path from backups in `.tmp/diag`, the clone's tracked files match `adb8864c` (only the earlier generated-header changes remain), and it was rebuilt clean.

## R412 canonical-shape check on added lines

`throw` appears twice in added lines: a re-indented existing `std::logic_error` in `Prepare`, and the `CheckFfx` `std::runtime_error` kept on the per-frame path. No new attribute or declaration forms. 0 new warnings.

## LANDABLE NOW

Repo VBVS--VIXEN, branch `lane-fsr3`:

- `43066907` Report FSR3 CPU record and dispatch timing
- `adb8864c` Return upscaler initialization results instead of throwing
- the commit carrying this report and `.spt-proposals/fsr3.jsonl` (branch tip)

## STOPs

- **Whole-frame bar not met.** FSR3 at 1080p @ 2/3 is 2.3x the native frame (242 vs 104-108 ms, paired). About all of the delta is the native-resolution pick trace in the jitter path; fixing it is a design change (pick on demand, reuse, or smaller region). A residual of about 25-30% remains at lower scales and from the reconstruction-inputs path.
- 1080p FSR3 GPU max is 2.8-3.3 ms against the 2.0 ms budget in this run (median 1.5-1.8 ms passes). Run 2's 1.997 ms max was not reproduced; the host was loaded.
- Recipe registry is not retry-safe: the fallback chain works end to end only if the host tolerates re-registration.

## SPT DISPOSITION

Proposals filed in `.spt-proposals/fsr3.jsonl` this run (5 new; 22 from earlier runs).

## CONSOLIDATION ISSUES

- proposed: Native pick trace at presentation extent dominates the FSR3 whole frame (tier:sol, effort:L)
- proposed: Prepare fallback retry is not idempotent for recipes registered in BuildRenderGraph (tier:luna, effort:M)
- proposed: Per-frame FSR3 Record path still throws (R213) (tier:luna, effort:S)
- proposed: APU timing runs are contaminated by concurrent host builds (tier:luna, effort:M)
- proposed: reconstruction_history_seed_ms is always 0 on the FSR3 path (tier:haiku, effort:S)
- earlier runs: 22 proposals already in the inbox, including the forced-failure hook and per-pass timing items.
