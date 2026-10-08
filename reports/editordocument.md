# R433 editor document lane report

## LANDABLE NOW

- Kernel: `92cfa58c3c7078aac01895d7ff1191c522cfe6c4` (landed main containing `e807763f4c856268c4b9b0a37c868c1383d60005`, named recipe parameter metadata).
- VIXEN: `3b6c73c569be869ea55c246f65a8c46e4d5cac7d` (editor implementation plus tracked landed-kernel pin). Run 2 merges wave `846ab1a999a542f663ee08764fe69b3de2beade3` in `5ba8a169`; final report/proposal commit follows this implementation tip.

## Run 2 — merged wave and landed kernel

**Merge and pin.** Run 2 started from `5cf28e6ff89a733059af096c652206d68f300b8b`, merged `origin/wave/authoring-convergence` at `846ab1a999a542f663ee08764fe69b3de2beade3`, and set `_vixen_tracked_yeroket_kernel_sha` in `VIXEN/codegen/CMakeLists.txt` to current kernel main `92cfa58c3c7078aac01895d7ff1191c522cfe6c4`. The CMake cache override is empty. The merge conflict in `EditorApplication.cpp` was resolved by retaining the editor's procedural provider, recipe id, and six snapshot parameters while initializing the R424 affine transform stream with identity translation/scale. This preserves editor parameter data in the cold material record while R424 supplies the instance transform through binding 47.

**Configure recovery and fresh build.** The queued configure under `--resource build` waited on a 17 GB class-max reservation and was cancelled before CMake started (exit 130; no configure result). Re-running the configure-only command through the live queue as `--resource light` passed and printed `[codegen] using tracked Yeroket kernel pin: 92cfa58c3c7078aac01895d7ff1191c522cfe6c4` (`/home/liory/.local/state/undertow/undertow-box-logs/1791460023-light-run2-configure-light.log`). CMake configure emitted a nonfatal Vulkan SDK `glslang` directory diagnostic but returned 0. A fresh queued `cmake --build build --parallel 4` passed; all 22 VIXEN `*_check` targets passed. The requested kernel CodegenTool restore and Release build also passed with 0 warnings/errors, and its direct `RecipeParams --check` against VIXEN's schema/artifact paths passed. A final queued no-op rebuild passed with all targets already built and no compile or link steps.

**Engine suites.** The merged-tree RenderGraph suite passed at `-j4`: 1,342/1,342, 11 skipped, with no DZN device loss, so a serial retry was unnecessary (`1791460591-test-run2-rendergraph-j4.log`). SVO reproduced only the known `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` opcode-94 failure, `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94` (T-1449); 752 tests passed, 8 were skipped, and one was disabled (`1791460824-test-run2-svo.log`).

The requested 11-case capture selection first failed in the three `HeadlessStarfieldGraph` cases with `VK_ERROR_INCOMPATIBLE_DRIVER`: those CTest cases omitted the configured Vulkan runtime library path when the caller's `LD_LIBRARY_PATH` was unset (`1791460950-test-run2-captures-11.log`). Re-running the same selection with the configured Mesa/windowing/Vulkan loader directories in `LD_LIBRARY_PATH`, while keeping `DISPLAY`, `WAYLAND_DISPLAY`, and Vulkan selector variables unset, passed 11/11 (`1791461024-test-run2-captures-11-libpath.log`). This was recovered and filed as a consolidation proposal.

**Editor witnesses and capture comparison.** The scripted editor capture edited the named parameter from `0.6` through `1.225`, toggled layer 2, undid and redid the layer edit, saved, and reopened. At each parameter revision, the log shows the CPU bake snapshot and virtual preview arrays match; reopen reports `programMatch=1 parametersMatch=1 maskMatch=1`. The focused editor selection passed 11/11, including byte-exact undo/redo render checks and `EditorDocumentFraming.FramesThreeDifferentlyBoundedDocumentsAndParameterizedRecipe` (`1791461170-test-run2-editor-document-tests.log`). The same-cel before/after pair (frame 5 at the default parameter and frame 25 after editing) differs in 2,148 pixels, with maximum channel delta 74 and bounds x=104–395, y=222–395.

The untouched wave tree at `846ab1a9` has tree hash `1709f17e99af44699ecd9d75d109a0492ff93179`, identical to the R424 Run 3 tree `a9d7c0febe5ad02a7de329f081e63cc10b8dc789`; the R424 report records all 26 shared captures byte-identical against untouched `92804a8f` captures. In the direct comparison against the untouched-wave baseline, offscreen and native HUD frames 5, 45, and 75 remained byte-identical. Existing editor frames 5, 45, 75, and 105 changed as expected; editor frames 25 and 115 are new script checkpoints. No other existing capture changes were observed.

