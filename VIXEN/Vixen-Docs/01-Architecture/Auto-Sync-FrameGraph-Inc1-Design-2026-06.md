---
title: Auto-Sync FrameGraph — Increment 1 Design (Tiers 1+2)
aliases: [auto-sync inc 1, framegraph sync scheduler, FrameSyncScheduler, AR#21 inc 1]
tags: [architecture, design, rendergraph, synchronization, AR21, framegraph, spec]
created: 2026-06-21
status: DESIGN — approved in brainstorming, pending spec review → implementation plan
related:
  - "[[Auto-Sync-FrameGraph-Design-2026-06]]"
  - "[[RenderGraph]]"
  - "[[Maturation-Backlog-2026-06]]"
  - "[[Subgraph-As-Node-Design-2026-06]]"
supersedes-scope-of: "Auto-Sync-FrameGraph-Design-2026-06 — this merges its increment 1 (intra-submit barriers) and increment 2 (multi-submit/timeline) into a single Tiers-1+2 deliverable, per the 2026-06-21 brainstorming decision."
---

# Auto-Sync FrameGraph — Increment 1 Design (Tiers 1+2)

> **One-line:** the node declares *resource-usage intent*; a centralized, loop-aware `FrameSyncScheduler`
> bakes a structured sync schedule at compile (intra-group `vkCmdPipelineBarrier2` + inter-group
> **timeline** semaphore edges); nodes *replay* it at execute. Replaces every hand-rolled transition
> and the brittle `leaveImageInGeneral` composite handoff.

This is the detailed design for the first build of the auto-sync frame graph. It supersedes the
*scope split* in the parked note [[Auto-Sync-FrameGraph-Design-2026-06]] (which separated intra-submit
barriers from multi-submit/timeline). The 2026-06-21 brainstorming decided to build **both tiers now**.

## Scope decisions (brainstorming, 2026-06-21)

| Decision | Choice | Why |
|---|---|---|
| Deliverable | **Centralize the foundation + a working multi-pass chain** (orig. options 1+2) | user wants the real payoff, not just plumbing |
| Multi-pass command orchestration | **A node**, by generalizing `MultiDispatchNode` (not graph-core surgery) | user: *"I always prefer node architecture from external systems"*; `MultiDispatchNode` is already this pattern |
| Sync tiers built | **Tiers 1 + 2** (intra-group barriers **and** inter-group timeline semaphores) | pulls AR#21 multi-submit forward; pure over incremental |
| Context structure | **Loop/group-aware + rebuildable**, not a flat baked-once schedule | future runtime graph mutation + multi-loop (UI/sim/…) must not force a rewrite |

## Motivation

From the parked note: the renderer must synchronize dependent GPU passes automatically — compute →
compute → render with no readback and no hand-written barriers. Today sync is hardcoded per node and
works only because the live graph is essentially linear. The moment a real cross-pass hazard or a
second submitting pass appears (multi-entity, multi-view, GPU-driven draw, deep-sim change bridge),
the manual model breaks. This closes the half-built access model (`SlotMutability` +
`ResourceAccessTracker`) and the proven-but-local barrier mechanism (`vkCmdPipelineBarrier2` in
`MultiDispatchNode`) into one frame-graph sync compiler.

## Current-state grounding (verified 2026-06-21, file:line)

| Fact | Location |
|---|---|
| Graph calls `node->Execute()` with `VK_NULL_HANDLE`; **nodes own their command buffers + submits** | `RenderGraph.cpp:851` |
| `ResourceAccessTracker` built at compile, but feeds **only** `WaveScheduler` parallel-conflict detection | `RenderGraph.cpp:534`, `WaveScheduler.h` |
| `SlotMutability {ReadOnly,WriteOnly,ReadWrite}` declared… | `ResourceConfig.h:149`, `SlotFields.h:50` |
| …but **unused for sync** (`TODO Phase 1: Check SlotMutability`) | `ResourceAccessTracker.cpp:92` |
| Slot metadata has `resourceType` + `mutability` but **no Vulkan stage/access/layout semantics** | `SlotFields.h:45-59` |
| `ComputeDispatchNode` hand-rolls `UNDEFINED→GENERAL→PRESENT` via **old `vkCmdPipelineBarrier`** | `ComputeDispatchNode.cpp:370,470` |
| `MultiDispatchNode::InsertAutoBarrier` uses modern `vkCmdPipelineBarrier2`; `DispatchBarrier` holds barrier2 vectors | `MultiDispatchNode.cpp:~645`, `DispatchPass.h:81` |
| `Resource::GetHandle<VkImage>()` resolves the runtime handle (type-erased); **no layout state tracked anywhere** | `CompileTimeResourceSystem.h:739,979` |
| **Single queue** — all submits go to `device->queue` (one graphics queue) | `VulkanDevice.h:58`, `VulkanDevice.cpp:249-262` |
| Per-frame sync: `FrameSyncNode` owns per-flight fences + `imageAvailable` semaphores; advances `currentFrameIndex` (mod `MAX_FRAMES_IN_FLIGHT=4`); waits the flight fence at frame start | `FrameSyncNode.cpp:64,90,111,117` |
| `SwapChainNode` owns per-**image** `renderComplete` binary semaphores; acquire signals per-**flight** `imageAvailable` | `SwapChainNode.cpp:290,502` |
| `PresentNode` `vkQueuePresentKHR` waits per-image `renderComplete` | `PresentNode.cpp:129` |
| **Composite path is already 2-submit**: ComputeDispatch (`leaveImageInGeneral`) → UIRenderNode (`compositeWaitSemaphore`→signals `uiComplete[image]`, owns fence) → Present | `ComputeDispatchNode.cpp:268`, `UIRenderNode.cpp:271` |
| **Timeline semaphores already proven in-repo** (`VK_SEMAPHORE_TYPE_TIMELINE`), but only for async upload, not render sync | `BatchedUploader.cpp:357-375` |
| Multi-loop infra exists: `LoopManager` (N loops, independent cadence), `LoopReference`, `LoopBridgeNode`; `WaveScheduler::Recompute()` documented *"call when graph structure changes"* | `LoopManager.h`, `LoopBridgeNodeConfig.h`, `WaveScheduler.h:125` |

Two consequences drive the design:
1. **Single queue ⇒** cross-submit sync is semaphores only — no queue-family ownership transfers.
2. **WSI hard constraint ⇒** `vkAcquireNextImageKHR`/`vkQueuePresentKHR` require **binary** semaphores;
   timeline semaphores are illegal there. The sync model is therefore *hybrid*.

## Architecture

### Layered model: declare → schedule → replay

- **Declarative (node/slot):** each access declares a `ResourceUsage` carrying the missing Vulkan
  semantics. *Node declares intent; it does not compute sync.* (Mirrors how `device` is ambient node
  state — but sync logic is cross-node, so it is **not** a node member.)
- **Analysis (compile, centralized):** `FrameSyncScheduler` consumes `(topology, accessTracker, loops)`
  and bakes the structured schedule. Pure analysis — **not** command orchestration, so it does not
  violate the node-architecture preference.
- **Replay (execute, nodes):** each submit-group node replays its baked barrier list + signals/waits
  its assigned timeline values. Command orchestration stays in nodes; `RenderGraph::Execute` is
  unchanged (`node->Execute()` per node).

Because execution order is static between recompiles, **layouts and timeline offsets are computed at
compile and baked**; execute only fills runtime handles (`GetHandle`/`GetImage(imageIndex)`) and adds
the per-frame timeline base. No shared mutable runtime layout state ⇒ no cross-node runtime coupling.

### Components

| # | Component | New / changed | Role |
|---|---|---|---|
| 1 | `ResourceUsage` enum + mapping table | new `Core/BarrierTypes.h` | `StorageImageWrite`, `StorageImageReadWrite`, `SampledImageRead`, `ColorAttachmentWrite`, `StorageBufferRead/Write`, `PresentSrc`, `TransferRead/Write`, … each → `{VkPipelineStageFlags2, VkAccessFlags2, VkImageLayout}`. Default `Auto` derives from `resourceType`+`mutability` so existing slots compile unchanged. |
| 2 | `ResourceAccessTracker` | complete TODO `:92`; carry `ResourceUsage` per access | correct `ReadWrite` tracking (WAR/WAW) + usage semantics |
| 3 | **`FrameSyncScheduler`** | new `Core/FrameSyncScheduler.{h,cpp}` | the centralized, structured, loop-aware context builder + `Rebuild()` seam |
| 4 | Submit-group model | new concept (data only) | the unit that owns one command buffer + one submit |
| 5 | `FrameSyncNode` | extended | owns the per-loop **timeline** semaphore; assigns the per-frame value base |
| 6 | `MultiDispatchNode` (generalized) + `ComputeDispatchNode` (barrier-replay) | changed | Tier-1 consumers; extract a reusable pass-recording core so the graphics path inherits no benchmark baggage |
| 7 | Composite-path migration | changed | drop `leaveImageInGeneral`; compute→UI→present becomes scheduler-derived edges (Tier-2 proof) |

### The context — `FrameSyncSchedule` (build product)

The "centralized per dispatch-group **and** inter-dispatch-node, loop-aware" structure:

```
FrameSyncSchedule {
  topologyVersion
  loops: [ LoopSchedule {
      loopId
      submitGroups: [ SubmitGroup {
          groupId, loopId, passes[]
          intraBarriers[]            // Tier 1: vkCmdPipelineBarrier2 within this submit
          waits[]  : [ {semaphore, valueOffset} ]   // Tier 2: timeline waits
          signals[]: [ {semaphore, valueOffset} ]   // Tier 2: timeline signals
          swapchainAdjacent: {wantsAcquireWait, wantsPresentSignal}  // binary WSI wiring
      } ]
  } ]
  interGroupEdges: [ SyncEdge { fromGroup, toGroup, resource, valueOffset } ]   // Tier 2/3
}
```

- **Tier 1** = `intraBarriers` (barriers inside one submit).
- **Tier 2** = `SyncEdge`s between groups in the same loop → timeline signal/wait offsets.
- **Tier 3** = `SyncEdge`s crossing loops → modeled now (carry `loopId`), cadence handling deferred.

### Sync mechanism — hybrid timeline + binary

- **Intra-group:** `vkCmdPipelineBarrier2` (Tier 1).
- **Internal inter-group:** one **timeline** semaphore per loop, monotonic values. Fan-in (group C
  waits on A *and* B) is two waits — binary cannot express it cleanly. This is the AR#21 payoff.
- **Swapchain-adjacent boundaries:** first internal group waits binary `imageAvailable[frame]`; last
  internal group *also* signals binary `renderComplete[image]` for Present (alongside its timeline
  signal). WSI stays binary; everything internal is timeline.
- **Submit grouping (initial rule):** 1 node = 1 group; a pass-group node = 1 group of N passes.
  Merging independent nodes into one submit is a **future optimization**, out of scope here.

### Integration with existing infra (no reinvention)

- `FrameSyncScheduler` is built in `RenderGraph::Compile` right after the access tracker
  (`RenderGraph.cpp:534`), beside `WaveScheduler`, from the same inputs. Rebuilt through a
  `Recompute()`-style seam (the pattern already exists on `WaveScheduler`; device-loss already triggers
  a full graph rebuild; the runtime graph-edit *trigger* is the Sprint-8 mod API — future).
- `FrameSyncNode` (already the per-frame sync-primitive owner) gains the timeline semaphore + per-frame
  value base. Fence/acquire ownership is unchanged.
- The composite path drops `leaveImageInGeneral`; the compute→UI→present handoff becomes one internal
  timeline edge + the binary present edge, both scheduler-derived.

## Compile algorithm (`FrameSyncScheduler::Build`)

1. **Group assignment** — each node → own submit group; pass-group node → one group of N passes. Tag
   each group with its `loopId` (loop propagation).
2. **Per-resource ordered access list** — walk topo order; emit `(group, pass, usage{stage2,access2,
   layout})` per access, maintaining a per-image **running layout simulation** (start `UNDEFINED`;
   swapchain image `PRESENT_SRC`/`UNDEFINED` on first use).
3. **Hazard classification** — for each producer→consumer pair with RAW/WAR/WAW or a layout change:

   | Producer/consumer | Image | Buffer |
   |---|---|---|
   | **Same group** | intra-group `VkImageMemoryBarrier2` (old→new from sim; stages/access from usages) | `VkBufferMemoryBarrier2` / `VkMemoryBarrier2` |
   | **Different group** | timeline `SyncEdge` **+** layout-transition barrier recorded at the **consumer** group start (`old`=producer-final, `new`=consumer-needs) | timeline `SyncEdge` only (semaphore covers visibility; no layout) |

4. **Timeline value assignment (4-frame ring)** — the schedule bakes **relative offsets** (group
   ordinal within a frame); at execute `FrameSyncNode` adds a monotonic `frameBase` so values never
   collide across overlapping in-flight frames. Compile bakes offsets, runtime resolves values (same
   split as layouts). The flight **fence** still gates CPU pacing; the **timeline** gates intra-frame
   GPU ordering.
5. **Swapchain-adjacent binary wiring** — groups touching the swapchain `IRenderTarget` are tagged: the
   first waits `imageAvailable[frame]`; the last also signals `renderComplete[image]` for Present.

## Error handling / edge cases

- **WSI law** — compile-time assert: no timeline semaphore reaches acquire/present (checked on
  swapchain-adjacent groups).
- **Resize / swapchain recreate** — image-count/extent change → `topologyVersion` bump → schedule
  rebuild. The per-image binary array is already `SwapChainNode`-owned and resized; the per-loop
  timeline semaphore survives (keeps counting).
- **Device-loss** — full graph rebuild already fires (`FrameSyncNode` fence wait → `NotifyDeviceLoss`);
  the timeline semaphore is recreated like any primitive; schedule re-baked.
- **Buffers** — no layout; already tracked (resource-agnostic access model). Intra = memory/buffer
  barrier; inter = timeline edge only.
- **Multi-loop skip-deadlock (Tier-3 hazard, flagged)** — if a loop is skipped this frame
  (`shouldExecuteThisFrame=false`) it never signals, so a cross-loop consumer waiting on it would hang.
  Inc-1's live graph is single-loop, so it cannot occur yet; the `SyncEdge`/`loopId` model gives the
  cadence-aware resolution (conditional wait / last-value) a home. **This is the explicit correctness
  problem to solve when Tier-3 cadence lands.**
- **Validation gate** — every path runs with **0 errors under Vulkan *synchronization* validation**
  (syncval), not just standard layers.

## Testing / validation

- **Pure unit tests (no GPU)** — the scheduler is pure logic ⇒ the bulk of coverage. Synthetic
  topologies → assert the produced schedule. Cases: linear chain; compute→compute RAW (buffer);
  compute→render image layout handoff; **fan-in** (2 producers → 1 consumer = 2 timeline waits);
  intra- vs inter-group classification; redundant-transition elision; timeline-offset assignment.
  Plus a `ResourceUsage → {stage,access,layout}` table test.
- **Live regression gate** — existing voxel **and** composite paths render with **0 syncval errors**.
  Proves the auto-schedule reproduces correct sync and the `leaveImageInGeneral` migration is faithful.
- **Multi-submit demo** — new env-flag graph (à la `VIXEN_INSTANCING_DEMO`): a genuine fan-in
  (2 compute groups → 1 render → present) → 0 syncval errors + visual confirmation. The Tier-2
  end-to-end proof.
  - Gotcha (memory): WSL bash does not pass env vars to Windows `.exe`; run via
    `cmd.exe /c "set VIXEN_<FLAG>=1&& VIXEN.exe"`.

## Internal phasing (each step builds green + passes + renders before the next)

| Phase | Deliverable | Gate |
|---|---|---|
| **P1** | `ResourceUsage` + table + tests; complete `ResourceAccessTracker` SlotMutability TODO + carry usage | build green, no behavior change |
| **P2** | `FrameSyncScheduler` (pure) → structured schedule (intra barriers + inter edges), fully unit-tested | scheduler tests pass |
| **P3** | Tier-1 leaf replay: `ComputeDispatchNode` consumes baked barriers; drop hand-rolled transitions; standardize `barrier2` | live voxel, 0 syncval |
| **P4** | Generalized `MultiDispatchNode` (trailing render pass, intra-group schedule, reusable pass-recording core) | demo, visual + 0 syncval |
| **P5** | Tier-2 timeline: `FrameSyncNode` timeline + `frameBase`; inter-group edges; migrate composite off `leaveImageInGeneral` | live composite, 0 syncval |
| **P6** | Fan-in multi-submit demo | visual + 0 syncval |

Risky Tier-2 (P5) lands only after the pure scheduler (P2) is test-proven and Tier-1 (P3) proves the
barrier path on the live graph. Each phase is an independent clean commit.

## Out of scope (future)

- **Tier-3 inter-loop cadence sync** — cross-loop edges modeled, not resolved (skip-deadlock above).
- **Submit-group merging** — collapsing independent nodes into one submit (optimization).
- **Multi-queue** — async compute / dedicated transfer queue (device currently makes one queue, AR#61).
- **Runtime graph-edit trigger** — the mod API / `GraphEditorNode` that fires `Rebuild()` on live edits
  (Sprint 8). The schedule is *built to receive* it; the trigger itself is future.
- **`MultiDispatchNode` → full `PassGroupNode`/Subgraph-As-Node collapse** — see
  [[Subgraph-As-Node-Design-2026-06]]; we generalize in place now, collapse later.

## Design decisions & rationale

- **Sync logic is centralized analysis, not a node member.** `device` is ambient read-only per-node
  context; a barrier/semaphore is a property of the *edge* between passes and needs whole-graph
  knowledge — so it cannot be computed correctly inside one node. The current per-node hardcoded
  transitions are exactly the anti-pattern being removed.
- **Command orchestration *is* a node** (generalized `MultiDispatchNode`). Per the user's preference and
  existing precedent — `MultiDispatchNode` already records N passes into one command buffer with
  `barrier2` and submits once. Graph-core stays out of command handling.
- **Compile-time baking.** Static execution order ⇒ layouts + timeline offsets are computable at
  compile; runtime only replays. Removes shared mutable runtime layout state and its coupling.
- **Hybrid timeline + binary.** Timeline gives the inter-group DAG (fan-in); WSI forces binary at
  acquire/present. Both, cleanly separated by swapchain-adjacency.
- **Loop/group-aware, rebuildable context.** Anticipates runtime graph mutation and multi-loop
  (UI/sim/…) so the abstraction does not need rewriting when Tier-3 / mod-API land — the user's core
  concern. Reuses `LoopManager`/`WaveScheduler`/`FrameSyncNode` rather than reinventing.

## Key files to touch

- `libraries/RenderGraph/include/Core/BarrierTypes.h` *(new)* — `ResourceUsage` + mapping.
- `libraries/RenderGraph/include/Core/FrameSyncScheduler.h` + `src/Core/FrameSyncScheduler.cpp` *(new)*.
- `libraries/RenderGraph/{include/Core,src/Core}/ResourceAccessTracker.*` — TODO `:92`, carry usage.
- `libraries/RenderGraph/src/Core/RenderGraph.cpp` — build/rebuild hook (`~:534`), schedule storage,
  expose via `Context`.
- `libraries/RenderGraph/{include,src}/Nodes/FrameSyncNode.*` — timeline semaphore + `frameBase`.
- `libraries/RenderGraph/src/Nodes/ComputeDispatchNode.cpp` — barrier replay; delete `TransitionImageTo*`.
- `libraries/RenderGraph/{include,src}/Nodes/MultiDispatchNode.*` + `include/Data/DispatchPass.h` —
  trailing render pass; extract reusable pass recorder.
- `libraries/RenderGraph/src/Nodes/UIRenderNode.cpp` + `ComputeDispatchNode.cpp` — composite migration
  off `leaveImageInGeneral`.

## Open questions / risks

- **`ResourceUsage` placement** — **Decision: both** — on slot metadata (for graph-wide leaf-node
  analysis) **and** on the pass descriptor (for the generalized node's node-local analysis). Adding a
  slot field touches the X-macro in `SlotFields.h`; the `Auto` default keeps existing slots compiling.
  (Confirm the exact field shape during planning.)
- **`MultiDispatchNode` benchmark coupling** — must extract the pass-recording core cleanly so the
  graphics path does not inherit benchmark/query-manager baggage. Risk to watch in P4.
- **Timeline `frameBase` arithmetic** — must guarantee monotonicity across the 4-frame ring with no
  wraparound hazard; covered by P2 offset tests + a P5 live syncval run.
- **syncval availability on the target ICDs** — confirm synchronization validation runs on the dev GPU
  (NVIDIA RTX 3060, `--gpu 1`) and on WSL/lavapipe for the gated tests.

---

*Created 2026-06-21 by Claude Code (brainstorming → spec). Next: spec review → implementation plan via
the writing-plans skill.*
