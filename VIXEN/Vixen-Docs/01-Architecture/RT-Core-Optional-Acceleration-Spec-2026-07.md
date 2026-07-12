# RT-Core Optional Acceleration — Spec (2026-07)

**Status:** DRAFT spec — not scheduled. Downstream of the sampled-lighting/GI research (2026-07-10, two verified deep-research passes); sibling to `Lazy-Procedural-Delta-Baseline-Design-2026-07.md` (shares residency/epoch plumbing).
**Principle:** Hardware ray tracing is an **optional accelerator tier for lighting rays**, never a requirement, never a second renderer. One lighting architecture, two traversal backends behind one seam. The software compute path remains canonical and is the reference implementation every accelerated result is checked against.

---

## 1. Motivation

The sampled-lighting research (photon sampling / material-driven raycasting / ReSTIR passes) converged on a design whose dominant runtime cost is **secondary lighting rays**: ReSTIR shadow/visibility rays, DDGI-style probe rays, and VNDF-sampled specular marches. All of these are ray workloads with no dependence on *how* visibility is answered — only that it is answered against the same world state. That makes them exactly the workloads RT cores accelerate, and exactly the seam where hardware can be optional:

- The ReSTIR literature assumes hardware RT but its resampling math is traversal-agnostic.
- DDGI explicitly supports "shader-based world-space ray tracing" in place of RT APIs (JCGT 2019 §3); its authors preferred one batched coherent dispatch over RT-API dispatch anyway. The one number that must be re-measured for VIXEN is the probe ray-cast cost (0.8 ms for ~524K rays on a 2080 Ti **with** HW RT) — RT cores are how capable machines win that number back.
- Khronos hybrid-rendering guidance (Wolfenstein: Youngblood) licenses roughness-driven Tmax cuts under a denoiser — applicable to both backends identically.

VIXEN already carries a dormant **Phase K** hardware-RT skeleton. This spec activates it, redirects it from "alternative renderer" to "lighting-ray accelerator", and defines the tier model, the AS design for ESVO content, the shader seam, and the parity gates.

### What already exists (Phase K inventory — verified in source 2026-07-10)

| Asset | Location | State |
|---|---|---|
| RTX extension/feature negotiation (accel-structure + RT-pipeline feature structs in the pNext chain) | `VulkanDevice::CreateDevice` (`VulkanResources/src/VulkanDevice.cpp` ~line 63 "RTX Extensions (Phase K)") | Working; gated on the app *requesting* the extensions |
| `RTXCapabilities{accelerationStructure, rayTracingPipeline, rayQuery, SBT props, AS limits}` + `CheckRTXSupport()` + `GetRTXExtensions()` + capability-graph `"RTXSupport"` | `VulkanDevice.h:31` | Struct has a `rayQuery` flag but **`VK_KHR_ray_query` is never feature-mapped or enabled** |
| Per-solid-voxel AABB extraction (`VkAabbPositionsKHR`-compatible) + material-ID + brick-mapping buffers | `CashSystem::VoxelAABBCacher`, `VoxelAABBConverterNode` | Working but keyed to legacy `VoxelSceneData` (procedural grid test scenes), **not** the live `BodyOctreeSceneNode`/ShellPool path; per-voxel granularity |
| BLAS-from-AABBs + TLAS build, Static/Dynamic modes, `prefer_fast_trace`/`allow_update`/`allow_compaction` | `AccelerationStructureNode` (+ `CashSystem::AccelerationStructureData`) | Working scaffold; `TLAS_HANDLE` output already wired for `DescriptorSetNode` |
| Full RT pipeline path (SBT, `vkCmdTraceRaysKHR`) | `RayTracingPipelineNode`, `TraceRaysNode` | Experimental; **no covering tests**; not in any live graph |

## 2. Goals / Non-goals

**Goals**
- G-A: A machine without any RT extension renders **byte-identically** to today. Zero cost, zero behavior change (Tier 0).
- G-B: On capable machines, lighting-ray workloads (shadow/visibility, probe, specular) can run through RT cores, selected at device creation, off by default until parity-gated.
- G-C: One shading/estimator implementation. The backends differ only in *who finds the brick*; the in-brick march and material fetch are the same GLSL for both.
- G-D: Dynamic/procedural content (delta-baseline program) keeps working: AS lifecycle is epoch-keyed to residency, staleness is a declared bias absorbed by the lighting layer's temporal accumulation.

