# Native Windows capture runner and C1 shadow follow-up

Lane: `lane-nativerunner`

Starting lane tip: `4bdab24460807a0b91d69c42cb3e5ac02f92c838`

C1 shadow fix: `9492f3c988f443c7a0a857c5413b70ea78ce0dc3`

Queue deployment: box-queue `master` at `4a3c638` (controller-reported on ws and laptop)

## LANDABLE NOW

- VIXEN shader fix `9492f3c9` rejects procedural shadow hits at the ray origin. The native C1 capture passes and all 52 former dot sites now match Dozen exactly.
- The Dozen C1 PNG remains byte-identical to its saved pre-fix image (SHA256 `192b4d05dafe8bd91ff2c32bb47314e8d10d05c9f7c0096c084899f70eba60af`).
- The native C1 first and repeat captures are byte-identical across all five artifacts. Existing property assertions and thresholds are unchanged.

## Native capture jobs

Each native state used a separate `gpu-native` shared correctness job; neither job used `--exclusive`.

| State | Capture build | Queue job | CTest result | Frames per process | First/repeat bytes |
|---|---|---|---:|---:|---|
| `lookdev/midday` | `2e8c008bd4fa54ed5074bb82bdcc9c04f468b146` | `nativerunner:native-lookdev-midday` | Passed | 32 | Equal (5 files) |
| `planet/C1` | `9492f3c988f443c7a0a857c5413b70ea78ce0dc3` | `nativerunner:native-planet-C1` | Passed | 8 | Equal (5 files) |

The look-dev first-pass artifacts are under `build/native-captures/2e8c008bd4fa54ed5074bb82bdcc9c04f468b146/runtime-captures/`. The fixed C1 first-pass and repeat artifacts are under `build/native-captures/9492f3c988f443c7a0a857c5413b70ea78ce0dc3/runtime-captures/planet/`. CTest reports first/repeat byte equality; `cmp` independently confirmed `C1.png`, `C1.hits.bin`, `C1.lighting.bin`, `C1.meter.bin`, and `C1.scene.json`.

## C1 dot diagnosis

The original native C1 at build `2e8c008b` had 52 black pixels on the sunlit disk. The 20 largest were native `(0,0,0)` versus Dozen `(245,245,245)` (max delta 245):

`(424,310), (655,310), (356,356), (723,356), (514,399), (565,399), (310,424), (769,424), (399,514), (680,514), (399,565), (680,565), (310,655), (769,655), (514,680), (565,680), (356,723), (723,723), (424,769), (655,769)`.

The other 32 were native `(0,0,0)` versus Dozen `(227,227,227)` (max delta 227):

`(424,224), (655,224), (424,226), (655,226), (424,228), (655,228), (310,294), (769,294), (294,310), (785,310), (224,424), (226,424), (228,424), (851,424), (853,424), (855,424), (224,655), (226,655), (228,655), (851,655), (853,655), (855,655), (294,769), (785,769), (310,785), (769,785), (424,851), (655,851), (424,853), (655,853), (424,855), (655,855)`.

These are radially symmetric about the projected center `(539.5,539.5)`, with screen radii grouped near 143 px, 257–260 px, and 332–336 px. The outer band includes 2-pixel neighboring samples; nearest-neighbor spacings across the full set are 2, 22.63, 51, 77.2, and 82 px, rather than a uniform 115-pixel pitch. `ShadowVisibilityWave.comp` linearizes pixel slots into 64-lane groups; the 52 coordinates occupy 42 distinct lane residues `(y×1080+x) mod 64`, not one workgroup lane. The C1 planet uses `PROVIDER_PROCEDURAL`; its rays do not cross ESVO bricks or nodes, so the pattern is not aligned to stored-world traversal boundaries. The groups follow the projected sphere/ray geometry.

The hit readback rules out a primary camera-ray miss: all 52 pixels have the hit flag. Before the fix, the native analytic-light visibility nibble was `0xE` (light 0 occluded) while Dozen's was `0xF` (visible). C1's camera is about 1.586 billion metres from the planet and its configured shadow bias is 19,145.82 metres. The native hit positions at the dot sites were about 19.2–21.1 km inside the analytic sphere; Dozen's were about 2–188 m inside. Thus the native bias still left the shadow origin inside the body, while Dozen's bias moved it outside.

The source path is `ShadowVisibilityWave.comp`'s biased origin and `tmin=0`, then `TraceWorldShadowImpl`'s procedural branch, then `traceUberRecipeBody` in `SdfRecipes.glsl`. The marcher initializes at the bounding-sphere `tNear` and accepts `d <= EPS_MIN` immediately, including a negative signed distance when float rounding has left the origin inside. That reports the starting sample at `pT=0`; `TraceWorldShadowImpl` previously accepted it because the caller supplied `tmin=0`. The per-pixel, radially symmetric locations and visibility-bit readback match this self-shadow path, not a race or an uninitialized primary hit.

The 13- and 17-delta look-dev samples are a different class: angle 0 has one lit-pixel color shift at `(338,338)` (native `(75,101,21)`, Dozen `(62,101,27)`); angle 3 has one at `(328,388)` (native `(241,207,139)`, Dozen `(241,224,139)`). Neither is a black self-shadow pixel or a visibility-bit change.

## Fix and pixel comparison with Dozen

`TraceWorldShadowImpl` now requires a procedural occluder to be at least 1 mm from the ray origin in world space, even when the caller's `tmin` is zero. The surface-biased origin remains unchanged; the guard rejects only its zero-distance SDF sample. The primary intersection path is untouched.

