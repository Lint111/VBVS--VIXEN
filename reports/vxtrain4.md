# Lane vxtrain4 — VIXEN merge train 2

## LANDABLE NOW

Product tree tip: `d3d7700f`; follow-up harness commit: `6d5d28f3`. The lane remains based on `origin/wave/authoring-convergence` at `a48a42df` (refetched and unchanged).

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

### GPU suite and B1 attribution

The first clean exclusive tip suite (`1791754627-gpu-vxtrain4:full-rgsvo-gpu-lock-lander.log`) selected 2,181 cases and completed in 994.35 seconds. Its only failing name was `B1OcclusionAbTest.OccludedInstancesDropIterationsAndPixelsStayIdentical`. `vixen_hud_capture_producer` passed in 53.67 seconds, `vixen_multiinstance_cache_gate` passed in 446.78 seconds, and opcode 94 passed at the pinned kernel. This classifies the earlier HUD timeout and solo-app `-11` as contention; B1 remained reproducible under the exclusive GPU lock.

Per-merge B1 attribution used the same Dozen ICD and target: wave base `a48a42df`, R505 `6ead78d1`, and R485 `5f30e53b` passed; the failure first appeared at R506 `545f0026` and remained at R512 `26bfc844` and R507 `7066e37d`. The diagnosis was Vulkan validation rejecting SPIR-V 1.6 modules under Dozen's Vulkan 1.2 semantics. The B1 executable loads five CMake-built artifacts, so its ray-march variants, Hi-Z, instance cull, and shadow-trace targets now compile for Vulkan 1.2 (SPIR-V 1.5). The focused target builds passed; latest queued build log: `/home/liory/.local/state/undertow/undertow-box-logs/1791761244-build-vxtrain4:b1-all-shader-env-build.log`. The post-fix focused test and full suite are queued together as one `resource=gpu` job; the command records the focused result and runs the full suite even if that focused gate fails. The latest wave-base failing-name set is empty per the updated base result; opcode 94 passed on this pinned tip.

### Dozen look-dev sheets

The three fresh tip captures completed in one queued GPU job (`1791759637-gpu-vxtrain4:tip-midday-overcast-night-captures.log`). Each sheet has four angles across the top wave-base row and four across the bottom tip row:

- Midday: `reports/vxtrain4-visual/comparison/midday-wavebase-v-tip.png`
- Overcast: `reports/vxtrain4-visual/comparison/overcast-wavebase-v-tip.png`
- Night-service: `reports/vxtrain4-visual/comparison/night-service-wavebase-v-tip.png`

The DZN log resolves composed traversal to `grid-DDA`. Midday and overcast retain the same object layout and framing, while the tip shows strong new cast shadows from the R505 default path. They are not pixel-identical: midday changed 250,000/250,000 pixels in each view (maximum channel delta 190); overcast changed 244,048–250,000/250,000 pixels (maximum delta 171). Night-service keeps the same layout; the tip adds warm exterior light pools on the building wall and ground, consistent with R506 moving the lights outside the hull. Night changed 249,543–249,973/250,000 pixels (maximum delta 222). I opened and reviewed all three sheets.

The system Python lacks Pillow (`from PIL import Image, ImageDraw` failed with `ModuleNotFoundError`), so I generated the paired sheets with the repository pixel-comparer's decoder and Python's standard PNG/zlib support. A consolidation proposal records the missing declared dependency.

### Native R6 and 1080p A/B/A

The first two queued native attempts exited 3 before CTest because `run_editor_script.bat` searched for `VIXEN\binaries\vixen_editor.exe`, while MSVC emitted the executable under `build\vxtrain4-native\binaries`. Commit `6d5d28f3` passes the configured executable path into the script. The corrected native R6 plus ESVO/default/ESVO 1080p A/B/A session is queued against that commit; no current-tip R6 budget or A/B/A timing has been witnessed yet. The earlier travdefault values (22.760 ms composed and 42.962 ms ESVO at 1080p) remain prior-lane reference values, not a replacement for this requested session.

## STOPs

- The full suite has not yet completed after the B1 shader-target correction. The queued GPU job is `vxtrain4:final-rgsvo-full-exclusive-vulkan12`; the exact final failing-name set remains pending.
- Native R6 and the native 1080p A/B/A session remain queued. The previous exit-3 attempts are attributed to the launcher path and predate commit `6d5d28f3`.
- At the three-hour checkpoint, stop the queued work, record the exact CTest progress/log state, and leave the pending full-suite/native witnesses for a follow-up lane if they have not completed.

## SPT DISPOSITION

No tracked SPT task IDs were attached.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Pin planetref numerical dependencies for reproducible fit witnesses
- proposed: Add progress-preserving log capture for queued VIXEN builds
- proposed: Make GPU-backed VIXEN correctness suites queue-exclusive
- proposed: Fix look-dev contact-sheet row height
- proposed: Provide a declared dependency for capture-sheet generation
