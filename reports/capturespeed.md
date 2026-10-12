# Lane sn-capturespeed: look-dev capture speed (R536)

Test: `Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically`. One process captures 4 angles of one state; the runner starts 2 fresh processes per state (first + `.repeat`), so 8 processes for 4 states.

Raw numbers: `reports/capturespeed/evidence/timing.txt` (every `[CAPTURE_TIMING]` line) and `png-sha256.txt` (96 hashes). Sample captures I opened: `evidence/png/`. Reproduction: `reports/capturespeed/reproduce/` (`native-build.sh`, `native-capture.sh`).

Labels. **Dozen = Mesa dzn on the 8060S (translated D3D12), non-representative, correctness only.** **Native = AMD proprietary 25.20.18 on the 8060S, contended (no `--exclusive`; that is orchestrator-only).** Adapter and driver are now logged on every run (`[ADAPTER]`).

## FINDINGS

### Per-process timing, one state (midday), one process

| Run | Frames (4 angles) | Wall | Startup | Frame p50 / p95 / max (ms) | Notes |
|---|---|---|---|---|---|
| Dozen, before (uninstrumented baseline) | 256 | 486 s | ~107 s | ~1500 avg | un-instrumented binary; startup is a cold-cache figure |
| Dozen, before (instrumented, 8/angle) | 32 | ~105 s | 59.6 s | 1398 / 1558 / 1699 | `instr-f8` |
| Dozen, persisted cache cold (8/angle) | 32 | ~156 s | 101.3 s | 1673 / 2017 / 2298 | first run on an empty cache |
| Dozen, **after**, warm cache (default 8/angle) | 32 | **52.7 s** | **1.65 s** | 1541 / 1744 / 1799 | `dozen-after-midday-2` |
| Dozen, **after**, via real `run_lookdev_capture.cmake` | 2 x 32 | 97 s for first+repeat | 1.55 s / 1.40 s | 1417 / 1639; 1387 / 1698 | suite budget 900 s |
| Native, before (first run in a fresh clone) | 256 | 48.6 s | 37.5 s | 35.8 / 64.4 / 120 | cold AMD driver cache |
| Native, persisted cache, driver cache cold-ish (64/angle) | 256 | 9.26 s | 3.8 s | 17.13 / 17.40 / 58.8 | |
| Native, persisted cache warm (64/angle) | 256 | 8.68 s | 1.90 s | 17.16 / 42.9 / 71.5 | |
| Native, **after** (default 8/angle) | 32 | **2.72 s** | **1.35 s** | **17.05 / 20.8 / 49.6** | `native-after-midday` |

All four states, native, after (each its own process): wall 2.7 s (midday, late-afternoon, overcast) and 28.8 s (night-service, see ROOT CAUSE 2: its first cold compile); `native-ref64-night-service` at 64/angle: 6.9 s.

Whole-suite estimate (8 processes, no change to structure): Dozen 8 x 486 s = ~65 min before, 8 x ~49 s = ~6.5 min after (plus ~100 s once per cache). Native 8 x 9 s = ~72 s before (steady) or ~6.5 min with a cold driver, 8 x 2.7 s = ~22 s after.

### Startup per phase

Native, before (cold driver, 37.5 s): node-compile 37.1 s = `test_compute_pipeline` (BodyInstanceRayMarch) 13.95 s, `probe_gather` 8.9 s, `shadow_visibility_wave` 7.5 s, `shadow_ray_trace` 2.5 s, `voxel_grid` 1.3 s, `body_octree_scene` 0.73 s, `main_device` 0.38 s. Process start to `BuildRenderGraph` 0.17 s, graph build 0.13 s.

Native, after (1.35 s): process start 58 ms, `BuildRenderGraph` 115 ms, node-compile to first frame 1.18 s, first frame 50 ms. All `vkCreateComputePipelines` calls under 7 ms.

Dozen, cold (101.3 s): `vkCreateComputePipelines` sum 98.7 s = BodyInstanceRayMarch 45.2 s, ProbeGather 28.6 s, ShadowVisibilityWave 19.1 s, ShadowRayTrace 5.4 s. Dozen, warm: 1.65 s total; `vkCreateComputePipelines` sum 10.9 ms.

### Where a frame goes (Dozen, non-representative)

Only `test_dispatch` is a heavy GPU pass. It is tiled into 81 chunks (cold edge floor of 7 workgroups at 63 x 63) of ~16.5 ms each = ~1.3 s per frame. The CPU waits in `vkQueueWaitIdle` (retire, 41 per frame at ~33 ms). Every other node's Execute is under 3 ms. Native: `test_dispatch` Execute p50 29.6 ms on the first run; frame time then sits at the `PaceFrameUntil` floor of ~17.1 ms (60 Hz pacing) because the GPU work no longer exceeds it.

