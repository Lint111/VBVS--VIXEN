# vixkpin: kernel pin 96570e48 -> 38da3fa1 (checkpoint: build is red, design question open)

Branch `lane-vixkpin`, off `origin/wave/authoring-convergence` `b740376d` (base unchanged at the end). Undertow pair `pair-hlsl` fast-forwarded to `origin/main-wave` `27880296c`, clean. Release, FSR3 OFF.

## Result in one line

The stb pin is done and landable. The kernel pin bump regenerates cleanly and every drift guard passes, but the C++ build does not link up with the new kernel's glyph/path model, and fixing that needs a design decision (STOP below).

## What changed

- `f20e97c2` (LANDABLE): `VIXEN/dependencies/CMakeLists.txt:28-35` stb pinned to `2b2e64bc`, `GIT_SHALLOW` removed (a shallow clone holds only the branch tip, so a pinned SHA failed with "unable to read tree"). MEASURED: fresh FetchContent cache, configure exit 0, `stb-src` HEAD `2b2e64bc`, `stb_image_resize2.h` at the root. Other moving refs: `Vulkan-Headers GIT_TAG main` (`dependencies/CMakeLists.txt:20`) and `stbrumme/hash-library GIT_TAG master` (`:58`, only used when OpenSSL is missing). Both reported, not changed.
- Checkpoint commit (NOT landable, build red):
  - `VIXEN/codegen/CMakeLists.txt:132` and `VIXEN/cmake/tests/test_kernel_snapshot_reuse.cmake:5`: pin `38da3fa122d4801b73125dd43bfffae25b20a732`. Configure prints "[codegen] using tracked Yeroket kernel pin: 38da3fa1...".
  - `VIXEN/codegen/CMakeLists.txt` (was ~:775): the `SubstrateKinds.cs` overlay exclusion is removed. The new pin parses `.Rate`.
  - `frame-targets.schemas.json` / `FrameTargets.g.cs` regenerated: 33 Substrate kinds are back (the `part_of` catalogue entry that sn-vixleft flagged as dangling is present again; the `factions` field line now carries `"refRelation": "PartOf"`). `frame_targets_schema_check` and `frame_targets_ref_check` pass.
  - All other generated sources regenerated through the `*_regen` targets (config headers, `GlyphOutlineInstance.*`, `SdfCoreKernels.g.{hpp,hlsl,glsl}`, `SdfOpCodes.g.h`, `RecipeSimd.g.hpp`, eval/GLSL dispatch, `ExposureTonemapSystem.comp`).
  - New `VIXEN/codegen/UnityMathematicsTypes.cs`, passed as one extra `--recipe-kernels-extra` to `recipe_simd` only (`codegen/CMakeLists.txt`, the `_recipe_simd_args` block). Reason below.

## Drift guards (MEASURED, `reports`-evidence in `/home/liory/codeman-cases/undertow/reports/vixkpin-evidence/`)

- First run on the new pin (`checks1.log`): 20 of 37 `*_check` targets failed (octreeconfig, frame_targets_schema, glyph_outline_instance, recipe_simd, sdf_core_kernels, recipe_opcode_mirror, and the 14 config/buffer headers: lighting, shadow, accumulation, prevcamera, bodyinstancemotion, reservoirconfig, probegrid, lighttree, miningbeam, reservoirrecord, emittersample, emittertotals, recipeemitterentry, sparseemitterentry).
- After regenerating (`checks2.log`): all 37 pass, exit 0.
- R660 classification: these `*_check` targets compare a checked-in generated file with the generator output. That is an invariant ("the generated source is current"), not a hard-coded value, so I regenerated rather than converted or deleted. The 14 config/buffer headers drifted by one banner line only ("...[GpuStruct]." became "...[GpuStruct] declarations.").
- I did not reach the ctest layer, so I could not yet classify tests that assert hard-coded values of generated code. Candidates to inspect once the build is green: the `GlyphOutlineInstance` size (2584 becomes 3616), the segment stride (40 becomes 56), `GlyphOutlineInstanceLayoutHash`, and `ProceduralRecipeRenderTest` / `EditorDocumentRenderTest` / `AppFlowEditorToggleRenderTest` (the vixleft failures). No golden was re-blessed by hand.

## Why the kernel pin needs a stub (kernel regression, INFERRED from source + MEASURED)

