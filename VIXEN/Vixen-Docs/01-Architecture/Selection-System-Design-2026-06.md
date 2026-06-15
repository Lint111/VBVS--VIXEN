---
title: Selection system — ISelectable / SelectContext (engine-wide picking)
aliases: [ISelectable, SelectContext, ISelectionProvider, SelectionCoordinator, selection system]
tags: [architecture, design, rendergraph, picking, selection, input]
created: 2026-06-15
status: design — pending approval, then implement
related:
  - "[[GPU-IDBuffer-Picking-Design-2026-06]]"
  - "[[Picking-Design-2026-06]]"
  - "[[Maturation-Backlog-2026-06]]"
---

# Selection system — ISelectable / SelectContext

**Motivation (user, 2026-06-15):** picking/selection must be **engine-wide and extensible** — 3D voxels,
UI elements, 3D meshes, and custom capture all go through one abstraction, not a voxel-specific node. The
working GPU voxel pick (AR#35) becomes the **first provider** under this system. A UI selection provider
follows from the WSL customer branch (UI injection).

## Core types

- **`SelectContext`** — the single query object that flows through *all* selection logic:
  - `screenPoint` (crosshair = center today; real cursor when a cursor-release mode exists), `viewport`,
    `CameraData` (to build the world ray / unproject), input `modifiers` (Shift/Ctrl/Alt → Replace / Add /
    Toggle / Range), `button`, and the resolved `hit` + a reference to the running `SelectionSet`.
  - Built once per pick by the coordinator; passed by ref to each provider.
- **`SelectionId`** — domain-tagged stable identity: `{ ProviderKind kind; uint64 payload; }`
  (voxel → `payload = pickID`; UI → element handle; mesh → entity id). Cheap, comparable, hashable.
- **`ISelectable`** — the lightweight *result* identity + metadata for a hit: `SelectionId id`, optional
  `bounds`, optional display name. (NOT "every voxel is an object" — see Decision 1.)
- **`ISelectionProvider`** — owns a domain and resolves a context to its candidate hit:
  `std::optional<Hit> resolve(const SelectContext&)`, where `Hit = { SelectionId id; float depth;
  glm::vec3 worldPos; }`. Registered with the coordinator with a layer/priority.
  - **VoxelSelectionProvider** = the current GPU ID-buffer readback (PickingNode logic) → `pickID`.
  - **UiSelectionProvider** = rect/hit-test at `screenPoint` → UI element (from the WSL branch).
  - Mesh / custom providers slot in the same way.
- **`SelectionCoordinator`** (the generalized PickingNode): on a click edge, build the `SelectContext`,
  run providers in **priority order** (UI on top → 3D → voxel), take the topmost/nearest `Hit`, apply the
  modifier to the `SelectionSet`, and broadcast a **`SelectionChangedEvent`** (generalizes
  `PickResultEvent`).

## Priority / occlusion
Providers carry a layer priority; the coordinator queries highest-layer first and takes the first hit
(UI occludes the world). If two providers can report comparable depth, depth-sort across them. Start
simple: ordered priority (UI → voxel), first hit wins.

## Selection state
A **`SelectionSet`** (set of `SelectionId`) owned by the coordinator (engine-side), mutated per modifier
(Replace/Add/Toggle/Range), broadcast on change. This is the durable selection the current fire-and-forget
`PickResultEvent` lacks — consumers (highlight, UI, gameplay) subscribe.

## Migration from the shipped voxel pick
- `PickingNode`'s readback logic → **`VoxelSelectionProvider`** (unchanged GPU ID-buffer readback).
- `PickingNode` → **`SelectionCoordinatorNode`**: owns `SelectContext` construction + the provider list +
  the `SelectionSet`; emits `SelectionChangedEvent`. Providers are registered (initially just voxel).
- `PickResultEvent` → keep as the per-pick raw event, add `SelectionChangedEvent` for set changes (or fold).
- Keep `ComputePickRay` (it builds the `SelectContext` ray for ray-based providers / future cursor mode).

## WSL customer branch (UI injection) sync
The UI work lands a **`UiSelectionProvider`** (hit-test the injected UI at the screen point) registered
ahead of the voxel provider (UI occludes world). Sync = merge the WSL branch onto this base, then wire its
UI hit-test as a provider. (Identify the exact branch ref at sync time.)

## Decisions (confirmed 2026-06-15)
1. **Provider-owned hit-testing.** Each `ISelectionProvider` resolves its own domain (voxel = GPU
   readback, UI = rect test, mesh = ray test); `ISelectable` is just the result identity. No per-object
   intersect API, no instantiating 300k voxel selectables.
2. **Engine-native, node-based, dependency-light.** The `SelectionCoordinator` is a **native RenderGraph
   node** (`SelectionCoordinatorNode`) that owns the `SelectionSet` and broadcasts `SelectionChangedEvent`
   on the existing EventBus. Providers are **engine-native C++ interfaces** registered with the node — NO
   external dependencies (VIXEN keeps its dep surface minimal). Selection is "just another graph node",
   consistent with the rest of the engine. The app/UI/highlight subscribe to the event; the node is the
   single source of truth.

## Out of scope (later)
- Drag-rectangle multi-select (a *region* SelectContext → providers return multiple hits).
- Visual highlight of the selection (consume `SelectionChangedEvent` → tint via shader).
- World-pos / Morton from the voxel `pickID` (brick→world inverse).
