# Upscale step (a): final native measurement

## Protocol

All captures ran on the AMD Radeon(TM) 8060S Graphics, driver `32.0.22018.5`, in the native Windows checkout. Each requested round ran as its own `with-test-lock.sh --resource gpu --gpu-benchmark` job and completed with exit 0.

For each candidate, the sequence was native A-before, candidate B, native A-after, with the same four camera yaws and recipe. Fixed LOD used `VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE=0.00165685425`; coupled LOD left the override unset. Exposure was fixed at 1, recipe seed was `0x5a17f13d`, and each yaw captured 64 frames. Reported gains compare B's aggregate p50 to the mean of the adjacent A-before and A-after p50 values. P95 deltas use the same adjacent-control calculation. Bracketed values are the minimum and maximum of the three per-round percentage deltas.

The primary pass metric is the sum of ESVO traversal/shade, shadow visibility, and spatial reuse GPU times. The displayed-workload metric is `whole_frame_gpu_span_ms`. A reduced-scale candidate passes when median primary gain is at least 30% at 2/3 or 50% at 1/2, median displayed gain is at least 20%, and neither primary nor displayed p95 regresses in any round. Scale 1 is diagnostic and has no reduction threshold.

Capture windows (UTC) were fixed 500×500: 20:38:51–21:02:48; coupled 500×500: 21:02:53–21:12:42; fixed 1920×1080: 21:12:47–22:09:00 on 2026-10-16. Per-capture UTC start, completion, and elapsed time are retained in each `run-timing.txt`.

## Fixed LOD, 500×500

| Candidate | Primary p50 gain % (median [min,max]) | Displayed p50 gain % (median [min,max]) | Primary p95 delta % (median [min,max]) | Displayed p95 delta % (median [min,max]) | Gate |
|---|---:|---:|---:|---:|:---:|
| Linear, 1 | −1.17 [−1.66, 0.36] | −1.19 [−2.32, −0.97] | −2.40 [−6.26, 4.55] | −2.14 [−5.15, 5.49] | n/a |
| Linear, 2/3 | 45.87 [42.48, 45.94] | 43.12 [41.22, 44.11] | −39.42 [−39.55, −38.55] | −34.43 [−40.14, −34.21] | PASS |
| Linear, 1/2 | 58.24 [57.65, 61.32] | 55.16 [55.03, 59.53] | −59.15 [−60.33, −51.98] | −57.23 [−58.64, −45.74] | PASS |
| FSR 1, 1 | 2.81 [1.61, 47.00] | 1.46 [0.71, 43.59] | −5.88 [−68.27, 2.49] | −5.51 [−65.33, 3.75] | n/a |
| FSR 1, 2/3 | 43.25 [42.58, 84.14] | 40.31 [39.47, 81.24] | −36.93 [−45.90, −32.55] | −32.18 [−44.21, −26.41] | PASS |
| FSR 1, 1/2 | 57.45 [45.14, 77.71] | 54.47 [42.43, 73.47] | −51.51 [−57.31, −47.55] | −46.80 [−55.65, −46.65] | PASS |

**Extent verdict:** all four reduced-scale candidates pass at 500×500 in fixed LOD.

## Coupled LOD, 500×500

| Candidate | Primary p50 gain % (median [min,max]) | Displayed p50 gain % (median [min,max]) | Primary p95 delta % (median [min,max]) | Displayed p95 delta % (median [min,max]) | Gate |
|---|---:|---:|---:|---:|:---:|
| Linear, 2/3 | 44.65 [37.98, 65.32] | 43.83 [34.17, 60.95] | −42.86 [−69.43, −40.16] | −38.32 [−68.25, −35.35] | PASS |
| Linear, 1/2 | 61.99 [54.23, 75.86] | 57.98 [52.21, 72.35] | −52.96 [−60.43, −48.19] | −48.62 [−59.32, −41.31] | PASS |
| FSR 1, 2/3 | 51.07 [49.77, 56.49] | 47.68 [44.43, 51.58] | −62.88 [−63.27, −43.59] | −59.45 [−59.99, −41.56] | PASS |
| FSR 1, 1/2 | 56.03 [54.11, 81.06] | 48.83 [47.46, 76.86] | −74.86 [−81.30, −58.97] | −68.85 [−77.70, −52.35] | PASS |

**Extent verdict:** all four reduced-scale candidates pass at 500×500 in coupled LOD.

## Fixed LOD, 1920×1080

| Candidate | Primary p50 gain % (median [min,max]) | Displayed p50 gain % (median [min,max]) | Primary p95 delta % (median [min,max]) | Displayed p95 delta % (median [min,max]) | Gate |
|---|---:|---:|---:|---:|:---:|
| Linear, 1 | −5.98 [−6.36, 49.49] | −5.71 [−7.29, 49.43] | 5.03 [−42.13, 7.35] | 5.88 [−42.00, 6.52] | n/a |
| Linear, 2/3 | 48.68 [−22.26, 70.92] | 48.60 [−22.01, 70.00] | −35.32 [−70.33, −26.03] | −35.40 [−69.76, −25.57] | PASS |
| Linear, 1/2 | 53.65 [50.01, 62.09] | 52.38 [49.54, 61.11] | −50.95 [−72.85, −41.19] | −50.97 [−71.41, −40.23] | PASS |
| FSR 1, 1 | −2.63 [−45.89, 11.48] | −3.46 [−46.05, 11.04] | 3.73 [2.41, 40.33] | 3.65 [3.27, 39.65] | n/a |
| FSR 1, 2/3 | 28.26 [11.11, 28.58] | 27.71 [9.77, 28.25] | −29.36 [−30.55, 2.17] | −27.04 [−29.13, 3.07] | **FAIL** |
| FSR 1, 1/2 | 69.80 [41.55, 78.38] | 68.57 [40.42, 77.84] | −56.35 [−77.43, −43.49] | −55.84 [−76.35, −42.88] | PASS |

