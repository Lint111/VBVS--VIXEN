---
title: Quick Lookup Index
aliases: [Index, Lookup, Quick Reference]
tags: [index, navigation, claude]
created: 2025-12-07
---

# Quick Lookup Index

Fast-access reference for Claude Code. Read this file first for any documentation lookup.

---

## Recent Updates

| Date | Topic | File |
|------|-------|------|
| 2026-08-10 | E11-T1 stencil perf legs: tile reduction + evaluator-skip SHIPPED env-gated (`VIXEN_POLICY_STENCIL_TILES`, off-path byte-identical; stencil-byte-deterministic) — HONEST NEGATIVE on the dense scene: tiles+skip cost 63-67% MORE frame time than stencil-off; skip scoped to whole-tile-miss (engine can't express the doc's virtual/materialized split yet); NEW KI-052 undeclared RAW hazard on the ON-path (tile buffer has zero ResourceAccessTracker entries — fix path documented, gate before graduation). Payoff needs a sparse tile-uniform scene, which the rig lacks | [[../04-Development/Known-Issues]], `perf/e11-t1-stencil-perf-report.md` (undertow) |
| 2026-08-10 | E9/E10 orbital-demo chain GREEN: far-field LEVEL TARGETING solved — determinant = tree occupancy fraction `occFrac=(4/3·π·r³)/kN³` (≥0.0005 resolves, ≤0.00013 collapses to {0}; the kN-384 regression = starved occupancy); ⚠ RECIPE GOTCHA: octree resolution rounds UP to `nextpow2(kN)` BEFORE `bpa`/`depth` derive (SdfBake.h:399-409) — with the REAL depth, ALL 13 well-above-floor points hit top level = `depth−1` exactly (never depth; budget `depth ≥ L+1`). E10-T1: one carved slab+tower structure VISIBLE FROM ORBIT (~120wu, levels {0,1,2}, deterministic). NEW `VIXEN_HUD_SUPPRESS` capture mode (off-path byte-identical); E10-T1's 8,324-px count was 93% HUD chrome — corrected structure-region count 533 px, visibility claim stands | `perf/e9-t1-level-targeting-report.md`, `perf/e10-t1-orbital-fixture-report.md`, `perf/e10-t2-rung-calibration-report.md` (undertow) |
| 2026-08-10 | E8-T1 multi-rung fixture GREEN: 5 distinct `MipReadBytes` levels {0,1,2,3,4} (vs ≥4 bar) via plateau camera schedule + two non-tier-linked sibling bodies; ROOT CAUSE of every earlier level-0-only capture = missing `VIXEN_COMPOSED_TRAVERSAL` (`EnsureRtQueryTlasBuilt` returns immediately without it/`VIXEN_RTQUERY_TRAVERSAL` — the RTQuery far-field mip path never engages; ⚠ ANY capture protocol modeled on E6-T2b's env list needs the flag to exercise far-field). Also: `recordPolicyLevel` (SceneBindings.glsl:784) has zero call sites = dead instrumentation; single-body `kN`→level-set relation is NON-MONOTONIC (384 regressed to {0}) — empirically routed around, mechanism not understood | `perf/e8-t1-multirung-report.md` (undertow) |
| 2026-08-10 | E7-T1 stencil slice SHIPPED, MEASURED (orchestrator ruling): primary per-pixel policy stencil materialized into `HitRecord._pad0[2]` bits 8-15 behind `VIXEN_POLICY_STENCIL`; shadow-word preservation fixed in both writers, zero mismatches; off-identity byte-proof holds incl. KI-050 two-hash set; NEW KI-051 mixed-leg reservoir/secondary-wave COUNTER variance (stencil byte itself stays bit-identical) — characterized and parked, same bar as KI-049/KI-050 | [[../04-Development/Known-Issues]], `perf/e7-t1-stencil-report.md` |
| 2026-08-10 | Virtual-source census UNBLOCKED with zero code change: shipped Cornell virtual path yields deterministic 55.1M-sample pure-virtual primary population; mixed leg calibrates all three source classes; stencil re-gate source-axis prerequisite met; NEW KI-050 stored-control image bistability (orthogonal to KI-049, not counter-induced) | [[../04-Development/Known-Issues]] |
| 2026-08-09 | KI-049 bistability: frame-sync payload AND pipeline creation order both FALSIFIED (24-boot frozen adjudication; both rules degenerate to constant predictors; identical-fingerprint regime splits); next candidate = mip-policy state (12/12 LOW under VIXEN_MIP_POLICY=1, unproven) | [[../04-Development/Known-Issues]] |
| 2026-08-09 | KI-047 weighted coverage propagation SHIPPED (covMin 63/64, floor2 hash b654ae71→3951c2c5, identity byte-stable); composite blocker recharacterized: blend executes, second candidate is black — four unblock levers pending user pick | [[../04-Development/Known-Issues]] |
| 2026-08-09 | Residency unification design: one FootprintRegime function for render policy + brick residency; benchmark bandwidth instrumentation gap pinned; three-axis measurement plan | [[../Deep-Field-Residency-Unification-2026-08]] |
| 2026-08-09 | Deep-field mip policy cost closed: all four backend/regime cells certified (−7.91% to −15.23%); matched-level nodes are approximately pixel-order, with LEVEL_FLOOR test instrument and tree-of-trees production shape | [[../01-Architecture/Deep-Field-Mip-Accessor-Policy-2026-08]] |
| 2026-08-09 | Deep-field policy stencil / C3 successor is DESIGN-FUTURE and re-gate BLOCKED twice; current composition census is materialized-only, so source-axis skips need a procedural validation scene | [[../Deep-Field-Policy-Stencil-Grouping-2026-08]] |
| 2026-08-09 | Measurement closure: six-attempt lineage, completed fingerprint calibration, pairwise-precedence hypothesis, missing march→shadow semantic edge, resumable round-robin protocol, and cmd.exe UNC-cwd stall fingerprint | [[../01-Architecture/Measurement-Discipline-2026-08]] |
| 2026-08-09 | Known issues: KI-047 coverage adjudication, KI-048 UNC-cwd hang, KI-049 config-dependent boot-regime bias | [[../04-Development/Known-Issues]] |
| 2026-07-27 | Gaia bulk voxel mutation: multicore immutable assembly, single-owner commit, batched range upload, atomic page publication | [[../03-Research/Gaia-Bulk-Voxel-Mutation-and-Upload-Research-2026-07]] |
| 2026-07-26 | Voxel asset editor research: hybrid authoring, runtime-capability library partition, sim context, and explicit Blender ownership | [[../03-Research/Voxel-Asset-Editor-Product-Research-2026-07]] |
| 2026-07-18 | Recipe load tiers: footprint gating ✅; precision routing 🚧 mechanism complete/full consumer open; content-detail LOD deferred | [[../01-Architecture/Recipe-Load-Tier-Contract-Direction-2026-07]] |
| 2026-07-18 | Recipe nested invocation: M1 mechanism ✅; M2 unroll-vs-natural scale A/B open | [[../01-Architecture/Recipe-Nested-Invocation-Unroll-AB-Direction-2026-07]] |
| 2026-07-17 | Live recipe bucketing ✅ opt-in; measured no statistically clear performance win | [[../01-Architecture/Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] |
| 2026-07-15 | Recipe parameterization (`ReadParam`) + content-hash pipeline cache ✅ | [[../01-Architecture/Recipe-Parameterization-Plan-2026-07]] |
| 2026-07-12 | Sparse-mip residency + true-scale nested/tiered ESVO traversal ✅ | [[../01-Architecture/Tiered-ESVO-Observer-Addressing-Design-2026-07]] |
| 2026-07-12 | Lazy-Procedural Inc1b plan: resolvability-gated recipe evaluation ("mip for compute") | [[../01-Architecture/Lazy-Procedural-Delta-Baseline-Inc1b-Plan-2026-07]] |
| 2026-07-10 | Kernel physics dispatch contract (field/mip-aware simulation job ABI) | [[../01-Architecture/Kernel-Physics-Dispatch-Contract-Spec-2026-07]] |
| 2026-07-03 | Voxel authoring editor Inc1 (VoxelDocument format + vixen_editor app) | [[../01-Architecture/Voxel-Authoring-App-Inc1-Design-2026-07]] |
| 2026-06-14 | Embedding VIXEN in a host (find_package → EngineContext → own-the-loop) [AR#12] | [[../06-Embedding/Hosting-VIXEN]] |
| 2026-06-12 | Consumer feedback / feature requests (UNDERTOW integration) | [[../05-Progress/features/consumer-feedback-undertow]] |
| 2026-01-04 | Sprint 6.0.1: Unified Connection System COMPLETE (archived record) | [[../_archive/2026-07/features-sprints/Sprint6.0.1-Unified-Connection-System]] |

---

## Architecture

| Topic | File |
|-------|------|
| System overview | [[../01-Architecture/Overview]] |
| RenderGraph system | [[../01-Architecture/RenderGraph-System]] |
| RenderGraph connection API (archived sprint record) | [[../_archive/2026-07/features-sprints/Sprint6.0.1-Unified-Connection-System]] |
| Vulkan pipeline | [[../01-Architecture/Vulkan-Pipeline]] |
| Type system | [[../01-Architecture/Type-System]] |
| Voxel authoring app Inc1 (VoxelDocument format, flatten-to-VRC1, vixen_editor) | [[../01-Architecture/Voxel-Authoring-App-Inc1-Design-2026-07]] |
| Kernel physics dispatch contract | [[../01-Architecture/Kernel-Physics-Dispatch-Contract-Spec-2026-07]] |
| Sparse-mip ESVO LOD | [[../01-Architecture/Sparse-Mip-ESVO-LOD-Direction-2026-07]] |
| Tiered ESVO observer addressing | [[../01-Architecture/Tiered-ESVO-Observer-Addressing-Design-2026-07]] |
| Runtime tiered recipe pipeline/JIT | [[../01-Architecture/Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] |
| Recipe load-tier contract | [[../01-Architecture/Recipe-Load-Tier-Contract-Direction-2026-07]] |
| Recipe spatial contract / two-pass culling (future) | [[../01-Architecture/Recipe-Spatial-Contract-Two-Pass-Culling-Direction-2026-07]] |
| Deep-field mip accessor policy / certified cost | [[../01-Architecture/Deep-Field-Mip-Accessor-Policy-2026-08]] |
| Measurement discipline / wavefront evidence rules | [[../01-Architecture/Measurement-Discipline-2026-08]] |

---

## Implementation How-To

| Task | File |
|------|------|
| Create a new node | [[../templates/Node-Documentation]] |
| Embed VIXEN in a host app | [[../06-Embedding/Hosting-VIXEN]] |
| Add logging | [[../Libraries/Logger]] |
| Write tests | [[../04-Development/Testing]] |
| Coding standards | [[../04-Development/Coding-Standards]] |

---

## Libraries

| Library | File |
|---------|------|
| RenderGraph | [[../Libraries/RenderGraph]] |
| SVO | [[../Libraries/SVO]] |
| ShaderManagement | [[../Libraries/ShaderManagement]] |
| EventBus | [[../Libraries/EventBus]] |
| CashSystem | [[../Libraries/CashSystem]] |
| Logger | [[../Libraries/Logger]] |
| ResourceManagement | [[../Libraries/ResourceManagement]] |

---

## Research & Algorithms

| Topic | File |
|-------|------|
| Gaia bulk voxel mutation, job dispatch, region upload, and removal semantics | [[../03-Research/Gaia-Bulk-Voxel-Mutation-and-Upload-Research-2026-07]] |
| Voxel asset editor product, runtime-share partition, UI/UX, materials, metadata, and Blender round-trip | [[../03-Research/Voxel-Asset-Editor-Product-Research-2026-07]] |
| ESVO (Efficient Sparse Voxel Octrees) | [[../03-Research/ESVO-Algorithm]] |
| Voxel ray-tracing research overview | [[../03-Research/Overview]] |
| Voxel / field physics | [[../03-Research/Voxel-Field-Physics-Research-2026-07]] |
| DXT compression | [[../02-Implementation/Compression]] |

---

## Session Context

| File | Purpose |
|------|---------|
| `memory-bank/activeContext.md` | Current focus, recent changes |
| `memory-bank/progress.md` | What's done, what's left |
| [[../05-Progress/archive/2025-12/Current-Status]] | Archived detailed status (December 2025) |
| [[../05-Progress/archive/2025-12/Phase-History]] | Archived project timeline |

---

## Project Management & Integration

| Topic | File |
|-------|------|
| HacknPlan integration guide | [[../04-Development/HacknPlan-Integration]] |
| Session workflow integration | [[../04-Development/Session-Workflow-Integration]] |
| MCP Sync Engine design (archived) | [[../_archive/2026-07/mcp-development/glue-sync-engine]] |

---

## Analysis

| Topic | File |
|-------|------|
| Benchmark data summary | [[../Analysis/Benchmark-Data-Summary]] |
| Data quality report | [[../Analysis/Data-Quality-Report]] |

---

## Node Catalog

### Infrastructure
- WindowNode, DeviceNode, SwapChainNode, FrameSyncNode

### Pipeline
- RenderPassNode, FramebufferNode, GraphicsPipelineNode, ComputePipelineNode, DescriptorSetNode

### Rendering
- GeometryRenderNode, ComputeDispatchNode, PresentNode, UIRenderNode (RmlUi → swapchain)

### Resources
- DepthBufferNode, TextureLoaderNode, VertexBufferNode, ShaderLibraryNode

### Specialized
- CameraNode, VoxelGridNode, LoopBridgeNode, ConstantNode

### Applications
- `application/main` — the standalone `VIXEN.exe` (default 3-body scene)
- `application/editor` — `vixen_editor`: loads a `.vxd` VoxelDocument, layer-list UI (RmlUi), live preview via the existing recipe-pool render path (see [[../01-Architecture/Voxel-Authoring-App-Inc1-Design-2026-07]])

---

## Code Paths

| Component | Path |
|-----------|------|
| RenderGraph core | `libraries/RenderGraph/src/Core/` |
| Node implementations | `libraries/RenderGraph/src/Nodes/` |
| Node configs | `libraries/RenderGraph/include/Data/Nodes/` |
| SVO library | `libraries/SVO/` |
| Shaders | `shaders/` |
| Main application | `application/main/` |
| Voxel authoring editor app | `application/editor/` |

---

## Search Patterns

For grep searches when file not in index:

```
# Find node implementation
grep "class.*Node.*:" libraries/RenderGraph/

# Find slot definitions
grep "INPUT_SLOT\|OUTPUT_SLOT" libraries/RenderGraph/include/

# Find Vulkan calls
grep "vk[A-Z]" libraries/

# Find ESVO algorithm
grep "ESVO\|traversal\|octree" libraries/SVO/
```