**Non-goals**
- Primary visibility on RT cores. The ESVO march **is** the renderer's contract (LOD, tier-crossing, sparse-mip fallback, lazy-procedural evaluation); an AS cannot represent unmaterialized procedural content. Primary rays stay Tier 0 permanently.
- The RT-*pipeline* path (`TraceRaysNode`, SBT) as the lighting integration. It stays experimental (see §4 Tier 2).
- Shader Execution Reordering, opacity/displacement micromaps, tensor-core NRC — out of scope; noted as future hooks only.

## 3. Tier model

| Tier | Backend | Requires | Role |
|---|---|---|---|
| **0** | Software ESVO march in compute (today's path) | nothing new | Canonical + reference. Only tier for primary rays. Runs everywhere (lavapipe, Dozen/WSL). |
| **1** | `VK_KHR_ray_query` **inside the existing compute passes** | `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`, `VK_KHR_deferred_host_operations`, BDA (already enabled via Vulkan12Features) | The HW lighting tier. Preserves the compute-only architecture: same nodes, same auto-sync timeline edges, same PassGroupNode assembly — only the traversal inner call changes. |
| **2** | RT pipeline + SBT (`RayTracingPipelineNode`/`TraceRaysNode`) | + `VK_KHR_ray_tracing_pipeline` | Experimental sandbox only. Kept compiling; never a lighting dependency. Revisit only if Tier 1 profiling shows scheduling limits ray-query can't fix (then SER-class features live here too). |

Ray query over RT pipeline is a deliberate architectural choice, not a shortcut: VIXEN's whole execution model (ComputeDispatchNode/ComputeStageNode, vkQueueSubmit2 timeline edges, declared-hazard scheduling) is compute-shaped. Tier 1 rides all of it unchanged; Tier 2 would fork dispatch, sync, and pipeline management for no algorithmic gain on these workloads.

## 4. Capability negotiation & selection

1. **Feature mapping (Inc0):** add `VK_KHR_RAY_QUERY_EXTENSION_NAME` → `VkPhysicalDeviceRayQueryFeaturesKHR` to the `deviceExtentionMappings` table in `VulkanDevice::CreateDevice` (same pattern as the existing accel-structure/RT-pipeline entries; set `rayQuery = VK_TRUE` when the extension is present). Populate `RTXCapabilities.rayQuery` in `CheckRTXSupport()`.
2. **Capability-graph node:** new `"RayQueryLighting"` = accelerationStructure ∧ rayQuery ∧ bufferDeviceAddress. The existing `"RTXSupport"` (AS ∧ RT-pipeline) is unchanged and keeps gating Tier 2. All queries go through `device.HasCapability(...)` — no ad-hoc flag checks in nodes.
3. **Selection knob:** `VIXEN_RT_LIGHTING = auto | off | force` (env/config, read once at graph build).
   - `auto`: Tier 1 for lighting rays iff `RayQueryLighting` available **and** the feature has passed its parity gate for this build (a compile-time default flag flipped per increment).
   - `off`: Tier 0 everywhere (the A/B lever for parity/perf gates).
   - `force`: fail loudly if unavailable (CI on RT hardware).
4. **Per-device function pointers.** All AS/ray-query entry points (`vkCreateAccelerationStructureKHR`, `vkCmdBuildAccelerationStructuresKHR`, …) resolve via `vkGetDeviceProcAddr` into **per-`VulkanDevice` members**, exactly like `fpCmdPipelineBarrier2` — never globals. This is the KI-004 device-loss lesson codified; DeviceLostRecovery must rebuild AS handles and PFNs with the new device.
5. **Environment honesty:** Dozen (WSL2 Vulkan-over-D3D12) and lavapipe must be *runtime-probed*, never assumed — the sync2 promotion gap taught us Dozen's reporting is idiosyncratic. Expected: neither exposes ray query today → the dev-WSL loop always exercises Tier 0; Tier 1 live gates run Windows-native on the real GPU (per the repo's Windows-native build rules).

## 5. Acceleration-structure design for ESVO content

### 5.1 Granularity: brick, not voxel

The existing `VoxelAABBCacher` emits one AABB **per solid voxel**. That is fine for the small Phase-K demo grids and wrong for the lighting tier: real content is millions of voxels (GB-class BLAS, long builds), and it duplicates topology the octree already encodes at finer granularity than the HW needs.

