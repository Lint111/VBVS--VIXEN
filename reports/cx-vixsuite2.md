# cx-vixsuite2 — VIXEN correctness-suite landing report

## STATUS

Landable from the VIXEN lane. The published tips are merged: Undertow P845 `554112ecc`, kernel `24fec7d79`, and VIXEN authoring-convergence `3974cb94` (lane merge tip before this work: `e6b28e14`).

This lane adds declarative CTest policy for benchmark/on-demand selection, translated-device GPU smoke selection, a target launcher that returns exit 77 for non-smoke translated GPU cases, plain-GPU disabling on dzn, and a cached SDI drift node. It also registers the measured slow families and removes the unused glyph helper from the synthetic SwitchCost shader harness so its dzn smoke can compile.

Evidence is under `/home/liory/codeman-cases/undertow/reports/cx-vixsuite2-evidence/`.

## CLASSIFIED

The source table contained 105 tests at or above 10 seconds. Every entry is listed below. “GPU render/pixel” includes GPU readback/device-render witnesses; GPU lifecycle and traversal-only tests remain “other”.

| Class | Count | Tests and reason |
|---|---:|---|
| Measurement/benchmark | 8 | `BandwidthAbMeasurementTest.MixedSceneThroughLiveResidencyTriggerMeasuresRealisticBandwidthReduction`, `BandwidthAbMeasurementTest.MipOnlyFarBodiesUploadDrasticallyFewerBytesThanBaseline`, `EmitterPower.LaneBakeTimeAndMemoryReport`, `GenerationCostBenchmark.StandardBakeStageBreakdown`, `RecipeBucketingPerfTest.N100_BucketedVsColdStandIn`, `ShellDerive.SerialVsWaveWallClockMeasurement`, `Trees/EmitterPowerSizes.SigmaLeafPowerEqualsRootPower/64`, `EmitterPower.InteriorNodePowerIsChildSum` — report bytes, timings, or scaling rather than correctness. Moved to `benchmark`; default-disabled and opt-in. |
| GPU render/pixel | 42 | `BakedVsVirtualParityTest.VirtualRendersGeometricallyEquivalentToBaked`, `ShadowCorrectnessTest.CelPointLightFalloffAndPurposeRemainLocalThroughLastSlot`, `B1OcclusionAbTest.OccludedInstancesDropIterationsAndPixelsStayIdentical`, `ShadowCorrectnessTest.RegisteredRecipesRemainStableAcrossLightVariants`, `HeadlessStarfieldGraph.CaptureFirst`, `BakedVsVirtualParityTest.RimLeakEditorScene`, `ShadowCorrectnessTest.OccludedPixelDoesNotBorrowFarLight`, `ShadowCorrectnessTest.EmissivePointLightFaces`, `HeadlessStarfieldGraph.CaptureSecond`, `BodyInstanceRayMarchRenderTest.RenderStoredSdfBodiesNoHoles`, `MipFallbackRenderTest.MultiOctreeMipFallback`, `BodyInstanceRayMarchRenderTest.HitRecordCompositing`, `BodyInstanceRayMarchRenderTest.RenderRecipeBakedBody`, `BakedVsVirtualParityTest.RimLeakSlabSphere`, `BodyInstanceRayMarchRenderTest.RematerializeEditLoop`, `RecipeAuthoringGateTest.CsgSubtractRendersNonTrivial`, `MipFallbackRenderTest.SecondOctreeMipFallback`, `BodyInstanceRayMarchRenderTest.RenderStoredSdfMultiChannel`, `AppFlowEditorToggleRenderTest.ToggleThenUndoRestoresRender`, `RecipePoolRenderTest.FourRecipesAllRender`, `TierCrossingGpuTest.ThirtyAuTierCrossing`, `MipFallbackRenderTest.ResidentTree`, `BodyInstanceRayMarchRenderTest.EmptySkipMask`, `ShadowCorrectnessTest.EmissivePointLightPick`, `BodyInstanceRayMarchRenderTest.SkipMask`, `EditorDocumentRenderTest.DisablingCutLayerChangesPixels`, `TierCrossingGpuTest.NonResidentChildTierCrossing`, `RecipeAuthoringGateTest.DefaultSceneRenders`, `BodyInstanceRayMarchRenderTest.PickSelection`, `BodyInstanceRayMarchRenderTest.RenderNearViewToPng`, `MipFallbackRenderTest.MipOnlyRoundTrip`, `BodyInstanceRayMarchRenderTest.MultiKindSdf`, `BodyInstanceRayMarchRenderTest.DepthDistance`, `HitRecordReadbackTest.ReadbackPreservesHitRecord`, `EditorDocumentRenderTest.GoldenAllDocumentLayers`, `EditorDocumentRenderTest.LayeredSlab`, `CanonicalDilationGpuTest.Parameterized/3`, `CanonicalDilationGpuTest.Parameterized/2`, `CanonicalDilationGpuTest.Parameterized/1`, `CanonicalDilationGpuTest.Parameterized/0`, `TierCrossingGpuTest.SubPixelTierCrossing`, `PartialBrickUploadTest.BricksAllocatedFullSizeButPopulatedOnlyAfterResidencyRequest` — pixel/readback/device correctness. Native runs retain the full set; dzn executes one smoke per family and skips the rest with exit 77. |
| CPU mirror | 16 | `StoredSdfMarchMirror.RootCause_DisplacedOctree1MultiOctree`, `StoredSdfMarchMirror.OccupancyFixRemovesHoles`, `StoredSdfMarchMirror.M5bFull8BodyWithMipsRepro`, `StoredSdfMarchMirror.M5bBackWallConcatViewRepro`, `StoredSdfMarchMirror.RootCause_SentinelContaminationOrigin`, `StoredSdfMarchMirror.DiagFindExactMisses`, `StoredSdfMarchMirror.RootCause_EsvoBrickVsFloorBrick`, `StoredSdfMarchMirror.ReproducesBrickAlignedHoles`, `StoredSdfMarchMirror.M5bBackWallHopContinuationRepro`, `StoredSdfMarchMirror.M5bBackWallFarHitRepro`, `StoredSdfMarchMirror.DiagFrontViewFillRatio`, `StoredSdfMarchMirror.DiagBrickSelection`, `StoredSdfMarchMirror.DiagAsciiSilhouette`, `StoredSdfMarchMirror.GpuHolePixelsOnCpu`, `StoredSdfMarchMirror.DiagProbeRays`, `StoredSdfMarchMirror.DiagStepOverSequence` — CPU mirrors of GPU traversal; heavy reproductions/root-cause cases are on-demand, diagnostic mirrors remain correctness. |
| Drift/codegen | 1 | `sdi_merged_drift_check` — committed merged SDI outputs are keyed by the manifest/source/output closure; the second unchanged invocation is cached. |
| Other | 38 | `BodyOctreeLifetimeTest.ShellCachePopulatedThroughNodeCompile`, `vixen_editor_capture_producer`, `vixen_hud_capture_producer`, `Native.WslRouteLauncher`, `SdfBakeTest.DeepInteriorDistanceField`, `BodyInstanceOcclusionRejectTest.NonOccludedInstanceTraversesNormally`, `BodyInstanceOcclusionRejectTest.OccludedInstanceHasZeroTraversalIterations`, `RecipeBakerTest.BudgetCheckEnforced`, `RecipeBakerTest.BakesTwoRecipes`, `RecipeBakerTest.BakesMipPool`, `ResidencyDefaultTest.AllMipFlagIsLazy`, `MipFallbackRenderTest.RegistryHasMips`, `ShellDerive.NormalByteIdentical`, `RecipeBakeTest.SphereAnalytic`, `ShellRevalidateTest.EditPublishes`, `ShellDerive.BakedNormalMip`, `BodyOctreeLifetimeTest.DeviceDestroyReportsNoLeakedObjects`, `UberShaderSpliceTest.SharedTraversalMatchesExpected`, `BodyOctreeLifetimeTest.DiversityStressParamSweepAtScaleNeverMarksNodeNeedsRecompile`, `ShellDerive.ProxyAabbs`, `ShellDerive.DirtyRevalidate`, `BodyOctreeLifetimeTest.ReadParamValueSweepNeverMarksNodeNeedsRecompile`, `BodyOctreeLifetimeTest.CoarseLod`, `RecipeBakerTest.FailsLoudOnBadRecipe`, `BodyOctreeLifetimeTest.RealNodeRingLifecycleHasNoValidationErrors`, `RecipeBakerTest.UnboundedRecipe`, `ShellDerive.ShellSuperset`, `ShellDerive.RepeatedEditStability`, `ShellDerive.BakedNormalsAngular`, `ShellDerive.ApplyBrickEdit`, `ShellDerive.DirtyRevalidateRebakes`, `ShellDerive.ShellThickness`, `SoaSdfSerializeTest.InteriorBricks`, `ShellDerive.ShellVsFull`, `ShellDerive.ShellGridLookup`, `RecipeGlslNumericalParityTest.GlslMatchesCpuEvalAcrossCorpus`, `ResidencyDefaultTest.MixedPool`, `ShellDerive.BakedNormalsContinuous` — lifecycle, fixture, serialization, route, or CPU setup costs; no broad semantic rewrite was justified in this lane. |

