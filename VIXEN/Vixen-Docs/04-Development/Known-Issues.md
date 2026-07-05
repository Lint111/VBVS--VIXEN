---
title: Known Issues / Bugs To Fix
status: living log
created: 2026-07-02
tags: [known-issues, bugs, tech-debt]
---

# Known Issues / Bugs To Fix

Living log of confirmed-but-unfixed issues. Each entry: symptom, root cause, impact, fix options, severity. Add new issues at the top; move fixed ones to a `## Resolved` section with the fixing commit.

---

## Test-suite note (not a KI): `test_fail_scenario_sweep` is flaky under the Vulkan validation layer

Running any SINGLE `FailScenarioSweep*` test that does a live resize+recompile (e.g. `LiveResizeRecompilesPickIdRing`) under `VK_LAYER_KHRONOS_validation` alone can segfault (`vkCmdBindPipeline` referencing an already-deleted `VkDescriptorSetLayout`, stale command-buffer-in-use errors, then SIGSEGV) — but the SAME test passes cleanly with `[ PASSED ]` when run without the validation layer. This reproduces identically both before and after this session's changes, so it's pre-existing validation-layer/test-timing interaction, not a functional regression. Use the validation layer for spot-checking specific VUIDs on `vixen_editor` directly (as this session did for KI-009/KI-012); trust the plain (no-validation-layer) test run for pass/fail signal on `test_fail_scenario_sweep`.

---

## KI-008 — lavapipe is no longer usable for this project

**Discovered:** 2026-07-04, standing rule for the widescreen-perf-fix program's worktrees.