**Spec:** one AABB per **resident leaf brick** (the shell/brick tier — `BodyOctreeSceneNode::GetBrickTierLevel()`), per body. The BLAS answers "which brick, entry t"; the shader then marches *inside* the brick with the **same** in-brick march code Tier 0 uses (SDF or occupancy channels from the SoA pool). Consequences:

- Primitive counts drop by ~512× (8³ bricks) vs per-voxel; BLAS memory and build time scale with `residentBrickCount`, the same quantity the brick pool already budgets.
- Hit parity is by construction: the *committed* hit t always comes from our own brick march (`rayQueryGenerateIntersectionEXT` with our computed t). HW only proposes candidates and orders traversal; it never invents an intersection Tier 0 couldn't produce. Backend divergence is limited to candidate ordering/culling — testable with an epsilon.
- The existing per-voxel cacher stays as-is for the Tier-2 sandbox; the lighting tier gets a new brick-granularity provider (§5.3).

### 5.2 BLAS/TLAS topology

- **One BLAS per body-octree** (per shell-pool octree slot), built from that body's resident-brick AABBs in body-local space.
- **One TLAS for the scene**, one instance per `BodyInstanceGpu`: instance transform = body transform (TLAS refit/rebuild per frame is cheap and absorbs body motion without touching any BLAS); `instanceCustomIndex` (24 bits) = body/octree slot index → shader recovers the shell-pool slot + instance record, same indirection the Tier-0 marcher uses.
- This mirrors the Tiered-ESVO world: a tier/body = a BLAS; the scene = TLAS instancing. When nested tiers land, a far tier that is only a `TierRef` simply has no BLAS — lighting rays fall back to coarse tier occupancy exactly as Tier 0 does (§6.3).

### 5.3 Provider & node wiring

