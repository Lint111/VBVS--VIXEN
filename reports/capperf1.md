# Lane capperf1: planet capture 64 → 8 frames with a sweep witness; the cache gate and the stale GPU classes off the default split (R565 C2 + C5, C1 partly)

Branch `lane-capperf1` off `origin/wave/authoring-convergence` (`82bbaf78`). Worktree `/home/liory/projects/VBVS--VIXEN/.claude-worktrees/capperf1`, build `build/wsl` (Linux Release, Mesa Dozen on the 8060S). **Every timing here is Dozen and non-representative** (owner rule 2026-10-11); the sweep and split runs are correctness witnesses, and the queue was shared with other lanes' GPU jobs throughout.

## Summary

- **C2 landed.** The planet capture derives its frame count from the ring: `2 × MAX_FRAMES_IN_FLIGHT = 8` by default, 64 only when the reservoir path is on, `VIXEN_PLANET_CAPTURE_FRAMES` (1..64) overrides, and the process states a budget (`VIXEN_PLANET_CAPTURE_BUDGET_MS`, 300 s) and prints `[PLANET_CAPTURE_TIMING]`. The per-state frame sweep (1, 2, 4, 8, 16, 32, 64, each a fresh process, each state its own GPU queue job) found **8 byte-identical to 64 for all 12 states**, PNG and all four readback files; see the table. No state keeps 64.
- **C1 partly landed.** `Application.PlanetCapture.<state>` is registered per state (12 tests, label `Application`, 900 s each); the 12-state aggregate keeps its name under the `native` label only, with 12 × 900 s. The runners stream the capture process output through `cmake -P` (`ECHO_OUTPUT_VARIABLE`), so `ctest -V` and the queue log show progress and per-process wall time instead of one dump at the end.
- **C5 landed.** `vixen_multiinstance_cache_gate` carries only the `nightly` label; the split runner excludes `native` and `nightly` by default (`--nightly` includes them). The classification pass over the 941 stale (`classified-by-link`) rows measured 16 gpu and 925 cpu: the default GPU selection drops from **607 tests to 140**, 478 names moved gpu → cpu, and the R517 guard reported zero misclassifications when those 478 ran in the CPU part.
- Two queue defects cost about two hours of idle GPU during this lane and are filed as proposals (CONSOLIDATION ISSUES).

## 1. C2: planet capture frames (commit `d6be24f9`)

**What changed.** `VIXEN/application/main/tests/test_planet_capture.cpp` replaced `kFramesPerState = 64` with the look-dev derivation (`test_lookdev_capture.cpp:124-128`): `kRingSlotCount = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT` (4), `kDefaultFramesPerState = 2 × kRingSlotCount` (8), `kMaxFramesPerState = 2 × 32` (two reservoir caps), `DefaultFramesPerState()` returns 64 when `ResolveReservoirEnabled()`. `VIXEN_PLANET_CAPTURE_FRAMES` overrides (1..64, rejected otherwise). The test prints `[PLANET_CAPTURE_TIMING] state=… frames=… wall_ms=… budget_ms=…` and fails when the process exceeds `VIXEN_PLANET_CAPTURE_BUDGET_MS` (default 300 s, the look-dev precedent from R536). The ring argument is the one R536 proved for look-dev: every per-frame ring (LightingConfig, camera, previous camera, shadow config) has four slots that must all be rewritten for the state; two full rings give a warmed steady state.

