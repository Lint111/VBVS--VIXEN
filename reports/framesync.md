# Lane framesync

Witness tree: f7dc8ca839518ee61c3ec8cab0c5073717943075, the 846ab1a999a542f663ee08764fe69b3de2beade3 wave tip plus shadowflake's 3cce1755 frame-completion fix. Baseline tree: untouched 846ab1a999a542f663ee08764fe69b3de2beade3.

No source changes were needed. The full-frame resource-completion fix passed this isolated witness.

## Candidate witness

- Fresh validation-on configure completed. The queued Release build completed 1,143/1,143 steps; all 22 generated-code check targets passed.
- RenderGraph CTest: 1,341 passed, 5 skipped, 0 failed.
- SVO CTest: 750 passed, 1 failed, 8 skipped, 1 disabled. The failure was RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes / M4d_Output_IsPassthrough, with the known opcode-94 recipe-gradient capability mismatch. This matches shadowflake run 2 and is reported as a finding, not a STOP.
- Standard capture CTest selection: 11/11 passed with caller DISPLAY, WAYLAND_DISPLAY, VK_ICD_FILENAMES, and VK_LAYER_PATH unset.
- Native capture witness: 1/1 passed and wrote 7 PNGs. Cel capture selection: 4/4 passed.
- The HeadlessStarfieldGraph CaptureFirst, CaptureSecond, and CompareIndependentCaptures sequence passed 20 complete rounds. All four PNG SHA-256 values matched across every round; off and repeat-off were 2cc9f0c7a02d4757e682a88e0dbcf5f98429d6111cda18aa09f96e81a02279a1, and on and repeat-on were 1590f10e24d43cece0e7aa9df39299810c9abf34e905224e88e968b59650eb33.
- FrameSyncTimeline tests: 3/3 passed on the rebuilt binaries.
- Validation-log scan covered 160 test_dispatch log lines, the standard capture run, the native capture logs, and the headless process-pair run. Counts were 0 for VUID-vkBeginCommandBuffer-commandBuffer-00049, VUID-vkQueueSubmit2-commandBuffer-03875, and VUID-vkUpdateDescriptorSets-None-03047.
- Direct CodegenTool restore succeeded. Build succeeded with 0 warnings and 0 errors. A dotnet run --check for the Hud view blob succeeded. The standalone run used .tmp/codegen-artifacts for its outputs; the CMake build also passed all 22 generated checks.
- The first post-baseline candidate build invocation reconfigured and rebuilt 1,076 steps. A second queued invocation reported ninja: no work to do.

## Untouched-wave capture comparison

I configured and built capture targets from the exact 846ab1a9 source archive, then ran the standard capture selection 11/11, the editor/HUD capture producers 2/2, the cel captures 4/4, the native witness 1/1, and the headless starfield process-pair checks 3/3. The archived CTest assertion target was built before the 11/11 baseline run.

All 33 generated PNGs matched the candidate byte-for-byte: 4 cel captures, 15 headless captures, 7 editor/HUD offscreen captures, and 7 native captures. tools/compare-capture-pixels.py with --require-byte-identical reported byte_equal=yes and pixel_diff=0 for every file.

## Recovery notes

- The first candidate capture command used the wrong relative CTest directory and failed before launching tests; rerunning against the worktree-level ../build/wsl passed.
- The first process-pair attempt supplied an absent manual VK_LAYER_PATH and failed Vulkan instance creation. Removing that override let the renderer use its configured WSL setup; the capture passed, followed by all 20 complete rounds.
- The first baseline native witness could not find tools/run-vixen-windowed-captures.sh because the temporary archive initially contained only VIXEN. I restored tools/ from the exact 846ab1a9 commit inside the temporary archive, then the native witness passed.
- The first no-op invocation regenerated CMake and rebuilt 1,076 steps; the second invocation confirmed zero build work.
- SPT rejected the prescribed consolidation-only tag because a category tag is also required. Retrying with velocity and consolidation recorded the proposals.

## KFR follow-up

The follow-up callsite is KernelFederationRenderer/app/src/session_renderer.cpp, SessionRenderer::BuildRenderGraph(). No KFR files were changed.

## STOPs

None.

## CONSOLIDATION ISSUES

- proposed: Reuse the provisioned Vulkan SDK for temporary baseline sources
- proposed: Include repository-root tools in baseline capture archives
- proposed: Separate mutable FetchContent outputs across independent VIXEN build trees
- proposed: Provide a worktree-local artifact preset for standalone CodegenTool runs
- proposed: Calibrate the queue estimate for cold full VIXEN builds
- proposed: Document the required SPT category tag in worker proposal instructions
