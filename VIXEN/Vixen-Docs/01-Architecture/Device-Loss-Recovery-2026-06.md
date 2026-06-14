---
tags: [architecture, error-model, refactor, AR1, device-loss, recovery, phase3]
created: 2026-06-13
status: phase 3 — Inc 1 + Inc 2 DONE. Device-loss recovery fully working: rebuild on a fresh device, resume rendering indefinitely, zero validation errors. (Inc 3 = automated test + remaining detection sites.)
related: ["[[Error-Model-Refactor-2026-06]]", "[[Window-Abstraction-Design-2026-06]]", "[[Architecture-Review-Game-Renderer-2026-06-12]]"]
---

# Device-Loss Recovery (AR#1, Error-Model Phase 3) — Design

> **Goal:** when the GPU device is lost at runtime (`VK_ERROR_DEVICE_LOST` — TDR/driver reset,
> hung shader, hot-unplug), the engine **rebuilds itself on a fresh device and keeps rendering**
> instead of crashing or stopping. The genuine, recoverable runtime failure that Error-Model
> Phases 1–2 explicitly deferred to Phase 3.

## 0. Why this — and NOT "convert the 396 throws"

The backlog framed Phase 3 as "adopt `VK_CHECK`/status across the 396 `throw`s." A categorization
of those throws (2026-06-13) shows that framing is **wrong**: the large majority are
**validation/invariant** checks — "shader bundle not set", "render pass not set", "VulkanDevice
input is null", "Owning graph not available". Those are **programmer-error invariants** (a mis-wired
graph). Converting them to silent statuses would be a *regression* — it hides bugs that should
fail loudly and fast. Post-Phase-2 they no longer crash the C# host (every host-facing boundary
catches), so there is no robustness reason to touch them.

The genuinely valuable Phase-3 work is **surgical *recovery* of the truly-transient runtime
failures**. By far the highest-value one is **device loss**: a long-running host (UNDERTOW) WILL
hit a TDR / driver reset eventually, and today that is an unrecoverable hard stop.

## 1. What "device lost" means

`VK_ERROR_DEVICE_LOST` can be returned by `vkQueueSubmit`, `vkQueuePresentKHR`,
`vkAcquireNextImageKHR`, `vkWaitForFences`, `vkDeviceWaitIdle`, and others. After it, the `VkDevice`
and **every** child object (queues, command pools, buffers, images, pipelines, descriptor sets,
swapchain, fences, semaphores) are invalid. `vkDestroy*`/`vkFree*` on the lost device remain
**safe** (the spec requires them to be), but you may not submit work, and waits return
`VK_ERROR_DEVICE_LOST` immediately. Recovery = destroy everything device-scoped, recreate the
device, recreate everything.

**Survives device loss** (NOT device-children, must be kept): the `VkInstance`, the OS window, and
the `VkSurfaceKHR` (surface is instance-level). In VIXEN these are owned by `InstanceNode` /
`WindowNode`.

## 2. What the engine already has (the enablers)

This refactor is **mostly wiring existing machinery**, not building from scratch:

1. **The device is an in-graph node.** `DeviceNode::CompileImpl` creates the `VulkanDevice`,
   publishes it on `VULKAN_DEVICE_OUT`, registers caches, builds the allocator/uploader/updater/
   query-manager. Every other node receives `VulkanDevice*` by graph connection.
2. **A device-invalidation event already exists** — `EventBus::DeviceInvalidationEvent` with a
   `Reason` enum that *already includes* `DriverReset` ("TDR or driver crash recovery") and
   `DeviceDisconnected`. `MainCacher` subscribes and clears device-dependent caches. Today only
   `DeviceRecompilation` is ever published; the device-lost reasons were designed for but never fired.
3. **A recompile cascade.** `RenderGraph::RecompileDirtyNodes` recompiles dirty nodes in execution
   order and, when a provider recompiles, marks **all transitive dependents** dirty
   (`cleanupStack.GetAllDependents`). It already skips `vkDeviceWaitIdle` during recompile and
   defer-retries per-node failures.
4. **A cleanup-reason lifecycle.** `CleanupReason {Recompile, FinalTeardown}` lets a node keep
   persistent resources across a recompile. **`WindowNode` is the only node that keeps anything**
   across `Recompile` — exactly the window+surface that survive device loss. Every other node
   already releases all its device-child resources on `Recompile`.