**Sweep tool.** `tools/planet-capture-frame-sweep.py <build> <state> <out>` runs one fresh process per count, hashes `<state>.png`, `.hits.bin`, `.lighting.bin`, `.meter.bin` and `.scene.json` against the 64-frame reference, writes `<out>/<state>/sweep.tsv` and the per-count process logs, and exits 1 when the default count differs. `tools/planetcapture.py` holds the registered-environment lookup (the test's `ENVIRONMENT`/`ENVIRONMENT_MODIFICATION` from CTest, including the ICD and witness-layer paths) shared with `tools/run-planet-single-capture.py`.

**Sweep witness** (`.tmp/sweep/<state>/sweep.tsv`, one queue job per state, `cp1:sweep-<state>`; hashes are SHA-256 prefixes; wall is per fresh process on Dozen with other GPU jobs sharing the device):

| state | 1 | 2 | 4 | 8 | 16 | 32 | 64 | PNG sha (8 = 64) | hits sha (8 = 64) | 8 == 64 (all five files) |
|---|---|---|---|---|---|---|---|---|---|---|
| C1 | L | L | = | = | = | = | ref | `192b4d05dafe` | `8f657ff7f90f` | **yes** |
| C1-epic | L | L | = | = | = | = | ref | `1af639afc9ab` | `9a94d76c08ca` | **yes** |
| C2 | L | L | = | = | = | = | ref | `2fcfdf2d9a0c` | `9adbc1a49a17` | **yes** |
| C2-term | L | L | = | = | = | = | ref | `b8f1be76eff9` | `db5ca43ea132` | **yes** |
| C4a | H+L+S | L | = | = | = | = | ref | `2df4f7f5cef7` | `193ac94b4447` | **yes** |
| C4b | H+L+S | L | = | = | = | = | ref | `37c9fb1158a6` | `357b7293da47` | **yes** |
| C4c | H+L+S | L | = | = | = | = | ref | `7d5ff621cdd6` | `cfcedc8c8885` | **yes** |
| C5a-moon | L | L | = | = | = | = | ref | `f462551fabde` | `ec01a445ecab` | **yes** |
| C7 | L | L | = | = | = | = | ref | `1bf89c7801a6` | `79438710a72e` | **yes** |
| C7-60 | L | L | = | = | = | = | ref | `79905960fbf8` | `8570f635d270` | **yes** |
| C7-60-1p5au | L | L | = | = | = | = | ref | `f5c2f7e8531a` | `d78fd2ac64dc` | **yes** |
| C9 | H+L+S | L | = | = | = | = | ref | `da585269142c` | `a09f24696798` | **yes** |

**12 of 12 states: 8 frames == 64 frames** (`[SWEEP] … IDENTICAL` in every `.tmp/sweep/<state>.job.log`; 84 fresh processes, 0 retries). `=` all five files byte-identical to the 64-frame capture. `L` only `.lighting.bin` differs; `H` `.hits.bin`; `S` `.scene.json`. **The PNG is byte-identical from 1 frame up at every state.** Below 4 frames only readbacks differ, and the reason is the ring itself: `.lighting.bin` is ring slot 0 of the LightingConfig, which at 1 and 2 frames still holds `exposureCompensationEV = 0.0` and from frame 4 on holds the state's 1.585 EV (checked with `struct.unpack` at the scene's `exposureOffset` 200); at the multi-body states (C4a, C4b, C4c, C9) the frame-1 `.scene.json` differs only in `runtimeIndex` (the body order settles after the first frame) and its `.hits.bin` with it. That is exactly the "every ring slot rewritten" argument, and 8 = 2 × 4 is the conservative minimum. Accumulation is off by default in this graph (`AccumulationConfigNode.cpp:43`), so no history makes later frames differ.

**Before/after pair opened and pixel-diffed.** `tools/compare-capture-pixels.py .tmp/sweep/C1/f64 .tmp/sweep/C1/f8` → `C1.png: byte_equal=yes byte_diff=0 pixels=1080x1080 pixel_diff=0 max_channel_delta=0 bbox=none`; `cmp` agrees for C1 and C2. I opened `C1/f8/C1.png` (the 64-frame file is the same bytes): the C1 close-up, a cel-shaded disc with three tone bands, no blank or corrupt frame.