The fixed native image has no dots at the 52 recorded coordinates: every coordinate is now byte-for-byte equal to the Dozen RGB value (`245` at the first 20 sites and `227` at the other 32). The fixed native image still has ordinary thin limb/rasterization differences from Dozen, so the two full images do **not** agree pixel-exactly.

| State/image | Size | Differing pixels | Difference bounding box | Max channel delta |
|---|---:|---:|---:|---:|
| `lookdev/midday`, angle 0 | 500×500 | 143 | (57, 212)–(472, 480) | 13 |
| `lookdev/midday`, angle 1 | 500×500 | 631 | (0, 134)–(499, 449) | 1 |
| `lookdev/midday`, angle 2 | 500×500 | 33 | (85, 189)–(419, 395) | 1 |
| `lookdev/midday`, angle 3 | 500×500 | 55 | (94, 192)–(405, 490) | 17 |
| `planet/C1`, fixed native `9492f3c9` vs Dozen | 1080×1080 | 3,138 | (128, 128)–(951, 951) | 192 |

Look-dev totals 862 differing pixel instances across its four files; its union bounding box is (0, 134)–(499, 490), with max delta 17. Its 13/17-delta isolated color pixels are listed above and are not the C1 shadow-dot class.

Dozen C1 was saved before the fix at `build/nativerunner-before-fix/planet/C1.png`. Before and after the final fix, both PNGs have SHA256 `192b4d05dafe8bd91ff2c32bb47314e8d10d05c9f7c0096c084899f70eba60af`. The patched C1 Dozen test also passed its own first/repeat byte-equality check.

One fixed pair was opened: `build/wsl/runtime-captures/planet/C1.png` and `build/native-captures/9492f3c988f443c7a0a857c5413b70ea78ce0dc3/runtime-captures/planet/C1.png`.

## Timing

Process wall is the capture application's `wall_ms`; it excludes queue admission. Frame percentiles are host wall-time samples around successful `VulkanGraphApplication::Render()` calls. Dozen timings are included as correctness-run observations and are not representative performance measurements.

| State/run | Frames | Native process wall, first / repeat | Native frame p50, first / repeat | Native frame p95, first / repeat | Dozen process wall | Dozen frame p50 | Dozen frame p95 |
|---|---:|---:|---:|---:|---:|---:|---:|
| `lookdev/midday` (existing witness) | 32 | 36.245 s / 3.700 s | 17.15 / 17.13 ms | 28.71 / 27.11 ms | 51.98 s per process | 1,517.95 ms (one reported log) | 1,799.98 ms (one reported log) |
| `planet/C1` (fixed `9492f3c9`) | 8 | 42.087 s / 4.289 s | 85.51 / 39.48 ms | 145.51 / 130.82 ms | 47.825 s / 29.536 s, first / repeat | 3,126.97 / 3,163.47 ms, first / repeat | 3,272.87 / 4,437.38 ms, first / repeat |

The earlier controller-supplied C1 Dozen timings were 38.806 s first and 32.990 s repeat; the refreshed run above has the same PNG hash as that original Dozen image.

## Golden source

The native C1 from build `2e8c008b` is **not** a valid golden source while it contains the 52 false-shadow dots. Use the clean first-pass native C1 from build `9492f3c9`; its existing property assertions pass and no golden assertions or expected property values changed. Keep the four native look-dev angle PNGs as that state's property goldens. Cross-driver pixel differences remain a separate witness, not a replacement for those properties.

## Recovery and verification

- CodeGraph was queried before source inspection, but this checkout has no `.codegraph` index; the trace was followed through the shader call sites and hit readbacks.
- The native Windows targets were built by `reports/capturespeed/reproduce/native-build.sh` through the live queue for commit `9492f3c9`; both `vixen_lookdev_capture` and `vixen_planet_capture` completed successfully.
- The C1 Dozen target build `cmake --build build/wsl --target vixen_planet_capture --parallel 2` passed through the live build queue. Its C1 GPU capture then passed through the shared `gpu` queue and compiled the final shader at runtime.
- The native C1 was a separate shared `gpu-native` job and passed `Application.PlanetCapture.C1`; its first and repeat outputs matched across all five files.
- An earlier all-target WSL build was cancelled at 229/626 steps (exit 130) after the C1-specific target was identified; it produced no target result and is not used as verification evidence.
- A broader experiment replacing the primary sphere quadratic did alter the Dozen PNG, so it was discarded. The final shadow-only change was rerun and the Dozen hash returned to the exact saved baseline above.
- The original runner recovery remains recorded: initial native attempts on `bf2fc4d0` exited `0xc0000135` because `tbb12.dll` was absent from the capture process `PATH`; commit `122b549a` added the runtime-directory lookup. The native build and captures then passed on `2e8c008b`, and the new fixed C1 passed on `9492f3c9`.

## STOPs

- None for the native C1 dot defect. Native and Dozen still differ at limb/rasterization pixels (3,138 pixels, max delta 192); that residual is disclosed above.

## SPT DISPOSITION

- None; no SPT task ids were assigned to this lane.

## CONSOLIDATION ISSUES

- proposed: Resolve Windows interop executable paths for queued native jobs
- proposed: Pass Windows batch paths to cmd.exe without doubled shell quoting
- proposed: Expose periodic progress for queued Windows builds
- proposed: Native capture runner omits the staged TBB DLL directory