## MEASURED

- Baseline from the brief: 3,161 tests, 5,477 seconds total (~91.3 minutes) on WSL/dzn, `--parallel 4`; the 105 slow tests accounted for the listed 35.6-minute GPU, 15.4-minute measurement, 14.4-minute other, 14.2-minute mirror, and 3.4-minute SDI costs.
- Final CPU correctness selection: 1,242 tests, 100% passed, 67.82 seconds real time, including `sdi_merged_drift_check`.
- Final GPU correctness selection: 1,042 tests, 100% passed, 156.75 seconds real time on dzn. The two class-specific jobs total about 225 seconds (~3.7 minutes); the combined selection was also about 180 seconds but exposed the unrelated FramePipeline scheduling red below.
- Selection proof: 12 benchmark tests, 19 on-demand tests, and 22 executable `gpu_smoke` tests. Disabled benchmark/on-demand cases are not tagged as smoke.

## LANDABLE NOW

- CTest policy and launcher: benchmark/on-demand labels, translated smoke-only behavior, exit-77 skip semantics, and native full-GPU preservation.
- Slow-family registrations: 12 benchmarks, 19 on-demand cases, and the one-family dzn smoke selection.
- SDI drift cache: CPU-classified generated wrapper preserves the `sdi_tool` dependency while avoiding the generic Vulkan link-closure false positive.
- GPU harness correction: SwitchCost uses the existing `WithoutGlyphOutlineKernel` transform; its dzn smoke passes.
- Fresh queued build passed for `rendergraph_svo_tests`, `test_upscaler_selection`, `test_aa_selection`, and `test_closure_manifest`.
- Final queued CPU and GPU correctness selections passed; native GPU selection was not run on ws in this lane and remains for the landing witness.