`--recipe-simd-cpp` over the kernel's own `SdfCoreKernels.cs` throws `SdfCore_Sphere lane parameter: unsupported type 'float3'`. At `38da3fa1` `CppWidenedDualProgramRenderer.DualType` (`SourceGenerator~/Transpiler/CppWidenedDualProgramRenderer.cs:201`) lifts the symbol through `PodTypeLifter`; the standalone CodegenTool compilation has no `Unity.Mathematics`, so `float3` is an error type. The old pin matched the display name string. The kernel's tests pass because their fixtures declare `float3`. MEASURED: with the small type stub the command succeeds. The proper fix is in the kernel (its recipe loader should supply the types it requires). The stub is a VIXEN-side workaround.

## STOP: build is red, needs an owner decision

`cmake --build build/wsl -- -j4` exits 1 (`build1.log`, first failure `Text/src/GlyphAtlas.cpp`). Causes (MEASURED):

1. The kernel now has two fixed-bucket types in `GlyphOutlineSegment.cs` that share structs: `GlyphOutlineInstance` (64 segments) and the new `PathInstance` (256 segments), both built on `PathPoint` (3 floats, was `GlyphOutlinePoint` with 2) and `PathSegment` (56 bytes, was `GlyphOutlineSegment` at 40). The regenerated `SdfCoreKernels.g.hpp:80` uses `PathInstance`, which VIXEN does not generate. `--recipe-parameter-block` takes one type per invocation and each C++ header defines `PathPoint` and `PathSegment` itself (the GLSL has include guards, the C++ does not; MEASURED by generating `PathInstance` to a scratch dir). So a second `PathInstance` header cannot be included next to `GlyphOutlineInstance.g.hpp`: redefinition.
2. Hand-written code names the old types: `Text/include/Text/GlyphOutline.h:20-21`, `Text/src/GlyphOutline.cpp`, `Text/src/GlyphAtlas.cpp`, `Text/tests/test_text_shaping.cpp`, `application/main/tests/test_lookdev_capture.cpp`, `SVO/tests/test_recipe_glsl_numerical_parity.cpp`. The glyph payload also grows (2584 to 3616 bytes), so GPU buffers sized from it change. Those are mechanical once (1) is decided, but they touch the text pipeline.

Options for (1):
- A (recommended): kernel change so `--recipe-parameter-block` can emit several blocks into one header (shared structs once), or so the C++ emitter guards each struct as the GLSL one does. VIXEN then adds a `PathInstance` output through the existing target form.
- B: VIXEN-only: post-process or split generated headers. This is a bespoke mechanism (R394 deviation smell); I did not build it.
- C: hold the pin at a kernel commit before `2eaf05361` (fixed-bucket path). Not possible: that commit precedes the one that adds `.Rate` parsing (`9feadc5fc`), so no SHA gives `.Rate` without the path types.

Nothing else was attempted past this point: no ctest label was run (the tree does not build), so the requested before/after of `RenderGraph` (before = 1404 pass / 25 fail, measured by sn-vixleft, base not rebuilt), Textures, VulkanResources, CashSystem and the capture producers is NOT measured. The 10 RmlUi "No font engine" and the Dozen text-shader device-lost remain as vixleft reported them (not re-run).

Hard-rule grep on hand-written added lines (EntityId, `KindRef<All>`, explicit `Id`, attributes): none. Regenerated `frame-targets.schemas.json` carries 3 generated `detail` strings quoting `EntityId.Value` for Undertow-owned Substrate fields (`asset`, `controller`, `ownerStamp`); they are descriptive text mirrored from Undertow's `SubstrateKinds.cs`, not a VIXEN field, parameter or cast. The EntityId count in hand-written VIXEN code did not change.

## SPT DISPOSITION

- stb pin fetchable: CLOSE (`f20e97c2`).
- `frame_targets_schema_check` real cause (stale pin): PARTIAL. Pin, exclusion revert and regenerated schema are done and green; landing needs decision (1).
- Kernel pin bump to `38da3fa1` with goldens as invariants: PARTIAL / OPEN (blocked on the PathInstance/GlyphOutlineInstance emitter question).
- 10 `ProceduralRecipeRenderTest` HLSL failures: OPEN, not re-tested (regenerated `SdfCoreKernels.g.hlsl` no longer uses `GlyphOutlineInstance.SegmentCapacity`, but nothing was run).

## CONSOLIDATION ISSUES

- Kernel `--recipe-simd-cpp` on the kernel's own `SdfCoreKernels.cs` needs a `Unity.Mathematics` prelude; the VIXEN stub (`codegen/UnityMathematicsTypes.cs`) is a workaround to delete once the kernel supplies it.
- Kernel `--recipe-parameter-block` cannot emit `GlyphOutlineInstance` and `PathInstance` together (duplicate `PathPoint`/`PathSegment` in C++).
- The `FactionAi` overlay exclusion in `codegen/CMakeLists.txt` (~:766) still says the pinned reader cannot parse the named visibility argument; not re-tested on the new pin.
