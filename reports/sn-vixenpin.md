# sn-vixenpin: make VIXEN follow kernel main (T-4116, R619.4 rule 2)

Status: PARTIAL. The kernel half is done and tested. The VIXEN half stops at a design question, so VIXEN's kernel pin is **not moved** and no generated file is changed on this branch.

## What was asked

Move VIXEN's kernel pin (`VIXEN/codegen/CMakeLists.txt:132`, `_vixen_tracked_yeroket_kernel_sha`, now `96570e48d607bc6f1fa23d0007e5f27eb3f5d472`) to kernel main with no side kernel branch. The known blockers were `--recipe-simd-cpp` throwing on `float3` and a set of drifted generated files.

## Kernel fix (done)

`pending kernel 56be3e7d008ac899b0e0d98fa6d031ca522177f1` on kernel branch `lane-vixenpin` (one commit on top of kernel `origin/main` `ed6f114e7`). It is not landed yet; the VIXEN pin may only move once it is reachable from kernel main.

Reproduced at kernel main (MEASURED): `recipe_simd_check` throws `NotSupportedException: SdfCore_Sphere lane parameter: unsupported type 'float3'`.

Cause (MEASURED by tracing): the headless codegen compilation has no Unity.Mathematics reference, so a kernel's `float3` binds to a global-namespace error type. `PodTypeLifter.FromSymbol` returns null for error types. The older display-string matching used to accept the written name; when type lifting moved to pods, three places lost that: the dual renderer's lane-parameter spelling, its constant spelling (through the transpiler-cpp type-domain provider), and the lane recognizer (which had its own inline fallback). calculus3's `6bb1d1ee0` does not touch this.

Change: one tolerant lift, `PodTypeLifter.FromSymbolOrWrittenName` (`SourceGenerator~/Transpiler/TypeDomainProvider.cs`, before `FromSymbol`), used by the provider default (`TypeDomainProvider.cs`, `_fromSymbol = fromSymbol ?? ...`), by `LaneTypeRecognizer.FromSymbol` (`LaneCatalog.cs`, replacing the inline fallback) and by `CppWidenedDualProgramRenderer.DualType` (`CppWidenedDualProgramRenderer.cs`). It only accepts an error type in the global namespace whose written name is a Unity.Mathematics type, so other unknown types still fail with `UTTYPE002`. No new attribute, declaration form, abstraction or `throw`.

Test added: `RecipeKernelCodegenCliTests.RecipeSimdEmitsFloat3LaneParametersWhenUnityMathematicsIsUnresolved` (`CodegenTool~/Tests/RecipeKernelCodegenCliTests.cs`). It runs `--recipe-simd-cpp` on a `float3` kernel and asserts exit 0 and the emitted `SdfCore_SphereGradientSimd4(yk::simd4::dual3 p, ...`. It failed before the fix and passes after.

Kernel suites, run once via `tools/check-kernel-test-suite.sh --suite all` at `56be3e7d0` (MEASURED):

| Suite | Passed | Failed | Skipped |
|---|---|---|---|
| SourceGenerator (SDFNodeGenerator.Tests) | 1250 | 0 | 22 |
| CodegenTool | 2561 | 0 | 16 |

## VIXEN at the fix SHA (what was measured)

Configured with `-DVIXEN_YEROKET_KERNEL_SHA_OVERRIDE=56be3e7d008ac899b0e0d98fa6d031ca522177f1` (configure line showed the new SHA). `recipe_simd_check` no longer throws (MEASURED). The checks then report 25 STALE files and one schema `drift:`; `*_regen` rewrote 28 files (the 25, the schema JSON, `FrameTargets.g.cs`, `ExposureTonemapSystem.comp`). The full diff is kept in the evidence directory (not committed); the regenerated files were reverted here because they cannot be committed without the pin and the consumer changes below.

Classification of every drift:

| Files | What changed | Class |
|---|---|---|
| 15 GpuStruct headers (`AccumulationConfig`, `EmitterSample`, `EmitterTotals`, `LightTreeBuffer`, `LightingConfig`, `MiningBeamBuffer`, `PrevCameraConfig`, `ProbeGridConfig`, `RecipeEmitterEntry`, `ReservoirConfig`, `ReservoirRecord`, `ShadowConfig`, `SparseEmitterEntry`, `BodyInstanceMotion`, `OctreeConfig`) | one banner comment: `[GpuStruct].` became `[GpuStruct] declarations.` | Expected kernel evolution, text only |
| `SdfOpCodes.g.h`, `RecipeSimd.g.hpp`, `SdfRecipeEvalDispatch.g.inc`, `SdfRecipeCodegenGlslDispatch.g.inc` | new opcode 183 `PathDistance` (tables 123 to 124, 185 to 186, new dispatch cases, `SdfCore_PathDistanceSimd4`); also `broadcast_mask` and `dual3 lerp` helpers | Expected kernel evolution (kernel `d08b75637`, fixed-bucket path distance); helpers from calculus lanes |
| `GlyphOutlineInstance.g.hpp`, `.g.glsl`, `.layout.sha256` | `GlyphOutlinePoint`/`GlyphOutlineSegment` replaced by shared 3-float `PathPoint`/`PathSegment`; segment 40 to 56 bytes; `GlyphOutlineInstance` 2584 to 3616 bytes; layout hash changes | Expected kernel evolution (same commit), but it changes a GPU layout VIXEN consumes |
| `SdfCoreKernels.g.hpp`, `.g.glsl`, `.g.hlsl` | `SdfCore_PathDistance` and helpers, which take a `PathInstance` | Expected kernel evolution (same commit) |
| `frame-targets.schemas.json`, `FrameTargets.g.cs`, `ExposureTonemapSystem.comp` | follows the current Undertow content (rate facets, namespaced-id refs, `REMOVE` command stubs); the shader moves its SSBO blocks to named `meter.values[0]` form | Not caused by the float3 fix; see below |