**Timing per state, 64 and 8 frames** (one fresh process each, from the sweep logs; Dozen, non-representative, device shared with other lanes' jobs):

| state | 64 frames | 8 frames | 1 frame (≈ startup) | saved at 8 |
|---|---|---|---|---|
| C1 | 299 s | 54 s | 29 s | 82% |
| C1-epic | 238 s | 47 s | 28 s | 80% |
| C2 | 125 s | 20 s | 6 s | 84% |
| C2-term | 232 s | 35 s | 7 s | 85% |
| C4a | 169 s | 31 s | 9 s | 82% |
| C4b | 196 s | 33 s | 8 s | 83% |
| C4c | 161 s | 25 s | 6 s | 84% |
| C5a-moon | 200 s | 59 s | 13 s | 70% |
| C7 | 310 s | 55 s | 11 s | 82% |
| C7-60 | 286 s | 44 s | 12 s | 84% |
| C7-60-1p5au | 225 s | 35 s | 10 s | 85% |
| C9 | 101 s | 14 s | 4 s | 86% |

Sum over the 12 states, one process each: 64 frames 2,542 s, 8 frames 452 s (−82%). Startup (pipeline cache warm) is 4 to 29 s of each process, so the 8-frame process is startup-dominated; R536's native figures put the remaining per-frame cost at tens of milliseconds, and native timing was not required here. In the registered per-state tests (first + repeat, inside the after-split GPU part, device shared with the combined run) the 12 states took 34 to 153 s each, 1,048 s in total, against the 3,600 s aggregate that stalled before.

## 2. C1 partly: per-state registration, native-only aggregate, streamed output (commit `82ec23eb`)

- `test_planet_capture.cmake` registers `Application.PlanetCapture.{C1, C1-epic, C2, C2-term, C4a, C4b, C4c, C5a-moon, C7, C7-60, C7-60-1p5au, C9}` with `-DPLANET_CAPTURE_STATES=<state>`, label `Application`, `TIMEOUT 900`, the same environment as before, and the `vixen_gpu_device` lock. `Application.PlanetCapture.RendersStatesDeterministically` stays (all 12 states) under label `native` only, `TIMEOUT 10800` (12 × 900). The state list mirrors `Planet::StateNames()`.
- `run_planet_capture.cmake`: `-DPLANET_CAPTURE_STATES` wins over `VIXEN_PLANET_CAPTURE_STATE`; each process runs with `ECHO_OUTPUT_VARIABLE`/`ECHO_ERROR_VARIABLE` so its output reaches CTest while it runs; it prints `Planet <state> -> <dir>: attempt N started`, `… wall N s` plus the `[PLANET_CAPTURE_TIMING]` line, `first and repeat captures are byte-identical`, and the suite wall. `run_lookdev_capture.cmake` gets the same echo.
- Verified with a scratch CTest project that `ctest -V` prints a test's lines as they arrive (timestamps one second apart) when the `cmake -P` runner echoes, so the queue's output-idle detector and a person tailing the box log see progress. The per-state job form is `ctest --test-dir build/wsl -R '^Application\.PlanetCapture\.C2$' -V` inside a `--resource gpu` queue job: `Application.PlanetCapture.C2 … Passed 40.04 sec` (two 8-frame processes of 19.8 s and 19.9 s, `first and repeat captures are byte-identical`), and the job log (`.tmp/witness/perstate/C2.job.log`, 7,097 lines) shows the `attempt 1 started`, `[PLANET_CAPTURE_TIMING]` and `wall 20 s` lines as the processes ran, which is the progress the queue's output-idle detector and a person tailing the box log need. The cost of streaming is about 3,500 log lines per capture process.
- Classification: the new per-state tests are plain `add_test` rows, so they take the link class of `vixen_planet_capture` (`gpu`, `classified-by-link`) until a classify pass runs them; that is the correct class. The lane's classify pass left them out on purpose (12 captures of queue time for a known answer).
- `tools/run-planet-single-capture.py` reads the environment from any `Application.PlanetCapture.*` test with a `-DPLANET_CAPTURE_EXE=` command, so it works with both registrations.

## 3. C5: the cache gate on `nightly`, stale classes measured (commits `89ae896e`, `a1811a52`)

**What changed.**
- `VIXEN/application/editor/CMakeLists.txt`: `vixen_set_suite_properties(nightly GPU TESTS vixen_multiinstance_cache_gate)` (was `RenderGraph`). `ctest -L nightly` or `run-tests-split.sh --nightly` runs it.
- `tools/run-tests-split.sh`: `--exclude-labels RE` (default `^(native|nightly)$`) applied as `-LE` to both parts and to the suite-label discovery; `--nightly` clears it; the banner prints it.
- `tools/classify-tests-gpu.py`: `-E/--exclude` regex, forwarded to `ctest`; excluded tests keep their rows.
- `VIXEN/tests/gpu-class-manifest.tsv`: the pass `classify-tests-gpu.py --build-dir build/wsl -L classified-by-link -E '^Application\.PlanetCapture\.|^vixen_multiinstance_cache_gate$' -j 4` (queue job `cp1:classify`, 728 s on Dozen, 938 tests ran, 1 failed: `AppFlowBlob.InteriorEditPreservesShapeHash`, a base red). Result: `941 tests, measured gpu 16, cpu 925; manifest freshness before: fresh 0, stale 110, new 831; link guess gpu 493 / cpu 448; disagreements 477; narrowing 391; widening 0; hand-declared vixen_gpu_device lock on a measured-cpu test (removable): 477`.

**Per-label selected counts, before and after** (`ctest -N` on the same build tree; `.tmp/witness/{before,after3}-label-counts.tsv`):

| label | cpu before | gpu before | by-link before | cpu after | gpu after | by-link after |
|---|---|---|---|---|---|---|
| RenderGraph | 1198 | 200 | 114 | 1300 | 98 | 1 |
| SVO | 783 | 15 | 31 | 783 | 15 | 0 |
| Application | 0 | 24 | 14 | 10 | 25 | 12 |
| ResourceManagement | 0 | 238 | 238 | 238 | 1 | 1 |
| CashSystem | 0 | 117 | 117 | 117 | 0 | 0 |
| VulkanResources | 0 | 13 | 13 | 12 | 1 | 0 |
| native (new) | — | — | — | 0 | 1 | 1 |
| nightly (new) | — | — | — | 0 | 1 | 0 |
| **all classified** | 2398 | 607 | 943 | 2876 | 142 | 14 |
| **runner default** (`-LE '^(native|nightly)$'`) | 2398 | 607 | | 2876 | 140 | |

The other labels (AppFlow 57, GaiaArchetypes 128, GaiaVoxelWorld 100, ShaderManagement 62, VoxelComponents 9, VoxelData 24, VoxelDocument 8, logger 28, VIXENCapture 1, WSL 1) are unchanged in class; their rows went from by-link to by-run. Application gpu goes 24 → 25: minus the aggregate (now `native`), minus 10 measured-cpu tests, plus 12 per-state planet tests. The 14 remaining by-link rows are the 13 planet `cmake -P` tests (12 per-state + aggregate) and one unlabelled cpu test.

**Names that moved** (478, all gpu → cpu; full list `.tmp/witness/moved-names.tsv`): ResourceManagement 237, CashSystem 117, RenderGraph 101 (+1 shared with ResourceManagement), VulkanResources 12, Application 10 (`AppRunTick.*` 6, `ViewBlobEquiv.*` 3, `HudViewTest.RegistersAndProjectsFactionData`). Moved out of the default selection by label: `vixen_multiinstance_cache_gate` (→ `nightly`), `Application.PlanetCapture.RendersStatesDeterministically` (→ `native`). Widened: none. The R517 guard (`[VIXEN_TEST_NO_GPU] MISCLASSIFIED TEST`, exit 86) fired **0 times** when the 478 ran in the after-split CPU part.

**Split equals combined** (R517 form; before = base tree with the base runner, planet aggregate excluded by name because it is the test R565 retires; after = this branch):

| run | tests ran | passed | failed | ctest wall (Dozen, non-representative) |
|---|---|---|---|---|
| before, split cpu (`-j8`, `--resource test`) | 2393 | 2391 | 2 | 43 s |
| before, split gpu (`-j1`, `--resource gpu`) | 606 | 606 | 0 | 2259 s |
| after, split cpu (`-j8`, `--resource test`) | 2871 | 2870 | 1 | 39 s |
| after, split gpu (`-j1`, `--resource gpu`) | 140 | 140 | 0 | 2540 s (device shared with the combined run and two sweeps) |
| after, combined serial (`-j1`, one gpu job) | 3010 | 3009 | 1 | 2719 s |

**Union equals combined** (JUnit files `build/wsl/.tmp/witness/{split-after,combined}/*.xml`): the after split's cpu part has 2874 names and its gpu part 140, union 3013 = the combined run's 3013 names, and the per-test status is identical for every name (2986 run, 1 fail, 5 disabled, 21 not run; 0 differences). The 21 not-run are the same capture-fixture set as on the base. One name appears in both parts, a pre-existing oddity (CONSOLIDATION ISSUES). Against the base: the names differ only by the 12 new per-state planet tests and the absent `vixen_multiinstance_cache_gate` (now `nightly`).

Failing names: before cpu `AppFlowBlob.InteriorEditPreservesShapeHash` and `SdiLifecycleTest.ValidateSdiContentStructure`; after cpu, after gpu and combined `AppFlowBlob.InteriorEditPreservesShapeHash` only. Both are base reds (`AppFlowBlob` fails on the base in every run; `SdiLifecycleTest` failed once on the base at `-j8` and passed in the classify pass, the after split and the combined run, a timing-sensitive test, not a split effect).

GPU-queue time of the gpu part did not fall on this contended run (2259 s → 2540 s) because the 12 per-state planet tests (1,048 s) and the look-dev capture (495 s) are now inside it while the device was shared with my own combined run and two sweep jobs; what left it is the 478 by-link tests (R517's measured 132 s of serial GPU-lock acquisitions) and the cache gate (620 to 800 s), and what no longer happens is the 3,600 s aggregate stall.

## 4. Build witness

`cmake --preset vixen-wsl` + `cmake --build build/wsl -j4` on the base: 1167/1167 steps, `BUILD_OK`, every codegen golden check passed (`.tmp/logs/build.log`). After the changes: `cmake --build build/wsl --target vixen_planet_capture accumulationconfig_check appflow_check callables_check emittersample_check emittertotals_check lightingconfig_check lighttreebuffer_check miningbeambuffer_check no_new_mutex_check octreeconfig_check prevcameraconfig_check probegridconfig_check recipe_opcode_mirror_check recipe_simd_check recipeemitterentry_check recipeparams_check reservoirconfig_check reservoirrecord_check sdf_core_kernels_check shadowconfig_check sparseemitterentry_check view_editor_layers_check view_hud_blob_check view_hud_check view_hud_markup_check view_hud_writer_check view_noun_enum_check` → `exit=0` (`.tmp/logs/build-after.log`). The test binary carries the new strings (`VIXEN_PLANET_CAPTURE_FRAMES`, `PLANET_CAPTURE_TIMING`).

R412 checklist on the added lines (413): every mechanical grep 0 hits; no `throw`, no pipes, no kinds (C++/CMake/Python/bash only). `git status` clean at each commit.

## LANDABLE NOW

All on `lane-capperf1` (base `82bbaf78`), each self-contained:

- `d6be24f9` C2: ring-derived planet frames, env override, budget, timing line; sweep tool and shared helper. Checks: build, sweep (12/12 states byte-identical at 8 vs 64), pixel-diff 0.
- `82ec23eb` C1 partial: per-state planet tests, aggregate `native`-only, streamed runner output. Checks: configure registers 12 + 1 tests with the expected labels/timeouts/locks (`ctest --show-only=json-v1`); `Application.PlanetCapture.C2` passed under `ctest -V` with streamed progress; all 12 per-state tests passed in the after-split GPU part and the combined run.
- `89ae896e` C5: `nightly` label on the cache gate, split-runner exclusion, classifier `--exclude`. Checks: `ctest -N` counts (table above), split union equals combined, same failing name as the base.
- `a1811a52` C5: measured manifest (941 rows). Checks: classify pass output, 0 guard hits when the 478 moved tests ran in the CPU part, split and combined green except the base red.
- `bbea7091` SPT proposals; the report commit follows.

## STOPs

No design STOP; the rulings settle every choice (R565 C1/C2/C5, R536 for the frame derivation and the budget default, R517 for the classes). Every witness in the brief ran to completion.

**Queue blocker met on the way (environment, filed as a proposal):** three times between 21:00 and 23:08 the GPU sat idle for 20 to 60 minutes while another lane's exclusive benchmark job waited: the line head was a shared GPU job blocked because a benchmark was waiting, and `_flow_head_bypass_ok` never lets a GPU job pass a GPU-blocked head, so neither could run. Twice the head was mine and I withdrew and resubmitted my jobs (the benchmark was admitted within seconds); once the head was the other lane's own split job, which I could not touch, and the block cleared by itself after 40 minutes. About two hours of this lane's wall time went to that.

## SPT DISPOSITION

The brief names no SPT task ids; none were created or changed. Ruling items met: R565 C2 (CLOSE: 12/12 states identical at 8 vs 64, default now 8, 64 behind the reservoir path and `VIXEN_PLANET_CAPTURE_FRAMES`), R565 C5 (CLOSE: cache gate on `nightly`, 478 stale rows measured, split runner excludes it, split equals combined), R565 C1 (PARTIAL: per-state default registration, native-only aggregate with 12× timeout and streamed output landed; C3's in-process multi-state loop and the native runner C4 are not this lane).

## CONSOLIDATION ISSUES

- **Queue deadlock** (`.spt-proposals/capperf1.jsonl`, red-state/velocity): a GPU job cannot bypass a GPU-blocked head while a benchmark waits; the device idles until someone withdraws the head. Fix in `with-test-lock.sh` `_flow_head_bypass_ok`.
- **Benchmark label heuristic** (velocity): a label matching `(bench|native|timing|perf|pacing|calibrat)` makes a GPU job exclusive, so a lane named `capperf1` with `--label <lane>:<step>` would block every other lane's shared GPU jobs. This lane used the prefix `cp1`. Match the step only, or require `--gpu-benchmark`.
- **Reconfigure rewrites generated SDI headers** (velocity, INFERRED cause): a CMake re-run rewrote `CapabilityGraphBuildConfig.h` and the `generated/sdi/*.g.h` with unchanged content and recompiled seven objects plus relinked `libVixenApp.a` and `vixen_planet_capture`.
- `tools/classify-tests-gpu.py` reports 477 hand-declared `vixen_gpu_device` locks on measured-cpu tests as removable (R517 report already lists this as a feature item); not touched here.
- `SdiLifecycleTest.ValidateSdiContentStructure` is timing-sensitive (failed once in the base CPU part at `-j8`, passed three times after); red-state, pre-existing.
- A test registered under a decorative name (`═══….Trimmed Build Compatible (Headers Only)`) is selected by both the `cpu` and the `gpu` part of the after split (the one overlapping name; the base split had none). Pre-existing registration; worth a real name and one class (audit).
- `tools/run-tests-split.sh` passes a relative `--out` directory to `ctest --output-junit` unchanged, so the JUnit files land under `build/wsl/.tmp/...` while the logs land under the worktree's `.tmp/...`; absolutising `--out` would put them together (velocity, small).
- The old `kFramesPerState` comment ("two full 32-frame reservoir-history caps") was a number that never reached the output (R536 said the same for look-dev); the sweep makes that measured for all 12 planet states.
