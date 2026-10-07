# vixmerge2 — combined engine slice witness

## Merge set

Merged the four finished branches in the requested order, each with `--no-ff`; all merges were textually clean and needed no conflict resolution:

| Branch | Tip | Merge commit | Scope |
|---|---|---|---|
| `lane-pickinst` | `46ef1e10` | `047c8d89` | T-1126, instance index in channel G of the RG32 pick image |
| `lane-inputaxes` | `a591e33a` | `0e9127d3` | T-1164, per-frame keyboard axes and mouse deltas in `InputState` |
| `lane-starfield` | `0f53e3f0` | `9182cc22` | T-1138, opt-in procedural starfield, disabled by default |
| `lane-starlight` | `87c845e7` | `60f70063` | T-1139, emissive instances populate the existing four point-light slots |

`lane-instorient` was held and not merged, per R424. T-1170 `minebeam` remains for a later run.

## Combined pick and lighting test

Added `ShadowCorrectnessTest.EmissivePointLightPickReturnsTheLitBodyInstance` in `VIXEN/libraries/RenderGraph/tests/Nodes/test_shadow_correctness.cpp`. It reuses the first star, planet, and camera setup from starlight’s `EmissivePointLightFacesThreeBodiesTowardTheStar`, dispatches the same production march → visibility → shade chain, and reads picks from that live RG32 image through `VoxelSelectionProviderNode`. It searches the small 8×8 output for one pixel whose RGB luma is above 60 and whose selection candidate reports body instance 1. The targeted case passed 1/1; it also passed in the final full RenderGraph run.

The first run exposed incomplete test-side `VulkanDevice` setup: the shadow fixture set its device but not its queue or queue-family properties, so provider readback had no usable queue. The fixture now initializes those fields using the established pickinst fixture pattern. No product shader or provider behavior changed.

Salvage reuse: I read `/home/liory/scripts/codex-runs/vixmerge-salvage.diff` and applied only its `tools/run-vixen-windowed-captures.sh` hunk. The runner now derives the provisioned X11/Vulkan runtime library directories from `CMakeCache.txt`, because direct app launches do not inherit CTest’s `LD_LIBRARY_PATH`. I did not apply its unrelated recipe-render test edit; this lane’s combined test uses the existing starlight scene and pickinst provider path.

## Build and checks

The clean baseline was `7f4db6877bac7062502b1cb603b68464cf42da21`. A detached worktree at that exact SHA was freshly configured and built for the baseline SVO and capture checks, then removed after verification. The combined tree was freshly configured with the documented `vixen-wsl` preset. A second queued configure enabled `-DVIXEN_FAIL_SCENARIOS=ON`, because the default preset leaves that option off and does not build/register `test_input_axes`; the final RenderGraph run therefore included the merged input-axis tests.

The combined queued full build passed after two recoveries. One queued invocation was launched from the `VIXEN/` subdirectory and exited 1 because `VIXEN/build/wsl` did not exist; rerunning from the worktree root found the actual `build/wsl` tree. That build then caught an lvalue/rvalue error in the new test’s `Resource::SetHandle<T>` calls; the test now uses the existing by-value `SetHandleVal` pattern, and the full build graph completed successfully. The final targeted rebuild of the combined test also passed.

The full build executed all 22 requested checks successfully: 21 generated/schema checks plus the separate no-new-mutex gate.

1. `octreeconfig_check`
2. `recipeparams_check`
3. `recipe_simd_check`
4. `sdf_core_kernels_check`
5. `recipe_opcode_mirror_check`
6. `lightingconfig_check`
7. `shadowconfig_check`
8. `accumulationconfig_check`
9. `prevcameraconfig_check`
10. `reservoirconfig_check`
11. `probegridconfig_check`
12. `lighttreebuffer_check`
13. `reservoirrecord_check`
14. `view_hud_check`
15. `view_editor_layers_check`
16. `view_hud_markup_check`
17. `view_hud_blob_check`
18. `view_hud_writer_check`
19. `appflow_check`
20. `view_noun_enum_check`
21. `callables_check`
22. `no_new_mutex_check`

The mutex gate passed after scanning 820 files (81 mutex-syntax files, 47 owning declarations); it reported two existing unclassified inventory rows in `KernelDispatch/TaskExecutor.h` and `TaskExecutor.cpp`. An explicit queued invocation of the 22-target set found it up to date. The final queued no-op rebuild exited 0 with `ninja: no work to do` (0 units).

## Test and capture results

