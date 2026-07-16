---
title: Dormant / Unwired Foundational Work — Live-Path Inventory
status: complete (inventory); integrations scheduled in the pipeline plan
created: 2026-07-16
---

# Dormant-Work Inventory (2026-07-16)

> Provenance: read-only git-history + live-path investigation (dedicated Opus agent,
> user question: "a lot of foundational work is not integrated in the main render
> dispatch pattern"). Live path = the `BuildRenderGraph.cpp` compute graph (74 nodes).
> Integrations from this inventory are scheduled in
> [[Baked-Perf-Fix-Pipeline-Plan-2026-07]] (M2b, M4.3, M5.1, M6.5, M7.4 + Phase-2
> backlog). Classifications: LIVE / GATED (off by default) / DORMANT (unwired) /
> PARTIAL / DEAD.

**Load-bearing scope correction:** the TRUE default boot (no env var,
`BuildRenderGraph.cpp:3862-3896`) seeds **3 procedural bodies — zero bake, zero
octree**. Only `VIXEN_DDGI_CORNELL_BAKED_DEMO` bakes stored-SDF octrees. "The baked
path" = the baked Cornell demo; default boot and the virtual demo are analytic.

## Ranked inventory

| # | Work item | Class | Evidence | Wire | Value (baked-perf push) |
|---|---|---|---|---|---|
| 1 | `direct_lighting` ReSTIR dispatch | **DEAD every frame, all paths** | `reservoirEnabled=0` `ReservoirConfigNode.cpp:34`; shader early-return `DirectLighting.comp:216`; no CPU skip-guard (`ComputeStageNode.cpp:425` only guards zero-size) | M | removal win, low-med |
| 2 | `probe_update` DDGI on DEFAULT boot | **DEAD on default boot; LIVE on Cornell** | `probeGridEnabled=0` `ProbeGridConfigNode.cpp:51`; early-return `ProbeUpdate.comp:330`; Cornell force-enables `BuildRenderGraph.cpp:3370`, consumed `SpatialReuseShade.comp:490-499` | M | high on non-DDGI boots; NOT skippable on Cornell |
| 3 | Shader disk cache | **DEAD — glslang recompile every boot** | `ShaderCacheManager` only in its test; live builders `BuildRenderGraph.cpp:837-1031` never call `EnableCaching` (`ShaderBundleBuilder.cpp:347`) | S-M | high (boot). HAZARD: key must hash the SPLICED source (`:871-892`), not file bytes |
| 4 | SerializeSdf bulk entity paths | bulk **DEAD**; live path slow | `getBrickEntitiesInto` `GaiaVoxelWorld.cpp:432` / `getEntityFast` `EntityBrickView.cpp:157` zero callers; live per-voxel `getEntityByWorldSpace` + 4× fetch `ShellOctreeGpu.h:717,723-762` | M | med (boot) |
| 5 | Instance SSBO upload | **PARTIAL / negative-work** | full `PackInstances`+memcpy every frame `BodyOctreeSceneNode.cpp:401-419`; dirty-upload only a direction doc | M-L | low-med (52-FPS-ceiling item) |
| 6 | Sparse-Mip LOD march (`MipFallback.glsl`) | **DORMANT** (baked+uploaded on Cornell, read side inert) | pools baked `BuildRenderGraph.cpp:3302`; included `SceneBindings.glsl:279`; suppressed by forced eager residency `:3352` + sub-pixel footprint never fires `SceneBindings.glsl:947` | M | med — scheduled **M4b** |
| 7 | Recipe content-hash cache (JIT Inc1) | **DORMANT — write-only, and NOT a bake cache** | write `VulkanGraphApplication.cpp:2497`, zero readers; dedups recipe bytecode only. **No bake-skip cache exists anywhere** — the 87-190 s bake re-runs every boot | L | none as-is; real bake cache = new design (M7.4) |
| 8 | Box-tight bake regions (`cd9e1362`) | **PARTIAL** (thin axis only) | ON by default `BuildRenderGraph.cpp:3214-3223`; cube grid still forced `SdfBake.h:405`, `SVORebuild.cpp:260` | L | real fix = per-axis grids (M5 Phase-2) |
| 9 | Tiered-ESVO tier-crossing (Inc1+2) | **DORMANT — inert on ALL live scenes** | demo-gated `:1527`; single-tier scenes: `getFarBit` false + empty `tierRefTable` `SceneBindings.glsl:722,728`; even demos run childScale==1.0 | L | none at room scale (planet/orbit payoff) |
| 10 | `OctreeConfig.gridMin/gridMax` | **DORMANT / negative-work** | uploaded `OctreeConfig.g.h:27-34`; read only by un-dispatched `VoxelRayMarch.comp:494`; live path uses matrices (`TraceWorld.glsl:203`) | M | low — fold removal into M5.1 schema change |
| 11 | Temporal accumulation (history nodes) | **GATED off** (fully wired) | `enabled=0` `AccumulationConfigNode.cpp:43`; read-when-on verified `SpatialReuseShade.comp:572,588` | S | none (quality feature) |
| 12 | Widescreen render-scale (M4, `9ddbb854`) | **GATED off** | `renderScale=1.0` default `BuildRenderGraph.cpp:696`; only `VIXEN_RENDER_SCALE` lowers (27% dispatch cut at 0.5) | S | med-high — validated dial nobody turns (M6.5) |
| 13 | debug_capture 10-frame readback | **LIVE negative-work on main** | `PARAM_AUTO_EXPORT=true` `BuildRenderGraph.cpp:3927` (fixed in pipeline worktree M0.2; lands on main at pipeline merge) | S | med for benches |
| 14 | Spec-constant plumbing | **DEAD** | `ComputePipelineCacher.cpp:154-162` gated on maps that have zero producers | M | none/low (feed-or-remove at spec-constant work) |
| 15 | MultiDispatchNode | **DEAD** (in no graph) | registered `MultiDispatchNode.cpp:743`; zero `AddNode` in /application; un-scoped global barrier `:653-668` | L | none — archive candidate |

## Seed-list corrections (stale beliefs fixed)

- **InstanceSort is LIVE** — its cull consumer was wired in M4b of an earlier epic
  (`TraceWorld.glsl:266-279`, `entryTWorld>bestT` skip); the audit's "structurally dead"
  claim referred to the bounds problem, not the sort.
- **RaySizeCoefNode is LIVE** — consumed for ESVO descent termination
  (`OctreeTraversal-ESVO.glsl:213`), not a dormant LOD path.
- **The JIT Inc1 cache does not cache the bake** — recipe-bytecode dedup, write-only.
- **PickIdTarget is PARTIAL by design** (1-instruction tail on the live march; read on
  click) — not removable, costs no dispatch.

## Top integration opportunities (value-per-effort, realtime-Cornell)

1. CPU-side no-op dispatch guards (#1+#2) — NEW → pipeline **M4.3 (extended)**.
2. Shader disk cache (#3) — pulled forward → **M2b**.
3. Sparse-Mip secondary rays (#6) — already **M4b**.
4. SerializeSdf bulk path (#4) — already **M7.2**.
5. Widescreen render-scale exposure (#12) — NEW → **M6.5** (bench capability curve).

Plus: real bake-artifact cache does not exist → **M7.4** (design-first, L);
instance-SSBO dirty upload + dead-branch cleanup (MultiDispatchNode, spec-constants,
gridMin/gridMax) → Phase-2 backlog.
