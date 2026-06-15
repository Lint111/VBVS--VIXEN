---
title: Picking / selection — CPU click ray-pick (AR#35, increment 1)
aliases: [PickingNode, picking, ray-pick, MouseButtonEvent, PickResultEvent, AR#35]
tags: [architecture, design, rendergraph, input, presentation-layer, AR35]
created: 2026-06-15
status: SUPERSEDED — the CPU ray-pick (GaiaVoxelWorld) is null on cached-scene hits; shipped approach is
  GPU pixel-exact ID-buffer picking, see [[GPU-IDBuffer-Picking-Design-2026-06]]. This doc kept for the
  input/event/unproject foundation (MouseButtonEvent, PickResultEvent, ComputePickRay) which still stands.
related:
  - "[[Maturation-Backlog-2026-06]]"
  - "[[RenderGraph]]"
---

# Picking / selection — CPU click ray-pick (AR#35, increment 1)

Left-click a voxel → unproject the cursor to a world ray → march the CPU voxel world → log + broadcast
the hit. First selection capability. Source: AR#35, scope "identify (log + PickResultEvent)".

## Context (what existed)

- Input is **pull-based** (`InputNode` polls GLFW → `InputState{mousePosition (px), mouseButtons[3]}`;
  consumers pull each frame — CameraNode is the template).
- `CameraData` already carries **real** `invProjection`/`invView` (computed every frame by CameraNode);
  picking is their **first consumer** (the voxel ray-march shader builds rays from `cameraPos/dir/fov`).
- `GaiaVoxelWorld` query API exists: `getEntityByWorldSpace(pos)`, `getPosition(id)`, `exists(id)`,
  `getEntityByMorton`, `queryRegion`. (No exposed CPU raycast — so we march and sample.)
- Two gaps fixed here: `MouseButtonEvent` was declared but never published; the CPU `GaiaVoxelWorld` was
  not durably reachable (transient cacher scratch).

## What shipped

1. **`MouseButtonEvent` publish fix** — `InputNode::PublishMouseEvents` now publishes on press/release
   edges (was disabled); `InputState` polling is unchanged (both coexist). Plus a new **`PickResultEvent`**
   (entity, hit, worldPos, morton, screenPos, button) on the bus. (`EventBus` now links `glm`.)
2. **Durable world ownership** — moved `GaiaVoxelWorld` ownership from the cacher's transient
   `m_voxelWorld` onto the cached `VoxelSceneData` (a `unique_ptr` member), and exposed it as
   `VoxelGridNodeConfig::VOXEL_WORLD` (`GaiaVoxelWorld*`, output index 10). Non-null on both cache-miss
   and cache-hit; freed only with the cached scene (no dangling/wrong-scene pointer — the prior design's
   latent bug).
3. **`ComputePickRay`** (pure, `Data/PickRay.h`) — screen px → world ray. **NDC convention:**
   `x = 2·px/W − 1`, `y = 2·py/H − 1` (NOT `1 − 2·py/H`) because CameraNode bakes the Vulkan Y-flip into
   the projection (`projection[1][1] *= -1`) and uses `GLM_FORCE_DEPTH_ZERO_TO_ONE`. Unproject near
   (z=0)/far (z=1) through `invProjection`→`invView`; `dir = normalize(far − near)`. Unit-tested (5 cases,
   incl. a top/bottom Y-sign regression guard) against CameraData built exactly as CameraNode builds it.
4. **`PickingNode`** (type 125) — inputs `InputState`, `CameraData`, `VOXEL_WORLD`, viewport
   (`WindowNode WIDTH_OUT/HEIGHT_OUT`). On the left-click **down-edge**: `ComputePickRay` → march `t` 0→512
   by 0.25 (sub-voxel; grid is 1 unit = 1 voxel) sampling `getEntityByWorldSpace`, first
   `world->exists(e)` is the hit → `NODE_LOG_INFO` + publish `PickResultEvent` (entity via
   `Entity::value()`, morton via `MortonCode64::fromWorldPos`). Pure-CPU sink (also a `LAST_PICK_HIT`
   status output); only marches on the click edge. Wired into the **live voxel graph** (`BuildRenderGraph`).

## Verification

- `test_pick_ray` 5/5 (ray math incl. Y-sign). Cacher suites 39/39 (ownership move). Full build green.
- App smoke: PickingNode wired (5 connections), `VOXEL_WORLD` ptr non-null, graph compiles, **0
  VK_ERROR/VUID**. Headless can't synthesize a click → **live click-pick is a manual user test**
  (left-click a voxel → `[PickingNode] HIT entity=...` in the log).
- Residual risk the click-test covers: the pick ray (from `invProjection`/`invView`) must match the
  voxel shader's ray (from `cameraPos/dir/fov`) — consistent if CameraNode's matrices and vectors agree
  (unit test confirms center-pixel ray ≈ `cameraDir`), but only a real click confirms pixel-accurate hits.

## Out of scope (later increments)

- **Drag-rectangle multi-select** (project corners → `queryRegion` AABB).
- **GPU pixel-exact ID-buffer** (render entity IDs to an offscreen target [AR#28], read back the pixel) —
  the precise alternative to CPU ray-march.
- **Visual highlight** (tint the selected voxel via the ray-march shader) + a durable **selection state**
  (component/app-state); today a pick only logs + emits an event.
