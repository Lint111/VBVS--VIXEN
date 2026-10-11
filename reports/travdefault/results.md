# Composed traversal default (R505)

## LANDABLE NOW

- `BuildRenderGraph.cpp` requests composed traversal by default. `VIXEN_COMPOSED_TRAVERSAL=0/1` remains an explicit developer A/B override.
- The existing `RayQueryLighting` CapabilityGraph path selects ray query on capable devices and the internal `VIXEN_COMPOSED_DDA_FALLBACK` shader tier otherwise.
- `VIXEN_BRICKMAP_TRAVERSAL` and its `kFeatureBrickmapTraversal` product switch are retired. The stored-SDF DDA implementation remains as the composed software fallback.
- The brickcost capture matrix now compares default-on composed traversal with `VIXEN_COMPOSED_TRAVERSAL=0` (ESVO forced); the old standalone DDA configuration is removed.
- The native 8060S 500×500 look-dev matrix completed all eight captures: default and ESVO forced across midday, late-afternoon, overcast, and night-service.

## R491 FEATURE INVENTORY DELTA

The shared R491 inventory is in the Undertow report checkout, outside this lane's assigned worktree. Its exact replacement classifications are recorded in `reproduce/README.md` for the landing controller:

| Feature | Updated classification |
| --- | --- |
| `VIXEN_COMPOSED_TRAVERSAL` | Planned/default-on composed traversal; CapabilityGraph selects ray query when available and DDA fallback otherwise; `0/1` remain developer A/B overrides. |
| `VIXEN_BRICKMAP_TRAVERSAL` | Retired standalone product switch; DDA remains an internal composed fallback tier. |

## VERIFICATION

- Base SHA: `5dd78d8675160314f5831e4b3d10b3fdefd8c164`; it matched `origin/wave/authoring-convergence` before lane edits. The branch was merged from that remote before final witnesses; it was already up to date.
- Before edits, the queued WSL Release build completed 1,151 Ninja steps and ran the project generation checks. Baseline RenderGraph/SVO CTest passed 29 tests and skipped `SVOBuilderTest.GeometricError` (30 counted, 0 failed).
- The first post-edit queued build was stopped by the idle watchdog during `BuildRenderGraph.cpp` compilation (exit 75, no compiler diagnostic). A retry with a 60-second heartbeat compiled and linked the changed target. A subsequent queue-wrapped build status check exited 0 with `ninja: no work to do`.
- All 22 primary CMake generation-check targets completed successfully (already up to date); `sdi_tool merge-variants shaders/sdi-variants.json --check` reported all merged SDI headers up to date.
- Post-change RenderGraph/SVO CTest passed 29 tests and skipped `SVOBuilderTest.GeometricError` (30 counted, 0 failed).
- Native MSVC Release build of `vixen_lookdev_capture` succeeded after the helper fast-forwarded the assigned native checkout to commit `f0172b4d`. Native build log: `/home/liory/.local/state/undertow/undertow-box-logs/1791709891-build-travdefault:native-build-fast-forward-retry.log`.
- Native 8060S look-dev 500×500 default/ESVO matrix: 8/8 capture processes exited 0. Output is under `/mnt/c/vixen-native/travdefault/reports/travdefault/native/r505-lookdev-500/`.
- Dozen identifies the AMD 8060S through the software Vulkan Dozen driver and logs `RayQueryLighting extensions not available` and `[ComposedBackend] DDA`. A full 64-frame attempt was interrupted after 108 seconds (exit 130) because it rendered at roughly 14 seconds per frame. A direct launch without the CTest runtime library path failed with `VK_ERROR_INCOMPATIBLE_DRIVER`; applying the paths from `build/wsl/CMakeCache.txt` reached DDA, then the one-frame run exposed the fixture's full-length exposure-ring assertion. The short-run-only assertion adjustment is awaiting its queued rebuild and correctness retry.
- The initial native helper invocation fetched the branch but launched its `.bat` before fast-forwarding the native checkout; Windows failed to find the script (exit 1, before CMake configure). The WSL helper now merges `FETCH_HEAD` before invoking Windows, and the native target build passed.
- Still queued at report update: native look-dev 1080p and stored-SDF 500×500/1080p matrices; stored-SDF PNG SHA-256 comparison against brickcost composed captures; short-mode rebuild and Dozen correctness rerun; final native incremental build of the latest fixture change.

## STOPs

- No product compile or required RenderGraph/SVO test failure is established.
- The canonical shared R491 inventory row remains unchanged because its checkout is outside the worktrees assigned to this lane. The replacement row is included above and in `reproduce/README.md` for the controller to apply.
- The queued native and Dozen witnesses listed above have not completed yet.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- Queue runner must keep silent compiler phases alive without lane heartbeats
- Provision the per-lane native VIXEN checkout before dispatch
- Reuse one provisioned Vulkan SDK across VIXEN worktrees
- Add a short correctness mode to the look-dev capture fixture
- Fast-forward the native lane before invoking fetched helpers
- Apply the configured Vulkan runtime path to direct WSL GPU witnesses
