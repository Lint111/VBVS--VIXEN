# HDR exposure meter lane report (R505)

Worktree: `lane-hdrmeter`; merged wave base `a48a42df` plus `lane-mirrorassert` `69a18bda`. Native checkout: `/mnt/c/vixen-native/hdrmeter`.

## LANDABLE NOW

- Current work includes the wave merge (`941c27c9`), the mirror-assert merge (`834f7856`), the full-frame meter fixture and bounded capture option (`2921f907`), and the standalone default-on switch (`382e7e0f`). The default switch is in its own commit. No commits were pushed.
- The merged wave already dispatches meter work over the whole render extent and reduces all tile results. The mirror fixture now makes the missed-corner failure concrete: its top-left 16×16 tile is black while the other three tiles in a 32×32 input are lit. The expected full-frame exposure differs from a top-left-only result, and repeating the mirror calculation is deterministic. No additional production meter shader change was needed in this lane.
- Native MSVC Release build passed for `VixenApp`, `vixen_lookdev_capture`, and `test_exposure_meter_mirror` on the merged tree. The post-default-switch mirror CTest passed 1/1 (`test_exposure_meter_mirror`) with Release assertions live. Logs: `1791758452-build-hdrmeter:default-on-build.log`, `1791755425-test-hdrmeter:corner-fixture-final.log`, and `1791763103-test-hdrmeter:mirror-release-post-default.log`.
- All 26 VIXEN generation checks passed. `recipe_simd_check` was initially stale on the Windows-mounted checkout; documented regeneration made no semantic output change and the recheck passed. Logs: `1791742971-build-hdrmeter:native-codegen-26.log`, `1791743105-build-hdrmeter:recipe-simd-regen.log`, and `1791743215-build-hdrmeter:native-codegen-26-recheck.log`.

### Native pass timing

Nearest-rank p50/p95, native AMD Radeon 8060S. The short runs captured 24 frames per camera angle (4 angles, 96 CSV rows); four startup rows have zero pass timing, leaving 92 nonzero samples. The full 500 baseline used 252 nonzero samples. Timings are per pass.

| Resolution / sample | Meter p50 / p95 ms | Reduction p50 / p95 ms | Tonemap p50 / p95 ms |
|---|---:|---:|---:|
| 500×500 initial baseline, 252 samples | 0.12544 / 0.16380 | 0.00448 / 0.00568 | 0.01292 / 0.01788 |
| 500×500 short retime, 92 samples | 0.05080 / 0.07368 | 0.00212 / 0.00268 | 0.00580 / 0.00744 |
| 1920×1080 short retime, 92 samples | 0.35516 / 0.43744 | 0.00752 / 0.01356 | 0.05028 / 0.19384 |

The whole-frame implementation is present in the merged wave for both the initial baseline and short retime; this is a baseline-to-retime comparison, not a controlled pre-fix/post-fix shader A/B. The black-corner mirror fixture is the coverage proof. R505's 0.5 ms exposure-meter cap and R494's 0.5 ms tonemap cap are per-pass limits; meter p95 is below its cap at both sizes, and reduction and tonemap are also individually below 0.5 ms. These are pass timings, not an end-to-end frame percentile. CSVs: [initial 500×500](perf-before-500.csv), [short 500×500](perf-after-500-short.csv), [short 1920×1080](perf-after-1080-short.csv).

The first full-length 1080p capture had been terminated by the queue before it produced a CSV. The bounded `VIXEN_LOOKDEV_CAPTURE_FRAMES=24` route completed the timing run. That option is opt-in; visual captures below retain 64 frames per angle.

### Default-on visual review

`VIXEN_HDR_EXPOSURE` now defaults on when unset; explicit `0` still disables it. HDR-off capture rows have no meter/reduction/tonemap columns, while default-on captures have 252 nonzero pass samples out of 256 rows for every state. Each capture used 64 frames at each of four angles at 500×500. The sheets place HDR-off/HDR-on pairs side-by-side per angle (angles 0–1 on the top row, 2–3 on the bottom row):

- [Midday off/on](visual-midday-off-on-500.png)
- [Overcast off/on](visual-overcast-off-on-500.png)
- [Late-afternoon off/on](visual-late-afternoon-off-on-500.png)
- [Night-service off/on](visual-night-service-off-on-500.png)
- [Per-angle SHA-256 comparison](visual-hashes.csv)

All 16 off/on angle pairs changed. Those are the four declared HDR-on states, so each state change is expected; the hash manifest records every pair.

Against C16 and its R2 lighting references, the fixture remains a simplified station scene, so these judgments cover its exposure, palette, and lighting cues rather than matching the reference composition:

- **Midday — partly right.** HDR-on restores readable building, trees, and figures and gives a clear, bright mood. The fallback sky is violet-blue rather than the reference's natural blue, and the simple geometry lacks the reference's atmospheric landscape and industrial detail.
- **Overcast — not right yet.** HDR-on lifts the building and water, but the flat violet-blue background and hard contrast do not read as the reference's soft, foggy diffuse light.
- **Late afternoon — partly right.** Warm ground and highlights are clearer, but the scene lacks the reference's golden sky and atmospheric warmth; the fallback sky stays violet-blue.
- **Night-service — not right yet.** HDR-on reveals more of the station and warm service lights, but the scene does not reproduce the reference's deep blue industrial depth and broad, high-contrast floodlight pools. The shadows and background still need look-dev review.

Native capture logs: `1791761101-gpu-hdrmeter:visual-off-500.log` and `1791762968-gpu-hdrmeter:visual-on-500.log`. The default-on build and four comparison sheets are committed locally for owner review.

- The earlier 500 capture once failed before rendering with `Failed to create ray trace buffer (binding 4)` (base `834f7856`, log `1791748378-gpu-hdrmeter:baseline-perf-500-exclusive.log`). Stage diagnostics were added; the unchanged capture retry passed in 97 seconds and reported no failing Vulkan stage (`1791749032-gpu-hdrmeter:raytrace-diagnostic-500.log`). The original transient cause remains unclassified.

## STOPs

- **Owner review is required before the default-on commit goes on the wave.** Midday is only partly aligned with C16; night-service still lacks the reference's broad, high-contrast light pools. The local branch has the separate default-on commit and the review sheets; no wave merge or push was made.
- Native generation, build, mirror test, 500×500 timing, 1920×1080 timing, and all four off/on captures completed. No required native witness remains blocked.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Expose HDR passes in the native performance CSV
- proposed: Keep silent native builds alive under the box queue
- proposed: Make the native build entry point use the installed Visual Studio toolchain
- proposed: Resolve the Yeroket kernel root in native WSL-backed checkouts
- proposed: Provision the assigned native checkout with the lane base
- proposed: Keep native build helper parameters compatible across wave bases
- proposed: Quote Windows paths in queued native build invocations
- proposed: Ignore DrvFS executable-bit noise in Windows native worktrees
- proposed: Preserve canonical line endings in generated native outputs
- proposed: Enable MSVC extended aligned storage in the native preset
- proposed: Preserve MSVC exception flags when adding native CXX options
- proposed: Keep the exposure mirror assertions live in Release builds
- proposed: Run native CMake presets from the VIXEN source directory
- proposed: Make direct CMake inherit the provisioned Vulkan SDK pin
- proposed: Scope native Git trust to the WSL kernel provider root
- proposed: Stage TBB for native lookdev capture CTest
- proposed: Keep native CTest captures alive under the box idle watchdog
- proposed: Expose Vulkan buffer allocation failure stages in native captures
- proposed: Provide a supported look-dev PNG contact sheet composer
- proposed: Expose bounded frame counts for lookdev timing captures
