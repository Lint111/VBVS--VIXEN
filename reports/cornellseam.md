# Cornell shared-wall seam regression

Date: 2026-10-02

Worktree: `/home/liory/projects/VBVS--VIXEN/.claude-worktrees/cornellseam`

Branch: `lane-cornellseam`

Starting tree: `8b113ffd0181bfcc269cfdedd43f755dd6ecd269`

## Required scope

The target is `HeadlessCornellGraph.ProductionGraphRendersDeterministicCornellWithStableSharedWallSeams`. Its verification scope includes the production `BuildRenderGraph` path, the RenderGraph ray-march shader and runtime body order, the pinned Yeroket CodegenTool snapshot, the VIXEN schema catalogue used at configure, and fresh application/test binaries. The requested gates are the 22 codegen/mutex checks, RenderGraph, SVO, 11 capture checks, ten-image pixel comparison, and a zero-unit no-op rebuild.

The dispatch reports this test passing 2/2 at `61df9883` and failing 2/2 at `1529073e`, then failing 3/3 at `8b113ffd`. The original failing runner command and log were not included in the dispatch. I rebuilt clean source archives for both endpoint commits and recorded the exact pin/catalogue combinations below.

## Bisect matrix

All seam tests below ran three times with fresh target binaries. Caller `DISPLAY`, `WAYLAND_DISPLAY`, and Vulkan loader/layer variables were unset; CTest applied the configured Dozen runtime environment to the test itself.

| VIXEN source | Kernel pin | Catalogue at configure | Seam test |
| --- | --- | --- | --- |
| `61df9883` | `b8dddb7a54620e32610d33392af3f0ea2ecba876` | v141, SHA-256 `85d375534c072010ec1c50ffabc4911c5cd4de689c2ea6ceb536cec7b3eac8fd` | PASS 3/3 |
| `61df9883` | `e9e6ee0da98a82a9d74a420550a8c3ce96416776` override | v141, same SHA-256 | PASS 3/3 |
| `61df9883` | `e9e6ee0da98a82a9d74a420550a8c3ce96416776` override | v142, SHA-256 `329f077935fe1523a449bf38c80c1619f50521f76730ac9f942a78d36312472a` | PASS 3/3 |
| `1529073e` | `e9e6ee0da98a82a9d74a420550a8c3ce96416776` tracked | v142, same SHA-256 | PASS 3/3 |
| `8b113ffd` | `e9e6ee0da98a82a9d74a420550a8c3ce96416776` tracked | v142, same SHA-256 | PASS 3/3 |
| `8b113ffd` | `e9e6ee0da98a82a9d74a420550a8c3ce96416776` tracked | v143, SHA-256 `8c24d8ca6655e84321fea4b123404d5636a602f9d124b7335ce46b1ec19994d2` | PASS 3/3 |

The first three rows change one axis at a time: pin, then catalogue. The fourth row changes only the VIXEN source from `61df9883` to `1529073e` while holding e9/v142 fixed. No transition reproduced the dispatched failure.

The `61df9883..1529073e` VIXEN diff contains six files: the tracked kernel SHA in `VIXEN/codegen/CMakeLists.txt`, the generated `ViewNounId.g.h`, and four deleted generated DeepFieldCells outputs. It does not change Cornell scene construction, authored body order, `TraceWorld.glsl`, or the seam assertion. The current selection rule resolves an exact distance tie by lower body slot; `InstanceSort` preserves the authored order for identical centers.

## Cause

No cause was isolated on this host. The pin change, v141-to-v142 catalogue change, and P620 VIXEN source change each passed when varied independently. The current v143 tree also passed. The dispatch failure therefore remains unreproduced here; the available evidence does not establish whether it depends on a different GPU/driver/runtime or another unrecorded runner input.

## Fix

No product-code change was made. The renderer and seam test pass across all fresh endpoint and axis builds on this host, so changing tie ownership without a reproducing failure would be speculative and could alter unrelated captures. The assertion was not loosened. To continue the cause-directed fix, the owner needs to provide the failing runner's exact configure/build/test command, GPU and driver details, effective CTest environment, and failing capture.

