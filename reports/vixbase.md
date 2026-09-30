On unchanged base `0e8b5bbab11a6b972faa7a93a9442773262a802b`, the staged RenderGraph/SVO run reproduced all 17 dispatched failures. On the merged wave tree, nine are fixed and pass, six are explicitly skipped because this host has only an integrated GPU, and two remain STOPs pending owner decisions. The 17 failures classify as: **1 real engine bug, 8 stale tests, 6 GPU-environment cases, 0 dead-feature tests, and 2 STOPs**.

The post-merge full runs selected all 1,315 RenderGraph and 742 SVO tests. RenderGraph has one failure: the editor mask capture remains pixel-identical. SVO has one failure: recipe opcode 94 fails the pinned generator’s gradient-capability validation. Both match the original base failures. The separate full-build `appflow_check` red is also documented below; scoped RenderGraph/SVO targets and suites do not depend on that check.

## Baseline and scope

- **Base:** `0e8b5bbab11a6b972faa7a93a9442773262a802b` (`lane-vixbase` before source edits).
- **Baseline evidence:** `build/wsl/vixbase-rg-svo-staged-baseline.log` records 17 failures in the unchanged-base RenderGraph/SVO sweep. The failure diagnostics are summarized in the table below.
- **Merged witness tree:** source fix commit `ae1317e7`; merged `origin/wave/authoring-convergence` through `4ed5144db467db3532db8f9a4013aa776a9e541f` before the final build and test witnesses.
- **Required test scope:** all CTest cases registered from `VIXEN/libraries/RenderGraph/tests` and `VIXEN/libraries/SVO/tests`, plus their generated-code and shader compile checks. After the merged configure, `ctest -N -V` mapped these suites to IDs 493–1807 (1,315 tests) and 1941–2682 (742 tests).

## Classification of the 17 failures