No regression was found: every STALE file is explained by a kernel commit, and none needed a hand edit.

Frame-targets chain (MEASURED): at the **current** pin `96570e48`, `frame_targets_schema_check` already fails against the current Undertow checkout with `SubstrateKinds.cs:173:13: unsupported schema field fluent call 'Rate'`. So that chain is red on the wave today and moving the pin is what makes it buildable; it also means those three files depend on which Undertow checkout is discovered.

## STOP: the pin move needs VIXEN to adopt the new path primitive

The regenerated code does not compile in VIXEN (MEASURED, WSL `-k 0` build of all unit-test targets, `SVO` fails first):

- `RecipeSimd.g.hpp:3025` and `SdfCoreKernels.g.hpp:80` use `PathInstance`, which no VIXEN codegen target generates.
- `SdfRecipeEvalDispatch.g.inc:931` uses `pathInstances`, which `evalRecipe` does not have (it has `glyphInstances`).
- INFERRED from `rg`, not yet compiled: `Text/GlyphOutline.h:20-21` aliases the removed `GlyphOutlineSegment` and `GlyphOutlinePoint`; `Text/src/GlyphAtlas.cpp`, `Text/src/GlyphOutline.cpp`, `Text/tests/test_text_shaping.cpp`, `SVO/tests/test_recipe_glsl_numerical_parity.cpp:945` and `application/main/tests/test_lookdev_capture.cpp:472` use them.

Why this is a design question and not a mechanical fix:

1. `--recipe-parameter-block` (`CodegenTool~/Wizard/RecipeParameterBlockModule.cs`) takes one type and writes that type's dependencies unguarded in C++. Adding a second block for `PathInstance` would redefine `PathPoint` and `PathSegment` next to `GlyphOutlineInstance`. The options are a kernel change (several blocks per invocation, or one `GlyphOutlineInstance`+`PathInstance` output), or one VIXEN target that emits only `PathInstance` and drops the glyph one. Each is a new declaration shape.
2. Making `pathInstances` real needs storage and GPU binding next to `glyphInstances` (`RecipeRegistry.h:103`, `SdfRecipeEval.h:45`, `SdfBake.h:401`, `SdfRecipeCodegenGlsl.h:64`, `UberShaderSplice.h`, `SpecializedRecipeShaderGlsl.h`). A default-empty parameter would compile, but opcode 183 would index an empty span if any recipe ever used it. No VIXEN content uses it today.
3. The Text library and the lookdev harness must move from the 2D glyph point types to the shared 3D `PathPoint` (Z stays 0), which changes the glyph GPU row size VIXEN uploads.

Recommended: let the lane that adopts PathDistance in VIXEN own items 1 to 3 together with this pin move, since it must choose the declaration shape anyway. This lane's kernel commit is the prerequisite for it.

## Witnesses not produced

- VIXEN unit tests and lookdev captures, native and Dozen: not run. They only mean something after the consumers compile, and the pin did not move, so the wave's behavior is unchanged by this branch. Nothing here is a claim about pixels or hit records.
- Native Windows clone `C:\vixen-native\vixenpin` was not created.

## Evidence

Raw logs (not committed): `/home/liory/codeman-cases/undertow/reports/sn-vixenpin-evidence/` including `configure-fix.log`, `checks-fix.log`, `regen-fix.log`, `vixen-regen-at-56be3e7d0.diff`, `build-fix2.log`, `frame-oldpin.log`, `kernel-suites.log`, `kernel-focused-2.log`.

## R412 checklist on added lines

Kernel: no new attribute, no `Catalog<T>`, no new `throw`, no new declaration form; one static helper reused at three call sites. VIXEN: this report only.

## SPT DISPOSITION

- T-4116: PARTIAL. Kernel float3 emitter fixed and suite-green (`pending kernel 56be3e7d008ac899b0e0d98fa6d031ca522177f1`); VIXEN pin move OPEN, blocked on adopting the kernel path primitive (`PathInstance`/`pathInstances`, 3D `PathPoint`/`PathSegment`).

## CONSOLIDATION ISSUES

- Landing order: kernel `56be3e7d0` must reach kernel main before any VIXEN pin change. VIXEN `main` was not touched.
- The frame-targets check chain is red at the current pin against the current Undertow checkout (`unsupported schema field fluent call 'Rate'`); it depends on the discovered Undertow checkout rather than on the pin alone.
- `FidelityFX_SC.exe` is gitignored, so a fresh worktree cannot build the WSL test targets until it is copied in; this lane copied it from the fsr3 worktree (ignored file, not committed).
- `--recipe-parameter-block` handles one type per invocation and emits shared dependency structs unguarded, which blocks emitting both `GlyphOutlineInstance` and `PathInstance`.
