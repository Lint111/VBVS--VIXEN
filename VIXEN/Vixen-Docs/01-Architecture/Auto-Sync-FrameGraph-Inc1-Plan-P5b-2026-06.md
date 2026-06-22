---
title: Auto-Sync FrameGraph — P5b Implementation Plan (Timeline edge consumption + composite migration + fan-in proof)
aliases: [auto-sync P5b, timeline submit, composite migration, fan-in smoke, AR#21 P5b]
tags: [architecture, plan, rendergraph, synchronization, AR21, framegraph, timeline]
created: 2026-06-22
status: PLAN — ready for milestone-pipeline execution
related:
  - "[[Auto-Sync-FrameGraph-Inc1-Design-2026-06]]"
  - "[[Auto-Sync-FrameGraph-Inc1-Plan-P5a-2026-06]]"
  - "[[live-verification-authoritative-for-gpu-work]]"
---

# Auto-Sync FrameGraph — P5b Implementation Plan (Timeline edge consumption + composite migration + fan-in proof)

> **For agentic workers:** execute via the **post-brainstorm-context-manager** milestone pipeline. Clean-commit gate. **CRITICAL — [[live-verification-authoritative-for-gpu-work]]: every milestone here changes GPU submit behaviour; NONE is done until it has actually run with sync validation on. A wrong `frameBase`/offset DEADLOCKS (a hang, not a syncval error) — only a live run catches it. The live gates are controller-driven.**

**Goal:** Consume the baked timeline edges at submit (`vkQueueSubmit2`), prove the timeline on a genuine multi-submit fan-in, then migrate the live composite path off `leaveImageInGeneral` + the binary compute→UI handoff onto the scheduler-derived timeline edge — all at **0 syncval**.

**Architecture:** P5a landed the primitive (`FrameSyncNode` timeline semaphore + per-frame `frameBase`, published via slots) and the swapchain `Resource*` identity (so the scheduler bakes the compute→UI `SyncEdge` + per-group `waitEdges`/`signalEdges`). P5b makes submitting nodes *consume* them: each node resolves its group's edges to `(timelineSemaphore, timelineOffset + frameBase)` and adds them to a `vkQueueSubmit2`. We keep the binary handoff in place first (additive, M1), prove the timeline on a fan-in demo (M2), then remove the binary handoff so the timeline alone orders compute→UI (M3), then assert/clean up (M4). WSI acquire/present stay **binary**.

**Tech Stack:** C++23, Vulkan 1.3 (`synchronization2` + `timelineSemaphore` enabled). `vkQueueSubmit2` + `VkSemaphoreSubmitInfo` (timeline value inline). GoogleTest. Build = `cmd.exe /c _ninja_preset_build.bat` FOREGROUND (`timeout: 600000`). **Build dir is `ENABLE_COVERAGE=OFF` (P5a fixed a contamination) — if `test_timer`/`test_scene_generators` ever link-fail with "corrupt COFF"/coverage, STOP: the cache got re-contaminated.**

---

## Context (from P5 investigation + P5a)

- **Edges are baked + on the live path now (P5a M2).** `FrameSyncScheduler::Build(..., swapchainResource)` populates `SyncEdge{fromGroup,toGroup,resource,timelineOffset}` + `SubmitGroup.signalEdges`/`waitEdges` (indices into `FrameSyncSchedule::edges`) + `timelineValuesPerFrame`. The live composite already has a baked compute→UI edge (just not consumed).
- **`FrameSyncNode` publishes (P5a M1):** output slots `TIMELINE_SEMAPHORE` (VkSemaphore) + `TIMELINE_FRAME_BASE` (uint64_t, advanced once/frame before publish, survives recompile).
- **Submit ownership:** `ComputeDispatchNode` (`vkQueueSubmit` ~:246-269) and `UIRenderNode` (~:263-271) each own a submit; both read `GetOwningGraph()->GetFrameSyncSchedule()` + `FindGroupForNode(sched,this)` (ComputeDispatchNode already does, ~:341).
- **Composite handoff to remove (M3):** `ComputeDispatchNode` `PARAM_LEAVE_IMAGE_IN_GENERAL=true` (`BuildRenderGraph.cpp:613`); the binary `renderComplete→COMPOSITE_WAIT_SEMAPHORE` connection (`BuildRenderGraph.cpp:1136-1137`); UI render pass owns GENERAL→PRESENT_SRC via attachment ops (`BuildRenderGraph.cpp:615-620` — KEEP this).
- **Decisions (locked):** `vkQueueSubmit2` (not legacy + `VkTimelineSemaphoreSubmitInfo`); minimal layout baking (acquire stays node-managed, render pass keeps GENERAL→PRESENT_SRC); UI's `SWAPCHAIN_INFO` gets a GENERAL-layout AccessKind so no redundant transition is baked (M3); fan-in smoke included.
- **THE deadlock hazard:** compute signals value `V=offset+frameBase`; UI waits `V`. If they read different `frameBase`, UI waits a value never signalled → **hang**. P5a publishes `frameBase` once/frame before consumers read it, so they must read the same value — verify live (M1).

## Reference implementations (lift-and-adapt)

| Need | Read & follow (file:line) |
|---|---|
| Current binary submit to convert to `vkQueueSubmit2` | `ComputeDispatchNode.cpp:246-269`; `UIRenderNode.cpp:263-271` |
| Read baked group + edges | `ComputeDispatchNode.cpp:341-342` (`GetFrameSyncSchedule`+`FindGroupForNode`); `SubmitGroup.waitEdges/signalEdges`, `SyncEdge.timelineOffset` (`Core/FrameSyncSchedule.h`) |
| FrameSyncNode timeline slots to connect | `FrameSyncNodeConfig.h` outputs `TIMELINE_SEMAPHORE`, `TIMELINE_FRAME_BASE` (P5a) |
| Env-gated multi-node demo graph (clone for fan-in) | `application/main/source/graph/BuildAutoSyncDemoGraph.cpp` (P4) + env dispatch `BuildRenderGraph.cpp:87-114` |
| Composite wiring to edit | `BuildRenderGraph.cpp:613-620, 1097-1137` |

---

## Milestone Map

> Confirm before execution; do not re-segment on resume. **Order is de-risked: prove the timeline on a fan-in BEFORE migrating the live composite.**

- **M1 ✅ DONE — Edge consumption at submit (additive).** `ComputeDispatchNode` + `UIRenderNode` migrate to `vkQueueSubmit2` and add timeline signal/wait from their group's `signalEdges`/`waitEdges` (value = `offset + frameBase`), reading the FrameSyncNode timeline slots. **Keep the binary handoff** (additive → behaviour unchanged + a safety net). *Gate:* build; **LIVE syncval — live composite renders, 0 syncval, NO hang** (timeline values correct).
- **M2 — Fan-in smoke demo (the genuine timeline proof).** New `VIXEN_FANIN_DEMO` graph: 2 independent compute nodes (each its own submit/group) write 2 buffers → 1 consumer node (its own submit) waits BOTH via 2 timeline waits → present. *Gate:* build; **LIVE — fan-in renders + 0 syncval** (proves 2-wait fan-in timeline works, which the 1-edge live composite can't).
- **M3 — Composite migration off `leaveImageInGeneral`.** Drop `PARAM_LEAVE_IMAGE_IN_GENERAL` + the binary `renderComplete→COMPOSITE_WAIT_SEMAPHORE` connection; the compute→UI timeline edge (proven in M1/M2) now solely orders the two submits. Declare UI `SWAPCHAIN_INFO` AccessKind (GENERAL color-attachment) so no redundant baked transition; render pass keeps GENERAL→PRESENT_SRC. *Gate (the P5b gate):* **LIVE — live composite renders + 0 syncval via the timeline edge.**
- **M4 — WSI assert + cleanup.** Compile-time assert "no timeline on acquire/present" in `FrameSyncScheduler::Build`; remove now-dead binary-handoff code. *Gate:* build + unit; **final LIVE regression** (live composite + fan-in + standalone voxel, all 0 syncval).

---

## Tasks

### Task 1: `vkQueueSubmit2` + timeline edge consumption (M1)

**Files:** `ComputeDispatchNode.{cpp,h}` + config; `UIRenderNode.{cpp,h}` + config; `BuildRenderGraph.cpp` (wire slots)

- [ ] **Step 1: Add timeline input slots.** To `ComputeDispatchNodeConfig.h` and `UIRenderNodeConfig.h` add input slots `TIMELINE_SEMAPHORE_IN` (VkSemaphore) + `TIMELINE_FRAME_BASE_IN` (uint64_t). Copy the X-macro shape from existing input slots; bump INPUTS + static_asserts.
- [ ] **Step 2: Wire them** in `BuildRenderGraph.cpp` — connect `FrameSyncNode::TIMELINE_SEMAPHORE` → both nodes' `TIMELINE_SEMAPHORE_IN`, and `TIMELINE_FRAME_BASE` → `TIMELINE_FRAME_BASE_IN` (FrameSyncNode is already upstream of both — see the existing FrameSync wiring ~:1095-1129).
- [ ] **Step 3: Convert ComputeDispatchNode's submit to `vkQueueSubmit2`.** Replace the `VkSubmitInfo`/`vkQueueSubmit` block (~:246-269) with `VkSubmitInfo2` + `VkCommandBufferSubmitInfo` + `VkSemaphoreSubmitInfo[]`. Binary waits/signals (imageAvailable/renderComplete) become `VkSemaphoreSubmitInfo{semaphore, value=0, stageMask}`. Then add timeline SIGNALS: for each `idx` in `group.signalEdges`, append `VkSemaphoreSubmitInfo{timelineSem, value=schedule.edges[idx].timelineOffset + frameBase, stageMask=COMPUTE_SHADER_BIT}`. (Compute is a producer → it signals.)
- [ ] **Step 4: Convert UIRenderNode's submit to `vkQueueSubmit2`** similarly, adding timeline WAITS: for each `idx` in `group.waitEdges`, append `VkSemaphoreSubmitInfo{timelineSem, value=schedule.edges[idx].timelineOffset + frameBase, stageMask=FRAGMENT_SHADER_BIT|COLOR_ATTACHMENT_OUTPUT_BIT}`. **Keep the existing binary `COMPOSITE_WAIT_SEMAPHORE` wait too (additive).** Read `frameBase`/`timelineSem` from the new input slots.
- [ ] **Step 5: Build green** (`_ninja_preset_build.bat`).
- [ ] **Step 6: Commit** — `git commit -m "feat(rendergraph): consume baked timeline edges via vkQueueSubmit2 (additive) (auto-sync P5b M1)"`

### Task 2: LIVE gate M1 (HANDS-ON)
- [ ] Run the live app under `VIXEN_VULKAN_VALIDATION=1`; confirm: renders, **0 syncval, NO hang** (the additive timeline edge resolves to a value compute actually signals). A hang here = `frameBase` mismatch between compute and UI → debug the publish/read ordering (P5a's `frameBase` advance). Record result.

### Task 3: Fan-in smoke demo (M2)

**Files:** Create `application/main/source/graph/BuildFanInDemoGraph.cpp` + shaders; modify `BuildRenderGraph.cpp` (env dispatch) + the app header.

- [ ] **Step 1: Author the graph** — clone `BuildAutoSyncDemoGraph.cpp`'s self-contained infra. Two independent `ComputeDispatchNode`s (or compute-pipeline+dispatch nodes) `fanin_a` and `fanin_b`, each writing its own storage buffer (`bufA`, `bufB`), each its OWN submit (each is its own SubmitGroup). One consumer node that reads BOTH `bufA` and `bufB` (declares 2 input accesses) and writes the swapchain → present. The scheduler bakes 2 `SyncEdge`s (A→consumer, B→consumer) → the consumer group has 2 `waitEdges` → 2 timeline waits. This is the fan-in the live composite can't exercise.
- [ ] **Step 2: Shaders** — `fanin_a.comp`/`fanin_b.comp` write distinct patterns to their buffers; the consumer (compute or fullscreen-fragment) combines both (e.g. `out = a + b`) so a missing wait → visibly wrong/garbage.
- [ ] **Step 3: Env dispatch** — `VIXEN_FANIN_DEMO` in `BuildRenderGraph.cpp` (~:87-114), mirroring `VIXEN_AUTOSYNC_DEMO`.
- [ ] **Step 4: Build green.**
- [ ] **Step 5: Commit** — `git commit -m "feat(app): multi-submit fan-in timeline demo (auto-sync P5b M2)"`

### Task 4: LIVE gate M2 (HANDS-ON)
- [ ] Run `cmd.exe /c "set VIXEN_FANIN_DEMO=1&& set VIXEN_VULKAN_VALIDATION=1&& C:\cpp\VBVS--VIXEN\VIXEN\binaries\VIXEN.exe"`; screenshot; reap. Confirm: the combined (a+b) output renders correctly (both producers' data present → both timeline waits honoured), **0 syncval, no hang**. This is the genuine Tier-2 timeline proof. Record + screenshot.

### Task 5: Composite migration off `leaveImageInGeneral` (M3)

**Files:** `BuildRenderGraph.cpp` (:613, :1136-1137); `UIRenderNodeConfig.h` (SWAPCHAIN_INFO AccessKind); possibly `BarrierTypes.h` (new GENERAL color kind)

- [ ] **Step 1: UI AccessKind.** Give UIRenderNode's `SWAPCHAIN_INFO` an AccessKind that resolves to layout GENERAL + stage COLOR_ATTACHMENT_OUTPUT (so the baked schedule does NOT insert a redundant layout transition — the render pass already loads from GENERAL). If no existing `AccessKind` fits, add `ColorAttachmentWriteGeneral` to `BarrierTypes.h` (stage COLOR_ATTACHMENT_OUTPUT, access COLOR_ATTACHMENT_WRITE, layout GENERAL) + its `ResolveAccess`/`AccessWrites` arms.
- [ ] **Step 2: Drop the hacks.** Set `PARAM_LEAVE_IMAGE_IN_GENERAL=false` (or remove it) at `BuildRenderGraph.cpp:613`; REMOVE the binary `renderComplete→COMPOSITE_WAIT_SEMAPHORE` connection (:1136-1137) and the now-unused `COMPOSITE_WAIT_SEMAPHORE` wait in UIRenderNode (the timeline edge from M1 now orders compute→UI). Keep the UI render pass's GENERAL→PRESENT_SRC attachment ops.
- [ ] **Step 3: Build green.**
- [ ] **Step 4: Commit** — `git commit -m "feat(rendergraph): migrate composite off leaveImageInGeneral to timeline edge (auto-sync P5b M3)"`

### Task 6: LIVE gate M3 — the P5b gate (HANDS-ON)
- [ ] Run the live app under validation; confirm the live composite (BodyInstanceRayMarch + HUD) renders correctly with compute→UI ordered SOLELY by the timeline edge (binary handoff gone), **0 syncval, no hang/tearing**. If it hangs → the timeline edge isn't correctly signalled/waited without the binary net → debug. Record + screenshot.

### Task 7: WSI assert + cleanup (M4)

**Files:** `FrameSyncScheduler.cpp` (the assert); remove dead binary-handoff code

- [ ] **Step 1: Assert.** In `FrameSyncScheduler::Build`, after swapchain-adjacency tagging, assert no timeline edge targets a swapchain-acquire/present binary point (the spec's WSI law). A small unit test in `test_frame_sync_scheduler.cpp` for a swapchain-adjacent graph.
- [ ] **Step 2: Remove dead code** — the `COMPOSITE_WAIT_SEMAPHORE` slot/param if fully unused, `PARAM_LEAVE_IMAGE_IN_GENERAL` if removed, and any now-dead transition branches. Build green + unit.
- [ ] **Step 3: Commit** — `git commit -m "feat(rendergraph): WSI timeline-law assert + remove dead binary-handoff code (auto-sync P5b M4)"`

### Task 8: LIVE final regression (HANDS-ON)
- [ ] Run all three under validation: live composite, `VIXEN_FANIN_DEMO`, and (if it still exists) the standalone voxel path — all render + **0 syncval**. Record. This closes P5b.

---

## Out of scope (P6)
- Full fan-in production graph / generalized multi-submit composition beyond the smoke demo.
- Inter-loop (Tier-3) cadence sync.
- Baking the WSI-lifecycle transitions (acquire UNDEFINED→GENERAL, present GENERAL→PRESENT_SRC stay node/render-pass-managed).

## Self-Review (done at authoring)
- **Spec coverage:** P5 design row "Tier-2 timeline: FrameSyncNode timeline + frameBase (P5a); inter-group edges; migrate composite off leaveImageInGeneral" → Tasks 1/5; the user-added fan-in proof → Tasks 3-4; WSI assert → Task 7. ✓
- **Placeholder scan:** submit-conversion + fan-in described with concrete `VkSubmitInfo2`/`VkSemaphoreSubmitInfo` shape + value formula + precise cites; implementers read the cited submit blocks. ✓
- **Type consistency:** `SubmitGroup.signalEdges/waitEdges`, `SyncEdge.timelineOffset`, `FrameSyncSchedule.edges`, `TIMELINE_SEMAPHORE`/`TIMELINE_FRAME_BASE` slots all as defined by P2/P5a. ✓
- **Live-gate placement:** EVERY behaviour-changing milestone (M1/M2/M3/M4) has an explicit hands-on live gate — a `frameBase` deadlock is a hang, invisible to build/unit/static review. ✓

## Progress Log
- Milestone 1 (Task 1 + live gate Task 2): DONE · commits `15cb43a2` (vkQueueSubmit2 + additive timeline edge consumption) + `1011a49f` (fix) · Opus code-validator APPROVED (deadlock-safety: compute signal value ≡ UI wait value), THEN **the live gate caught a bug static review missed**: `VUID-VkSubmitInfo2-semaphore-03882` — ComputeDispatchNode signaled the same timeline value 3× per submit (all of a producer's `signalEdges` share `timelineOffset = producer.groupId`). Fixed by deduping signal values (one `VkSemaphoreSubmitInfo` per distinct value; consumer waits unchanged). **Live re-verified: 0 VUID-03882 (was 20), 0 syncval/hazard, render loop reached, "Exiting normally" (no hang); instrumented `timelineValuesPerFrame=29`, frameBase advances 29→58→87.** Tests: scheduler 11/11, timeline 2/2. Binary handoff still present (additive — removed in M3). · 2026-06-22