### Evidence integrity

- Native midday: baseline vs after, 4 angles byte-identical. Dozen midday: baseline (64/angle) vs after (8/angle) vs repeat, 4 angles byte-identical each.
- Native frame-count sweep on midday N in {1, 2, 4, 8, 16, 24, 32, 40, 48} vs 64: 0 differing bytes at every N, all 4 angles. late-afternoon, overcast, night-service: default (8) vs 64: byte-identical on all 4 angles.
- Reservoir path enabled (`VIXEN_RESERVOIR_CONFIG_ENABLED=1`) midday at 4, 8, 64 per angle: identical to each other and identical to reservoir-off.
- Native and Dozen hashes differ from each other (different drivers), as expected; each is stable within itself.
- Opened and looked at `native-after-midday-angle-0`, `native-after-night-service-angle-2`, `native-after-overcast-angle-1` (in `evidence/png/`): all show the turntable stage (hull, tree, rock, figure, plinth) with the expected lighting, no blank or corrupt frames.
- The real runner on Dozen (midday) printed the per-process timing and passed the first-vs-repeat byte check.
- The budget fails loudly: `VIXEN_LOOKDEV_CAPTURE_BUDGET_MS=1000` on native gives `look-dev capture exceeded its runtime budget: 2828 ms > 1000 ms`, exit 1.

## ROOT CAUSE

