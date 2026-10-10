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

## STOPs

- The required full RenderGraph/SVO CTest command was stopped at the three-hour lane checkpoint. CTest had completed 2,107 of 2,124 tests: 2,105 passed and two failed. Sixteen tests had not started, and one active test was terminated with the queue job. The queue reports exit 141 from that intentional termination; there is no final CTest summary.
  - `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` failed with `recipe gradient capability mismatch: 94`.
  - `ShadowCorrectnessTest.OccludedPixelMatchesCpuReferenceShadowRay` failed its shadow-disabled brightness check (171 luma versus the expected value above 191).
  - Neither red has been matched against the pre-change base or a clean-wave result. Treat both as **unclassified**; the shadow result may overlap the rendering scope and needs owner follow-up.
  - `BandwidthAbMeasurementTest.MipOnlyFarBodiesUploadDrasticallyFewerBytesThanBaseline` was active for more than three minutes when the suite stopped.
- Only before captures were taken and committed: seven native, four exposure, and four history PNGs under `reports/upscalebugs-visual/`. After captures, byte comparisons, and visual review of the deltas were not reached. No claim is made about capture identity or visual improvement.
- The post-merge native capture witness and exposure/history after-capture runs remain outstanding.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Derive compute-stage order from declared resource hazards
- proposed: Serialize GoogleTest discovery during parallel builds
- proposed: Make FetchContent cache selection automatic for worktrees
