---
title: Selection system — ISelectable / SelectContext (engine-wide picking)
aliases: [ISelectable, SelectContext, ISelectionProvider, SelectionCoordinator, selection system]
tags: [architecture, design, rendergraph, picking, selection, input]
created: 2026-06-15
status: DONE (core + coordinator + voxel provider) — refactored 2026-06-15 so PROVIDERS ARE GRAPH NODES feeding the coordinator via a candidate slot. Candidate fan-in is single-source today (engine has no runtime accumulation-gather — see "Engine gap"); resolution is already N-ready. UI provider syncs from WSL branch next.
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

- **`SelectContext`** — *(removed in the providers-are-nodes refactor, 2026-06-15)*. It was the single
  query object the coordinator built per pick and passed by ref to each C++ provider's `resolve()`. Now
  each provider is a node and reads exactly the graph inputs it needs (the voxel node: InputState +
  ID_IMAGE + viewport; a future ray provider: + CameraData), so there is no shared context object. The
  modifier (Shift/Ctrl/Alt → Replace/Add/Toggle/Range) is read by the coordinator from InputState. A
  future cursor-release mode adds a screen-point input to the providers that need it.
- **`SelectionId`** — domain-tagged stable identity: `{ ProviderKind kind; uint64 payload; }`
  (voxel → `payload = pickID`; UI → element handle; mesh → entity id). Cheap, comparable, hashable.
- **`SelectionCandidate`** — the graph-passable *result* a provider node emits on its `CANDIDATE`
  output: `{ bool hit; SelectionId id; float depth; int priority; glm::vec3 worldPos; }`. A provider
  emits one **every frame** (`hit=false` off the click edge / on a miss); it carries `priority` (the
  provider's layer) and `depth` so the coordinator resolves occlusion without holding a provider list.
  Registered as a compile-time resource type (so it — and `std::vector<SelectionCandidate>` for the
  fan-in slot — are valid slot value types). It supersedes the old C++ `Hit`/`ISelectionProvider`.
- **Provider NODES** — each owns a domain and is a first-class graph node that resolves its domain on
  a click and emits a `SelectionCandidate`:
  - **`VoxelSelectionProviderNode`** = the GPU ID-buffer readback (was the PickingNode logic / the
    short-lived C++ `VoxelSelectionProvider`) → `pickID`. Inputs: InputState, ID_IMAGE, device,
    command pool, frame index, viewport w/h. Param `PARAM_PRIORITY` (default 0 = world layer).
  - **UI provider node** = rect/hit-test at the screen point → UI element (from the WSL branch); it
    wires its `CANDIDATE` into the SAME coordinator slot with a higher priority so UI occludes world.
  - Mesh / custom provider nodes slot in identically — just another node into the same MultiConnect slot.
- **`SelectionCoordinatorNode`** (the generalized PickingNode): consumes a provider candidate; on a click
  edge it picks the best (**max `priority`, tie-break min `depth`** — `pickBestCandidate()`, the shared
  rule, which takes a `std::vector<SelectionCandidate>` and is N-provider-ready), applies the modifier to
  the `SelectionSet`, and broadcasts a **`SelectionChangedEvent`** (generalizes `PickResultEvent`). It
  owns no providers and does no GPU work. The candidate input is **intended** to be a MultiConnect
  accumulation slot (`PROVIDER_CANDIDATES`, `std::vector<SelectionCandidate>`) but is a single-source slot
  (`PROVIDER_CANDIDATE`) today — see "Engine gap" below.

## Priority / occlusion (the candidate fan-in)
Each provider node stamps its `CANDIDATE` with a `priority` (its layer). The coordinator resolves with
**max `priority`, tie-break min `depth`** (`pickBestCandidate()`, `Selection/SelectionResolve.h`),
non-hits ignored — UI (high priority) occludes the world voxel (priority 0). `pickBestCandidate` takes a
`std::vector<SelectionCandidate>`, i.e. it is already N-provider-ready; only the wiring is single-source
today (see below).

### Engine gap: no runtime accumulation-gather (why the fan-in is single-source today)
The intended shape is a **MultiConnect/Accumulation** input on the coordinator
(`std::vector<SelectionCandidate>`) so MANY provider nodes feed ONE coordinator. The RenderGraph engine
does not support this **at runtime**: `AccumulationConnectionRule::Resolve` only records the source list
+ an ordering edge and **never assembles the `std::vector<T>`** onto the consumer input. There is no
execute-phase (or compile-phase) gather that reads the sources' outputs into the vector, so
`ctx.In(accumSlot)` returns an **empty** vector every frame (confirmed at runtime: "0 candidate(s)
gathered" while the provider was emitting hits). `MultiDispatchNode` only reads its accumulation slot at
**compile** (a one-time snapshot), and `BoolOpNode`'s accumulation has no runtime test — neither
exercises a per-frame gather. The blocker is type erasure: `Resource` stores values in a `std::any`, so a
type-erased connection rule can't append element `T` into `std::vector<T>` without a per-element-type
registry (or a typed PreExecute hook registered from the typed `Connect` site — the infra exists via
`GraphLifecycleHooks` `PreExecute`, as `VariadicConnectionRule` uses, but wiring it for accumulation with
correct vector ownership/reset is a separate engine feature, out of scope for the selection refactor).

So **today** the coordinator's candidate input is a single-source Execute slot (`PROVIDER_CANDIDATE`,
`SelectionCandidate`) wired with a plain `Connect` — `DirectConnectionRule` DOES wire value structs every
frame (the same path `CameraData` flows through), so the candidate reaches the coordinator and the live
pick works. The coordinator still resolves through a vector, so when the engine grows an
accumulation-gather (or a tiny candidate-merge node is added), the slot flips to
`std::vector<SelectionCandidate>` with **no change to the resolution logic**. See backlog: "implement
RenderGraph runtime accumulation-gather (PreExecute typed hook)".

## Selection state
A **`SelectionSet`** (set of `SelectionId`) owned by the coordinator (engine-side), mutated per modifier
(Replace/Add/Toggle/Range), broadcast on change. This is the durable selection the current fire-and-forget
`PickResultEvent` lacks — consumers (highlight, UI, gameplay) subscribe.

## Migration from the shipped voxel pick
- `PickingNode`'s readback logic → **`VoxelSelectionProviderNode`** (unchanged GPU ID-buffer readback;
  it was briefly a C++ `VoxelSelectionProvider` object the coordinator owned, then MOVED verbatim into a
  graph node in the providers-are-nodes refactor — the device is the base `NodeInstance::device`).
