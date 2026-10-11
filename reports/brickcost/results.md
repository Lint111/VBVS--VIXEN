# Native brickmap traversal probe cost

## LANDABLE NOW

- `3c50ae1d` guards traversal and RT investigation probes with `VIXEN_COMPOSITION_COUNTERS` and erases off-mode call sites with macros.
- `4e8c815d` adds an opt-in Stored-SDF capture scene for the native look-dev harness. The ordinary capture remains unchanged unless `VIXEN_BRICKMAP_SCENE=1` is set.
- The default backend selection and feature defaults were not changed.

## STOPs

None. The native matrix completed with four fresh-process repeats at 500×500 and two at 1920×1080. Each repeat contributes 512 post-warmup samples per backend, exceeding 500 frames per condition.

## Method

The first LookdevCapture fixture was procedural-only. It could not execute `traverseCoarseGridInstancedSdf`, so its brickmap timings were not evidence about DDA. The capture now has an opt-in mode that retains the graph's six `FORMAT_STORED_SDF` instances and orbits them through the same four lighting states and four camera yaws. Every capture log confirmed both the scene seed and DDA backend activation.

Each fresh process captures four yaw angles at 64 frames per angle. The first 32 frames of each angle are excluded. A repeat therefore contributes 4 states × 4 angles × 32 measured frames = 512 samples per backend. The 500×500 matrix has four paired repeats (2,048 samples per backend and condition); the 1920×1080 matrix has two paired repeats (1,024 samples per backend and condition).

The before side used the pre-guard shader sources from `36af99df`; the after side used the committed guard sources. The native executable, stored scene, settings, camera angles, and capture protocol were held constant. Timings below use `esvo_traverse_shade_ms` for the raymarch/shade GPU stage and `shadow_visibility_wave_ms` for the shadow wave. Each number is the median of the per-repeat medians; “saved” is the median of the paired before-minus-after repeat values. Positive values mean the guarded version was faster.

Captures and CSV files are under `/mnt/c/vixen-native/brickcost/reports/brickcost/native/` in tags `brickmapscene500-baseline`, `brickmapscene500-guarded`, `brickmapscene1080-baseline`, and `brickmapscene1080-guarded`.

## Timings

### 500×500

| Backend | Ray/shade before → guarded (saved) | Shadow before → guarded (saved) |
|---|---:|---:|
| ESVO, flag off | 1.713 → 1.463 ms (+0.251 ms, 14.7%) | 0.074 → 0.073 ms (+0.001 ms) |
| Standalone brickmap DDA | 3.782 → 3.802 ms (−0.023 ms, −0.6%) | 0.041 → 0.041 ms (+0.001 ms) |
| RTQUERY | 1.916 → 0.812 ms (+1.104 ms, 57.6%) | 0.041 → 0.045 ms (−0.003 ms) |
| Composed | 1.914 → 0.804 ms (+1.108 ms, 57.9%) | 0.039 → 0.040 ms (−0.002 ms) |

### 1920×1080

| Backend | Ray/shade before → guarded (saved) | Shadow before → guarded (saved) |
|---|---:|---:|
| ESVO, flag off | 15.258 → 13.713 ms (+1.545 ms, 10.1%) | 1.390 → 1.419 ms (−0.029 ms) |
| Standalone brickmap DDA | 33.962 → 35.058 ms (−1.096 ms, −3.2%) | 1.334 → 1.344 ms (−0.010 ms) |
| RTQUERY | 18.838 → 7.507 ms (+11.331 ms, 60.1%) | 1.350 → 1.343 ms (+0.007 ms) |
| Composed | 18.823 → 7.493 ms (+11.330 ms, 60.2%) | 1.347 → 1.347 ms (−0.001 ms) |

Shadow-wave changes are small and within the observed run-to-run variation. The significant effect is in the raymarch/shade stage: removing probes saves 10–15% on ESVO and about 58–60% on RTQUERY/composed, but does not measurably improve standalone DDA.

