---
title: Baked-Perf Fix Pipeline — Milestone-Chunked Execution Plan
status: M9 RESOLVED (Opus-max debug, diagnostic-only commit 067615ff) — seam geometry is ALREADY COHERENT + non-flipping at the current 1e-4 tie-band; PROVEN oracle-based that disabling the tie-break flips AWAY from virtual (so 1e-4 is correct, do NOT touch it; the reverted 1e-5 was wrong). The one residual box-corner cell = 32³ box BAKE QUANTIZATION, accepted as the documented baseline (user 2026-07-17); SEAM ISSUE CLOSED. M10 DIAGNOSED (2026-07-18, Opus-high oracle-based): baked-fainter-than-virtual = stored-SDF shadow-ray FALSE-OCCLUSION of the dilated occupancy band (shadows-off ratio 1.00, shadows-on 0.88; baked floor loses 27.4% direct light, virtual 0%). Render-time; fix = tighten `marchBrickSdfAnyHit` occlusion crossing (StoredSdf.glsl:822), shadow/probe-only, seam untouched. **M10 SHIPPED (Task 10.2, commit `1964c134`, Opus-validated APPROVED 2026-07-18): TWO shadow false-occlusion mechanisms closed (mip-coverage any-hit + dilated-band SDF crossing `-EPS`→`-kBand`) — floor shadow-loss 24.5%→6.24%, interior baked/virtual ratio 0.895→0.970, geometry parity byte-identical 0/625, seam+primary-march untouched. Residual ~3% = a THIRD precision issue (wall thin-edge stored-vs-analytic divergence), out of scope, candidate follow-up KI.** (Task 10.1 `3142047c` was an honest-negative intermediate — correct but inert, kept.) Phase-2 (M0–M8) shipped+validated (877ms→~19 FPS class; bake cache 16ms warm; dedup 5.2×; +1.3–1.4× march; hybrid frames). KI-040/KI-041 filed. worktree fix/baked-perf-pipeline
created: 2026-07-16
---

# Baked-Perf Fix Pipeline — Execution Plan (2026-07-16)

Executes the verified findings of [[Baked-Content-Perf-Audit-2026-07]] toward the
end-state in [[Baked-Content-Perf-Consolidation-2026-07]] §0: **Cornell-class scenes at
realtime, benchmarked Windows-native**.

**Why both paths matter (user, 2026-07-16):** the final renderer composes BOTH providers
in one frame — procedural recipes as the compact exact baseline, baked voxel deltas
carrying edits/modifications (the [[lazy-procedural + delta baseline program]]:
instructions-first rendering, tiered deltas). Virtual and baked are not alternative
demos; they are the two halves of the eventual hybrid frame, already per-body via
`providerKind` (PROCEDURAL/STORED) through the same graph and lighting stack. This
pipeline's true deliverable: both providers at budget with parity semantics in the same
scene. GAP: the bench rig exercises the providers only separately — see the
mixed-provider gate below. Run under the `post-brainstorm-context-manager`
pipeline: thin controller, fresh Sonnet-medium implementer per milestone, Opus-high
validator per milestone, progress persisted here (this doc is the pipeline's memory —
a /clear loses nothing).

## Ground rules (pipeline-wide)

- **Isolation:** ONE worktree for the whole pipeline: `.claude/worktrees/baked-perf-pipeline`,
  branch `fix/baked-perf-pipeline` off main. Single consolidated branch (per the
  per-increment-worktrees lesson). **Merge policy (user-approved 2026-07-16): validated
  milestone boundaries merge to main incrementally** — first merge `0bcbc146` (through
  M2c @ `1538bdba`), integration-verified on main by full build + warm bench (297.9 ms,
  8 bodies) because main had concurrently received recipe-bucketing Inc2 (`7a27357f`).
  KI-number collision with the concurrent session resolved: their uncommitted entry
  renumbered KI-035→KI-037 (`bfb3f825`). Pre-bless the worktree-isolated tier at setup
  so workers run unattended.
- **Build/test Windows-native** per repo build rules + `vixen-build-policy` (machine-wide
  build lock; ONE build at a time; drive via `.bat` + `cmd.exe /c`). Workers watch long
  builds with a foreground ~15–30 s poll loop — NEVER blind-wait, NEVER switch to
  ScheduleWakeup/Monitor/background callbacks mid-wait.
- **Live-run gate is authoritative** (static review has repeatedly missed GPU bugs):
  every milestone runs `temp_bench/run_capability_bench.bat` (or the baked half) and the
  validator judges from fresh output, not claims.
- **Correctness rig (every milestone):** `[CornellDiag]` instIdx map at tick 150 must show
  ALL 8 bodies (0–7); OOB counter not materially above the ~40081 baseline; virtual-variant
  screenshot is ground truth. "Fast but missing bodies" is the standing failure mode.
- **Numbers discipline:** never trust another session's absolute FPS — every A/B toggles
  the change on the same machine/session. CSV gotcha: `steady_state_fps` is cumulative;
  use mean `cpu_frame_time_ms` over frames 31+. **Warm-run rule (M0 lesson): the first
  launch of a freshly-built exe is cold-cache (boot +100 s, inflated frames) — always
  bench the SECOND run**, and beware the tick-150/151 CornellDiag+capture spike when
  averaging (use median or exclude frame 151).
- **Reference implementations:** `fix/baked-perf-red-experiments` @ `6568c3ee` contains
  unvalidated prototypes for M3/M5 items (fast-path cell load, trace bounds, sync fixes).
  Workers may consult it but must re-derive against main and re-validate; never
  cherry-pick blind.
- **Codegen boundary:** `OctreeConfig` schema changes (M1, M5) go through
  `codegen/config-schemas/OctreeConfig.cs` + the kernel-framework codegen + drift guards
  (`kernel-framework` skill) — never hand-edit `.g.h`/`.glsl`.
- **Escalation ladder:** 1st failure → re-dispatch Sonnet-medium with findings; 2nd
  consecutive failure on the same milestone → Opus-max with full context from both
  attempts; if that fails → stop and ask the user. Validators always Opus-high.

## Baseline (main `e92acf44`, Windows-native, 2026-07-16)

| Metric | Virtual | Baked |
|---|---|---|
| wall ms/frame (31–160) | 6.49 (154 FPS) | 877.5 (1.1 FPS) |
| esvo_traverse_shade GPU ms | 2.8 | 222.4 |
| boot to render loop | fast | ~4.3 min |
| instIdx map | 8 bodies | 8 bodies (bar to preserve) |

Targets per trajectory (recalibrated at M3 close): M1 ≈ 4 → M3 ≈ 6.5 (fetch-volume cut delivered; 16-FPS figure over-attributed a red-prototype bundle) → M5 ≈ 16–20 (trace bounds + far-hit rejection) → M4/M4b → 40+ → Phase 2 (M8) 60+.

---

## Milestone M0 — Measurement infrastructure + boot triage (S)

Rationale: full frame attribution before touching sync or shaders; boot cut multiplies
every subsequent A/B (the pipeline itself runs the bench dozens of times).