**Run 2 queue logs.** Configure: `1791460023-light-run2-configure-light.log`; build: `1791460084-build-run2-full-build1.log`; RenderGraph: `1791460591-test-run2-rendergraph-j4.log`; SVO: `1791460824-test-run2-svo.log`; capture recovery: `1791461024-test-run2-captures-11-libpath.log`; focused editor tests: `1791461170-test-run2-editor-document-tests.log`; CodegenTool restore/build and check: `1791461396-build-run2-codegentool-restore-build.log`, `1791461410-light-run2-codegentool-recipeparams-check.log`; no-op rebuild: `1791461426-build-run2-noop-rebuild.log`. The initial capture failure is `1791460950-test-run2-captures-11.log`. The configure queue admission interruption had no CMake log because CMake never started.

**Run 2 disposition.** No STOP remains. The only suite failure is the previously observed T-1449 opcode-94 SVO case; the capture environment issue recovered with configured loader paths. The required merged-tree build, checks, captures, editor witnesses, and no-op rebuild passed.

## What changed

The headless `EditorDocumentModel` remains the only mutation path. It now exposes validated insert, delete, move, program-field, layer-enable, and parameter operations; UI AppFlow handlers route forward and inverse operations through those methods and the existing undo stack (`VIXEN/libraries/VoxelDocument/include/EditorDocumentModel.h:81-116`, `VIXEN/libraries/VoxelDocument/src/EditorDocumentModel.cpp:329-475`, `VIXEN/application/editor/source/EditorApplication.cpp:318-467`). Save/reopen checks the canonical flattened bytecode, parameter values, and enabled-layer mask (`EditorApplication.cpp:769-839`).

Named float parameters use the existing kernel VoxelDocument authoring shape: slot, type, name, unit, default, minimum, maximum, and persisted effective value (`Packages/com.utility.graph-framework/Runtime/VM/VoxelDocument.cs:23-38,59-67`). The VDC v2 C++ output is reflected from the canonical C# layout by the source generator (`Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/VoxelDocumentEmitter.cs:77-101,154-180`); no editor-side schema mirror was added. The editor exposes the metadata as controls. The six-float runtime carrier remains unchanged, and no per-recipe GPU parameter blocks were added.

`DocumentBakeSnapshot` records the document revision and six effective values (`EditorDocumentModel.h:50-53,79`). The editor sends that snapshot to the generic registry bake and copies the same values to the virtual preview instance (`EditorApplication.cpp:610-700`; `VIXEN/libraries/SVO/include/Recipe/RecipeRegistry.h:33-36`, `RecipeBaker.h:57-94`). Runtime value changes therefore update geometry without shader regeneration; rule-program changes continue to use the shared shader recompile path.

The editor camera now fits a conservative recipe sphere derived from document bounds, bake center, and resolution (`VIXEN/libraries/VoxelDocument/include/EditorDocumentFraming.h:15-41`, `EditorApplication.cpp:702-721`). This replaces the fixed golden-document framing constants.

The scripted UI witness edits the named parameter four times, edits a rule layer by toggling layer 2, then undoes, redoes, saves, and reopens. The sample declares `bulgeRadius` in voxels with default `0.6`, range `[0.25, 1.5]`, and a `ReadParameter` instruction (`Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Tests/VoxelDocumentGoldenTests.cs:129-162`).

## Witness

**Baseline and recovery.** VIXEN base was `92804a8f67c48653514c042a8f7b87b70bbb642b`; kernel base was `8d68e9839ac3af02b937b2b419c27f829e90cccc`. Before semantic edits, the queued baseline `cmake --build build -j4` exited 2 because the parent directory `build/application/main/vixen_stage_assets` was absent when the asset stamp was touched (`/home/liory/.local/state/undertow/undertow-box-logs/1791413768-build-baseline-all-build.log`). Creating that build-output directory and rerunning the same queued build passed (`1791413908-build-baseline-all-build-retry1.log`). The baseline full RenderGraph suite passed 1,341/1,341. Baseline SVO had the known `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` failure, `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94` (T-1449; `1791415527-test-baseline-svo.log`).

**Build and generation.** A fresh VIXEN configure pinned to kernel SHA `e807763f4c856268c4b9b0a37c868c1383d60005`, the full queued VIXEN build, and the final no-op build passed. All 22 VIXEN `*_check` targets passed. Kernel SourceGenerator tests passed 974/974. Kernel CodegenTool tests passed 2,081 with 2 skipped after supplying `UNDERTOW_ROOT=/home/liory/projects/undertow`; the run without that consumer root had failed fixture tests and was recovered with the documented root. A queued CodegenTool `dotnet restore` and Release build passed (0 warnings, 0 errors); a queued `dotnet run --no-restore --no-build` check of the VIXEN `EditorLayers.g.h` artifact passed.

