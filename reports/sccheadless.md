# SCC A-V1: headless VoxelDocument operations

## Baseline and navigation

- Started on `lane-sccheadless` at `7f4db6877bac7062502b1cb603b68464cf42da21`. Run 1's report/proposal checkpoint is commit `03a51e9453eca8e8df9693999f277a35f0b792ba`.
- Merged `origin/wave/authoring-convergence` at `d83b057eee4a71d52e5064b0ff5f5e8098af1d05`; the resulting merge commit is `0b59c9623fe8096d5567eedd20591f750c0361c9`.
- CodeGraph is unavailable: `tools/codegraph-vixen.sh` reports no VIXEN index in this worktree or canonical checkout. Read the Inc1 design and plan under `VIXEN/Vixen-Docs/01-Architecture/Voxel-Authoring-App-Inc1-*`.

## Current source map

| Concern | Source and boundary |
| --- | --- |
| VDC1 codec | Generated `ReadVoxelDocument` / `WriteVoxelDocument` remain in `VIXEN/libraries/SVO/include/Recipe/generated/VoxelDocument.g.h`. |
| Document operations | `Vixen::Editor::EditorDocumentModel` moved from `VIXEN/application/editor/include/EditorDocumentModel.h:24` to `VIXEN/libraries/VoxelDocument/include/EditorDocumentModel.h:20`; inline operations became definitions in `VIXEN/libraries/VoxelDocument/src/EditorDocumentModel.cpp` (`Load`: 12, `Flatten`: 60, `FlattenToRecipeEntry`: 68, `Save`: 128). The public names and signatures are unchanged. |
| Preview parity | The two cases moved from `VIXEN/libraries/RenderGraph/tests/Nodes/test_editor_document_model_preview.cpp:30` and `:64` to `VIXEN/libraries/VoxelDocument/tests/test_editor_document_model_preview.cpp` at the same lines. The old RenderGraph registration at `test_critical_nodes.cmake:481-497` is replaced by `VoxelDocument/tests/CMakeLists.txt:1-13` and ordinary `gtest_discover_tests`. |
| Editor shell | `EditorApplication::LoadDocument` at `VIXEN/application/editor/source/EditorApplication.cpp:182`; AppFlow handlers at `:217`; scene/recipe handoff at `:416`; capture stays in `CaptureFrameToPng` at `:473`; save delegates at `:510`; window/input handling stays in `Update` at `:617`. |

The public class name, namespace, method surface, and include spelling remain unchanged. CMake now exports the header from the `VoxelDocument` library, which links `SVO` and is linked by `vixen_editor` (`VIXEN/libraries/CMakeLists.txt:55`, `VIXEN/application/editor/CMakeLists.txt:32`). The preview test no longer inherits RenderGraph's Vulkan loader environment. Its binary has no Vulkan, GLFW, or X11 dynamic dependency.

Graphics work remains in `EditorApplication`: it registers/bakes the flattened entry and transfers it to the render scene; render graph construction, Vulkan capture, and GLFW input stay in the app shell.

## Defect observed, unchanged

`EditorDocumentModel::Load` resizes/reuses `rawBytes_` before parsing (`EditorDocumentModel.cpp:12–21`). If a reload fails during `ReadVoxelDocument`, `view_` may still refer to the prior buffer after it was overwritten or reallocated. This extraction preserves the existing behavior.

## Module extraction

- Added the `VoxelDocument` static library with the existing document model implementation and public include directory. Its public target dependency is `SVO`; it does not link RenderGraph.
- Moved the two direct-preview parity tests into `VoxelDocument/tests`; the assertions and expected VRC1 bytes are unchanged.
- Linked `vixen_editor` to `VoxelDocument` and removed the old RenderGraph test registration.

## Witness

