---
title: Known Issues / Bugs To Fix
status: living log
created: 2026-07-02
tags: [known-issues, bugs, tech-debt]
---

# Known Issues / Bugs To Fix

Living log of confirmed-but-unfixed issues. Each entry: symptom, root cause, impact, fix options, severity. Add new issues at the top; move fixed ones to a `## Resolved` section with the fixing commit.

---

## KI-004 — Nodes downstream of `FrameSyncNode` keep executing on a condemned frame after device loss, racing recovery teardown

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

**Severity:** High (crash, defeats the device-loss recovery feature under precise timing) / currently only reachable via synthetic fault injection · **Status:** OPEN — scenario gated via `knownIssueId = "KI-004"` (Fail-Scenario-Simulation Inc 1, `FailScenarioSweep_FrameSync.DeviceLostRecovery`); remove that gate once fixed, at which point the scenario becomes the permanent regression gate for this bug and validates the ORIGINAL manual `VIXEN_SIMULATE_DEVICE_LOSS` gate this scenario was meant to automate (Device-Loss-Recovery-2026-06.md Inc 3).

---

## KI-003 — `GeometryRenderNode` crashes (SIGSEGV) when `SwapChainNode` reports `IMAGE_INDEX = UINT32_MAX`

**Discovered:** 2026-07-02, by the `FailScenarioSweep_SwapChain.AcquireOutOfDate` fail-scenario (Fail-Scenario-Simulation-Design-2026-07.md, Inc 1 Task 6) — the first thing to deterministically force `VK_ERROR_OUT_OF_DATE_KHR` out of `vkAcquireNextImageKHR` on this dev box; no real GPU/driver here has apparently ever returned it outside a human-timed window resize.

**Symptom:** live-gated on WSLg + WSL2 Dozen ICD. `./build-wsl/application/main/test_fail_scenario_sweep --gtest_filter='FailScenarioSweep_SwapChain.AcquireOutOfDate'` dumps core (confirmed via `timeout ... ; echo EXIT_CODE=$?` → `timeout: the monitored command dumped core`). No gdb/lldb was available in this environment to pull a symbolized backtrace; the crash was isolated from log analysis instead — exactly 31 `VoxelGridNode::ExecuteImpl ENTERED` cycles run (30-frame warmup + the 1 frame where the fault fires), zero `SwapChainNode::Compile`/`RECOMPILATION TRIGGERED` lines ever appear, and the process dies silently mid-frame with no gtest `[  FAILED  ]`/assertion/signal text — consistent with a hard SIGSEGV inside frame N+1's execution, not a hang (the harness's 60s watchdog never has a chance to fire).

**Root cause (confirmed by source read, not just log inference):** `SwapChainNode::AcquireNextImage` (`SwapChainNode.cpp:309-330`) correctly detects `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR`, calls `MarkNeedsRecompile()`, and returns `UINT32_MAX`. `MarkNeedsRecompile()` only sets a **deferred** flag while the node is `Executing` — it does not recompile synchronously. `SwapChainNode::ExecuteImpl` (`SwapChainNode.cpp:179-189`) is correctly guarded and publishes `ctx.Out(IMAGE_INDEX, UINT32_MAX)` before returning early. But `RenderGraph::RenderFrame`'s per-frame node loop does not stop or skip remaining nodes this frame after a deferred-recompile flag is set — it continues executing the rest of `executionOrder` in the same frame. `GeometryRenderNode::ExecuteImpl` (`GeometryRenderNode.cpp`) reads that propagated `IMAGE_INDEX = UINT32_MAX` and indexes `renderCompleteSemaphores[imageIndex]` around line 165 — an **unchecked `operator[]`** — before its own `imageIndex == UINT32_MAX` guard, which exists but sits ~29 lines later (around line 194), gating only the command-buffer/submit logic. `UINT32_MAX * sizeof(VkSemaphore)` (~34GB) past the vector's data pointer lands on an unmapped page → SIGSEGV.

**Impact:** High in principle (any real OUT_OF_DATE/SUBOPTIMAL swapchain result — the standard signal for "recreate on resize/present-mode-change" — crashes the app instead of skipping the frame), but had zero prior observed impact because nothing on this dev box previously drove that Vulkan return code deterministically. This is plausibly related to (though not proven identical to) the user's separately-tracked live fullscreen-button crash class — same "swapchain reports an extent/index change, a downstream node doesn't defend against the transient invalid state" shape — but this KI is scoped to the concretely reproduced `GeometryRenderNode` crash only; do not conflate until confirmed.

**Fix options:**
1. Move the `imageIndex == UINT32_MAX` guard in `GeometryRenderNode::ExecuteImpl` to immediately after reading `IMAGE_INDEX` from the input context, before any indexing use (smallest, most local fix).
2. Harden `RenderGraph::RenderFrame` to stop executing (or skip-with-a-safe-default) the remaining nodes in `executionOrder` for the current frame once any node sets a deferred-recompile flag mid-frame — addresses the general "a frame with a just-invalidated swapchain must not let downstream nodes assume valid state" class, not just this one call site.
3. Both: local guard now (unblocks this scenario), graph-level hardening as a follow-up hardening pass.

