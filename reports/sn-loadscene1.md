# sn-loadscene1: heavy dynamic load scenario and first full-illumination measurement (R853)

## STATUS

**PARTIAL: the scenario is built, deterministic and measured on native Windows Vulkan; the quiet-window (`--resource bench`) measurement set is only 1 of 8 cells.** The rest is queued and still running (see BENCH REQUEST).

- Scenario `vixen_load_scenario`: 810 scripted frames (870 with warm-up), 10 legs, instance tiers 13 / 117 / 637 / 2,197, recipe edits and a recipe swap with shader recompiles, lighting-view switches, per-frame material changes, camera fly-through / fast pan / close-up. Same inputs give the same frames: the 10 capture hashes were **identical across three runs** of the default configuration, although their timings differed by 2x.
- Measured so far: 12 complete runs (8 `-r1` runs of every configuration x resolution, `d-500-r2/r3`, `fl-500-r2`, and the quiet-window `d-1080-q1`), plus one smoke run on an earlier build of the scenario. Only `d-1080-q1` was admitted as a `FULL-MACHINE-BENCH` quiet window (`concurrent=0`). Every other run was a `--resource gpu --gpu-benchmark --timing` job on a busy host (load 7 to 22, `INVALID-FOR-COMPARISON`). I fell back to that mode because the quiet window projected 30+ minutes away per job. **The size of the contention is visible in the data**: the same default 500x500 cell has GPU time for the 13-instance leg of 4.9 ms (`r2`), 5.1 ms (`r1`) and 21.2 ms (`r3`).
- Three repeats per cell were **not** reached (queue throughput was about one run per 40 minutes). Numbers below say which run and what load.
- Headline, in one line: **at 1080p the target is missed before any illumination is added** (default, quiet window, 13 instances: GPU 28.7 ms, p50 frame 32.6 ms), and full illumination at "high" costs about +11 ms more at that same leg; with 117 to 2,197 instances the frame is 150 to 380 ms. The traversal/shade pass (`esvo_traverse_shade`) is 80% or more of the GPU time in every leg; the illumination passes together are 6 to 10 ms.
- Two defects found on the way (below): GPU timestamp query slots leak across shader recompiles, and a cold shader cache makes a recipe edit stall 21 to 34 s.

Branch `lane-loadscene1` (nothing pushed). **Base:** `lane-framebase1` tip `ebe27bd1` (it carries FrameTrace, the per-frame counters and the INFO default of the capture, all measurement code), fast-forwarded onto the assigned base `4d202765`. It was not landed yet, so I depended on it as the brief allows.

## SCENARIO

Files: `VIXEN/application/main/tests/test_load_scenario.cpp` and `.cmake` (target `vixen_load_scenario`, not a CTest case), `tools/run-load-scenario.bat`. It subclasses `VulkanGraphApplication` exactly like the look-dev capture and reuses only existing seams: `RegisterProceduralRecipe` / `ReplaceProceduralRecipe` / `RecompileProceduralShader`, `SetBodyInstances`, the camera test setters, `SetSceneLightsLayer` / `SetSceneLookLayer`, `CaptureOffscreenFrameToPng`. No new attributes, no new env switches (resolution is the existing `VIXEN_WINDOW_WIDTH/HEIGHT`; the executable takes `--out DIR` only).

Scene: the look-dev scene (13 material-recipe instances) tiled over 1x1, 3x3, 7x7, 13x13 copies, 12 world units apart, centred on the original. Per-copy colour variation is a pure function of the copy index and a phase that advances with the frame.

| # | leg | frames | tier (instances) | camera | events |
|--:|---|--:|---|---|---|
| 1 | warm-orbit-t0 (excluded from stats) | 60 | 0 (13) | orbit | |
| 2 | flythrough-t0 | 120 | 0 (13) | low fly-through of the field | |
| 3 | fastpan-t1 | 60 | 1 (117) | one full 360 degree pan | tier step at start |
| 4 | view-switches-t1 | 60 | 1 (117) | orbit | lighting view switch every 15 frames (midday, late-afternoon, overcast, night-service) |
| 5 | closeup-t2 | 120 | 2 (637) | glide to 1.6 units from a surface and back | tier step |
| 6 | flythrough-t3 | 120 | 3 (2,197) | fly-through | tier step; material colours rewritten every frame (`SetBodyInstances`) |
| 7 | fastpan-t3 | 60 | 3 (2,197) | fast pan | |
| 8 | recipe-edits-t3 | 90 | 3 | orbit | Hull recipe edited at +40 and +80 (replace and shader recompile); material change every 5 frames |
| 9 | recipe-swap-t3 | 90 | 3 | close-up | a new recipe registered and the Hull instances moved onto it (recompile); one edit; view switch every 30 frames |
| 10 | scale-down-t1 | 90 | 1 (117) | fast pan | tier step down |

