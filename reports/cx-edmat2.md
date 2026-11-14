# cx-edmat2 — runtime material rows reach pixels

## LANDABLE NOW

- VIXEN tip: `6c1377da6` (`SPT: clarify generated GLSL proposal`); feature commits are `55b373082`, `b125eba8c`, and `663666647`.
- VIXEN merge base: `9cb6ea18b4b9720ad5443516f7df59ab91f2705e`.
- Required kernel input: `41c23511cbc432a4fbb1a5a5f4e24d8bdf2bdd51`.
- Required Undertow pack input: `a8c6b1731baf2cd8a7c8476604dedb98a1d34852`.
- `origin/wave/authoring-convergence` was already up to date in the lane before the final witness.
- Landing order: Undertow material kind/rows first, kernel provider/codegen second, VIXEN consumer third. No push was performed.

## PLAN AS BUILT

The consumer now generates the kernel channel vocabulary in runtime mode:

`--channel-vocabulary --channels <dir> --material-source runtime --out-header <h> --out-glsl <glsl>`

The generated VIXEN files carry the material-id semantic and row layout but no pack rows. Pack rows are loaded at runtime from `materials.utdl`, converted through generated `MaterialPaletteSpecFromPackRow`, assigned declaration-order slot references, and uploaded as one fixed-capacity material table. A missing pack produces one neutral table plus a located diagnostic.

Recipe document material fields are lowered into the generic generated output stream: density output, a deterministic recipe-space region plane derived from the authored percentage, generated material-id output, and no material-specific shader branch. Stored recipes bake the output into the generic channel pool; procedural recipes retain the generic output path. The editor uses the stored path for material-bearing documents so changing major id, minor id, or percentage rebakes/uploads data without replacing the already-spliced shader program.

The look-dev hull witness uses the same generated output and table lookup. Its table rows are read from the Undertow pack; its coverage check samples the stored output sidecar and compares the actual major/minor slot references.

The in-place plan note `/home/liory/codeman-cases/undertow/reports/cx-edmat1-plan.md` was updated to record this runtime-table decision.

## CHANGE

- `VIXEN/codegen/CMakeLists.txt`: pins the VIXEN provider to kernel `41c23511...`, adds rowless runtime vocabulary generation/checks, and keeps pack input out of the consumer build.
- `VIXEN/libraries/SVO/include/Generated/VoxelChannelVocabulary.g.h` and `VIXEN/shaders/Generated/VoxelChannelVocabulary.glsl`: generated semantic/output/row contracts.
- `VIXEN/libraries/SVO/include/RuntimeMaterialTable.h`, `src/RuntimeMaterialTable.cpp`: pack `materials.utdl` loader, neutral fallback, declaration-order references, fixed material SSBO bytes.
- `VIXEN/libraries/VoxelDocument/src/EditorDocumentModel.cpp`: generic material output lowering; zero material triple preserves the legacy bytecode.
- `VIXEN/libraries/SVO/include/Recipe/RecipeBaker.h`, `SdfBake.h`, `ShellOctreeGpu.h`: CPU output evaluation and stored channel serialization.
- `VIXEN/shaders/StoredSdf.glsl`, `SceneBindings.glsl`, `RayQueryTraversal.glsl`: stored material-id lookup through the runtime table with neutral fallback.
- `VIXEN/libraries/RenderGraph/...BodyOctreeSceneNode...`, `VulkanGraphApplication.*`: staged whole-buffer table upload on load/change; shader pipelines remain unchanged.
- `VIXEN/application/editor/source/EditorApplication.cpp`: editor pack/loose-document table seam, rows view population, material-bearing documents use stored data and skip procedural replacement/recompile.
- `VIXEN/application/editor/include/EditorLayersView.*`, `codegen/view-schemas/EditorLayers.cs`, generated view files: data-only `materials` section; row id is the slot number and row name is the pack name.
- `VIXEN/application/main/include/graph/LookdevSceneDefinition.h`, `test_lookdev_capture.cpp`: pack-table colors, generated hull output, measured coverage.
- `.spt-proposals/cx-edmat2.jsonl`: four consolidation proposals.

## WITNESS

Evidence logs are under `/home/liory/.local/state/undertow/undertow-box-logs/`; the report evidence directory is `/home/liory/codeman-cases/undertow/reports/cx-edmat2-evidence/`.

