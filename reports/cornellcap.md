# Cornell and headless capture lane

## Summary

The production main render graph now selects either its existing window/swapchain path or an offscreen terminal target through the same public PresentationTarget setting. The graph stages above that target are shared. A new headless Cornell test captures the production graph twice and checks byte determinism plus stable ownership along both shared-wall seams.

The editor capture runner now has opt-in offscreen and top-down camera inputs. Its default interactive camera is unchanged. The existing pixel threshold and layer-state checks pass.

Implementation commits: 472fbd8b (main graph and Cornell capture), eb0765c3 (editor capture camera). The final witness tree includes origin/wave/authoring-convergence at 903949c71d4f6a72b485f5a44b7ea607c2fd0995.

## Main graph offscreen presentation

BuildRenderGraph selects main_offscreen_target when PresentationTarget is Offscreen, and otherwise keeps main_window, main_swapchain, and present. Consumer wiring routes through the selected target. The offscreen terminal target supports storage access, color attachment writes, BlitNode transfer destination, and PNG readback transfer source. CaptureOffscreenFrameToPng uses the selected graph target and device, so the same public readback method works for the UI graph and production main graph.

The offscreen branch omits WSI nodes and semaphore connections. Compute/Blit nodes accept the target's image index without requiring acquire/present semaphores. InputNode accepts an absent optional WINDOW input.

## Cornell capture

HeadlessCornellGraph.ProductionGraphRendersDeterministicCornellWithStableSharedWallSeams instantiates VulkanGraphApplication, selects Offscreen, and calls the production BuildRenderGraph. It verifies the graph has main_offscreen_target and no main_window, main_swapchain, or present nodes. After five completed frames it captures the selected target twice through CaptureOffscreenFrameToPng.

The 500x500 RGB captures are byte-identical. The P584 seam check samples columns 151 and 348 for rows 170 through 220, outside the HUD panels. The left seam consistently selects the red wall and the right seam the green wall: zero unexpected pixels and zero row-to-row color transitions on both lines.

Witness: ctest --test-dir ../build/wsl -R HeadlessCornellGraph --verbose -j1; 1/1 passed in 4.17 seconds. The test also passed in the serial CTest selection.

## Editor capture camera

VIXEN_EDITOR_OFFSCREEN_CAPTURE=1 selects the same production offscreen graph path. VIXEN_EDITOR_TEST_CAMERA=top-down sets yaw to 0 and pitch to 1.45 for capture runs only; the interactive default camera remains unchanged. The runner used toggle:2@30, undo@60, redo@90, settings@100, back@110; captures were taken at frames 5, 45, 75, and 105, then the editor exited at frame 120.

The state log records masks 7 -> 3 -> 7 -> 3, correct undo/redo stack depths, and afterBack=0. All four EditorToggleUndoCapture tests pass. FirstEditReachesRenderPipeline measured wholeImageDiffPixels(png5,png45)=1022 for 500x500, above the unchanged >20 threshold. Undo and redo restore their corresponding captures byte-for-byte.

## Baseline and final witness

Before source edits, the worktree was at e5fb7a161d887f2b702acb0165364acc5fee491c. The baseline command was ctest --test-dir ../build/wsl -I 43,1073 --output-on-failure -j1; it exited 8 with 1,031 cases, 11 skipped, and one failure. The failing set was only EditorToggleUndoCapture.FirstEditReachesRenderPipeline (test 704): wholeImageDiffPixels(png5,png45)=0, expected greater than 20. Log: /home/liory/.local/state/undertow/undertow-box-logs/1790802629-test-rendergraph-ctest-pre-edit-established.log.

Before the final witness, origin/wave/authoring-convergence was fetched and merged as a fast-forward to 903949c71d4f6a72b485f5a44b7ea607c2fd0995. From VIXEN/, configure used VIXEN_FETCHCONTENT_CACHE=<worktree>/build/cornellcap-fetchcontent-cache cmake --preset vixen-wsl -DVIXEN_SCHEMA_CATALOG=/home/liory/projects/undertow/core/src/Undertow.Authoring/Schema/schemas.json and exited 0. CMake printed “Fatal Error: glslang directory not found” for the SDK Include path while still generating successfully. All 22 test_rendergraph_* targets plus test_headless_cornell_graph, VIXEN, and vixen_editor built successfully. VIXEN and vixen_editor were built separately after a combined app build raced while copying into the shared binaries/assets directory. VIXEN/cache/global/manifest.txt was restored after the editor run.

Recovery notes: the first preset command from the repository root failed because CMakePresets.json lives under VIXEN (log: /home/liory/.local/state/undertow/undertow-box-logs/1790804027-build-configure-after-edits.log); rerunning from VIXEN with the local FetchContent path and schema catalog succeeded. Every build/test command used the configured queue at /home/liory/projects/undertow/tools/with-test-lock.sh because this worktree has no local tools/with-test-lock.sh. The initial CodeGraph query had no .codegraph index or read-only route, so source discovery continued with rg and file reads without creating an index.

HUD file assertions used the existing PNGs in VIXEN/temp/cornellcap-baseline by setting an absolute VIXEN_HUD_CAPTURE_DIR. The HUD producer was not rerun because DISPLAY was unset; the editor captures above were freshly generated in a separate absolute capture directory.

The current CTest inventory contains 1,315 RenderGraph cases at positions 43-1355 and 1357-1358. The serial runs covered all those cases: 1,304 passed, 11 were skipped, and none failed. The initial range copied from the previous lane, -I 493,1807, selected 968 tests and exited 8 because 91 unrelated SVO/application entries were marked NOT_BUILT. Parsing ctest --show-only=json-v1 identified the RenderGraph executable paths; the missing first 450 RenderGraph cases were then run with ctest --test-dir ../build/wsl -I 43,492 --output-on-failure -j1. The broad selection's 865 RenderGraph cases all passed or skipped, so this selection issue did not produce a failing RenderGraph case. Logs: build/wsl/cornellcap-rendergraph-final.log and build/wsl/cornellcap-rendergraph-prefix.log.

After the edit, the RenderGraph failing-name set is empty. HeadlessCornellGraph passed, and EditorToggleUndoCapture.FirstEditReachesRenderPipeline—the sole pre-edit failure—passes.

## STOPs

None. The editor camera ruling was explicit, and no editor API change was needed.

## CONSOLIDATION ISSUES

- proposed: Expose the box queue wrapper to VIXEN worktrees
- proposed: Make the CodeGraph query route available in isolated VIXEN worktrees
- proposed: Keep FetchContent in the active VIXEN worktree by default
- proposed: Make VIXEN presets discoverable from the repository root
- proposed: Avoid parallel app asset staging collisions
- proposed: Label CTest cases by owning suite
- proposed: Run windowed capture producers as CTest fixtures
- proposed: Derive render-target usage from graph consumers
- proposed: Honor optional slots in InputNode validation
- proposed: Represent WSI synchronization capability in the target contract
