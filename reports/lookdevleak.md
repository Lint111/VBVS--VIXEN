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

## Run 2 witnesses

- **App build recovered.** `VIXEN/libraries/RenderGraph/CMakeLists.txt:464-475` changes `ui_shader_spirv_regen` from a phony custom target to an `OUTPUT`-tracked custom command, so an unchanged generated header is not regenerated on every app build. `VIXEN/application/main/cmake/FidelityFXFsr3.cmake:9-15` removes the queue-specific ambient `PATH` from the custom-command identity; the SDK tool directory already contains both `FidelityFX_SC.exe` and `glslangValidator.exe`.
- The serialized queued FidelityFX witness processed **40/40** blob edges, with **64 permutations per edge**, and exited 0 (`ffx-serial-stable.log`). The final queued `vixen_lookdev_capture` build exited 0 with `ninja: no work to do` (`final-app-build.log`). A parallel `-j4` retry remained stuck in WSL PE compiler children; the documented queue watchdog workaround was an increased idle grace and serialized `-j1`, not a longer app timeout.
- **Dozen text-scene measurement:** the capped `Application.LookdevCapture.GlyphSdf.latin-carved` / `RendersGlyphSdfLocalizedSign` run reached `memory.peak=17,179,869,184` bytes (16 GiB) and `memory.events max=6,746`, with no OOM kill. It was terminated after 6:42 while blocked in the driver compile call; no capture/reference image was produced. Compared with the base controller kill at **31,756 MiB**, this is a capped upper bound, not a flat-under-cap pass. The nine glyph text-scene cases therefore remain `native`-excluded on Dozen; no re-enable was made.
- **Measured vs inferred:** generated GLSL still uses the runtime `std430` `glyphOutlineInstances[]` SSBO and indexed `SdfCore_GlyphOutline` calls; the recipe and parity witnesses prove transport/indexing. The remaining Dozen cap-bound compile is inferred to be in the driver’s handling of the 64-segment `SdfCore_GlyphOutline` struct/loop shape, not compile-time outline literals. A further generator design is outside this run’s authorized smallest change.
- **CPU CTest:** the first scoped run exposed 29 CPU tests, of which 8 were `Not Run` because their binaries were absent. After a queued build of those eight targets, the same scope passed **29/29** (one disabled); this is a clean scoped result, not a full-suite comparison against the base’s 22 known failures.
- **Visual evidence:** the existing Dozen localized-sign PNG was inspected against the visual-bible concerns for glyph legibility, contrast, orientation, and reference provenance (T03/T04/T07/T10/T13). It is not a published reference and no new capture was produced, so no byte/tolerance visual claim is made.

## SPT DISPOSITION

- **R631 — PARTIAL.** R631(1) is covered by the existing `native` CTest labels and the runtime-buffer implementation; the app build is now landable and GPU parity remains green. R631(2) is not green on Dozen because the text-scene compile reaches the 16 GiB cap. R631(3) is intentionally not applied. Native Windows/RADV reference captures remain unavailable in this lane.

## STOPs

- **Dozen glyph text-scene remains cap-bound.** The runtime SSBO/parity path is correct, but the localized-sign capture reaches the enforced 16 GiB cap before producing a frame. R631(2)/(3) need a further generator change or owner decision about the shader’s 64-segment evaluation shape.
- **Native byte/tolerance reference comparison not run.** Native Windows/RADV is not available in this lane’s assigned worktree/launcher, so no native capture or reference comparison can be claimed.
- **Full CPU 22-failure delta not established.** The fresh app-focused tree exposed only 29 CPU tests; all 29 passed after building their missing binaries. The complete base CPU set was not built or rerun, so no full-suite before/after delta is claimed.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: VIXEN FidelityFX custom commands hash the ambient PATH
- carried forward: VIXEN Text CMake requires ambient UNDERTOW_ROOT
- carried forward: VIXEN SDI regeneration path depends on caller cwd
- carried forward: VIXEN CMake reconfigure needs cache properties for provisioning switches
- carried forward: VIXEN app object builds are coupled to a stalled FidelityFX shader generator
- carried forward: VIXEN app builds repeatedly regenerate UI SPIR-V without a bounded cache path