1. **Frame count, not frame cost.** 64 frames per angle were chosen as "two full 32-frame reservoir-history caps" but the capture output does not depend on frame count: it is pixel-identical from 1 frame up to 64 on midday (9 counts tested) and at 8 vs 64 on the other three states, with the reservoir path off or on (midday). Each frame costs ~1.4 s on Dozen (real shader execution on a translated driver) and ~17 ms on native (the 60 Hz pace). Cutting 256 frames to 32 removes ~224 frames: 5.2 min of 8 min per Dozen process, ~4 s of 9 s per native process. Confirmed by removal: native 8.7 s to 2.7 s, Dozen 49 s for 32 frames, images identical.
2. **Startup is pipeline compilation, repeated in every fresh process.** The compute `VkPipelineCache` was never created (null cache; the cacher's serialize/deserialize were stubs), so each of the 8 processes recompiled `BodyInstanceRayMarch`, `ProbeGather`, `ShadowVisibilityWave` and `ShadowRayTrace`. Cost: 60-101 s on Dozen (dzn compiles HLSL/DXIL), 37 s on native when the AMD machine-wide shader cache is cold. The controller's 70 s startup is this cold-cache number. Fix: `ComputePipelineCacher` now owns a lazily created `VkPipelineCache`, and persists it through the existing cacher save/load path under `RuntimeCacheDirectory()` (override `VIXEN_CACHE_DIR`), validated against the device UUID. Confirmed by removal: Dozen startup 101 s to 1.65 s, native 37.5 s to 1.35 s (cold-driver native 3.8 s with only the driver cache warm). Observed limits: the first run after a rebuild on Dozen missed 4 of 12 entries (75 s startup) and the next run hit all 12 (cause not established); night-service has a different `BodyInstanceRayMarch` key from midday, so it pays one cold compile (27 s native) the first time on a machine.
3. **Frame pacing is a floor, not a cause of the multi-minute runs.** `PaceFrameUntil` holds each native frame to ~17 ms; at 256 frames that is ~4.4 s of the baseline's 9 s. The reduced frame count removes most of it. Changing the pacing is a renderer design choice (PROPOSALS).
4. Not causes (checked): host work and fences (`frame-sync` waits ~0 ms), PNG capture (0.5 s per process), GPU tiling (edge 21 vs the learned edge gives ~1.1 s vs ~1.4 s per Dozen frame, same order), software rasterizer (the adapter is the real 8060S through dzn, not WARP).

### Why 8 frames per angle

The count is derived in `test_lookdev_capture.cpp`: `kDefaultFramesPerScenario = 2 x MAX_FRAMES_IN_FLIGHT = 2 x 4 = 8`. All per-frame rings (LightingConfig, camera, previous camera, reservoir config, hit accumulation) hold 4 slots, so every slot must be rewritten for the new angle, and a second full ring gives margin; the `LightingConfig` ring-slot readback runs whenever a capture has at least 4 frames. The reservoir temporal history is capped at 32 samples (`temporalCap`). The reservoir path is off in this capture by default, and even with it on the PNGs are identical at 4, 8 and 64 frames, so the cap is not observable here; to stay conservative the harness keeps two full caps (64) as the default whenever `ResolveReservoirEnabled()` is true. `VIXEN_LOOKDEV_CAPTURE_FRAMES` (1..64) still overrides.

### Budgets (stated and failing loudly)

- Per process: `VIXEN_LOOKDEV_CAPTURE_BUDGET_MS`, default 300 s. Measured worst cases: Dozen cold-cache 8/angle ~156 s, Dozen with the 4-miss cache 125 s, native cold-driver ~49 s. Warm: Dozen ~50 s, native ~2.7 s.
- Suite: `LOOKDEV_CAPTURE_BUDGET_S`, default 900 s, set in `test_lookdev_capture.cmake`; the ctest `TIMEOUT 1200` stays as the outer backstop. Measured: Dozen first+repeat of one state 97 s, so four states are ~390 s warm. The runner now prints wall, startup, per-frame p50/p95 and the budget for every process.

## LANDABLE NOW

Branch `lane-capturespeed` (based on `d15a7141`), not pushed:

- `543fcb7f` env-gated phase timing (`VIXEN_PHASE_TIMING=1`), `[ADAPTER]`, harness `[CAPTURE_TIMING]`
- `664e9f11` native Windows build reproduction
- `a76e0692` native Windows capture runner
- `e9f2c61d` persist the compute `VkPipelineCache`
- `08f40272` derive frames from the ring size; process and suite runtime budgets; runner timing print
- tip: the commit that adds this report and `reports/capturespeed/evidence` (see `git log -1`)

Everything is behind the existing build; the phase timing is off unless `VIXEN_PHASE_TIMING` is set. Output bytes are unchanged on every state and angle tested.

## PROPOSALS

These are owner decisions; none is made here.

1. **Capture structure.** Today each state runs 2 fresh processes (first and `.repeat`) of 4 angles. With the fixes a process costs ~2.7 s native / ~49 s Dozen, so the structure is no longer the dominant cost on native. Options: (a) keep it; (b) one process per state captured twice in-process (halves processes, loses the fresh-process determinism witness); (c) one process for all 16 captures plus a fresh-process repeat of one state (the cold compile is paid once, not per state). On Dozen the frame cost (32 frames x 1.4 s per process) is what remains, so (b)/(c) would help there only.
2. **Frame pacing in capture mode.** `PaceFrameUntil` floors each frame at ~17 ms. Skipping pacing for offscreen capture would cut a further ~0.5 s per native process; it touches the renderer's frame loop, so it is yours to decide.
3. **Cold driver compile (37 s native, 60-101 s Dozen, once per new shader key).** The persisted cache removes it from repeat processes, but not from a clean machine or CI cache. A CI-persistent `VIXEN_CACHE_DIR` for the look-dev capture would make that first-run cost a one-time event.
4. **Frame count policy.** If you want a stricter "converged" witness than ring warm-up, say so; the evidence is that output does not change from 1 to 64 frames, which also means the old 64 never protected anything in this scene.

## STOPs

- None blocking. The controller's 34 min and 70 s figures were not reproduced exactly; they match the cold-cache, 64-frame structure (Dozen 8 x 486 s = 65 min here, but Dozen speed varies with driver warmth: 4.8 s, 47.8 s and 101 s startups seen on identical binaries).
- Not done: native timing with `--exclusive` (orchestrator-only); Dozen runs for late-afternoon, overcast, night-service (Dozen is correctness-only, so I ran midday there and all four states native); a full 4-state ctest on native (each state ran as its own job instead, as the lane rules require).
- Unexplained: Dozen's first run after a rebuild missed 4 of 12 cached pipelines (see ROOT CAUSE 2).

## SPT DISPOSITION

The brief names no SPT items and I created or changed none. If the owner wants tracking: one item for the frame-count and budget change (landed), one for the persisted compute pipeline cache (landed), one each for PROPOSALS 1-3.

## CONSOLIDATION ISSUES

- The compute pipeline cacher's `VkPipelineCache` serialization was a stub while the graphics `PipelineCacher` persisted its cache: the two cachers should share one cache owner. The compute side reuses `PipelineCacheBlobMatchesDevice` from `PipelineCacher.h` for validation.
- The old harness comment "two full 32-frame reservoir-history caps" justified 64 frames with a number that never reached the output; any other captures with that default (look for `kDefaultFramesPerScenario`-style constants) deserve the same sweep.
- `native-capture.ps1` strips every inherited `VIXEN_*` variable then sets its own, so `VIXEN_CACHE_DIR` must be passed through the `ExtraEnv` argument to use a non-default cache location.
- ctest `TIMEOUT 1200` is now looser than the 900 s suite budget; keep the budget as the primary gate and drop the timeout only if the outer limit is no longer wanted as a backstop.
