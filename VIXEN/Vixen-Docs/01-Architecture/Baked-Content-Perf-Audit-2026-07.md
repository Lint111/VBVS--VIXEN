---
title: Baked-Content Perf Audit — Verified Findings + Research Patterns
status: complete (audit); follow-up actions are tracked and partially landed in the
`Baked-Perf-Fix-Pipeline` (`e92acf44`, `1964c134`).
created: 2026-07-16
---

> Provenance: multi-agent audit workflow `wf_cbb5d960-e5d` (2026-07-16) — 9 area audits +
> 3 web-research agents, every finding adversarially verified against main by a dedicated
> refuter agent (22 agents total). Result: **74 confirmed, 1 refuted, 0 uncertain**
> findings + 35 research patterns, synthesized below. Companion state doc:
> [[Baked-Content-Perf-Consolidation-2026-07]].
>
> The one REFUTED finding, for the record: "Cross-instance occlusion reject is dead when
> the camera is inside an octree's AABB — every pixel fully traverses and SDF-marches all
> 5 overlapping wall octrees" (the reject is dead for a different, confirmed reason — see B3).

# Baked Cornell Performance Audit — Synthesis Report

**Scope:** Baked stored-SDF Cornell demo (`VIXEN_DDGI_CORNELL_BAKED_DEMO=1`), context per `Vixen-Docs/01-Architecture/Baked-Content-Perf-Consolidation-2026-07.md`. Established numbers: baked baseline **0.9–1.1 FPS** (877 ms wall, 222 ms esvo pass on fresh Windows-native), virtual ground truth ~126–154 FPS, Exp-A ceiling **~52 FPS** (march stubbed), red-worktree runs **wall 62–170 ms vs ~30 ms GPU esvo pass**. All findings below were adversarially verified against code on main.

---

## 1. Top 10 Actions (ranked by expected-impact-per-effort on baked Cornell)