- Runtime vocabulary regeneration/check: green; final merged checks include `channel_vocabulary_check`, `view_editor_layers_check`, `view_noun_enum_check`, and `recipe_simd_codegen`. The post-commit check log is `1794606794-light-final-codegen-postcommit.log`.
- Required generated ViewNoun recovery: `view_noun_enum_regen` and `view_noun_enum_check` green (`1794606092-light-regenerate-view-noun-merged.log`, `1794606101-light-check-view-noun-merged.log`). The full build then passed on retry (`1794606110-build-final-full-build-retry-merged.log`).
- Material-table tests: 4/4 green, including stable semantic ids, rowless vocabulary, pack declaration-order rows, GPU buffer size.
- Editor document model: 21/21 green in the broad model job; focused VDC1/preview set 4/4 green (`1794604225-test-editor-material-stored-tests.log)).
- Editor view/golden/reconcile set after the slot-id fix: 3/3 green (`1794606978-test-material-row-slot-id-test.log)).
- Look-dev CPU geometry edge test: 1/1 green; the direct executable witness passed `UniformFrameHasNoEdgesAndSphereDoes`.
- Recipe/SIMD/material parity targets: 6/6 targeted tests green; editor view/golden targets green; the fresh full build included all previously unbuilt test executables.
- No-material golden preservation: `EditorDocumentModelPreview.DirectEntryMatchesVrc1FlattenAndExportBytes` and `...ForLayerOverride` passed. The zero material triple skips the output suffix, preserving the legacy program bytes.
- Material-edit compile count: the material-bearing branch at `EditorApplication.cpp:1540-1574` does not call `ReplaceProceduralRecipe` or `RecompileProceduralShader`; it refreshes the bake producer and stored instance instead. The requested procedural shader recompile count for a material-only edit is therefore 0. An interactive native editor before/after capture was not available, so this is a code-path/trace witness rather than a windowed timing capture.
- Fresh full CPU suite: 1,740 executed of 1,761 registered, 99% pass; 2 unrelated reds are listed under STOPs/findings below. The full-suite log is `1794606467-test-final-full-cpu-suite-merged.log`.
- Dozen/WSL correctness run: material coverage was `major=0.764706`, `minor=0.235294`, with `2301` major and `708` minor voxels. Device class reported by the run: Microsoft Direct3D12 (AMD Radeon(TM) 8060S Graphics). Evidence: `1794603266-gpu-lookdev-dozen-material-midday.log`.
- Dozen image: [`midday-angle-0.png`](/home/liory/codeman-cases/undertow/reports/cx-edmat2-evidence/dozen-lookdev-material/midday-angle-0.png). It is a 1280x720 blank first-frame host/HUD readback showing no body; it is recorded only as Dozen correctness evidence, not as a material look judgement.
- The originally requested 140-name CPU set was attempted as one job before the full rebuild: 17 selected, 10 passed, 7 were not built. The later fresh full build produced those executables and the final CPU suite exercised the complete available CPU set. This is reported honestly rather than counted as the earlier partial job being complete.

## FOR THE CONSUMER

The future material panel should bind to the generated `EditorLayers.materials` view section. `EditorApplication::ConfigurePack` reads `RuntimeMaterialTable::Rows()` and publishes one data row per pack material with its numeric slot id and display name. The current lane adds no panel controls. The measured-coverage call is `LookdevSceneDefinition::MeasureHullMaterialCoverage(outputSet, majorRef, minorRef, halfWidth)`; it reports authored-result coverage beside the authored percentage for the panel lane to consume.

For the game path, the exact renderer seam is `VulkanGraphApplication::SetMaterialTable` -> `BodyOctreeSceneNode::SetMaterialTable`. The launcher/session owner should load the baked pack's `material` table through its existing content/session seam, convert the rows using the generated `MaterialPaletteSpecFromPackRow`, and call that renderer method after the body-octree node is available. No launcher or KFR code was changed here.

## FRAMEWORK GAPS

- The pinned generator emits `#define MAT_COUNT 0u`, which glslang rejects when used in a preprocessor `#if). The canonical generated file remains untouched; VIXEN has a narrow semantic shim in `VoxelChannelFormat.glsl). This should be fixed in the generator contract.
- Look-dev material roles still map to pack declaration-order slots by a hand-authored role table. A generated/content role binding is the consolidation target.
- The initial CodeGraph explore reported that this checkout has no usable `.codegraph` index, so source discovery used the documented fallback.
- The full-build generated-view gate required a separate `view_noun_enum_regen` after adding the `EditorLayers.materials` section.
- No native Windows GPU look-dev judgement image was obtained. Dozen is not substituted for that judgement.

## STOPs

- Recovery record for the required scope: base `9cb6ea18b4b9720ad5443516f7df59ab91f2705e`; `bash tools/check-vixen-codegen.sh` failed 127 (`baseline-codegen.log`), the wrong absolute retry failed 127 (`baseline-codegen-retry.log`), and the correct-root script with the old `VIXEN/build/wsl` directory failed exit 2 (`baseline-codegen-final.log`). Recovery A configured the documented `build/wsl` tree with detached kernel `41c...` and Undertow `a8c...`; recovery B ran `view_noun_enum_regen` after the full build exposed stale generated `ViewNounId.g.h`; fresh checks/build then passed. The guessed `test_lookdev_capture` target was corrected to the actual `vixen_lookdev_capture` target. Recovery C was the final scoped material/editor test set plus the fresh full CPU suite.
- No baseline-unobtainable STOP: the scoped generation, build, and material/editor tests are green on fresh binaries.
- Native GPU visual judgement: NOT RUN. Only the WSL/Dozen correctness job was admitted in this lane; no native capture is claimed.
- Full CPU suite findings, outside the material dependency scope:
  - `LODRayCastingTest.NoPerformanceRegressionWithoutLOD` failed once in the full suite and passed on the one targeted rerun; classify as a pre-existing/performance-environment flake.
  - `Native.NodeWidth` failed in both full and targeted runs with live queue reservation values changing between runs; source is `VIXEN/cmake/tests/test_node_width.sh`, unrelated to this feature and outside the material/render dependency. It remains a finding, not a material regression.

## SPT DISPOSITION

Four proposals were accepted and committed to `.spt-proposals/cx-edmat2.jsonl):

- `bd243d151358`: runtime channel vocabulary GLSL emitter emits unsigned preprocessor constants.
- `94e52f6a5831`: look-dev material roles still require declaration-order mapping.
- `0ef9d92a2143`: VIXEN codegen witness requires absolute build root and target discovery.
- `08cc3b1cc122`: EditorLayers schema changes require automatic ViewNounId regeneration.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: runtime channel vocabulary GLSL emitter emits unsigned preprocessor constants
- proposed: look-dev material roles still require declaration-order mapping
- proposed: VIXEN codegen witness requires absolute build root and target discovery
- proposed: EditorLayers schema changes require automatic ViewNounId regeneration