The recipe legs are last on purpose: each recompile leaks GPU timestamp query slots (finding 1), so per-pass GPU time is valid only before them.

**Determinism.** Every input is a pure function of the frame index (no wall clock, no random source). Frames at the end of each leg are written as PNG and hashed (FNV-1a 64 of the file) into `hashes.csv`. `d-500-r1`, `d-500-r2` and `d-500-r3` produced the same 10 hashes (frames 59, 179, 239, 299, 419, 539, 599, 689, 779, 869) although their frame times differed by 2x. The illumination runs differ from the default from the first capture (expected: different lighting); they were run once each, so their own run-to-run determinism is **not** shown. (A first version hashed the file before the asynchronous capture had written it; fixed in `c2a91be6`.)

Outputs per run: `timeline.csv` (frame, leg, tier, instances, events, render ms, camera), `hashes.csv`, `vram.csv` (device memory budget and usage per leg, `VK_EXT_memory_budget`), plus the existing `perf.csv` (per-frame CPU period, `pace_wait_ms`, `gpu_wait_ms`, whole-frame GPU span, per-pass GPU ms from the timestamp loggers) and `frame-trace.csv` (FrameTrace spans, sn-framecpu1 buckets).

## CONFIGURATIONS

All runs: native Windows Vulkan (Radeon 8060S), Release with `UNDERSET_NATIVE_BENCHMARK=ON`, offscreen presentation, `VIXEN_HDR_EXPOSURE=1`, `VIXEN_HUD_SUPPRESS=1`, `VIXEN_HIDDEN_WINDOW=1`, INFO log level.

**Owner definition (R853.3-.5): full illumination = current setup + better shading sampling + photon-sampling GI, each feature with a per-feature config struct that sets how large and how iterative its work is.** What the code has today:

| feature | struct (generated `[GpuStruct]` or node config) | size / iteration fields | reachable from outside today |
|---|---|---|---|
| shadow | `ShadowConfig` | `enabled`, `raysPerLight` (=1), `maxShadowDistance`, `biasEpsilon` | `enabled` and `biasEpsilon` by env; `raysPerLight` **not reachable** |
| shading sampling (reservoir / RIS) | `ReservoirConfig` | `reservoirEnabled`, `candidateCount`, `spatialRadius`, `spatialCount` (=4), `temporalCap` (=32), `biasedModeEnabled`, `lightTreeCutThreshold` | `reservoirEnabled`, `spatialCount` by env; the rest **not reachable** |
| probe GI (DDGI) | `ProbeGridConfig` | `probeGridEnabled`, `countX/Y/Z`, `raysPerProbe` (=64, max 256), `hysteresisRate`, `amortizationFactor` (=8) | enabled, `raysPerProbe`, `amortizationFactor` by env; counts and hysteresis **not reachable** |
| temporal accumulation | `AccumulationConfig` | `enabled`, `alpha`, `maxFrames`, `resetOnMotion`, `reprojectionEnabled` | `enabled`, `reprojectionEnabled` by env; `alpha`, `maxFrames` **not reachable** |
| photon-cell GI | `PhotonCellConfiguration` + `PhotonCellParams` block | `cellSize0` (=0.5), `temporalAlpha`, `radianceClamp`, `maxAge` | feature on/off, size, alpha, max age, clamp by env |
| hit accumulation | `HitAccumParams` | detail size and resolve/lean/premerge flags | by env |
| volumetrics (future) | none | | **slot reserved, nothing built** |

**What I did not do, and why.** The owner asks that each feature's struct be declared through the kernel schema path with no env switches (R491). I did not change schemas or the kernel: that is a cross-repo design change and a STOP-class decision (see OWNER QUESTIONS). The sweep below therefore sets the struct values the only way the code allows today, the existing per-field "bench lever" environment variables, set by the run script (`queue-run.sh`, evidence folder), **not** by the scenario executable. A configuration is a set of struct values; where a field has no lever it stays at its compiled default, and that is stated.

