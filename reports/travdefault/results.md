# Composed traversal default (R505)

## LANDABLE NOW

- `BuildRenderGraph.cpp` now requests composed traversal by default. `VIXEN_COMPOSED_TRAVERSAL=0/1` remains an explicit developer A/B override.
- The existing `RayQueryLighting` CapabilityGraph path selects ray query on capable devices and the internal `VIXEN_COMPOSED_DDA_FALLBACK` shader tier otherwise.
- `VIXEN_BRICKMAP_TRAVERSAL` and its `kFeatureBrickmapTraversal` product switch are retired. The stored-SDF DDA implementation remains as the composed software fallback.
- The brickcost capture matrix now compares default-on composed traversal with `VIXEN_COMPOSED_TRAVERSAL=0` (ESVO forced); the old standalone DDA configuration is removed.

## R491 FEATURE INVENTORY DELTA

The shared R491 inventory is in the Undertow report checkout, outside this lane's assigned worktree. Its exact replacement classifications are recorded in `reproduce/README.md` for the landing controller:

| Feature | Updated classification |
| --- | --- |
| `VIXEN_COMPOSED_TRAVERSAL` | Planned/default-on composed traversal; CapabilityGraph selects ray query when available and DDA fallback otherwise; `0/1` remain developer A/B overrides. |
| `VIXEN_BRICKMAP_TRAVERSAL` | Retired standalone product switch; DDA remains an internal composed fallback tier. |

## VERIFICATION

- Baseline SHA: `5dd78d8675160314f5831e4b3d10b3fdefd8c164`; it matches `origin/wave/authoring-convergence` before lane edits.
- Before edits, the queued WSL Release build completed 1,151 Ninja steps and ran the project generation checks. Baseline RenderGraph/SVO CTest passed 29 tests and skipped `SVOBuilderTest.GeometricError` (30 counted, 0 failed).
- The first post-edit queued build was stopped by the idle watchdog during `BuildRenderGraph.cpp` compilation (exit 75, no compiler diagnostic). A retry with a 60-second heartbeat compiled and linked the changed target. A subsequent queue-wrapped build status check exited 0 with `ninja: no work to do`.
- All 22 primary CMake generation-check targets completed successfully (already up to date); `sdi_tool merge-variants shaders/sdi-variants.json --check` reported all merged SDI headers up to date.
- Post-change RenderGraph/SVO CTest passed 29 tests and skipped `SVOBuilderTest.GeometricError` (30 counted, 0 failed).
- Dozen DDA fallback capture and native 8060S A/B matrix: pending.
- The native assigned path was absent initially. `reproduce/native-build.sh` provisions it from the committed lane branch; the native build and captures require the implementation commit first.

## STOPs

None established. Native byte-comparison and fallback witnesses remain pending.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- Queue runner must keep silent compiler phases alive without lane heartbeats
- Provision the per-lane native VIXEN checkout before dispatch
- Reuse one provisioned Vulkan SDK across VIXEN worktrees
