---
title: RenderTargetNode + IRenderTarget — Design (AR#28)
aliases: [RenderTargetNode, IRenderTarget, render-to-texture, AR#28 design]
tags: [architecture, design, rendergraph, presentation-layer, AR28, spec]
created: 2026-06-14
status: approved (design) — implementation pending
related:
  - "[[RenderGraph]]"
  - "[[Maturation-Backlog-2026-06]]"
  - "[[Hosting-VIXEN]]"
---

# RenderTargetNode + IRenderTarget — Design (AR#28)

Render-to-texture / offscreen render targets as a first-class graph concept. This is the **keystone of
the P3 Presentation Layer** — the review: *"essentially the entire grand-strategy presentation layer
hangs off this single change"* (minimap, portraits, picking ID-buffers, fog-of-war, post-processing,
multi-view, and a headless render path all depend on offscreen targets). Source: AR#28 / Decision #3 in
[[Architecture-Review-Game-Renderer-2026-06-12]].

## Problem

Every recording node and `FramebufferNode` **non-nullably requires `SwapChainPublicVariables*`**;
compute/RT passes write swapchain images directly. There is no way to render anywhere but the
swapchain. The gap is **node-layer only** — the VMA/Direct allocators already make arbitrary images,
`DescriptorSetNode` binds arbitrary `VkImageView`s, and `DepthBufferNode` already proves the
non-swapchain attachment pattern. So the fix is a **producer node + an abstraction**, not new
infrastructure.

## Decisions (brainstorm 2026-06-14)

| # | Decision | Choice |
|---|---|---|
| Q1 | Scope | **Full migration**: introduce `IRenderTarget` and migrate all recording nodes + FramebufferNode off `SwapChainPublicVariables*` onto it (swapchain and RenderTargetNode both implement it). |
| Q2 | Abstraction | **Abstract interface** `IRenderTarget` (virtual accessors + non-virtual conversion operators). Proper dependency inversion; swapchain-specifics stay off the interface. |
| Q3 | Node shape | **Color-only** producer, mirroring the swapchain. Depth via the existing `DepthBufferNode`; `FramebufferNode` combines — the same composition the swapchain path uses. MRT = multiple RenderTargetNodes. |

## 1. `IRenderTarget` interface

New abstract interface in **VulkanResources** (beside `SwapChainPublicVariables`,
`libraries/VulkanResources/include/VulkanSwapChain.h` or a new `IRenderTarget.h`):

```cpp
struct IRenderTarget {
    virtual ~IRenderTarget() = default;

    virtual uint32_t    GetImageCount()   const = 0;   // frames-in-flight count (>=1)
    virtual uint32_t    GetCurrentIndex() const = 0;   // index into the image set this frame
    virtual VkImage     GetImage(uint32_t i) const = 0;
    virtual VkImageView GetView(uint32_t i)  const = 0;
    virtual VkFormat    GetFormat()   const = 0;
    virtual VkExtent2D  GetExtent()   const = 0;

    VkImage     GetCurrentImage() const { return GetImage(GetCurrentIndex()); }
    VkImageView GetCurrentView()  const { return GetView(GetCurrentIndex()); }

    // Non-virtual ergonomic conversions (preserve existing node call sites that rely on the
    // swapchain's implicit conversions). Resolve to the CURRENT image/view.
    operator VkImageView() const { return GetCurrentView(); }
    operator VkImage()     const { return GetCurrentImage(); }
};
```

- **`SwapChainPublicVariables : public IRenderTarget`** — implement the accessors from its existing
  fields (`colorBuffers` → image/view, `currentColorBuffer` → index, `swapChainImageCount`, `Format`,
  `Extent`). Keep its swapchain-specific fields (`surface`, `swapChain`) and their existing conversion
  operators. **Behaviorally unchanged.** (The current `operator VkImageView()/VkImage()` are subsumed
  by the interface's; keep behavior identical — current image/view.)
- **`RenderTargetData : public IRenderTarget`** — the offscreen target's own data: a vector of
  color buffers (`{VkImage, VkDeviceMemory, VkImageView}` per in-flight frame), `currentIndex`,
  `format`, `extent`, `imageCount`.

## 2. `RenderTargetNode` (color-only producer)

A `TypedNode<RenderTargetNodeConfig>` that allocates an offscreen color image set and outputs an
`IRenderTarget*`, mirroring how `SwapChainNode` outputs `SwapChainPublicVariables`.

- **Inputs:** `VULKAN_DEVICE (VulkanDevice*)`; optional swapchain input only when `followSwapchainExtent`.
- **Outputs:** `RENDER_TARGET (IRenderTarget*)`; plus convenience `CURRENT_VIEW (VkImageView)`,
  `EXTENT (VkExtent2D)`, `FORMAT (VkFormat)` (like WindowNode/SwapChainNode expose extras).
- **Parameters:**
  - `width` / `height` — fixed extent; **or** `followSwapchainExtent` (bool, default false): size to
    the swapchain extent and recreate on resize (rides the existing recompile cascade).
  - `format` (VkFormat, default `VK_FORMAT_R8G8B8A8_UNORM`).
  - `imageCount` (default = **frames-in-flight** — the graph's max in-flight frame count — so a frame
    writing the target never races a prior frame still sampling it; set 1 for a render-once-sample-many
    static target). `currentIndex` advances with the **in-flight frame index** (distinct from the
    swapchain's *acquired-image* index, which is what `SwapChainPublicVariables::currentColorBuffer`
    tracks — the interface unifies both behind `GetCurrentIndex()`).
  - `usage` (VkImageUsageFlags, default `COLOR_ATTACHMENT_BIT | SAMPLED_BIT`; opt-in `STORAGE_BIT`
    for compute writes, `TRANSFER_SRC_BIT` for readback/headless).
- **Allocation:** mirror `DepthBufferNode` (vkCreateImage + memory + image view per buffer) **but use
  the working `FindMemoryType`** (`NodeHelpers/BufferHelpers.h`) — **not** DepthBufferNode's
  `memoryTypeIndex = 0 // Placeholder` (the AR#18 stub bug). Track allocations via
  `DeviceBudgetManager` for budget accounting.
- **Lifecycle (FR-7, see [[RenderGraph]] §3.1):** branch on `ctx.reason`. Keep the images **persistent
  across `Recompile`/`DeviceLost`** unless the extent actually changes (`followSwapchainExtent` →
  recreate on resize). Release everything only on `FinalTeardown`. **No `vkDeviceWaitIdle` in the
  recompile path.**

## 3. Migration (the wide part)

Change recording-node slots from `SwapChainPublicVariables*` to `IRenderTarget*` across ~13 node
configs + both graph factories (~68 sites). Migration surface:

`CameraNode`, `ComputeDispatchNode`, `DepthBufferNode`, `DescriptorSetNode`, `FramebufferNode`,
`GeometryRenderNode`, `MultiDispatchNode`, `RenderPassNode`, `StructSpreaderNode`, `SwapChainNode`
(output), `SwapChainStructSpreaderNode`, `TraceRaysNode`, `UIRenderNode`, + Profiler `FrameCapture`.
(Skip dead `DescriptorSetNodeConfig_OLD.h`.)

- `SwapChainNode` now outputs `IRenderTarget*` (pointing at its `SwapChainPublicVariables`, which
  IS-A `IRenderTarget`) → consumers are **behaviorally unchanged**.
- **Incremental & safe:** because `SwapChainPublicVariables` IS-A `IRenderTarget`, migrate slot types
  node-by-node, building + running the benchmark suite between steps. No single big-bang switch.
- Watch the typed-connection system: slot type `SwapChainPublicVariables*` → `IRenderTarget*` must be
  updated in both the slot config and any `FieldExtractor`/connection presets that reference it.

## 4. Testing

- **Benchmark suite is the regression harness** — every existing render path now flows through
  `IRenderTarget`; the suite must stay green (it exercises the swapchain path end-to-end).
- **New focused test:** RenderTargetNode produces a valid image/view set of the requested
  extent/format/count; a downstream consumer (e.g. a sampling/compute pass) reads it; assert the
  render-to-texture round-trip (and that `currentIndex` advances with frames-in-flight).
- **Headless render path** (no swapchain; RenderTargetNode as the final target + `TRANSFER_SRC` readback)
  "falls out" of this work — a headless smoke/CI test is a natural follow-up (optional this pass).

## 5. Risks / mitigations

- **Wide slot migration (~68 sites, ~13 nodes + 2 factories)** — the review's flagged risk. *Mitigation:*
  IS-A keeps swapchain behavior identical; migrate incrementally; build + benchmark each step.
- **DepthBufferNode `memoryTypeIndex = 0` placeholder (AR#18)** — do **not** propagate it; RenderTargetNode
  uses the real `FindMemoryType`. *Optional adjacent fix:* correct DepthBufferNode's placeholder too
  (small, same root cause) — confirm in the plan whether to include.
- **Frames-in-flight hazards** — `imageCount` defaults to the in-flight frame count + per-frame
  `currentIndex` advance, so a frame writing the target never races a prior frame sampling it.

## Out of scope (later P3 items)

- `CompositeNode` for multi-view fan-in (AR#29/#30 multi-view).
- Full headless render pipeline / CI metrics conversion (this just makes it possible).
- MRT as a single node (use multiple RenderTargetNodes).
- Picking ID-buffer specifics (AR#35), fog-of-war (separate feature) — they *consume* this.
