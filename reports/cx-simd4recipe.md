# cx-simd4recipe — R758 recipe SIMD4 witness

## LANDABLE NOW

NO for the complete brief: the recipe implementation and generated consumer are landable, but the look-dev unchanged gate is not closed. The retained train-7 midday references do not match the current capture set; see STOPs.

Kernel commits:

- `138835a95a6855584f0930112867551e2452bc73` — scalar-lane recipe forms use the SIMD4 emitter path.
- `703b992abbc6d07d4946fa10c9ca766ea9d7fbca` — deterministic hash callables receive SIMD4 ABI forms.
- `58ae2c1f88294d106393b1d31f6919c0a8e7d89e` — C++ SIMD4 runtime adapts `glm::vec3` uniforms and composite min/max/select.
- `d07c89424c18c307e6493bc8c152a2ae81a4f09d` — generated runtime whitespace cleanup.
- `be8e9eab1cb7b2451a118a10adfc77307b174744` — typed rejected callables keep SIMD4 scalar-ABI forms without attempting unsupported dual emission.

VIXEN pins `be8e9eab1cb7b2451a118a10adfc77307b174744`.

## CENSUS DELTA

The checked-in pre-change census (`reports/simd4-census/simd4-summary.csv`) was:

| kind | total | SIMD4 | scalar-only | not lowered |
|---|---:|---:|---:|---:|
| recipe op | 120 | 46 | 61 | 13 |

The 61 scalar-lane recipe entries now all have a generated SIMD4 callable form. Admitted per-point float/float3 bodies use `WidenedProgram` plus the C++/GLSL widened renderers; unsupported typed or non-vectorizable bodies use an exact four-lane scalar thunk behind the same ABI. Rejected derivative metadata no longer suppresses a native primal form when the callable is dual-renderable.

The five widened-program entries (`FloatToQ16`, `Q16Add`, `Q16Mul`, `Q16Sub`, `Q16ToU32`) have generated forms. The thirteen previously missing/control entries (`ComposeFloat3`, `DecomposeFloat3`, `DistanceTo`, `Output`, `Passthrough`, `PositionChannel`, `PushFloat3`, `PushParam`, `ReadParam`, `ReadParamFloat3`, `ReadParamQ16`, `ReadParamU32`, `RestorePos`) are present in the generated CPU/GLSL dispatch/body artifacts and are covered by the parity corpus.

Final generated-header scan: 125 metadata rows including aliases; `RecipeExecutionClass::ScalarLaneFallback` rows: 0; classes are 89 `Elementwise`, 25 `Masked`, 6 `ResolvedAtLowering`, and 5 `LoweredAway`. The generated header contains 181 SIMD4 callable definitions.

## PARITY

- SourceGenerator focused witness: 39/39 passed (`WidenedCalculusTests` + `RecipeSimdEmitterTests`).
- Full paired kernel SG: 1,353/1,353 passed.
- Full paired kernel CG: 2,878 passed, 1 skipped, 0 failed.
- VIXEN recipe parity selection: 119/119 passed; three optional tests were skipped by capability/fixture guards. The corpus programs were bit-identical across four lanes, including the typed recipe-value corpus. Hash, typed integer/conversion, float3, control, parameter, and per-op analytical parity cases passed.

## WITNESS

Final VIXEN build used the real queue, `UNDERTOW_ROOT`, `YEROKET_ROOT`, `VIXEN_WITH_FSR3=OFF`, and the shared Vulkan SDK. `bash ./build-wsl.sh --config Release` passed all 239 build/codegen steps. `recipe_simd_regen` and `recipe_simd_check` passed; the final `be8e9eab` regeneration reported no generated-content change from the already-built header.

Full labels were run on the generated artifact immediately before the final signature-only kernel commit; regeneration against `be8e9eab` was byte/content unchanged:

- SVO: 800/802 executed tests passed, 2 train-7 failures (`DeclaredPositionRenderTest.IntervalPrunedTileTapesMatchFullAndUnrolledGpuPixels`, `DeclaredPositionRenderTest.CorpusInlineLiteralsMatchRuntimeOperandsAndTape`).
- RenderGraph: 1,463/1,473 passed, 10 failures. The names are the retained train-7 set: three `EditorDocumentRenderTest` captures, `AppFlowEditorToggleRenderTest.ToggleThenUndoRestoresRender`, five `ShellRevalidateNodeTest` cases (the disjoint case plus canonical-dilation `/0`–`/3`), and `vixen_text_shader_capture_producer`. No recipe-specific `ProceduralRecipeRenderTest` case failed.
- The current SVO/RenderGraph failure names are a subset of `/home/liory/codeman-cases/undertow/reports/sn-vixtrain7-evidence/final-train/real-failed.txt`; no new failure name was observed.
- GPU look-dev repeat witness: `Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically` passed 1/1 in 492.53 seconds with `DISPLAY=:0`, producing 32 captures. A stored-SDF timing probe passed in 25.23 seconds; its capture timing was 14,720.2 ms frame time and 24,867.8 ms process time versus 12.47 seconds for the retained train-7 stored-SDF test. This is a timing note, not a performance claim.

## STOPs

- **Visual unchanged gate is unresolved.** Comparing current `build/wsl/runtime-captures/lookdev/midday` with the retained train-7 `triage/out4` references produced byte differences and 96,660 differing pixels in each of the four 500×500 angles (maximum channel deltas 204–212). The inspected contact sheet shows scene/content differences, not a zero-diff capture. The current run is repeat-identical internally, but cannot be claimed unchanged against the retained references. Owner action: provide the correctly matched train-7 reference set or explicitly accept this provenance mismatch.
- The SVO/RenderGraph red is a known, scope-independent train-7 baseline finding, not a new recipe regression; recipe parity and all ten procedural recipe render cases passed.

Recovery ladder: A repaired the invocation by using the global queue path, `UNDERTOW_ROOT`, and `build-wsl.sh --config Release`; B regenerated fresh pinned output and passed `recipe_simd_check`; C matched the remaining broad GPU failures against the train-7 failure manifest and validated the narrower recipe parity scope. The first full SG attempt exposed two emitter regressions; those were fixed and the final full SG/CG run is green.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: VIXEN queued builds require a manually paired `UNDERTOW_ROOT`
- proposed: `build-wsl.sh` requires `--config` for queued Release builds

