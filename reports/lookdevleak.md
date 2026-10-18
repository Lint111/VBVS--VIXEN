# R631 glyph buffer lookdev lane

Base: `f750d6e9d2535dfc97329584c9d067b9a9269784` (`origin/wave/authoring-convergence`, merged and already up to date).

## Landable implementation

- `VIXEN/libraries/SVO/include/Recipe/SdfRecipeCodegenGlsl.h:81-112` no longer emits `GlyphOutlineInstance` rows as GLSL locals. It rebases each recipe-local `GlyphOutline` operand into a deterministic flattened SSBO index; nested recipe walks use the same registry order.
- `VIXEN/libraries/SVO/include/Recipe/UberShaderSplice.h:47-119` concatenates typed glyph rows in `RecipeRegistry::Ids()` order and publishes that payload from the primary shader-build callback.
- `VIXEN/shaders/RecipeGlyphBindings.glsl:1-13` declares the shared runtime buffer at binding 51. The production traversal/lighting sources include it, and `SpatialReuseShade` exposes it in both normal and shadow-debug variants. SDI merged headers were regenerated through `sdi_tool` and pass `--check`.
- `VIXEN/libraries/RenderGraph/include/Data/Nodes/BodyOctreeSceneNodeConfig.h:181-267` and `BodyOctreeSceneNode.cpp:447-1380` add the persistent descriptor-valid glyph SSBO, upload/retire/destroy it with the octree generation, and expose it through the scene provider at `BuildRenderGraph.cpp:8944`.
- `VIXEN/application/main/source/VulkanGraphApplication.cpp:1099-1424` wires binding 5 for specialized glyph recipes and passes the flattened prefix base to both specialized emitters.
- `VIXEN/application/main/tests/test_lookdev_capture.cmake:113-165` labels the 9 glyph text-scene cases `native`, using the existing CTest split mechanism. `ctest -N -L native` lists all 9 glyph cases (plus the unrelated planet aggregate); non-text SDF cases remain unlabeled.

## Witnesses

- `cmake ...` setup with `UNDERTOW_ROOT=/home/liory/projects/undertow`: recovered the initial missing-environment failure; the focused targets reconfigured successfully.
- Queued build of `test_recipe_codegen_glsl`, `test_recipe_glsl_numerical_parity`, and the BodyOctreeSceneNode target: passed.
- `test_recipe_codegen_glsl`: **6/6 passed**, including `GlyphOutlineUsesRuntimeBufferAndRebasedIndex`.
- `test_uber_shader_splice`: **11/11 passed** (1 disabled), including `GlyphRowsUseFlattenedRuntimeBuffer`; the real BodyInstanceRayMarch shader compiled through ShaderBundleBuilder.
- `sdi_tool merge-variants shaders/sdi-variants.json --check`: passed; all merged headers current.
- Dozen GPU parity under `systemd-run --user --scope -p MemoryMax=16G`: `RecipeGlslNumericalParityTest.GlyphOutlineBucketMatchesCpu` **1/1 passed**. Measured cgroup peak for this capped glyph shader compile/run: **93,102,080 bytes (~89 MiB)**.
- Prior published Dozen text-scene evidence for the base records a controller kill at **31,756 MiB** (`/home/liory/codeman-cases/undertow/reports/lookdevleak-evidence/f750d6e9.rss`). This is the measured pre-transport text-scene failure; the current post-transport measurement is the capped glyph parity case, not a text-scene capture.

## STOPs

- **Application/native capture witness unavailable.** The current `VixenApp` build cannot reach the changed application translation units: after the documented `UNDERTOW_ROOT` repair and CMake cache repair, the queued target repeatedly stalls in `ui_shader_spirv_regen` (`timeout 90s` and `timeout 120s`, both exit 124). An earlier pre-cache attempt reached the pinned FidelityFX shader-blob prerequisite and stalled in `FidelityFX_SC.exe`. No current `vixen_lookdev_capture` binary is therefore valid evidence for this change.
- **Native byte/tolerance reference comparison not run.** Native Windows/RADV is not available in this lane, and the app target did not produce a current capture executable.
- **Full CPU CTest failing-set comparison not run.** The required full CPU set (base reports 22 known failures) cannot be compared against fresh binaries until the app build prerequisite is repaired; no before/after failure delta is claimed.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: VIXEN Text CMake requires ambient UNDERTOW_ROOT
- proposed: VIXEN SDI regeneration path depends on caller cwd
- proposed: VIXEN CMake reconfigure needs cache properties for provisioning switches
- proposed: VIXEN app object builds are coupled to a stalled FidelityFX shader generator
- proposed: VIXEN app builds repeatedly regenerate UI SPIR-V without a bounded cache path