- **RenderGraph CTest, combined tree:** 1,360 tests, 0 failures, 14 skipped. This includes the new combined pick test, the pickinst GPU tests, starlight tests, both capture fixture producers, and input-axis coverage.
- **SVO CTest, clean base and combined tree:** the queued `ctest --test-dir <tree>/build/wsl -L SVO --output-on-failure` selection exited 8 on each tree. Each discovered 752 tests; 751 were runnable, with 742 passed, 1 failed, 8 skipped, and 1 disabled. The sole failure on both trees was `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`, reporting `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94` (T-1449). The clean-base log is `/home/liory/.local/state/undertow/undertow-box-logs/1791384024-test-vixmerge2-base-svo-ctest.log`; the combined-tree log is `/home/liory/.local/state/undertow/undertow-box-logs/1791386785-test-vixmerge2-combined-svo-ctest.log`. The matching diagnostic on clean `7f4db6877bac7062502b1cb603b68464cf42da21` classifies it as a pre-existing finding, not a merged-branch regression.
- **Held `instorient` GLSL aborts:** its report records two GLSL corpus aborts at `SdfRecipeCodegenGlsl.h:95`. Neither occurred on the clean base nor on the combined tree. They are specific to the held, unmerged `lane-instorient` tree; they are not base failures and do not implicate any of these four branches.
- **Capture CTest:** the final 11-case selection passed 11/11 with caller `DISPLAY` and all `VK_*` variables unset. The selection was seven editor/HUD assertions, three `HeadlessStarfieldGraph` checks, and `vixen_wsl_capture_witness`. The two producer fixture tests had already passed in the full RenderGraph run and were excluded from this separate count. The three headless app checks initially failed to create a Vulkan instance when `LD_LIBRARY_PATH` was also unset; supplying the configured Mesa/Vulkan/X11 runtime directories made the same selection pass 11/11. The native witness itself passed 1/1.
- **Direct native runner:** `tools/run-vixen-windowed-captures.sh` passed with caller `DISPLAY`, `WAYLAND_DISPLAY`, all Vulkan variables, `VULKAN_SDK`, and `LD_LIBRARY_PATH` unset. The runner selected `DISPLAY=:0`, derived its runtime library path from CMake’s cache, and produced seven editor/HUD PNGs.
- **Pixel comparison:** `tools/compare-capture-pixels.py --require-byte-identical .tmp/vixmerge2-base-captures .tmp/vixmerge2-captures` passed. All seven 500×500 editor/HUD PNG pairs were byte-identical, with 0 changed pixels and maximum channel delta 0. This confirms the starfield remains off by default in existing captures.

The tracked runner scripts on `lane-starfield` and `lane-pickinst` were identical. The runtime difference was that CTest supplies configured library paths to its tests, while the standalone runner directly launches the applications and previously did not reconstruct that environment. The salvage runner hunk fixes that tooling gap; no product change was made. The separate missing runtime path on headless app CTest entries was handled through the queued test environment and filed as a consolidation proposal.

CodeGraph was queried before code search, but this VIXEN worktree has no index; no index was created.

## KFR follow-up

- **Pickinst:** `KernelFederationRenderer/app/src/session_renderer.cpp:28`, `SessionRenderer::BuildRenderGraph()`, is the graph integration site; the existing UI selection provider is wired at lines 47–57.
- **Input axes:** the current KFR read site is `session_renderer.cpp:107`, where it obtains `input->GetInputState()`. T-1165/T-1166 simulation ingress can read `InputState::GetAxis(...)` and `mouseDelta`; `CameraNode` already consumes both.
- **Starfield:** its lane report records no additional KFR follow-up site.
- **Starlight:** `SessionRenderer::BuildRenderGraph()` currently builds graph and UI wiring; it has no lighting-config replacement site.
- **Held instorient context:** its report points to `BuildRenderGraph()` around line 28 for instance upload/orientation and `PreTick()` around line 86 for pose updates. R424 supersedes that lane’s quaternion proposal with a 3×4 affine local-to-world transform plus its inverse, stored in buffers split by update rate. That transform work follows in its own lane.

## STOPs

None. The only combined SVO red is the opcode-94 T-1449 failure reproduced on the exact clean base. The held instorient GLSL aborts were not reproduced on either the base or combined tree. No failure was attributable to the four merged branches after recovery.

## CONSOLIDATION ISSUES

- proposed: Windowed capture runner omits provisioned X11 runtime libraries
- proposed: Default VIXEN preset omits input-axis fail-scenario test
- proposed: Headless capture CTest cases omit provisioned runtime libraries
- proposed: Share complete VulkanDevice setup across GPU test fixtures
