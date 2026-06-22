---
title: Auto-Sync FrameGraph — P5a Implementation Plan (Timeline primitive + swapchain identity)
aliases: [auto-sync P5a, FrameSyncNode timeline, swapchain Resource identity, AR#21 P5a]
tags: [architecture, plan, rendergraph, synchronization, AR21, framegraph, timeline]
created: 2026-06-22
status: PLAN — ready for milestone-pipeline execution
related:
  - "[[Auto-Sync-FrameGraph-Inc1-Design-2026-06]]"
  - "[[Auto-Sync-FrameGraph-Inc1-Plan-P4-2026-06]]"
---

# Auto-Sync FrameGraph — P5a Implementation Plan (Timeline primitive + swapchain identity)

> **For agentic workers:** execute via the **post-brainstorm-context-manager** milestone pipeline (fresh implementer + Opus validator per milestone; controller thin). Steps use `- [ ]`. Clean-commit gate (build + tests + the change does what it claims, from fresh evidence). **Per the P4 lesson [[live-verification-authoritative-for-gpu-work]]: any milestone that changes the live GPU render path is NOT done until it has actually run with validation on — static review is insufficient.**

**Goal:** `FrameSyncNode` gains a per-loop **timeline semaphore** + a per-frame **`frameBase`** (exposed via output slots, not yet consumed), and the real swapchain `Resource*` is wired into the scheduler — which turns on the already-coded baked **image-barrier replay** — verified to NOT regress the live composite (0 syncval).

**Architecture:** Tier-2 needs a monotonic timeline + a per-frame base so the scheduler's relative timeline offsets don't collide across the 4-frame in-flight ring. P5a lands the *primitive* (M1, zero behaviour change — created + published but nothing waits/signals yet) and the *swapchain image identity* (M2 — passing the real `Resource*` to `FrameSyncScheduler::Build` flips `isImage=true`, so the already-written `ComputeDispatchNode::ReplayEntryBarriers` image arm starts firing baked layout barriers). Edge consumption at submit + composite migration are **P5b**.

**Tech Stack:** C++23, Vulkan 1.3. `VK_SEMAPHORE_TYPE_TIMELINE` requires the `timelineSemaphore` device feature — enabled via the **capability graph**, exactly as P3 enabled `synchronization2` (commit `fbde5c04` on main). GoogleTest; build = `cmd.exe /c _ninja_preset_build.bat` FOREGROUND (`timeout: 600000`).

---

## Context (grounded by investigation, 2026-06-22)

- **The schedule already bakes edges.** `BuildScheduleFromTimelines` (`libraries/RenderGraph/src/Core/FrameSyncScheduler.cpp:19-53`) already populates `SyncEdge`s, `SubmitGroup.waitEdges/signalEdges`, and `timelineValuesPerFrame=groupCount`. P5 is **consumption + identity**, not baking. (P5a does NOT consume edges — that's P5b.)
- **Swapchain `Resource*` already exists.** `ComputeDispatchNode` (`SWAPCHAIN_INFO` slot) and `UIRenderNode` (`SWAPCHAIN_INFO` slot) both connect from the SAME `SwapChainNode` output `SWAPCHAIN_PUBLIC` → they resolve to one `Resource*` (the tracker keys on slot `Resource*`s: `ResourceAccessTracker.cpp:81,104`). P3/P4 passed `nullptr` at `RenderGraph.cpp:~541`. P5a passes the real one.
- **Live path:** the standalone `VIXEN.exe` runs the composite path (BodyInstanceRayMarch compute → UI → present), so M2 changes the **live render path** → live syncval gate required.
- **frameBase deadlock hazard (the subtle one):** every node in a frame must read the SAME base. `FrameSyncNode` executes first (upstream of compute/UI), so it advances + publishes `frameBase` at the top of `ExecuteImpl`; all downstream consumers read the published value. In P5a nothing consumes it, so M1 is zero-risk; the monotonicity unit test locks the arithmetic before P5b relies on it.

## Reference implementations (lift-and-adapt; implementers read these)

| Need | Read & follow (file:line) |
|---|---|
| Enable a device feature via the capability graph (mirror sync2) | `libraries/VulkanResources/src/VulkanDevice.cpp` `QueryAvailableDeviceFeatures` + `src/CapabilityGraph.cpp` `BuildStandardCapabilities` (P3 commit `fbde5c04`: `DeviceFeature:synchronization2` query→register→gated enable, hard-error if absent). Do the identical thing for `timelineSemaphore` (`VkPhysicalDeviceVulkan12Features.timelineSemaphore`). |
| Create a timeline semaphore | `libraries/ResourceManagement/src/Memory/BatchedUploader.cpp:357-367` — `VkSemaphoreTypeCreateInfo{ .semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE, .initialValue=0 }` chained via `pNext`, then `vkCreateSemaphore`. |
| Node config X-macro slots + `static_assert` counts | `libraries/RenderGraph/include/Data/Nodes/FrameSyncNodeConfig.h` (current 3 outputs; the X-macro pattern). |
| Read the baked schedule from inside a node | `ComputeDispatchNode.cpp:341-342` — `GetOwningGraph()->GetFrameSyncSchedule()` + `FindGroupForNode`. |
| Scheduler unit-test harness | `libraries/RenderGraph/tests/test_frame_sync_scheduler.cpp` (esp. `WriterThenReader_ProducesEdge` adapter test ~:185-206; the `FrameSyncScheduler::Build` path). |

**SSOT types (use exactly):** `FrameSyncSchedule{groups, edges, timelineValuesPerFrame, valid}`, `SubmitGroup{...swapchainAcquireWait, swapchainPresentSignal}`, `SyncEdge{timelineOffset...}` (`Core/FrameSyncSchedule.h`); `FrameSyncScheduler::Build(executionOrder, tracker, swapchainResource)` (`Core/FrameSyncScheduler.h`).

---

## File Structure

| File | New/changed | Responsibility |
|---|---|---|
| `libraries/VulkanResources/src/VulkanDevice.cpp` | modify | query + enable `timelineSemaphore` (Vulkan12Features) |
| `libraries/VulkanResources/src/CapabilityGraph.cpp` | modify | register `DeviceFeature:timelineSemaphore` |
| `libraries/RenderGraph/include/Nodes/FrameSyncNode.h` + `src/Nodes/FrameSyncNode.cpp` | modify | `timelineSemaphore_`, `frameBase_`, advance + publish |
| `libraries/RenderGraph/include/Data/Nodes/FrameSyncNodeConfig.h` | modify | 2 new output slots (`TIMELINE_SEMAPHORE`, `TIMELINE_FRAME_BASE`); OUTPUTS 3→5 |
| `libraries/RenderGraph/src/Core/RenderGraph.cpp` (~:541) | modify | locate swapchain `Resource*`, pass to `Build` |
| `libraries/RenderGraph/tests/test_frame_sync_node_timeline.cpp` | new | frameBase monotonicity unit test |
| `libraries/RenderGraph/tests/test_frame_sync_scheduler.cpp` | modify | swapchainResource → image-barrier + tagging test |
| `libraries/RenderGraph/tests/CMakeLists.txt` | modify | register the new test |

## Milestone Map

> Confirm before execution; do not re-segment on resume.

- **M1 ✅ DONE — FrameSyncNode timeline + frameBase (Tasks 1–3).** Feature gate + timeline semaphore + frameBase advance/publish + 2 output slots + monotonicity unit test. **Zero behaviour change** (primitive created/published, nothing consumes it). *Gate (agent):* build green + unit test pass (no GPU).
- **M2 ✅ DONE — Swapchain `Resource*` identity → baked image barriers (Tasks 4–6).** Pass the real swapchain `Resource*` to the scheduler (turns on image-barrier replay on the live path) + scheduler unit test. *Gate:* build + unit, **then HANDS-ON live syncval** — the live composite must render at 0 syncval with the newly-firing baked image barriers (no regression). REQUIRES a live GPU run.

---

## Tasks

### Task 1: Enable `timelineSemaphore` via the capability graph

**Files:** Modify `libraries/VulkanResources/src/VulkanDevice.cpp`, `src/CapabilityGraph.cpp`

- [ ] **Step 1: Mirror the sync2 enablement.** Read how P3 enabled `synchronization2` (commit `fbde5c04`: in `VulkanDevice::QueryAvailableDeviceFeatures`, query `VkPhysicalDeviceVulkan12Features.timelineSemaphore` alongside the v1.3 features; in `CapabilityGraph::BuildStandardCapabilities`, register a `DeviceFeature:timelineSemaphore` capability; at device creation, enable `vulkan12Features.timelineSemaphore = VK_TRUE` gated on availability with a hard error if absent). Implement the identical pattern for `timelineSemaphore`.
- [ ] **Step 2: Build green** (`_ninja_preset_build.bat`).
- [ ] **Step 3: Commit** — `git commit -m "feat(vulkan): enable timelineSemaphore via the capability graph (auto-sync P5a M1)"`

### Task 2: `FrameSyncNode` timeline semaphore

**Files:** Modify `include/Nodes/FrameSyncNode.h`, `src/Nodes/FrameSyncNode.cpp`

- [ ] **Step 1: Add the member + create it.** Add `VkSemaphore timelineSemaphore_ = VK_NULL_HANDLE;` to `FrameSyncNode.h`. In `CompileImpl`, create it once (guard `isCreated`/null) using the `BatchedUploader.cpp:357-367` pattern: `VkSemaphoreTypeCreateInfo{ .sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, .semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE, .initialValue=0 }`, chained into `VkSemaphoreCreateInfo.pNext`, `vkCreateSemaphore`.
- [ ] **Step 2: Destroy it final-teardown-only.** In `CleanupImpl`, destroy `timelineSemaphore_` ONLY on final teardown (mirror `UIRenderNode.cpp:311-338`'s persistent-across-recompile rationale) so resize/recompile keeps the monotonic counter. Set to `VK_NULL_HANDLE` after destroy.
- [ ] **Step 3: Build green.**
- [ ] **Step 4: Commit** — `git commit -m "feat(rendergraph): FrameSyncNode owns a per-loop timeline semaphore (auto-sync P5a M1)"`

### Task 3: `frameBase` advance + publish via 2 output slots + monotonicity test

**Files:** Modify `include/Data/Nodes/FrameSyncNodeConfig.h`, `FrameSyncNode.{h,cpp}`; create `tests/test_frame_sync_node_timeline.cpp`; modify `tests/CMakeLists.txt`

- [ ] **Step 1: Add 2 output slots** to `FrameSyncNodeConfig.h`: `TIMELINE_SEMAPHORE` (`VkSemaphore`) and `TIMELINE_FRAME_BASE` (`uint64_t`). Bump `OUTPUTS` 3→5; add the `OUTPUT_SLOT` macro entries, `INIT_OUTPUT_DESC`, and update the `static_assert` output count — copy the exact macro shape from the existing 3 output slots.
- [ ] **Step 2: Add `frameBase_` + advance/publish.** Add `uint64_t frameBase_ = 0;` to `FrameSyncNode.h`. In `ExecuteImpl`, at the top alongside the `currentFrameIndex` advance, compute the stride from the baked schedule and advance:
```cpp
const uint64_t stride = GetOwningGraph()->GetFrameSyncSchedule().timelineValuesPerFrame; // 0 if no edges yet
frameBase_ += stride;   // monotonic; survives recompile (never reset in CompileImpl)
```
Then publish BOTH new outputs (compile sets defaults; execute publishes the live values): `TIMELINE_SEMAPHORE = timelineSemaphore_`, `TIMELINE_FRAME_BASE = frameBase_`. Publish AFTER the advance so every downstream consumer in this frame reads the same base. **Do NOT reset `frameBase_` in `CompileImpl`** (it must survive resize/recompile).
- [ ] **Step 3: Write the monotonicity unit test** (`test_frame_sync_node_timeline.cpp`) — a pure CPU test of the advance arithmetic (factor the advance into a tiny free/static helper `NextFrameBase(prev, stride)` if needed to test without a device):
```cpp
#include <gtest/gtest.h>
#include <cstdint>
#include <set>
// Helper under test (define in FrameSyncNode.h as a static constexpr):
//   constexpr uint64_t NextFrameBase(uint64_t prev, uint64_t stride){ return prev + stride; }
#include "Nodes/FrameSyncNode.h"
using Vixen::RenderGraph::NextFrameBase;
TEST(FrameSyncTimeline, BaseAdvancesByStrideAndNeverCollidesWithin4Frames) {
    const uint64_t stride = 3;        // e.g. 3 groups/frame
    uint64_t base = 0; std::set<uint64_t> seen;
    for (int f = 0; f < 8; ++f) {
        for (uint64_t o = 0; o < stride; ++o)          // every (base+offset) value this frame
            EXPECT_TRUE(seen.insert(base + o).second); // unique across all frames (no ring collision)
        base = NextFrameBase(base, stride);
    }
    EXPECT_EQ(base, stride * 8);
}
TEST(FrameSyncTimeline, ZeroStrideHoldsBase) {
    EXPECT_EQ(NextFrameBase(5, 0), 5u);   // no edges -> base unchanged
}
```
- [ ] **Step 4: Register the test** in `tests/CMakeLists.txt` (mirror an existing `test_frame_sync_scheduler` entry, renamed).
- [ ] **Step 5: Build + run** `test_frame_sync_node_timeline.exe --gtest_brief=1` → 2 PASSED.
- [ ] **Step 6: Commit** — `git commit -m "feat(rendergraph): FrameSyncNode frameBase advance + timeline output slots + tests (auto-sync P5a M1)"`

### Task 4: Wire the real swapchain `Resource*` into the scheduler

**Files:** Modify `libraries/RenderGraph/src/Core/RenderGraph.cpp` (~:534-541)

- [ ] **Step 1: Locate the swapchain `Resource*`.** Before the `frameSyncScheduler_.Build(...)` call, find the swapchain node and read its `SWAPCHAIN_PUBLIC` output `Resource*`. Discover it by node type (iterate the graph's nodes, find the `SwapChainNode`; read `node->GetOutput(SwapChainNodeConfig::SWAPCHAIN_PUBLIC slot index, 0)`). Store as `const Resource* swapchainResource` (nullptr if no swapchain node — headless/test graphs).
- [ ] **Step 2: Pass it to Build.** Change `frameSyncScheduler_.Build(executionOrder, resourceAccessTracker_, /*swapchainResource=*/nullptr)` → `... , swapchainResource)`.
- [ ] **Step 3: Build green.**
- [ ] **Step 4: Commit** — `git commit -m "feat(rendergraph): pass real swapchain Resource* to FrameSyncScheduler (auto-sync P5a M2)"`

### Task 5: Scheduler unit test — swapchainResource → image barrier + tagging

**Files:** Modify `tests/test_frame_sync_scheduler.cpp`

- [ ] **Step 1: Write the test.** Extend the existing adapter-style test (`WriterThenReader_ProducesEdge`, ~:185-206) with a case that builds a 2-node graph (writer then reader of a shared resource) and calls `FrameSyncScheduler::Build(order, tracker, &sharedResource)` passing that resource as `swapchainResource`; assert: the resulting schedule has (a) an `entryBarrier` with `isImage==true` on the consumer group, and (b) the first group tagged `swapchainAcquireWait==true` and the last `swapchainPresentSignal==true`. Reuse the test's existing graph-construction helpers; follow the file's patterns exactly.
- [ ] **Step 2: Build + run** `test_frame_sync_scheduler.exe --gtest_brief=1` → all PASS (existing + new).
- [ ] **Step 3: Commit** — `git commit -m "test(rendergraph): scheduler bakes image barriers + swapchain tagging with real Resource (auto-sync P5a M2)"`

### Task 6: LIVE syncval gate (HANDS-ON — controller-driven)

**Files:** none (verification)

- [ ] **Step 1: Build with validation** (`_ninja_preset_build.bat`).
- [ ] **Step 2: Run the live app under syncval** — `cmd.exe /c "set VIXEN_VULKAN_VALIDATION=1&& C:\cpp\VBVS--VIXEN\VIXEN\binaries\VIXEN.exe"` (default scene = the composite path). `taskkill /F /IM VIXEN.exe` to reap. Capture the validation log + a window screenshot.
- [ ] **Step 3: Confirm the gate** — the live composite (BodyInstanceRayMarch + HUD) renders, and **0 synchronization-validation errors** with the newly-firing baked image barriers (UNDEFINED→GENERAL acquire now baked-or-node-managed; confirm no double-transition with the render pass's GENERAL→PRESENT_SRC). A regression here = the baked image barrier conflicts with a still-node-managed transition → fix the conflict (the acquire transition may now be redundant with the baked barrier; reconcile per the spec's "minimal layout baking" decision: acquire stays node-managed, render pass keeps GENERAL→PRESENT_SRC, baked barriers cover only inter-pass image hazards).
- [ ] **Step 4: Record** the result in the Progress Log + memory.

---

## Out of scope (P5b / P6)
- Consuming `waitEdges`/`signalEdges` at submit (timeline signal/wait via `vkQueueSubmit2`) — **P5b**.
- Dropping `leaveImageInGeneral` + the binary composite handoff — **P5b**.
- The compile-time "no timeline on acquire/present" assert + dead-code cleanup — **P5b**.
- Multi-submit fan-in smoke (genuine timeline stress) — **P5b** (per the user decision to add it).
- Baking the WSI-lifecycle transitions (acquire UNDEFINED→GENERAL, present GENERAL→PRESENT_SRC) — stays node/render-pass-managed.

## Self-Review (done at authoring)
- **Spec coverage:** P5 design row "FrameSyncNode timeline + frameBase" → Tasks 1–3; "real swapchain Resource* identity / baked image barriers" → Tasks 4–6. Edge consumption + composite migration are explicitly P5b (split per user decision). ✓
- **Placeholder scan:** new pure logic (frameBase helper + tests) shown in full; lifts (capability-graph sync2 pattern, BatchedUploader timeline creation, config X-macro, swapchain-node lookup) are precise file:line cites for the milestone-pipeline implementers. ✓
- **Type consistency:** uses `FrameSyncSchedule.timelineValuesPerFrame`, `GetFrameSyncSchedule()`, `FrameSyncScheduler::Build(..., swapchainResource)`, `SubmitGroup.swapchainAcquireWait/swapchainPresentSignal` exactly as defined on main. New slots `TIMELINE_SEMAPHORE`/`TIMELINE_FRAME_BASE` defined in Task 3, consumed in P5b. ✓
- **Live-gate placement:** M1 is zero-behaviour-change (safe, no GPU gate needed); M2 changes the live path → explicit hands-on live syncval gate (Task 6). ✓

## Progress Log
- Milestone 1 (Tasks 1–3): DONE · commits `72987bc3`, `97d613ad` (Task 1 a no-op — `timelineSemaphore` was already enabled via the capability graph by prior BatchedUploader work; verified enabled end-to-end) · Opus validator APPROVED (8 checks; timeline semaphore final-teardown-only so the monotonic counter survives recompile, frameBase advances once/frame before publish + never reset in CompileImpl, ZERO behaviour change confirmed, test 2/2 + scheduler 10/10). FrameSyncNode → 5 output slots. **Build-env note:** a prior P4-debug agent had flipped `ENABLE_COVERAGE=ON` in the local `build-ninja` cache (`--coverage` corrupts objects on this MSVC toolchain → `test_timer`/`test_scene_generators` link-fail); reconfigured `ENABLE_COVERAGE=OFF` + full clean rebuild = **901/901 green** (repo CMake default is OFF — NOT a committed contamination). · 2026-06-22
- Milestone 2 (Tasks 4–6): DONE · commits `1274c516`, `01191473` · Opus validator APPROVED (6 checks; swapchain-node lookup null-safe — `GetOutput` bounds-checked, slot 1 = `SWAPCHAIN_PUBLIC`, headless→nullptr; scheduler test 11/11 non-vacuous; scope = only `RenderGraph.cpp` + test). **Live gate (Task 6, controller-driven): live composite renders + 0 syncval** (`m5_live.log`: render loop reached, zero SYNC-/VUID/hazard; the near-black visual is the pre-existing dark default body-octree scene, NOT M2). Note: on the current live paths compute is the swapchain's FIRST access (→ no baked entry barrier) and UIRenderNode does not replay the schedule yet, so M2 is effectively runtime-neutral — it lands the swapchain `Resource*` identity + the (unit-tested) image-barrier/tagging machinery as the FOUNDATION P5b consumes; the live gate confirms no regression. **P5a COMPLETE.** · 2026-06-22
