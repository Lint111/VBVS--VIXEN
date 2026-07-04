---
title: Known Issues / Bugs To Fix
status: living log
created: 2026-07-02
tags: [known-issues, bugs, tech-debt]
---

# Known Issues / Bugs To Fix

Living log of confirmed-but-unfixed issues. Each entry: symptom, root cause, impact, fix options, severity. Add new issues at the top; move fixed ones to a `## Resolved` section with the fixing commit.

---

## KI-008 — lavapipe is no longer usable for this project

**Discovered:** 2026-07-04, standing rule for the widescreen-perf-fix program's worktrees.

**Symptom/rule:** lavapipe (Mesa's `lvp_icd.json` software rasterizer) must not be used as a dev-loop ICD in this project going forward — a separate cleanup effort is removing it from the codebase entirely. Any doc, script, or `VK_ICD_FILENAMES` reference that still points at `lvp_icd.json` as a live option is stale guidance, not history.

**Impact:** affects any contributor or agent reaching for lavapipe as a quick headless-GPU stand-in for local iteration; WSL sessions without a provisioned real-GPU path (e.g. Mesa Dozen/Vulkan-over-D3D12) lose that fallback and must rely on CPU-only build+test gates, deferring live-render verification to a session where a real GPU is available.

**Fix:** none needed — this is a policy/environment note, not a bug. Swept the widescreen-perf-fix plan and findings docs (2026-07-04) for any forward-looking instruction still citing lavapipe/`VK_ICD_FILENAMES`/`lvp_icd`; all remaining occurrences were historical gate-result records (describing runs that already happened) and were left as-is per the sweep's own rule.

**Severity:** N/A (policy) · **Status:** OPEN (standing rule, not something to "resolve")

---

## KI-007 — `ComputeDispatchNode::seenRenderTargetImages_` never prunes stale `VkImage` handles across resizes

**File/line:** `libraries/RenderGraph/include/Nodes/ComputeDispatchNode.h:125` (declaration), `libraries/RenderGraph/src/Nodes/ComputeDispatchNode.cpp:397-404` (usage).

**Symptom:** `seenRenderTargetImages_` is a `std::set<VkImage>` used to pick the correct `oldLayout` (`UNDEFINED` vs `TRANSFER_SRC_OPTIMAL`) for the render-target image's WSI-acquire barrier, keyed on whether a given `VkImage` handle has been seen before. Entries are only ever inserted (`.insert(writeImage)`), never erased.

**Root cause:** every window resize destroys and recreates the swapchain/render-target images, producing new `VkImage` handles; the old handles become dead entries in the set. Nothing prunes them, so the set grows by one stale entry per resize for the lifetime of the node.

**Impact:** tiny and real — not a correctness bug (stale handles are never looked up again, since a destroyed `VkImage` is never re-issued to compare against), just an unbounded-in-principle memory/lookup-cost growth tied to resize frequency. Not a practical concern at normal resize rates; flagged for completeness, not urgency.

**Fix options:** clear the set (or erase the specific old handle) whenever `RenderTargetNode` reports a new image for the same slot, e.g. on `CleanupImpl(reason=Recompile)`/recompile-driven image replacement.

**Severity:** Low · **Status:** OPEN (filed, not fixed — out of the widescreen-perf-fix program's bounded scope)

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
