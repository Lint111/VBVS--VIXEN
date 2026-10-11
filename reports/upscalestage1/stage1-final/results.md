# Stage 1: render scale and FSR 1

## LANDABLE NOW

**Capture source tip:** `8f84ca33` (`Freeze measured exposure for stage-one captures`), after merge commit `decc023f` containing lane-gpuyield `e7d8a198`. The live `origin/wave/authoring-convergence` tip was an ancestor before the final witness. Performance captures used the native Windows Vulkan route on the **AMD Radeon(TM) 8060S Graphics**. Dozen was used only for canonical correctness.

The prototype decouples the offscreen render extent from presentation extent through `VIXEN_RENDER_SCALE` (`1.0`, `0.6666667`, `0.5`). It adds the existing linear enlargement comparison and optional FSR 1 input AA, EASU, and RCAS under `VIXEN_UPSCALER=fsr1`. The vendored AMD FidelityFX FSR 1 source is version **1.0.2**, upstream revision `a21ffb8f6c13233ba336352bdff293894c706575`, under the MIT license at `VIXEN/shaders/third_party/amd_fsr1/LICENSE`.

### Captures and visual review

- [500×500 comparison sheet](500/comparison-sheet.svg), [24-angle contact sheet](500/pan-contact-sheet.svg), and [six-second slow pan](500/fsr1-pan-scale-2-3/slow-pan.apng).
- [1080p comparison sheet](1080/comparison-sheet.svg).
- Raw PNGs, `perf.csv` files, and lighting readbacks are retained in each configuration directory under `500/` and `1080/`.
- Still captures used the midday recipe, fixed EV100 `4.66783`, fixed LOD coefficient `0.00165685425`, and yaw angles 0°, 90°, 180°, and 270°. The pan sampled 24 yaw positions at 15° increments. Coupled-LOD runs cleared the LOD override and were measured separately.

I inspected the stills and pan frames. The house, tree, and foreground object silhouettes remain recognizable, and the large red, green, and shadow cel regions stay separated at both reduced scales. Linear half scale makes roof and curved pond/rock edges visibly coarser; the small foreground figure is rougher. FSR 1 restores some edge contrast, with some fine-edge softness remaining. No obvious cel-band dropout appears across the sampled pan. Owner review is still needed before any default change.

### 500×500 fixed-LOD timing, first matrix

Each row has 128 steady samples: the first 32 frames of each of four yaw blocks were discarded. “Primary” is the per-frame sum of `esvo_traverse_shade_ms`, `shadow_visibility_wave_ms`, and `spatial_reuse_ms`. “Displayed” is `whole_frame_gpu_span_ms`. Values are milliseconds, p50/p95; p95 uses inclusive interpolation.

| Mode | Primary p50 / p95 | Displayed p50 / p95 | p50 reduction vs native: primary / displayed | Displayed p95 delta |
| --- | ---: | ---: | ---: | ---: |
| Native / linear, R/D 1.0 | 38.25 / 60.98 | 39.82 / 62.36 | — | — |
| Linear, R/D ≈ 2/3 | 23.56 / 36.24 | 25.32 / 38.31 | 38.4% / 36.4% | −38.6% |
| Linear, R/D 1/2 | 25.45 / 30.55 | 27.40 / 33.02 | 33.5% / 31.2% | −47.1% |
| FSR 1, R/D 1.0 | 35.30 / 51.05 | 37.03 / 52.78 | 7.7% / 7.0% | −15.4% |
| FSR 1, R/D ≈ 2/3 | 19.43 / 35.18 | 21.27 / 38.07 | 49.2% / 46.6% | −39.0% |
| FSR 1, R/D 1/2 | 10.69 / 25.36 | 12.75 / 27.61 | 72.1% / 68.0% | −55.7% |

