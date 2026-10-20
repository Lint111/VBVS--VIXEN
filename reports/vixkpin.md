# vixkpin round 2: kernel pin 96570e48 -> b1576418 (recipeguard), PathInstance block, consumers on PathPoint/PathSegment

Branch `lane-vixkpin` (lane sn-vixkpin2). Merged `origin/wave/authoring-convergence` twice (last: gaia pin / UninstallVulkan, 7 files, clean, rebuilt after). Undertow pair `pair-hlsl` at `origin/main-wave`. Release, FSR3 OFF, Dozen ICD (correctness only, no timing).

## Result in one line

The kernel pin is at `b1576418a1ca4cf29dd22853ff3cd1aba6372c83`, the build is green, all 37+1 drift guards pass, Text/Textures/VulkanResources/CashSystem and three of four capture producers are green. The 10 `ProceduralRecipeRenderTest` and 10 SVO HLSL tests are NOT fixed by this pin: the kernel HLSL emitter output is invalid (STOP below).

## What changed

- `VIXEN/codegen/CMakeLists.txt:132` and `VIXEN/cmake/tests/test_kernel_snapshot_reuse.cmake:5`: pin `b1576418...`. `rg 38da3fa1` outside reports finds nothing.
- `VIXEN/codegen/UnityMathematicsTypes.cs` deleted. The kernel's own `UnityMathematicsPrelude.cs` is enough for `--recipe-simd-cpp`, `--recipe-kernels-cpp` and `--recipe-opcode-mirror`; the `--recipe-kernels-extra` argument and all `_recipe_math_types_run` references are gone. No kernel follow-up needed for the floats.
- `VIXEN/codegen/CMakeLists.txt`: new `path_instance_check` (ALL) and `path_instance_regen` targets, same form as the glyph block. Generated: `SVO/include/Recipe/generated/PathInstance.g.hpp`, `PathInstance.layout.sha256`, `SVO/shaders/recipe/PathInstance.g.glsl`; `GlyphOutlineInstance.g.hpp` regenerated (recipeguard `#ifndef` guards let both headers coexist, so round 1's STOP is resolved).
- Consumers moved to `PathPoint` (3 floats) / `PathSegment` (56 B): `Text/include/Text/GlyphOutline.h:20-21`, `Text/src/GlyphOutline.cpp`, `Text/src/GlyphAtlas.cpp`, `Text/tests/test_text_shaping.cpp`, `application/main/tests/test_lookdev_capture.cpp`, `SVO/tests/test_recipe_glsl_numerical_parity.cpp:945`. The glyph payload is 2584 -> 3616 B and no hand-written code carries that number.
- SVO plumbing for `PathInstance` (the generated eval dispatch and kernels need it): `SdfRecipeEval.h` (`evalRecipe` takes `std::span<const PathInstance>`), `RecipeRegistry.h` (`pathInstances` entry field), `SdfRecipeCodegenGlsl.h` (emits `pathInstances[N]` rows), `UberShaderSplice.h`, `SpecializedRecipeShaderGlsl.h`, `VoxelDocument/src/EditorDocumentModel.cpp` (includes). Files shared with other lanes touched minimally: `SdfRecipeEval.h`, `RecipeRegistry.h`, `SdfRecipeCodegenGlsl.h`, `EditorDocumentModel.cpp`, Text sources.
- `SVO/tests/test_recipe_glsl_numerical_parity.cpp`: `GlyphOutline` and `PathDistance` exempted from `CorpusCoversEveryValidOpcode` (tracked gap for PathDistance, see issues).

## R660 classification (one line each)

- 37 `*_check` generated-source targets plus `path_instance_check`: INVARIANT ("source is current"); regenerated, not re-blessed.
- `GlyphOutlineInstance` 2584 -> 3616 B, segment stride 40 -> 56, `...LayoutHash`: no hard-coded copy in hand-written code (grep); generated `static_assert`s derive from the struct. Nothing to convert.
- `CorpusCoversEveryValidOpcode` (182, 183 uncovered): INVARIANT ("every valid opcode is exercised or exempted"); kept, exemptions documented in the test.
- `ProceduralRecipeRenderTest.*`, `EditorDocumentRenderTest.*`: image/compile thresholds, left as is (not re-blessed).

## Tests (MEASURED, evidence in `/home/liory/codeman-cases/undertow/reports/vixkpin2-evidence/`)

- Plain `cmake --build build/wsl` exit 0 (`build4`, and `build5` after the final wave merge).
- `ctest -L ^SVO$`: 789/799 pass, 10 fail: `RecipeCodegen.{EmitsMirrorCsgCompilesToSpirv, EmitsCsgModifierCompilesToSpirv, EmitsCompilableProceduralShader, EmitsLeafPrimitivesCompilesToSpirv, EmitsPositionedLeafPrimitivesCompilesToSpirv, EmitsM3b3PrimitivesCompilesToSpirv}`, `SdfRecipeCodegen.{M4b_TwistTransformSpirV_Compiles, M4a_RevolutionSpirV_Compiles, M4c_ValueMathDisplacementSpirV_Compiles}`, `CodegenEmit.M4d_Float3AndVMControl_CompilesSpirV`. Cause: "HLSL parsing failed" (see STOP). `RecipeGlslOpcodeCoverage` passes after the exemption.
- `ctest -L RenderGraph` (Dozen): 1414/1429 pass (11 of them skipped), 15 fail. Failing by name: `ProceduralRecipeRenderTest.{RenderProceduralRecipe, RenderMirrorCsgRecipe, RenderSubtractBoxSphere, RenderTorus, RenderCone, RenderPyramid, RenderRevolution, RenderTwist, RenderDisplacement, RenderComposition}`, `EditorDocumentRenderTest.{GoldenDocumentAllLayersRendersVisibleBody, DisablingCutLayerChangesTopFaceSilhouette, LayeredSlabSubtractSphereGuardsAreExactAndCaptureHasHole}`, `AppFlowEditorToggleRenderTest.ToggleThenUndoRestoresRender`, `vixen_text_shader_capture_producer` (Timeout).
- INFERRED base comparison: I did not build the merged wave base. Sn-vixleft's base run (`reports/vixleft.md`) lists the same names (10 Procedural, 3 EditorDocument with hitPixels 42082 vs 50000, 1 AppFlow with boreDiffPixels 0, the text-shader producer). So: the 10 `ProceduralRecipeRenderTest`, the 3 `EditorDocumentRenderTest` and the `AppFlowEditorToggleRenderTest` expected greens are all STILL RED, unchanged from the base, and no test newly failed.
- `BodyInstanceRayMarchRenderTest.RenderMultiKindBodiesProvesStrideFix` passes (no segfault in the full RenderGraph run this time; vixleft saw one in the text-shader producer). `vixen_text_shader_capture_producer`: Timeout at the 184 s ctest property, alone and in the full run: unchanged.
- Capture producers, each its own GPU job: `vixen_editor_capture_producer` PASS, `vixen_hud_capture_producer` PASS, `vixen_instancing_demo_capture_producer` PASS, `vixen_text_shader_capture_producer` Timeout.
- `ctest -L ^Textures$` 3/3, `^VulkanResources$` 17/17, `^CashSystem$` 117/117, `^Text$` 15/15.
- Hard-rule grep on added hand-written lines (EntityId, `KindRef<All>`, explicit `Id`, attribute forms): no matches.

## STOPs

- The 10 HLSL-composing SVO tests and the 10 `ProceduralRecipeRenderTest` fail because the kernel's HLSL emitter (`SdfCoreKernels.g.hlsl:10,12`) emits `GlyphOutlineInstance.SegmentCapacity` / `PathInstance.PathSegmentCapacity` (C# static member syntax) and defines no HLSL structs. MEASURED: error at shader:6 column 57, identical construct on the base. The brief's and vixleft's "stale pin" attribution is wrong. Fixing it is a kernel HLSL-emitter change; a VIXEN-side rewrite of the generated file would be a deviation, so I did not build one.
- The 3 `EditorDocumentRenderTest` and the AppFlow toggle test fail on image thresholds with the same numbers as the base; whether that is Dozen or a real regression is not isolated (needs a native GPU run). They are not the HLSL parse failure.

## SPT DISPOSITION

- stb pin fetchable: CLOSE (`f20e97c2`, round 1).
- `frame_targets_schema_check` real cause (stale pin): CLOSE (pin, exclusion revert and regenerated schema are green and landed with this branch).
- Kernel pin bump with goldens as invariants: CLOSE (`7e8e5284` + opcode exemption commit).
- Round-1 proposal "Kernel --recipe-parameter-block cannot emit GlyphOutlineInstance and PathInstance together": OBSOLETE, recipeguard fixed it; the Unity.Mathematics stub is deleted. Cannot be edited here, flag for closure.
- 10 `ProceduralRecipeRenderTest` HLSL failures: OPEN, owner is the kernel HLSL emitter (proposal below).
- 3 `EditorDocumentRenderTest` + 1 `AppFlowEditorToggleRenderTest`: OPEN (proposal below).
- Text-shader capture timeout/segfault: OPEN, unchanged.

## CONSOLIDATION ISSUES

- Kernel HLSL emitter output is not valid HLSL (static member access, no struct defs); breaks 10 SVO and 10 RenderGraph tests (`tier:sol effort:M`, `.spt-proposals/vixkpin2.jsonl`).
- `PathDistance` (opcode 183) has no producer, corpus program or parity test; exempted from the opcode-coverage corpus (`tier:luna effort:M`).
- Editor render image-threshold failures under Dozen are not isolated (`owner-question effort:M`).
- Round-1 kernel-emitter proposal in `.spt-proposals/vixkpin.jsonl` is obsolete (recipeguard landed in the kernel).
- `Vulkan-Headers GIT_TAG main` and `stbrumme/hash-library GIT_TAG master` remain moving refs (round 1, not changed).
