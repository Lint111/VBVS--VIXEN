---
title: Subgraph-as-node — composite/reusable graphs (architecture principle)
aliases: [subgraph, composite node, graph-as-node, reusable subgraph, CompositeNode]
tags: [architecture, design, rendergraph, composition, principle]
created: 2026-06-15
status: design note — principle established, capability greenfield, not started
related:
  - "[[RenderGraph]]"
  - "[[Selection-System-Design-2026-06]]"
  - "[[Maturation-Backlog-2026-06]]"
---

# Subgraph-as-node — composite / reusable graphs

**Principle (user, 2026-06-15):** *"If a process requires more than one node to compose it, it should be
assembled into a graph, and the graph itself should act as a node, to be reused — this prevents the
creation of nodes with the same functionality."*

I.e. **don't write a new monolithic node that re-implements behavior that already exists as nodes.**
Compose the existing nodes into a **subgraph**, give that subgraph defined inputs/outputs, and expose it
as a single reusable **composite node**. DRY at the graph level.

## Current state: greenfield

The RenderGraph today has flat nodes + connections + a topological executionOrder. There is **no**
composite-node / nested-graph abstraction — only `RenderGraph::CleanupSubgraph(rootNodeName)` (a cleanup
helper over a subtree) and node-access "encapsulation" (`INodeWiring`). So this is a new capability.

## What "subgraph-as-node" needs

A `CompositeNode` (a `NodeInstance` that *contains* an inner `RenderGraph`) with:
- **Boundary slots** — the composite's declared inputs/outputs map to inner nodes' slots (an interface:
  external slot ↔ internal `{node, slot}`). The outside sees one node; inside is a wired subgraph.
- **Construction** — a registered builder that assembles the inner graph (AddNode + Connect) once, like a
  mini `BuildRenderGraph`. Reusable: instantiate the composite many times.
- **Execution** — the composite's lifecycle (Setup/Compile/Execute/Cleanup) drives the inner graph's
  topo-sorted nodes. Either inline-expand the inner nodes into the parent's executionOrder at compile
  (flatten — simplest, keeps one scheduler), or run a nested executor. **Flattening at compile** is the
  lower-risk first cut (no nested scheduler, the existing barrier/sync/parallel machinery still sees one
  flat order).
- **Identity/registration** — a composite type is registered like any `NodeType`, so graphs (and other
  composites) can `AddNode<TheComposite>()`. Enables a library of reusable subgraphs.

## How it applies to the selection system (in progress)

The selection coordinator is **one node + engine-native C++ provider objects** (providers are not nodes),
so it does *not* itself require subgraph-as-node — and it already honors the DRY half of the principle by
**reusing** the existing GPU readback machinery (`PickIdTargetNode` + the readback) rather than
duplicating it. So the selection build can proceed without this capability. Subgraph-as-node becomes
relevant when a *process* genuinely spans multiple nodes (e.g. a "voxel-render" subgraph: VoxelGrid →
gatherer → descriptor → dispatch → present, packaged as one reusable `VoxelRenderComposite`).

## Candidates that would collapse into composites (DRY wins)

- The voxel-compute render chain (VoxelGrid → DescriptorGatherer → DescriptorSet → ComputePipeline →
  ComputeDispatch → Present) → one `VoxelRenderComposite`.
- The instancing demo raster chain (vertex+instance+mvp+texture → descriptor → pipeline → geometry →
  present) → one `InstancedMeshComposite`.
- A "pick" composite (PickIdTarget + the dispatch ID write + readback) once selection settles.

## Scope / recommendation

This is a **foundational graph capability** (comparable in weight to the auto-sync epic) — boundary-slot
mapping, the builder/registration, and the compile-time flatten-vs-nested-exec decision each need care +
tests, and it touches the core (`RenderGraph`, `NodeInstance`, topology/executionOrder). It deserves its
own focused design + build, not a tail-end add-on. Establish the **principle now** (prefer composing +
reusing over new monolithic nodes); build the **capability** as a dedicated epic.

## Out of scope (later, within the epic)
- Nested execution vs compile-flatten (start with flatten).
- Composite parameter forwarding; composite-of-composites.
- A visual/graph-editor representation of composites.
