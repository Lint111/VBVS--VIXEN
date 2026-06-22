---
title: Automatic GPU sync (frame-graph barrier scheduling) — design note (AR#21+)
aliases: [auto-sync, barrier scheduler, frame graph, GPU-GPU passing, AR#21 timeline]
tags: [architecture, design, rendergraph, synchronization, AR21, parked]
created: 2026-06-14
status: SUPERSEDED (scope) by [[Auto-Sync-FrameGraph-Inc1-Design-2026-06]] — that spec merges this note's increments 1+2 into one Tiers-1+2 deliverable (2026-06-21). This note remains the audit/background.
related:
  - "[[RenderGraph]]"
  - "[[Maturation-Backlog-2026-06]]"
  - "[[Instancing-Increment-Design-2026-06]]"
---

# Automatic GPU sync (frame-graph barrier scheduling) — design note

> [!note] Increment 1 is now designed — see [[Auto-Sync-FrameGraph-Inc1-Design-2026-06]]
> The 2026-06-21 brainstorming merged this note's **increment 1** (intra-submit barriers) and
> **increment 2** (multi-submit/timeline) into a single **Tiers 1+2** deliverable, with a centralized,
> loop-aware `FrameSyncScheduler` and an internal P1–P6 phasing. This note remains the original
> audit/background; the detailed, implementation-ready design lives in the linked spec.

**Motivation (user, 2026-06-14):** "make nodes have deferred outputs/inputs so we can pass inner GPU
process passing — compute → compute → render without GPU↔CPU readback." Chosen direction: **automatic
barriers/sync for chains** — the graph should track each pass's resource reads/writes and insert the
right pipeline barriers + image-layout transitions + submit/semaphore sync so multi-pass chains are
correct automatically.

> **Key reframe:** GPU→CPU→GPU readback is *already avoided* — connections pass GPU **handles**
> (`VkBuffer`/`VkImage`/views/descriptor sets) wired at compile time; bytes never leave the GPU. The
> voxel path (`VoxelGridNode` SSBOs → `ComputeDispatchNode` ray-march → swapchain → present) is fully
> on-GPU, zero readback (only `DebugBufferReaderNode` reads back, and it's opt-in debug). **The missing
> piece is not data passing — it's automatic GPU *synchronization* of dependent passes.**

## Current-state audit (2026-06-14)

| Aspect | Today | Manual? | Centralized? |
|--------|-------|---------|--------------|
| Barriers | hardcoded per-node (`ComputeDispatchNode` UNDEFINED→GENERAL→PRESENT, **old `vkCmdPipelineBarrier` API**); `MultiDispatchNode::InsertAutoBarrier` auto-inserts compute→compute via **`vkCmdPipelineBarrier2`** (opt-in, node-local) | yes | no (except within MultiDispatchNode) |
| Image layouts | per-node manual (compute) or render-pass-implicit (`VkAttachmentDescription` initial/final) | yes | no layout-state model |
| Resource read/write | **`SlotMutability {ReadOnly,WriteOnly,ReadWrite}` declared but UNUSED** (`// TODO: check SlotMutability` in `ResourceAccessTracker.cpp` + `VirtualResourceAccessTracker.cpp`); `ResourceAccessTracker` tracks read/write per node **only for parallel-exec conflict detection, not GPU sync** | declared | tracked, not used for sync |
| Submits | each leaf node does its **own `vkQueueSubmit`**; binary semaphores only; one producer→consumer chain | yes | no |
| Timeline semaphores / events | **none** (all binary) | — | — |
| Execution order | topological sort at compile, static across frames | — | yes (topo) |

**Why the app has 0 validation errors today:** the live graph is essentially **linear**
(single-writer→single-reader), so topo order + the one fence/semaphore chain is sufficient. There are no
cross-node RAW/WAR/WAW hazards yet. They appear the moment a real compute→compute hazard or a second
submitting pass is added — exactly the multi-entity / multi-view / GPU-driven future.

**This is "finish a half-built system," not greenfield:** the access model (`SlotMutability` +
`ResourceAccessTracker`) exists, and the barrier mechanism (`vkCmdPipelineBarrier2` in
`MultiDispatchNode`) is proven. They're just not connected.

## Proposed design — a barrier scheduler

At **compile**, walk the topological order + each node's read/write set (from `ResourceAccessTracker`
completed with `SlotMutability`) to:
1. Detect hazards per resource (RAW / WAR / WAW) between producer and consumer passes.
2. Compute required image-layout transitions from a **centralized layout-state model** (track each
   image's current layout; emit transitions only when it actually changes — kills redundant transitions).
3. Produce a **barrier schedule** keyed by (consumer pass, resource) with src/dst stage + access masks.

At **execute/record**, inject `vkCmdPipelineBarrier2` at consumer record points from the schedule,
replacing the hardcoded per-node transitions. Standardize **all** barriers on `vkCmdPipelineBarrier2`.

**Barriers vs submits are coupled:** two dependent passes in the *same* submit need a **barrier**; in
*different* submits they need a **semaphore**. So the scheduler must know the submit grouping — which is
why this splits into two increments.

## Increments

- **Increment 1 — auto-barriers from the access model** (intra-submit + centralized image-layout state).
  Complete the unused `SlotMutability`→barrier path; emit `barrier2` + layout transitions for hazards
  between passes recorded into the same command buffer/submit; replace `ComputeDispatchNode`'s hardcoded
  transitions; standardize on `barrier2`. Makes a compute→compute→render chain authored as a sequence
  correct without manual barriers. **Contained, high-value, builds on existing infra.**
- **Increment 2 — multi-submit / timeline semaphores [AR#21].** Replace the single-binary-semaphore
  model so independent submitting passes compose across submits/queues. The backlog names the fix
  ("Sprint 8 `TimelineNode`"). Bigger and riskier; do after increment 1.

## Highest-leverage files (from the audit)

1. `libraries/RenderGraph/include/Core/ResourceAccessTracker.h` / `src/Core/ResourceAccessTracker.cpp`
   — extend to generate a barrier schedule; complete the `SlotMutability` ReadWrite check (the TODO).
2. `libraries/RenderGraph/src/Core/RenderGraph.cpp` (`Compile` ~L533 builds the access tracker;
   `RenderFrame`/`Execute` ~L553-588 / ~L774-850) — hook schedule creation post-compile; hook barrier
   injection in execute.
3. `libraries/RenderGraph/src/Nodes/ComputeDispatchNode.cpp` (`RecordComputeCommands` ~L278-346,
   `TransitionImageTo*` ~L352-477) — consume the auto schedule instead of hardcoded transitions; the
   reference for the modern API is `MultiDispatchNode::InsertAutoBarrier` (`MultiDispatchNode.cpp:645`).
4. `libraries/RenderGraph/include/Data/DispatchPass.h` — extend to carry implicit barrier/access metadata.

## Quick wins notable during the audit

- Complete the unused `SlotMutability` ReadWrite check (refuses unsafe parallel execution of RAW pairs).
- Standardize `ComputeDispatchNode` onto `vkCmdPipelineBarrier2` (MultiDispatchNode already uses it).
- A central image-layout cache to skip redundant transitions.

## Relationship to the backlog

This subsumes **AR#21** (sync model: >1 submitting pass per frame) and underpins P3 multi-view, GPU-driven
draw (`vkCmdDrawIndirect`), and any compute→compute→render effect chain. It's a frame-graph-sized epic —
give it its own session runway.
