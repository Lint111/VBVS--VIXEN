# rjs2 — render-job S2 (tracked kernel pin `dc6c87b7`)

## LANDABLE NOW

- Branch: `lane-rjs2`. The authoring-convergence wave was merged as `213d54f2`; S2 and the tracked-pin update are committed in `8f312716` (`rjs2: pin VIXEN GPU codegen to dc6c87b7`). The report update is a follow-up documentation commit.
- `VIXEN/codegen/CMakeLists.txt` now pins `YEROKET_KERNEL_SHA` to `dc6c87b7262c6791c745f37e583ca12f2f8706fe`. The build-tree override was cleared. Tone-map outputs were regenerated from the tracked pin; `ExposureTonemapSystem.comp` and `ExposureTonemapSystem.g.h` were unchanged. The regenerated FrameTargets relation references are part of the implementation commit.
- WSL and native full Release builds passed, including every default `*_check` target. The WSL build also ran the shader golden check. `glslangValidator` is absent on WSL; the native VIXEN Vulkan build is the shader compile witness.
- The native parity sheet is [rjs2-parity.csv](rjs2-parity.csv): all 16 state/angle reference-vs-system pairs were byte-identical at 500×500 (0 changed pixels; max channel delta 0). Captures compared the hand-written `ExposureTonemapReference.comp` with generated `ExposureTonemapSystem.comp`. The sheet was captured with kernel `928c40c9`; the dc6 regeneration left both compared tone-map outputs unchanged, but parity was not recaptured in this continuation. Angle-0 images for all four states were opened during that run.
- Look-dev A/B remains complete for overcast, late-afternoon, and night-service: 12 pairs byte-identical, with angle-0 images opened. Midday remains pending.

### Build and generation evidence

- WSL configure: `/home/liory/.local/state/undertow/undertow-box-logs/1791935449-build-rjs2-pin-dc6-wsl-configure.log`.
- WSL tone-map regeneration from the tracked pin: `1791935468-build-rjs2-pin-dc6-wsl-tonemap-regen.log` (shader/header unchanged; dependent FrameTargets references regenerated).
- WSL all-target build: `1791935549-build-rjs2-pin-dc6-wsl-all-checks.log`, exit 0.
- Native configure with the dc6 pin: `1791936106-build-rjs2-pin-dc6-native-configure-vcvars-retry1.log`.
- Native all-target build: first attempt `1791936217-build-rjs2-pin-dc6-native-all-checks.log` failed during GoogleTest discovery because `tbb12.dll` was not on the discovery PATH. Retry `1791936620-build-rjs2-pin-dc6-native-all-checks-tbbpath.log` added the native TBB runtime directory and passed, exit 0.

## STOPs

- The required full split was started with `tools/run-tests-split.sh --build-dir build/wsl --all-correctness-labels --agent rjs2` and the runner defaults (`native` and `nightly` excluded). It selected 2,876 CPU and 172 GPU tests. The CPU leg completed with one failure, `AppFlowBlob.InteriorEditPreservesShapeHash`, matching the known CPU baseline failure. Four tests were skipped. The GPU leg was stopped at 47/172 at the three-hour lane checkpoint; the completed prefix had no failing name. Queue log: `/home/liory/.local/state/undertow/undertow-box-logs/1791936756-gpu-rjs2:split-gpu.log` (`WRAPPER_SIGNALED`, payload exit 141). No complete GPU failure-name comparison is available.
- Per-label counts below use CPU/GPU order. The merged-wave inventory is the published capperf1 after-split baseline (`reports/capperf1.md`); the current `Application` GPU increase is the 32 S2 parity tests. Counts overlap across labels and are not additive.

| Correctness label | Merged-wave baseline CPU/GPU | rjs2 current CPU/GPU |
| --- | ---: | ---: |
| AppFlow | 57/0 | 57/0 |
| Application | 10/25 | 10/57 |
| CashSystem | 117/0 | 117/0 |
| GaiaArchetypes | 128/0 | 128/0 |
| GaiaVoxelWorld | 100/0 | 100/0 |
| RenderGraph | 1300/98 | 1300/98 |
| ResourceManagement | 238/1 | 238/1 |
| SVO | 783/15 | 783/15 |
| ShaderManagement | 62/0 | 62/0 |
| VIXENCapture | 0/1 | 0/1 |
| VoxelComponents | 9/0 | 9/0 |
| VoxelData | 24/0 | 24/0 |
| VoxelDocument | 8/0 | 8/0 |
| VulkanResources | 12/1 | 12/1 |
| WSL | 0/1 | 0/1 |
| logger | 28/0 | 28/0 |
| **Selected total** | **2876/140** | **2876/172** |

- The baseline CPU failure `AppFlowBlob.InteriorEditPreservesShapeHash` is unchanged. The GPU prefix ended before the S2/application capture cases; the same-base GPU run documented at the prior checkpoint was itself incomplete, so this run cannot establish the requested full GPU failure-name delta.
- Midday look-dev A/B and the editor capture producer were not run in this continuation. Previous editor producer attempts were canceled before payload launch; no producer result is claimed.
- Native timing was not requested before the checkpoint. No five-run median or spread is claimed and the `<0.5 ms` gate remains unverified. The controller needs to schedule a dedicated quiet native `--gpu-benchmark` window.
- The three-hour checkpoint was reached at approximately 2026-10-14 00:15 UTC. The active GPU split was stopped at 47/172 rather than extending the lane beyond its checkpoint.

## SPT DISPOSITION

The brief supplied no SPT task IDs. The existing rjs2 proposals are recorded in `.spt-proposals/rjs2.jsonl`; this continuation added no proposal.

## VERIFICATION DETAILS

- The split output directory is `build/wsl/rjs2-split-dc6-current/`; CPU output is complete in `cpu.log`/`cpu.xml`. GPU output ends at test 1200 (47/172); the queue wrapper log records the checkpoint signal.
- Native configure recovery: the first queued invocation failed with `powershell.exe: command not found` (exit 127; log `1791935974`). Two path-conversion attempts produced malformed `C:vixen-native...` paths (logs `1791935994`, `1791936019`). A WSL-to-Windows path conversion reached CMake but lacked the Visual Studio environment and failed the compiler probe with `C1083: stddef.h` (log `1791936029`). The queued configure using the converted path and `vcvars64.bat` passed (`1791936106`).
- Native build recovery: GoogleTest discovery initially failed for nine executables with `0xc0000135` because `tbb12.dll` was absent from PATH (`1791936217`). Adding `C:\vixen-native\rjs2\build\native\msvc_19.44_cxx_64_md_release` to the PATH under the Visual Studio environment let the full native build pass (`1791936620`).
- The stale build-tree kernel override required `-DVIXEN_YEROKET_KERNEL_SHA_OVERRIDE=` during configure; this is covered by the proposal “Clear stale VIXEN kernel override when returning to tracked pin.” Native snapshot fetching and TBB discovery workarounds are covered by existing rjs2 proposals.
- Historical native parity jobs: `1791917647`, `1791917941`, `1791918050`, and `1791922095` (`928c40c9`). Historical look-dev A/B jobs: `1791918083`, `1791919351`, and `1791919465` (`928c40c9`).

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
- Derive state-frame projection providers from GPU declarations
- Keep build waiters queued through same-lane fairness turns
- Make shared queue waitlists interoperable across wrapper versions
- Clear stale VIXEN kernel override when returning to tracked pin
