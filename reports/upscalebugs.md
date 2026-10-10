# Upscaling defect lane report

## LANDABLE NOW

The three requested fixes and their red-to-green focused witnesses are committed in `ee1ebb11` (`fix: repair VIXEN upscaling defects`). The lane also includes the required merge of `origin/wave/authoring-convergence` at `4ddea473`; the integration merge tip before this report is `e25960cbc9844a01ae793d017e6ef0fa2ea3b77a`.

1. **Exposure meter coverage and EV units**
   - Red: `test_exposure_meter_mirror` reproduced origin-tile-only metering; a frame with log luminance 0 in the first 16x16 tile and -4 in the next returned 0 EV instead of +2 EV.
   - Fix: meter every 16x16 tile, reduce tile sums/counts in `ExposureReduce.comp`, and keep EV in stops for `exp2` in `ExposureTonemap.comp`. The image-wide percentile sort uses a deterministic parallel bitonic network.
   - Green: `test_exposure_meter_mirror` passed; it checks +2 EV, `exp2` = 4, and partial edge tiles.

2. **Render scale extent mismatch**
   - Red: `RenderTargetNodeFollowExtent.BuilderTruncationReproducesOddSizeFractionalScaleMismatch` reproduced truncation against the render target's ceil extent.
   - Fix: `BuildRenderGraph.cpp` now uses `RenderTargetNode::ComputeFollowExtent` for lighting, shadow-wave, B1, and exposure dimensions.
   - Green: all eight `RenderTargetNodeFollowExtent.*` tests passed, including odd dimensions and fractional scales. The merged-tree CTest run also passed `OddDimensionsAtFractionalScalesUseCanonicalCeilExtent`.

3. **History read/write race**
   - Red: `test_spatial_reuse_history_mirror` reproduced the in-place interleaving: the second pixel became 130 rather than the previous-frame result 105.
   - Fix: radiance and world-position history each use a persistent previous image and a separate current output image. `SpatialReuseShade.comp` reads only the previous images and writes only the current pair; graph execution swaps their roles.
   - Green: `test_spatial_reuse_history_mirror` passed. The merged-tree CTest run also passed this test.

**Merged-tree checks completed:** the queued build of `rendergraph_svo_tests`, `VixenApp`, `vixen_lookdev_capture`, and both new mirrors passed with `--parallel 2`; all 22 codegen check targets passed; `sdi_tool merge-variants shaders/sdi-variants.json --check` reported all merged SDI headers up to date. The final focused pre-merge CTest run passed 10/10 tests.

## Takeover follow-up (Claude Sonnet 5.5, after Codex usage limit)

- **Suite classification:** `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` is the only remaining failure (known pre-existing wave red; recorded by the predecessor run, `recipe gradient capability mismatch: 94`). `ShadowCorrectnessTest.OccludedPixelMatchesCpuReferenceShadowRay` was caused by this lane: its harness lacked the new history/world-pos output bindings 48/49 and used an 8-bit history format. The harness fix (`test_shadow_correctness.cpp`) is committed, and the test passes on the tip and on base `4ddea473`. The remaining 17 tests: 13 passed, 4 skipped.
- **Native captures (7 frames, HUD + editor): byte-identical** before vs after.
- **Look-dev midday captures (4 angles):** all four differ from before. I viewed angle 0 and angle 2 before/after: the after frames are uniformly slightly darker, with the sky going from lighter to deeper violet. Geometry, edges and layout are unchanged. This is consistent with the exposure fix (the meter now covers the whole image, not the origin tile, and EV is applied in stops). The same capture run backs `exposure-after` and `history-after` (byte-identical to each other, as are the two before sets), so the history change cannot be separated visually from the exposure change. The history fix is witnessed by `test_spatial_reuse_history_mirror` only. I did not open angles 1 and 3.

## STOPs

- No final CTest summary exists for a single uninterrupted run; the suite was completed in pieces (earlier run, remainder run, shadow rerun).
- History visual delta not separable from exposure delta (see above); an isolated before/after would need a build with only one fix reverted. Not done.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Derive compute-stage order from declared resource hazards
- proposed: Serialize GoogleTest discovery during parallel builds
- proposed: Make FetchContent cache selection automatic for worktrees
