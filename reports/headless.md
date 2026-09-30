# Headless VIXEN frame

VIXEN now lets graph setup choose between the existing window presentation chain and an offscreen image target. The graph above the target is shared: the same UI render node, render pass, frame synchronization, and framebuffer nodes render either path. The offscreen target can be read back to PNG through VIXEN's public application API. The focused VIXEN witness passed; the broad RenderGraph/SVO suite retains base-proven failures described below.

## Measured graph terminal

At base `9e7e104fcead5292db614c59e941645e7dbfae61`, `VulkanGraphApplication::BuildUIGraph()` unconditionally created `WindowNode`, `SwapChainNode`, and `PresentNode` (`VIXEN/application/main/source/graph/BuildUIGraph.cpp:37-44`). The graph wired the window into the swapchain at lines 69-80, used swapchain images for the render pass and framebuffer at lines 83-88, fed swapchain acquisition and render-complete semaphores into `UIRenderNode` at lines 90-100, then ended with `PresentNode` at lines 102-111. Thus frame acquisition and presentation required a display even though the UI render itself did not.

The available offscreen GPU render fixtures were private executables: `test_rendergraph_criticalnodes_gpurender1` and `...gpurender2` are declared in `VIXEN/libraries/RenderGraph/tests/test_critical_nodes.cmake:284-299,434-442`. They build fixture-owned Vulkan resources and are not included by KFR's `BUILD_TESTS=OFF` provider configuration.

## Change

- Added public `VulkanGraphApplication::PresentationTarget { Window, Offscreen }`; Window remains the default. `BuildUIGraph()` now selects either the window/swapchain/present chain or a `RenderTargetNode`, while retaining the same UI render pass and rendering nodes (`VIXEN/application/main/source/graph/BuildUIGraph.cpp:33-55,77-148`).
- Added a live `RenderTargetNodeConfig::IMAGE_INDEX` output and publish the ring's current image index during compile and execution. `UIRenderNode` now permits missing WSI semaphore arrays and submits the selected offscreen image using the frame fence.
- Disabled GLFW surface and presentation-extension setup for offscreen graphs. The offscreen render pass ends in transfer-source layout, and `CaptureOffscreenFrameToPng()` exposes the existing PNG readback helper to application clients (`VIXEN/application/main/source/VulkanGraphApplication.cpp:4672-4703`).
- Updated the existing RenderTarget node tests and added `HeadlessUiGraph.RendersAndReadsBackAnOffscreenFrame`, which builds the UI graph without a window, renders one frame, decodes the PNG, and checks for non-clear pixels (`VIXEN/application/main/tests/test_headless_ui_graph.cpp:47-72`).

## Verification

- Recorded base before semantic edits: `9e7e104fcead5292db614c59e941645e7dbfae61`. The first fresh RenderGraph/SVO suite run exited 8 with 23 failing tests. Its exact failure set was rerun on unchanged base; logs: `/home/liory/.local/state/undertow/undertow-box-logs/1790772647-test-vixen-rg-svo-baseline-retry.log` and `/home/liory/.local/state/undertow/undertow-box-logs/1790773114-test-vixen-rg-svo-base-failures-recheck.log`.
- Configured with the documented `vixen-wsl` preset and built the VIXEN application, new headless test, existing RenderTarget tests, and GPU render targets `gpurender1/2`. The allowed VIXEN native build route was `nice -n 10 cmake --build build/wsl ... -- -j8`; it completed successfully.
- Focused queued CTest witness: **50/50 passed**, including the new headless UI test, RenderTarget config tests, and GPU tests from both gpurender targets. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1790777872-test-vixen-final-focused-witness.log`.
- The final queued full-list run completed with **17 failures of 2,056 executed tests**, 14 skipped, and one disabled. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1790777040-test-vixen-rg-svo-final-absolute-list.log`. Its failing cases were AppFlowEditorToggleRender, EditorToggleUndoCapture.FirstEditReachesRenderPipeline, six SwitchCostIsolation cases, ViewHudGolden, ViewEditorLayersReconcile, two ViewWire cases, TypedAccessorEmitter, ViewContainerBuilder, two NodeSelfRegistration cases, and RecipeSimdParity. The failures match the unchanged-base failures; the editor first-edit pixel failure was additionally reproduced on base with the same zero-pixel-diff result (`1790777661-test-vixen-editor-visual-gate-base-proof.log`). Fixture staging and capture-directory recovery cleared the missing editor/HUD artifact failures from the first run.
- Codegen gates that do not consume the current Undertow authoring catalogue passed. `appflow_check` and `view_noun_enum_check` still reject that catalogue because `DepletionMaterialRow` declares `gaia:true` without a row scope or ref-field pairing. The pinned codegen snapshot is `b50d40f5`; this is a provider/catalogue contract mismatch, outside the headless graph change. Initial queued generator build also required `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1` because this host has no ICU; the recovered checks ran with that setting.
- CMake configuration prints `Fatal Error: glslang directory not found in Vulkan SDK path`, but configure and the fresh build finish with status 0; this is known environment output, not a failed target.
- Process note: while preparing SPT proposal metadata, malformed shell quoting triggered one CTest command outside the queue and one raw no-op build attempt. The CTest exited before running tests because it received a relative test-list path. Neither invocation is used as verification evidence. All actual suite witnesses cited above used the box queue; the VIXEN native build used the direct route specified by this lane.

## Downstream KFR smoke

KFR's renderer was configured against committed VIXEN tip `8ff936210dae2482659bbb61f1b32e93c59628f1` using an ignored source overlay whose provider pin was changed only in the overlay. The final merged KFR build passed and CTest passed **8/8**, including the one-frame headless session smoke. The image is [reports/kfrsmoke.png](/home/liory/projects/KernelFederationRenderer/.claude-worktrees/kfrsmoke/reports/kfrsmoke.png), 500 × 500 RGB, 21,239 bytes, SHA-256 `072e2c0af01894d906ce1d9fb036821eae259be4fd59116e2ae8672dbe96495c`. The KFR fixture loads the prepared sample contract state and renders its `MainMenu` at frame 1; it checks the sample pack exists but does not execute the externally owned production pack loader.

## STOPs and owner follow-up

- **STOP — requested broad gate is not green.** The brief requests the existing RenderGraph/SVO suites to pass. Their 17 remaining failures are reproduced on the unchanged base, and the focused headless/gpurender witness is green, but this lane cannot claim the full requested gate passed.
- **REFER-TO-ORCHESTRATOR — codegen contract.** Decide how the pinned VIXEN codegen snapshot and current Undertow catalogue should agree on `DepletionMaterialRow`'s Gaia scope/ref-field contract. No generated schema was hand-edited.

## CONSOLIDATION ISSUES

- proposed: Stage the WSL editor binary and assets for capture runs
- proposed: Share one capture directory between editor apps and CTest
- proposed: Preserve WSL loader libraries in the GPU capture launcher
- proposed: Provide an invariant globalization mode for the pinned .NET codegen tool
- proposed: Resolve CTest suite-list paths before changing test directory
- proposed: Allow a lane-local VIXEN provider override without editing the committed pin
- proposed: Keep runtime cache writes out of the tracked global manifest
- proposed: Reuse provisioned Vulkan SDKs across KFR scratch builds
- proposed: Reuse staged X11 development packages across KFR build trees
- proposed: Avoid refetching the kernel pin for scratch source overlays
- proposed: Build CodeGraph indexes for disposable worktrees