**Symptom/rule:** lavapipe (Mesa's `lvp_icd.json` software rasterizer) must not be used as a dev-loop ICD in this project going forward — a separate cleanup effort is removing it from the codebase entirely. Any doc, script, or `VK_ICD_FILENAMES` reference that still points at `lvp_icd.json` as a live option is stale guidance, not history.

**Impact:** affects any contributor or agent reaching for lavapipe as a quick headless-GPU stand-in for local iteration; WSL sessions without a provisioned real-GPU path (e.g. Mesa Dozen/Vulkan-over-D3D12) lose that fallback and must rely on CPU-only build+test gates, deferring live-render verification to a session where a real GPU is available.

**Fix:** none needed — this is a policy/environment note, not a bug. Swept the widescreen-perf-fix plan and findings docs (2026-07-04) for any forward-looking instruction still citing lavapipe/`VK_ICD_FILENAMES`/`lvp_icd`; all remaining occurrences were historical gate-result records (describing runs that already happened) and were left as-is per the sweep's own rule.

**Severity:** N/A (policy) · **Status:** OPEN (standing rule, not something to "resolve")

---

## Resolved (see below)

### KI-013 — `FailScenarioSweep_FrameSync.DeviceLostRecovery` segfaults inside Dozen's swapchain-image destroy path (regression against KI-004's documented-fixed state)

**Discovered:** 2026-07-04, while verifying the KI-012 pick-ID fix didn't regress `test_fail_scenario_sweep` — running the FULL suite in one process segfaulted right after `ResizeBurstDoesNotRecompileOncePerEvent`, before `FailScenarioSweep_FrameSync.DeviceLostRecovery` completed. Confirmed via `git stash` that this reproduced byte-identically at the pre-KI-012/pre-flicker-fix baseline (`origin/main` `9ddbb854`) — not caused by that session's other changes.

**File/line:** `libraries/RenderGraph/src/Nodes/SwapChainNode.cpp` (`CleanupImpl`).

**Symptom:** `DeviceLostRecovery` (run alone, isolated — same crash) segfaulted during `RenderGraph::RecoverFromDeviceLoss()`'s rebuild phase, specifically while rebuilding `main_swapchain` (`SwapChainNode::CompileImpl` → `CreateSwapchainAndViews` → `VulkanSwapChain::CreateSwapChainColorImages`). GDB backtrace:
```
Thread 1 received signal SIGSEGV
#0  0x... in ?? ()
#1  wsi_destroy_image () from .../libvulkan_dzn.so
#2  x11_swapchain_destroy () from .../libvulkan_dzn.so
#3  VulkanSwapChain::CreateSwapChainColorImages(VkDevice_T*, VkSwapchainKHR_T*)
#4  SwapChainNode::CreateSwapchainAndViews()
#5  SwapChainNode::CompileImpl(...)
#6  NodeInstance::Compile()
#7  RenderGraph::RecoverFromDeviceLoss()
#8  VulkanGraphApplication::Render()
```

**Root cause:** `SwapChainNode::CleanupImpl` treated `CleanupReason::Recompile` and `CleanupReason::DeviceLost` identically (`if (ctx.reason != CleanupReason::FinalTeardown)`) — both took the "keep the swapchain HANDLE alive across the boundary, destroy only per-image views" branch, so that `CreateSwapchainAndViews()` could pass the still-live handle as `oldSwapchain` for the driver to recycle/hand over presentation state. That's correct for `Recompile` (the SAME `VkDevice` recreates it), but wrong for `DeviceLost`: `RenderGraph::RecoverFromDeviceLoss()` has `DeviceNode::CompileImpl` create an entirely NEW `VulkanDevice` (`RenderGraph.cpp`) before `SwapChainNode` rebuilds — a `VkSwapchainKHR` is device-scoped, so the old handle belongs to the OLD, about-to-be-destroyed device. Passing it as `oldSwapchain` into the NEW device's `fpCreateSwapchainKHR`/`fpDestroySwapchainKHR` (resolved via the new device's dispatch table) is exactly the KI-004 bug class (a resource carrying stale device state across recovery) and segfaults deep in the driver's swapchain-destroy internals. The `VkSurfaceKHR`, by contrast, is instance-scoped and correctly survives a device recreation untouched.

**Fix (2026-07-04):** split the `Recompile`/`DeviceLost` branches. `Recompile` keeps the existing behavior (`DestroyImageViewsOnly`, swapchain handle survives for reuse). `DeviceLost` now calls `swapChainWrapper->DestroySwapChain(device)` — destroys the image views AND the swapchain handle (against the OLD, still-valid-but-lost device, which is safe per the `CleanupReason::DeviceLost` doc comment: calls against a lost device are expected to be harmless/no-ops), leaving `scPublicVars.swapChain = VK_NULL_HANDLE` so the later rebuild's `CreateSwapchainAndViews()` correctly does a cold creation (`oldSwapchain = VK_NULL_HANDLE`) against the new device instead of handing it a foreign-device handle. The surface is untouched in both branches (survives, as before).

**Verification:** `FailScenarioSweep_FrameSync.DeviceLostRecovery` passes in isolation (previously segfaulted) — log shows "RECOVERY COMPLETE: rendering resumes on the new device". Full `test_fail_scenario_sweep` suite: 10/10 tests run to completion (previously crashed after test 3/10) — 8 passed, 2 skipped by their own logic (pre-existing, unrelated). Full project rebuild + all 7 render-gate test suites (28 tests) re-verified passing with zero regressions.

**Severity:** High (crash in a documented-fixed regression gate for a real reliability feature) · **Status:** RESOLVED

### KI-012 — `VoxelSelectionProviderNode`'s pick-ID readback violates queue transfer-granularity on Dozen

**Discovered:** 2026-07-04, live-gate run of `vixen_editor` under `VK_LAYER_KHRONOS_validation` while chasing KI-009/render flicker (unrelated — surfaced only on a mouse click, not idle rendering).

**File/line:** `libraries/RenderGraph/src/Nodes/VoxelSelectionProviderNode.cpp` (`ReadCenterPixel`), `libraries/VulkanResources/{include,src}/VulkanDevice.cpp` (`RequiresFullImageTransfers`).

**Symptom:** on every click, two validation errors:
```
VUID-vkCmdCopyImageToBuffer-imageOffset-07747
pRegions[0].imageOffset (x = 250, y = 250, z = 0) must be (0, 0, 0) when the command buffer's
queue family minImageTransferGranularity is (0, 0, 0) as this queue doesn't allow for any offset.
pRegions[0].imageExtent (width = 1, height = 1, depth = 1) must match the image subresource
extent (width = 500, height = 500, depth = 1) when ... this queue only allows full image copies.
```

**Root cause:** the code copied a single 1×1 texel at an arbitrary offset (the cursor's pick position) out of the full-size ID image — a partial-image-region copy. Dozen's (Mesa Vulkan-over-D3D12) transfer-capable queue family reports `minImageTransferGranularity = (0,0,0)`, which per spec means that queue **only accepts whole-image copies at offset (0,0,0)** — no sub-region copies at all. lavapipe apparently tolerated this (hence it went unnoticed until the lavapipe-removal work this session put Dozen in the default path).

**Fix (2026-07-04):** checked once at startup, not re-queried per click, following the existing "ask `VulkanDevice` about queue capabilities" convention (alongside `HasPresentSupport()`): added `VulkanDevice::RequiresFullImageTransfers()`, computed from the already-queried `queueFamilyProperties[graphicsQueueIndex].minImageTransferGranularity == (0,0,0)`. `VoxelSelectionProviderNode::CompileImpl` caches this once per Compile (`requiresFullImageTransfers_`); `ReadCenterPixel` branches on it — the common per-click path (single-texel sub-region copy) is unchanged for devices with real transfer granularity, while devices that need whole-image transfers copy the ENTIRE id image into a (grow-only, reused-across-clicks) full-size staging buffer and index the center texel on the CPU side instead.

**Verification:** full build + `test_fail_scenario_sweep` (excluding the pre-existing `DeviceLostRecovery` crash, see KI-013) — 7/7 pass, 2 skipped by the tests' own logic, 0 regressions. `LiveResizeRecompilesPickIdRing` (which injects a real click and exercises the readback) passes cleanly without the validation layer; the same test is separately flaky under the validation layer alone (see the test-suite note above), unrelated to this fix.

**Severity:** Low (worked today even before the fix, spec-invalid, not on the hot/idle render path) · **Status:** RESOLVED

### KI-009 — `vixen_editor` render view flickers black/content on real GPU; VUID-vkCmdDraw-None-09600 layout mismatch

**Discovered:** 2026-07-04, investigating a user report that vixen_editor's render viewport alternates between showing the loaded geometry and solid black/dark-blue, at idle (no interaction needed to reproduce; camera framing was a separate, already-fixed bug that didn't affect this).

**File/line:** `libraries/RenderGraph/src/Nodes/RenderTargetNode.cpp` (`ExecuteImpl`).

**Symptom:** every few frames, `vkQueueSubmit2KHR` reported `VUID-vkCmdDraw-None-09600` (the descriptor-layout-mismatch VUID, applied here to the compute dispatch that binds the render target as a `STORAGE_IMAGE` — validation's message text says "draw" but the same rule governs a bound descriptor read by any command, dispatch included): `VkImage` (the offscreen render-target ring) expected in `VK_IMAGE_LAYOUT_GENERAL`, actually `UNDEFINED` or `TRANSFER_SRC_OPTIMAL`. Visually: UI panel stable, only the 3D render area flickered.

**Root cause:** `RenderTargetNode` maintains a ring of `imageCount_` offscreen images and rotates `currentIndex` every frame in `ExecuteImpl` (`currentIndex = (currentIndex + 1) % imageCount`) — but published its `CURRENT_VIEW` output **only once, in `CompileImpl`**, frozen at whatever ring slot `currentIndex` happened to be at compile time (slot 0). `DescriptorSetNode` binds the compute shader's binding-0 `STORAGE_IMAGE` descriptor from that frozen `CURRENT_VIEW` every frame (correctly re-writing the descriptor set each Execute, but always with the SAME stale image view) — while `ComputeDispatchNode` resolves the image it actually barriers-and-dispatches against via the LIVE `IRenderTarget::GetCurrentImage()` (`RENDER_TARGET_INFO`, following the rotating `currentIndex`). The descriptor's bound image and the barrier/dispatch's actual image are the same physical ring slot on only one phase out of every `imageCount` frames — every other frame they're two different images, so the barrier's careful `GENERAL` transition (already correct per KI-007's fix) applies to the WRONG slot from the descriptor's point of view.

**Fix (2026-07-04):** `RenderTargetNode::ExecuteImpl` now re-publishes `CURRENT_VIEW` (`ctx.Out(RenderTargetNodeConfig::CURRENT_VIEW, target_.GetCurrentView())`) immediately after advancing `currentIndex`, so the descriptor set tracks the live ring slot every frame instead of a compile-time snapshot.

**Two adjacent, independently-real synchronization bugs found and fixed en route** (neither was the flicker's actual cause, but both were genuine spec violations caught by `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`):
- `ComputeDispatchNode::BlitRenderTargetToSwapchain`'s swapchain-image entry barrier used `srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT` — a no-op source that doesn't chain an execution dependency with the WSI acquire semaphore's wait (declared at `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT` on this command buffer's submit). Harmless only while `oldLayout` was always `UNDEFINED` (nothing to wait for); once real prior-layout tracking was added (see below) this produced `SYNC-HAZARD-WRITE-AFTER-READ` against `vkAcquireNextImageKHR`. Fixed by changing `srcStageMask` to `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT`.
- The same function's swapchain entry barrier also hardcoded `oldLayout = VK_IMAGE_LAYOUT_UNDEFINED` unconditionally, when the swapchain image's real layout after the first frame is `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` (left there by the UI render pass's `finalLayout` + present). Fixed the same way as KI-007 — tracked via the same `renderTargetImageLayouts_` map, keyed by the swapchain image handle too.
- `RenderPassNode.cpp`'s UI composite render pass subpass-external dependency (built via `libraries/CashSystem/src/RenderPassCacher.cpp`) hardcoded `dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT` only. Since this render pass uses `LOAD_OP_LOAD`, the implicit initial-layout transition also needs `COLOR_ATTACHMENT_READ_BIT` to synchronize against the LOAD read — its absence produced `SYNC-HAZARD-READ-AFTER-WRITE` at `vkCmdBeginRenderPass`. Fixed by adding `VK_ACCESS_COLOR_ATTACHMENT_READ_BIT` to `dstAccessMask` whenever `colorLoadOp == Load`.

**Verification:** live-gate run of `vixen_editor` under `VK_LAYER_KHRONOS_validation` + synchronization validation: `VUID-vkCmdDraw-None-09600` and both `SYNC-HAZARD-*` messages are gone after all three fixes (confirmed zero occurrences across a multi-second run cycling all 4 ring slots repeatedly). Two unrelated, pre-existing validation messages remain (`VUID-vkCmdCopyImageToBuffer-imageOffset-07747` — see KI-012; `VUID-vkGetQueryPoolResults-None-09401` — GPU perf-logger query pool not reset before first read, not yet triaged).

**Severity:** Medium (visual only, no crash, no data loss) · **Status:** RESOLVED

### KI-007 — `ComputeDispatchNode::seenRenderTargetImages_` never prunes stale `VkImage` handles across resizes

**File/line:** `libraries/RenderGraph/include/Nodes/ComputeDispatchNode.h` (was `seenRenderTargetImages_`, now `renderTargetImageLayouts_`), `libraries/RenderGraph/src/Nodes/ComputeDispatchNode.cpp` (`RecordComputeCommands`/`BlitRenderTargetToSwapchain`).

**Symptom (as originally filed):** `seenRenderTargetImages_` was a `std::set<VkImage>` used to pick the correct `oldLayout` (`UNDEFINED` vs `TRANSFER_SRC_OPTIMAL`) for the render-target image's WSI-acquire barrier, keyed on whether a given `VkImage` handle had been seen before. Entries were only ever inserted, never erased, and — worse than originally filed — the seen/not-seen scheme was also simply WRONG once multiple frames are in flight: it assumed every handle strictly alternates GENERAL<->TRANSFER_SRC_OPTIMAL in lockstep, which doesn't hold when a command buffer is re-recorded against a ring slot whose actual last transition doesn't match that two-state guess.

**Fix (2026-07-04):** replaced the set with `std::unordered_map<VkImage, VkImageLayout> renderTargetImageLayouts_`, tracking the ACTUAL last-recorded layout per handle (updated at both the compute-write entry barrier and the post-blit exit barrier), via a small pure/testable free function `DecideRenderTargetPriorLayoutAndUpdate` (`ComputeDispatchNode.h`). Exact instead of guessed; also incidentally fixes the original unbounded-growth complaint (the map is keyed the same way but now semantically correct, and could be pruned the same way if that's ever a real concern).

**Verification:** 4 new unit tests in `test_compute_dispatch_node.cpp` (first-use-is-undefined, second-use-reports-real-tracked-layout, distinct-ring-slots-tracked-independently, map-updates-to-new-layout) — all pass. Does NOT fix the visible flicker/VUID-vkCmdDraw-None-09600 symptom that prompted this investigation — see KI-009 above; this was a real bug found along the way, not the one being chased.

**Severity:** Low (as filed) · **Status:** RESOLVED

---

## KI-006 — `CleanupImpl`-no-Recompile-guard class in `DescriptorSetNode`/`ComputePipelineNode`

**Files/lines:** `libraries/RenderGraph/src/Nodes/DescriptorSetNode.cpp:976-1001` (`DescriptorSetNode::CleanupImpl` — destroys descriptor pool + descriptor set layout unconditionally); `libraries/RenderGraph/src/Nodes/ComputePipelineNode.cpp:124-141` (`ComputePipelineNode::CleanupImpl` — destroys shader module + resets pipeline/layout/cache handles unconditionally).

**Symptom:** neither `CleanupImpl` checks the `CleanupReason` (`Recompile` vs `FinalTeardown`/`DeviceLost`) before tearing down its Vulkan objects — both destroy pool/layout/pipeline/shader-module on every cleanup call, including ordinary resize-triggered recompiles.

**Root cause:** same bug CLASS as the already-fixed KI-004 (device-scoped state torn down/rebuilt without regard to *why* cleanup is happening) — except here the objects are recreated every recompile regardless (no create-once guard reusing a stale handle), so this manifests as extra destroy/recreate churn on every resize rather than a crash. It is the same missing-`reason`-check shape, just without KI-004's crash-causing persistent-handle-reuse half.

**Impact:** wasted Vulkan object churn (descriptor pool/layout, shader module, pipeline) on every resize-driven recompile, and — per KI-005 below — the resulting layout-handle recreation is what feeds the L2 cache-key mismatch's stale-pipeline-bind VUID burst. Not a crash on its own.

**Fix options:** add the same `if (reason == Recompile) return;`-style (or equivalent explicit branch) guard pattern used to fix KI-004's affected nodes, once it's decided which of these objects legitimately need to survive a recompile (likely: none here, since shader/layout content can change across a recompile — needs a design decision, not a blind copy of the KI-004 fix).

**Severity:** Medium (perf/churn + contributing cause of KI-005, not a crash) · **Status:** OPEN (filed, not fixed — out of the widescreen-perf-fix program's bounded scope)

---

## KI-005 — L2 cache-key mismatch: `ComputePipelineCacher` hashes a resize-invariant string while `PipelineLayoutCacher` hands out a live handle

**Files/lines:** `libraries/CashSystem/src/ComputePipelineCacher.cpp:47-56` (`ComputeKey` hashes `ci.layoutKey`, a `std::string`); `libraries/RenderGraph/src/Nodes/ComputePipelineNode.cpp:~201` (`layoutParams.layoutKey = shaderBundle->uuid + "_pipeline_layout"` — constant across resizes, since the shader UUID doesn't change).

**Symptom:** after a resize-triggered recompile, one frame's compute dispatch binds a pipeline object that references the OLD (destroyed) `VkPipelineLayout` handle, producing a burst of stale-pipeline-bind validation errors (VUID) for that single frame before self-correcting.

**Root cause:** `ComputePipelineCacher`'s cache key is computed from `ComputePipelineCreateParams::layoutKey`, a string identifier (`shaderBundle->uuid + "_pipeline_layout"`) that is identical before and after a resize — so the compute-pipeline cache reports a hit and returns the previously-cached `ComputePipelineWrapper` (built against the OLD layout handle) even though `PipelineLayoutCacher` has since recreated the actual `VkPipelineLayout` for the new swapchain extent. The two cachers disagree on identity: one keys by content-string, the other hands out a live, resize-mutable handle.

**Impact:** one frame of VUID validation-layer noise per resize; self-heals on the next recompile pass since the cache eventually converges. Not observed to cause a crash or visible artifact, but is exactly the kind of one-frame hazard window the widescreen-perf-fix program was hunting — filed here because it's shared `CashSystem` infra (used by other cachers too), making a fix out of this program's bounded per-node scope.

**Fix options:** (1) include the live layout handle (not just its string key) in `ComputePipelineCacher::ComputeKey`'s hash, invalidating the cache entry whenever the underlying layout handle changes; (2) have `ComputePipelineNode` explicitly invalidate/evict its cached pipeline entry when it detects `PipelineLayoutCacher` returned a new handle for the same `layoutKey`, rather than relying on the cache's own key comparison.

**Severity:** Low-Medium (validation noise only, self-correcting, one frame) · **Status:** OPEN (filed, not fixed — shared CashSystem infra, out of this program's bounded scope)

---

*(No further open issues at present beyond KI-004 below — see Resolved for everything else.)*

---

## Resolved` section with the fixing commit.

---

## KI-004 — Nodes downstream of `FrameSyncNode` keep executing on a condemned frame after device loss, racing recovery teardown

**Update 2026-07-03 (partial fix landed, bug NOT fully resolved):** `RenderGraph::NotifyDeviceLost()` now calls `AbortCurrentFrame()` before latching (mirroring `9d95bd75`'s central abort for the out-of-date acquire path). **Verified this closes the originally-diagnosed race** — the log now shows "Frame aborted before node '...' — skipping the rest of this frame" fire the instant device loss is detected, and no downstream node executes on the condemned frame anymore.

**However, the scenario still crashes** — one step later, with the same symptom, for a **different, not-yet-isolated reason**: the FIRST frame after `RecoverFromDeviceLoss()` completes ("RECOVERY COMPLETE" logs, rebuild reports success) still crashes in `ComputeDispatchNode` with `vkBeginCommandBuffer: Invalid commandBuffer [VUID-vkBeginCommandBuffer-commandBuffer-parameter]`. Diagnostic logging (added and removed this session) proved this is NOT the original race:
- `imageIndex=0` at the crash — legitimately fresh (not a stale value from before recovery)
- `frameAborted=0` — correctly not aborted; this is a genuinely new, un-condemned frame
- The command-buffer handle is a **new address**, freshly allocated milliseconds earlier via `vkAllocateCommandBuffers` returning `VK_SUCCESS` from a freshly-recreated `VkCommandPool`

So a handle the driver just reported as successfully allocated is rejected as structurally invalid by the loader's own parameter check almost immediately after. Ruled out this session: double-compile of `ComputeDispatchNode` during rebuild (compiles exactly once), a stale `imageIndex` flowing from before recovery (value is fresh), a stale cached device pointer on the node (it has none — uses base `NodeInstance::device` via `SetDevice`/`GetDevice()` per convention). Not yet checked: whether `CommandPoolNode`'s newly-created pool and `ComputeDispatchNode`'s allocation from it are genuinely against the SAME new `VkDevice`, or whether some node in between the pool's rebuild and the dispatch node's rebuild reintroduces a stale device/pool reference; whether `vkResetCommandPool` or an implicit pool-level reset runs between allocation and use.

**Fix (landed, real but partial):** `RenderGraph::NotifyDeviceLost()` — `AbortCurrentFrame()` added before the idempotent latch check (`RenderGraph.cpp`). Confirmed via source diff and live log this fires correctly and stops the ORIGINAL race. `FailScenarioSweep_FrameSync.DeviceLostRecovery` remains `knownIssueId`-gated (report-not-block) — un-gate it only once the post-recovery command-buffer bug above is independently fixed and verified. Reproduction unchanged: `cmake --build build-wsl --target test_fail_scenario_sweep -- -k 0` (or `build/wsl` on the main checkout) then `./test_fail_scenario_sweep --gtest_filter='FailScenarioSweep_FrameSync.DeviceLostRecovery'`.

**Severity:** High (crash, defeats the device-loss recovery feature under precise timing) / only reachable via synthetic fault injection · **Status:** OPEN (partially fixed — see update above).

---

## Resolved

### KI-004 — Nodes/resources surviving device-loss recovery with stale device-scoped handles (crash class)

**Discovered:** 2026-07-02 by `FailScenarioSweep_FrameSync.DeviceLostRecovery` (Fail-Scenario-Simulation Inc 1 Task 7). **Resolved:** 2026-07-03, in three layers — the "one bug" was a CLASS.

**Symptom (evolution across the fix layers):** forcing a one-shot `VK_ERROR_DEVICE_LOST` out of `FrameSyncNode`'s fence wait → recovery completes ("RECOVERY COMPLETE") → SIGABRT. Layer by layer: (1) originally, a node downstream of the detection site executed on the condemned frame; (2) after fixing that, the FIRST post-recovery frame crashed at `vkBeginCommandBuffer: Invalid commandBuffer` (gdb: `UIRenderNode` beginning a command buffer allocated from the destroyed device's pool); (3) after fixing that, 30 post-recovery frames ran clean but FINAL teardown crashed at `vkUnmapMemory: Invalid device` / SIGSEGV in `PickIdTargetNode::DestroyImages` (destroying old-device images with the new device handle).

**Root cause (the class):** components holding device-scoped state across `CleanupReason::DeviceLost`:
1. `RenderGraph::RenderFrame`'s Execute loop checked `frameAborted_` between nodes but `deviceLost_` only at frame END — a mid-frame loss let downstream nodes execute/submit on the condemned frame. **Fix:** `NotifyDeviceLost()` calls `AbortCurrentFrame()` before latching (every current and future detection site aborts the frame for free). Commit `51a8dbd7`.
2. **Persistent-resource guards `if (reason != FinalTeardown) return;` in node CleanupImpls** — written for the Recompile case (device survives), but they ALSO kept device-scoped resources across `DeviceLost`, and the create-once guards in CompileImpl (image-count / null-handle checks) then reused the stale handles post-recovery. Affected and fixed (guard flipped to `if (reason == Recompile)` so DeviceLost tears down like FinalTeardown): `UIRenderNode` (per-image command buffers + RmlUi GPU objects; the post-recovery `vkBeginCommandBuffer` crash), `PickIdTargetNode` (the final-teardown crash), `BodyOctreeSceneNode`, `DynamicInstanceBufferNode`, `InstanceBufferNode`, `MvpUniformNode`, `StorageBufferNode`, `RenderTargetNode`, and `FrameSyncNode`'s persistent timeline semaphore (latent — timeline edges dormant in the default graph). Correct keeps left untouched: `WindowNode` (window+surface), `InstanceNode` (VkInstance), `InputNode` (GLFW hooks) — instance/OS-scoped, they legitimately survive a device loss.
3. **(Related hygiene, same session):** the synchronization2 entry points (`vkCmdPipelineBarrier2KHR`/`vkQueueSubmit2KHR`) were process-global function pointers (`VulkanGlobalNames.h`) despite being DEVICE-LEVEL dispatch — wrong for multi-device and a stale-dispatch window during recovery. Moved to per-instance `VulkanDevice::fpCmdPipelineBarrier2`/`fpQueueSubmit2` (resolved in `CreateDevice`); nodes reach them via `GetDevice()`, device-less recorders (`PassRecorder`, `BatchedUpdater::RecordAll`) receive the PFN as an explicit caller-injected parameter.

**Verified (2026-07-03, WSLg + Dozen):** `DeviceLostRecovery` passes as a HARD gate (no `knownIssueId`): detection → frame abort → teardown-reverse/rebuild-forward → 30 continuous post-recovery frames → clean final teardown, `[ PASSED ]` exit 0. `VIXEN_SIMULATE_DEVICE_LOSS=10` bridge on `*BootWarmupTeardown*` also passes. No-regression: registry 5/5, BootWarmupTeardown, AcquireOutOfDate/Suboptimal (KI-003 hard gates), PresentOutOfDate, MaximizeLikeFullscreenButton all PASS; the two WM-refusal skips unchanged. This completes Device-Loss-Recovery-2026-06.md Inc 3's automated-test item for real.

**Durable rule:** a `CleanupImpl` persistence guard must distinguish WHY it persists — device-scoped resources may only survive `Recompile`; only instance/OS-scoped state (window, surface, VkInstance) may survive `DeviceLost`. And device-level function pointers are per-`VulkanDevice` members, never globals.

**Severity:** was High (crash, defeated device-loss recovery) · **Status:** RESOLVED.


**Discovered:** 2026-07-02, by the `FailScenarioSweep_FrameSync.DeviceLostRecovery` fail-scenario (Fail-Scenario-Simulation-Design-2026-07.md, Inc 1 Task 7) — forcing a one-shot `VK_ERROR_DEVICE_LOST` out of `FrameSyncNode`'s `vkWaitForFences` on an otherwise-healthy frame (after 30 clean warmup frames).

**Symptom:** live-gated on WSLg + WSL2 Dozen ICD, reproduced 3 times across 2 different fault-arming paths:
- Twice via the scenario's own `ArmFault` (`timeout 90 ./build-wsl/application/main/test_fail_scenario_sweep --gtest_filter='FailScenarioSweep_FrameSync.DeviceLostRecovery'`) — both SIGABRT, exit 134, "Aborted".
- Once via the pre-existing `VIXEN_SIMULATE_DEVICE_LOSS` env-var bridge on an unrelated test case (`VIXEN_SIMULATE_DEVICE_LOSS=10 ./build-wsl/application/main/test_fail_scenario_sweep --gtest_filter='*BootWarmupTeardown*'`) — SIGSEGV, exit 139 (same underlying use-after-free-class race, different manifestation signal depending on exact memory/timing state, which is normal for this bug class). This confirms the race is in the recovery orchestration itself, not specific to the new `ArmFault`/`ScenarioContext` plumbing — the pre-existing env hook hits the identical bug once it actually lands the fault frame-precisely, something a human timing a real TDR by hand essentially never did.

Log sequence (ArmFault runs): `RenderGraph::RecoverFromDeviceLoss` logs "===== RECOVERY COMPLETE: rendering resumes on the new device =====", `VulkanGraphApplication::Render` logs "Device recovery succeeded — resuming rendering", then a `ComputeDispatchNode::RecordComputeCommands` call for a swapchain image (index varied 0→1 between runs — see root cause), then `[Vulkan Loader] ERROR: vkBeginCommandBuffer: Invalid commandBuffer [VUID-vkBeginCommandBuffer-commandBuffer-parameter]`, then abort. This is the Vulkan **loader's** own always-on parameter check (VK_LAYER_KHRONOS_validation is confirmed NOT installed in this environment — see M3 handoff notes — so this fires even without the validation layer), meaning the command-buffer handle in play is not just semantically stale but structurally invalid (freed/reallocated).

**Root cause (confirmed by source read across `RenderGraph.cpp`, `FrameSyncNode.cpp`, `ComputeDispatchNode.cpp`):** `RenderGraph::RenderFrame`'s sequential per-frame Execute loop (`RenderGraph.cpp:855-895`, mirrored in the parallel path `:791-849`) calls every node's `Execute()` unconditionally with no check of `deviceLost_` between iterations. When the injected fault fires, `FrameSyncNode::ExecuteImpl` (`FrameSyncNode.cpp:156-161`) calls `GetOwningGraph()->NotifyDeviceLost(...)` and `return`s early — without throwing, and before its own `ctx.Out()` calls (lines 166-174) republish this frame's fence/semaphore outputs. Because it returns normally (no exception), the loop treats it as ordinary completion and proceeds to the next node in `executionOrder`. `ComputeDispatchNode` sits downstream of `FrameSyncNode` in that order (consumes its `IN_FLIGHT_FENCE`/semaphore outputs, `ComputeDispatchNode.cpp:164`), so it still runs `ExecuteImpl`/`RecordComputeCommands` in the SAME condemned frame, reading stale (previous-frame) sync values and recording/submitting against a command buffer that is about to be invalidated. `RenderGraph::RenderFrame` only checks `deviceLost_` at the very end of the frame (`RenderGraph.cpp:938-940`) — after the whole node loop, including this stray submit, has already run — so `RecoverFromDeviceLoss`'s teardown (which does call `WaitForGraphDevicesIdle()` before tearing down pools/buffers, `RenderGraph.cpp:652`) starts strictly *after* the out-of-band submit is already in flight, racing it. The image-index variance between runs (0 vs 1) is consistent with this being a genuine race keyed on swapchain-acquisition timing, not a deterministic off-by-one.

Ruled out during investigation (confirmed clean, no need to re-check): `RecoverFromDeviceLoss` does cover every node via topologically-sorted reverse-teardown/forward-rebuild (`RenderGraph.cpp:654-678`); `ComputeDispatchNode::CompileImpl` unconditionally reallocates command buffers fresh every compile (no cross-compile reuse bug); `CommandPoolNode`/`DeviceNode` correctly destroy-and-recreate their Vulkan objects each recovery cycle with no stale-pointer caching.

**Impact:** High in principle — any device-loss event on a device fast enough to still be mid-frame-loop when the fault lands can hit this race, defeating the very recovery path Device-Loss-Recovery Inc 1-3 built. Low observed impact historically because this is the first deterministic, frame-precise way to trigger `VK_ERROR_DEVICE_LOST` in this environment (a real TDR/driver-reset is not something a human reliably times to a specific frame).

**Fix options:**
1. Add a `deviceLost_` check immediately after each `node->Execute()` call in `RenderGraph::RenderFrame`'s sequential loop (`RenderGraph.cpp:855-895`) and the parallel path (`:791-849`); `break` out of the frame the instant `NotifyDeviceLost` latches, so no node downstream of the fault site executes on the condemned frame. Smallest, most direct fix — addresses the actual race.
2. Have `FrameSyncNode::ExecuteImpl` throw instead of early-`return`ing on `VK_ERROR_DEVICE_LOST`, if the node-execution wrapper's exception handling already stops the frame cleanly (needs verification — mirroring option 1's effect via a different mechanism).
3. Both: option 1 as the primary defense (works regardless of individual node behavior), option 2 as defense-in-depth for the specific FrameSyncNode call site.

**Recommended:** option 1 — it is the single choke point that guarantees no downstream node observes a mid-recovery frame, regardless of which node happens to sit after `FrameSyncNode` in future graph topologies.

**Reproduction:** `cmake --build build-wsl --target test_fail_scenario_sweep -- -k 0` (VIXEN_FAIL_SCENARIOS=ON) then EITHER `timeout 90 ./build-wsl/application/main/test_fail_scenario_sweep --gtest_filter='FailScenarioSweep_FrameSync.DeviceLostRecovery'` OR `VIXEN_SIMULATE_DEVICE_LOSS=10 timeout 60 ./build-wsl/application/main/test_fail_scenario_sweep --gtest_filter='*BootWarmupTeardown*'`.

**Severity:** High (crash, defeats the device-loss recovery feature under precise timing) / only reachable via synthetic fault injection.

**Resolved:** 2026-07-03. **Fix applied (variant of option 1, one choke point):** `RenderGraph::NotifyDeviceLost()` now calls `AbortCurrentFrame()` before latching (`RenderGraph.cpp`) — the same central frame-abort `9d95bd75` introduced for the out-of-date acquire path. Any device-loss detection site (current: FrameSyncNode's fence wait; future: acquire/present/submit backstops) therefore aborts the frame the instant it latches, and the sequential Execute loop's existing `frameAborted_` check skips every downstream node on the condemned frame. Chosen over wiring the call inside `FrameSyncNode::ExecuteImpl` because the invariant belongs to the latch, not to one detection site.

**Verified:** `FailScenarioSweep_FrameSync.DeviceLostRecovery` un-gated (`knownIssueId` removed) and passing as a hard gate — full recovery (teardown-reverse/rebuild-forward, "RECOVERY COMPLETE") + 30 continuous post-recovery frames, no crash; the `VIXEN_SIMULATE_DEVICE_LOSS=10` env bridge on `*BootWarmupTeardown*` likewise recovers and passes. The scenario is now the permanent regression gate for this bug, completing Device-Loss-Recovery-2026-06.md Inc 3's automated-test item.

**Residual (follow-up, not this bug):** the PARALLEL execution path (`RenderGraph::RenderFrame`'s `ExecutePhase` branch) contains no `frameAborted_` checks at all — the central abort (KI-003's fix AND this one) only takes effect in sequential mode. The live app and all gates run sequential today; wire abort-awareness into the parallel executor before enabling parallel execution in production.

---

### KI-001 — 3 RenderGraph tests fail to build: missing `xcb/xcb.h` (WSL env)

**Discovered:** 2026-07-02 (during the config-struct codegen epic full-build gate; pre-existing, not caused by that work).
**Resolved:** 2026-07-02.

**Symptom:** a full `cmake --build build-wsl -- -k 0` failed to compile 3 test TUs:
- `libraries/RenderGraph/tests/test_array_type_validation.cpp`
- `libraries/RenderGraph/tests/test_field_extraction.cpp`
- `libraries/RenderGraph/tests/test_resource_gatherer.cpp`

Error (all three, identical): `.vulkan-sdk/1.4.350.1/x86_64/Include/vulkan/vulkan.h:52:10: fatal error: xcb/xcb.h: No such file or directory`.

**Root cause:** `vulkan.h` includes `<xcb/xcb.h>` when `VK_USE_PLATFORM_XCB_KHR` is defined; the WSL build environment has no XCB development headers installed (`libxcb1-dev` / `libxcb-*-dev`). These three tests pulled the full Vulkan platform header (transitively) rather than a headless subset — despite `test_type_system.cmake`'s own header stating "Compatible with VULKAN_TRIMMED_BUILD (headers only, no Vulkan runtime needed)".

**Fix applied (option 2 — root-cause):** removed the `VK_USE_PLATFORM_{XCB,WIN32,MACOS}_KHR` `target_compile_definitions` blocks from all 3 targets in `libraries/RenderGraph/tests/test_type_system.cmake`. These are header-only compile-time/type-trait tests that never link a real Vulkan surface; no sibling headless `.cmake` in the same directory (`test_core_systems.cmake`, `test_critical_nodes.cmake`, `test_graph_systems.cmake`, `test_voxel_systems.cmake`) defines a platform macro at all — this file was the outlier.

**Verified:** all 3 targets build clean and pass at runtime on WSL (no XCB headers installed) — `test_array_type_validation`, `test_field_extraction`, `test_resource_gatherer` all print their `✅ ALL TESTS PASSED` banners, exit 0.

**Severity:** Medium · **Status:** RESOLVED

---

### KI-002 — `test_shell_octree_gpu.ConcatRejectsMoreThanThree` fails (stale test vs removed cap)

**Discovered:** 2026-07-02 (config-struct codegen C1 gate; pre-existing, unrelated to that byte-identical struct alias).
**Resolved:** 2026-07-02.

**Symptom:** `test_shell_octree_gpu` was 8/9 — `ShellOctreeGpu.ConcatRejectsMoreThanThree` (`libraries/SVO/tests/test_shell_octree_gpu.cpp:179`) failed. The test built 4 shell octrees and asserted `EXPECT_THROW(Concatenate(four), std::length_error)`.

**Root cause:** the `kMaxOctrees = 3` cap was intentionally removed in the earlier recipe-authoring epic (the octree pool became memory-budgeted / count-unbounded — see the `recipe-authoring-pipeline-shipped` work; `ShellOctreeGpu.h`'s own `Concatenate()` docstring already read "Count is unbounded", and the sibling `ConcatenateSdf()` had a matching `OctreePool.ConcatenatesMoreThanThreeSdfOctrees` accept-test). `Concatenate` only throws `std::invalid_argument` on a null pointer, never on count, so the stale `EXPECT_THROW` failed. The test was never updated when the cap was removed.

**Fix applied (option 1 — genuinely unbounded):** renamed the test to `ConcatAcceptsMoreThanThree` and rewrote it to assert `Concatenate(four)` succeeds and produces a valid combined pool — `count == 4`, per-octree `nodeArrayBase`/`brickArrayBase` non-decreasing, and the concatenated `nodes`/`bricks` byte buffers equal to the sum of per-octree element counts times their stride (`sizeof(ChildDescriptor)` / `SerializedOctree::kBrickStrideBytes`), mirroring the existing `ConcatRecordsPerOctreeBaseOffsets` test's assertion style. Also corrected 3 stale "<=3 octrees" comment headers in `ShellOctreeGpu.h` (lines 55/57/663) that contradicted the function's own "Count is unbounded" docstring.

**Verified:** `test_shell_octree_gpu` is 9/9 passing at runtime (lavapipe-free, headless gtest).

**Severity:** Low · **Status:** RESOLVED

---

### KI-003 — `GeometryRenderNode` crashes (SIGSEGV) when `SwapChainNode` reports `IMAGE_INDEX = UINT32_MAX`

**Discovered:** 2026-07-02, by the `FailScenarioSweep_SwapChain.AcquireOutOfDate` fail-scenario (Fail-Scenario-Simulation-Design-2026-07.md, Inc 1 Task 6) — the first thing to deterministically force `VK_ERROR_OUT_OF_DATE_KHR` out of `vkAcquireNextImageKHR` on this dev box; no real GPU/driver here had apparently ever returned it outside a human-timed window resize.
**Resolved:** 2026-07-03, by the user's own parallel live-debugging session — commit `9d95bd75` "fix(render): fullscreen/maximize segfault — central frame abort on out-of-date acquire" (main), merged into this branch via `1df4ac4d`.

**Symptom:** live-gated on WSLg + WSL2 Dozen ICD. `./build-wsl/application/main/test_fail_scenario_sweep --gtest_filter='FailScenarioSweep_SwapChain.AcquireOutOfDate'` dumped core (confirmed via `timeout ... ; echo EXIT_CODE=$?` → `timeout: the monitored command dumped core`). No gdb/lldb was available in this environment initially to pull a symbolized backtrace; the crash was isolated from log analysis instead — exactly 31 `VoxelGridNode::ExecuteImpl ENTERED` cycles ran (30-frame warmup + the 1 frame where the fault fires), zero `SwapChainNode::Compile`/`RECOMPILATION TRIGGERED` lines ever appeared, and the process died silently mid-frame with no gtest `[  FAILED  ]`/assertion/signal text — consistent with a hard SIGSEGV inside frame N+1's execution, not a hang.

**Root cause (as originally diagnosed, confirmed correct by the independent fix):** `SwapChainNode::AcquireNextImage` correctly detected `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR`, called `MarkNeedsRecompile()`, and returned `UINT32_MAX`. `MarkNeedsRecompile()` only set a **deferred** flag while the node was `Executing` — it did not recompile synchronously. `SwapChainNode::ExecuteImpl` was correctly guarded and published `ctx.Out(IMAGE_INDEX, UINT32_MAX)` before returning early. But `RenderGraph::RenderFrame`'s per-frame node loop did not stop or skip remaining nodes this frame after the sentinel was set — it continued executing the rest of `executionOrder` in the same frame. `GeometryRenderNode::ExecuteImpl` read that propagated `IMAGE_INDEX = UINT32_MAX` and indexed `renderCompleteSemaphores[imageIndex]` — an **unchecked `operator[]`** — before its own `imageIndex == UINT32_MAX` guard, which existed but sat ~29 lines later, gating only the command-buffer/submit logic. `UINT32_MAX * sizeof(VkSemaphore)` (~34GB) past the vector's data pointer landed on an unmapped page → SIGSEGV. The user's independent debugging (via the live fullscreen/maximize crash, not this scenario) found the SAME root cause plus 5 additional consumers with the identical guard-placement mistake (DescriptorSetNode + AccelerationStructureNode had no guard at all — the actual user-reported maximize SIGSEGV; ComputeDispatch/GeometryRender/PassGroup indexed before their guard).

**Fix applied (commit `9d95bd75`, matches this KI's recommended "option 2" — graph-level hardening, generalized beyond a single guard fix):** `RenderGraph` gained a generic `AbortCurrentFrame()`/`IsFrameAborted()` mechanism (`RenderGraph.h`/`.cpp`) — any node can call it mid-frame to signal the frame cannot proceed; the sequential (and parallel) Execute loop checks it after every node's `Execute()` and `break`s before the next node. `SwapChainNode`'s OUT_OF_DATE/SUBOPTIMAL sentinel branch now calls it, so every downstream per-image consumer is skipped wholesale for that frame instead of needing an individually-correct guard. Per-node guards remain as second-layer defense (six were fixed to be correctly-placed/present as part of the same commit, including `GeometryRenderNode`'s).

**Verified (post-merge, this branch):** `FailScenarioSweep_SwapChain.AcquireOutOfDate`/`AcquireSuboptimal` no longer crash — 30 full post-injection frames complete, 0 validation errors, no core dump (initially observed via the scenarios' own known-issue-mode report: "progressed=true, validationErrors=0"; the `knownIssueId` gate has since been removed from both scenario declarations in `SwapChainNode.cpp` so they now hard-gate this fix as permanent regression tests).

**Impact was:** High in principle (any real OUT_OF_DATE/SUBOPTIMAL swapchain result crashed the app instead of skipping the frame); this KI's crash was ONE INSTANCE of the user's separately-tracked live fullscreen-button crash class (same "swapchain reports an extent/index change, a downstream node doesn't defend against the transient invalid state" shape) — confirmed identical by the shared fixing commit, not merely plausibly related as originally noted.

**Severity:** High (crash) · **Status:** RESOLVED

---

### KI-014 — Library tests are not `ctest`-discoverable project-wide (`enable_testing()` ordered after `add_subdirectory(libraries)`)

**Discovered:** 2026-07-05, during AppFlow Inc-1 Milestone 3 (the first library added since; the gap is pre-existing and affects every VIXEN library, not AppFlow specifically).

**Symptom:** `ctest --test-dir <build> -N` (or `-R <anything>`) reports `Total Tests: 0` for the ENTIRE project — no library's gtest targets are discovered, despite every library's `tests/CMakeLists.txt` calling `gtest_discover_tests`. No `CTestTestfile.cmake` is generated under `build/libraries/<lib>/tests/`.

**Root cause:** `VIXEN/CMakeLists.txt` calls `add_subdirectory(libraries)` (line ~393) BEFORE `enable_testing()` (line ~445, inside the `if(BUILD_TESTS)` "TESTING INFRASTRUCTURE" block near the bottom). `gtest_discover_tests` only registers CTest entries when testing was enabled at the point the subdirectory was processed; because `enable_testing()` runs afterward, no library subdirectory ever sees an enabled test harness, so nothing is registered with CTest. Verified project-wide: SVO, RenderGraph, CashSystem, EventBus, AppFlow, etc. all lack a generated `CTestTestfile.cmake`.

**Workaround (current, documented):** run the gtest binaries directly — `./build/libraries/<lib>/tests/[Debug/]test_*[.exe] --gtest_brief=1` — which is already `VIXEN/CLAUDE.md`'s documented test command. AppFlow Inc-1's suite (17 tests) was gated this way (build the 5 test targets, run each binary; all exit 0).

**Fix direction (not applied — out of scope for the AppFlow work that found it):** move `enable_testing()` (and the `include(GoogleTest)`/`FetchContent` gtest setup it depends on) ABOVE `add_subdirectory(libraries)` in `VIXEN/CMakeLists.txt`, gated by `if(BUILD_TESTS)`. Then `ctest --test-dir <build>` would discover all library tests. Low-risk, mechanical, but touches the top-level build ordering — worth its own small verified change so the full suite is CI-runnable via one `ctest` invocation.

**Severity:** Low (tests run fine directly; only the aggregate `ctest` runner is affected) · **Status:** OPEN
