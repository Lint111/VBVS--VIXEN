# wavereds

## LANDABLE NOW

VIXEN started at `dc2ea8828b887b71c183282d09f460463e63bc00`. The assigned kernel worktree is already at `c3fb3847979e3f9eef04783510c42b70d7058ae7`, equal to `origin/main`; this lane made no kernel edits.

### Causes and fixes

- **`appflow_check` and `view_noun_enum_check`:** the catalog contains derived queries without a `view` member after the view contract was retired. VIXEN’s tracked kernel snapshot, `77574f29728c40da71365d9e3c8125acf6a506ed`, still indexes `q.Members["view"]` unconditionally. Kernel commit `1efc6b3e` changed `DerivedQueryCatalogReader.Read` to skip queries without a view face. VIXEN’s schema declarations and generated AppFlow/ViewNoun outputs are current; direct `dotnet run --check` calls using the kernel `c3fb384` source passed without changing either output.
- **Additional generated drift under `c3fb384`:** `recipe_simd_check` and `sdf_core_kernels_check` exposed outputs made stale by kernel R441/R442 since the old pin. The documented `recipe_simd_regen` and `sdf_core_kernels_regen` targets regenerated `RecipeSimd.g.hpp` and `SdfCoreKernels.g.glsl`; the diff adds the generated recipe-analysis API and precise GLSL return expressions. Their C++/HLSL outputs did not change.
- **Two SVO assertion aborts:** `readParamU32Op` and `readParamQ16Op` in `VIXEN/libraries/SVO/include/Recipe/RecipeParityCorpus.h` set `paramMask=1`. The registry and GLSL emitter allow a nonzero mask only for `ReadParam` and `ReadParamFloat3`; typed reads still store the index in `data[0]`, with the mask left zero. The assertion in `SdfRecipeCodegenGlsl.h` remains unchanged. The failing tape came from the handwritten corpus helper, not the rva1/rvcompact merges or generated kernel tape.
- **Opcode 94:** `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` still fails with `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`, the documented open T-1449 issue.

### Witness

- All 22 `*_check` targets passed with the per-build kernel override set to `c3fb3847979e3f9eef04783510c42b70d7058ae7`. The direct `dotnet run --check` invocations for AppFlow and ViewNounId also passed.
- The fresh Release build passed: `cmake --build build/wsl --parallel 1` through the box queue.
- Assertion-enabled Debug SVO tests passed for `RecipeGlslNumericalParityTest.GlslMatchesCpuEvalAcrossCorpus` and `RecipeGlslCompiles.EmittedGlslCompilesForEveryCorpusProgram`.
- The queued `ctest --test-dir build/wsl --output-on-failure -j8 -L "RenderGraph|SVO"` selected 2,109 tests. Its only failure was T-1449; CTest also marked configured cases skipped or disabled. All 11 capture assertions passed: six `EditorToggleUndoCapture`, three `HudRenderCapture`, `HeadlessUiGraph`, and `HeadlessCornellGraph`.
- That broad label run also included `vixen_editor_capture_producer`, owned by gpudevloss; it passed. It changed tracked `VIXEN/BuiltAssets/documents/sample_tri_layer.edited.vxd` from 588 to 916 bytes. The lane restored that test-written artifact. The producer is not counted as a standard capture assertion.
- The required no-op rebuild passed with `ninja: no work to do`.
- Initial reds were captured before source edits at base `dc2ea882`: the queued AppFlow target failed with `KeyNotFoundException: 'view'` (`1791548917-build-wavereds:baseline-codegen-failures.log`); the separate ViewNoun check failed identically (`1791549897-light-wavereds:baseline-view-noun-enum-failure-light.log`). Assertion-enabled Debug baseline tests reproduced both ParamMask aborts and T-1449 (`1791549708-test-wavereds:baseline-debug-svo-tests-root.log`). After the fix, the targeted Debug run passed the two assertion cases and retained only T-1449 (`1791550063-test-wavereds:debug-svo-fix-tests.log`). The complete RenderGraph/SVO run is logged at `1791552255-test-wavereds:rendergraph-svo-full.log`.

### STOPs and landing action

- No view-concept choice was needed; the kernel reader was stale. Per `_kernel-vixen-lane.md`, this lane did not change VIXEN’s tracked pin. The controller must advance the tracked kernel SHA from `77574f2` to `c3fb384` when landing; without that action, a default configure still selects the old reader and reproduces the catalog exception. The green codegen witness used the supported per-build override.
- T-1449 remains open and is the sole expected SVO failure.
- The editor capture producer belongs to gpudevloss. It passed when included by the broad label selection; its tracked-file side effect was restored and proposed for isolation.

### KFR follow-up

None. This fix has no KFR consumer call site and made no KFR edits.

### SPT disposition

T-1449 remains open as the documented opcode-94 gradient-capability issue. This lane added two consolidation proposals to `.spt-proposals/wavereds.jsonl`.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Derive recipe corpus instruction helpers from opcode metadata
- proposed: Isolate editor capture writes from tracked sample documents
