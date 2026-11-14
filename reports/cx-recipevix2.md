# cx-recipevix2 — R841 recipe lowering, VIXEN consumer slice 7

## STATUS

Complete and landable. The remaining production/test consumers of the retired bounds, occupancy,
tile-specialization, whole-domain-compaction, and old GLSL field-emitter paths now use the three
recipe services (`YkRecipeRuntime.h`, `YkRecipeGpuLowering.h`, and `YkRecipeAnalysis.h`) through
`RecipeServices.h`. The four old headers, the old GLSL emitter, and both generated GLSL-dispatch
fragments are deleted. `RecipeSimd.g.hpp` remains only for document flatten/export consumers.

Base was VIXEN `30ca56cf54daba51304ad0a56adf67d5cba2c1ef` (slice-6 tip). CodeGraph was attempted
first but is unavailable in this checkout (`codegraph explore`: no `.codegraph`); the inventory then
used `git grep`/`rg`.

## INVENTORY

| Consumer | New home / disposition |
|---|---|
| `SpecializedRecipeShaderGlsl.h` | `RecipeGpuField`, with no environment fallback or old-emitter fallback. |
| `UberShaderSplice.h` | `RecipeGpuField` and service-owned declared-position handling. |
| `test_switch_cost_isolation.cpp`, `test_uber_shader_splice.cpp` | Service GPU field lowering. |
| `test_recipe_codegen_glsl.cpp`, `test_recipe_glsl_numerical_parity.cpp` | `RecipeGpuField`; tests retain the same GLSL compile/numerical contracts. |
| `test_recipe_declared_position_render.cpp` | `RecipeGpuTape`, `RecipeGpuField`, and `AnalyzeRecipeScene`. |
| `test_recipe_nested_invocation.cpp` | `EvaluateRecipe4`, closed-tape `RecipeGpuField`, and service GPU parity. |
| `test_recipe_bounds.cpp`, `test_recipe_occupancy.cpp`, `test_emitter_sampling.cpp` | `AnalyzeRecipeRegistration`; occupancy probes use `EvaluateRecipe4`. |
| `test_recipe_interval_pruning.cpp` | `AnalyzeRecipeScene` for tile specialization and whole-domain compaction. |
| `test_recipe_services.cpp` | Service-vs-recorded-golden tests for the old field, bound, occupancy, tile, and compaction outputs. |
| `test_recipe_simd_parity.cpp` | `EvaluateRecipe4`, with the old SIMD output removed from the test. |
| RenderGraph baked/virtual, editor-document, and switch tests; VoxelDocument preview tests | Service analysis/GPU APIs; the editor/render paths retain only required flattening support. |
| `test_appflow_editor_toggle_render.cpp`, `test_editor_document_render.cpp`, `test_voxel_document_flatten.cpp`, `EditorDocumentModel.cpp` | Keep `RecipeSimd.g.hpp` because these consumers call `FlattenVoxelDocument` for VRC1/document/editor materialization. |

The initial-base inventory found the old direct includes/calls in the specialized shader, uber splice,
and the recipe/RenderGraph/VoxelDocument tests listed above. The final C++/CMake audit has no direct
reference to the four deleted headers, `SdfRecipeCodegenGlsl.h`, or `EmitProceduralFieldFunctionGlsl`.

## PARITY

The old outputs were captured before deleting the oracle and are now recorded in
`test_recipe_services.cpp`:

| Contract | Recorded old oracle | Service assertion |
|---|---|---|
| Sphere GLSL field | 227 bytes; FNV-1a `9945844444134442959` | Exact service string bytes/hash. |
| Bound sphere | raw center/radius bits `1065353216, 3221225472, 1077936128, 1083179008` | Exact bound source and bits. |
| Occupancy sphere | `ok`, dim `16`, cell-size bits `1058013184`; values FNV-1a `677295323107829139` | Exact dimensions, metadata, and values hash. |
| Tile/compact scene | source `[sphere origin r1, sphere x80 r2, union]`, domain `[-2,2]`, revision `41`; tile retained `1`, compacted final `1`; tape FNV-1a `14988329129560913562` | Exact service scene-analysis/tape golden. |

Fresh witnesses after deletion:

- Queued post-delete CMake build of `rendergraph_svo_tests` and `VixenApp`: passed.
- Queued `recipe_simd_check` and `recipe_services_codegen`: passed; no work remained after the
  regenerated artifacts were validated.
- CPU recipe/corpus selection: `240` tests, `237` passed and `3` expected skips, `0` failures.
- GPU recipe/render selection on dzn through the GPU queue: `49` tests, `47` passed and `2` expected
  skips, `0` failures. The CTest dzn configuration was used; lavapipe was not used.

## DELETED

- `VIXEN/libraries/SVO/include/Recipe/RecipeBounds.h`
- `RecipeOccupancy.h`
- `RecipeTileSpecialization.h`
- `RecipeWholeDomainCompaction.h`
- `VIXEN/libraries/SVO/include/Recipe/SdfRecipeCodegenGlsl.h`
- `VIXEN/libraries/SVO/include/Recipe/generated/SdfRecipeCodegenGlslDispatch.g.inc`
- `VIXEN/libraries/SVO/shaders/recipe/SdfRecipeCodegenGlslDispatch.g.inc`
- The `VIXEN_S1_RECIPE_GLSL` environment fallback and all old emitter fallbacks.
- The CMake `--out-glsl-dispatch` generation path.

## KEPT

- `EditorDocumentModel.cpp` and the four flatten/editor consumers: they still call generated
  `FlattenVoxelDocument`, which is a document serialization/materialization contract rather than a
  retired recipe analysis or GLSL-lowering path.
- `SdfRecipeEval.h` and `SdfRecipeEvalDispatch.g.inc`: canonical scalar document/runtime evaluation
  remains required and is not an old field emitter.
- `RecipeSimd.g.hpp`: generated flattening and VRC1 serialization remain required; SIMD parity
  consumers themselves have moved to the CPU service ABI.
- `SdfRecipeCodegen.h`: retained only for the standalone HLSL procedural-compute tests. It is not
  the retired GLSL field emitter and has no equivalent service API for that separate HLSL fixture.

## LANDABLE NOW

Yes. VIXEN changes, the SPT inbox proposal, and this report are ready to commit. No kernel changes
were needed; the assigned kernel worktree remains clean.

## QUESTIONS

None.

## STOPs

None.

## SPT DISPOSITION

Filed `.spt-proposals/cx-recipevix2.jsonl` with the title below. The first generation attempt used an
auto-derived home provider and tracked pin, returned success without service outputs, and failed at
the target build. Recovery explicitly paired the assigned provider worktree and full kernel SHA,
then regenerated and built successfully.

## CONSOLIDATION ISSUES

- proposed: Recipe service generation needs an explicit paired provider and exact kernel pin