**Suites.** The current full RenderGraph suite passed 1,341/1,341 at `-j1` with the documented DZN ICD (`1791422974-test-rendergraph-final-j1-dzn.log`); all 11 capture producer/assertion tests passed. The requested `-j4` run with that ICD did not complete: after ten minutes, 1,340/1,341 results had arrived and the queue wrapper cancelled the stalled run (exit 76 / payload 141; `1791422286-test-rendergraph-final-j4-dzn.log`). Concurrent DZN workloads logged device removal in B1, editor capture, and other GPU tests. This diagnostic is unclassified against the exact base at `-j4`; the full serial suite is green, and a consolidation proposal records the host GPU concurrency issue. Current SVO ran 753 tests: 752 passed and the only failure is the same T-1449 opcode-94 diagnostic seen on the base; 8 were skipped and 1 disabled. All changed VDC sample tests passed. The headless model/framing group passed 8/8, including framing three differently bounded documents. Focused offscreen editor capture tests passed 8/8. The native WSL capture witness passed 1/1 with `DISPLAY`, `WAYLAND_DISPLAY`, and Vulkan environment overrides unset, producing seven native captures.

**UI, canonical state, and pixels.** The editor script log reports equal CPU bake and preview parameter arrays at every revision, and save/reopen reports `programMatch=1 parametersMatch=1 maskMatch=1`. Undo restores the parameter-edited render byte-for-byte; redo restores the layer-edited render byte-for-byte. With the default bi-modal cel shading active (legacy Lambert/GGX override disabled), frame 5 to frame 25 changes 2,148 pixels (maximum channel delta 74; bounds x=104–395, y=222–395), demonstrating visible geometry change from the parameter control. Captures are under `build/runtime-captures/editor/`.

Compared with the untouched-wave captures, the existing editor frames 5, 45, 75, and 105 changed as expected from bounds-based framing and the parameterized document; frames 25 and 115 are new script checkpoints. Existing HUD frames 5, 45, and 75 remained byte-identical. The same four existing editor frames changed in the native capture comparison; native HUD frames remained byte-identical. No other existing captures changed.

## Hazards 3–5

- Hazard 3: the editor still performs a synchronous whole-document bake on accepted edits. This lane adds no incremental or background bake job and does not worsen the existing responsiveness limit.
- Hazard 4: fixed framing is replaced by conservative bounds-based auto-fit and exercised with three differently bounded documents.
- Hazard 5: nested-recipe callee registry wiring remains untouched; lane `rva2` owns it. This lane does not claim nested recipes are newly supported.

## Shared files touched

The shared backend changes are in `VIXEN/libraries/SVO/include/Recipe/RecipeBaker.h`, `RecipeBounds.h`, `RecipeRegistry.h`, and generated `VoxelDocument.g.h`. The shared demo host files `VIXEN/application/main/include/VulkanGraphApplication.h` and `VIXEN/application/main/source/VulkanGraphApplication.cpp` add an idle wait and device-loss handling before shader-library recompilation after a rule-program change. Editor view/AppFlow schemas, generated artifacts, UI assets, and RenderGraph tests also changed. The body-instance record was not touched. The editor remains its own `vixen_editor` app; its preview wiring is `EditorApplication::ApplyDocumentToScene` (`VIXEN/application/editor/source/EditorApplication.cpp:610`); no KFR files or editing tools were added.

## SPT DISPOSITION

- T-1103 is already closed; this lane reuses and extends its headless operations.
- T-1108 remains open for the broader proof, dirty-overlay, and editor-control scope; numeric controls in this lane do not close it.
- P705 remains for the later per-recipe parameter-block work; this lane preserves the six-float carrier and adds no per-recipe blocks.
- T-1449 remains the reproduced opcode-94 SVO failure; the current diagnostic matches the baseline.
- No existing SPT task was closed by this lane. New issue proposals are listed below as plain text titles.

## CONSOLIDATION ISSUES

- proposed: Create vixen_stage_assets stamp directory before touching stamp
- proposed: Provide UNDERTOW_ROOT to consumer-dependent CodegenTool tests
- proposed: Resolve the root build directory for nested VIXEN regen targets
- proposed: Expose a canonical VDC artifact regeneration target
- proposed: Serialize DZN-backed CTest workloads under parallel runs
- proposed: Give VIXEN CMake configure a light queue reservation
- proposed: Set Vulkan runtime paths on headless starfield CTest captures
