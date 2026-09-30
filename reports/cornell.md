# Cornell fixture extraction report

**Status: STOPPED before source edits.** The active material-ID fallback is used by shared rendering and voxel baking. Removing its scene palette requires the material data-set contract called out by T-1365; the current audit says material identity does not yet flow through render view data to a response table. I did not choose or invent that contract.

Base SHA: `44d728e138a7a237cc6a6a97480c3eff2f404b1e` (`lane-cornell`).

## Summary

No shader, C++ source, fixture, or golden capture was changed. The required extraction cannot be completed safely until the owner decides how fixture material identity and response data reach both GPU rendering and CPU voxel baking.

The CodeGraph-first query was attempted (`codegraph explore "Cornell-specific palette material table tie-break rules shared shaders C++ fixtures"`), but this worktree has no `.codegraph/` index. Per CodeGraph's output, I did not initialize one.

## Sites reviewed

| Site | Evidence | Status |
|---|---|---|
| `VIXEN/shaders/Materials.glsl:18-45` | Defines ID-to-color palettes for Cornell, tunnel, and city materials. `VIXEN/shaders/SceneBindings.glsl:40` includes it; the dense voxel hit path calls `getMaterialColor` at `SceneBindings.glsl:1304`. The production `BodyInstanceRayMarch.comp` includes `SceneBindings.glsl` at line 122. | **STOP:** this is a shared rendering input. Externalizing the lookup requires a declared material data source and binding contract. |
| `VIXEN/libraries/SVO/src/SVORebuild.cpp:68-120,730` | `MaterialIdToColor` duplicates the palette and is used when voxel data has a material ID but no explicit color. | **STOP:** the CPU bake path also needs the same authored data or an agreed pre-baked color/channel representation. |
| `VIXEN/libraries/CashSystem/src/VoxelSceneCacher.cpp:441-470` | Constructs a Cornell material palette in scene-cache output. | Fixture-owned scene content is a candidate to move, but its relationship to the shared CPU/GPU material contract must be settled with the blocked sites. |
| `VIXEN/shaders/TraceWorld.glsl:27-52,213,329-332,602-609,743` | Shared traversal applies the relative epsilon and lower-instance-index tie rule; the file documents that it was added for abutting Cornell walls. | Shared production behavior. Moving the rule requires an explicit source and binding point for fixture-selected tie policy while preserving deterministic selection for other scenes. |
| `VIXEN/shaders/TraceWorld.glsl:398-401,757` and `VIXEN/application/main/source/graph/BuildRenderGraph.cpp:6181-6197,6368-6374` | The procedural Cornell variant supplies its light emission through `recipeParams[3]`, which `TraceWorld` reads. The baked/stored variant also writes that slot, but its own comment says the stored-provider path does not read it; the stored hit instead uses the `SEM_EMISSION` channel. | Not changed. Resolve with the same material-data decision if this value is part of the requested scene-specific content. |

The audit's §4 lists three possible material seams (specialization constants with compact parameters, emitted recipe variants, or data-driven records). The owner must select the compatible material identity/data contract under T-1365 before this lane can bind fixture-owned values. No option was implemented here.

## Image comparison

Not run: no rendering source or fixture input changed, so there is no before/after image pair to compare and no pixel-diff result to report.

## Verification

- No CMake configure, build, or GPU test was run because implementation stopped at the required design gate.
- Focused witness suites remain unrun: `HeadlessUiGraph`, `RenderTargetNodeConfigTest`, `BodyInstance`, `EditorDocumentRender`, `HitRecordReadback`, `TierCrossing`, and Cornell-scene tests.
- This report is the only artifact and is validated with `git diff --check` before commit.

## STOP

**Decision needed:** define how fixture material identity and response data are supplied consistently to the GPU dense-voxel lookup and the CPU SVO rebuild fallback, and where the Cornell tie policy is selected for a dispatch.

Options for owner review:

1. Bind fixture-owned material records to the shared render/bake inputs, with a declared identity key and fallback behavior.
2. Have fixtures bake explicit color/material channels before the shared renderer consumes them, and define how legacy ID-only voxel inputs are handled.

Until that choice is made, deleting the active palette would change production rendering/baking, while replacing it with a guessed table or implicit default would violate the image-preservation requirement.

## CONSOLIDATION ISSUES

None. The CodeGraph index is absent in this worktree; no tooling workaround was used.

## Run 2 — R326 shared traversal tie-break

**Base SHA:** `0c7099363812757ddd2ed4e71117c680046de1ef` (`lane-cornell`). `origin/wave/authoring-convergence` was merged first and was already up to date.

### Rule

Replaced the Cornell-specific relative-epsilon seam behavior with one generic lexicographic hit key: **lowest world-space hit distance wins; at an exact distance tie, the lower `bodyInstances[]` slot (`instIdx`) wins.** This slot is also the TLAS custom instance index and the instance ID written to hit records, so the ESVO/procedural and RT-query paths use the same key. There is no epsilon band and no Cornell-specific branch in shared traversal.

