# DDGI Hardware-RT Acceleration + Multi-Queue — Direction (2026-07)

**Status:** DRAFT direction — not scheduled. Downstream of Inc4 (DDGI probe volume, shipped `ad17d45c` 2026-07-13) and the existing `RT-Core-Optional-Acceleration-Spec-2026-07.md` (Tier 1 = `VK_KHR_ray_query`, drafted before Inc4 existed). This doc grounds that spec's Tier-1 plan specifically against DDGI's now-measured cost problem, and treats multi-queue as a **separate, later** axis — the two should not be conflated into one increment.

**Trigger:** Inc4's own headline finding — the shipped DDGI defaults (raysPerProbe=64, 8×8×8=512-probe grid) cost ~20–24ms/frame for the probe-update pass alone, exceeding the entire 16.6ms (60fps) budget by itself. See `Sampled-Lighting-Inc4-Plan-2026-07.md`'s M6/M7 Progress Log for the full measurement.

**Historical grounding:** the user's own graduation research ("Which Graphics Method is Fastest for Minecraft-Style Games?", Lior Yaari, HOWEST DAE 2025-2026 — raw data in `~/Downloads/VIXEN_benchmark_*`, presentation `VIXEN-Research-Presentation.pptx`) measured Hardware RT **2.1–3.6× faster than compute** for the primary voxel ray-march, and — contrary to the study's own hypothesis — **more consistent across scene density** (4.0% variation vs compute's 12.9%). That study predates DDGI and doesn't test lighting rays specifically, but it's the strongest available real-GPU signal that HW RT is the biggest single lever on this codebase's ray workloads, on machines that have it.

---

## 1. Two separable problems — don't conflate

1. **DDGI probe-update cost is too high on ALL machines today** (single-queue, software SVO traversal). The fix that helps every machine, RT-capable or not, is **reducing ray/probe work** (amortization, ray-budget, grid density) — this is cheap, already-scoped (Inc4 M6's own Progress Log), and should ship regardless of what happens with RT or multi-queue.
2. **Hardware ray-query can additionally accelerate the probe-update pass on capable machines**, per-machine-optional, per the existing RT-Core spec's Tier-1 model. This is a real, substantial increment on top of (1), not a replacement for it — RT hardware won't be present on every target machine (per the spec's own G-A goal: zero-RT machines must stay byte-identical to today, zero cost).

Multi-queue (async compute) is a **third, independent axis** — grounded research below found it is NOT a natural fit for accelerating this specific pass (see §4), and the codebase has zero existing infrastructure for it (one queue, no queue-family-aware sync). It's real, potentially valuable elsewhere, but should be scoped as its own increment after both of the above, not bundled in.

---

## 2. Universal fix first (works on every machine, RT or not)

Per Inc4 M6's own Progress Log, the levers are, in likely priority order:

- **Amortize**: update a fraction of the 512 probes per frame (e.g. 1/8, round-robin) rather than all of them every frame — the standard DDGI/RTXGI production pattern. Composes naturally with the hysteresis blend Inc4 already shipped and already gate-verified tolerant of slower update cadence (M6's edit-loop convergence gate).
- **Reduce raysPerProbe**: M6's bench shows near-linear scaling (16 rays ≈ 8.9ms delta, 64 rays ≈ 20.5ms delta on that session's numbers) — dropping the default alone recovers a large fraction of the budget.
- **Reduce grid density**: 512 probes (8×8×8) was a default, never tuned against a real scene's actual coverage needs.

This should be scoped and shipped as **its own increment** (call it Inc5 in the Sampled Lighting roadmap, or a small Inc4b) before HW-RT acceleration — it's cheap, de-risks the frame-budget problem for every machine including non-RT ones, and gives HW-RT acceleration (§3) a smaller, already-partially-fixed baseline to accelerate rather than accelerating an un-tuned worst case.

---

## 3. HW-RT acceleration for DDGI probe rays (Tier 1, per the existing spec)

### 3.1 What already exists vs. what's net-new (confirmed by direct source read, 2026-07-13)

| Piece | State |
|---|---|
| `RTXCapabilities.rayQuery` detection (`CheckRTXSupport()`, `VulkanDevice.cpp:474`) | **Exists** — checks `VK_KHR_RAY_QUERY_EXTENSION_NAME` presence |
| `VK_KHR_ray_query` actually **enabled** at device creation (feature struct in the pNext chain) | **Missing** — narrow, mechanical Inc0-of-the-spec gap |
| `"RayQueryLighting"` composite capability (accelStruct ∧ rayQuery ∧ BDA, without requiring the full RT-pipeline) | **Missing** — only `"RTXSupport"` exists today, which also requires `rayTracingPipeline` (wrong gate for Tier 1) |
| `AccelerationStructureNode` (BLAS/TLAS build, static/dynamic modes) | **Real, compiles, registered** — but **zero tests**, **not wired into any live graph** |
| `VoxelAABBCacher` (AABB extraction) | **Real but wrong granularity/path** — per-solid-voxel, keyed to legacy `VoxelSceneData`, not the live `BodyOctreeSceneNode`/brick-pool path DDGI's `TraceWorld` actually walks |
| `TRACE_BACKEND` shader seam (software-vs-hardware branch inside `TraceWorld.glsl`) | **Does not exist** — `TraceWorld`/`TraceWorldShadow` are today single monolithic ~472-line functions, zero `#ifdef`/specialization-constant structure to branch on |
| `tmin`/`tmax` functional enforcement in `TraceWorld` | **Not honored today** (`TraceWorld.glsl:45-52`'s own comment: accepted for signature generality, `tmax` unenforced, `bestT` just starts at `1e30`) — a real ray_query TLAS bound *needs* a real tmax, so this must be fixed as part of the seam work, not assumed already correct |
| Per-node runtime capability-gated dual-path (a `RenderGraph` precedent to model the RT/software fallback on) | **No precedent exists** — capability gating today only decides *whether to build* certain graph nodes, never an intra-frame per-invocation branch. This increment would be establishing that pattern for the first time. |

**Bottom line:** the spec's Inc0 (capability plumbing) is genuinely close to free. Inc1 (finish + test + wire the AS/BLAS/TLAS nodes, at brick granularity against the live `BodyOctreeSceneNode` path, not the legacy per-voxel cacher) is real, unstarted work — budget it as "build this," not "wire up something proven." Inc2 (the `TRACE_BACKEND` shader seam) is 100% new code with no existing scaffolding.

### 3.2 Why DDGI specifically is a strong first Tier-1 consumer

- **Probe rays are the textbook RT-friendly workload**: DDGI's own reference (JCGT 2019 §3) explicitly supports either RT-API tracing or shader-based world-space tracing — this is a documented, expected swap point, not a novel adaptation.
- **Coherence**: probe rays from a fixed grid point in near-uniform directional patterns (spherical Fibonacci, per Inc4's own shipped sampler) are more coherent than camera-divergent primary rays — the workload RT hardware handles best.
- **Staleness tolerance already proven**: Inc4 shipped and gate-verified a hysteresis EWMA blend specifically because DDGI tolerates a lagging/approximate irradiance estimate. The RT spec's own AS-staleness story (§6, rebuild-not-refit, epoch-keyed, budgeted N bodies/frame) leans on exactly this same tolerance — DDGI is close to the *easiest* consumer to prove Tier-1 staleness-as-declared-bias against, before trying it on ReSTIR's shadow rays (which are less staleness-tolerant) or VNDF specular (Inc5, not yet built).
- **Isolated, already-instrumented cost lever**: Inc4 M6/M7 already built the CPU FrameTimer A/B measurement harness and the `VIXEN_PROBE_RAYS_PER_PROBE`/`VIXEN_DDGI_LEAK_GATE_DEMO` scene infrastructure — a Tier-1 A/B (`VIXEN_RT_LIGHTING=off` vs `auto`) slots directly into gates that already exist, no new measurement infrastructure needed.

### 3.3 Sketch: milestone shape (NOT a committed plan — for scoping discussion)

Following the existing spec's own gate list (G0–G6) and this program's established Sonnet-impl/Opus-validate pipeline:

1. **Prereq: universal cost fix** (§2) ships first, independently — reduces both the non-RT baseline AND the RT-accelerated target, and de-risks the frame-budget story before adding a second variable.
2. **RT-spec Inc0** (capability plumbing): enable `VK_KHR_ray_query` in the device feature chain, add `"RayQueryLighting"` composite capability, `VIXEN_RT_LIGHTING` knob. Gate: G0 — byte-identical on every machine (the extension being enabled must not change a single rendered pixel by itself).
3. **RT-spec Inc1** (AS at brick granularity): new brick-granularity AABB provider against the live `BodyOctreeSceneNode`/brick-pool (NOT `VoxelAABBCacher`, which is legacy-path-only), one BLAS per body-octree, one scene TLAS, wired for the first time into a live graph with real tests (the existing `AccelerationStructureNode` has none). Gate: AS builds/updates syncval-clean, no consumer yet.
4. **RT-spec Inc2** (`TRACE_BACKEND` shader seam, DDGI-only first): add the backend branch to `TraceWorld.glsl`/`TraceWorldShadow` — fix `tmax` enforcement as part of this (it's currently a no-op), Tier-1 path calls `rayQueryEXT` against the TLAS to find brick+entry-t, then falls through to the SAME in-brick march + material fetch Tier 0 uses (per spec §5.1's hit-parity-by-construction design). Wire ONLY `ProbeUpdate.comp`'s probe-ray call site first — not `DirectLighting.comp`'s shadow rays, not VNDF specular — narrowest possible first consumer. Gate: G2 hit-parity (per-ray t/brick compare, CPU-mirror-checked, per the project's own `gpu-shader-debug` skill pattern) + G3 image parity (PSNR) — feature stays `off` by default until both pass.
5. **Measure**: `VIXEN_RT_LIGHTING=auto` vs `off` A/B on RT-capable hardware, using the exact FrameTimer harness Inc4 M6/M7 already built. This is the actual payoff milestone — answers "how much of the ~20ms (post-amortization, smaller) probe-update cost does Tier 1 recover" with real numbers, not the graduation study's extrapolation from primary-ray costs.
6. **Close-out**: `VIXEN_RT_LIGHTING=auto` becomes the shipped default IF the measured win is real and gates hold; `off`/software-only remains correct and byte-identical for every non-RT machine (lavapipe, Dozen/WSL, older GPUs) — this is a hard non-negotiable per the spec's own G-A goal, not a "nice to have."

This is deliberately DDGI-scoped and narrow — extending Tier 1 to `DirectLighting.comp`'s shadow rays or a future VNDF specular pass is explicitly a later increment, not bundled here, per the spec's own "one seam, activated one consumer at a time" philosophy.

---

## 4. Multi-queue / async compute — separate, later, smaller expected payoff here

### 4.1 What exists today: nothing

Confirmed by direct source read (2026-07-13): VIXEN creates **exactly one queue** (`deviceInfo.queueCreateInfoCount = 1`, `VulkanDevice.cpp:213`), on whichever family has `VK_QUEUE_GRAPHICS_BIT` (assumed to also support present). No dedicated compute-only or transfer-only queue is ever requested. The `FrameSyncSchedule`/`SyncEdge`/`SubmitGroup` auto-sync system that bakes all of this program's dependency edges (including Inc4 M5's own disjoint-sibling proof) has **no queue-family field anywhere in its data model** — it is purely an intra-queue submit-group ordering scheme. Building real multi-queue support means adding a queue-family dimension to that scheduler from scratch, plus real cross-queue timeline-semaphore waits — greenfield, not an extension of existing plumbing.

### 4.2 Why it's the wrong first lever for THIS problem specifically

As discussed in conversation before this doc was written: async compute's classic win is overlapping compute work with **concurrent graphics-queue (raster) work** that would otherwise leave shader cores idle during fixed-function stages (ROP, rasterization, depth testing). VIXEN's live app has **no concurrent raster work** — the entire pipeline (march, shadow rays, ReSTIR, DDGI) is already compute-only, chained on one queue, blitted to the swapchain at the end (per the standing `vixen-app-compute-only-raster-disabled` project memory). Two compute-heavy passes on separate queues on most desktop GPU architectures still contend for the same shader-core/ALU/memory-bandwidth throughput unless the hardware has genuinely spare independent ACE-engine capacity — moving probe-update to a second queue does not create GPU throughput that isn't there; it can only change *scheduling*, not the ~20-24ms of actual traversal work.

### 4.3 Where it COULD genuinely help (a real, smaller, later opportunity)

Per Inc4 M5's own already-verified finding: the probe-update pass and the direct/ReSTIR pass are **already disjoint siblings with zero data dependency into the current frame** (confirmed via a live `FrameSyncSchedule` edge dump — no `probe_update→direct_lighting` edge exists). That means probe-update for frame N+1 could, in principle, be issued and progress *concurrently* with the direct/ReSTIR work for frame N, on a second queue, without correctness risk — a genuine frame-pipelining opportunity, not a same-frame occupancy-fill trick. This is a real, compatible-with-hysteresis (probe updates are already tolerant of being "one frame behind," exactly per Inc4's own staleness/hysteresis design) lever — but it is a scheduling refinement layered on TOP of an already-reduced-cost pass (§2 + §3), not a substitute for reducing the work itself, and it requires building the queue-family-aware sync-scheduler extension described in §4.1 from scratch first.

### 4.4 Recommendation

Scope multi-queue as its own future increment, sequenced AFTER §2 (universal cost fix) and §3 (HW-RT acceleration) have shipped and been measured — by then the probe-update cost will be much smaller (both from doing less work and from HW acceleration), and the question "is a second queue worth the real architectural investment (queue-family-aware `SyncEdge`, cross-queue timeline semaphores) for whatever residual cost remains" can be answered with real numbers instead of speculation. Do not build multi-queue speculatively ahead of that data — it is greenfield infrastructure work with a currently-unverified payoff for this specific bottleneck.

---

## 5. Summary / sequencing recommendation

1. **Inc-next (small, universal, no new architecture)**: amortized per-probe update schedule + tuned raysPerProbe/grid-density defaults. Ships fastest, helps every machine, de-risks the baseline for everything below.
2. **Inc-next+1 (RT-spec Inc0-Inc2, DDGI-scoped)**: HW ray-query acceleration for the probe-update pass specifically, per-machine-optional, byte-identical fallback mandatory. Real, substantial work (capability plumbing is cheap; AS/BLAS/TLAS wiring and the shader seam are not). Grounded in the user's own thesis data (2.1–3.6× HW-RT speedup, most-consistent-across-density) as the strongest available signal this is worth the investment on capable machines.
3. **Inc-next+2 (multi-queue, deferred)**: only after (1)+(2) are measured — frame-pipelining probe-update N+1 against direct/ReSTIR N on a second queue, exploiting Inc4 M5's already-proven disjoint-sibling property. Requires building queue-family-aware sync scheduling from scratch; not a quick win, sequence last.

None of this is scheduled yet — this doc exists to make the next scoping conversation concrete rather than starting from a blank page.

## Related

- `RT-Core-Optional-Acceleration-Spec-2026-07.md` — the tier model, AS design, and gates this doc's §3 is grounded against; still DRAFT/not scheduled, this doc doesn't supersede it, it grounds it against DDGI's now-measured need.
- `Sampled-Lighting-Inc4-Plan-2026-07.md` — the frame-budget finding (M6/M7) that triggered this doc, and the FrameTimer/gate-scene infrastructure §3.3's measurement milestone reuses.
- `Sampled-Lighting-Design-2026-07.md` §6 — open-decision tracking; this doc's Inc-next items should be folded in there once actually scheduled.
- Project memory `sampled-lighting-gi-program-direction` (Inc4 entry) — the standing summary this doc's trigger references.