In this first matrix, FSR 1 meets the proposed 30% primary reduction at 2/3, 50% at 1/2, 20% displayed p50 improvement, and no displayed p95 regression. FSR input AA / EASU / RCAS p50 costs were **0.0147 / 0.0547 / 0.0141 ms** at scale 1, **0.0062 / 0.0426 / 0.0111 ms** at 2/3, and **0.0036 / 0.0350 / 0.0091 ms** at 1/2. Their summed p50 costs were 0.0835, 0.0600, and 0.0477 ms.

### 500×500 half-scale interleaved repeat

The first-matrix half-scale FSR p50 did not reproduce. A later FSR / linear / FSR sequence recorded:

| Mode | Samples | Primary p50 / p95 | Displayed p50 / p95 |
| --- | ---: | ---: | ---: |
| FSR 1 half, repeat A | 128 | 28.62 / 41.76 | 31.15 / 44.58 |
| Linear half | 128 | 27.48 / 38.50 | 29.45 / 40.46 |
| FSR 1 half, repeat B | 128 | 26.51 / 44.22 | 29.04 / 46.75 |

Against the original single native reference, those FSR primary p50s imply only 25–31% reduction, not the 50% target. FSR 1 and linear half were close in the interleaved repeat. The displayed p50 remained lower than the original reference, and the displayed p95 did not regress, but the primary half-scale gate is **not reproducibly met**. No same-sequence native control or GPU clock/power telemetry was captured for this A/B/A run, so these values are not averaged with the first matrix.

### Coupled LOD, measured separately at 500×500

The fixed LOD override was cleared for these runs. This table is separate from the isolated fixed-footprint gate.

| Mode | Primary p50 / p95 | Displayed p50 / p95 |
| --- | ---: | ---: |
| Linear, R/D ≈ 2/3 | 33.16 / 39.49 | 35.18 / 41.96 |
| Linear, R/D 1/2 | 28.04 / 34.96 | 30.05 / 37.08 |
| FSR 1, R/D ≈ 2/3 | 10.61 / 12.66 | 12.53 / 14.61 |
| FSR 1, R/D 1/2 | 8.44 / 10.13 | 10.31 / 12.37 |

The FSR pan produced 768 steady samples; its primary p50/p95 were 10.71/18.87 ms and displayed p50/p95 were 12.39/20.99 ms. These are motion samples and are not mixed into the fixed-angle gate table.

### Dispatch, LOD, fixed costs, and memory observations

The runtime logs confirm that presentation remains 500×500 while the primary dispatch follows the offscreen target: scale 1 uses 62×62 workgroups (the preserved canonical floor tiling), 2/3 uses 334×334 pixels and 42×42 workgroups, and 1/2 uses 250×250 pixels and 32×32 workgroups. At 1080p, the corresponding inputs were 1920×1080 (240×135 groups), 1281×721 (161×91), and 960×540 (120×68). The fixed-LOD coefficient and four-slot lighting-config readback were identical across the matrix.

At 500×500, the 1/2 target has 73% fewer primary workgroups than scale 1, but linear primary p50 improved only 33.5%. The ray-march stage dominates the measured primary sum; shadow-wave and spatial-shade p50s were small by comparison. The median difference between displayed span and primary sum was about 1.6–2.0 ms. Tracked steady-state uploads were zero, allocated capacity was 1,895,599 bytes, and populated shader-readable data was 212,992 bytes across the first matrix. These are buffer/upload ledgers, not a GPU memory-bandwidth measurement.

### 1080p follow-up

The first 500×500 matrix met all proposed performance thresholds, so the 1920×1080 matrix was run. The later repeat showed why the half-scale result should remain diagnostic rather than be used to promote a default.

