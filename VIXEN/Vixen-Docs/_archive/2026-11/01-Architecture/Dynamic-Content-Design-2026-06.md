---
title: Per-frame dynamic content — animated instancing (AR#33, increment 1)
aliases: [DynamicInstanceBufferNode, PerFrameResources ring, per-frame dynamic buffer, AR#33]
tags: [architecture, design, rendergraph, presentation-layer, AR33]
created: 2026-06-15
status: DONE — animated instancing via per-frame ring buffer, merged to main 2026-06-15
related:
  - "[[Instancing-Increment-Design-2026-06]]"
  - "[[Auto-Sync-FrameGraph-Design-2026-06]]"
  - "[[Maturation-Backlog-2026-06]]"
---

# Per-frame dynamic content — animated instancing (AR#33, increment 1)

Make GPU buffer **contents change every frame** (no readback): the instancing demo's 64 cubes now
**animate** (spin), driven by a ring-buffered instance-transform SSBO rewritten each frame. First real
per-frame dynamic-buffer machinery in the engine. Source: AR#33, scope "per-frame dynamic content".

## Context (what existed)

- **No per-frame buffer updates existed.** Only push constants (camera) changed per frame; every
  `VkBuffer` was written once at compile (incl. `InstanceBufferNode`/`MvpUniformNode`). Animating via a
  *buffer* was new ground.
- **`PerFrameResources`** (`libraries/RenderGraph/include/Core/PerFrameResources.h`) — a ring-buffer
  helper (N per-frame buffers + mapped ptrs + per-frame descriptor-set slots) existed but was **unused**.
- **`DescriptorSetNode::ExecuteImpl` already re-binds descriptors per frame** (`vkUpdateDescriptorSets`
  per frame, with per-frame sets to dodge the "update-while-in-use" hazard) — so a per-frame-changing
  buffer handle is followed automatically.
- `BatchedUpdater` (the backlog's "drain me") records GPU **commands** (`vkCmd*`) for device-local /
  TLAS updates — a **different mechanism**, mismatched for CPU-written host-visible per-frame data.

## Decision

Use the **`PerFrameResources` host-visible ring** (the right tool for CPU-written per-frame data) — not
`BatchedUpdater`. `BatchedUpdater` is deferred to its proper home (dynamic TLAS / device-local streaming);
see [[Auto-Sync-FrameGraph-Design-2026-06]] for where it fits.

## Design

### `DynamicInstanceBufferNode`
A producer mirroring `InstanceBufferNode` but **dynamic**:
- **Inputs:** `VULKAN_DEVICE_IN`; `CURRENT_FRAME_INDEX (uint32_t, Execute role)` — the ring index from
  FrameSyncNode.
- **Outputs:** `INSTANCE_BUFFER (VkBuffer)` re-emitted **every** `ExecuteImpl`; `INSTANCE_COUNT (uint32_t)`.
- **Params:** `gridDim` (8 → 64), `spacing` (2.5), `rotationSpeed` (0.02).
- **CompileImpl (FR-7, once):** `perFrame_.Initialize(device, N)` where `N = FrameSyncNodeConfig::
  MAX_FRAMES_IN_FLIGHT (4)`; `perFrame_.CreateStorageBuffer(i, count*sizeof(mat4))` per ring slot.
- **ExecuteImpl (per frame):** `frameIndex = ctx.In(CURRENT_FRAME_INDEX) % N`; compute animated
  transforms (`translate(gridPos) * rotate(frameCounter*speed*(1+0.1*i), Y)`); `memcpy` into
  `GetUniformBufferMapped(frameIndex)`; `ctx.Out(INSTANCE_BUFFER, GetUniformBuffer(frameIndex))`.
- **CleanupImpl:** FR-7; `perFrame_.Cleanup()` only on `FinalTeardown`.

### `PerFrameResources` extension
Added `CreateStorageBuffer(frameIndex, size)` (the helper hardcoded uniform usage). Both
`CreateUniformBuffer` and `CreateStorageBuffer` now delegate to a private
`CreateBufferImpl(frameIndex, size, usage)` — host-visible+coherent, persistently mapped.

### Per-frame descriptor re-bind (why it works)
1. The node **re-emits** the current ring buffer each `ExecuteImpl` (transient output; same idiom as
   `CameraNode` re-publishing `CameraData`).
2. The demo wires `INSTANCE_BUFFER → gatherer binding 2` with `SlotRole::Dependency | Execute` (re-read
   per frame).
3. `DescriptorSetNode::ExecuteImpl` re-runs `vkUpdateDescriptorSets` per frame against per-frame sets.

No new descriptor infrastructure — the rails existed; this is the first node to exercise them.

## Verification

Headless proof of animation: with `VIXEN_INSTANCING_DEMO=1`, binding 2 cycles through **all 4 distinct
ring-buffer handles** across frames (`0x26/0x28/0x2a/0x2c`), **0 VK_ERROR/VUID** (no
update-while-in-use), demo renders. Default voxel path regression-clean. `test_dynamic_instance_buffer_node`
config suite passes. **User visually confirmed the cubes animate on screen.**

## Out of scope (later)

- **AR#34** — dynamic *geometry* / multi-draw for text+UI (`UIDrawListNode`); per-frame vertex/index
  streaming (vs. just transforms).
- A generic reusable `DynamicBufferNode` (this increment is instance-transform-specific).
- `BatchedUpdater` revival for device-local / dynamic-TLAS updates.
- Real elapsed-time animation (frame-counter-driven today — deterministic, no wall clock).
