# Lane vxtrain4 — VIXEN merge train 2

## LANDABLE NOW

Product tree tip: `d3d7700f`; report and runner commits are on top through `69a67a8a`. The lane remains based on `origin/wave/authoring-convergence` at `a48a42df`.

### Ordered merges

| Lane | Merge commit | Lane tip | Note |
| --- | --- | --- | --- |
| `lane-travdefault` | `6ead78d1` | `d9c25b5e` | Composed traversal is on by default; updated R491 inventory rows in `/home/liory/codeman-cases/undertow/reports/vixenmodules.csv`. |
| `lane-togglevx` | `5f30e53b` | `a4715ae2` | R485 binary32 recipe GLSL operands; pinned VIXEN to kernel `3803e4a6a813ab23710cfe24695b069186d6a551`, which includes kernel half `8760581a`. |
| `lane-lightunify` | `545f0026` | `363bf151` | Night lights moved out of the hull and capture checker added. Conflict resolutions retain the R505 SDF guard and R506 readback, and preserve bindings 35, 48, 49 with count 30. |
| `lane-mirrorassert` | `26bfc844` | `69a18bda` | Assert-based test mains keep assertions enabled in Release. |
| `lane-planetoffline` | `7066e37d` | `73011ebb` | Mitsuba offline reference pipeline under `tools/planetref/` (Python only). |

All merges were made in the requested order, with one merge commit each. The wave base is an ancestor of the lane tip.

### Generation and builds

- Pin regeneration checked all 26 generated outputs with no diff; all 26 `*_check` targets and `no_new_mutex_check` passed. Logs: `/home/liory/.local/state/undertow/undertow-box-logs/1791741849-build-vxtrain4:configure-pin-3803.log`, `1791742105-build-vxtrain4:regen-pin-3803.log`, and `1791742390-build-vxtrain4:all-checks-pin-3803.log`.
- Focused Release build of `vixen_lookdev_capture`, `test_rendergraph_criticalnodes_gpurender2b`, and `test_uber_shader_splice` passed: `/home/liory/.local/state/undertow/undertow-box-logs/1791742956-build-vxtrain4:lightunify-conflict-targets.log`.
- Full Linux Release build passed on retry 2 (383 Ninja steps): `/home/liory/.local/state/undertow/undertow-box-logs/1791744965-build-vxtrain4:linux-release-final-retry2.log`. The first attempt was stopped by the queue idle watchdog; the first retry then encountered a missing object after that interruption. Retry 2 recovered Ninja's interrupted build state and completed successfully. The final success is fresh; no stale binary is used as evidence.
- Native MSVC Release configure and `vixen_editor` build passed on the assigned checkout `C:\vixen-native\vxtrain4`; `test_rendergraph_criticalnodes_windowedcapture` also built successfully. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1791746244-build-vxtrain4:native-msvc-release.log`.
- The planetref pipeline and its 8 unit tests passed. Source EXR hashes stayed unchanged; the fit JSON varied with installed NumPy/SciPy versions, and generated drift was restored. Logs: `/home/liory/.local/state/undertow/undertow-box-logs/1791743606-test-vxtrain4:planetref-pipeline.log` and `1791743705-test-vxtrain4:planetref-unittest.log`.
- The external inventory file change is not part of this repository commit: `VIXEN_COMPOSED_TRAVERSAL` is classified as planned/default-on composed traversal with CapabilityGraph ray query where available and DDA fallback otherwise; `VIXEN_BRICKMAP_TRAVERSAL` is retired as a standalone product switch.

### Full RenderGraph/SVO suite

The wave-base report records 2,124 selected cases and exactly one accepted failure, `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` (opcode 94): `/home/liory/.local/state/undertow/undertow-box-logs/1791736360-gpu-wave-base-rendergraph-svo-suite.log`.

The first full tip run was exposed to concurrent GPU tests and builds. It selected 2,181 cases and reported 15 failures, including fixture-dependent `Not Run` cases, a segfault, and producer/gate timeouts. Its N100 bucket took 253.87 seconds compared with 40.46 seconds in the published wave run. A second run using the standard test resource was explicitly interrupted at 654/2,181 after more concurrent GPU jobs began. These are not used as the final product result. Logs: `/home/liory/.local/state/undertow/undertow-box-logs/1791746135-gpu-vxtrain4:rendergraph-svo-final.log` and `1791748392-test-vxtrain4:rendergraph-svo-final-testqueue.log`.

An exclusive, serial recovery run is pending in the global box queue as `vxtrain4:rendergraph-svo-exclusive-recovery` (`ctest --test-dir build/wsl -L 'RenderGraph|SVO' --output-on-failure --parallel 1`). It requests all 15 queue tokens so concurrent jobs cannot contaminate the witness. Final result and exact failing-name comparison: pending.

### Native performance and visual witnesses

The previous `travdefault` lane measured Radeon 8060S look-dev at 1920×1080: whole-frame p50 composed→ESVO `22.760→42.962 ms` (+20.202 ms), traversal/shade `18.637→38.694 ms` (+20.057 ms). These are prior-lane results, not an A/B measurement on this merged tip. The current-tip 1080p A/B runner and native R6 are pending.

Current-tip midday, overcast, and night-service capture sheets and pixel comparisons are pending. Expected attribution from the merged work is pixel-identical midday/overcast, with the intended night-only change from lightunify; R505 traversal default should not alter pixels.

## STOPs

- No product compile or generation failure is established. The full Linux Release build and all generated checks passed.
- The final full RenderGraph/SVO result, native R6 ≤2.0 ms result, current-tip native 1080p A/B timings, and current-tip look-dev sheets/comparisons are still pending at this report revision. Do not treat this report as a complete landing witness until these are filled or recorded as unfinished at the lane checkpoint.
- While composing a proposal command, shell backticks inside a double-quoted body triggered an accidental unqueued `cmake --build build/wsl --parallel 4` for about 44 seconds. The processes were terminated when detected. This output was not used as evidence; all reported build/test evidence above came from the queue.

## SPT DISPOSITION

No tracked SPT task IDs were attached.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Pin planetref numerical dependencies for reproducible fit witnesses
- proposed: Add progress-preserving log capture for queued VIXEN builds
- proposed: Make GPU-backed VIXEN correctness suites queue-exclusive
