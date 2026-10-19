# instdemo: instancing demo Prepare failure and owed before/after capture

Branch `lane-instdemo`, off `origin/wave/authoring-convergence` `e4a18398`. Evidence: `/home/liory/codeman-cases/undertow/reports/instdemo-evidence/`.

## What changed

- `VIXEN/application/main/source/graph/BuildRenderGraph.cpp` (new `VulkanGraphApplication::SelectNativeUpscalerForModulelessGraph`, declared in `VulkanGraphApplication.h`): builds the one-candidate `native` set and runs the same `SelectUpscalerProviderForInitialization` the main graph uses. It is called from the four demo branches that `return` early (UI, instancing, AUTOSYNC, FANIN). The check in `PrepareWithUpscalerFallback` is unchanged and has no special case.
- `VIXEN/application/main/source/graph/BuildInstancingDemoGraph.cpp`: device and swapchain nodes are named `main_device` and `main_swapchain`, so the existing HUD capture path (`VIXEN_HUD_CAPTURE_FRAMES`, `VIXEN_HUD_CAPTURE_DIR`) works on the demo with no new capture code.
- `VIXEN/application/editor/CMakeLists.txt`: new `vixen_instancing_demo_capture_producer` test next to the HUD and editor producers. It writes to `${UT_QUEUE_CAPTURE_ROOT:-${CMAKE_BINARY_DIR}/runtime-captures}/instancing`.

## Root cause (MEASURED)

The demo graphs return from `BuildRenderGraph` before the candidate set and selected provider are built. The compiled graph then has a provider id that is not in the empty `upscalerCandidatesForCompile_`, and the check at `VulkanGraphApplication.cpp:334` throws. Reproduced: exit 255 with "Compiled upscaler provider is absent from its candidate set". After the fix `VIXEN_INSTANCING_DEMO=1` prepares and runs. The UI, AUTOSYNC and FANIN demos had the same defect and now also prepare and run 20 frames (exit 0).

## Capture before and after (MEASURED)

The before tree is `eb3085d9` (the textures1 merge parent) with only this fix cherry-picked, since the demo cannot run without it. The after tree is this branch. Frames 5, 45 and 75 are 500x500. Dozen (software) rendering, so correctness-only.

- Before and after sha256 are identical for all three frames (`87f18e09...`, `dd089a94...`, `e4c9abcb...`). A third run from the ctest producer in this build gave the same hashes. textures1 changes nothing in the instancing output.
- Image description (LOOKED at frames 5 and 75): a dark navy background with an 8x8 grid of instanced cubes, each a grey and white checkerboard with visible faces. Frame 5 shows an even, regular grid of cubes. Frame 75 shows the cubes rotated at different phases, so the rows look like a staggered brick pattern with some cubes overlapping their neighbours. The grid sits left of centre and low in the 500x500 frame, with empty margin on the right and top. Checkerboard textures are intact and not black, magenta or flat.

## Tests (MEASURED, Release, FSR3 OFF, Dozen ICD)

- `HudRenderCapture.*`: 5/5 pass.
- `EditorToggleUndoCapture.*`: 9/9 pass.
- `vixen_editor_capture_producer`, `vixen_hud_capture_producer`, `vixen_instancing_demo_capture_producer`: 3/3 pass.
- Textures (`TextureDecode`, `TextureCacher`, `TextureLoaderNodeBinding`): 5/5 pass.
- VulkanResources: 17 tests, 16 pass, 1 skip (same as `vixwit.md`).
- RenderGraph (`-L RenderGraph -L cpu`, 1258 tests): 1248 pass, 10 fail. The `vixwit.md` run reported 42/42 on a smaller core group, so these are not the same set.
  - The 10 failures are `UiCompositionHostTest.*` (6, segfault), `ViewEditorLayersGolden.GeneratedBindFunctionCompilesAndBinds` (`Rml::Initialise()` returns false, "No font engine interface set!"), `ViewEditorLayersReconcile.ExternalGaiaWriteReconcilesIntoBoundView` and `BlobView.*` (2), all segfaults.
  - INFERRED unrelated: they are RmlUi tests and this branch changes only four files under `application/main` and `application/editor`, none linked into those tests. I did not build the base to confirm they fail there too.
- Full build with `-k 0`: three failures, none in touched files (see consolidation issues).
- Hard-rule greps on added lines (EntityId, `KindRef<All>`, explicit `Id`, attributes): no hits. No new warnings in touched files.

## SPT DISPOSITION

- 314c85117dcd (fix the instancing demo's Prepare failure on the wave): CLOSE. The demo prepares and captures, and the check is unchanged.
- textures1's owed instancing before/after witness: CLOSE (identical bytes, evidence path above).

## CONSOLIDATION ISSUES

- `no_new_mutex_check` fails on the base: "stale allowlist row (file gone or mutex-free): VIXEN/libraries/Textures/src/TextureLoader.cpp" (textures1 removed the mutex or file; the row needs `--fix`). It also warns of three unclassified owning declarations (`TaskExecutor.h`, `TaskExecutor.cpp`, `PhaseTiming.h`). This breaks a full `cmake --build` unless `-k 0` is used.
- `libraries/CashSystem/tests/test_registration_api` fails to link on this tree: undefined vtable for `CashSystem::TextureCacher`. Not caused by this lane; likely textures1 left the test target without the TextureCacher source.
- `frame_targets_schema_check` fails with "unsupported schema field fluent call 'Rate'" at `SubstrateKinds.cs:173`. My configure used `UNDERTOW_ROOT=.../undertow/.claude-worktrees/pair-hlsl`, which may be older than the schema; INFERRED environment mismatch, not verified.
- RmlUi has no font engine in a fresh fetched build here ("No font engine interface set!"), which breaks 10 RenderGraph CPU tests. Probably a FreeType or RmlUi configure issue in this environment; not verified on the base.
- The deployed queue does not set `UT_QUEUE_CAPTURE_ROOT` (provisioning is on the undeployed `lane-queuegpu`). This lane's producer honours it and falls back to `build/wsl/runtime-captures/instancing`.
- The stb pin `2c980bb5` is not fetchable fresh; configure needed `-DFETCHCONTENT_SOURCE_DIR_STB=<cached stb>`.
- The instancing grid sits off-centre in the 500x500 capture. That may be intended camera framing or an aspect bug in the demo; not investigated.