- New `ShellBrickAABBProvider` (CashSystem cacher or a lean extraction on `BodyOctreeSceneNode`'s shell cache): resident bricks → `VkAabbPositionsKHR` buffer + brick-index buffer (AABB primitive i → brick pool offset), keyed on **(bodyId, shell-pool epoch)**. Reuses `BatchedUploader` like every other upload path.
- `AccelerationStructureNode` is reused: gains an input path for the brick-AABB provider (the `VoxelAABBData*` slot generalizes to an `AabbSourceData*`), keeps Static/Dynamic modes, `prefer_fast_trace` for lighting BLAS, `allow_update` off (see §6.1 — rebuild, not refit).
- TLAS per frame-in-flight in Dynamic mode (already supported by the node's design).
- Memory: AS allocations go through the tracked allocator (`AllocateBufferTracked`) under a named budget bucket (`RTAccelStructures`) so the brick pool's budget manager sees them; log BLAS/TLAS sizes at build.

## 6. Dynamic & procedural content

### 6.1 Rebuild lifecycle (epoch-keyed)

Voxel edits change brick occupancy → primitive count changes → **rebuild** the affected body's BLAS, never refit (refit requires stable topology). Policy:

- The shell-pool/residency epoch (already double-buffered on `BodyOctreeSceneNode`; the delta-baseline program's materialized deltas bump it per body) is the cache key. Epoch change ⇒ enqueue BLAS rebuild for that body.
- Rebuilds are **budgeted per frame** (N bodies/frame, Q-Games "one cascade level per frame" pattern) on the compute-capable queue, interleaved with rendering via the normal timeline-edge machinery.
- TLAS rebuilds every frame it is consumed (cheap at our instance counts; refit-vs-rebuild is a measured decision later).

### 6.2 Staleness = declared bias

Between an edit and its BLAS rebuild completing, Tier-1 lighting rays see the **previous** brick set for that body. This is acceptable *because these are lighting rays*: the research design already routes all reuse error into the temporal-accumulation/denoise budget (DDGI hysteresis absorbs world edits over a few frames by design; ReSTIR temporal reuse is validated per-frame anyway). Rules that keep it honest:

- Primary rays never consult the AS ⇒ geometry on screen is never stale.
- A body's shadow/GI contribution may lag its edit by ≤ rebuild-queue latency (bounded, logged).
- If the lag is ever visible (fast destruction), the escape hatch is per-body: a body with a pending rebuild is marked `traceTier0` in the instance record and its lighting rays take the software path that frame — correctness restored at the cost of that body's ray perf. This flag is the *only* per-body tier mixing allowed.

### 6.3 Unmaterialized procedural content

The AS contains **resident (materialized) bricks only** — identical to what Tier-0 lighting rays traverse today. Regions that are pure recipe (lazy baseline, not yet materialized) are invisible to lighting rays in both tiers, or represented by coarse tier-level occupancy in both tiers — whichever the delta-baseline program's open "conservative recipe evaluation" decision lands on. The invariant this spec pins: **Tier 1 samples the same world state as Tier 0** — the tiers must agree on *what exists*, so parity gates stay meaningful and the choice of backend never changes the image's content, only its cost.

## 7. Shader seam

- One GLSL include, `trace_world.glsl`, exporting the lighting-ray query:
  `bool TraceWorld(vec3 o, vec3 d, float tmin, float tmax, out WorldHit hit)` (+ `TraceWorldShadow` any-hit variant that early-outs on first confirmed hit — the shadow-ray fast path).
- Two implementations behind `TRACE_BACKEND` (specialization constant / variant define):
  - `0`: the existing ESVO traversal, factored out of the current marcher into the include (refactor, no behavior change — this factoring is itself gated by render parity).
  - `1`: `rayQueryEXT` loop over the TLAS; per AABB candidate → fetch brick via `instanceCustomIndex` + primitive→brick buffer → run the **shared** in-brick march → `rayQueryGenerateIntersectionEXT(rq, tBrickHit)`; committed hit reconstructs `WorldHit` (brick, voxel, normal, material-channel offsets) with the same code path as backend 0.
- `GL_EXT_ray_query` needs SPIR-V 1.4+/Vulkan-1.2 targeting in glslang — ShaderManagement compiles the backend-1 variant **only** on devices with `RayQueryLighting` (Tier-0-only devices never see the extension).
- **SpirvReflector work item:** support `VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR` in reflection/SDI so the TLAS binding is discovered like any other resource (analogous to the nested-SSBO extension from the octree-config drift-guard work). The `TLAS_HANDLE` node output feeds `DescriptorSetNode` as designed in Phase K.
- Pipeline identity: backend is part of the pipeline key (specialization constants participate in interface hashing — re-verify against the ComputePipelineNode interface-hash aliasing fix from the P4 epic).

## 8. Consumers (priority order, gated per lighting-program increment)

1. **ReSTIR shadow/visibility rays** — highest volume, incoherent, the biggest HW win; reservoir logic untouched (traversal-agnostic by design).
2. **DDGI-style probe rays** — the batched coherent probe dispatch swaps its march call for `TraceWorld`; this is where the "re-measure the 0.8 ms line" open question gets answered on both backends with `GPUPerformanceLogger`.
3. **VNDF specular/glossy secondary rays** — with roughness-driven Tmax cuts applied identically in both backends (the cut is estimator policy, not backend policy).
4. *(future)* photon gathering (Evangelou-style HW-RT gathering) — only if the photon layer is ever adopted.

Each consumer adopts the seam in its own increment with its own parity + perf gate; no consumer may require Tier 1.

## 9. Render-graph & sync integration

- AS builds are declared hazard producers in the auto-sync FrameGraph: the analysis model gains the AS resource class with `ACCELERATION_STRUCTURE_BUILD_KHR` stage / `ACCELERATION_STRUCTURE_WRITE_KHR|READ_KHR` access, consumed by lighting dispatches at `COMPUTE_SHADER` stage with `ACCELERATION_STRUCTURE_READ_KHR`. Scheduler bakes these like any declared edge (P5 machinery); no hand baked barriers.
- BLAS-rebuild passes join a PassGroupNode with the frame's compute passes; TLAS build orders before the first consumer via a declared edge.
- Syncval-clean is a gate (G4) — validation layers cover AS build/use hazards.

## 10. Verification gates (live-run authoritative)

| Gate | What | Where it runs |
|---|---|---|
| **G0** | Seam refactor (backend-0 extraction into `trace_world.glsl`) renders byte-identically to pre-refactor | lavapipe + real GPU |
| **G1** | Non-RT device: graph builds, renders byte-identically to baseline, capability log shows Tier 0, zero new allocations | lavapipe, Dozen/WSL |
| **G2** | Hit parity: deterministic ray set (fixed seeds, probe-grid pattern) dispatched through both backends on the same scene → per-ray compare (hit?, t within ε, brickIndex, voxelIndex exact). CPU mirror of the backend-1 candidate/commit loop (gpu-shader-debug pattern) runs as the hardware-free pre-gate | real GPU (Windows-native); mirror test everywhere |
| **G3** | Image parity: full lighting pass Tier 0 vs Tier 1, same seeds — PSNR threshold (bias policies §6.2 documented as the only allowed divergence) + perf: timestamped speedup per ray class reported; feature default stays `off` until G2+G3 pass | real GPU |
| **G4** | Syncval clean with AS build + consume in the graph | real GPU + validation |
| **G5** | Device-loss recovery: `VIXEN_SIMULATE_DEVICE_LOSS` path rebuilds AS + PFNs on the new device (extends the KI-004 guard set) | flag-gated test |
| **G6** | Dynamic content: edit-loop live run with budgeted rebuilds — no VUIDs, staleness bounded & logged, `traceTier0` fallback exercised | real GPU |

## 11. Increments

- **Inc0 — plumbing (independent, can land now):** ray-query feature mapping + `CheckRTXSupport` wiring + `RayQueryLighting` capability + `VIXEN_RT_LIGHTING` knob + per-device PFN audit. Gate: G1 (+ capability log on RT hardware). Zero behavior change.
- **Inc1 — brick AS provider (independent):** `ShellBrickAABBProvider` + `AccelerationStructureNode` fed from the live body path + TLAS from `BodyInstanceGpu` + budget bucket. Static scene. Gate: builds validate, sizes logged, G4 for the build passes.
- **Inc2 — seam + first consumer:** `trace_world.glsl` (G0), backend 1, shadow-ray or probe-ray test dispatch. Gates: G2, G3, G4.
- **Inc3 — dynamic:** epoch-keyed rebuild queue, per-frame budget, staleness policy + `traceTier0` flag, auto-sync AS hazard class. Gates: G5, G6.
- **Inc4+ — consumer adoption:** rides the sampled-lighting program's own increments (ReSTIR, DDGI, VNDF specular), each re-running G2/G3 for its ray class.

Inc0/Inc1 have no dependency on the lighting program and can be scheduled anytime; Inc2+ should land with (or after) the first lighting increment so the seam has a real consumer.

## 12. Risks & open questions

- **Ray-query AABB-candidate loop cost varies by vendor** (traversal HW differences, candidate-loop divergence). The G3 perf gate is per-machine evidence; `auto` never assumes HW is faster, it assumes the gate said so for this class of device.
- **AS memory competes with the brick pool.** Brick-granularity keeps it proportional to residency, but the GigaVoxels-LRU flip-triggers memory applies: if residency grows GPU-LRU-shaped, AS rebuild churn becomes a cost class of its own — measure in Inc3.
- **Dozen/WSL cannot exercise Tier 1** — the dev-loop stays Tier 0; every Tier-1 gate is a Windows-native run. This is a workflow cost, accepted and explicit.
- **Legacy per-voxel AABB path**: keep for the Tier-2 sandbox for now; decide retirement when Inc1 lands (avoid two AABB providers long-term).
- **Nested tiers (Tiered-ESVO)**: BLAS-per-tier composition and far-tier `TierRef`-only bodies interact with TLAS instancing — design deferred to when nested tiers meet lighting; §5.2's per-body BLAS is forward-compatible.
- **Conservative recipe evaluation** (delta-baseline open decision) may add coarse occupancy for unmaterialized regions — must land symmetrically in both backends per the §6.3 invariant.

## 13. References

- Research pass 1 (photon sampling / material-driven raycasting / ReSTIR — verified findings): ReSTIR (Bitterli et al. 2020; Wyman et al. SIGGRAPH 2023 course), VNDF sampling (Heitz JCGT 2018; Dupuy & Benyoub 2023), stochastic SSR (Stachowiak 2015), tiled deferred photon gathering (Mara et al. I3D 2013), Khronos hybrid-rendering best practices (Tmax-vs-roughness).
- Research pass 2 (voxel GI): DDGI (Majercik et al. JCGT 2019 — HW-RT-independent probe tracing, Chebyshev visibility), RTXGI-DDGI SDK seam (application-traced rays; compute blend/relocate/classify), VCT lineage (Crassin 2011, VXGI GDC15, Q-Games CEDEC 2014).
- In-repo: Phase K sources listed in §1; auto-sync FrameGraph design docs (declared-hazard scheduling); `Fail-Scenario-Simulation-Design-2026-07.md` (KI-004 device-scoped-state class); `Lazy-Procedural-Delta-Baseline-Design-2026-07.md` (epochs, materialized deltas, conservative-evaluation open decision).
