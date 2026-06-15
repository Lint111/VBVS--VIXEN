---
title: GPU pixel-exact ID-buffer voxel picking (AR#35, increment 2)
aliases: [ID-buffer picking, GPU picking, pickID, AR#35 GPU]
tags: [architecture, design, rendergraph, picking, voxel, AR35]
created: 2026-06-15
status: design approved — implementation in progress
related:
  - "[[Picking-Design-2026-06]]"
  - "[[RenderTarget-Design-2026-06]]"
  - "[[Maturation-Backlog-2026-06]]"
---

# GPU pixel-exact ID-buffer voxel picking (AR#35, increment 2)

CPU ray-pick via `GaiaVoxelWorld` is a dead end: the ECS world is process-local (`unique_ptr`, not
serialized), so it's **null on every disk-cache hit** (the common case) — VoxelGridNode logs
`VOXEL_WORLD output ptr=0 (NULL - no CPU world on cached scene!)`. The cached scene keeps GPU octree data
(+ CPU brick arrays) but not the ECS world. So picking must use the **GPU**: the ray-march already
determines the hit voxel per pixel — have it write a per-pixel ID, then read back the crosshair pixel.

## Approach

1. **Shader** — `shaders/VoxelRayMarch_Compressed.comp` (live, `USE_COMPRESSED_SHADER=1`) **and**
   `shaders/VoxelRayMarch.comp`. At the brick-DDA hit, both `brickIndex` and `voxelLinearIdx` (0–511) are
   in hand. Write `pickID = (brickIndex << 10) | voxelLinearIdx` (0xFFFFFFFF on miss) to a new
   `layout(binding = 9) uniform writeonly uimage2D idOutputImage;` via `imageStore(idOutputImage,
   pixelCoords, uvec4(pickID,0,0,0))`, beside the existing color store to binding 0. (32-bit; decode on
   CPU as `brick = id >> 10`, `voxel = id & 0x3FF`. No Morton needed.)
2. **ID image** — an `R32_UINT` storage image, swapchain extent, one per in-flight frame (ring, like the
   dynamic instance buffer), `STORAGE` + `TRANSFER_SRC` usage. Created by a small dedicated node (or a
   format-extended RenderTargetNode). Bound at descriptor binding 9 of the compute dispatch via the
   DescriptorResourceGatherer (mirrors how the swapchain image is bound at binding 0).
3. **Readback** — dedicated staging path (cleaner than DebugBufferReaderNode, which is per-frame/full-
   buffer): host-visible staging buffer per in-flight frame. On a left-click edge, record
   `vkCmdCopyImageToBuffer` of the **center pixel** (crosshair — the cursor is locked center) into the
   current frame's staging buffer; after that frame's `IN_FLIGHT_FENCE` signals (read the completed
   frame), map + read the `uint32`, decode, and report (log + `PickResultEvent`).
4. **PickingNode rework** — drop the CPU ray-march (`ComputePickRay` + `getEntityByWorldSpace`); the node
   now owns the readback trigger + decode. `VOXEL_WORLD` input removed. (Keep `ComputePickRay` for a
   future RTS cursor / other providers.)

## Sync / frames-in-flight

Single command buffer orders dispatch → copy → present, so no extra barrier for the copy. The CPU reads
the **previous completed frame's** staging buffer after its fence — staging + ID image are per-in-flight-
frame (ring) so a click's copy never races a prior frame's read. Reuse `FrameSyncNode` fences.

## Why this is the right backend

- Uses the **cached GPU octree** — works on every run (cache hit or miss); no CPU-world dependency.
- **Pixel-exact** — picks exactly what's rendered (same traversal as the visible image).
- Click-triggered single-pixel readback — negligible cost, not the per-frame readback we avoid.
- Natural fit for the `ISelectable`/`SelectContext` design: this is the **voxel selection provider**;
  UI / 3D-mesh providers slot in beside it; `SelectContext` carries the crosshair/cursor point.

## Plan (phased)

- **P1 — GPU write:** shader binding-9 ID write (both shaders) + the ID storage-image node + descriptor
  wiring at binding 9. Verify: renders clean, 0 VK errors, ID image populated.
- **P2 — readback + rework:** staging readback on click (copy center pixel, fence, read, decode) +
  PickingNode rework + `PickResultEvent`. Verify (user): aim crosshair at a voxel, click → correct
  brick/voxel logged; aim at sky → miss sentinel.
- **P3 — cleanup + close:** strip temp DIAG logging; backlog/docs; merge.

## Deferred

- World-position / Morton from the picked brick+voxel (needs the brick→world inverse) — add if a consumer
  needs world coords.
- RTS cursor-release selection (uses the real cursor pixel) — `ISelectable` redesign.
- Multi-pick / drag-rectangle (read an ID region, dedupe).