The image files were compared by SHA-256: all 256 matched 500×500 pairs and all 128 matched 1920×1080 pairs were byte-identical (384 pairs, no mismatches).

## Probe and cell-step evidence

The unguarded 500×500 r1 logs report, summed across four lighting-state processes:

- Standalone DDA: 9,201,920 `FarFieldCandidates` increments.
- RTQUERY/composed: 9,202,688 `FarFieldCandidates` and 9,221,632 `RtLoopEntries` increments.
- ESVO: 1,188,608 `FarFieldColorResolved` and 430,080 `FarFieldRectRays` increments.

The corresponding guarded logs report zero for these investigation counters across all four configurations. `MipPoolSize` remains because it is a resource-size report, not a traversal probe.

A temporary native-only measurement counter, compiled only for `VIXEN_BRICKMAP_TRAVERSAL`, counted valid DDA loop invocations and in-bounds cell steps on the same 500×500 Stored-SDF scene. Across four lighting states it recorded 1,202,688 loop invocations and 9,274,112 cell steps: **7.711 cell steps per DDA loop invocation**. The source was restored to the committed guarded shader immediately afterward. The baseline's 9,201,920 per-cell candidate increments are close to the 9,274,112 measured cell steps, confirming that the old candidate probe fired on nearly every visited cell.

Despite removing those atomics, DDA remained 3.80 ms at 500×500 and 35.06 ms at 1920×1080, versus guarded ESVO at 1.46 and 13.71 ms. On this six-body Stored-SDF fixture, the unguarded DDA path was already about 121–123% slower than unguarded ESVO; after guarding, the gap is about 156–160%. This fixture does not reproduce the earlier +62–70% result, and its corrected DDA path is substantially slower. The first procedural-only fixture could not measure DDA at all. Probe removal is therefore a useful cleanup for the other backends, but it does not explain the standalone DDA penalty here. The results point toward the flat DDA's repeated cell lookup/refinement work; a hierarchical-skip A/B would be needed to establish its contribution.

## Standalone DDA recommendation

`BuildRenderGraph.cpp` resolves composed traversal by capability: RTQUERY when ray-query is available and grid DDA when it is not. On this native device, composed resolved to RTQUERY, which is why its timings closely match the RTQUERY mode. Outside that composed fallback, `VIXEN_BRICKMAP_TRAVERSAL` is an explicit standalone A/B switch.

Recommendation: remove the standalone override from the supported product path after this A/B use is retired, but retain the DDA implementation as the composed software fallback for devices without ray-query. Deleting the DDA implementation itself would remove that fallback. The DDA default and override were not changed in this work.

## Verification and recovery

- The fresh native `vixen_lookdev_capture` target was built through the live box queue. Runtime shader compilation and capture succeeded for both pre-guard and guarded sources.
- Queue command: `bash /home/liory/.local/bin/with-test-lock.sh --agent brickcost --resource build --label brickcost:baseline-native-build-defaults --credits 4 -- bash reports/brickcost/reproduce/native-build.sh`.
- GPU captures also ran through the live queue with `--resource gpu`. No raw build or test command was used.
- No full test suite was run; this task's witness was the queued native build plus the native GPU capture matrix and output comparison.
- The native setup required runner fast-forward ordering, Git `safe.directory`, a heartbeat, two MSVC compatibility flags, preserving MSVC's default compiler flags, and explicit PowerShell `ExtraEnv`. Each recovered setup gap is recorded as an SPT proposal in `.spt-proposals/brickcost.jsonl`.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- Native VIXEN runner must fast-forward before launching fetched scripts
- Native VIXEN runner must pass safe.directory for pinned WSL sources
- Native VIXEN build helper needs heartbeat output
- Native MSVC preset must handle generated C4457 warnings
- Native MSVC preset must enable extended aligned storage
- Native measurement preset overrides must preserve Windows compiler defaults
- Native capture wrapper should expose ExtraEnv clearly
- Native look-dev capture must support stored-SDF traversal fixtures