## REDS

- The resumed lane initially hit a wrong preset-relative build path (`VIXEN/build/wsl`); the documented source-root configure path recovered it. Evidence: queue log `1794192716-build-cx-vixsuite2:build-targets.log`; SPT proposal filed.
- A cold focused build was required because no warm target artifact was available in this worktree. It recovered and passed, but is recorded as a lane red/finding and has an SPT proposal.
- Before the final policy, ten windowed editor/HUD tests ran without their external capture producers and failed on missing artifacts. They are now on-demand and disabled by default on dzn; the final affected-family witness passed.
- Before the final shader-harness correction, `SwitchCostIsolationTest.N3_RandomizedStress` failed at dzn shader compilation because `glyphOutlineInstances` was undeclared. The canonical glyph-free transform fixed it; the affected-family and final GPU witnesses pass.
- The combined `-L "^(gpu|cpu)$"` witness failed twice only at `FramePipelineTest.AdjacentFramesOverlapButCommitInSubmissionOrder` (`maxActive=1`, expected 2) when the GPU queue granted two CPUs. The exact test passed in isolation with three CPUs, and the split CPU final selection passed. This is an unclassified scheduling flake outside the changed files, not a final class-specific correctness failure; landing should keep the test red visible until its owner hardens its scheduling assumption.

## STOPs

None. Native GPU selection on ws is deferred to the landing witness as required by the brief.

## SPT DISPOSITION

No SPT task IDs were worked in this lane.

## CONSOLIDATION ISSUES

- proposed: Preset-relative VIXEN build path is ambiguous from the VIXEN source directory
- proposed: Focused VIXEN verification target omits declared application CPU tests
- proposed: VIXEN focused verification starts without a warm target artifact