## 3. Why naive "mark DeviceNode dirty" is NOT enough (the ordering hazard)

The tempting shortcut — on device loss, `MarkNodeNeedsRecompile(deviceNode)` and let the existing
cascade rebuild everything — has a **destruction-ordering bug**. `RecompileDirtyNodes` processes
nodes in **execution order**, and `DeviceNode` is *first*. So it would `Cleanup+Compile` the
DeviceNode (destroying the old `VkDevice` and creating a new one) **before** the downstream nodes
tear down their old buffers/images/pipelines. Destroying a `VkBuffer` after its `VkDevice` is
already destroyed is undefined behaviour (children must die before the parent device). On a lost
device this *often* survives by driver leniency — but it is not correct, and "prefer the pure,
fully-correct solution" rules it out.

## 4. The design — explicit, ordering-correct full rebuild (in-graph self-heal)

Recovery is a dedicated `RenderGraph::RecoverFromDeviceLoss()` that reuses the **per-node lifecycle**
at **full-graph scope** with **correct ordering**:

```
RecoverFromDeviceLoss():
  1. TEARDOWN  — Cleanup every node in REVERSE execution order, reason = DeviceLost.
                 → children released before the device; WindowNode keeps window+surface
                   (reason != FinalTeardown); DeviceNode (last in reverse) clears device caches
                   and destroys the old VkDevice last. No vkDeviceWaitIdle on the dead device.
  2. REBUILD   — Setup + Compile every node in FORWARD execution order.
                 → DeviceNode (first) creates the NEW VulkanDevice, publishes
                   DeviceInvalidationEvent{DriverReset}, rebuilds caches; every downstream node
                   re-reads the new VulkanDevice* from its input and recreates its resources.
  3. On success: clear deviceLost_, resume rendering.
     On failure (device truly gone): bounded retries, then terminal deviceLostUnrecoverable_ →
     RenderFrame returns VK_ERROR_DEVICE_LOST persistently so the host aborts gracefully.
```