- Merged-base configure and full direct build passed; both passed again after extraction. Commands: `nice -n 10 env VIXEN_FETCHCONTENT_CACHE=$PWD/VIXEN/.tmp/fetch cmake -S VIXEN -B VIXEN/build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DUSE_UNITY_BUILD=OFF` and `nice -n 10 cmake --build VIXEN/build --parallel 3`. Configure emitted a glslang-path diagnostic but exited 0; no workaround was required. Final no-op rebuild passed in 2 seconds with no compile, link, or generation work.
- CMake's 22 `*_check` targets passed on the merged base and after the extraction. The final full build also passed `octreeconfig_check`.
- Direct CodegenTool restore, Release build, and OctreeConfig `--check` passed against the pinned kernel snapshot. With `$YK_TOOL=/home/liory/.cache/vixen/kernel-codegen/sources/yeroket-a2b2d6b9e1e93b37b1adc0d7dd99e1c63bf96bad/Packages/com.yeroket.utility.kernel-framework/CodegenTool~`, the commands were `dotnet restore $YK_TOOL/CodegenTool.csproj --artifacts-path $PWD/VIXEN/.tmp/direct-codegen-artifacts`, `dotnet build $YK_TOOL/CodegenTool.csproj -c Release --no-restore --artifacts-path $PWD/VIXEN/.tmp/direct-codegen-artifacts -m:1 -nodeReuse:false -p:InvariantGlobalization=true`, and `dotnet run --project $YK_TOOL/CodegenTool.csproj -c Release --no-build --no-restore --artifacts-path $PWD/VIXEN/.tmp/direct-codegen-artifacts -- --schema $PWD/VIXEN/codegen/config-schemas --struct OctreeConfig --out-cpp $PWD/VIXEN/libraries/SVO/include/Generated/OctreeConfig.g.h --out-glsl $PWD/VIXEN/shaders/Generated/OctreeConfig.glsl --check`.
- The focused 75-case regression selection passed 75/75 on the merged baseline and after the change. This includes VDC1 decode/flatten, AppFlow/editor render, capture assertions/producers, and the preview parity cases.
- The moved preview executable passed 2/2 with `DISPLAY`, Wayland, Vulkan loader/layer, SDK, and library-path variables unset. `ldd` showed no Vulkan, GLFW, or X11 dependency.
- The native WSL capture witness passed. All 7 PNGs (4 editor and 3 HUD) matched the pre-change capture byte-for-byte; pixel diff was 0. Caller display/Vulkan variables were unset; CTest provided the native witness environment.

RenderGraph full suite: `ctest --test-dir VIXEN/build --output-on-failure --parallel 3 -L RenderGraph` passed 1,339/1,339; five tests were skipped by their declared conditions.

SVO full suite: `ctest --test-dir VIXEN/build --output-on-failure --parallel 3 -L SVO` exited 8 with three failures:

- `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`: `M4d_Output_IsPassthrough` reports `recipe gradient capability mismatch: 94` (the known T-1449 failure).
- `RecipeGlslNumericalParityTest.GlslMatchesCpuEvalAcrossCorpus` and `RecipeGlslCompiles.EmittedGlslCompilesForEveryCorpusProgram`: both abort at `SdfRecipeCodegenGlsl.h:95` because `paramMask` is nonzero on an opcode other than `ReadParam` / `ReadParamFloat3`.

All three failures were reproduced on merged-base source state after temporarily removing only this lane's uncommitted extraction, reconfiguring, and rebuilding the two SVO test targets. The filtered command `ctest --test-dir VIXEN/build --output-on-failure --parallel 3 -R 'RecipeGlslNumericalParityTest.GlslMatchesCpuEvalAcrossCorpus|RecipeGlslCompiles.EmittedGlslCompilesForEveryCorpusProgram|RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes'` exited 8 with matching diagnostics. The SVO, codegen, and shader trees have no diff against `d83b057e`; these are pre-existing SVO findings, not introduced by T-1103. Full-suite and filtered baseline logs are under `VIXEN/.tmp/sccheadless-*-ctest.log`.

## SPT DISPOSITION

- `T-1103` — CLOSE at landing. The operations now build and test as a headless library; the editor consumes that library, preview behavior is preserved, and capture pixels are identical.
- `T-1104` — OPEN downstream.
- `T-1105` — OPEN downstream.

## CONSOLIDATION ISSUES

- proposed: Headless module tests need a GPU-free CTest registration path
- proposed: vixen_stage_assets must create its stamp directory
- proposed: VIXEN needs a repo-local codegen gate instead of the shared generic script