## Recovery and invocation record

- The first configure attempt ran `cmake --preset vixen-wsl` from the worktree root and exited 1 because `CMakePresets.json` is in `VIXEN/`. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1790917486-build-cornellseam:configure-baseline-v142-e9.log`. Reconfiguring from the VIXEN source directory with explicit historical catalogue and kernel paths succeeded. This was an invocation failure before product build, not a product baseline red.
- The first historical test invocation ran the required relative `bash tools/with-test-lock.sh ...` from `VIXEN/` and exited 127 because the wrapper is at the worktree root. Rerunning the same test through the wrapper from the worktree root passed 61df9883 3/3. No test payload started on the failed invocation.
- CodeGraph was queried before source search as required, but no query/index was available in this isolated worktree. No index was created; source inspection continued with `rg` and reads. An earlier proposal for this same missing route is already recorded in `.spt-proposals/cornellcap.jsonl`.
- Configure printed a non-fatal Vulkan SDK diagnostic that the `Include/glslang` directory was missing; CMake returned 0 and all required builds/checks completed.
- Recovery ladder: invocation and path errors were corrected and the fresh scoped baseline was established. Regeneration was not a repair candidate for the dispatched seam result; endpoint snapshots used their recorded generated content and explicit catalogue inputs. Isolation was unnecessary after all fresh endpoint/axis checks passed. No required baseline check remains unobtainable.

## Witness

- **Builds:** clean target builds passed for the 61df9883 and 1529073e source archives. The current `8b113ffd` tree built `rendergraph_svo_tests`, `VIXEN`, `vixen_editor`, `test_headless_ui_graph`, and `test_headless_cornell_graph`. The first default-tree build completed 160 previously unbuilt units; the final queued `cmake --build build/wsl --parallel 4` reported `ninja: no work to do` (zero units).
- **Checks:** all 22 requested check targets completed with exit 0 under e9/v142. After configuring v143/e9, the full target invocation exited 0; the catalogue-sensitive AppFlow and ViewNoun checks ran and passed, and the remaining targets were up to date.
- **RenderGraph:** `ctest -L RenderGraph --output-on-failure -j1`: 1,336 selected, zero failures, 11 configured skips.
- **SVO:** 752 selected, 742 passed, 8 skipped, 1 disabled, and one failure: `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`, `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94` (opcode 94). No other SVO failure occurred.
- **Captures:** the requested capture selection passed 11/11 after the full build with caller `DISPLAY`, `WAYLAND_DISPLAY`, `VK_ICD_FILENAMES`, `VK_DRIVER_FILES`, `VK_LAYER_PATH`, and `LD_LIBRARY_PATH` unset. The suite's CTest properties supply Dozen settings to the GPU tests.
- **Capture comparison:** `tools/compare-capture-pixels.py <wave-dir> <after-dir> --require-byte-identical` compared all ten PNG artifacts: four editor, three HUD, two Cornell readbacks, and one UI readback. All ten were byte-identical and had zero changed pixels. The reference directory was captured from the unchanged `8b113ffd` wave tree before the final run; this is a repeatability comparison, not a before/after product fix.
- **Scheduling:** configure/build/test commands used the box queue. Native builds were run with `nice -n 10` and `--parallel 4` or `-j1` for serial CTest.

## STOPs

- **STOP — dispatched regression not reproduced on this host.** The exact `1529073e` e9/v142 build and every one-axis comparison passed 3/3. No safe cause-specific source fix can be validated without the failing runner inputs. This is not a baseline-unobtainable STOP: the required local baseline and verification scope completed.

## CONSOLIDATION ISSUES

- proposed: VIXEN configure preset resolves only from its subdirectory
- proposed: Box queue wrapper lookup depends on the current directory
- proposed: Codegen does not discover the project kernel checkout
- proposed: Pinned kernel snapshots need a worktree-local cache setting
- proposed: Historical VIXEN builds need a reusable FetchContent cache path
- proposed: Archived VIXEN source trees need an explicit Vulkan SDK cache path