- [x] Task 0.1 — Per-pass GPU timers: add `PerfCsvWriter::PassSource` columns for
  `direct_lighting`, `spatial_reuse`, `probe_update`, blit/UI (each node already owns a
  `GPUPerformanceLogger`), plus a whole-frame GPU span (first CB start → last CB end).
  (`VulkanGraphApplication.cpp:505-508`, `BuildRenderGraph.cpp:337-665`,
  `FrameSyncNode.cpp:149`; audit Top #9, pattern R12.)
- [x] Task 0.2 — Debug-capture off by default: `AUTO_EXPORT=false` +
  `RayTraceBuffer.h:195` `captureEnabled_` default false, re-enable via env knob
  (`VIXEN_DEBUG_CAPTURE=1`). Kills the every-10th-frame `vkWaitForFences` drain + JSON
  export in benches. (`BuildRenderGraph.cpp:3927-3930`, `DebugBufferReaderNode.cpp:103-106`;
  audit D2.)
- [x] Task 0.3 — Delete the TEMP DIAG re-serialize loop (23 s measured;
  `BuildRenderGraph.cpp:3283-3301`; audit F2).
- [x] Task 0.4 — Gate rebuild Phase 4 (DXT/normals/materials) on `m_signedDistanceField`
  (~50 s; `SVORebuild.cpp:646-765`; consumed only by disabled raster path; audit F3).
- [x] Task 0.5 — Re-baseline: run `temp_bench` both variants; record per-pass attribution
  table + boot time in Progress Log.

**Gate:** build green; Σ(per-pass GPU ms) ≈ whole-frame GPU span (attribution closes);
boot ≥ 70 s faster; 8 bodies present; baked FPS not below baseline.

## Milestone M1 — Correctness unblock + land the proven fix (S–M) — target ~4 FPS

- [x] Task 1.1 — `brickLookupBase` exact-prefix: add field to `OctreeConfig.cs` schema
  (codegen path), stamp the prefix sum in `ConcatenateSdf`/`ConcatenateSdfWithMips`
  (`ShellOctreeGpu.h:993`, `MipBake.h:361`; `ShellDerive.h:433` has the correct formula),
  read it in `_samplePoolVoxel` + `_sdfBrickAllocated` (`StoredSdf.glsl:78,:235`).
  (Audit B1 / Top #1 — prime suspect for vanishing bodies.)
- [x] Task 1.2 — `*subdiv` grid-unit fix in Cornell `makeWorldSpaceEval`
  (`BuildRenderGraph.cpp:3090` area; proven ~3.8× in rootfix doc §2).
- [x] Task 1.3 — Grid-unit occupancy band: `kBand` world→grid conversion
  (`BuildRenderGraph.cpp:3114-3119`, `SdfBake.h:121,:172`; audit B2).
- [x] Task 1.4 — Validation run: instIdx map MUST show bodies 5/6/7 (the previous
  attempt's failure mode); OOB counter ≤ ~40081; mirror + serialize + bake tests green.

**Gate:** all 8 bodies present; baked ≥ ~3.5× baseline FPS; tests green
(`test_stored_sdf_march_mirror`, `test_soa_sdf_serialize`, `test_sdf_bake`).
*Gate recalibrated at close (validator-approved): the ≥3.5× multiplier was calibrated
against the pre-M0 0.9-FPS baseline; achieved **2.71× / 3.16 FPS absolute** from the
already-improved M0 baseline — same absolute endpoint as the historical fix (3.4 from
0.9). The OOB gate figure (~40081) was from a third environment; correct comparator is
oob FRACTION 24.0%→27.5%, denominator-driven (bodies 5–7 now genuinely march) — benign.*

## Milestone M2 — GPU debug hooks out of the hot path (S)

- [x] Task 2.1 — `#ifdef VIXEN_GPU_TRACE_HOOKS` around `TraceRecording.glsl:28-30,:155,
  :187-198`, `snapshotTraversalState` sites (`SceneBindings.glsl:667-996`),
  `DebugRaySample` init (`TraceWorld.glsl:285-299,:472-486`), mirroring
  `ENABLE_SHADER_COUNTERS`; default OFF, env/config knob to re-enable. (Audit D1.)
- [x] Task 2.2 — Remove the 8 per-pixel `instanceIterCount` stores into the 1-byte
  placeholder SSBO (UB without robustBufferAccess) behind the same gate
  (`TraceWorld.glsl:123-332`, `SceneBindings.glsl:69-78`).
- [x] Task 2.3 — A/B on temp_bench: esvo ms hooks-off vs hooks-on (settles audit
  uncertain-item 4; expected ~20%).

**Gate:** esvo pass measurably down; image + instIdx identical; hooks re-enable cleanly.
*Gate recalibrated at close (validator-approved): met as wall −15% (−45 ms) concentrated
in probe_update (+31.5% hooks-on); the original "esvo −20%" text assumed hooks lived in
the primary march — they live in the SHARED trace path, so the reduction lands in the
ray-heavy probe pass (256 rays/probe). esvo flat is expected and correct.*

## Milestone M2b — Wire the shader disk cache (S–M) — pulled forward from M7.3

Dormant-work inventory #3: `ShaderCacheManager` exists (and is tested) but the four
live shader builders (`BuildRenderGraph.cpp:837-1031`) never call `EnableCaching` —
every boot glslang-recompiles everything. The pipeline reboots dozens of times; this
pays for itself immediately. **HAZARD:** the cache key MUST hash the final SPLICED
composite source (`BuildRenderGraph.cpp:871-892`) + compile options, not source-file
bytes — a stale-key bug silently serves outdated shaders and poisons every later A/B.

- [x] Task 2b.1 — Wire `EnableCaching` into the live builders, content-hash keyed on
  spliced source + options.
- [x] Task 2b.2 — Prove it: warm-vs-cold boot delta; poisoning test (touch a spliced
  fragment → key changes → recompile actually happens); bench output byte-identical
  cached vs uncached.

**Gate:** warm boot measurably faster; deliberate source touch busts the cache; 8 bodies.

## Milestone M2c — SPV-consumer test-health restoration (investigation, S–M)

Rationale (found during M2, 2026-07-16): forcing the first clean rebuild of the shared
`body_instance_raymarch_spv` surfaced a PRE-EXISTING blank-render (`hitPixels=0`)
failure class across its consumer tests (EditorDocumentRender ×2, most
BodyInstanceRayMarchRender, RecipeAuthoringGate, ShadowCorrectness, 1
TierCrossingLodResidency case, RecipePoolRender) — proven pre-existing via git-blame +
an M1-baseline compile. Also fixed during M2 as mechanical debt: KI-034 stale 76-byte
push-constant mirrors (7 files), missing bindings 15/18 + a 17-vs-18 HitRecordBuffer
miswire, undersized RayTraceBuffer placeholders, and a tier-crossing fixture missing
`setBrickLookupBase` (M1 follow-up). These tests are gate inputs for M3+.

- [x] Task 2c.1 — Root-cause the shared blank-render cause (suspect per M2 worker:
  boot/residency state interaction with M0's boot-triage changes — unconfirmed).
  Additional pre-existing signal from the M2 validator: `PushConstantGathererNode::
  Validate` type-mismatch ERRORs on the direct_lighting/spatial_reuse/probe gatherers
  (non-fatal, logs-but-passes; identical in pre-epic `gate-artifacts/gate_default_log.txt`
  @ `ef385d55`) — check whether it relates. Also: the tier-crossing `setBrickLookupBase`
  fixture fix was applied+correct yet its test STILL blanks → the shared cause is
  upstream of brick-lookup addressing.
- [x] Task 2c.2 — Fix; restore the whole consumer group to green; record the green
  baseline test list here. Any PRODUCT-code change this requires goes through the
  validator explicitly (test-health milestone; product changes are escalation-worthy).

**Gate:** SPV-consumer test group green, or each remaining red individually
root-caused + filed as a Known Issue with evidence.

## Milestone M2d — Automated visual-parity gate (S–M) — user-directed 2026-07-16

Rationale (user): visual certainty without a human in the loop — divergence between the
paths should be machine-detected. Corrected formulation: bit-exact virtual==baked is
impossible by construction (baked = discretized reconstruction; plus known ~1-cell
near-tie nondeterminism across launches), so the gate is two-tier:
Tier 1 same-path GOLDEN hash (hard gate) + Tier 2 cross-path parity metrics
(tracked now, enforced from M5's lighting-parity close onward).

- [x] Task 2d.1 — Parity tool `VIXEN/tools/bench/compare_parity.py` (tracked, not under
  the gitignored bench-output dirs): consumes two bench run dirs (run.log + perf.csv +
  hud_capture png); emits JSON + one PASS/FAIL line. Metrics: instIdx-map SHA + cell
  agreement % + bodies-present sets (from `[CornellDiag]` in run.log); OOB counts;
  image luminance stats (mean abs delta, p99, % pixels over threshold) between captures;
  per-pass ms deltas. Thresholds in a committed config file.
- [x] Task 2d.2 — Golden baselines + wiring: commit the current accepted baked + virtual
  instIdx maps as goldens (text) + thresholds; `temp_bench/run_parity_check.bat` runs the
  tool post-capture. Policy encoded in the config: Tier 1 (same-path vs golden,
  tolerance ≤2/625 cells) = HARD GATE for every subsequent milestone validator;
  Tier 2 (virtual↔baked) = REPORT-ONLY until M5 closes, then enforced.

**Gate:** tool PASSes on M2b's cold/warm artifact pair (same-path, byte-identical);
tool correctly REPORTS the known virtual↔baked lighting divergence on current captures
(i.e., detects the M5 target, doesn't mask it); goldens + thresholds committed; every
later milestone's validator instruction includes running it.

## Milestone M3 — March-loop package (M) — target ~16 FPS

- [x] Task 3.1 — Single-brick trilinear fast path: `_loadSdfTrilinearCell`-style intra-brick
  cell load (1 lookup + 8 contiguous pool loads `+{0,1,8,9,64,65,72,73}`), slow path only on
  brick-boundary cells; hoist `channelBaseFloats(SEM_SDF)` to march entry
  (`StoredSdf.glsl:160-177,:94-96,:45-50,:62-88`; audit A1 / Top #6; red branch reference).
- [x] Task 3.2 — Analytic gradient from the hit cell's 8 already-loaded corners, replacing
  the 7-trilinear-sample `sdfGradientStored` incl. redundant d0
  (`StoredSdf.glsl:197-222,:446`; audit A2 / Top #4; red `sdfGradientStoredFromCell`).
- [x] Task 3.3 — Hit-shading cell reuse: color + roughness resolve the corner cell once,
  not 4× (`StoredSdf.glsl:137-151,:103-123`; audit A3).
- [x] Task 3.4 — Update the CPU mirror test in parity (`test_stored_sdf_march_mirror` —
  gpu-shader-debug mirror discipline) + A/B bench.

**Gate:** esvo GPU ms substantially down (order 3×+ on the march-dominated pass); 8 bodies;
mirror tests green; no visual regression vs pre-milestone screenshot.

## Milestone M4 — Shadow/probe economy (S–M)

- [x] Task 4.1 — NdotL/BRDF gate BEFORE shadow traces (3-line reorder;
  `SpatialReuseShade.comp:326-334,:470-474`; `ProbeUpdate.comp:236-241` is the idiom;
  audit C3 / Top #3).
- [x] Task 4.2 — True any-hit shadow/probe march variant: no gradient/color/roughness
  payload, instance reject at entry-t > tmax, span clamped at light distance — a separate
  function, not a runtime flag (`TraceWorld.glsl:393-508,:495-502`,
  `SceneBindings.glsl:513-528`; audit C1/C2 / Top #7).
- [x] Task 4.3 — Generic CPU-side no-op dispatch guard (audit C8 + inventory #1/#2):
  a config-driven enable early-return in `ComputeStageNode::ExecuteImpl` — skips
  `direct_lighting` when `reservoirEnabled=0` (today a dead full-screen dispatch +
  submit every frame on ALL paths) and `probe_update` when `probeGridEnabled=0`
  (default boot). Cornell force-enables the probe grid — it must keep running there.
- [ ] Task 4.4 (RE-SCOPED to Phase-2 at M4 close; re-evaluate after M4b) — Converged-probe sleep (audit C4) — **PROMOTED from stretch by M0's
  attribution: probe_update = 431.7 ms is the LARGEST baked pass.** Per-probe blend-delta
  sleep (skip probes whose last update changed below epsilon) + wake on scene-dirty;
  `ProbeUpdate.comp:406,:241`, `ProbeGridConfigNode.cpp:58-72`.
- [x] Task 4.5 — A/B with M0's per-pass timers: spatial_reuse + probe_update ms deltas.

**Gate:** lighting-pass GPU ms down; shadowed image matches pre-milestone reference
(soft-compare screenshot); 8 bodies.

## Milestone M4b — Wake the dormant Sparse-Mip path for secondary rays (M)

Rationale (user-directed 2026-07-16): the Sparse-Mip LOD infra (mip pools via
`ConcatenateSdfWithMips`/`BakeMipPool`, marching via `MipFallback.glsl`) was built for
exactly this and is dormant on default paths (audit pattern R5). Post-M1 attribution:
probe_update 140.6 + spatial_reuse 74.1 ms = ⅔ of the frame is secondary rays that
don't need brick-resolution distances. Sequenced AFTER the baked-lighting-gap diagnosis
so mip A/Bs aren't confounded by a pre-existing shading defect.

- [x] Task 4b.1 — Dormancy audit: confirm mip pools are baked+uploaded on the Cornell
  baked path, `MipFallback.glsl` still compiles against current bindings, and M1's
  `brickLookupBase` stamping covers the mip lookup tables (`ConcatenateSdfWithMips` was
  stamped in M1 — verify the mip-march read side).
- [x] Task 4b.2 — Per-ray-type LOD policy: probe rays (`ProbeUpdate.comp`) and shadow
  rays (M4's any-hit variant) march coarse mip level(s) via the MipFallback path;
  primary rays unchanged at full res.
- [x] Task 4b.3 — A/B with per-pass timers: probe_update + spatial_reuse deltas;
  correctness: primary-ray instIdx map unchanged; image soft-compare vs full-res
  reference — watch for light leaks / over-darkening in the closed box.
- [ ] Task 4b.4 (DOCUMENTED + DEFERRED to Phase-2 at M4b close — risks same_path invariant, needs level blending; stretch, knob-gated OFF by default) — Footprint-driven mip selection
  for PRIMARY rays: reuse the existing `RaySizeCoefNode` ray-size term
  (`size = coef·t + bias`, already used for ESVO descent termination) to pick the brick
  mip level in `marchBrickSdf`. Accept only if the image gate shows no visible popping /
  iso-surface regression (level blending may be required — if so, document and defer to
  Phase 2 rather than half-land it). Expected small in room-scale Cornell (footprints
  ≈ mip 0); the real payoff is tiered/orbit scenes.

**Gate:** measurable probe/shadow ms reduction; no visible leak/darkening artifacts vs
reference; 8 bodies; tests green.

## Milestone M5 — Trace bounds + culling + baked-lighting parity (M) — RUNS BEFORE M4/M4b

Order change (2026-07-16, lighting-gap investigation): the baked demo's dimness vs
virtual is NOT GI convergence — it is (1) grazing far-hits (oversized cube bake AABBs)
writing corrupted `worldPos` into the DDGI gather (`SpatialReuseShade.comp:498`, probe
cell clamps to grid edge at `:265`) and (2) coarse baked normals suppressing NdotL in
every light term (`Brdf.glsl:71`, `ProbeUpdate.comp:236,251`) — objects near-black
because subdiv=1 small bodies bake at ~1 voxel/world-unit. Color/emissive plumbing is
proven identical across variants. Far-hit rejection needs the trace bounds from 5.1, so
M5 executes after M3 and BEFORE M4/M4b (mip A/Bs must not be confounded by the shading
defect). Full per-axis box-shaped bake grids (the complete fix for both causes) is
Phase-2 backlog — extends the shipped SDF-Bake-Box-Tight work.

- [x] Task 5.1 — Record allocated-brick min/max AABB in `SerializeSdf`'s existing loop →
  `traceBoundsMin/Max` `OctreeConfig` fields (codegen path; backward-safe zero default)
  (`SdfBake.h:399-406`, `ShellOctreeGpu.h:791-804`; audit B3 / Top #8; red branch
  reference). In the same schema change, REPLACE the dead `gridMin/gridMax` fields
  (uploaded but read by no dispatched shader — inventory #10): net-zero schema growth,
  removes dead weight.
- [x] Task 5.2 — Shader-side: AABB cull + front-to-back entry reject using the bounds in
  `TraceWorld.glsl:234,:266-279` (+ shadow variant from M4).
- [x] Task 5.3 — Instance sort key: cube center (or bounds center), not min-corner
  (`InstanceSort.h:26-34`; audit B3 corollary).
- [x] Task 5.4 — A/B bench + hole-hunt: grazing angles screenshot sweep. **Includes the
  user-reported wall-thickness-step repro (2026-07-16, screenshot at
  `temp_bench/reference/wall-thickness-steps-2026-07-16.png` in the worktree): silhouette
  steps at the LEFT-WALL TOP and the FLOOR FRONT-RIGHT EDGE — both grazing regions.
  Ranked suspects: (1) far-hit silhouette extrusion (fixed by 5.5 — verify these two
  spots specifically), (2) brick/band allocation quantization at 2-world-unit brick
  granularity (sentinel face probe-crawl), (3) box-tight bake-region boundary snapping
  (`cd9e1362` pow2 round-up) crossing the wall. Gate: both marked steps gone or
  root-caused with evidence.**
- [x] Task 5.5 — Far-hit rejection (lighting-parity interim fix): using 5.1's tight
  trace bounds, discard/clamp hits whose reconstructed `worldPos` falls outside the
  body's bounds+ε before the HitRecord write (`BodyInstanceRayMarch.comp:246` area) —
  stops corrupted `worldPos` from poisoning `gatherIndirectDiffuse`
  (`SpatialReuseShade.comp:498`, edge-clamped probe cells at `:265`) and the cross-region
  color bleed. Expect the OOB fraction (~27.5%) to collapse.
- [x] Task 5.6 — Small-body bake resolution bump (normals-parity interim fix): the
  subdiv=1 bodies (light/sphere/box) bake at `kSmallN=16` ≈ 1 voxel/world-unit — the
  coarsest normals in the scene, why they render near-black. Raise their bake resolution
  (e.g. 16→32 or subdiv 1→2; small absolute memory/bake cost) and A/B the sphere/box
  luminance vs the virtual capture.

**Gate:** wall ms down; no new holes vs reference captures; 8 bodies; OOB fraction
sharply down (5.5); sphere/box visibly lit, luminance qualitatively closer to the
virtual capture (5.6).

## Milestone M6 — Sync/overlap package (M) — only what M0 attribution justifies

- [x] Task 6.1 — Decouple the march submit from the WSI acquire semaphore (first real
  swapchain writer waits instead; verify cross-frame HitRecord hazard)
  (`ComputeDispatchNode.cpp:288-293`; audit E2). IMPLEMENTED (not skipped): BlitNode owns
  the acquire wait; per-flight `vkWaitForFences` (ring=frames-in-flight=4) guards
  cross-frame reuse, NOT the acquire — validator-verified against the 1c5e3836 bug class.
- [x] Task 6.2 — Blit exit barrier: return render target TRANSFER_SRC→GENERAL at frame
  edge (spec-violation fix; red `layout_sync` was best-wall) (`SwapchainBarriers.h:184,
  :100-103` vs `ComputeStageNode.cpp:348`; audit E3). `MakeRenderTargetPostBlitBarrier`
  restores GENERAL. E3 signature 100%→0% of runs.
- [x] Task 6.3 — Fix the orphaned per-image semaphore re-signal in composite mode
  (`BlitNode.cpp:172-177`; audit E4) + compute-scoped stage masks replacing
  ALL_COMMANDS signals (audit pattern R7). Composite publishes VK_NULL_HANDLE; 4 masks
  scoped (BlitNode→BLIT, ComputeStage/Dispatch→COMPUTE_SHADER, UIRender→COLOR_ATTACH).
  E4 signature 100%→0%. LATENT caveat (not live): a self-blitting terminal
  ComputeDispatchNode would under-sync Present — that config is never instantiated.
- [x] Task 6.4 — A/B with validation layers ON (sync changes never ship unvalidated).
  vixen-ninja Debug compiles VIXEN_VULKAN_VALIDATION=1; validator built pre-M6 binary,
  confirmed E3+E4 every run BEFORE, 0× either signature across 6 runs AFTER.
- [x] Task 6.5 — Expose the widescreen render-scale dial (inventory #12): document
  `VIXEN_RENDER_SCALE` in the bench rig and record a capability curve (1.0/0.75/0.5)
  — a merged, validated 27%-dispatch-cut dial currently sitting unused at 1.0. Default
  stays 1.0; the curve informs the realtime-target definition.

**Gate:** zero new validation-layer errors; wall/GPU-span ratio improves; 8 bodies.

## Phase 2 (dispatch after Phase-1 review with the user)

**Phase-2 backlog (from [[Dormant-Work-Inventory-2026-07]]):** instance-SSBO
dirty-only upload (inventory #5, direction doc exists); dead-branch cleanup —
MultiDispatchNode archive (#15), spec-constant plumbing feed-or-remove (#14);
per-axis box bake grids (#8, the full far-hit/normals fix); tiered-ESVO stays dormant
until multi-tier scenes exist (#9); shader-cache hygiene (M2b carry-forward): rename
misnamed `ComputeSHA256Hex`→`ComputeFNV1a64`, fix `currentCacheSizeBytes` never
incremented on Store (size eviction can never fire); **KI-035 per-octree residency
plumbing** (M2c finding: `CreateOctreeBuffers` stamps one whole-pool `brickResident`
scalar — real product gap, off the critical path since M3–M6b use uniform residency);
**KI-036 shadow-shading test coverage** (graph-level integration test through the real
RenderGraph).

## Milestone M6b — Hybrid-frame bench: mixed providers + delta modification (S–M)

Rationale: the end-state frame composes procedural baseline + baked voxel deltas;
nothing currently benches or verifies that composition. User-defined acceptance test
(2026-07-16): virtual scene + a visible modification (hole/feature) on one element
delivered via the stored/delta path.

- [x] Task 6b.0 — Instance-seam winner tie-break (user-reported 2026-07-17,
  Screenshot_244): checkerboard "z-fighting" patches at instance junctions —
  ceiling/leftWall/backWall corner and floor/rightWall corner (the SAME junction as the
  golden's row-21 cross-binary near-tie flip). Root cause hypothesis: two abutting
  Cornell slabs return near-equal hitT and per-pixel float noise flips the per-instance
  bestT winner. Fix DETERMINISTICALLY, not a depth-bias hack, with **depth as the primary
  arbiter** (user directive 2026-07-17): `if (|tA−tB| > eps*max(|t|,1)) winner = NEARER;
  else winner = lower instance index`. The stable-index tiebreak fires ONLY inside the
  RELATIVE band (`eps*max(|t|,1)`, NOT a fixed absolute epsilon — a 0.1-unit gap is
  coincident at hitT≈26 but resolvable at hitT≈1). Do NOT break ties by index
  unconditionally: a baked/stored DELTA in front of/behind a procedural baseline must
  still win on depth — that's the exact hybrid case M6b supports. Provider-priority-in-band
  (delta overrides baseline when truly coincident) is OUT of scope → delta program. Gate:
  checkered patches gone at both corners; cross_path agreement does not regress (run
  QUIET — same_path gate is contention-sensitive, see M6 verify note); same_path golden
  re-blessed ONLY if the map change is exactly seam cells becoming coherent (expect
  near-tie flips to disappear — the ≤2/625 slack may become unnecessary; note it if so).
- [x] Task 6b.1 — v1 "hole in the wall" (today's machinery): virtual Cornell with ONE
  body flipped to PROVIDER_STORED whose bake is the MODIFIED shape (e.g. right wall
  recipe minus a cylinder — `BakeRecipeInstructionsToSdfWorld` bakes arbitrary recipe
  instructions, zero new engine work). New `temp_bench/run_hybrid.bat`.
- [x] Task 6b.2 — Mixed-provider splits: walls stored + objects procedural, and the
  inverse (`temp_bench/run_mixed.bat`).
- [x] Task 6b.3 — DOCUMENT-AND-SKIP: no runtime (no-rebake) voxel-edit path exists in the
  codebase (grep VoxelAuthoring/RuntimeVoxelEdit/VoxelBrush/EditVoxel = zero non-test
  hits; not in the dormant-work inventory). What exists is bake-time recipe authoring,
  which 6b.1/6b.2 already exercise. Belongs to the delta program when a live-edit path lands.

**Gates:** hole/feature visibly correct (light passes through it: shadows + GI respond);
instIdx map correct for all 8 bodies in every variant; per-pass timers within the
sum-of-parts envelope; no provider-boundary artifacts where lighting/shadow rays cross
provider kinds. These variants become STANDING gates for M7/M8 and regression tests for
the delta program (v3, same-body delta-over-procedural, lives there — out of scope here).

## Milestone M7 — Boot parallel bake + serialize bulk path (M)

- [x] Task 7.1 — Parallelize the 8 independent per-body bakes (`std::async` per body;
  Gaia cross-world thread-safety smoke test FIRST — audit F1 + uncertain-11).
- [x] Task 7.2 — SerializeSdf bulk entity path (`getBrickEntitiesInto`/`getEntityFast`;
  audit F4) + skip double serialize/mip-bake (F5).
- [ ] Task 7.3 — ~~Wire shader cache~~ MOVED to M2b (pulled forward 2026-07-16).
- [x] Task 7.4 — Bake-artifact disk cache, design-first (inventory #7 clarification:
  NO bake cache exists anywhere — the 87–190 s bake re-runs every boot; JIT Inc1's
  content-hash cache is write-only recipe-bytecode dedup, unrelated). Key on
  recipe+params+resolution → warm boots at file-load speed. Effort L.
- [x] Task 7.5 — Emit per-body OCCUPANCY stats during bake/serialize (occupied bricks /
  bounding-volume bricks, and voxel fill ratio per body) to the log + a small artifact.
  This is the data M8 needs to decide the dense-3D-texture-vs-ESVO fork PER BODY (user
  2026-07-17): a dense texture only wins where sparseness is already near-zero. Cheap —
  the bake already knows these counts.

- [x] Task 7.6 — **Brick dedup by reference (content-addressed brick pool)** (user idea
  2026-07-17; DONE, commit ab4ad40a, own Opus validator APPROVED. Achieved 5.2x
  (4112→790 bricks), byte-identical parity. SCOPE as shipped: intra-octree (per-body)
  content-addressed dedup as an opt-in post-process wired ONLY into the StoredSdf-path
  Cornell baked demo — deliberately NOT folded into SerializeSdf/ConcatenateSdf generally,
  which is the load-bearing SAFETY decision: two brick-read paths exist (StoredSdf uses
  brickGridLookup which dedup rewrites; the ESVO material DDA uses getContourPointer which
  dedup does NOT touch), and the baked demo uses only StoredSdf. Cross-body dedup =
  documented follow-on (needs a global pool). Distinct from 7.4's cross-boot cache
  — this attacks IN-POOL duplication):
  hash each baked 8³ brick payload; keep a brick-content-hash → pool-slot map; when a new
  ESVO leaf would upload a brick whose content already exists, point that leaf's
  `brickLookupBase` at the EXISTING slot instead of uploading a duplicate. Kills
  intra-scene + inter-body brick redundancy (flat wall-slab interiors are the same brick
  repeated; the 5 Cornell walls share many identical bricks) → smaller pool (VRAM +
  upload bandwidth) + less serialize work. CORRECTNESS BAR (the M1 `brickLookupBase`
  addressing bug was exactly this surface): a shared brick must be BIT-identical incl. all
  addressing assumptions, and each referencing leaf must keep its own correct base/scale
  even when the payload is shared — reference-count slots so nothing is freed while
  referenced. Composes with 7.4 (orthogonal axes) AND is the sparseness-preserving
  foundation for M8's packed-atlas fallback (dense-texture compactness WITHOUT losing
  octree empty-space skipping) AND with the delta renderer (a delta sharing unchanged
  bricks by reference). Gate: pool slot count drops (report the dedup ratio); rendered
  output + serialize hash BYTE-IDENTICAL (dedup must be invisible to the image); parity PASS.

**Gate:** boot < 60 s; serialize output byte-identical (hash compare); tests green;
per-body occupancy stats recorded for the M8 decision; brick-dedup ratio reported with
byte-identical output preserved.

### Task 7.4 design note — bake-artifact disk cache (written 2026-07-17, before implementation)

**Where it lives:** `libraries/SVO/include/BakeArtifactCache.h` (header-only, same idiom as
`SdfBake.h`/`ShellOctreeGpu.h`). Files land in `cache/global/BakeArtifactCache/<hex-key>.bake`
— mirrors the existing `cache/global/` + `cache/devices/<id>/*.cache` convention
(`ShaderCacheManager`'s own `cacheDirectory` pattern), global (not per-device) because a bake
artifact has no GPU-specific content, matching `SerializedOctree`/`ConcatenatedOctrees` being
pure CPU-side byte buffers.

**Key derivation (must cover everything that changes the bake output):** per Cornell-baked-demo
call site, concatenate in FIXED body order (leftWall, rightWall, backWall, floor, ceiling,
light, sphereObj, boxObj — the same order `BuildCornellWorldSpaceBodies`/`octreesForCat` already
use) a byte stream of:
  1. a `uint32_t` format-version constant (bump on any change to this list or to
     `SerializedOctree`/`ConcatenatedOctrees`'s layout — an implicit invalidator for future
     schema changes);
  2. per body: the raw bytes of every `SdfInstruction` in `prog` (132 B POD, `sizeof`-exact,
     `std::memcpy`-able — covers the authored geometry/primitive-params completely, no manual
     field enumeration needed and no drift risk if a recipe changes);
  3. per body: `worldCenter` (vec3), `n` (bake grid resolution), `subdiv`, `worldHalfExtent`
     (box-tight region, 0 sentinel = full cube), `brickDepth` (always 3 here, included for
     future-proofing) — every numeric bake-shape parameter `bakeWorldSpaceBody`/the light's
     direct `BakeSdfWorld` call takes;
  4. the shared `kBand` constant (occupancy band, currently 2.0f) — a global that affects every
     body's bake;
  5. the light body's extra `kLightEmissionIntensity` scalar (its EmitFn's only free parameter).
Hash this byte stream with a 64-bit non-cryptographic hash (FNV-1a, matching the existing
`GaiaVoxelWorld::BlockQueryKeyHash` idiom already in this codebase) rendered as a 16-hex-digit
filename. Collision risk is a non-issue at this key's entropy for a single-machine dev cache;
if this ever needs to be shared/distributed, upgrade to a real cryptographic hash — noted, not
built, since it's YAGNI for the current single-dev-machine use case.

**What must NOT be in the key:** anything that doesn't change bake OUTPUT — e.g. render-scale,
probe-grid config, debug-capture flags. Including extra unrelated inputs in the key only costs
cache-miss churn, never correctness; the risk is exclusively on the OMISSION side (a bake input
this key forgets to cover silently serves a stale artifact for a changed recipe). This is why
every numeric parameter `bakeWorldSpaceBody` takes is listed explicitly above rather than trusting
recall — the validator should cross-check this list against `bakeWorldSpaceBody`'s actual
parameter list and `BakeSdfWorld`'s signature at review time.

**Format on disk (single file per body-set, matching `ConcatenatedOctrees`'s own all-byte-vectors
shape):** a small fixed header (format-version `uint32_t`, body count `uint32_t`, light-tree-cut
node count `uint32_t`) followed by, per body, a length-prefixed dump of every `SerializedOctree`
byte-vector field. `ConcatenateSdfWithMips`'s OUTPUT (`ConcatenatedOctrees`) is what's cached
directly, not the pre-concat per-body octrees — that's the exact struct `SetRecipePool` consumes,
and concatenation is deterministic given the same inputs, so caching post-concat also skips the
concat/mip-bake CPU work on a hit, not just the per-body bake. Each `std::vector<uint8_t>` /
`std::vector<TierRef>` member is written as `(uint64_t size, raw bytes)`; fixed-size fields
are written verbatim. The light-tree cut (`std::vector<LightTreeNode>`, built from the light
body's own mip pool, entirely a function of the light body's bake — already covered by the key)
is cached alongside since `BuildLightTreeCut`'s CPU cost is part of what a warm boot should skip
too.

**Invalidation:** purely content-addressed — a key miss (recipe/params/resolution/anything
above changed) simply bakes fresh and writes a new file under the new key; stale files are never
overwritten, only orphaned (acceptable dev-cache growth; no eviction policy at this scope, unlike
`ShaderCacheManager`'s size-based LRU — YAGNI until this actually fills a disk, noted for a future
increment if it becomes a problem, mirrors the "one cache line, not the whole subsystem"
philosophy this doc's ground rules ask for).

**Guard (validator-checkable):** a cache HIT must reproduce byte-identical
`ConcatenatedOctrees`/light-tree-cut to a cold bake — verified by hashing the in-memory
`ConcatenatedOctrees` byte-for-byte both ways (cold bake vs warm cache-load) on the SAME machine
state, plus the existing same_path parity gate on an actual warm-cache boot.

## Milestone M8 — 3D-texture brick pool prototype + relaxed stepping (L) — the 60+ lever

> **SPARSENESS CAVEAT (user 2026-07-17):** a DENSE per-octree 3D texture discards BOTH
> inter-brick sparseness (the octree's empty-space skipping — empty Cornell interior is
> free today) AND the sparse-mip hierarchical LOD (M4b). The trade buys hardware trilinear
> filtering + coherent linear access. It only wins for NEAR-SOLID bodies (the 5 walls),
> and loses badly for genuinely sparse ones. So: (a) prototype dense on the 5 walls ONLY;
> (b) the 8.3 decision is PER-BODY (dense texture for solid, keep ESVO for sparse), never
> a global switch; (c) gate the go/no-go on M7 Task 7.5's measured per-body occupancy.
> If dense proves wrong even for walls, the sparse-preserving fallback is a packed brick
> ATLAS with 1-voxel aprons still marched through the octree — hardware filtering WITHOUT
> dense allocation (this was M8's own phase-2; pull it forward if 8.1 disappoints).
> **M7 Task 7.6 (content-addressed dedup'd brick pool) is the concrete foundation for
> that atlas** — reference-counted content-addressed slots are already the packed,
> sparseness-preserving pool this fallback needs; M8 would add aprons + hw filtering on top.

- [x] Task 8.1 — ~~Dense per-octree R16F 3D texture for the 5 walls~~ **DROPPED (user
  decision 2026-07-17, on M7.5 occupancy evidence).** The dense-texture premise was "the
  walls are near-solid" — but M7.5 measured them at **~3% brick-occupancy** (thin slabs in
  a mostly-empty 128³ bounding volume); a dense texture sized to that volume would allocate
  ~30× the memory the wall uses — the exact sparseness disaster. The only 100%-occupied
  bodies (light/sphere/box) are tiny (64 bricks) with negligible payoff. NO viable
  dense-texture target exists in this scene. Moreover M7.6's dedup ALREADY delivered the
  compaction dense-texture was chasing (5.2×) while KEEPING sparseness — the dedup'd pool
  IS the packed sparse structure. Recorded as a no-go; the sparse-atlas+aprons idea (8.3)
  stays a future option on top of the 7.6 pool if hardware filtering is ever wanted.
- [x] Task 8.2 — Over-relaxed sphere tracing (ω=1.5 + unbounding-sphere overlap test) in
  `marchBrickSdf`'s primary iso-surface loop (commit `15d500d2`, Opus validator APPROVED).
  **~1.3–1.4× frame speedup** (esvo 69.6→55.0 ms = −21%; frame 105.8→75.0 ms = −29%).
  Overlap test (the crux, took 3 iterations for units-consistency): radii = RAW SDF `d`
  (true unbounding-sphere radii, NOT `honestStep=d*0.5773503` which is a step-size margin),
  travel = actual `stepTaken`; test `stepTakenPrev > dPrev + d` → fallback rolls back to the
  pre-relaxation point + a forced un-relaxed next step (flag, no infinite retry); sentinels
  never relaxed; ω=1.0 byte-reproduces the original loop. **CORRECTNESS NUANCE (record
  correction): relaxation is NOT bit-exact to non-relaxed marching — a ω=1.5-vs-1.0
  image diff shows 219 SUB-PERCEPTUAL edge pixels (216 @ mag 1/255, 3 @ 6/255, all on
  silhouettes = iso-crossing quantization drift), which is the EXPECTED, acceptable
  over-relaxation signature, NOT surface-skip.** A punch-through would be contiguous
  50–150-mag wall→box blocks — none exist; instIdx same_path byte-identical (0/625). The
  controller's initial "0/250000" diff was a STALE capture pair (likely a lingering
  VIXEN.exe per KI-041 serving a stale frame); the validator's 219 is the honest number.
  Scope: `marchBrickSdf` only (primary rays); `marchBrickSdfAnyHit` (shadow/probe) untouched.
- [x] Task 8.3 — ~~Decision doc: dense-vs-sparse~~ SUBSUMED (8.1 dropped). Residual future
  option: if hw trilinear filtering is ever wanted, spec the sparse-atlas+aprons layer ON
  TOP of the 7.6 content-addressed pool (aprons + `textureLod`, sparseness preserved). Not
  motivated by current numbers — the 8.2 relaxation win + 7.6 dedup already deliver.

**Gate:** MET — 8.2 esvo delta recorded (~1.3–1.4×); correctness rig (instIdx 0/625
byte-identical + 219 sub-perceptual edge pixels, no surface-skip); hw-atlas decision
documented as future-only.

## Milestone M9 — MIS-DIAGNOSED + REVERTED; real finding = baked object/lighting quality gap (2026-07-17, hardware eval, Screenshot_246)

> **RESOLUTION (2026-07-17): Task 9.1 was WRONG and is REVERTED (commit `af5da90b` reverts
> `4a456395`; epsilon back to 1e-4, golden restored).** The Opus validator (oracle-based)
> proved the tie-band tightening moved the baked path AWAY from analytic ground truth:
> controller diffed the VIRTUAL (analytic, no-tie-break) instIdx map vs baked — at rows
> 19-20/cols 18-19 the VIRTUAL render says rightWall(**1**), but the 1e-5 baked path wrongly
> said floor(**3**), and the re-bless endorsed that. So 1e-4 was CORRECT at that corner; the
> "wall eats floor wedge" was a controller MIS-DIAGNOSIS. The green presence at the
> floor/rightWall corner in Screenshot_246 is LEGITIMATE geometry (virtual shows it too).
> LESSON: never re-bless the baked path's output when it disagrees with the analytic oracle;
> always diff baked-vs-virtual instIdx BEFORE concluding a baked cell is "wrong."
>
> **THE REAL FINDING (from comparing the two rendered hud_capture_150.png images):** the
> BAKED path renders the sphere/box OBJECTS and LIGHTING markedly WORSE than the VIRTUAL
> path — virtual shows crisp, correctly-lit 3D sphere + box (real depth/shading); baked
> shows flat, washed-out, barely-visible grey smudges for the same objects, and a dimmer
> overall scene. THIS is the genuine baked-vs-virtual quality gap worth chasing (and the
> whole point of the hybrid "virtual baseline + baked deltas" architecture — the baked half
> is visibly weaker). Distinct from tie-break/seam issues. Needs its own diagnosis milestone
> (M10?) — candidate causes: baked object normals/shading (the fenced Phase-2 "small-body
> bake-normal" item from M5b?), lighting/GI applied differently per provider, or
> object-body bake resolution. GATE for any real fix: baked object shading + lighting must
> approach virtual's, verified by baked-vs-virtual IMAGE comparison at the objects, not just
> the instIdx map. PAUSED for user direction on scope.

> **OPUS-MAX DEBUG RESULT (2026-07-17, commit `067615ff` = diagnostic-only, ZERO shader
> change): the seam geometry is ALREADY COHERENT and non-flipping at the current 1e-4
> tie-band. No fix needed; the tie-band must NOT be touched.** Proven oracle-based against
> the virtual render:
> - **floor/rightWall seam is CORRECT as-is.** Disabling the tie-break (`SEAM_TIE_EPS_REL=0`,
>   pure float compare) flips those cells AWAY from virtual (rightWall→floor) — so the 1e-4
>   lower-index tie-break is doing the RIGHT thing there and is NECESSARY. This is the exact
>   seam the reverted 1e-5 attempt broke. Experimentally confirmed, not opinion.
> - **The original "checkering" (M6b's target) is GONE** — deterministic renderer, two runs
>   md5-identical, zero single-pixel flips in the seam window.
> - **The ONE residual cell that disagrees with virtual — pixel(290,390), box(7) vs floor(3)
>   — is NOT a tie-break artifact.** Disabling the tie-break leaves that boundary
>   byte-identical (the box/floor are cleanly depth-separated there, tie-break never fires).
>   ROOT CAUSE: the box object bakes at 32³ voxels over its bounding volume (~0.15
>   world-units/voxel ≈ 2 screen px at 34-unit depth), so its rounded-corner silhouette
>   quantizes ~1 voxel narrower than the analytic box. Floor hits are worldPos-IDENTICAL
>   baked-vs-virtual; only the box's edge ends 1-2px early. This is bake resolution, a
>   BAKE-SIDE decision, not a shader fix.
> - Baked instIdx map matches virtual at EVERY seam except that one quantized box-corner
>   cell. No golden re-bless (correctly avoided the trap). OOB 0, 8 bodies, no perf change.
>
> **DECISION (user 2026-07-17): ACCEPT the one-voxel box-corner cell as the documented
> baked/virtual quantization baseline.** Single cell, one voxel (~2px), coherent + stable;
> geometry-coherence goal is MET. Tie-band stays 1e-4 (proven correct). SEAM ISSUE CLOSED.
> Re-bake at 64³ was declined (not worth the box brick-pool/bake-time cost for a 1px edge).
> (SEPARATE, out of scope: baked sphere/box render fainter/darker than virtual = the known
> lighting/shading gap, candidate M10 — the user has flagged this as the next thing after
> the seam issue, known and pre-existing.)

## Milestone M10 — Baked/virtual lighting parity: stored-SDF shadow false-occlusion (2026-07-18)

**Root cause (PROVEN oracle-based, Opus-high diagnostician, diagnosis-only — no fix applied):**
the baked sphere/box/walls render fainter than virtual ENTIRELY because of the **direct-light
shadow term**. The baked (PROVIDER_STORED / ESVO) geometry casts **false shadows** on the
scene's directional light that the virtual (PROVIDER_PROCEDURAL, analytic) geometry does not.
Not normals (unit-length, aggregate-identical baked-vs-virtual), not albedo (byte-identical),
not roughness/DDGI (forcing both to match moved the image by 0.000). Mechanism: shadow rays
test the baked SDF's **dilated occupancy band** (kBand=2.0 voxels + wall subdiv thickening,
`BuildRenderGraph.cpp:3633/3782`) via `marchBrickSdfAnyHit`'s `d < EPS(0.01)` crossing test
(`StoredSdf.glsl:822`) — a fatter occluder cross-section than the true thin procedural surface,
so grazing shadow rays that cleanly MISS the virtual geometry clip the baked band.

**Evidence (direct in-engine A/B against the virtual oracle, virtual = ground truth):**
- shadows ON: baked/virtual interior mean = **0.88** (floor 0.72, Lwall 0.77, box 0.93, sphere 0.96; Rwall/backwall 1.00)
- shadows OFF: **1.00** everywhere → the shadow toggle explains the WHOLE gap.
- per-body shadow loss (off→on): virtual 0% everywhere; baked floor **27.4%**, Lwall 22.6%, box 6.7%, sphere 4.3%.
- region pattern confirms it: only positive-NdotL surfaces vs the (1,1,-1) light dim; Rwall/backwall (NdotL≤0, never shadow-tested) stay exactly 1.00.
- NOT self-acne: shadow bias 0.01→0.5 AND tmin 0→2.0 both left the baked image byte-identical → a distant fattened body along the escape ray, not origin-surface acne.

**Classification:** RENDER-TIME (shadow-ray occlusion over stored bodies). The band thickness
is a bake-time input, but the visible gap is produced at shade time by the shadow trace.

**Code path:** `SpatialReuseShade.comp:333-340` (computeLightingWithShadows) → `TraceWorldShadow`
(`TraceWorld.glsl:538`) → per-stored-instance `traverseOctreeInstancedAnyHit` (`TraceWorld.glsl:~652`)
→ `marchBrickSdfAnyHit` (`StoredSdf.glsl:822`). Touches shadow/probe-only any-hit march ONLY —
NOT the primary visible-hit `marchBrickSdf` (`StoredSdf.glsl:633`), NOT the seam tie-break
(`SEAM_TIE_EPS_REL 1e-4`, must stay untouched).

- [ ] Task 10.1 — Tighten `marchBrickSdfAnyHit`'s occlusion crossing test so stored-SDF shadow
  occlusion agrees with the true surface, not the dilated band: require a genuine sign-crossing
  into solid (d well below the band, or interval bracketing) rather than `d < 0.01` on the band
  shell; and/or skip the shadow-origin body (self) for the first band-thickness of arc-length.
  Do NOT touch the primary visible-hit march or the seam tie-break.

**Gate (oracle-based):** re-run the shadow off→on A/B — baked per-body shadow-loss (esp. floor,
currently 27.4%) must drop toward the virtual oracle's 0%; baked/virtual interior mean must rise
from 0.88 toward 1.00. Geometry/silhouette instIdx parity must stay byte-identical (0/625
same_path, cross_path ≥97%); 8 bodies present; OOB not materially up; seam epsilon still 1e-4.
Prove any brightness claim by baked-vs-virtual IMAGE diff at the objects, not eyeball, not the
instIdx map. NEVER re-bless a golden toward baked output that disagrees with virtual.

> Diagnostic tool available for validation: env-gated `VIXEN_CORNELL_NORMAL_DUMP` (off by
> default, +55 lines in `VulkanGraphApplication.cpp`, per-instance albedo/normal/roughness
> aggregator in the tick-150 [CornellDiag] block). The diagnostician also used a shadow off→on
> toggle for the decisive A/B — the implementer should reproduce that toggle to measure.

### (superseded) original Task 9.1 framing — kept for provenance, DO NOT re-attempt as written

- [ ] Task 9.1 — The M6b `SEAM_TIE_EPS_REL = 1e-4` tie-band (`TraceWorld.glsl:48`) is TOO
  WIDE. User caught on hardware: at the DIAGONAL wall/ceiling and wall/floor corners
  (where two abutting slabs meet at ~45°), a CONTIGUOUS wedge of the lower-index WALL
  (red Lwall=0 / green Rwall=1) wins pixels that geometrically belong to the grey
  ceiling(4)/floor(3) — a protruding wrong-color block that doesn't continue the
  surrounding geometry. Visible in the tick-150 instIdx map too: rows 3-8 the walls 0/1
  eat a widening triangular wedge of the ceiling 4; rows 18-22 similar at the floor.
  ROOT CAUSE (confirmed from code + its own comment): along the diagonal seam the TRUE
  depth gap between the two slabs can be smaller than `1e-4*max(|t|,1)` (≈2.6e-3 units at
  Cornell hitT≈26) while still being REAL and resolvable — so the band swallows a genuine
  depth difference and the lower-index tiebreak overrides the true-nearer surface. This is
  distinct from M6b's original checkering (adjacent pixels ALTERNATING); this is a stable
  WRONG-WINNER block. The band's own comment admits observed float noise is sub-1e-5 while
  the band is 1e-4 — 10× wider than needed. FIX (user decision 2026-07-17): TIGHTEN toward
  the noise floor (~1e-5; sweep 5e-6..3e-5) so genuinely-different depths resolve by depth
  and the index tiebreak fires ONLY on true sub-ULP coincident-slab ties. Apply to all 4
  tie-band sites (isCloserHit + the 3 early-reject/uber bands that reference SEAM_TIE_EPS_REL).
  GATE (BOTH failure modes — this is a two-sided coefficient tune): (a) NO checkering
  returns at the abutting seams (too-tight failure), (b) NO wall/floor/ceiling wrong-winner
  wedge at the grazing corners (too-wide failure — verify the instIdx map's rows 3-8/18-22
  show walls NOT eating the ceiling/floor, and inspect hud_capture_150.png at both corners
  vs the virtual render), (c) same_path parity still passes (golden may legitimately shift
  a few cells as the wedge cells flip to their correct body — re-bless ONLY if the change is
  exactly the wedge correcting; document which cells). Escalate to Opus-max if the sweep
  circles (coefficient-tune churn is the escalation-ladder's known trap).

**Gate:** no checkering AND no grazing-corner wrong-winner wedge; instIdx map corners
correct (walls don't eat ceiling/floor); parity passes (re-bless only if wedge-correcting).

---

## Milestone M11 — Cornell ceiling area-emitter + provider-symmetric emission (user-directed 2026-07-18)

**User intent:** make the light clearly come from the top ceiling square (unambiguous light
direction) and TEST per-body light/emission property handling across BOTH providers
(PROVIDER_PROCEDURAL recipe emission vs PROVIDER_STORED baked-voxel emission), vs the VIRTUAL oracle.

**Recon reframe (m11-scout, 2026-07-18 — what already EXISTS vs needs BUILDING):**
- The ceiling area-emitter ALREADY exists: body 5 "light" (`CornellBoxSceneDefinition.h:39-44`,
  kLightEmissionIntensity=6.0) bakes a per-voxel SEM_EMISSION channel → light-tree → already lights
  the scene via ReSTIR-direct (`SpatialReuseShade.comp:438-540`) + DDGI-indirect (probes lit by
  light-tree, `ProbeUpdate.comp:180-247`). NOT a build-from-scratch.
- A SEPARATE always-on DIRECTIONAL light (global node default `LightingConfigNode.cpp:30-42`,
  normalize(1,1,-1), maxShadowDistance=1000) that the Cornell scene never authored is an additive
  analytic term (`computeLightingWithShadows`, SpatialReuseShade.comp:325-354). **This directional
  light is the source of the M10 floor/wall shadow blotching** (grazing false-shadows).
- Emission is a BAKE-TIME per-voxel concept (SEM_EMISSION channel via BakeSdfWorld's EmitFn,
  `SdfBake.h:256`), NOT a per-instance field (`BodyInstanceGpu` has no emission field). The
  procedural recipe VM has NO emission — every variant fakes procedural emission with a mandatory
  side-bake of just the light body (`BuildRenderGraph.cpp:4400-4464`
  BakeRecipeInstructionsToSdfWorldWithEmission). **PROVIDER_PROCEDURAL cannot natively emit** — the
  biggest architectural gap.
- The emissive panel does NOT render self-lit at the primary hit (no `outColor += emission` for the
  camera-visible light face) — it only acts as a source for other surfaces.

**User scope decisions (2026-07-18):** FULL scope (all three increments). Blotching SUBSUMED by
M11 — verify it's gone once the directional light is off, don't chase it separately.
**M11.3 architecture (user, pure-solution):** procedural-native emission must use the EXISTING
kernel/content input→output convention — **emission becomes another recipe OUTPUT channel the
recipe produces at every sample point**, exactly like distance/color/material outputs flow through
the kernel-framework content configs. NOT a separate analytic light list, NOT a GPU tree rebuild.
Unifies the provider asymmetry at the root: BOTH paths source emission from the same recipe-output
contract — stored bakes the recipe's emission output into the channel, procedural evaluates it live.
Same authored value, two materializations. → hinges on the recipe-output SCHEMA / kernel-content
codegen boundary (guarded — `kernel-framework` skill, drift guards; never hand-edit `.g.*`).

- [x] Task 11.1 — DONE · commit `eb7533f0` · 2026-07-18. Disabled the stray directional light for
  Cornell (LightingConfigNode.cpp ExecuteImpl: `cfg.lightCount=0` when IsCornellDemo(), env-gate
  idiom matching sibling ReservoirConfigNode; ambient=0.3 kept — the shadow RAY was the blotching
  source, not ambient; non-Cornell scenes byte-identical). **RESULT: blotching GONE** (floor/walls
  clean, matching virtual), scene still lit from ceiling emitter, geometry parity 0/625, **cross_path
  luminance delta 1.80→0.07** (near-perfect parity). M10 residual/blotching CONFIRMED subsumed —
  it was entirely the directional light's grazing false-shadows. Note: scene now purely ceiling-GI-lit
  = softer/flatter (correct for a single area light) → motivates 11.2 self-lit panel + emission work.
  ORIGINAL: Disable the stray directional light for the Cornell demo so the existing ceiling
  emitter stands alone (zero/remove the LightingConfigNode directional contribution for Cornell, or
  scene-author it off). Re-shoot the baked+virtual A/B. GATE: light visibly comes from the ceiling
  square; the M10 floor/wall blotching is GONE (verify in the new A/B image vs virtual — this is the
  blotching-subsumed check); geometry parity byte-identical 0/625; 8 bodies; scene still lit (GI from
  ceiling emitter intact, not dark). If blotching persists, re-open it as a separate finding.
- [ ] Task 11.2 — Primary-hit self-lit emission term: the camera-visible light-panel face renders
  bright/glowing (add the emission contribution at the primary hit for an emissive body). Benefits
  BOTH providers. GATE: light panel reads as self-lit in baked AND virtual, matched to oracle; no
  regression to GI or geometry parity (0/625).
- [ ] Task 11.3 — Procedural-native emission via the recipe-output contract (the pure-solution
  engine addition): emission as a recipe OUTPUT channel evaluated per-sample, threaded through the
  kernel-content codegen to BOTH (a) the stored bake EmitFn and (b) the procedural live shade/GI
  path — so PROVIDER_PROCEDURAL emits without the mandatory side-bake. Remove/retire the
  side-bake workaround once native emission matches. GATE (oracle-based): procedural-native emission
  A/B vs the side-bake result AND vs virtual — light-tree/GI lighting from a procedural emissive body
  matches the baked-emission body within parity threshold; both providers now provider-symmetric on
  emission; geometry parity 0/625; codegen drift guards pass (no hand-edited `.g.*`). Depends on a
  recipe-output/codegen recon (dispatched) — may split into 11.3a (schema+codegen) / 11.3b (wire both
  consumers) / 11.3c (retire side-bake) once the contract is mapped.

**M11 gate (milestone):** light unambiguously from the ceiling; blotching gone; light panel self-lit;
procedural and stored emission provider-symmetric vs virtual oracle; geometry parity preserved
throughout; codegen boundary respected. Branch `fix/baked-perf-pipeline`, incremental merge to main.

---

## Milestone Map

| # | Name | Tasks | Effort | Target |
|---|---|---|---|---|
| M0 | Measurement + boot triage | 0.1–0.5 | S | attribution + boot −70 s |
| M1 | brickLookupBase + *subdiv + band | 1.1–1.4 | S–M | ~4 FPS, 8 bodies |
| M2 | GPU debug hooks gated | 2.1–2.3 | S | esvo −~20% |
| M2b | Shader disk cache | 2b.1–2b.2 | S–M | boot cut, every bench run |
| M2c | SPV-consumer test health | 2c.1–2c.2 | S–M | restore green gate baseline for M3+ |
| M2d | Automated visual-parity gate | 2d.1–2d.2 | S–M | golden-hash + cross-path divergence detector |
| M3 | March-loop package | 3.1–3.4 | M | ~16 FPS |
| M5 | Trace bounds + culling + lighting parity | 5.1–5.6 | M | **runs 3rd** — cull + DDGI un-poisoning |
| M5b | backWall far-hit root cause + enforce parity | 5b.1–5b.4 | M (Opus) | OOB collapse + cross_path enforced |
| M4 | Shadow/probe economy | 4.1–4.5 | S–M | lighting passes cut |
| M4b | Sparse-Mip for secondary rays | 4b.1–4b.4 | M | probe/shadow coarse-LOD |
| M6 | Sync/overlap | 6.1–6.4 | M | wall≈GPU span |
| M7 | Boot parallel bake (Phase 2) | 7.1–7.3 | M | boot < 60 s |
| M8 | 3D-texture pool proto (Phase 2) | 8.1–8.3 | L | 60+ decision |

Model policy: implementers Sonnet-5 medium; validators Opus high; controller thin.
Fable only on explicit user request. Escalation ladder per Ground rules.

## Progress Log

*(pipeline appends here; one line per milestone close)*

- M0 (Tasks 0.1–0.5): DONE · commits `53a20709..bf1101cf` · Opus validator APPROVED ·
  2026-07-16. **New baked attribution (worktree binary, frames 31–160): probe_update
  431.7 + esvo_traverse_shade 264.4 + spatial_reuse 160.3 ≈ 857 ms ≈ GPU span ≈ wall —
  frame is pure GPU work, no sync stall; probe_update is the LARGEST pass** (audit C4
  promoted into M4). Warm boot 123.1 s (−36% vs 191.7 s baseline; first cold run was
  262.5 s — see warm-run rule). Virtual without VIXEN_PERF_CSV: 5.56 ms/frame (beats
  6.49 baseline; timers are bench-only overhead, discipline verified: 64-bit,
  availability-polled, one frame late). 8 bodies intact in both runs.

- M1 (Tasks 1.1–1.4): DONE · commits `98443fc0..e0f5c123` · Opus validator APPROVED
  (multiplier gate recalibrated) · 2026-07-16. **Vanishing-bodies blocker BEATEN: audit
  B1 confirmed as root cause** — with brickLookupBase fixed, all 8 bodies survive the
  `*subdiv` fix (byte-identical positions across runs). Baked 857→**316.2 ms = 3.16 FPS
  (2.71×)**; per-pass: esvo 264.4→107.4, spatial_reuse 160.3→74.1, probe_update
  431.7→140.6 — uniform across all march passes as predicted. Boot ~87 s. Task 1.3 was
  algebraically free (band predicate reads the now-grid-unit eval directly; verified no
  other consumer assumes world units). OOB fraction 24.0%→27.5% benign
  (denominator-driven). Tests 12/12, 13/13, 7/7. HUD capture now matches virtual ground
  truth (black voids GONE, sphere/box/light rendered).

- M2 (Tasks 2.1–2.3): DONE · commits `281432f4..2bfdfc22` · Opus validator APPROVED
  (gate recalibrated) · 2026-07-16. `VIXEN_GPU_TRACE_HOOKS` gate, default OFF,
  `VIXEN_DEBUG_CAPTURE=1` = single end-to-end re-enable. **Baked 316→300 ms (3.33 FPS);
  hooks cost lives in probe_update (+31.5% when on), NOT esvo — hooks are in the shared
  trace path, 256 rays/probe compound them.** spirv-verified: all atomics + ~416
  instructions stripped; binding shape preserved structurally. Bonus debt repair
  (separate commit `4c599d7c`): KI-034 push-constant mirrors fixed in all 8 files,
  bindings 15/18 gaps + HitRecordBuffer 17→18 miswire, RayTraceBuffer sizing,
  tier-crossing `setBrickLookupBase` fixture fix (correct but test still blanks → M2c).
  Pre-existing `hitPixels=0` class confirmed independently NOT debt-caused → M2c.

- M2b (Tasks 2b.1–2b.2): DONE · commits `241b6819..7cc1c941` · Opus validator APPROVED ·
  2026-07-16. Gate met as scoped to the shader-compile window (all shaders touch):
  cold 5.105 s (misses=4) → warm 3.716 s (hits=4) = **−27.2% / −1.389 s**, byte-identical
  output (25×25 instIdx map + OOB worldPos bit-identical cold/warm, 8 bodies). Poison
  test passes: `VIXEN_DEBUG_CAPTURE` flip busts the cache (misses=4, 4 distinct new
  .spv), re-run reuses it (hits=4). Total boot unchanged — dominated by the ~90 s ECS
  bake (M7.4). Key = 64-bit FNV-1a (helper MISNAMED `ComputeSHA256Hex`) over
  post-splice/post-#include source + stage/entry/opts/Vulkan+SPIR-V target versions;
  collision risk negligible at this scale. Also fixed pre-existing key bug
  (raw-source-as-filename + missing target versions). Carry-forward (Phase-2 cleanup):
  (a) rename `ComputeSHA256Hex`→`ComputeFNV1a64`; (b) pre-existing ShaderCacheManager
  defect — `currentCacheSizeBytes` never incremented on Store, size eviction never fires.

- M2c (Tasks 2c.1–2c.2): DONE · commits `a969615f` + `1538bdba` (comment tail) · Opus
  validator APPROVED · 2026-07-16. **Root cause: KI-018 pass-split `784adff7` moved all
  color writes to SpatialReuseShade.comp; 7 single-pass harnesses asserted a
  never-written image** (matches pre-existing KI-032). Fix: HitRecordBuffer-based
  assertions (byte-exact CPU mirror, real falsifiable checks, incl. the previously
  VACUOUS SubPixel test). Second bug class fixed: 4 tests missing
  `RequestBrickResidency(true)` → mip-fallback instead of the SDF path under test.
  Result: 15 green / 2 red, both KI-filed — KI-035 (per-octree residency clobber,
  `BodyOctreeSceneNode.cpp:660-662`, REAL product gap → Phase-2 backlog; M3–M6b need
  only uniform residency) + KI-036 (shadow test dispatches a shader that no longer
  shades; zero shadow-shading coverage since `784adff7`). Zero product code touched.
  Handed to M2d as warm-ups: pattern-copy for `test_baked_vs_virtual_parity` +
  `test_mip_fallback_render` (same bug, KI-032 documents the shape) and the
  pre-existing HUD `[View]` schema-drift reds (`view_hud_writer_check`/
  `view_hud_blob_check` — stale checked-in `Hud.view.g.cs`/`Hud.blob.g.h`/
  `hud.viewblob` goldens) that would bite M2d's build gate.

- M2d (Tasks 2d.1–2d.2 + warm-ups): DONE · commits `d3a212f6..7a2faf12` · Opus validator
  APPROVED · 2026-07-16. Automated visual-parity gate (`compare_parity.py`,
  `parity_thresholds.json`) landed with two modes. same_path is the HARD gate
  (hash-equality, 0/625 cells, exit 1 on drift; tamper-proofed: body-swap → FAIL 9/625;
  tightening over the plan's ≤2-cell slack validator-approved — near-tie jitter was
  cross-config only, same-config runs are byte-identical). cross_path is report-only
  until M5. **Cross-path baseline (baked vs virtual, HEAD `7a2faf12`): 95.0% cell
  agreement, 8/8 bodies match, luminance mean-abs 1.59 / p99 38.77, 2.9% pixels over
  threshold; baked 3.3 FPS (probe_update 140 ms-dominated) vs virtual 155.6 FPS.**
  Warm-ups: KI-032 readback pattern-copied into mip_fallback (4/4) and
  baked_vs_virtual_parity (3/4, IoU sphere .876 / csg .866 / twist .924; readparam_sphere
  IoU .606 = genuine corpus bug → KI-038); HUD goldens regenerated for real schema drift
  0x55D27B8C→0x7F78462D (branch build fully green). Tool already caught a rig bug: env
  leak made "virtual" silently re-render baked (impossible 100%/0.00 reading).
  **EVERY subsequent milestone validator runs `temp_bench/run_parity_check.bat`.**

- Parity-gate calibration (post-M2d, main-merge verification): first real gate run
  caught a stable cross-BINARY near-tie flip — golden (worktree binary) vs main binary
  differ in exactly 1/625 cells (map row 21, floor(3)/Rwall(1) grazing corner);
  main-vs-main is byte-identical. same_path tolerance widened 0→≤2 cells per the
  thresholds file's own documented procedure, citation embedded (`e5827d0a` main /
  `e745a014` branch). bodies_match + OOB stay hard; tamper signature (9/625 +
  bodies mismatch) still fails.

- M3 (Tasks 3.1–3.4): DONE · commits `e1cdcc70`, `0b0ed807` · Opus validator APPROVED ·
  2026-07-16. Single-brick trilinear fast path (1 lookup + 8 contiguous pool loads for
  ~67% of cells), analytic closed-form cell gradient (replaces 7 trilinear samples/hit;
  honest-path normals now EXACT interpolant gradient — deliberate A2 improvement, not
  parity-identical to the old h=0.5 finite difference; FD kept verbatim for
  contaminated cells), hit-shading cell reuse (color+roughness share one lookup). CPU
  mirror synced 1:1, 12/12 green (all three code paths covered); serialize 13/13, mip
  6/6, sdf_bake 7/7; parity same_path hash-equal 0/625, 8 bodies. **A/B (frames
  31–160): esvo 100→52.7 (1.9×), spatial_reuse 69.5→22.5 (3.1×), probe_update
  137→79.9 (1.7×), whole frame ~300→153 ms (~2×, 6.5 FPS).** Trajectory recalibrated:
  the old "M3 ≈ 16 FPS" over-attributed a red-prototype bundle that included
  M5-owned trace bounds + sync; M3's fetch-volume mechanism is fully delivered, the
  residual is step-count × memory latency (M5/M4b/relaxed-stepping territory).

- M5 (Tasks 5.1–5.6): DONE · commits `2bd30fba..322fdec9` + golden regen `c0df3667` ·
  Opus validator APPROVED · 2026-07-17. **Perf + geometry-repro delivered: whole-frame
  138→106 ms (−23% vs validator's independent M3 baseline; esvo −48%, spatial −48%);
  BOTH user-repro wall-thickness silhouette steps FIXED by Task 5.2's cull alone; all
  8 bodies preserved; 3/625 golden drift individually justified (box-silhouette
  correctness improvement) and goldens regenerated under validator sign-off.**
  Schema: `traceBoundsMin/Max` replaced dead `gridMin/gridMax` at identical offsets
  (SPIR-V reflection parity proven). **Lighting parity RE-SCOPED to M5b: the backWall
  (octree 2) has PRE-EXISTING far-hit corruption across its visible region
  (worldPos.z≈−27 vs true z≈[4,6], confirmed present at the M3 baseline from main's
  own artifacts) — Task 5.4 (far-hit rejection) is implemented+pre-validated but
  DISABLED (`if(false&&)` at BodyInstanceRayMarch.comp:254) because enabling it blanks
  backWall.** Root cause documented precisely (TraceWorld.glsl:278-294): Laine-Karras
  ray setup assumes exterior-ray entry on the root-cube face; interior sub-box entry
  computes the span against the wrong face → degenerate/inverted span → instance
  silently missed — why the cull can only fast-reject, not constrain. Cross-path stays
  enforced:false (p99 38.77 unchanged) until M5b lands.

- M5b (Tasks 5b.1–5b.4): DONE · commits `7e2a983a` + golden regen `40352dce` · Opus
  implementer + Opus validator APPROVED · 2026-07-17. **backWall far-hit ROOT CAUSE:
  non-unit `instDir = rayDir/renderScale` fed into the ESVO ray setup — the hitT
  composition mixes a `length()`-distance (tEntryWorld) with `1/|dir|`-scaled
  t-parameters (state.t_min), agreeing only for unit directions → renderScale-inflated
  far-hits (backWall z≈−27; invisible at renderScale=1, why no prior scene showed it).
  Fix = `normalize(rayDir)` at TraceWorld.glsl:274/:544.** Overturns M5's
  interior-entry hypothesis (disproven: camera exterior; CPU mirror reproduced BOTH
  states byte-exact — z=6.002 fixed / z=−26.992 pre-fix; mirror's skipped normalize
  was exactly why it had always been "correct"). **OOB 51610→0 (28%→0%); this also
  closes the historical "~35–40k out-of-bounds worldPos" mystery chased since before
  the pipeline.** Far-hit rejection re-enabled (now rejects nothing — belt-and-braces).
  **cross_path parity ENFORCED and PASSING** (agreement 99.0%, OOB gate 0.02,
  luminance budgets documented as loose fences — p99 never tracked the far-hit; the
  residual small-body dimness is the Phase-2 bake-normal item). Warm same-session A/B
  vs the M5 boundary: **GPU span 173.6→69.0 ms (~2.5×), probe_update 85→25 ms,
  esvo 68→32, spatial 21→12** (absolute numbers float with machine state across
  sessions; the same-session ratio is the solid figure). Golden regenerated
  (29/625 = backWall+silhouette correction, verified cell-by-cell). Reds = KI'd set
  (035/036/038). NOTE for future validators: full parallel builds can surface ~5
  transient codegen-`_check` reds (dotnet SDFNodeGenerator file-lock race under -j12);
  serial re-run passes; not a regression.

- M4 (Tasks 4.1–4.3+4.5; 4.4 RE-SCOPED): DONE · commits `70b6e426..f23a8d01` · Opus
  validator APPROVED · 2026-07-17. 4.1 NdotL/W gate before both shadow-trace sites
  (provably zero image change). 4.2 parallel any-hit occlusion chain (unit conversions
  verified as correct INVERSIONS of the shipped shading-path math; NOTE: the any-hit
  chain has NO CPU-mirror coverage — its verification authority is the live same_path
  parity gate; NOTE for M4b/tiered scenes: on a taken tier-crossing whose child tree
  misses, shading serves the parked mip as a HIT while any-hit returns NOT-OCCLUDED —
  inert in single-tier Cornell, flagged for tiered-shadow work). 4.3 CPU no-op dispatch
  guard with two self-caught bugs fixed (shared query-pool reset preservation +
  SetParameter placed after all demo env overrides; single-source-of-truth Resolve*
  accessors keep CPU-skip and GPU-config in lockstep). Gates: same_path hash-equal
  0/625, cross_path ENFORCED 99.0%, 8 bodies, OOB 0; direct_lighting now FREE
  (~0.0002 ms, dead full-screen pass eliminated on every path); probe_update correctly
  still dispatches on Cornell. Reds = KI'd set. **Task 4.4 (converged-probe sleep)
  re-scoped to Phase-2: needs new persistent per-probe SSBO + scene-dirty wake hook +
  Inc6-amortization interaction — convergence-DYNAMICS risk class that could pass the
  tick-150 gate yet be wrong later; M4b's secondary-ray mips attack the same probe cost
  with less state risk. Re-evaluate after M4b.**

- M4b (Tasks 4b.1–4b.3; 4b.4 documented+deferred): DONE · commits `f123a0d9`,
  `c4bc07f5` · Opus validator APPROVED · 2026-07-17. **Zero shader changes**: new
  `secondaryRaySizeCoefConstant` (0.05, env `VIXEN_SECONDARY_RAY_SIZE_COEF`) feeds
  field 8 of the DirectLighting/SpatialReuse gatherers (replacing the primary-mirrored
  coefficient that could structurally never trip) and newly fields 8+9 of ProbeUpdate's
  (previously ZERO-FILLED → probe rays always ran full detail). **spatial_reuse
  8.0→~3.8 ms (~2.1×), probe_update 65→~30 ms (~2×, validator-reproduced), whole frame
  −~20%.** Field mapping proven at the reflection level (all 3 shaders share
  SceneBindings' PushConstants; field 8 = raySizeCoef byte 48); primary gatherer
  byte-identical (structural same_path safety); mip-gate math verified to coarsen
  progressively, not floor. Parity: same_path hash-equal, cross_path ENFORCED PASS
  both fences; no leaks/over-darkening; esvo 62–71 ms spread settled as GPU-timing
  noise (5-run evidence). 4.4 (probe sleep) CONFIRMED stays Phase-2 — M4b reclaimed the
  cost with no new state. Benign pre-existing finding for a future KI: systemic
  non-fatal `PushConstantGathererNode::ValidateFieldType` type-mismatch log noise on
  every field/gatherer (present pre-M4b; packing proven correct by the luminance gates).

- M6 (Tasks 6.1–6.5): DONE · commits `b00216a9` (code) + `7e6aaab3` (docs/KI/curve) ·
  Opus validator APPROVED · 2026-07-17. **Systemic validation-layer errors ELIMINATED
  on the live path**: E3 (`VUID-vkCmdDraw-None-09600`, render target left TRANSFER_SRC)
  and E4 (`VUID-vkQueueSubmit2-semaphore-03868`, orphaned binary semaphore) went
  **100%→0% of runs** (validator built pre-M6 binary to confirm BEFORE, ran 6 AFTER).
  6.2: `MakeRenderTargetPostBlitBarrier` restores GENERAL. 6.3: composite publishes
  `VK_NULL_HANDLE`, 4 ALL_COMMANDS masks scoped to real stages (latent non-live caveat:
  a self-blitting terminal ComputeDispatchNode would under-sync Present — never
  instantiated; worth a comment if it ever gains a render target). 6.1 IMPLEMENTED not
  skipped: BlitNode owns the acquire wait; the render-target WAR hazard is guarded by
  `FrameSyncNode`'s per-flight `vkWaitForFences` (ring=frames-in-flight=4=fence ring), so
  frame N cannot write slot N%4 until N−4's blit fenced-complete — the acquire only ever
  gated the swapchain image, which the `writesNoImage` march never touches (verified vs
  the 1c5e3836 reuse-while-pending bug class). **#1 risk (parity drift) DISPROVEN**: the
  implementer's 2/6 anomalous runs (57/625, 13/625 cells) reproduced 0/6 fresh, and the
  magnitude far exceeds the golden's ≤2-cell near-tie band → contended-machine capture
  artifact, backed by the fence mechanism. 6.5: `VIXEN_RENDER_SCALE` curve recorded
  (1.0/0.75/0.5) — esvo/spatial_reuse scale with resolution, probe_update does NOT (fixed
  probe count → largest pass at 0.5). Default stays 1.0. **KI-039 filed** (pre-existing
  boot-time UNDEFINED-layout flake, reproduces on pre-M6 binary, KI-033 trigger class, no
  correctness impact; no number collision with main's KI-037/038). No perf delta expected
  or seen from sync-only work (~102 ms/9.8 FPS, frame stays GPU-bound). Tests: 9/9 new
  unit + 13/13 serialize + 7/7 bake green. HARNESS NOTES (not M6 defects): `win_build.bat`
  targets a nonexistent `vixen_benchmark` and cd's to MAIN not the worktree — build from
  the worktree's VIXEN dir; `run_parity_check.bat` overwrites `baked/run.log` per run.
  **POST-MERGE VERIFY (main `e94f4769`, 2026-07-17):** clean build; parity run 1 PASS
  (0/625), run 2 FAIL (59/625) — but run 2 fired WHILE a concurrent session's `ninja`
  build contended for the machine. Root-caused the 59-cell diff: NOT scattered corruption
  but a COHERENT whole-scene sub-cell registration shift (total hits 183540→180224; every
  body silhouette translated <1 cell, aliased by the 20px grid). On a QUIET machine, 3/3
  fresh baked captures are byte-identical to the golden (hits=183540, cell_diff=0, same
  grid SHA `0e63e208`). Confirms the implementer's flagged 2/6 drift + validator's
  "contended-machine artifact" call — M6 touches zero geometry/camera code. NEW latent
  finding for the capture rig (candidate KI): under CPU/GPU contention the tick-150
  capture lands on a slightly different scene registration; the same_path hard gate is
  contention-sensitive → run parity on a quiet machine (or add a contention guard /
  multi-sample mode).

- M6b (Tasks 6b.0–6b.3): DONE · commits `805c2871` (6b.0) + `da893fa6` (6b.1) +
  `dd9ea1bf` (6b.2) · Opus validator APPROVED · 2026-07-17. **User-reported seam
  checkering (Screenshot_244) FIXED**: `isCloserHit()` in TraceWorld.glsl adds a
  relative-epsilon tie-band (`1e-4*max(|bestT|,1)`) at 4 sites (2 winner-compares +
  2 front-to-back early-rejects, symmetric band so no in-band candidate is pre-rejected);
  **depth is the primary arbiter** (`candidateT < bestT` outside the band — supports
  hybrid deltas in front of/behind a baseline), lower instance index wins only inside the
  band (coincident-slab symmetry break). Golden re-blessed HONESTLY: exactly 2/625 cells
  changed (rows 18-19, floor(3)→rightWall(1), lower-index-wins direction), no non-seam
  movement; ceiling corner already resolved to leftWall. Parity byte-identical 3/3 fresh
  runs even under load 12-18 (strong determinism). **HYBRID FRAME shipped** (6b.1,
  `VIXEN_DDGI_CORNELL_HYBRID_DEMO`): rightWall PROVIDER_STORED baked as RoundedBox−Cylinder
  through-hole (Transform/RestorePos wrapper around the local-Y-fixed Cylinder opcode);
  hole A/B-confirmed (lit patch through the bore vs uniform virtual wall), OOB 0/183682,
  8 bodies. **MIXED-provider** (6b.2, `VIXEN_DDGI_CORNELL_MIXED_DEMO=walls_stored|
  objects_stored`) both modes clean, OOB 0, no provider-seam artifacts. 6b.3 (runtime
  voxel-edit) document-and-skipped — no live-edit path exists (grep-verified). Found+fixed
  **2 latent OR-chain gaps** (CornellDiag readback gate + camera-preset gate both omitted
  new demo env vars) and — significant — **`run_baked.bat`/`run_virtual.bat` were silently
  benching the MAIN-checkout binary** via a hardcoded path; now `%~dp0`-self-resolve to the
  worktree (any earlier bench numbers from those two scripts may be stale-binary reads;
  `run_parity_check.bat` was already self-resolving so the parity gate stands). Standing
  tests green (7/7, 13/13, 16/16 — CPU mirror is single-instance so needs no tie-break
  mirror). Benign cosmetic note: CornellDiag instIdx-map *labels* differ across demo blocks
  (instance-slot ordering) but worldPos/color/geometry all correct — legend-only, doesn't
  touch the golden.

- M7 (Tasks 7.1/7.2/7.4/7.5/7.6): DONE · commits `e398e40f` (7.1/7.2/7.4/7.5) +
  `ab4ad40a` (7.6 dedup) · TWO Opus validators APPROVED (core + dedup) · 2026-07-17.
  **HEADLINE — bake-artifact disk cache (7.4): cold boot ~130-150s → WARM boot ~16-20ms,
  byte-identical.** No bake cache existed before; the full bake re-ran every boot. Key =
  FNV-1a-64 over format-version + per-body {132B SdfInstruction bytes, worldCenter/n/subdiv/
  worldHalfExtent} + kBand + light emission; caches POST-concat ConcatenatedOctrees +
  light-tree-cut. Validator MUTATE-KEY tested: changed light intensity → key changed → MISS
  → re-bake, never a stale serve. **7.6 BRICK DEDUP (user idea): 5.2x pool reduction
  (4112→790 bricks)**, byte-identical parity — intra-octree content-addressed dedup
  (payload hash + memcmp tie-break, complete key proven from the StoredSdf shader read
  path), wired ONLY into the StoredSdf-path baked demo (the ESVO material DDA uses a
  different brick lookup dedup doesn't touch — this scoping is the load-bearing safety
  decision). Fixed a 7.5↔7.6 interaction (occupancy sum taken pre-dedup would inflate
  fill ratio >1.0; recomputed from kept bricks → all bodies read exactly 1.0). Cache
  format-version bumped 1→2 (dedup'd content shape changed; content-addressed invalidation
  rejects v1). **7.5 OCCUPANCY DATA (feeds M8): walls ~3% brick-occupancy (thin slabs, and
  the source of the 5.2x dedup — identical repeated bricks), light/sphere/box 100%
  (box-tight); all voxelFillRatio=1.0.** 7.1 parallel bake hit a REAL hazard — gaia
  ChunkAllocator is a process-wide singleton with an unsynchronized free-list, crashed
  100% unlocked (caught only by live-run, not static analysis); fixed with a serializing
  mutex → net perf modest/negative, but validator recommends KEEP (with the cache, bake is
  a cold-boot-only cost; the mutex is the correctness floor). 7.2 bulk serialize
  (EntityBrickView + getEntityFast) + triple-serialize collapse, byte-identical (parity +
  13/13). Tests 7/7, 13/13, 16/16 green. PROCESS NOTE: a mid-flight scope-add (7.6) to an
  already-reported worker caused a HEAD-moved-under-validator collision + a stray
  uncommitted edit; resolved by reverting the stray edit + expanding validation scope.
  Lesson: once a worker reports, new work = fresh dispatch, never a mid-flight add.

- M8 (Task 8.2 only; 8.1 dropped, 8.3 subsumed): DONE · commit `15d500d2` · Opus
  validator APPROVED · 2026-07-17. **Over-relaxed sphere tracing → ~1.3–1.4× frame
  speedup** (esvo 69.6→55.0 ms −21%, frame 105.8→75.0 ms −29%, validator-measured n=128
  clean). ω=1.5 with the unbounding-sphere overlap test in `marchBrickSdf`'s primary loop;
  the correctness crux was units-consistency (took the implementer 3 iterations): radii =
  raw SDF `d`, travel = actual `stepTaken`, `stepTakenPrev > dPrev + d` → roll back +
  forced-honest next step; sentinels never relaxed; ω=1.0 reproduces the original loop.
  **USER-REPORTED artifact (Screenshot_245, box punching through walls) was the WIP** — the
  implementer's attempt-2 (honestStep-as-radius, broke parity 136/625); the committed
  attempt-3 is correct. **CORRECTNESS NUANCE: relaxation is NOT bit-exact — ω=1.5-vs-1.0
  image diff = 219 SUB-PERCEPTUAL edge pixels (216 @ 1/255, 3 @ 6/255, silhouette
  quantization drift), the EXPECTED over-relaxation signature, NOT surface-skip** (a
  punch-through would be contiguous 50–150-mag wall→box blocks; none exist). instIdx
  same_path byte-identical 0/625. The controller's initial "0-pixel" diff was a stale
  capture pair (KI-041 lingering-VIXEN.exe class); validator's 219 is the honest number —
  record corrected. Scope: primary rays only (`marchBrickSdfAnyHit` untouched). CPU mirror
  ports ω=1.0 with a documented drift note (test passes; GPU relaxation proven correct
  independently → coverage gap not correctness bug). Filed **KI-041** (pre-existing
  intermittent late-frame Vulkan-validation crash + lingering VIXEN.exe, reproduces at both
  ω, found during 8.2 validation) and **KI-040** earlier (bake-cache loader bad_alloc).

- M10 DIAGNOSIS (diagnosis-only, no fix): DONE · Opus-high diagnostician · 2026-07-18.
  Baked-fainter-than-virtual root cause PROVEN oracle-based = stored-SDF shadow-ray
  **false-occlusion** of the dilated occupancy band (kBand + wall subdiv thickening), not
  normals/albedo/roughness/GI (all ruled out by direct A/B: albedo byte-identical, normals
  unit-length aggregate-identical, roughness/DDGI forced-to-match moved image by 0.000).
  Decisive A/B: shadows OFF → baked/virtual = 1.00; shadows ON → 0.88 (baked floor loses
  27.4% direct light, virtual 0%); only positive-NdotL surfaces dim, Rwall/backwall untouched.
  RENDER-TIME (shadow trace over `marchBrickSdfAnyHit`, `StoredSdf.glsl:822`, `d<0.01` band
  shell). Fix direction (Task 10.1, not yet impl): tighten the any-hit occlusion crossing to
  the true surface / self-skip first band-thickness; shadow/probe-only, seam tie-break
  untouched. Left an env-gated `VIXEN_CORNELL_NORMAL_DUMP` diagnostic (+55 lines, off by
  default) in VulkanGraphApplication.cpp for fix validation. Shaders/libraries unchanged;
  seam epsilon still 1e-4. NEXT: dispatch Sonnet-medium implementer for Task 10.1.

- M10 Task 10.1 (fix attempt): DONE but HONEST NEGATIVE — the proven mechanism did NOT
  close the gap · commit `3142047c` · Sonnet-medium · 2026-07-18. Tightened
  `marchBrickSdfAnyHit` occlusion test `d<0.01` → `d<-EPS` (true inside-crossing; stored
  Density is a real signed distance). Strictly correct + zero-regression + geometry parity
  intact (0/625 same_path, cross_path 99.8% PASS), so KEPT. BUT the A/B is byte-identical
  before/after: floor shadow-loss 24.51% before AND after, interior baked/virtual ratio 0.895
  unchanged, overall 10.03% unchanged. Sensitivity probes (OCCLUDE_EPS=-1.0; 2.5-voxel
  self-skip) equally inert (4/250000 faint pixels). **The dilated-band crossing threshold is
  NOT the dominant cause for this scene** — the diagnostician's mechanism was real but minor.
  Added reproduction tools: `VIXEN/tools/bench/shadow_ab.py` + `temp_bench/run_baked_shadow_{on,off}.bat`
  + `run_virtual_shadow_on.bat` (the shadow on/off/virtual A/B triad).
  **OPEN ROOT CAUSE + SCOPE FORK (needs user direction before re-dispatch):** implementer
  flags a likely CONFLATION — `parity_thresholds.json` already attributes a ~40 p99-luminance
  gap to *"baked-normal-quality on subdiv=1 small bodies"* (a DIFFERENT, already-known,
  Phase-2-deferred issue). The per-body "shadow-loss" numbers may be measuring that
  normal-quality effect, not shadow false-occlusion. Other open candidates: (b) maxShadowDistance=1000
  → shadow rays graze near-parallel to wall faces over a long span; (c) live GPU instrumentation
  of `marchBrickSdfAnyHit`'s actual `d` distribution (never done — reasoned from threshold only).
  ESCALATION-LADDER read: this is a "new distinct blocker" (mechanism doesn't explain the gap),
  not a failure to fix a known one → escalate to a deeper Opus diagnostic, NOT re-dispatch Sonnet
  on the same premise. PAUSED for user: which target — (A) re-frame as the known baked-normal
  gap, (B) deeper shadow-geometry diagnostic, or (C) accept-and-defer. USER CHOSE (B).

- M10 DEEPER DIAGNOSTIC (INSTRUMENTED, diagnosis-only): DONE · Opus-high · 2026-07-18. VERDICT:
  the dimming IS shadow-driven via **TWO distinct false-occlusion mechanisms** (~50/50), NOT a
  normal-quality/subdiv=1 conflation. CONFLATION RULED OUT with live data: shadows-OFF baked
  matches virtual-ON to ~1.00 at the dim pixels (floor bOFF 88.96 vs vON 88.86; false-shadowed
  pixels collapse to exact ambient-only 64.8/24.8 with shadowFactor=0 while virtual stays lit
  103.5/44.5). Normal-quality is at most a ~1.5-3% residual tail, not the gap.
  - **MECH 1 (~25/57 floor pixels): coarse mip-coverage any-hit occlusion.** TraceWorldShadow's
    any-hit traversal has 3 `mipHasCoverage()` early-return-TRUE paths (sub-pixel/tier-cross leaf,
    brick-not-resident, LOD-subpixel non-leaf; `SceneBindings.glsl:~1649/~1677/~1705`) that report
    occlusion WITHOUT marching the SDF — the LOD sub-pixel footprint falls back to a whole-leaf
    coverage bit, which for a dilated-band brick reads "occupied" across the +band shell. Causation
    A/B (`VIXEN_SHADOW_NO_MIP_ANYHIT` disables the 3 returns): floor 67.16→76.29, sphere 69.00→72.11
    (= its shadows-OFF value), Lwall 24.75→29.31; 25/57 pixels FULLY recover, clean bimodal.
  - **MECH 2 (~32/57 floor pixels): SDF-march dilated-band false crossing at d≈-0.0104.** The
    non-mip-recovered pixels are occluded by `marchBrickSdfAnyHit` (`StoredSdf.glsl:822`) itself;
    live probes show false crossings cluster at d≈-0.0104 — just 0.0004 PAST the committed
    OCCLUDE_EPS=-0.01. **THIS is why fix 3142047c was inert:** it moved the threshold to -0.01 but
    the false band samples sit at -0.0104, still "occluded." The kBand=2.0 dilation (SdfBake.h:156)
    makes each body's SDF fatter than analytic, so the shadow ray clips the band where the true
    surface doesn't. Virtual lit at every one (oracle vON≈103.5).
  - maxShadowDistance=1000 grazing hypothesis RULED OUT (occluders are bodies' own near-surface
    bands at small sHit 1.3-6.4 grid-units, not distant tangential wall clipping).
  - Instrumentation env-gated OFF, verified inert (md5-identical to baseline with env unset).
    Gates: `VIXEN_SHADOW_DBG_PX/_PY` (per-pixel `d`/occluder/leafKind), `VIXEN_SHADOW_NO_MIP_ANYHIT`
    (Mech-1 causation). 6 files touched (all #ifdef-gated), seam + primary march untouched.

- [x] Task 10.2 — Close BOTH mechanisms (shadow/any-hit-only, primary march + seam untouched):
  (1) MECH 1: shadow any-hit rays must NOT accept coarse `mipHasCoverage` as an occluder — require
  a real leaf crossing (or descend) instead of the conservative coverage-bit fallback, at the 3
  sites (`SceneBindings.glsl:~1649/~1677/~1705`); mip coverage stays correct for the primary
  visible-hit path. (2) MECH 2: `marchBrickSdfAnyHit`'s crossing test must account for the +kBand
  dilation — march to a more-negative iso (d < -(kBand·voxelSize)) or shrink/skip the exterior band
  for occlusion; NOT another 0.005 OCCLUDE_EPS nudge (fragile). GATE (oracle-based): with both
  fixes, baked per-body shadow-loss (floor 24.5%, Lwall 32%) drops toward virtual's 0% and interior
  baked/virtual ratio 0.895→~1.00; verify with the `VIXEN_SHADOW_NO_MIP_ANYHIT` A/B (Mech 1 fully
  recovers 25/57) + per-pixel `d`-dump (Mech 2 crossings no longer occlude); geometry parity
  byte-identical (0/625 same_path, cross_path ≥97%), 8 bodies, seam 1e-4, no perf regression on the
  visible-hit path. NEVER re-bless a golden toward baked output disagreeing with virtual.

- M10 Task 10.2 (fix): DONE · commit `1964c134` · Sonnet-medium impl · Opus validator APPROVED · 2026-07-18.
  BOTH mechanisms closed, scope confined to any-hit/shadow. **Fix logic (default path):** (1) MECH 1 —
  the 3 `mipHasCoverage()` any-hit early-return-occlusion sites in `SceneBindings.glsl`
  (`traverseOctreeInstancedOnceAnyHit`) now return non-occlusion (coarse coverage no longer
  false-shadows a dilated-band brick; the surviving `return true` is the genuine SDF/DDA leaf-hit);
  (2) MECH 2 — `marchBrickSdfAnyHit` OCCLUDE_EPS `-EPS`→`-kBand`(-2.0 grid-voxel, = the bake-time
  dilation width, principled not a magic nudge; no world↔grid scale needed since makeWorldSpaceEval
  pre-multiplies by subdiv). **Validator-reproduced fresh A/B (oracle-measured, every body moved TOWARD
  virtual, NO overshoot):** floor shadow-loss 24.51%→**6.24%**, Lwall 31.86%→**14.83%**, interior
  baked/virtual ratio 0.895→**0.970**, cross_path luminance delta 4.68→**1.80**. Geometry parity
  byte-identical **0/625** (run twice fresh, md5-identical), cross_path 99.8%, OOB 0/183540, 8 bodies.
  **SEAM_TIE_EPS_REL (1e-4) + all 4 usages BYTE-IDENTICAL** to `3142047c`; primary `marchBrickSdf`
  untouched; the +102/+84/+46/+10 lines in VulkanGraphApplication/BuildRenderGraph/SpatialReuseShade/
  TraceWorld all `#ifdef VIXEN_SHADOW_DBG`/getenv-gated instrumentation (verified off by default).
  Perf: esvo_traverse_shade 55.20→50.77ms (favorable/noise); no visible-hit regression (proven by
  0/625 byte-identical primary output). Commit also lands the env-gated diagnostic harness + temp_bench
  A/B .bat triad. **RESIDUAL (honest, small, NOT masked): interior ratio 0.970 not 1.00** — dominated by
  floor's remaining ~5.45-lum gap (83.41 vs virtual 88.86) + ~1.1 on box; = the flagged **THIRD distinct
  precision issue** (stored-vs-analytic divergence at a wall's own true thin edge / rightWall corner
  under grazing shadow rays: stored crossing ~0.62 into the dilated band vs analytic gap ~0.114). Not
  fixed here (out of scope); candidate follow-up KI. Env-leak trap in validator's ad-hoc driver
  (BAKED_DEMO leaking into "virtual" runs via un-scoped `call`) caught+fixed with setlocal/endlocal;
  committed `run_parity_check.bat` already scopes it correctly.

## Milestone M5b — backWall far-hit root cause → enable far-hit rejection → enforce parity (M, OPUS implementer)

Rationale: the single remaining blocker between the pipeline and lighting parity +
the enforced cross-path gate + the 16–20 FPS tier. One Sonnet round bounced off the
coordinate-frame aspect; deep ESVO frame bugs historically needed Opus.

- [x] Task 5b.1 — Root-cause + fix the ESVO interior-entry defect: `TraceWorld.glsl:
  278-294` documents it precisely (traverseOctreeInstancedOnce → rayStartWorld /
  initRayCoefficients / initTraversalState; Laine-Karras exterior-entry assumption
  breaks for interior sub-box entry → inverted/degenerate span). Fix the ray setup so
  tight bounds genuinely CONSTRAIN the march — this should kill backWall's z≈−27
  far-hit class at the source. Signature: backWall instIdx 2, OOB 51610/183540
  baseline. Epsilon tuning is proven useless (1-brick and 2-brick both fail).
  Fallback lever if the frame fix stalls: Phase-2 per-axis box bake grids.
- [x] Task 5b.2 — Enable far-hit rejection: flip `if(false&&)` →
  `if(anyHit...` at `BodyInstanceRayMarch.comp:254`. Gate: OOB fraction collapses
  (28%→near-virtual levels); ALL 8 bodies incl. backWall present.
- [x] Task 5b.3 — Lighting-parity close: cross-path A/B (target luminance p99 ≪ 38.77);
  flip `enforced:true` in `parity_thresholds.json` with a measured p99 threshold
  (~5–10 range per M5's projection); regenerate goldens under validator sign-off
  (image legitimately changes: silhouettes + lighting).
- [x] Task 5b.4 — Cleanup + parity: delete stale diagnostic comment
  `ShellOctreeGpu.h:946-948`; update the CPU mirror 1:1 if march semantics changed;
  full test sweep + documented map deltas.

**Gate:** OOB sharply down; backWall + 8 bodies present; luminance p99 ≪ 38.77 and
cross_path ENFORCED; baked visually matches virtual (the original user lighting-gap
complaint resolved); mirror/serialize/bake/sdiparity green; map deltas documented +
goldens regenerated at close.