| config | name | env settings used (exact) | resulting struct values (changed fields) |
|---|---|---|---|
| `d` | today's default | none beyond the common set | shadow on, `raysPerLight=1`; reservoir, probe grid, accumulation, photon cells, hit accumulation all **off**; `VIXEN_RT_LIGHTING` unset (auto: ray-query backend selected, `RTQUERY`) |
| `fl` | full illumination, low | `VIXEN_RESERVOIR_CONFIG_ENABLED=1 VIXEN_RESERVOIR_SPATIAL_COUNT=2 VIXEN_PROBE_GRID_CONFIG_ENABLED=1 VIXEN_PROBE_RAYS_PER_PROBE=16 VIXEN_DDGI_AMORTIZATION_FACTOR=16 VIXEN_ACCUMULATION_ENABLED=1 VIXEN_PHOTON_CELLS=1 VIXEN_PHOTON_CELL_SIZE=1.0` | reservoir `spatialCount=2`; probe `raysPerProbe=16`, `amortizationFactor=16`; accumulation on (no reprojection); photon `cellSize0=1.0` |
| `fm` | full illumination, medium | `VIXEN_RESERVOIR_CONFIG_ENABLED=1 VIXEN_RESERVOIR_SPATIAL_COUNT=4 VIXEN_PROBE_GRID_CONFIG_ENABLED=1 VIXEN_PROBE_RAYS_PER_PROBE=64 VIXEN_DDGI_AMORTIZATION_FACTOR=8 VIXEN_ACCUMULATION_ENABLED=1 VIXEN_ACCUMULATION_REPROJECT=1 VIXEN_PHOTON_CELLS=1 VIXEN_PHOTON_CELL_SIZE=0.5 VIXEN_HIT_ACCUM=1 VIXEN_HIT_ACCUM_RESOLVE=1` | `spatialCount=4`; `raysPerProbe=64`, `amortizationFactor=8`; accumulation on with reprojection; `cellSize0=0.5`; hit accumulation and resolve on |
| `fh` | full illumination, high | as `fm` with `VIXEN_RESERVOIR_SPATIAL_COUNT=8 VIXEN_PROBE_RAYS_PER_PROBE=128 VIXEN_DDGI_AMORTIZATION_FACTOR=2 VIXEN_PHOTON_CELL_SIZE=0.25` | `spatialCount=8`; `raysPerProbe=128`, `amortizationFactor=2`; `cellSize0=0.25` |

`candidateCount`, `raysPerLight`, `temporalCap`, the accumulation `alpha`/`maxFrames` and the probe counts have no lever, so no configuration varies them: **"better shading sampling" cannot be raised past today's compiled default (`candidateCount`, `raysPerLight=1`) by anything I could set.** `VIXEN_SHADOW_CONFIG_ENABLED` is already on by default and `VIXEN_RT_LIGHTING` was left on auto (`force` needs RT hardware paths I did not exercise). These are the existing switches from `reports/vixenmodules.md`; I added none.

## MEASURED

Definitions. *Work time* = frame period minus the frame-pace sleep (the engine paces to 16.67 ms, so the paced period of a fast frame is always about 16.7 ms; work time is what the frame needed). *Stutter* = frame with work time over 2x the run's median work time. Frames of the warm-up leg are excluded; recompile and tier-step frames are included (a stutter is a stutter). GPU span = whole-frame GPU timeline from the timestamp loggers. Quality column: `Q` = admitted as quiet window (`FULL-MACHINE-BENCH`, nothing else running); `L7` = gpu-benchmark mode at host load 7 and no concurrent job (`concurrent=0..1`); `C` = contended (load 17 to 22, 2 to 3 concurrent jobs): **ratios inside a `C` run are usable, absolute times are inflated**.

### Frame time per run (all legs except warm-up, 810 frames)

| run | quality | work p50 ms | p95 | p99 | max | stutters | frames over 16.7 ms |
|---|---|--:|--:|--:|--:|--:|--:|
| d-1080-q1 | Q | 206.6 | 282.6 | 564.1 | 1,818 | 12 | 810 |
| fl-1080-r1 | L7 | 210.7 | 297.1 | 549.3 | 2,044 | 12 | 810 |
| fh-1080-r1 | L9 | 222.8 | 388.5 | 562.1 | 7,186 | 17 | 810 |
| fm-1080-r1 | C | 222.7 | 471.3 | 659.0 | 7,265 | 58 | 810 |
| d-1080-r1 | L9 | 259.5 | 365.5 | 651.8 | 2,171 | 13 | 810 |
| d-500-r2 | L7 | 44.4 | 76.4 | 136.1 | 1,851 | 26 | 715 |
| d-500-r1 | L? | 35.5 | 53.9 | 109.3 | 1,885 | 21 | 695 |
| d-500-r3 | C | 67.0 | 97.1 | 155.1 | 2,397 | 13 | 810 |
| fl-500-r2 | L10 | 36.8 | 76.3 | 132.6 | 2,193 | 49 | 795 |
| fl-500-r1 | C | 78.6 | 127.0 | 209.1 | 5,134 | 21 | 810 |
| fm-500-r1 | C | 72.0 | 108.9 | 166.7 | 17,487 | 17 | 810 |
| fh-500-r1 | C | 86.4 | 178.4 | 231.7 | 8,846 | 47 | 810 |