- `PickingNode` → **`SelectionCoordinatorNode`**: gathers `SelectionCandidate`s via the MultiConnect
  `PROVIDER_CANDIDATES` slot, owns the `SelectionSet`, emits `SelectionChangedEvent`. It no longer holds
  a provider list, builds a `SelectContext`, or touches Vulkan.
- `PickResultEvent` → keep as the per-pick raw event, add `SelectionChangedEvent` for set changes (or fold).
- `ComputePickRay` stays for future ray-based provider NODES / a cursor-release mode (such a provider
  reads CameraData itself).

## WSL customer branch (UI injection) sync
The UI work lands a **UI selection provider NODE** (hit-test the injected UI at the screen point) with a
higher `PARAM_PRIORITY` than the voxel node (UI occludes world). Adding a SECOND provider is the point
where the **engine accumulation-gather gap** (above) must be closed: either implement the runtime
accumulation-gather (then both provider nodes `.Connect` their `CANDIDATE` into the coordinator's
`std::vector<SelectionCandidate>` MultiConnect slot — no resolution change), or add a tiny candidate-merge
node that takes two `SelectionCandidate` inputs and emits the better one. `pickBestCandidate` already
handles the multi-candidate resolution; only the transport needs finishing. (Identify the exact branch
ref at sync time.)

## Decisions (confirmed 2026-06-15)
1. **Provider-owned hit-testing.** Each provider node resolves its own domain (voxel = GPU readback, UI =
   rect test, mesh = ray test) and emits a `SelectionCandidate`. No per-object intersect API, no
   instantiating 300k voxel selectables. (The SEL-P1 `ISelectable`/`Selectable` result-identity struct
   was unused and removed; `SelectionId` is the identity that flows.)
2. **Everything is a node (providers included).** Both the coordinator AND each provider are **native
   RenderGraph nodes**; a provider feeds the coordinator through a graph slot — no C++ provider interface,
   no coordinator-owned provider list. This is the subgraph-as-node / everything-is-a-node principle
   applied to selection: a new domain = a new node, wired in. The *intended* transport is the graph's
   MultiConnect/Accumulation fan-in (many providers → one slot), but that needs an engine
   accumulation-gather that doesn't exist yet, so the candidate flows over a single-source slot today
   (see "Engine gap"); the everything-is-a-node decision is unaffected — it's a transport detail. Still
   dependency-light (Vulkan + engine-native Selection headers only) and engine-native; the
   app/UI/highlight subscribe to `SelectionChangedEvent`; the coordinator node owns the `SelectionSet`
   (single source of truth).

## Out of scope (later)
- Drag-rectangle multi-select (a *region* SelectContext → providers return multiple hits).
- Visual highlight of the selection (consume `SelectionChangedEvent` → tint via shader).
- World-pos / Morton from the voxel `pickID` (brick→world inverse).