| Test | Class | Cause and original failure output | Fix |
|---|---|---|---|
| `AppFlowEditorToggleRenderTest.ToggleThenUndoRestoresRender` | **b — stale test** | `boreDiffPixels=0 (region=80x80)`. KI-018 (`784adff7`) moved ray-march output off the old `outputImage` path and into `HitRecordBuffer`; the test read back a retired output. | Read and compare the live `HitRecordBuffer`, including the compute-to-host barrier, while retaining the bore-delta and exact-undo assertions. A separate graph wiring defect found during the investigation is also fixed below. |
| `EditorToggleUndoCapture.FirstEditReachesRenderPipeline` | **STOP — design choice** | `[EDITOR/mask] wholeImageDiffPixels(png5,png45)=0 (500x500)`, expected `>20`. The windowed state trail is correct, but the default side view does not expose the vertical cut in the pixels. `Editor-Brick-Residency-Fix-Plan-2026-07.md` says to stop rather than weaken this gate. | Keep the threshold. Owner must choose whether the gate supplies an explicitly top-down camera or the editor’s default camera should make the layer cut visible. |
| `SwitchCostIsolationTest.N3_RandomizedStress` | **c — GPU environment** | `discreteGpuSelected_` was false; the selected adapter was `Microsoft Direct3D12 (AMD Radeon(TM) 8060S Graphics)`. | Explicit `GTEST_SKIP` with the selected adapter and discrete-GPU requirement. |
| `SwitchCostIsolationTest.N10_RandomizedStress` | **c — GPU environment** | Same diagnostic: `discreteGpuSelected_` false on the integrated AMD adapter. | Explicit `GTEST_SKIP` with the selected adapter and discrete-GPU requirement. |
| `SwitchCostIsolationTest.N100_RandomizedStress` | **c — GPU environment** | Same diagnostic: `discreteGpuSelected_` false on the integrated AMD adapter. | Explicit `GTEST_SKIP` with the selected adapter and discrete-GPU requirement. |
| `SwitchCostIsolationTest.N100_TinyMi_LowKi_Decoupling` | **c — GPU environment** | Same diagnostic: `discreteGpuSelected_` false on the integrated AMD adapter. | Explicit `GTEST_SKIP` with the selected adapter and discrete-GPU requirement. |
| `SwitchCostIsolationTest.N10_LargeMi_HighKi_Decoupling` | **c — GPU environment** | Same diagnostic: `discreteGpuSelected_` false on the integrated AMD adapter. | Explicit `GTEST_SKIP` with the selected adapter and discrete-GPU requirement. |
| `SwitchCostIsolationTest.N100_LargeMi_LowKi_Decoupling` | **c — GPU environment** | Same diagnostic: `discreteGpuSelected_` false on the integrated AMD adapter. | Explicit `GTEST_SKIP` with the selected adapter and discrete-GPU requirement. |
| `ViewHudGolden.GeneratedSequenceMatchesCanonicalSchema` | **b — stale test** | `got` contained `recentEventAge` and the inspector grievance, strength, and top-relation bindings absent from `kExpected`. Schema commit `141a65ad` added these HUD fields. | Update the sequence fixture to the generated canonical HUD schema; keep exact sequence equality. |
| `ViewEditorLayersReconcile.ExternalGaiaWriteReconcilesIntoBoundView` | **a — real engine bug** | `changed` was false after an external write. `GaiaVoxelWorld::setComponent` called `add` for an already-present component, so Gaia’s change version did not advance for `.changed<LayerMask>()`. | Use `set<T>()` for existing components and `add<T>()` only for absent components. The external-write reconcile test now passes. |
| `ViewWireRoundtrip.ReadsBackEveryField` | **b — stale test** | `ViewWireReader: top-field count mismatch (wire=9 blob=13)`. The fixture omitted top-level fields added by schema commit `141a65ad`. | Update the wire fixture to carry all 13 current fields; retain the strict count check. |
| `ViewWireSoaRoundtrip.ReadsBackEveryFieldIncludingEmptyStringRow` | **b — stale test** | `ViewWireReaderSoa: top-field count mismatch (wire=9 blob=13)`. The fixture had the same pre-`141a65ad` schema shape. | Update the SoA fixture with the current top-level fields; retain the strict count check and empty-row assertion. |
| `TypedAccessorEmitter.GeneratedAccessorsMatchRawStoreDecodedValues` | **b — stale test** | `ViewWireReaderSoa: top-field count mismatch (wire=9 blob=13)`. The generated accessor fixture lagged the schema introduced by `141a65ad`. | Update the fixture and comparisons to the generated schema’s current fields; do not skip the reader validation. |
| `ViewContainerBuilder.RealSimFrameRoundTripsThroughUtvcAndSurvivingSections` | **b — stale test** | `schema version mismatch (wire=0x0A962057 store=0x7C70EFEE)`. Commit `c780e705` changed the generated view schema version. | Update the fixture’s UTVC version to `0x7C70EFEE`; keep the mismatch rejection contract. |
| `NodeSelfRegistration.RegistersAllBuiltInNodes` | **b — stale test** | Registry count was 71 while the test expected 72. The `source-count + 1` heuristic from `814d4114` became false when `bd159652` added `SceneRadianceNode.cpp`, an alias translation unit with no registration; `ConstantNode.cpp` registers two types. | Derive the expectation from `VIXEN_REGISTER_NODE` declarations in the source list. |
| `NodeSelfRegistration.ReplaysIntoEachRegistryInstance` | **b — stale test** | Same 71-versus-72 assumption as the preceding case. | Use the same registration-declaration count for both registry assertions. |
| `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` | **STOP — external generator contract** | `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`. Opcode 94 (`Output`) is emitted as `LoweredAway` with gradient availability, but the pinned runtime validator rejects its missing resolver. `recipe_simd_check` itself passes. | Do not hand-edit the generated header. The kernel/generator owner must choose the `LoweredAway` gradient contract, update the validator or metadata, regenerate, and rerun parity. |

## Additional engine fix found during the investigation

`BuildRenderGraph.cpp` previously provided `sceneRadianceImage` from the fp16 history image even when HDR exposure was disabled. In that mode the writer and `BlitNode` use the legacy rgba8 render target, so the default graph presented a stale image. The provider now selects the history image only when the HDR/tonemap path is enabled and the render target otherwise. Repeated captures changed from the same stale hash to the same updated hash. This independent graph bug is not counted among the original 17 failure classes.

## Build recovery and verification output