| Mode | Primary p50 / p95 | Displayed p50 / p95 | p50 reduction vs native: primary / displayed | Displayed p95 delta |
| --- | ---: | ---: | ---: | ---: |
| Native / linear, R/D 1.0 | 257.33 / 391.89 | 259.00 / 393.66 | — | — |
| Linear, R/D ≈ 2/3 | 148.13 / 198.72 | 149.84 / 200.49 | 42.4% / 42.1% | −49.1% |
| Linear, R/D 1/2 | 75.42 / 131.87 | 77.03 / 133.51 | 70.7% / 70.3% | −66.1% |
| FSR 1, R/D 1.0 | 230.62 / 374.65 | 233.07 / 377.09 | 10.4% / 10.0% | −4.2% |
| FSR 1, R/D ≈ 2/3 | 150.28 / 225.85 | 152.74 / 228.85 | 41.6% / 41.0% | −41.9% |
| FSR 1, R/D 1/2 | 138.51 / 207.46 | 141.49 / 210.78 | 46.2% / 45.4% | −46.5% |

The 1080p FSR input AA / EASU / RCAS p50 costs were 0.0861/0.3605/0.0873 ms at scale 1, 0.0530/0.4809/0.1160 ms at 2/3, and 0.0435/0.6673/0.1596 ms at 1/2. The first matrix missed the 50% half-scale primary target.

The 1080p A/B/A half-scale repeat measured FSR primary p50s of 134.11 and 131.45 ms, versus 147.08 ms for linear; the displayed p50s were 137.07 and 134.28 ms versus 149.31 ms. This reverses the first matrix’s ranking (FSR primary p50 138.51 ms versus linear 75.42 ms). Both runs show lower FSR p95 than the native reference, but the mode ranking is sensitive to run order/state. The repeated FSR primary reductions versus the first native reference are about 48–49%, still below 50%.

### Canonical correctness and build witnesses

The final Dozen run passed `LookdevCapture.RendersFourStatesAtFourAnglesDeterministically` (exit 0, 401.339 s). Its four scale-1 PNG hashes exactly matched `dozen-baseline-midday`: `d000c8d0047cc34b2e50374c2de03dacc9b83108f317a4571c8fc7ecaf41ea2c`, `e66db895dbe610f0a935a61135f75340b3767737c951d1d780cbeb1e01c320fe`, `d6b3421d56f3c986ca05521da3e45c535ce2f30b5aaa4cd8e2a6650dac4d7f36`, and `d743fd38da561628b760f8979c1b9978f2c47ecf07008a719ee41fab66edc3d9`. FSR and the fixed-exposure experiment flag were disabled for this canonical check.

The native MSVC Release build on the merged tree linked `vixen_lookdev_capture.exe` with exit 0 and no C4457 or other compile error. FSR AA/EASU/RCAS shaders compiled successfully during native captures. The merged SDI generated-header check passed after regeneration; emitter checks and the Dozen target build passed. The full CTest matrix was not used as the final parity witness; the focused queued Dozen capture test was.

## STOPs

- **Native compile STOP cleared:** lane-gpuyield `e7d8a198` was merged at `decc023f`; the final native build and captures succeeded. No remaining C4457/MSVC consumer failure was observed.
- **Performance gate not reproducibly met:** the first 500×500 matrix passed, but the half-scale primary reduction fell below 50% in the interleaved repeat and the FSR/linear ranking changed between repeats. Do not claim an accepted cost gate or change defaults from these data. A same-sequence native control and GPU clock/power telemetry are needed for a trustworthy comparison.
- **Product integration decision referred to orchestrator:** this experiment hand-constructs the FSR stages in `BuildRenderGraph.cpp` and selects them through environment variables. The current VIXEN render facade has no typed upscaler capability/module for `CapabilityGraph` selection with a native fallback. Choose whether to add that declarative capability or keep this as a local measurement prototype; no default was changed. Stage 2 temporal work was not started.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Declare render scale and upscaler stages in the render facade
- proposed: Set up native test runtime DLL paths in the capture launcher
- proposed: Provide a fixed exposure capture mode for scale comparisons
- proposed: Make the direct native build route callable from WSL
- proposed: Clarify GPU resource locking versus exclusive heavy-slot admission
- proposed: Provision CodeGraph index for VIXEN lane worktrees
- proposed: Parameterize native capture output extent
- proposed: Refresh stale merged SDI headers on convergence merges
- proposed: Stabilize and interleave native GPU timing captures