**Recommended:** option 1 first (small, addresses the crash directly and lets this scenario go green), option 2 as a follow-up robustness item — this is the user's call, per Fail-Scenario-Simulation Inc 1's protocol of report-not-fix for scenarios that reproduce a bug class.

**Reproduction:** `cmake --build build-wsl --target test_fail_scenario_sweep -- -k 0` (VIXEN_FAIL_SCENARIOS=ON) then `./build-wsl/application/main/test_fail_scenario_sweep --gtest_filter='FailScenarioSweep_SwapChain.AcquireOutOfDate'`.

**Severity:** High (crash) / Low current exposure (never fired on real hardware here before) · **Status:** OPEN — scenario gated via `knownIssueId = "KI-003"` (Fail-Scenario-Simulation Inc 1, `FailScenarioSweep_SwapChain.AcquireOutOfDate`); remove that gate once fixed, at which point the scenario becomes the permanent regression gate for this bug.

---

## KI-001 — 3 RenderGraph tests fail to build: missing `xcb/xcb.h` (WSL env)

**Discovered:** 2026-07-02 (during the config-struct codegen epic full-build gate; pre-existing, not caused by that work).

**Symptom:** a full `cmake --build build-wsl -- -k 0` fails to compile 3 test TUs:
- `libraries/RenderGraph/tests/test_array_type_validation.cpp`
- `libraries/RenderGraph/tests/test_field_extraction.cpp`
- `libraries/RenderGraph/tests/test_resource_gatherer.cpp`

Error (all three, identical): `.vulkan-sdk/1.4.350.1/x86_64/Include/vulkan/vulkan.h:52:10: fatal error: xcb/xcb.h: No such file or directory`.

**Root cause:** `vulkan.h` includes `<xcb/xcb.h>` when `VK_USE_PLATFORM_XCB_KHR` is defined; the WSL build environment has no XCB development headers installed (`libxcb1-dev` / `libxcb-*-dev`). These three tests pull the full Vulkan platform header (transitively) rather than a headless subset.

**Impact:** medium (local/WSL). These 3 tests cannot build here. The offscreen/lavapipe render tests and SPIR-V-reflection tests are UNAFFECTED (they don't require the XCB surface platform) — e.g. `test_body_instance_raymarch_render` and `test_octree_config_sdi_parity` build + pass. A CI/host with the xcb dev package would not hit this.

**Fix options (pick one):**
1. Install the XCB dev headers in the WSL env: `sudo apt-get install libxcb1-dev libxcb-*-dev` (fastest; unblocks locally, but every dev must do it).
2. Do not define `VK_USE_PLATFORM_XCB_KHR` for headless test targets — they need no window surface. Scope the platform define to the app/windowing targets only.
3. CMake-gate these 3 tests on XCB header availability (`check_include_file`), skipping them with a `message(STATUS ...)` when absent (mirrors the existing dotnet-gated / SDK-gated patterns).

**Recommended:** option 2 (root-cause: headless tests shouldn't require a window-surface platform), with option 1 as the immediate unblock.

**Severity:** Medium · **Status:** OPEN

---

## KI-002 — `test_shell_octree_gpu.ConcatRejectsMoreThanThree` fails (stale test vs removed cap)

**Discovered:** 2026-07-02 (config-struct codegen C1 gate; pre-existing, unrelated to that byte-identical struct alias).

**Symptom:** `test_shell_octree_gpu` is 8/9 — `ShellOctreeGpu.ConcatRejectsMoreThanThree` (`libraries/SVO/tests/test_shell_octree_gpu.cpp:179`) fails. The test builds 4 shell octrees and asserts `EXPECT_THROW(Concatenate(four), std::length_error)`.

**Root cause:** the `kMaxOctrees = 3` cap was intentionally removed in the earlier recipe-authoring epic (the octree pool became memory-budgeted / count-unbounded — see the `recipe-authoring-pipeline-shipped` work). `Concatenate` no longer throws for >3 octrees, so the test's `EXPECT_THROW` fails. The test was not updated when the cap was removed.

**Impact:** low. This is a STALE TEST, not a functional regression — the unbounded-concat behavior is the intended design. But it leaves the SVO suite permanently red (1 test), which erodes the "green suite" signal.

**Fix options:**
1. If concat is now genuinely unbounded: replace the test with one asserting `Concatenate(four)` SUCCEEDS and produces a valid combined octree (rename e.g. `ConcatAcceptsMoreThanThree`).
2. If a new upper bound exists (e.g. a memory-budget or `kMaxChannels`-style limit): assert rejection at that real boundary instead of 3.

**Recommended:** confirm the intended concat contract (unbounded vs new-limit), then option 1 or 2 accordingly.

**Severity:** Low · **Status:** OPEN