- The first full build on the base failed in RmlUi 6.0’s `robin_hood.h` under GCC 15 (`uint32_t`/`uint64_t` undeclared). The required wave merge supplied RmlUi 6.3 (`44d728e1`), which compiled and linked `rmlui_debugger`.
- A build attempt initially regenerated CMake against the shared FetchContent cache and had begun compiling RmlUi there when I caught the path and canceled it (exit 130). I did not try to clean that shared cache. I then configured with `VIXEN_FETCHCONTENT_CACHE=<worktree>/build/vixbase-fetchcontent-cache` and `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1`; `build/wsl/CMakeCache.txt` confirmed the worktree-local FetchContent root. The CodegenTool bootstrap also needed invariant globalization after reporting that ICU was unavailable.
- The merged full build compiled the VIXEN graph and test binaries, then exited 1 at `appflow_check`: `RuntimeState 'Undertow.Content.Core.Systems.Economy.DepletionMaterialRow' declares gaia:true without a row scope or ref-field pairing`. The same base SHA and contract failure are documented in `reports/rmlui.md`. This check consumes Undertow’s external schema and is outside the RenderGraph/SVO test-target dependency graph. The 24 scoped RenderGraph/SVO build targets then returned exit 0 (`ninja: no work to do`). The recipe generated-header check passed; the runtime parity test remains the SVO STOP above.
- The editor and HUD capture runners both exited 0. The HUD file assertions passed after setting `VIXEN_HUD_CAPTURE_DIR` to the absolute worktree `VIXEN/temp` path. The editor state trail and exact undo/redo checks passed; only the visible-mask delta failed.
- **P579 focused gate:** 89 passed, 6 discrete-GPU cases skipped, 2 pre-disabled cases; exit 0. AppFlow, HUD/view schema, Gaia reconcile, and related regression tests passed.
- **Final RenderGraph run:** `ctest --test-dir build/wsl -I 493,1807 --output-on-failure -j1`, with absolute editor/HUD capture directories; 1,315 selected, 1,303 passed, 11 skipped, 1 failed; exit 8, 406.84 s. Failure: test 1154, `EditorToggleUndoCapture.FirstEditReachesRenderPipeline`, `wholeImageDiffPixels(png5,png45)=0`, expected `>20`. Log: `build/wsl/vixbase-rendergraph-merged-final.log`.
- **Final SVO run:** `ctest --test-dir build/wsl -I 1941,2682 --output-on-failure -j1`; 742 registered, 732 passed, 8 skipped, 1 disabled, 1 failed; exit 8, 46.34 s. Failure: test 2492, `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`, `recipe gradient capability mismatch: 94`. Log: `build/wsl/vixbase-svo-merged-final.log`.
- **Unchanged-base failure log:** `build/wsl/vixbase-rg-svo-staged-baseline.log` records the original 17 failures and their outputs. The full-build AppFlow contract failure has independent unchanged-base evidence in `reports/rmlui.md`.

## STOPs and owner decisions

1. **Editor visual gate:** choose whether the live test sets a top-down camera or the editor default camera should expose the cut. Keep the pixel threshold and layer-state checks intact until the decision is made.
2. **Recipe opcode 94:** define how gradient capability validation treats `LoweredAway` opcodes, then update the generator/validator and regenerate its VIXEN artifact.
3. **Full-build AppFlow check, outside the 17-test scope:** decide whether `DepletionMaterialRow` receives a scoped row/ref-field identity or is excluded from the RuntimeState/AppFlow view catalog. `reports/rmlui.md` records the same failure on the unchanged base; the scoped RenderGraph/SVO build and suites pass independently.

## CONSOLIDATION ISSUES

- proposed: Preserve worktree-local FetchContent cache across reconfigure
- proposed: Make VIXEN CodegenTool bootstrap independent of host ICU
- proposed: Keep WSL GPU loader paths complete in the documented runner
- proposed: Expose CodeGraph queries in disposable VIXEN worktrees
- proposed: Provide an aggregate RenderGraph and SVO test build target
- proposed: Select RenderGraph and SVO tests with stable CTest labels
- proposed: Use the live runner capture directory in windowed CTest fixtures
- proposed: Derive image layout in RenderTargetReadback
- proposed: Keep the runtime manifest out of the tracked global cache
- proposed: Pin RmlUi to a GCC 15 compatible upstream revision
- proposed: Document the WSL binary directory and scoped target names
