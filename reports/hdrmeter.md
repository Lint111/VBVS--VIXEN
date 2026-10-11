# HDR exposure meter lane report (R505)

Starting lane tip: `d63a0e0c`. The native VIXEN checkout is `/mnt/c/vixen-native/hdrmeter`.

## LANDABLE NOW

- Merged `origin/wave/authoring-convergence` at `a48a42df` (merge `941c27c9`) and the `lane-mirrorassert` fix at `69a18bda` (merge `834f7856`). Baseline evidence is committed at `0982b645`; this report is the final lane commit.
- Native MSVC Release build passed for `VixenApp`, `vixen_lookdev_capture`, and `test_exposure_meter_mirror` on the merged tree. A later diagnostic rebuild of `VixenApp` and `vixen_lookdev_capture` also passed.
- All 26 VIXEN generation checks passed. `recipe_simd_check` was initially stale on the Windows-mounted checkout; the documented regeneration reported no semantic output change, and the recheck passed. Logs: `1791742971-build-hdrmeter:native-codegen-26.log`, `1791743105-build-hdrmeter:recipe-simd-regen.log`, and `1791743215-build-hdrmeter:native-codegen-26-recheck.log`.
- `test_exposure_meter_mirror` passed 1/1 in Release with `/UNDEBUG` live (the compiler reported `/DNDEBUG` overridden by `/UNDEBUG`). Log: `1791743958-test-hdrmeter:mirror-test-release.log`.
- Native 500×500 HDR-on baseline completed on the unchanged exposure shader. There are 252 nonzero timing samples; four startup rows are zero and excluded. Percentiles use nearest rank.

| Pass | Before p50 ms | Before p95 ms |
|---|---:|---:|
| Exposure meter | 0.12544 | 0.16380 |
| Exposure reduction | 0.00448 | 0.00568 |
| Tonemap | 0.01292 | 0.01788 |

  The raw CSV and opened 2×2 midday sheet are committed at `reports/hdrmeter/perf-before-500.csv` and `reports/hdrmeter/midday-before-500.png`. The sheet uses explicit `VIXEN_HDR_EXPOSURE=1`; it is baseline context, not proof of default-on behavior. It shows a saturated amber ground plane, deep blue-violet sky, bright cream/red structure, and crisp toy-scale forms. Against C16's atmospheric industrial references, composition and material detail differ, so this fixture supports an exposure/color-cue review rather than a direct scene match.
- The first post-merge native 500 command was `bash /home/liory/.local/bin/with-test-lock.sh --agent hdrmeter --resource gpu --credits 2 --label hdrmeter:baseline-perf-500-exclusive -- bash /mnt/c/vixen-native/hdrmeter/.tmp/hdrmeter-heartbeat.sh bash /mnt/c/vixen-native/hdrmeter/.tmp/hdrmeter-native-invoke.sh perf midday 500 500 'C:\vixen-native\hdrmeter\build\hdrmeter-native\hdrmeter-profiling\before-500\perf.csv' 'C:\vixen-native\hdrmeter\build\hdrmeter-native\hdrmeter-profiling\before-500' 1`. It failed before its render loop at `VoxelGridNode::CompileImpl` with only `Failed to create ray trace buffer (binding 4)` (exit 1; base `834f7856`; log `1791748378-gpu-hdrmeter:baseline-perf-500-exclusive.log`). Since `RayTraceBuffer::Create` hid the failing Vulkan stage, stage/result diagnostics were added and built. The unchanged capture then passed and wrote all four angles (97 seconds; log `1791749032-gpu-hdrmeter:raytrace-diagnostic-500.log`). No diagnostic fired, so the original failure did not recur and its cause remains unclassified. The diagnostic improvement and the SPT proposal are committed.
- No HDR shader optimization or default-on change is landable from this checkpoint; the committed work is the native baseline diagnostic and 500p evidence.

## STOPs

- The native 1920×1080 baseline did not complete. The command `bash /home/liory/.local/bin/with-test-lock.sh --agent hdrmeter --resource gpu --credits 2 --label hdrmeter:baseline-perf-1080 -- bash /mnt/c/vixen-native/hdrmeter/.tmp/hdrmeter-heartbeat.sh bash /mnt/c/vixen-native/hdrmeter/.tmp/hdrmeter-native-invoke.sh perf midday 1920 1080 'C:\vixen-native\hdrmeter\build\hdrmeter-native\hdrmeter-profiling\before-1080\perf.csv' 'C:\vixen-native\hdrmeter\build\hdrmeter-native\hdrmeter-profiling\before-1080' 1` was submitted at 23:08:36 and admitted at 23:45:30 after the older 15-token exclusive RenderGraph/SVO run released the queue. At 23:48:23 the wrapper was signaled (queue exit 76, payload exit 143; log `1791751518-gpu-hdrmeter:baseline-perf-1080.log`). It produced one partial angle image and no CSV. I removed the partial image/readback, terminated the orphaned Windows capture process, and verified that no `vixen_lookdev_capture` process remained. No 1080p timing is reported.
- The first 500p failure was recovered by retrying the same native capture with stage-specific diagnostics. Invocation/provisioning recovery is complete, and generation plus the Release mirror witness are green. The 1080p performance scope is still required and cannot be inferred from 500p; it is not classified as a pre-existing red or excluded. No HDR shader optimization, default-on switch, after timings, or after look-dev sheets were produced. Midday/night owner review therefore remains pending.
- R494's 0.5 ms tone-map/exposure budget and R505 were read. The lane stopped at 23:48:23 local under the three-hour worker checkpoint. The HDR meter task is not complete.

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
