# rjs2 — S2 exposure tone map and native parity

## LANDABLE NOW

- Branch: `lane-rjs2`; the S2 implementation is in `557d4856` and the wave integration merge is `3ba81db1796589c2f42b64964e8f41195eaf1c5c`. The per-build Yeroket override was `928c40c929c09496156c7237614609f3bf4e2d87`; VIXEN's tracked `YEROKET_KERNEL_SHA` remains `7a2b1f1e83310b7129fdba19975235ef94198757`.
- The tone-map shader and SDI binding header were regenerated with the `928c40c9` kernel. Its entry point loads singleton SSBO values at element 0 and passes value parameters into the body; per-element input still uses `linearId`. WSL and native SDI merges pass. The native Vulkan `sdi_tool` merge compiled the emitted shader; WSL has no standalone `glslangValidator`.
- WSL and native Release app builds pass with all `_check` targets.
- The native parity sheet is [rjs2-parity.csv](rjs2-parity.csv). Midday, late-afternoon and overcast each have a separate GPU queue job. All 12 completed reference/system angle pairs are byte-identical at 500×500: zero changed pixels and zero maximum channel delta. The compared passes are the hand-written `ExposureTonemapReference.comp` and generated `ExposureTonemapSystem.comp`. Angle 0 images for all three states were opened and inspected.
- Look-dev A/B is complete for overcast, late-afternoon and night-service: all 12 reference/system angle pairs are byte-identical; angle 0 images for each state were opened. Midday A/B remains queued.
- The split runner selected all correctness tests across 16 suite labels, including `RenderGraph`, `SVO`, `Application`, and the runner's default labels. CPU selected 2,398 tests for both base and current; GPU selected 607 on base and 639 on current (the current tree includes 32 parity tests). Label counts are CPU/GPU; rows overlap when a test has multiple labels.

| Suite label | Base CPU/GPU | Current CPU/GPU |
| --- | ---: | ---: |
| AppFlow | 57/0 | 57/0 |
| Application | 0/24 | 0/56 |
| CashSystem | 0/117 | 0/117 |
| GaiaArchetypes | 128/0 | 128/0 |
| GaiaVoxelWorld | 100/0 | 100/0 |
| RenderGraph | 1198/200 | 1198/200 |
| ResourceManagement | 0/238 | 0/238 |
| SVO | 783/15 | 783/15 |
| ShaderManagement | 62/0 | 62/0 |
| VIXENCapture | 0/1 | 0/1 |
| VoxelComponents | 9/0 | 9/0 |
| VoxelData | 24/0 | 24/0 |
| VoxelDocument | 8/0 | 8/0 |
| VulkanResources | 0/13 | 0/13 |
| WSL | 0/1 | 0/1 |
| logger | 28/0 | 28/0 |

The base CPU run failed `AppFlowBlob.InteriorEditPreservesShapeHash` and `SdiLifecycleTest.RegisterShaderInSDIRegistry`; current CPU failed only `AppFlowBlob.InteriorEditPreservesShapeHash`. No new CPU failing name appeared. GPU halves are still queued, so their failure-name comparison is not available yet.

## STOPs

- Native timing was not run. Observed host CPU ranged from 35–60%, with two or three benchmark jobs and several correctness GPU jobs queued. This was not a quiet timing window; no median or spread is claimed, and the 0.5 ms gate is unproven.
- The same-base and current GPU split-suite halves remain queued. GPU failing names cannot yet be compared.
- Night-service parity and midday look-dev A/B remain queued. The first night-service A/B attempt passed the reference capture, then the wrapper was signaled during the system capture (`exit=76`, `payload_exit=143`, no diagnostic); a separate retry passed both captures. The first late-afternoon A/B attempt exited by signal before a completion record; its separate retry passed both captures.

## Verification

- Generation / toolchain: `1791908702-light-regen-tonemap-kernel-928c40c9.log`; CodegenTool restore/build/check logs `1791918587-build-codegentool-restore-928.log`, `1791918598-build-codegentool-build-928.log`, and `1791918628-light-codegentool-check-928.log`.
- SDI: `1791910257-light-sdi-merge-wsl-928-final.log` and `1791911328-light-sdi-merge-native-928.log`.
- Full app builds: `1791910344-build-build-wsl-all-checks-app-928-merged.log` and `1791913331-build-build-native-all-checks-app-928-post-regeneration.log`.
- No published wizard baseline matched base `2685b1e980a052f85b938d7ac31b47f5930e015f` and kernel `928c40c929c09496156c7237614609f3bf4e2d87`. The same-base source was freshly generated and built with that override; its all-target WSL build passed after regenerating three shared-struct schemas (`1791916337-build-build-same-base-wsl-928-all-struct-regens.log`).
- Native parity jobs: `parity-midday-928`, `parity-late-afternoon-928`, `parity-overcast-928`; logs `1791917647-gpu-parity-midday-928.log`, `1791917941-gpu-parity-late-afternoon-928.log`, and `1791918050-gpu-parity-overcast-928.log`. Captures are under `C:/vixen-native/rjs2/build/native/runtime-captures/exposure-parity/`.
- Completed A/B jobs: `lookdev-ab-overcast-928`, `lookdev-ab-night-service-928-retry1`, and `lookdev-ab-late-afternoon-928-retry1`; logs `1791918083-gpu-lookdev-ab-overcast-928.log`, `1791919351-gpu-lookdev-ab-night-service-928-retry1.log`, and `1791919465-gpu-lookdev-ab-late-afternoon-928-retry1.log`. Outputs are under `C:/vixen-native/rjs2/build/native/runtime-captures/lookdev-ab/`.
- Split suite outputs: `build/wsl-baseline/rjs2-split-928-base/` and `build/wsl/rjs2-split-928-current/`; CPU logs completed, GPU logs contain queue status only so far.
- Device: AMD Radeon(TM) 8060S Graphics, driver `32.0.22018.5`.

## CONSOLIDATION ISSUES

- Support scoped schema overlays for pinned source readers
- Launch dotnet run output from artifacts-path
- Expose source-resolvable GLSL math intrinsics
- Derive GPU dispatch extents from texture facets
- Default the SDI merger to the manifest shader root
- Lower singleton SSBO inputs into legal GLSL body parameters
- Make native kernel snapshots work with WSL-owned sources
- Make native multi-label CTest filters select label unions
- Make CodeGraph indexes available in VIXEN lane worktrees
- Couple shared shader-schema outputs to kernel override regeneration
- Prevent trailing whitespace in native FetchContent cache paths
- Stage native TBB runtime for GoogleTest discovery
- Preserve VS toolchain PATH in native build launchers
- Normalize generated code line endings for native drift checks
- Share the WSL Vulkan SDK cache with baseline source snapshots
- Share X11 development cache with baseline source snapshots
- Route native CMake build-tree commands through Windows paths
- Distinguish native correctness captures from GPU benchmarks
- Explain wrapper signals during chained native capture jobs