`TraceWorld.glsl` now owns the comparator. Its bound-sphere and ESVO entry culls reject only entries strictly farther than the current best, leaving exact ties eligible. Regime-3 compositing also tracks the second candidate's instance slot and uses the same key, so equal-distance behind layers are independent of traversal order. `RayQueryTraversal.glsl` declares and calls that same comparator for its far-field and marched-SDF candidates; its lower-bound cull likewise keeps exact ties. No changes were made to `Materials.glsl` or `SVORebuild.cpp`.

Static grep over `TraceWorld.glsl` and `RayQueryTraversal.glsl` found no `SEAM_TIE_EPS_REL`, tie-band remnants, or Cornell-specific tie comments/branches.

### Image and determinism

No Cornell before/after image pair was produced, so pixel changes are unmeasured and the same-image-twice comparison remains unverified. The normal Cornell capture is window/swapchain based; this shell has neither `DISPLAY` nor `WAYLAND_DISPLAY`, and `Xvfb`/`xvfb-run` are unavailable. `HeadlessUiGraph` reads back a UI frame but does not exercise Cornell traversal. The old epsilon rule could select a lower slot even when that hit was slightly farther; the new rule always chooses the smaller distance and uses the slot only for exact ties, including equal-distance second layers in composite mode. Near- and exact-tie seam pixels are the expected places for visible differences. No image tuning was attempted.

### Baseline and verification

Required scope was the shared `TraceWorld` shader and its ESVO/procedural and RT-query consumers, validated by fresh shader compilation, the listed GPU/offscreen tests, and Cornell scene tests. The CodeGraph-first query was attempted before source search; no usable index/route was available, so the remaining discovery used `rg`.

- **A — invocation/provisioning:** `cmake --preset vixen-wsl -DVIXEN_SCHEMA_CATALOG=/home/liory/projects/undertow/core/src/Undertow.Authoring/Schema/schemas.json` passed through the queue. The initial build's missing `spirv_reflect.h` was cleared by repeating that documented configure and rebuilding. The subsequent ICU failure was cleared for target verification with `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1`.
- Before semantic edits, the first queued full build failed at `SpirvReflector.cpp:3` because `spirv_reflect.h` was unavailable (exit 1; log `/home/liory/.local/state/undertow/undertow-box-logs/1790789136-build-baseline-build.log`). Re-running the documented queued configure and build got past that header failure.
- **B — regenerate/check:** the next full-build attempt failed because the .NET CodegenTool could not find ICU (exit 1; log `/home/liory/.local/state/undertow/undertow-box-logs/1790789435-build-baseline-build-retry.log`). Queued CodegenTool restore/build and its documented schema `--check` passed with `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1`; no generated-file diff resulted.
- **C — isolate the remaining red:** with that recovery, the full `ALL` build reached the AppFlow schema check and failed on the unchanged Undertow catalog: `DepletionMaterialRow` declares `gaia:true` without row scope/ref-field pairing (exit 1; log `/home/liory/.local/state/undertow/undertow-box-logs/1790790064-build-baseline-build-codegen.log`). This was captured on the base SHA before edits. The check consumes the external Undertow schema and is outside the changed shader dependency graph. The fresh scoped target build and same 91-test focused set passed on the unchanged base before edits, and passed again after edits. No generated source or schema was edited.
- After edits, the first scoped build invocation used the nonexistent target name `test_rendergraph_criticalnodes_voxelsystems` and stopped before compilation (exit 1; log `/home/liory/.local/state/undertow/undertow-box-logs/1790790923-build-r326-target-build.log`). The corrected queued build, using `test_rendergraph_voxelsystems`, passed for `test_rendergraph_criticalnodes_infra1`, `test_rendergraph_criticalnodes_gpurender1`, `test_rendergraph_criticalnodes_gpurender2`, `test_rendergraph_voxelsystems`, `test_cornell_box`, `test_ray_casting_comprehensive`, and `test_headless_ui_graph`.
- The default and B1 shader variants compiled in that build. The optional RT-query plus regime-3 composite shader branch was separately compiled through the queue with bundled `glslc`, `--target-env=vulkan1.3`, `-DVIXEN_RTQUERY_TRAVERSAL=1`, and `-DVIXEN_REGIME3_COMPOSITE=1`; it passed.
- The requested focused CTest regex plus Cornell/SceneGenerator tests passed **91/91** (`100% tests passed, 0 tests failed`, 31.83 s): `HeadlessUiGraph|RenderTargetNodeConfigTest|BodyInstance|EditorDocumentRender|HitRecordReadback|TierCrossing|Cornell|SceneGenerator`. This includes GPU body-instance/readback renders, tier-crossing, Cornell scene tests, and an offscreen UI capture.
- `git diff --check` passed.

No baseline STOP: the full-build AppFlow red was reproducible on the unchanged base and scoped checks passed. The Cornell image/pixel and repeated-capture witness is the remaining verification limitation.

## CONSOLIDATION ISSUES

- proposed: CodeGraph query route is unavailable in isolated VIXEN worktrees
- proposed: VIXEN CodegenTool needs ICU provisioning or invariant globalization setting
- proposed: First VIXEN configure leaves spirv-reflect header unavailable to the initial build
- proposed: Add a registered compile check for the RTQuery traversal shader variant
- proposed: Add an offscreen Cornell render-and-hash witness