Three-run set: only the default 500x500 cell has three runs (p50 35.5, 44.4, 67.0 ms: contention, not the engine). No other cell has three runs. The scenario's heavy legs dominate these whole-run numbers (most frames are in tier 1 to 3), so use the per-leg table to read the 13-instance (look-dev) case.

### Per-leg work time p50 / p99, GPU span p50 in brackets (ms), 1920x1080

| leg | d-1080-q1 (Q) | fl-1080-r1 (L7) | fh-1080-r1 (L9) | fm-1080-r1 (C) |
|---|--:|--:|--:|--:|
| warm-orbit-t0 | 32.6 / 35 (28.7) | 36.7 / 40 (32.7) | 43.9 / 56 (39.3) | 65.8 / 88 (58.3) |
| flythrough-t0 | 36.9 / 66 (32.1) | 40.9 / 76 (36.5) | 46.8 / 86 (41.7) | 67.3 / 196 (60.7) |
| fastpan-t1 | 156.0 / 199 (147.4) | 159.7 / 214 (155.0) | 173.5 / 229 (166.9) | 214.7 / 396 (206.1) |
| view-switches-t1 | 160.4 / 220 (156.7) | 161.0 / 231 (154.5) | 171.7 / 249 (166.6) | 216.5 / 428 (209.1) |
| closeup-t2 | 255.6 / 337 (247.4) | 265.0 / 356 (259.1) | 292.8 / 370 (284.6) | 411.5 / 525 (398.3) |
| flythrough-t3 | 206.8 / 268 (202.2) | 211.9 / 310 (207.6) | 279.3 / 470 (255.6) | 331.8 / 560 (323.3) |
| fastpan-t3 | 252.7 / 295 (248.0) | 265.5 / 307 (260.0) | 385.6 / 562 (376.5) | 279.8 / 366 (273.4) |
| recipe-edits-t3 | 207.5 / 617 (202.3) | 211.0 / 756 (205.3) | 217.8 / 730 (211.8) | 212.4 / 757 (207.0) |
| recipe-swap-t3 | 215.4 / 899 (n/a) | 219.3 / 1,088 (n/a) | 227.1 / 1,328 (n/a) | 225.2 / 1,423 (n/a) |
| scale-down-t1 | 156.2 / 218 (n/a) | 160.7 / 208 (n/a) | 173.2 / 226 (n/a) | 167.7 / 224 (n/a) |

"n/a" = GPU timing was dead after the first recompiles (finding 1). The fm-1080 row is contended and not comparable.

### Per-leg, 500x500 (default: 3 runs; low: 2)

| leg | d-500-r1 | d-500-r2 | d-500-r3 (C) | fl-500-r2 | fl-500-r1 (C) |
|---|--:|--:|--:|--:|--:|
| warm-orbit-t0 | 10.1 / 13 (5.1) | 9.0 / 12 (4.9) | 29.5 / 40 (21.2) | 17.6 / 36 (12.9) | 32.8 / 62 (23.4) |
| flythrough-t0 | 10.1 / 24 (4.9) | 11.4 / 23 (4.6) | 27.3 / 40 (18.0) | 23.1 / 53 (13.6) | 35.5 / 79 (21.8) |
| fastpan-t1 | 33.0 / 46 (27.0) | 46.1 / 86 (40.4) | 51.3 / 108 (44.2) | 56.2 / 117 (46.8) | 78.3 / 163 (66.9) |
| closeup-t2 | 38.8 / 112 (35.2) | 57.8 / 136 (52.5) | 75.8 / 154 (67.7) | 40.3 / 154 (37.1) | 87.5 / 221 (78.8) |
| flythrough-t3 | 30.8 / 53 (26.6) | 48.2 / 108 (42.9) | 69.2 / 147 (59.4) | 30.4 / 82 (26.2) | 81.7 / 156 (69.6) |

The 13-instance default at 500x500 reproduces sn-framecpu1's 5.0 ms GPU (4.6 to 5.1 ms here, runs `r1`/`r2`). Run-to-run spread in the heavy legs at 500x500 (e.g. closeup-t2 38.8 / 57.8 / 75.8 ms for the same configuration) is host contention; I could not separate it. Full table for all configurations: `sn-loadscene1-evidence/t-500.md`, `t-1080.md`.

### Per-pass GPU time (p50 ms), 1080p, leg flythrough-t0 (13 instances) and flythrough-t3 (2,197 instances)