| # | Action | File:line | Expected impact | Effort | Backing |
|---|--------|-----------|-----------------|--------|---------|
| 1 | **Fix `brickLookup` base for mixed `bricksPerAxis`**: add `OctreeConfig.brickLookupBase` stamped as exact prefix sum in `ConcatenateSdf`/`ConcatenateSdfWithMips`; read it in `_samplePoolVoxel` and `_sdfBrickAllocated` | `StoredSdf.glsl:78,:235`; `ShellOctreeGpu.h:993`; `MipBake.h:361` | Fixes garbage-addressed marches on octrees 5–7 every ray, and is the prime candidate for the vanishing-bodies blocker **gating the proven ~3.8× `*subdiv` fix** — highest single multiply available | **S** | Confirmed finding (brickLookupBase, ×2 areas); `ShellDerive.h:433` proves the correct formula; red commit `6568c3ee` has the repair |
| 2 | **Land the `*subdiv` unit fix + grid-unit band** once #1 unblocks it: scale eval to grid units and fix `kBand=2.0` world-units→8-voxel band | `BuildRenderGraph.cpp:3090,:3114-3119`; `SdfBake.h:121,:172` | Doc-proven **~3.8×** (0.9→~3.4) + band shrink cuts allocated shell 8→~2 voxels, multiplying with miss-path cost | **S–M** | Consolidation doc §2 (proven); confirmed band findings (×2); validate vs `[CornellDiag]` instIdx map |
| 3 | **NdotL/BRDF gate before shadow traces** (3-line reorder) | `SpatialReuseShade.comp:326-334,:470-474` | Skips ~half of all analytic shadow scene-marches (each the same cost class as a primary ray) for zero image change; `ProbeUpdate.comp:236-241` already proves the idiom | **S** | Confirmed (trace-before-NdotL, ×2 areas) |
| 4 | **Analytic gradient from the hit cell's 8 corners** (replace 7-trilinear-sample `sdfGradientStored`, incl. redundant re-sample of d0) | `StoredSdf.glsl:197-222,:446`; caller `marchBrickSdf:444` | Removes ~56 corner chains (~150–330 SSBO loads) per hit, per pass — comparable to the whole march loop for near-entry hits | **S** | Confirmed; red worktree `sdfGradientStoredFromCell` prototyped it |
| 5 | **Gate debug capture + GPU trace hooks out of production**: `#ifdef` `TraceRecording`/`snapshotTraversalState`/`DebugRaySample`/`instanceIterCount`; default `AUTO_EXPORT=false` on `DebugBufferReaderNode` | `TraceRecording.glsl:29,:155`; `SceneBindings.glsl:667-996,:78`; `TraceWorld.glsl:332`; `DebugBufferReaderNode.cpp:103-105`; `BuildRenderGraph.cpp:3927-3930` | ~20% of esvo pass (red CSV: 24.5 vs 30–31 ms) + kills the every-10th-frame `vkWaitForFences(UINT64_MAX)` drain + JSON export (~10% of run) and de-noises every future bench | **S** | Confirmed (×5 overlapping findings); Exp-A ceiling component per doc §5.4 |
| 6 | **Single-brick trilinear fast path + hoisted channel base**: 1 `brickLookup` fetch + 8 contiguous pool loads (`+{0,1,8,9,64,65,72,73}`) when the 2×2×2 cell is intra-brick (~67% of cells); hoist `channelBaseFloats(SEM_SDF)` to march entry | `StoredSdf.glsl:160-177,:94-96,:45-50,:62-88` | ~3–4× SSBO-load reduction inside the **Exp-A-proven dominant loop** (0.09→52 FPS when stubbed); part of the red config that hit ~16 FPS | **M** | Confirmed (trilinear amplification cluster, ×4 areas); red `_loadSdfTrilinearCell` |
| 7 | **True any-hit shadow/probe path with tmax clamp**: separate march variant with no gradient/color/roughness, instance reject at entry-t > tmax, traversal span clamped at light distance | `TraceWorld.glsl:393-508,:495-502`; `SceneBindings.glsl:513-528`; callers `SpatialReuseShade.comp:328,:470`, `ProbeUpdate.comp:241` | Every occluded shadow ray wastes ~88 corner chains; shadow marching ~doubles the dominant brick-march tax in the baked demo (untimed passes = most of the 877−222 ms remainder) | **M** | Confirmed (any-hit cluster, ×4 areas); keep a separate function, not a runtime flag (red measurement) |
| 8 | **Conservative allocated-brick trace bounds per octree**: record min/max brick AABB in `SerializeSdf`'s existing loop → `traceBoundsMin/Max` config fields → AABB cull + front-to-back reject in `TraceWorld`/`TraceWorldShadow` | `TraceWorld.glsl:234,:266-279`; `SdfBake.h:399-406`; `ShellOctreeGpu.h:791-804` | Revives the structurally-dead cull/occlusion-reject (oversized 32-unit wall cubes make all 8 descents run per ray, ~89M starts/frame); red `trace_bounds` ranked 2nd-best (65.2 ms wall) | **M** | Confirmed (×2 findings); unvalidated implementation exists on `fix/baked-perf-red-experiments` |
| 9 | **Whole-frame GPU span + per-pass timers before more sync work** (march CB start → UI CB end; columns for direct_lighting/spatial_reuse/probe_update) | `FrameSyncNode.cpp:149`; `VulkanGraphApplication.cpp:505-508`; `BuildRenderGraph.cpp:337-665` | Only 1 of 7 submits is timed; the "~125 ms CPU stall" is largely untimed GPU brick-march in shadow/probe passes — this attribution gate prevents chasing sync mis-attributions (red sync experiments moved wall only ~5%) | **S** | Confirmed (pacing-fence finding); doc §1 independently agrees |
| 10 | **Boot triage**: delete TEMP DIAG re-serialize loop (23 s measured), gate rebuild Phase 4 on `m_signedDistanceField` (~50 s), parallelize the 8 independent bakes (~5–7×) | `BuildRenderGraph.cpp:3283-3301,:3214-3237`; `SVORebuild.cpp:646` | ~4.3-min boot → well under a minute; multiplies every A/B iteration of this investigation (doesn't change steady-state FPS) | **S+S+M** | Confirmed (×6 boot findings, log-timestamped) |

**Deep fix queued behind #6:** SDF brick pool as 3D texture with hardware trilinear (finding at `SceneBindings.glsl:54`) — the doc's own hypothesis-stack item 2's endgame; effort L, see research patterns.

---

## 2. Confirmed Findings by Area (deduped)

### A. Stored-SDF march inner loop (the proven dominant cost)

**A1. Trilinear fetch amplification — per-corner channel scan + per-corner brick lookup** *(merged from 4 findings across stored-sdf-march / instanced-traversal / lighting-probes / descriptors-pipelines)*
`StoredSdf.glsl:160-177` (`sampleSdfTrilinear` = 8 independent `_sampleSdfVoxel`), `:94-96` (per-corner `channelBaseFloats(SEM_SDF)` dynamic scan over `octreeConfig.channels[]`, `:45-51`), `:62-88` (per-corner `brickCoord` math + `brickLookup[]` fetch + `configs[]` re-reads). ~24–32 dependent SSBO loads per march step where an intra-brick cell (~67% by `(7/8)³`) needs 1 lookup + 8 contiguous pool loads. Runs every sphere-trace step (`:441-453`, MAX_STEPS=96), multiplied by up to 8 instances × 3 full-res marching passes. Impact: **high**. Caveat: driver may CSE some readonly loads — the 8 dynamically-addressed `brickLookup` fetches cannot be statically coalesced.

**A2. Hit-normal costs 7 full trilinear samples (56 corner chains) including a redundant d0**
`StoredSdf.glsl:197-222` (`sdfGradientStored`), `:199` re-samples exactly what `marchBrickSdf:444` just computed. The analytic trilinear-cell gradient is closed-form from the 8 corners already loaded. Paid by every hit in every pass — including shadow/probe hits whose normal is discarded (`SpatialReuseShade.comp:328,:470`; `ProbeUpdate.comp:241`). Impact: **high**, effort S.

**A3. Hit-shading gathers re-resolve the same 8 corners 4×** *(merged ×2)*
`StoredSdf.glsl:137-151`: `sampleChannelVec3Trilinear` re-runs the full corner resolution per component (16 of 24 chains duplicate; only `comp*512` differs); roughness (`:103-123`, called `SceneBindings.glsl:527-528`) resolves the same cell a 4th time. Once per hit; rides along with the A1 cell-loader refactor. Impact: low, effort S.

**A4. Miss rays re-march the brick chain quadratically + fresh per-hop budgets** *(merged ×2)*
`StoredSdf.glsl:379-381` — `marchBrickSdf` takes state/coef/stack **by value**; its hop loop (`:418`, `MAX_BRICK_HOPS=2048` at `:407`) walks every remaining leaf via `_advanceToNextSdfLeaf` (fresh MAX_ITERS=512 per hop, `:297`), then on a miss all progress dies and the outer loop (`SceneBindings.glsl:929`) advances one leaf and calls it again → K(K+1)/2 brick traces for a K-brick miss. Sentinel-contaminated face samples force 1-voxel probe crawl (`:453`). Hits miss-heavy populations: LIT shadow rays, grazing primaries, probe rays; K inflated by the thick band (B2). Impact: **medium** (per-effort strong on shadow/probe rays), effort M. Fix: commit march progress (inout / return marched arc-length) or let the outer loop drive brick continuation — re-verify the concave-seam notch regression.

**A5. By-value ~290 B stack/state copy per SDF leaf entry**
`SceneBindings.glsl:513` → `StoredSdf.glsl:379-381`: 23×8 B dynamically-indexed (scratch-resident) stack + ~40 B state + ~52 B coef copied per leaf entered per ray; semantically required by the current structure, so it survives optimization. Impact: medium, effort M (resolves naturally if A4's restructure lands).

**A6. Brick pool is a raw SSBO — no filterable/apron-ed representation exists**
`SceneBindings.glsl:54` (binding 11 raw float SSBO, apron-less 512-float SoA slabs; no `sampler3D`/`image3D` anywhere in shaders). Forces manual 8-corner trilinear (A1), sentinel probe-crawl at brick faces (A4), and 12 KB brick stride that scatters the hot 2 KB SDF lanes. Dense per-octree R16F volumes for Cornell = only ~20 MiB. Impact: **high** (deep fix), effort **L**. Descriptor infra for COMBINED_IMAGE_SAMPLER exists; upload path is new work.

### B. Bake-side geometry that inflates march work

**B1. GPU lookup base assumes uniform `bpa` — mis-addressed mixed-resolution scenes** *(merged ×2 — Top action #1)*
`StoredSdf.glsl:78,:235` compute `lookupBase = octreeIdx*bpa³` with the *current* octree's bpa; CPU concatenation appends variable-size tables exact-prefix (`ShellOctreeGpu.h:993`, comment `:940-943` admits the limit; `MipBake.h:361` mirrors; `ShellDerive.h:433` does it correctly). Cornell mixes bpa=16 walls with bpa=2 small bodies → octrees 5–7 read wall-0's table (GPU bases 40/48/56 vs true 20480/20488/20496), then index the pool unbounded (`:84-87`). Correctness bug + garbage-driven march modes + the natural candidate for the vanishing-bodies blocker. Impact: **high**, effort **S**.

**B2. Occupancy band baked 8 grid-voxels thick (world-vs-grid unit mismatch, bake half)** *(merged ×2)*
`BuildRenderGraph.cpp:3090` (`kBand=2.0` world) → `SdfBake.h:121,:172` (predicate in grid voxels) at subdiv=4 = 8-voxel outward shell + one more brick of dilation (`:185-201`). Doc-proven component of the ~3.8× fix; also inflates entity counts (566–575 K/wall) and pool size. Impact: **high**, effort S–M (land with #2).

**B3. Octree presents the full 128³ cube; no allocated-brick trace bounds** *(merged with dead-cull finding, Top action #8)*
`SdfBake.h:399-406` (worldMax always full cube), `TraceWorld.glsl:234` (cull tests only `[0,1]³`), `:266-279` (entry reject skipped when origin inside cube). Oversized fixed-[0,10]-frame cubes (walls span 32 world units around ~3-unit slabs, `BuildRenderGraph.cpp:3100,:3316`) put the whole interior inside all 5 wall cubes → cull and front-to-back reject structurally dead → 8 descents per ray in every pass. `configs[].gridMin/gridMax` never read by the shader (`:1624-1626`). Impact: **high**, effort M. Corollary: **instance sort keys on cube min-corner not center** (`InstanceSort.h:26-34`, `BuildRenderGraph.cpp:3097-3099`) — masked today, matters the moment bounds land; one-line fix.

**B4. Pass-1 bake scan: origin-anchored region, no brick-level prefilter, triple iteration**
`SdfBake.h:167,:229-234,:241` + extent-only clamp `:133-134`; ~9 M `evalRecipe` VM calls per boot; brick-center conservative test would cut evals ~512×. Impact: medium, effort M (boot only).

**B5. Channel pool bakes 6×512 floats/brick with 5 of 6 lanes constant** — `ShellOctreeGpu.h:673-692`; ~68 MB pool, hot SDF lanes spread 12 KB apart. Impact: low, effort M. **SetRecipePool deep-copies the pool twice** (`BodyOctreeSceneNode.cpp:490`, wrong "shallow copy" comment; ~80 MB duplicate resident) — impact low, effort S.

### C. Lighting, shadow, and probe passes (the untimed majority of the GPU frame)

**C1. Per-pixel analytic shadow ray re-marches the whole stored-SDF scene** *(merged ×2)*
`SpatialReuseShade.comp:326-331` (unconditional per light per hit pixel; defaults `enabled=1`, `maxShadowDistance=1000`, 1 directional light — `ShadowConfigNode.cpp:31-33`, `LightingConfigNode.cpp:30`). Same `marchBrickSdf` path as primary → roughly +100% of the dominant cost class (any-hit early exit makes it somewhat less than a full closest-hit march). Note: the box is open on −Z, so the sun trace is *not* constant-occluded, but ceiling/right-wall/back-facing pixels have NdotL=0. Impact: **high**. Fix: NdotL gate (Top #3) then any-hit path (Top #7); optionally `lightCount=0` for DDGI-lit demos.

**C2. Shadow/probe rays pay the full nearest-hit payload and cannot stop at tmax** *(merged ×4)*
`TraceWorld.glsl:393-508` — `TraceWorldShadow` reuses the full traversal; `[tmin,tmax]` tested only after (`:502`); no instance reject beyond light distance; `handleLeafHitInstancedSdf` (`SceneBindings.glsl:513-528`) unconditionally computes gradient(56)+color(24)+roughness(8) ≈ 88 corner chains discarded per occluded ray. Impact: **high** aggregate (Top #7).

**C3. Trace-before-zero-test ordering** — `SpatialReuseShade.comp:470` traces before NdotL (`:474`) and W (`:477`); analytic path `:326-334` traces before `evalBRDF`. `ProbeUpdate.comp:236-241` already does it right. Impact: medium, effort **S** (Top #3).

**C4. ProbeUpdate re-marches a static, converged scene forever** — `ProbeUpdate.comp:406` + `:241`: 4096 TraceWorld + up to 4096 shadow marches/frame (512 probes, 64 rays, amortization 8 — `ProbeGridConfigNode.cpp:58-72`), no converged-probe sleep, no scene-dirty invalidation, hysteresis 0.02 recomputing identical results. Impact: medium, effort M. Fix: per-probe blend-delta sleep + coarse-LOD/mip marching for probe rays.

**C5. ProbeUpdate workgroup 4× oversized** *(merged ×3)* — `ProbeUpdate.comp:75-77` local_size_x=256 vs raysPerProbe=64 (`ProbeGridConfigNode.cpp:61`): 192/256 lanes idle through ~6 KB shared arrays and 8 barrier reduction rounds (`:448-456`). Impact: low, effort S (specialization constant).

**C6. Probe atlas: 320 serial `imageStore`s by lane 0, 319 never read** — `ProbeUpdate.comp:458,:513-533`; consumers read only the block origin (`SpatialReuseShade.comp:231,:237`; `ProbeUpdate.comp:305,:311`). Impact: low, effort S.

**C7. Probe light sampling scans the whole light-tree cut per ray** — `ProbeUpdate.comp:217-225`, O(cutCount≤64) node reads + serial RNG vs the sibling M=8 RIS estimator (`DirectLighting.comp:188-195`). Impact: low, effort S.

**C8. DirectLighting is a full-screen no-op dispatch in this demo** — `DirectLighting.comp:216` early-return (`reservoirEnabled=0` default, `ReservoirConfigNode.cpp:34`; demo never enables), yet full descriptor set + dispatch + hazard edge into spatial_reuse per frame (`BuildRenderGraph.cpp:400,:427-428`). Impact: low, effort S (CPU-side skip when config is 0).

### D. Debug instrumentation & readback (pure production overhead)

**D1. GPU-side trace hooks compiled into all 4 marching passes** *(merged ×3)*
`TraceRecording.glsl:28-30` (grid capture default-on, never overridden), `:155` (global `atomicAdd`), `:187-198` (48 B/step writes); `snapshotTraversalState` unconditional at 8 sites (`SceneBindings.glsl:667-996`); 14-field `DebugRaySample` zero-init per instance per ray (`TraceWorld.glsl:285-299,:472-486`); data consumed by ~1/4096 pixels. Red CSV: esvo 24.5 ms with hooks off vs 30–31 ms (~20%). Also: **8 per-pixel `instanceIterCount` stores to a 1-byte placeholder SSBO** (`TraceWorld.glsl:123-332`; `SceneBindings.glsl:69-78`) — and robustBufferAccess is *not* among enabled device features, so these OOB stores are technically UB. Impact: **medium** aggregate, effort **S** (in-source `#ifdef`, mirroring `ENABLE_SHADER_COUNTERS`).

**D2. CPU-side capture: fence drain + JSON export every 10th frame, always wired** *(merged ×2)*
`BuildRenderGraph.cpp:637,:3927-3930,:4068-4079`; `RayTraceBuffer.h:195` (`captureEnabled_=true`, zero disabling call sites); `DebugBufferReaderNode.cpp:103-106` `vkWaitForFences(UINT64_MAX)` on the *current* frame's fence (drain if scheduled after the UI submit; torn read if before), ~790 KB map+copy, up-to-multi-MB synchronous JSON write. Doc-bounded ≈10% of the run and a hard spike every 10th frame that pollutes every bench. Impact: medium–high for measurement validity, effort **S**.

**D3. DDGI gate demos hold `vkDeviceWaitIdle` every tick** — `VulkanGraphApplication.cpp:1054` (`LEAK_GATE_DEMO`), `:1214` (`EDIT_LOOP_DEMO`): any FPS captured under these env vars is structurally invalid. Not the baked demo path (its diag waits once at tick 150). Impact: medium (measurement hazard), effort S.

### E. Sync, submits & frame pacing

**E1. The "~125 ms CPU stalls" are pacing fences waiting on ~6 untimed GPU passes that also brick-march** — `FrameSyncNode.cpp:149`, `SwapChainNode.cpp:176,:197` (4-flight vs 3-image ring desync documented in-code); all fences signaled only by the last submit (`UIRenderNode.cpp:357`); 7 same-queue submits/frame with only `test_dispatch` timed (`VulkanGraphApplication.cpp:505-508`). Wall≈2×esvo is mostly *real untimed GPU work* (C1/C2/C4). Impact: **high** for attribution — Top #9 before any further sync hunting.

**E2. March submit head-waits the WSI acquire semaphore despite writing no swapchain image** *(merged ×2)* — `ComputeDispatchNode.cpp:288-293` unconditional acquire wait; `BuildRenderGraph.cpp:3966` sets `PARAM_WRITES_NO_IMAGE=true`; BlitNode (first real swapchain writer) deliberately carries none (`BlitNode.cpp:154-158`). Frame's heaviest pass gated on presentation-engine image release; forbids GPU frame overlap. Impact: medium, effort M — red `acquire_sync` cut esvo to 25.2 ms but its *wall* number wasn't cleanly better; land with cross-frame HitRecord hazard check.
**FIXED (Baked-Perf M6 Task 6.1, 2026-07-17):** BlitNode now owns the acquire wait
(`ComputeDispatchWaitsForSwapchainAcquire`, `ComputeDispatchNode.h`); hazard re-analyzed and found
non-issue — `FrameSyncNode::ExecuteImpl`'s own per-flight `vkWaitForFences(UINT64_MAX)` (not the
acquire semaphore) is what actually guards this dispatch's cross-frame HitRecord reuse, and it runs
regardless of which node consumes the acquire. Validated: same_path hash-equal, cross_path
ENFORCED PASS.

**E3. Blit leaves the render target in TRANSFer_SRC across the frame boundary; next writer declares GENERAL from a private map** *(merged ×2)* — `SwapchainBarriers.h:184,:100-103` vs `ComputeStageNode.cpp:348`: per-frame Vulkan spec violation (oldLayout mismatch) on the presented image from frame 2 onward; red `layout_sync` (adds TRANSFER_SRC→GENERAL exit barrier) was the best-wall run (62.5 ms). Impact: low-perf/high-hygiene, effort **S**.
**FIXED (Baked-Perf M6 Task 6.2, 2026-07-17):** `SwapchainBarriers::MakeRenderTargetPostBlitBarrier`
restores the render target to GENERAL (the stable cross-node boundary layout every
`ComputeStageNode` IMAGE_WRITE producer already assumes) before `BlitRenderTargetToSwapchain`
returns. Validation-layers A/B (9 fresh runs each side): the `VUID-vkCmdDraw-None-09600`
`TRANSFER_SRC_OPTIMAL`-mismatch signature was present in 100% of pre-fix runs (always ≥1 per run)
and 0% of post-fix runs.

**E4. BlitNode re-signals an orphaned per-image binary semaphore every frame in composite mode** — `BlitNode.cpp:172-177` vs Present waiting `uiComplete` (`BuildRenderGraph.cpp:4060-4064`); the exact re-signal VUID class `ComputeDispatchNode.cpp:319-327` documents and avoids. Impact: low, effort S.
**FIXED (Baked-Perf M6 Task 6.3, 2026-07-17):** `BlitSubmissionPolicy::signalsPresentSemaphore`
gates the binary signal to terminal blits only (mirrors ComputeDispatchNode's existing convention
for the same signal); composite-mode `RENDER_COMPLETE_SEMAPHORE` output now publishes
`VK_NULL_HANDLE` honestly instead of a handle nothing signals. Also fixed alongside (pattern R7):
all 4 `ALL_COMMANDS_BIT` binary-signal stage masks (`BlitNode.cpp`, `ComputeDispatchNode.cpp`,
`ComputeStageNode.cpp`, `UIRenderNode.cpp`) scoped to the actual producing stage
(BLIT/COMPUTE_SHADER/COLOR_ATTACHMENT_OUTPUT respectively). Validation A/B: `VUID-
vkQueueSubmit2-semaphore-03868` present in 100% of pre-fix runs, 0% of post-fix runs.
**New finding surfaced by the A/B, filed separately (KI-039):** an intermittent, pre-existing
(confirmed via disposable-worktree isolation against unmodified `c4bc07f5`), boot-time-only
`UNDEFINED`-layout/acquire-semaphore flake tied to the one-time `body_octree_scene` recompile
right after boot pause/resume — same trigger class as KI-033, unrelated to E2/E3/E4, unchanged by
this milestone.

**E5. Five separate `vkQueueSubmit2` for a linear same-queue compute chain** — `ComputeStageNode.cpp:246-252`, `ComputeDispatchNode.cpp:344-348`, `BlitNode.cpp:191-197`; timeline edges where in-CB COMPUTE→COMPUTE barriers suffice. Impact: low today, effort L (architecture).

**E6. Per-frame CB re-record for a 16-byte push constant** *(merged ×2)* — `ComputeDispatchNode.cpp:270-276` (own TODO) and same pattern in ComputeStage/Blit/UIRender: all 7 submits re-record every frame. Impact: low, effort M.

### F. Boot / bake CPU pipeline (~4.3 min measured, log-timestamped)

**F1. 8 independent bakes run sequentially single-threaded** *(merged ×2)* — `BuildRenderGraph.cpp:3214-3237`; 208 s of the 259 s scene build; each body owns its own world/registry (`SdfBake.h:137-148,:359-363`). ~5–7× from `std::async` per body (validate Gaia global component-registration state first). Impact: **high** (iteration speed), effort M. Longer term: disk-cache `ConcatenatedOctrees` keyed by recipe hash → warm boots at file-load speed.

**F2. TEMP DIAG loop re-serializes all 8 bodies to log two counts — 23.0 s measured** *(merged ×2)* — `BuildRenderGraph.cpp:3283-3301`; counts available O(1) from the octree. Delete. Impact: high-per-effort, effort **S**.

**F3. rebuild() Phase 4 (DXT + normals + materials) runs for stored-SDF bodies whose render path never reads it** *(merged ×2)* — `SVORebuild.cpp:646-765`; ~40 M hash/ECS lookups, ~50 s of the bake window. Consumed only by the disabled raster path (via `VoxelSceneCacher`/`VoxelGridNode`), so **gate on `m_signedDistanceField`** (set before rebuild, `SdfBake.h:414`) — do not delete outright. Bonus defect: `getOccupancy`'s `density>0` treats the SDF exterior band as solid. Impact: high, effort **S**.

**F4. SerializeSdf does ~7 dispatched ECS lookups per voxel (~3 ms/brick measured)** — `ShellOctreeGpu.h:706-762`; runs ≥2× per body per boot + on every runtime recipe edit (`BodyOctreeSceneNode.cpp:784`). Bulk path exists unused (`GaiaVoxelWorld::getBrickEntitiesInto`, `GaiaVoxelWorld.cpp:432`; `getEntityFast`). Impact: high, effort M.

**F5. ConcatenateSdfWithMips re-serializes everything; light body serialized 3× and mip-baked 3×** *(merged ×2)* — `MipBake.h:336-344`; `BuildRenderGraph.cpp:3245-3247` back-to-back `BakeAndAttachMipPool`+`BakeMipPool`; 28.6 s measured for the concat pass. Impact: medium, effort S–M (overload taking pre-serialized octrees).

**F6. Boot bake creates ~2.9 M Gaia entities with 6 sequential structural adds each (~17 M archetype moves) + unreserved mortonIndex; the ECS is pure round-trip for a static bake** — `GaiaVoxelWorld.cpp:562-579`; pass 2 already has dense per-brick values (`SdfBake.h:241-267`). Pure fix: bake straight to dense channel arrays, bypassing ECS (aligns with lazy-procedural program). Impact: high, effort **L**; mitigation tier (prototype-entity copy + reserve) is M.

**F7. Shader cache exists but is never wired** — `ShaderLibraryNode.cpp:193-194` never calls `EnableCaching`; ~200 KB GLSL glslang-compiled per pipeline per boot/recompile (~7 s window). Impact: medium, effort **S**.

**F8. Minor CPU**: `BakeMipPool` per-voxel channel-table rescan + double leaf reduction (`MipBake.h:120,:200,:278` — S); unbounded `logEntries` vector (`Logger.cpp:61-68` — S); instance SSBO full re-pack/upload every frame with 2 heap allocs (`BodyOctreeSceneNode.cpp:403-419` — negligible at N=8, direction doc exists); per-camera-move occluder rebuild + sort (`VulkanGraphApplication.cpp:2650-2706` — microseconds at N=8); per-frame identical `vkUpdateDescriptorSets` across 5 passes (`DescriptorSetNode.cpp:406-428` — low).

### G. Shader micro-overheads (traversal-wide, low individually)

- **No specialization constants anywhere**: instance count/channel layout/bpa ride push constants/SSBOs though fixed at graph build (`TraceWorld.glsl:76-77,:397-398`; dormant spec plumbing exists in `ComputePipelineCacher.cpp:155-161`). Impact: medium, effort M.
- **23-entry stack fully cleared per traversal** though only ~7 scales reachable (`ESVOTraversal.glsl:121-124` vs `:355-357`). S.
- **Tier farBit probe fetch before the empty-table check** (`SceneBindings.glsl:717-728`); hoist `tierRefTable.length()>0`. S.
- **local→world→local entry-point round trip** (2 cancelling mat4s + length per traversal, `SceneBindings.glsl:657-662`, `ESVOCoefficients.glsl:53-57`). M.
- **MultiDispatchNode unconditional inter-pass barriers** (`MultiDispatchNode.cpp:577,:481`) — dormant; fix before adoption. S.
- **Render-target GENERAL↔TRANSFER_SRC ping-pong** (`SwapchainBarriers.h:146,:184`) — folds into E3. S.

---

## 3. Uncertain — needs live measurement

*(No findings arrived formally "uncertain"; these are the measurement-dependent caveats inside confirmed verdicts, each with the experiment that settles it.)*

1. **Is B1 (brickLookupBase) actually the vanishing-bodies blocker?** — Apply the fix on top of `fix/baked-sdf-perf-rootfix`, run the baked demo, check the `[CornellDiag]` 25×25 instIdx map at tick 150 for bodies 5/6/7 present and OOB counter back near/below the 40081 baseline.
2. **How much of wall−esvo (62−30 ms) is untimed GPU work vs true CPU stall?** — Add the whole-frame GPU timestamp span + per-pass timers (Top #9); compare Σ(pass GPU times) against wall. Nsight Systems queue-gap view as cross-check.
3. **Does the driver already CSE the redundant per-corner channel-scan/config loads (A1)?** — A/B the hoisted-base build vs main on the Windows-native rig (`temp_bench/`), esvo-pass ms only; alternatively inspect ISA in Nsight/RGP for the scalar-load count per step.
4. **Real cost of the debug hooks in a clean session (D1's "~20%")** — the red `debughooks_off` row (115.6 ms wall) is from a noisy non-isolated session. Re-run hooks-on vs hooks-off with everything else fixed, frames 31–160 mean.
5. **Net wall benefit of the acquire-wait move (E2)** — red data shows esvo 25.2 ms but wall 69.2 ms (worse than clean_sync 65.8). Isolate: acquire-gate change alone vs main, wall + esvo + present-mode logged (FIFO vs IMMEDIATE materially changes the answer).
6. **3D-texture pool speedup magnitude (A6)** — prototype dense R16F SDF volume for the 5 walls only, march via `textureLod`; measure esvo pass vs the SSBO fast path (A1 landed) at 1440p. Decides whether phase 2 (sparse atlas + aprons) is justified.
7. **Shadow-march multiplier (C1's "+100%")** — per-pass GPU timers (item 2) directly give spatial_reuse's share; then NdotL-gate A/B gives the recoverable fraction.
8. **Miss-path quadratic frequency (A4)** — add a temporary shader counter (subgroup-aggregated) of marchBrickSdf invocations per pixel per frame in shadow/probe passes; if mean ≪ 2, deprioritize the restructure.
9. **Hop-crawl incidence (A4/sentinel)** — counter for `d > SENTINEL_D` probe steps as a fraction of total steps; settles how much the band-shrink (B2) already removes.
10. **Spec-constant unroll gain (G)** — single-variable A/B: hardcode `instanceCount=8`/`channelCount=4` as literals in a test compile and diff esvo ms; if <2%, drop to backlog.
11. **Gaia cross-world thread safety for F1's parallel bake** — smoke test: 2 concurrent `bakeWorldSpaceBody` on distinct worlds under TSan/repeat-runs before shipping thread-per-body.

---

## 4. Research patterns worth adopting (concrete VIXEN mappings only)

1. **GigaVoxels brick pool as 3D texture with 1-voxel apron; hardware trilinear** → replaces A1/A6's 8-fetch manual trilinear in `StoredSdf.glsl` with one `textureLod` from an R16F atlas; brickLookup maps grid-brick → atlas coord; sentinel probe-crawl disappears with dense/apron-ed storage. Phase 1 dense per-octree volumes (~20 MiB for Cornell walls), phase 2 sparse 10³-brick atlas. Changes `SceneBindings.glsl:54`, `StoredSdf.glsl`, `BodyOctreeSceneNode.cpp:314` upload path. — https://maverick.inria.fr/Publications/2009/CNLE09/CNLE09.pdf
2. **Over-relaxed/auto-relaxed sphere tracing (Keinert '14 / Bálint-Valasek '18)** → drop-in to `marchBrickSdf`'s step loop (`StoredSdf.glsl:441-453`): step ω·d (ω≈1.4–1.6) with the unbounding-sphere overlap test; each avoided step saves a full trilinear sample. Composable with everything above. — https://people.inf.elte.hu/csabix/publications/articles/eurographics-2018-shortpaper.pdf
3. **Cone soft-shadow tracker `min(k·d/t)` with early-out (Quilez / UE distance fields)** → in the shadow-march variant (Top #7), track closest pass-by and terminate at a low-occlusion threshold; gives soft shadows/AO free and cuts worst-case 96-step shadow bricks. Changes the C2 any-hit leaf handler. — https://iquilezles.org/articles/rmshadows/
4. **Beam optimization: 1/8-res conservative min-hit-t prepass (Laine-Karras)** → seeds full-res ray starts in `TraceWorld.glsl`/`RayGeneration.glsl`, taking min over the 8 instances; attacks the same empty-cube span as B3 from the other side; pure GPU work, no readback. — https://research.nvidia.com/publication/2010-02_efficient-sparse-voxel-octrees
5. **Distance-mip / coarse-LOD marching for probe & shadow rays** → re-activate the dormant Sparse-Mip path (`MipFallback.glsl`) for `ProbeUpdate.comp` rays (C4) — probe irradiance doesn't need brick-resolution iso hits. — https://maverick.inria.fr/Publications/2009/CNLE09/CNLE09.pdf
6. **Never block on the current frame's fence; N-buffered readbacks with availability polling** → `DebugBufferReaderNode.cpp:103-106` reads frame N−3's ring slot via `vkGetFenceStatus` poll instead of `vkWaitForFences(UINT64_MAX)` (D2); same rule for any future ray-guided-streaming feedback (JIT epic). — https://docs.vulkan.org/samples/latest/samples/performance/wait_idle/README.html
7. **Compute-only stage masks instead of ALL_COMMANDS** → `renderSig.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT` at `BlitNode.cpp:176`, `ComputeDispatchNode.cpp:325`, `ComputeStageNode.cpp:209`, `UIRenderNode.cpp:342` should be COMPUTE (or BLIT/TRANSFER for the blit) in this compute-only app, enabling same-queue overlap between independent passes (probe update vs primary march). — https://developer.nvidia.com/blog/vulkan-dos-donts/
**FIXED (Baked-Perf M6 Task 6.3, 2026-07-17):** all 4 sites scoped — BlitNode→BLIT_BIT,
ComputeDispatchNode/ComputeStageNode→COMPUTE_SHADER_BIT, UIRenderNode→COLOR_ATTACHMENT_OUTPUT_BIT
(graphics pass, not compute — matches its own acquire-wait's stage mask).
8. **Timeline semaphores for coarse CPU/GPU sync** → collapses `FrameSyncNode.cpp:52-159`'s fence array + `SwapChainNode.cpp:176-197`'s ring juggling (E1's 4-vs-3 beat) to one u64 counter; gives D2 its clean non-blocking `vkGetSemaphoreCounterValue` test. Binary semaphores stay only at WSI edges. — https://www.khronos.org/blog/vulkan-timeline-semaphores
9. **Specialization constants for pipeline-creation-known config** → instance count (8), channel count/SDF base, bpa, `raysPerProbe` (fixes C5's workgroup size too) via the *already-present but never-fed* spec plumbing in `ComputePipelineCacher.cpp:155-161`; recompile-on-scene-rebuild already handles staleness. — https://docs.vulkan.org/samples/latest/samples/performance/specialization_constants/README.html
10. **sebbbi/perftest buffer-shape rules: wide loads + wave-uniform hoisting + 16-bit SDF** → if the SSBO pool stays short-term: fetch corner pairs as 64-bit loads, `subgroupBroadcastFirst` the wave-uniform `configs[octreeIdx]` fields out of the per-step path, and store SDF as half (halves bandwidth). Complements A1's fast path. — https://github.com/sebbbi/perftest
11. **NanoVDB-style per-thread last-brick cache & bottom-up re-entry** → 1-entry (brickIdx, poolBase) cache in `_samplePoolVoxel` kills repeat lookup chains within a brick; resume traversal from the cached parent on brick exit instead of `_advanceToNextSdfLeaf`'s fresh descent (A4). — https://developer.nvidia.com/blog/accelerating-openvdb-on-gpus-with-nanovdb/
12. **Per-pass timestamp bracketing, read one frame late, 64-bit, no WAIT flag** → the concrete implementation recipe for Top #9's instrumentation, dropping into the existing `GPUPerformanceLogger`/PerfCsvWriter plumbing. — https://docs.vulkan.org/samples/latest/samples/api/timestamp_queries/README.html

---

## 5. Expected FPS trajectory for baked Cornell

From the 0.9–1.1 FPS baseline, the path is multiplicative and well-anchored: landing B1+B2 (the unblocked `*subdiv` unit/band fix) recovers the proven ~3.8× to **~3.5–4 FPS**; the march-loop package (A1 fast path + A2 analytic gradient + C1–C3 shadow gating/any-hit) then attacks the Exp-A-proven dominant cost *across all four marching passes* — the red worktree's combined-but-unvalidated configuration already demonstrated this class of build runs at **~62–65 ms wall / ~30 ms esvo ≈ 16 FPS**. Stripping the debug hooks/readback (D1/D2, ~20% of the GPU pass + ~10% of the run) and reviving instance culling (B3 trace bounds, red-measured 2nd-best) pushes toward the **~52 FPS Exp-A ceiling**, whose remaining components (8-octree descent, DDGI probe budget C4/C5, sync overlap E1/E2) are each now itemized with fixes. Breaking *through* ~52 FPS to the 60-FPS realtime target requires shrinking the ~30 ms GPU pass itself — the 3D-texture brick pool (A6) plus relaxed stepping are the levers for that, converting the march from a scattered-SSBO-latency problem into the texture-filtering problem the >100×-faster virtual variant never pays. Net: ~0.9 → ~4 → ~16 → ~40–52 FPS on the enumerated short/medium-effort work, with 60+ contingent on the pool-representation change — subject at every step to the instIdx-map/virtual ground-truth correctness rig, since the investigation's history shows "fast but missing bodies" is the standing failure mode.

---

## 6. Render-scale capability curve (Baked-Perf M6 Task 6.5, inventory #12)

`VIXEN_RENDER_SCALE` (M4's render-scale decoupling) shrinks the offscreen render target the
compute dispatch writes into relative to the swapchain, then blits it back up — a merged,
validated dial currently sitting unused at its default of 1.0. Recorded here (Cornell baked,
frames 31–160 excl. 150/151, warm run, this machine, 2026-07-17) to inform the realtime-target
definition; **default stays 1.0**, nothing in the shipped app's default path changes:

| scale | cpu_frame_ms | gpu_span_ms | esvo_ms | spatial_reuse_ms | probe_update_ms | bodies | OOB |
|---|---|---|---|---|---|---|---|
| 1.0 | 101.88 | 102.75 | 68.39 | 3.67 | 30.58 | 8/8 | 0 |
| 0.75 | 89.44 | 90.56 | 57.58 | 2.88 | 30.00 | 8/8 | 0 |
| 0.5 | 72.74 | 73.41 | 28.23 | 2.11 | 42.95 | 8/8 | 0 |

`esvo_traverse_shade`/`spatial_reuse` scale down with resolution as expected (fewer pixels to
march/shade). `probe_update` does **not** scale with render-scale — it dispatches at a fixed
probe count via `ProbeGridConfigNode`, independent of render-target resolution (M4b's per-ray-type
LOD affects probe-ray march cost, not probe count) — so at 0.5 scale it becomes the single largest
pass (42.95 ms, exceeding esvo's 28.23 ms), inverting the 1.0/0.75 ordering. All three scales:
8/8 bodies present, OOB 0 (correctness invariant holds independent of render-scale). Documented
in `temp_bench/run_baked.bat`'s own comment block alongside the reproduction recipe.
