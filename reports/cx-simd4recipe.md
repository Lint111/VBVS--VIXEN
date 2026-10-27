# cx-simd4recipe2 — widened recipe pixels and stored-SDF timing

## LANDABLE NOW

YES for the SIMD4 recipe implementation. The two reported symptoms are explained:

- The 96,660-pixel deltas were between current captures and a stale/mismatched retained reference set. Exact train-7 and current-tip captures are byte-identical.
- The stored-SDF probe does not execute recipe SIMD4 code, and the six-run alternating sample does not reproduce a 2× regression. No timing workaround or semantic code change is justified.

The witnessed trees are VIXEN `6ed2eb56` and kernel `be8e9eab`; `origin/wave/authoring-convergence` and `origin/main` were already merged and up to date before the final witness.

## PIXELS

I built the exact train-7 VIXEN source at `6f3c64e1` and the current tip with the same Release/CMake settings, then ran the four look-dev states on the GPU queue with `DISPLAY=:0`:

`midday`, `late-afternoon`, `overcast`, and `night-service`, each at four angles, with first/repeat captures for 32 PNG comparisons total.

`tools/compare-capture-pixels.py --require-byte-identical` reported `byte_equal=yes`, `pixel_diff=0`, `max_channel_delta=0`, and `bbox=none` for every pair. The contact sheet is [lookdev-train7-vs-current-contact.png](/home/liory/codeman-cases/undertow/reports/cx-simd4recipe2-evidence/lookdev-train7-vs-current-contact.png).

The retained reference comparison still differs by 96,660 pixels per four-angle state, so those references are not the train-7 tip output used by this control. This is a provenance mismatch, not a widening change.

## TIMING

Alternating `Application.LookdevCapture.StoredSdf.Brickmap` GPU runs, same machine/build configuration:

| run | tree | wall seconds | frame CPU ms |
|---|---|---:|---:|
| 1 | train-7 | 18.61 | 16,175.8 |
| 1 | current | 20.02 | 17,538.7 |
| 2 | train-7 | 18.64 | 16,455.0 |
| 2 | current | 17.06 | 15,420.2 |
| 3 | train-7 | 16.92 | 14,581.7 |
| 3 | current | 19.46 | 16,972.1 |

Train-7 mean/median wall time was 18.057/18.61 s; current was 18.847/19.46 s. The current median is only 4.6% higher, with one current run faster than both first two train-7 runs; this does not reproduce the prior 2× observation. All six CSVs have the same boot bytes, populated pools, resident signature, and zero steady-state GPU timing columns because this is a one-frame capture.

The stored-SDF path is not a recipe SIMD4 execution path: `test_lookdev_capture.cpp:1130` selects the stored scene, `:1181-1185` skips procedural `BodyInstances` for it, and `BuildRenderGraph.cpp:7576-7661` creates stored instances with `providerKind=0` and `recipeId=0`. Therefore there is no SIMD4 thunk, gather path, early-out, or execution-class change to fix in this probe.

## WITNESS

- VIXEN aggregate build: queued `rendergraph_svo_tests`, exit 0.
- Fresh SIMD4 parity: `RecipeSimdParity` 2/2 passed. The implementation’s full recipe parity corpus remains 119/119 from the prior witness.
- RenderGraph/SVO split: CPU 1,437 executed, 0 failed; GPU 826 selected, 12 failed. Every failure name is present in the retained train-7 manifest at `/home/liory/codeman-cases/undertow/reports/sn-vixtrain7-evidence/final-train/real-failed.txt`; no new name appeared. The known names were:
  - `EditorDocumentRenderTest.GoldenDocumentAllLayersRendersVisibleBody`
  - `EditorDocumentRenderTest.DisablingCutLayerChangesTopFaceSilhouette`
  - `EditorDocumentRenderTest.LayeredSlabSubtractSphereGuardsAreExactAndCaptureHasHole`
  - `AppFlowEditorToggleRenderTest.ToggleThenUndoRestoresRender`
  - `ShellRevalidateNodeTest.DisjointShellGenerationPassesGetZeroBarriers`
  - `CanonicalDilation/ShellRevalidateNodeTest.GpuDispatchMatchesCanonicalFlagsAndCompactedBytes/0` through `/3`
  - `DeclaredPositionRenderTest.IntervalPrunedTileTapesMatchFullAndUnrolledGpuPixels`
  - `DeclaredPositionRenderTest.CorpusInlineLiteralsMatchRuntimeOperandsAndTape`
  - `vixen_text_shader_capture_producer`
- Full kernel SG: 1,374 passed, 0 failed, 0 skipped.
- Full kernel CG: 3,089 passed, 2 skipped, 0 failed, total 3,091.
- Raw logs and timing CSVs are under [cx-simd4recipe2-evidence](/home/liory/codeman-cases/undertow/reports/cx-simd4recipe2-evidence/).

Recovery ladder: A used the documented live queue and isolated Release CMake builds; B built the exact train-7 source control and rechecked generated recipe output; C matched the remaining broad GPU failure names against the train-7 manifest and passed the targeted recipe parity scope. No required check remains unresolved.

## STOPs

None for this lane. The retained-reference mismatch and the 12 RenderGraph/SVO failures are documented findings, all explained by control/reference evidence and pre-existing train-7 failure names.

## SPT DISPOSITION

No SPT task ID was provided or worked on in this lane.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: VIXEN queued builds require a manually paired `UNDERTOW_ROOT`
- proposed: `build-wsl.sh` requires `--config` for queued Release builds