| pass | t0 d (Q) | t0 fl (L7) | t0 fh (L9) | t3 d (Q) | t3 fl (L7) | t3 fh (L9) |
|---|--:|--:|--:|--:|--:|--:|
| esvo_traverse_shade | 24.03 | 22.28 | 24.77 | 197.58 | 197.58 | 235.69 |
| shadow_visibility_wave | 4.32 | 4.07 | 4.49 | 1.53 | 1.47 | 1.54 |
| photon_cell_deposit | | 3.21 | 3.49 | | 1.11 | 1.27 |
| spatial_reuse | 1.52 | 1.61 | 1.59 | 1.39 | 1.40 | 1.40 |
| direct_lighting | | 0.95 | 1.07 | | 0.88 | 0.90 |
| shadow_visibility_wave_reservoir | | 1.02 | 1.04 | | 1.05 | 1.11 |
| probe_gather | | 0.10 | 1.01 | | 0.47 | 4.89 |
| spatial_reuse_gather | | 0.85 | 0.88 | | 0.70 | 0.73 |
| hit_accum_accumulate | | | 0.73 | | | 1.15 |
| exposure (meter, reduce, tonemap) | 0.19+ | 0.19+ | 0.20+ | 0.19+ | 0.19+ | 0.21+ |
| photon_cell_fold, shadow_ray_trace, probe_apply, hit_accum_cell_shade, blit | < 0.1 each | | | | | |

### Per-feature cost curves (500x500, leg flythrough-t0, p50 ms; set `C`: same load for all three runs, so the **ratios** are usable)

| feature pass | struct value low (`fl`) | medium (`fm`) | high (`fh`) | reading |
|---|--:|--:|--:|---|
| probe_gather | 0.39 (rays 16, amortization 16) | 0.97 (64, 8) | 4.04 (128, 2) | the only pass whose cost follows its struct: x2.5 then x4.2 |
| spatial_reuse | 0.49 (spatialCount 2) | 0.49 (4) | 0.52 (8) | flat: `spatialCount` 2 to 8 costs nothing measurable |
| direct_lighting | 0.20 | 0.24 | 0.24 | flat |
| photon_cell_deposit | 1.81 (cellSize 1.0) | 1.70 (0.5) | 1.75 (0.25) | flat: cell size 1.0 to 0.25 costs nothing measurable at t0 |
| hit_accum accumulate + cell_shade | off | 0.18 | 0.21 | small |

At 1080p near-quiet (`L7`/`L9`, t3) probe_gather goes 0.47 to 4.89 ms from `fl` to `fh`: the same shape. **Only the probe grid has a visible cost curve today**; the other illumination structs' size fields either have no lever (candidateCount, raysPerLight, temporalCap, alpha) or move no pass cost in this scene (spatialCount, photon cell size). Whether that is a real property of the shaders or the scene being too small for them is not established; I did not isolate it (the one-at-a-time sweeps were not run).

### CPU time per stage (ms per frame, mean; sn-framecpu1 buckets over each leg; frame-trace)

| stage | d-1080-q1 t0 fly | d-1080-q1 t3 fly | fh-1080-r1 t0 fly | fh-1080-r1 t3 fly |
|---|--:|--:|--:|--:|
| frame period | 37.0 | 210.3 | 48.5 | 279.6 |
| GPU waits | 31.5 | 204.2 | 41.0 | 264.7 |
| uploads (instance rings, bricks; `body_octree_scene`) | 2.34 | 2.03 | 2.53 | 3.79 |
| submit | 0.68 | 0.79 | 1.21 | 3.65 |
| timestamp query readback | 0.63 | 0.69 | 1.06 | 1.20 |
| descriptor sets, gatherers | 0.54 | 0.52 | 0.87 | 1.01 |
| command recording | 0.25 | | 0.47 | 2.07 |
| node Execute glue | 0.24 | 0.25 | 0.38 | 1.20 |
| sim / update on render thread | | 0.75 | | 0.87 |
| **CPU work (period minus GPU waits)** | **5.5** | **6.1** | **7.5** | **14.9** |

CPU work is 5.5 to 7.5 ms at 13 instances: higher than sn-framecpu1's 3.1 to 3.7 ms because the camera moves in this scenario (that run was static); the extra is mostly `body_octree_scene` (2.3 ms mean at 13 instances; sn-framecpu1 saw 0.01 ms). The cause (instance front-to-back sort or camera-driven residency) is inferred, not measured. At 2,197 instances with illumination, CPU work reaches 14.9 ms mean (submit and recording means include recompile-adjacent frames). It is below the GPU time in every leg at 1080p, so the frame is GPU-bound throughout; at 500x500 the default 13-instance frame is CPU 5 ms plus pace sleep.