This **lives in the graph** (consistent with the window refactor and the "no systems outside the
graph" rule). The host just sees a hitched frame; it can also observe the `DeviceInvalidationEvent`.

### New `CleanupReason::DeviceLost`
Distinct from `Recompile` because cleanup code must **skip dead-device waits**
(`vkDeviceWaitIdle`/`vkWaitForFences` on a lost device) — a node that waits in `CleanupImpl` branches
`if (reason != DeviceLost)`. WindowNode's existing `reason != FinalTeardown` guard already keeps the
window for `DeviceLost`, so **no WindowNode change is needed**.

## 5. Detection — where device loss is observed

A node that gets `VK_ERROR_DEVICE_LOST` from a GPU call calls
`GetOwningGraph()->NotifyDeviceLost(site)`, which latches a graph-level `deviceLost_` flag (idempotent;
first detection wins). `RenderFrame` checks the flag and returns `VK_ERROR_DEVICE_LOST` distinctly
(not the generic `VK_ERROR_UNKNOWN` from Phase 2a), so the trigger can route to recovery.

**Primary detection site:** `FrameSyncNode::ExecuteImpl` waits on the in-flight fence **every frame**
(`vkWaitForFences`, currently result-ignored) — the universal, earliest backstop: any device loss
surfaces here within one frame. Secondary sites (acquire/present/submit) are wired for promptness.

## 6. Phased increments

- **Increment 1 — detection + distinct reporting (foundation).** ✅ DONE (commit `16d0b8ec`).
  `deviceLost_` flag, `NotifyDeviceLost()`/`IsDeviceLost()`, `FrameSyncNode` fence-wait detection,
  `RenderFrame` returns a distinct `VK_ERROR_DEVICE_LOST`. Device loss is now *detected and cleanly
  reported* (no crash/UB) even before recovery exists. Validated: clean build; live app renders the
  loop with no false detection.
- **Increment 2 — the rebuild path.** ⏳ MECHANISM VALIDATED, one post-recovery crash remains.
  Landed: `CleanupReason::DeviceLost`; `RecoverFromDeviceLoss()` (drain → teardown-reverse →
  rebuild-forward); in-graph self-heal trigger from the app `Render()`; single-attempt + terminal
  `deviceLostUnrecoverable_`; `DeviceInvalidationEvent{DriverReset}`; a **fault-injection harness**
  (`VIXEN_SIMULATE_DEVICE_LOSS=<frame>`) that latches a synthetic loss in a live run (the
  teardown/recreate is valid on a healthy device, so it faithfully drives recovery). Also fixed
  **ConstantNode** to be rebuildable (it moved its value into the output once and reset it on cleanup,
  so a rebuild's second Compile threw "Value not set" — now it retains a re-populator and keeps the
  value across non-final cleanups, like the window survives a device loss).
  **Proven by the harness:** synthetic loss at frame 60 → all 19 nodes torn down (reverse) → device
  recreated → all 19 rebuilt (forward) → "RECOVERY COMPLETE" → swapchain recreated → ~73s of continuous
  post-recovery rendering until the test timeout, **no crash, zero Vulkan validation errors**.

  **Two root-causes found + fixed to get there (both pre-existing, surfaced by the rebuild):**
  1. **Use-after-free on the GPU task profile.** Nodes cache a raw `ITaskProfile*` (e.g.
     `ComputeDispatchNode::gpuProfile_`, acquired in `CompileImpl`). `DeviceNode` publishes
     `DeviceMetadataEvent` on every (re)compile → `CalibrationStore::Load()` → `TaskProfileRegistry::
     LoadState()`, which did `RegisterTask(std::move(new))` — **replacing and freeing** the existing
     profile objects, dangling every cached pointer. The next `gpuProfile_->Sample()` (its `Sampler`
     ctor calls the virtual `GetEstimatedCostNs()`) dereferenced freed memory → silent crash. The
     rebuild re-published `DeviceMetadataEvent` so it bit reliably post-recovery; once a `calibration/`
     file existed it also bit on a normal first frame (non-deterministic, heap-layout dependent — which
     is why it presented as "post-recovery only" at first). **Fix:** `LoadState` now loads calibration
     **into the existing profile in-place** (`existing->LoadState(json)`) when one with that id exists,
     preserving object identity; only registers a new object when absent. `TaskProfileRegistry.h`.
  2. **`VUID-vkDestroyInstance` during teardown.** `InstanceNode::CleanupImpl()` unconditionally
     destroyed the `VkInstance`; the device-loss teardown was the first non-final teardown it ever saw,
     so it destroyed the instance while `WindowNode`'s surface (created from it, kept across the rebuild)
     was still alive. **Fix:** `InstanceNode` now **persists the instance across recompile / device-loss**
     (keeps it unless `FinalTeardown`) and its `CompileImpl` is idempotent (reuse + re-publish), mirroring
     `WindowNode`. The instance + surface + window all survive a device loss; only the device and its
     children rebuild. `InstanceNode.{h,cpp}`.

  Also fixed **ConstantNode** to be rebuildable (it moved its value into the output once and reset it on
  cleanup → a rebuild's second Compile threw "Value not set"; now a re-populator rebuilds the value each
  Compile and survives non-final cleanups, like the window survives a device loss).
- **Increment 3 — hardening + coverage + tests.** Wire the remaining acquire/present/submit detection
  sites; promote the fault-injection harness into an automated test; exercise the unrecoverable-loss
  terminal path; live-app no-regression validation.

## 7. Acceptance

- ✅ A runtime `VK_ERROR_DEVICE_LOST` is detected and surfaced as a distinct status, not a crash (Inc 1).
- ✅ The graph rebuilds on a fresh device with correct destroy-before-recreate ordering and resumes
  rendering (Inc 2 — validated: ~73s continuous post-recovery rendering, no crash).
- ✅ Window + surface + instance survive the rebuild (no OS-window/instance recreate — WindowNode and
  InstanceNode keep them on `DeviceLost`; zero `VUID-vkDestroyInstance`).
- ✅ Post-recovery rendering is stable across frames (UAF + VUID fixes; 600k+ post-recovery log lines).
- ⬜ An unrecoverable loss (device gone) terminates gracefully via status, with no infinite spin
  (`deviceLostUnrecoverable_` path — coded, not yet exercised since the simulated device is healthy). Inc 3.
- ⬜ Automated simulated device-loss test (Inc 3); live app unaffected with the env var unset (holds:
  recovery code is dormant unless a real loss or the env var triggers it).