**Extent verdict:** the 1080p gate is not met across all candidates. FSR 1 at 2/3 misses the 30% primary median threshold and has a positive p95 delta in one round (+2.17% primary, +3.07% displayed). Linear at both reduced scales and FSR 1 at 1/2 pass. The round spread is material for several 1080p candidates; the adjacent controls make that variation visible rather than hiding it in a single reference.

## FSR 1 pass cost

This separate queued sweep used one FSR 1 capture per profile, four yaws, and 64 frames per yaw. Values are p50/p95 in milliseconds across the 128 steady samples (frames 33–64 per yaw). The final column is the sum of the three stage p50s, not a separately sampled total percentile. Input AA is included.

| Profile | Input AA p50/p95 ms | EASU p50/p95 ms | RCAS p50/p95 ms | Sum of stage p50s ms |
|---|---:|---:|---:|---:|
| Fixed 500, scale 1 | 0.0087 / 0.0113 | 0.0310 / 0.0405 | 0.0083 / 0.0108 | 0.0480 |
| Fixed 500, 2/3 | 0.0047 / 0.0172 | 0.0269 / 0.1173 | 0.0074 / 0.0301 | 0.0390 |
| Fixed 500, 1/2 | 0.0028 / 0.0030 | 0.0239 / 0.0242 | 0.0064 / 0.0066 | 0.0331 |
| Coupled 500, 2/3 | 0.0039 / 0.0044 | 0.0238 / 0.0248 | 0.0065 / 0.0068 | 0.0342 |
| Coupled 500, 1/2 | 0.0027 / 0.0029 | 0.0238 / 0.0243 | 0.0064 / 0.0066 | 0.0329 |
| Fixed 1080p, scale 1 | 0.0515 / 0.0668 | 0.2114 / 0.2786 | 0.0522 / 0.0665 | 0.3151 |
| Fixed 1080p, 2/3 | 0.0264 / 0.0460 | 0.2337 / 0.4202 | 0.0569 / 0.1012 | 0.3170 |
| Fixed 1080p, 1/2 | 0.0126 / 0.0144 | 0.1861 / 0.2115 | 0.0458 / 0.0514 | 0.2445 |

## Correctness and visual review

- Native A/B/A captures prepared and completed for native, Linear, and FSR 1 at scale 1, 2/3, and 1/2. The scale-1 native fallback remains the default; no default was changed.
- `SubGraphNodesTest.TypedGroupInputConnectsToItsVariadicMemberSlot` passed. The render path binds the selected module input from the typed slot declaration; provider code does not wire the outer sub-graph input by hand.
- `Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically` passed under Dozen. `diff -qr` against `build/wave-reference-src/build/wsl/runtime-captures/lookdev` found the complete capture tree identical (32 PNGs).
- I inspected the fixed and coupled 500×500 sheets, the fixed 1080p sheet, individual reduced-scale FSR stills, and representative frames from the slow pan. The broad cel-color bands remain visible through the turns. At 1/2 scale, the small figure limbs and foliage contours are visibly coarser/softer; FSR 1 keeps the large silhouette and color regions, with no apparent band dropout. The low-poly scene looks similar in the adjacent A/B/A rows at a glance, while small edge detail is the main visible loss. The owner should review the full sheets before any default change.

Paired sheets (all three rounds for each workload):

- Fixed 500×500: [round 1](sheets/fixed-500-round-1.png), [round 2](sheets/fixed-500-round-2.png), [round 3](sheets/fixed-500-round-3.png)
- Coupled 500×500: [round 1](sheets/coupled-500-round-1.png), [round 2](sheets/coupled-500-round-2.png), [round 3](sheets/coupled-500-round-3.png)
- Fixed 1080p: [round 1](sheets/fixed-1080-round-1.png), [round 2](sheets/fixed-1080-round-2.png), [round 3](sheets/fixed-1080-round-3.png)
- Existing slow-pan sequence: [pan animation](../stage1-final/500/fsr1-pan-scale-2-3/slow-pan.apng), [pan contact sheet](../stage1-final/500/pan-contact-sheet.svg)

The raw run tree includes the paired CSVs, still PNGs, sheets, per-capture timing, manifests, and adjacent-control trial summaries. FSR stage CSVs and profile timing are under `fsr-stage-cost/`. GPU clock and power counter paths were not exposed by Windows performance counters; `amd-smi.exe` was present but its probe did not return usable telemetry. Per-capture UTC timestamps and elapsed times are recorded instead.

## R604 module boundary for the provider lane

Central module declarations and implementations are in `VIXEN/application/main/include/graph/UpscalerModules.h` and `VIXEN/application/main/source/graph/UpscalerModules.cpp`. The provider shaders and AMD FidelityFX FSR 1 source/notice are `VIXEN/shaders/FsrInputAA.comp`, `Fsr1Easu.comp`, `Fsr1Rcas.comp`, and `VIXEN/shaders/third_party/amd_fsr1/` (`ffx_fsr1.h`, `ffx_a.h`, `license.txt`, `README.md`; MIT notice retained).

`BuildRenderGraph.cpp` declares the typed render-path upscaler slot, applies CapabilityGraph selection for `Upscaler:Fsr1`, and preserves the empty-slot native path. `VulkanGraphApplication.h/.cpp` owns render-path configuration and resolves upscaler pass loggers after graph compilation. Generic declared variadic sub-graph input binding and its regression are in `RenderGraph.cpp` and `test_subgraph_nodes.cpp`.