### Events (1080p, `d-1080-q1`)

Render time of the frame that applies a change (ms): tier step t0 to t1 26 (frame 180), t1 to t2 133 (frame 300), t2 to t3 258 (frame 420), down to t1 156 (frame 780); recipe-edit frames 214 to 245; recipe swap 205 (frame 690). No multi-second hitch was seen at a tier step (a ring-growth recompile was not observed). The worst single frames are 1.8 s (default) and 7.2 to 7.3 s (`fh`/`fm`, shader recompile with illumination on).

### VRAM (device heap usage by this process, `VK_EXT_memory_budget`, MB)

| run | heap 0 (device local) | heap 1 |
|---|--:|--:|
| d-500-r1 | 59 to 60 | 42 |
| fh-500-r1 | 71 to 72 | 42 |
| d-1080-q1 | 260 to 261 | 288 |
| fh-1080-r1 | 273 to 274 | 288 |

VRAM is dominated by the resolution (render targets), not by the instance tier (2,197 instances added about 1 MB) or by illumination (+12 to 13 MB at both resolutions). Budgets are 34.4 GB and 68.7 GB (the APU shares system memory), so VRAM is not a constraint here.

## RANKED GPU PASSES

At 1080p, full illumination "high" (`fh`), p50 ms, with the 13-instance look-dev leg (flythrough-t0, `L9`) and the 2,197-instance leg (flythrough-t3, `L9`). Sum of listed passes is below the whole-frame GPU span because of gaps between submits and unlisted small passes.

| rank | pass | t0 ms | t3 ms | what scales it |
|--:|---|--:|--:|---|
| 1 | esvo_traverse_shade | 24.8 | 235.7 | resolution (3.0 ms at 500x500, 24 ms at 1080p: x7.9 for x8.3 pixels) and instance count (x8.2 default, x9.5 with `fh`, for x169 instances: sub-linear); +2 to +38 ms with full illumination at t3 (heavier hit shading); not by recipe edits or view switches in this data |
| 2 | shadow_visibility_wave | 4.5 | 1.5 | resolution and the visible lit surface of the view (drops at t3 where the camera looks across many instances); present in the default |
| 3 | probe_gather | 1.0 | 4.9 | its struct: `raysPerProbe` and `amortizationFactor`; x10 from low to high at t3 |
| 4 | photon_cell_deposit | 3.5 | 1.3 | resolution (hit count) and view; not the cell size in this scene |
| 5 | spatial_reuse | 1.6 | 1.4 | resolution; in the default as well |
| 6 | direct_lighting | 1.1 | 0.9 | resolution |
| 7 | shadow_visibility_wave_reservoir | 1.0 | 1.1 | resolution |
| 8 | hit_accum_accumulate | 0.7 | 1.2 | resolution; only with hit accumulation |
| 9 | spatial_reuse_gather | 0.9 | 0.7 | resolution |
| 10 | exposure meter / reduce / tonemap | 0.2 | 0.2 | fixed |

By group at 1080p t0: scene (traverse/shade, shadow wave, spatial reuse, exposure): about 30 ms in the default (quiet) and 31 ms with `fh`; **illumination additions (photon, probe, reservoir, direct lighting, hit accumulation, gathers): 8.4 ms at "high", 6.2 ms at "low"**.

## GAP TO TARGET

Target: native Windows Vulkan, 1080p, full illumination: frame p50 well under 16.7 ms, p99 at most 16.7 ms, no stutter spikes.

| case (1080p) | GPU span p50 | work p50 | vs 16.7 ms |
|---|--:|--:|---|
| default, 13 instances (quiet) | 28.7 | 32.6 | 1.7x over **before any illumination** |
| full illumination low, 13 instances (`L7`) | 32.7 | 36.7 | 2.0x over |
| full illumination high, 13 instances (`L9`) | 39.3 | 43.9 | 2.4x over |
| default, 117 instances | 147 | 156 | 9x |
| default, 637 instances (close-up) | 247 | 256 | 15x |
| default, 2,197 instances | 202 to 248 | 207 to 253 | 12 to 15x |
| high, 2,197 instances | 256 to 377 | 279 to 386 | 17 to 23x |

