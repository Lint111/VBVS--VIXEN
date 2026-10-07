# SCC A-V1: headless VoxelDocument operations

## Baseline and navigation

- Starting tree: `7f4db6877bac7062502b1cb603b68464cf42da21` on `lane-sccheadless`.
- CodeGraph is unavailable in this worktree and its canonical VIXEN checkout; `tools/codegraph-vixen.sh` reports no index. The source map below uses `rg` and direct reads.
- Read the Inc1 design and plan in `VIXEN/Vixen-Docs/01-Architecture/Voxel-Authoring-App-Inc1-Design-2026-07.md` and `...Plan-2026-07.md`. A-V1 extracts the existing document operations; this lane does not define the A-K4 interface.

## Current code map

| Concern | Current implementation | Graphics/window boundary |
| --- | --- | --- |
| VDC1 read/write | Generated `ReadVoxelDocument` and `WriteVoxelDocument` in `VIXEN/libraries/SVO/include/Recipe/generated/VoxelDocument.g.h`. | None in the codec. |
| Document state and operations | `Vixen::Editor::EditorDocumentModel` in `VIXEN/application/editor/include/EditorDocumentModel.h`: `Load` (line 28) owns bytes backing the view; `View`, `SourcePath`, `LayerCount`, `LayerName`, and `OpName` (lines 45–66); `Flatten` to VRC1 (line 70); `FlattenToRecipeEntry` for preview (line 80); and `Save` with the current enabled mask (line 143). | None in this type. It includes generated data and SVO recipe headers only. |
| Layer edit, undo/redo, save dispatch | `EditorApplication::RegisterAppFlowHandlers` in `VIXEN/application/editor/source/EditorApplication.cpp` (line 217) routes `ToggleLayer` through AppFlow `ActionStack`, the layer provider and generated `applyToggle`; Undo/Redo use the same stack. Save calls `SaveDocument` (line 277), which delegates to the document model (line 510). | AppFlow state and the view are app concerns. `Update` receives UI selection events and polls GLFW keys (lines 617, 634, 646–676). |
| Load and layer view | `EditorApplication::LoadDocument` (line 182) loads the model, initializes AppFlow layer state, writes the Gaia-backed layer mask, populates the layer view, and registers handlers. `main.cpp` calls it before `Run` (line 66). | UI binding is connected through `EditorLayersViewBridge.cpp`; `BuildRenderGraph` wires the view to `UIRenderNode` (line 341). |
| Flattened preview | `FlattenToRecipeEntry` constructs the same recipe bytecode as the VRC1 path. `ApplyDocumentToScene` (line 416) registers and bakes that entry into a recipe pool, then sends the pool, body instance and residency request to the scene (lines 422–468). | The seam is `SetRecipePool` (line 438): prior work is headless CPU document/geometry work; scene upload and residency are graph operations. `BuildRenderGraph` (line 341) creates the standard window/scene/UI graph and camera setup. |
| Capture | `CaptureFrameToPng` (line 473) reads `compute_render_target` through the graph and Vulkan device. `PostTick` invokes it after rendering (line 730). | Direct Vulkan/RenderGraph readback; stays editor-only. |

The existing operation surface is `EditorDocumentModel`: `Load`, `View`, `SourcePath`, `LayerCount`, `LayerName`, `OpName`, `Flatten`, `FlattenToRecipeEntry`, and `Save`. Layer-mask toggling remains in the existing AppFlow owner; no new edit contract is introduced.

All graphics/window access stays outside that operation surface, in the editor shell:

- `EditorApplication` derives from `VulkanGraphApplication`; `BuildRenderGraph` creates the window, scene, UI nodes, and camera wiring, including the layer view bridge to `UIRenderNode`.
- `ApplyDocumentToScene` calls headless `FlattenToRecipeEntry`, then crosses into scene work: recipe registration/baking, `SetRecipePool`, `SetBodyInstances`, and `RequestBodyBrickResidency`.
- `CaptureFrameToPng`, called after render by `PostTick`, reads the render target through the Vulkan device/graph.
- `Update` forwards to the base application, handles UI selection, and polls window key state through GLFW.
- `main.cpp` owns application startup and invokes `LoadDocument` before the windowed run loop.

## Defect observed, unchanged

`EditorDocumentModel::Load` resizes/reuses `rawBytes_` before parsing (line 33). If a reload fails at `ReadVoxelDocument` (line 37), `view_` can still contain pointers into the previous buffer, now overwritten or reallocated. The public app method supports reloads; callers could observe invalid view data after a failed reload. This lane preserves behavior and does not fix it.

## Module extraction

Implementation and final line references will be filled after the move.

## Witness

Baseline configure command (completed successfully; queue log under
`/home/liory/.local/state/undertow/undertow-box-logs/1791384212-build-baseline-configure.log`):

```bash
VIXEN_FETCHCONTENT_CACHE="$PWD/VIXEN/.tmp/fetch" bash /home/liory/.local/bin/with-test-lock.sh \
  --agent sccheadless --resource build --label baseline-configure -- \
  cmake -S VIXEN -B VIXEN/build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DUSE_UNITY_BUILD=OFF
```

The fresh baseline build is queued as `sccheadless:baseline-build`. At report drafting, it has not started because the two active VIXEN builds hold all seven pair tokens; the queue reports it waiting for three tokens. This is queue admission, not a build result. Baseline build and target-test results will be recorded here after the queue admits them. Windows-native builds are unavailable in this environment because `vswhere.exe` and `cl.exe` are absent; the repository's WSL configure is the available documented route.

Existing regression targets found:

- VDC1 codec and flatten: `test_voxel_document_flatten` (`VoxelDocumentDecode.SampleTriLayerGoldenDecodesToExpectedValues`, and seven `VoxelDocumentFlatten` cases).
- Direct preview parity: `test_editor_document_model_preview` (`DirectEntryMatchesVrc1FlattenAndExportBytes`, `DirectEntryMatchesVrc1ForLayerOverride`). This currently belongs to the RenderGraph CMake suite despite being headless; its final suite and runtime environment will be recorded after extraction.
- AppFlow layer/render and capture: `test_appflow_editor_toggle_render`; `test_rendergraph_criticalnodes_windowedcapture` with `EditorToggleUndoCapture` cases; and `vixen_editor_capture_producer` plus its capture assertions.
- AppFlow unit regressions: the `test_appflow_golden`, `test_action_stack`, `test_flow_state_machine`, `test_binding_store`, `test_appflow_loader`, `test_appflow_data_seam`, `test_layer_controller`, `test_snapshot_undo`, `test_input_profile`, `test_keychord`, `test_binding_pattern`, `test_flow_return`, `test_return_dispatch`, and `test_appflow_blob` targets.

## Landing

SPT task expected to close at landing: `T-1103`. `T-1104` and `T-1105` remain downstream tasks.

## CONSOLIDATION ISSUES

Will list only proposals filed for incidental build/test tooling workarounds, or `None`.