- Gap at the look-dev scene: **12 ms of GPU time before illumination, 4 to 11 ms more for it**. At 500x500 the default is 5 ms (inside budget) and full illumination low is about 13 ms (`L10`), so 500x500 is the only configuration under 16.7 ms.
- The gap is the traversal/shade pass (24 ms at 1080p, 13 instances), not the illumination features. Cutting the illumination work to zero would still leave 1080p at 1.7x the budget. The stutter targets are also missed: 12 to 58 frames per run are over 2x the median, the worst frames are recompile frames (1.8 to 7.3 s), and every frame of the 1080p runs is over 16.7 ms.
- CPU is not the limit: 5.5 to 7.5 ms CPU work in the 13-instance legs, under the GPU time at 1080p (sn-framecpu1's conclusion holds, with the camera-motion cost added).
- Which instance tier is the product scene is not settled (OWNER QUESTION 3); the tiers decide whether the target is a 10 ms or a 200 ms problem.

## LANDABLE NOW

VIXEN branch `lane-loadscene1` (on top of `lane-framebase1` `ebe27bd1`, which must land first or with it): 

- `fb424bc7` `feat(bench): deterministic heavy dynamic load scenario executable` (`test_load_scenario.cpp/.cmake`, `application/main/CMakeLists.txt` include, `tools/run-load-scenario.bat`)
- `8ea2575d` `fix(bench): load scenario takes the app's own render path config`
- `c2a91be6` `fix(bench): load scenario hashes captures after publish; recipe legs last`

Checks passed: native Release build of `vixen_load_scenario` through the queue (log `sn-loadscene1-evidence/build3.log`, zero compiler warnings in the target; the log's only failure was the first version's `gRenderPathConfig` error, fixed in `8ea2575d`); 12 complete scenario runs; determinism over 3 runs. No CTest registration (a benchmark executable); no kernel or schema change; `Program.Legacy.cs` untouched. No tests were added because the executable is the measurement.

## BENCH REQUEST

The quiet-window chain `evidence/benchchain.sh` is still queued and running: `fh-1080-q1`, `fm-1080-q1`, `fl-1080-q1`, `d-500-q1`, `fh-500-q1`, `fm-500-q1`, `fl-500-q1` (one `--resource bench --timing` job each, logs in `sn-loadscene1-evidence/logs/`). Their outputs land in `/mnt/c/vixen-native/loadscene1-evidence/<name>/`; `python3 -I table.py` then `tables2.py <names>` in the evidence folder regenerate the tables. A controller who wants all three repeats in a planned quiet window: run `bash queue-run.sh <name> <w> <h> <d|fl|fm|fh>` with `QUEUE_RESOURCE=bench`, one per window (about 7 minutes each at 1080p).

## OWNER QUESTIONS

1. **Product default for illumination (not decided here).** Options: (a) default stays as today and full illumination is an opt-in quality tier; (b) full illumination "low" becomes the default (about +4 ms at 1080p, 13 instances); (c) a device-capability policy picks the tier (R491 target policy). Recommendation: decide after the quiet runs, because at today's traversal cost no tier reaches 16.7 ms at 1080p.
2. **Per-feature config structs (R853.3-.5).** The structs exist (generated `[GpuStruct]` blocks and the photon/hit-accum nodes) but their size and iteration fields are reachable only through per-field env "bench levers", several not at all (`candidateCount`, `raysPerLight`, `temporalCap`, accumulation `alpha`/`maxFrames`, probe counts), and no struct is set from a schema-declared quality tier. This deviates from the unified form (R394): a kernel-schema-declared config family with named presets (low/medium/high) per feature, applied by a module, instead of env reads inside each config node. Conversion: declare one illumination quality record per feature through the schema path, generate the existing `.g.h`, make the config nodes read the record, delete the env reads (R491), reserve a `Volumetrics` slot. I did not build it; confirm that design (and whether the presets belong in the pack or in VIXEN) before a lane does.
3. **Which scene and instance budget is the product target?** The default look-dev scene (13 instances) misses the budget at 1080p by itself; a game scene with hundreds of instances is 10x further. The target needs a scene definition, not only a resolution.
4. **Is `esvo_traverse_shade` (24 ms at 1080p, 13 instances) in scope for this goal?** It is 80% of the GPU frame; illumination is 5 to 8 ms. If the target is 16.7 ms at 1080p, the traversal needs a 2x cut first (resolution scaling or upscaling is the existing lever; `render scale` is a render-path config, not measured here).
5. **Report period or work time?** The engine paces to 16.67 ms, so a paced p50 is always about 16.7 ms when the GPU keeps up. I reported work time (period minus pace sleep). Confirm that is the number the target means.

## STOPs

- **S1. Config-struct model not built** (owner question 2): needs the schema/kernel path and an owner design confirmation. Bridge used: existing env levers.
- **S2. Quiet-window coverage 1 of 8 cells** at report time; three repeats not reached (queue throughput, shared APU).
- No STOP on the base or the build.

## FINDINGS

1. **GPU timestamp query slots leak across shader recompiles.** After two to three `RecompileProceduralShader` calls the run log shows `[GPUPerf] No free GPU query slot for '<pass>' (32 of 32 in use): this pass's GPU timing is disabled`; `whole_frame_gpu_span_ms` falls to about 0.001 and every per-pass column reads 0 for the rest of the run (legs 9 and 10). Repro: `vixen_load_scenario`, any run, `run.log` around the first recipe edit. Expected: slots released when a stage node is recompiled. Owner: VIXEN (`GPUQueryManager`, `DeviceNode.cpp` `maxQueryConsumers`). Size S to M.
2. **A recipe edit stalls 21 to 34 s with a cold shader cache** (first run, `gpub-d-500-smoke`: legs of 21.0 s and 34.0 s); with a warm cache the worst frame of an edit leg is 0.4 to 1.9 s, and the edit frame itself costs 36 to 52 ms at 500x500 and 214 to 262 ms at 1080p (`recipe-edits-t3`, `recipe-swap-t3`). With illumination on, the worst recompile frame is 7.2 s. The recipe live-edit seam is not interactive on a cold cache. Owner: VIXEN (procedural shader compile path); the compile belongs off the frame thread.
3. **Camera motion costs 2 to 3 ms of CPU in `body_octree_scene` per frame even at 13 instances** (sn-framecpu1 measured 0.01 ms with a static camera). Cause inferred (front-to-back sort or residency), not isolated.
4. **`spatialCount` (2 to 8) and photon `cellSize0` (1.0 to 0.25) change no pass time** in this scene (above). Either the shaders do not use them as work size or the scene is too small to show it. Needs a one-at-a-time sweep at a heavier view.

## CONSOLIDATION ISSUES

- try, fail, remedy, success: (a) native build of `vixen_load_scenario` failed on `gRenderPathConfig` (an anonymous-namespace global of the capture test) and was fixed in the executable; no pipeline gap. (b) The first scenario run hashed PNGs before the asynchronous capture wrote them, so every hash was the hash of an empty file: `CaptureOffscreenFrameToPng` returns before the file exists and gives no completion handle. Remedy: hash after the drain. Gate that would make it automatic: a completion callback or future for the capture request.
- `benchmark-configure.bat` is untracked, so the native checkout never has it (`vixen-native.sh prepare` does not copy it): `UNDERSET_NATIVE_BENCHMARK=ON` is lost on a fresh lane unless someone copies the file (same as sn-framecpu1/framebase1). Should be tracked or a `vixen-native.sh configure --benchmark` verb.
- Killing a queued wrapper (`kill <pid>`) while its Windows run is live does not stop the run (`fl-500-r2` completed after I killed its wrapper). Known (memory note); the run was valid and kept.
- Queue throughput for native timing jobs: `--resource gpu --gpu-benchmark` waited 20 to 40 minutes per job (blockers: `pinned-drain`, `host cpu 100% >= 70%`); `--resource bench` windows projected 30 to 60 minutes and slipped repeatedly. About 12 jobs in 6 hours. Under-use of the node by my own jobs is not the cause (each was a single exclusive job), the wait is admission.
- Evidence sizes: native run folders are 668 MB for 13 runs (10 PNGs per run, about 600 KB each, 6 to 7 MB per run); only CSVs, hashes and compressed logs (27 MB) were copied to the evidence folder. PNGs stay on `/mnt/c/vixen-native/loadscene1-evidence/`.

## REDS

- `GPU timestamp query slots exhausted after recompiles` (finding 1): reproduces on the lane build (`ebe27bd1` plus this lane) in every run, e.g. `d-500-r1`, log `runs/d-500-r1/run.log.xz`. Whether it reproduces on the published wave base `4d202765` was not run.
- `[CacheCodec] Failed to save DescriptorSetLayoutCacher ... ca...` at shutdown of the native runs (`run.log`): not mine; present in every run.

## SPT DISPOSITION

No SPT task id was given for this lane. Proposals not filed (the SPT workspace was outside this session's directories at the end); the OWNER QUESTIONS, FINDINGS and CONSOLIDATION ISSUES above are in the shared-finding shape for the controller to file.

## EVIDENCE

`~/codeman-cases/undertow/reports/sn-loadscene1-evidence/`: `runs/<run>/` (compressed perf, trace, logs; timeline, hashes, vram), `summary.json`, `t-frames.md`, `t-500.md`, `t-1080.md`, `analyze.py`, `table.py`, `tables2.py`, `cpu-stage.py` (reuses sn-framecpu1's `split.py`), `queue-run.sh`, `sweep.sh`, `benchchain.sh`, `logs/` (queue logs with load and window class per job), `build3.log`, `configure.log`, `prepare.log`.
