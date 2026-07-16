---
title: Baked-Perf Fix Pipeline — Milestone-Chunked Execution Plan
status: RUNNING — M0+M1+M2+M2b DONE; M2c in flight; worktree fix/baked-perf-pipeline
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
  per-increment-worktrees lesson); merge to main only at pipeline Finish. Pre-bless the
  worktree-isolated tier at setup so workers run unattended.
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

Targets per trajectory: M1 ≈ 4 FPS → M3 ≈ 16 FPS → M5 ≈ 40–52 FPS → Phase 2 (M8) 60+.

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

- [ ] Task 2c.1 — Root-cause the shared blank-render cause (suspect per M2 worker:
  boot/residency state interaction with M0's boot-triage changes — unconfirmed).
  Additional pre-existing signal from the M2 validator: `PushConstantGathererNode::
  Validate` type-mismatch ERRORs on the direct_lighting/spatial_reuse/probe gatherers
  (non-fatal, logs-but-passes; identical in pre-epic `gate-artifacts/gate_default_log.txt`
  @ `ef385d55`) — check whether it relates. Also: the tier-crossing `setBrickLookupBase`
  fixture fix was applied+correct yet its test STILL blanks → the shared cause is
  upstream of brick-lookup addressing.
- [ ] Task 2c.2 — Fix; restore the whole consumer group to green; record the green
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

- [ ] Task 2d.1 — Parity tool `VIXEN/tools/bench/compare_parity.py` (tracked, not under
  the gitignored bench-output dirs): consumes two bench run dirs (run.log + perf.csv +
  hud_capture png); emits JSON + one PASS/FAIL line. Metrics: instIdx-map SHA + cell
  agreement % + bodies-present sets (from `[CornellDiag]` in run.log); OOB counts;
  image luminance stats (mean abs delta, p99, % pixels over threshold) between captures;
  per-pass ms deltas. Thresholds in a committed config file.
- [ ] Task 2d.2 — Golden baselines + wiring: commit the current accepted baked + virtual
  instIdx maps as goldens (text) + thresholds; `temp_bench/run_parity_check.bat` runs the
  tool post-capture. Policy encoded in the config: Tier 1 (same-path vs golden,
  tolerance ≤2/625 cells) = HARD GATE for every subsequent milestone validator;
  Tier 2 (virtual↔baked) = REPORT-ONLY until M5 closes, then enforced.

**Gate:** tool PASSes on M2b's cold/warm artifact pair (same-path, byte-identical);
tool correctly REPORTS the known virtual↔baked lighting divergence on current captures
(i.e., detects the M5 target, doesn't mask it); goldens + thresholds committed; every
later milestone's validator instruction includes running it.

## Milestone M3 — March-loop package (M) — target ~16 FPS

- [ ] Task 3.1 — Single-brick trilinear fast path: `_loadSdfTrilinearCell`-style intra-brick
  cell load (1 lookup + 8 contiguous pool loads `+{0,1,8,9,64,65,72,73}`), slow path only on
  brick-boundary cells; hoist `channelBaseFloats(SEM_SDF)` to march entry
  (`StoredSdf.glsl:160-177,:94-96,:45-50,:62-88`; audit A1 / Top #6; red branch reference).
- [ ] Task 3.2 — Analytic gradient from the hit cell's 8 already-loaded corners, replacing
  the 7-trilinear-sample `sdfGradientStored` incl. redundant d0
  (`StoredSdf.glsl:197-222,:446`; audit A2 / Top #4; red `sdfGradientStoredFromCell`).
- [ ] Task 3.3 — Hit-shading cell reuse: color + roughness resolve the corner cell once,
  not 4× (`StoredSdf.glsl:137-151,:103-123`; audit A3).
- [ ] Task 3.4 — Update the CPU mirror test in parity (`test_stored_sdf_march_mirror` —
  gpu-shader-debug mirror discipline) + A/B bench.

**Gate:** esvo GPU ms substantially down (order 3×+ on the march-dominated pass); 8 bodies;
mirror tests green; no visual regression vs pre-milestone screenshot.

## Milestone M4 — Shadow/probe economy (S–M)

- [ ] Task 4.1 — NdotL/BRDF gate BEFORE shadow traces (3-line reorder;
  `SpatialReuseShade.comp:326-334,:470-474`; `ProbeUpdate.comp:236-241` is the idiom;
  audit C3 / Top #3).
- [ ] Task 4.2 — True any-hit shadow/probe march variant: no gradient/color/roughness
  payload, instance reject at entry-t > tmax, span clamped at light distance — a separate
  function, not a runtime flag (`TraceWorld.glsl:393-508,:495-502`,
  `SceneBindings.glsl:513-528`; audit C1/C2 / Top #7).
- [ ] Task 4.3 — Generic CPU-side no-op dispatch guard (audit C8 + inventory #1/#2):
  a config-driven enable early-return in `ComputeStageNode::ExecuteImpl` — skips
  `direct_lighting` when `reservoirEnabled=0` (today a dead full-screen dispatch +
  submit every frame on ALL paths) and `probe_update` when `probeGridEnabled=0`
  (default boot). Cornell force-enables the probe grid — it must keep running there.
- [ ] Task 4.4 — Converged-probe sleep (audit C4) — **PROMOTED from stretch by M0's
  attribution: probe_update = 431.7 ms is the LARGEST baked pass.** Per-probe blend-delta
  sleep (skip probes whose last update changed below epsilon) + wake on scene-dirty;
  `ProbeUpdate.comp:406,:241`, `ProbeGridConfigNode.cpp:58-72`.
- [ ] Task 4.5 — A/B with M0's per-pass timers: spatial_reuse + probe_update ms deltas.

**Gate:** lighting-pass GPU ms down; shadowed image matches pre-milestone reference
(soft-compare screenshot); 8 bodies.

## Milestone M4b — Wake the dormant Sparse-Mip path for secondary rays (M)

Rationale (user-directed 2026-07-16): the Sparse-Mip LOD infra (mip pools via
`ConcatenateSdfWithMips`/`BakeMipPool`, marching via `MipFallback.glsl`) was built for
exactly this and is dormant on default paths (audit pattern R5). Post-M1 attribution:
probe_update 140.6 + spatial_reuse 74.1 ms = ⅔ of the frame is secondary rays that
don't need brick-resolution distances. Sequenced AFTER the baked-lighting-gap diagnosis
so mip A/Bs aren't confounded by a pre-existing shading defect.

- [ ] Task 4b.1 — Dormancy audit: confirm mip pools are baked+uploaded on the Cornell
  baked path, `MipFallback.glsl` still compiles against current bindings, and M1's
  `brickLookupBase` stamping covers the mip lookup tables (`ConcatenateSdfWithMips` was
  stamped in M1 — verify the mip-march read side).
- [ ] Task 4b.2 — Per-ray-type LOD policy: probe rays (`ProbeUpdate.comp`) and shadow
  rays (M4's any-hit variant) march coarse mip level(s) via the MipFallback path;
  primary rays unchanged at full res.
- [ ] Task 4b.3 — A/B with per-pass timers: probe_update + spatial_reuse deltas;
  correctness: primary-ray instIdx map unchanged; image soft-compare vs full-res
  reference — watch for light leaks / over-darkening in the closed box.
- [ ] Task 4b.4 (stretch, knob-gated OFF by default) — Footprint-driven mip selection
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

- [ ] Task 5.1 — Record allocated-brick min/max AABB in `SerializeSdf`'s existing loop →
  `traceBoundsMin/Max` `OctreeConfig` fields (codegen path; backward-safe zero default)
  (`SdfBake.h:399-406`, `ShellOctreeGpu.h:791-804`; audit B3 / Top #8; red branch
  reference). In the same schema change, REPLACE the dead `gridMin/gridMax` fields
  (uploaded but read by no dispatched shader — inventory #10): net-zero schema growth,
  removes dead weight.
- [ ] Task 5.2 — Shader-side: AABB cull + front-to-back entry reject using the bounds in
  `TraceWorld.glsl:234,:266-279` (+ shadow variant from M4).
- [ ] Task 5.3 — Instance sort key: cube center (or bounds center), not min-corner
  (`InstanceSort.h:26-34`; audit B3 corollary).
- [ ] Task 5.4 — A/B bench + hole-hunt: grazing angles screenshot sweep. **Includes the
  user-reported wall-thickness-step repro (2026-07-16, screenshot at
  `temp_bench/reference/wall-thickness-steps-2026-07-16.png` in the worktree): silhouette
  steps at the LEFT-WALL TOP and the FLOOR FRONT-RIGHT EDGE — both grazing regions.
  Ranked suspects: (1) far-hit silhouette extrusion (fixed by 5.5 — verify these two
  spots specifically), (2) brick/band allocation quantization at 2-world-unit brick
  granularity (sentinel face probe-crawl), (3) box-tight bake-region boundary snapping
  (`cd9e1362` pow2 round-up) crossing the wall. Gate: both marked steps gone or
  root-caused with evidence.**
- [ ] Task 5.5 — Far-hit rejection (lighting-parity interim fix): using 5.1's tight
  trace bounds, discard/clamp hits whose reconstructed `worldPos` falls outside the
  body's bounds+ε before the HitRecord write (`BodyInstanceRayMarch.comp:246` area) —
  stops corrupted `worldPos` from poisoning `gatherIndirectDiffuse`
  (`SpatialReuseShade.comp:498`, edge-clamped probe cells at `:265`) and the cross-region
  color bleed. Expect the OOB fraction (~27.5%) to collapse.
- [ ] Task 5.6 — Small-body bake resolution bump (normals-parity interim fix): the
  subdiv=1 bodies (light/sphere/box) bake at `kSmallN=16` ≈ 1 voxel/world-unit — the
  coarsest normals in the scene, why they render near-black. Raise their bake resolution
  (e.g. 16→32 or subdiv 1→2; small absolute memory/bake cost) and A/B the sphere/box
  luminance vs the virtual capture.

**Gate:** wall ms down; no new holes vs reference captures; 8 bodies; OOB fraction
sharply down (5.5); sphere/box visibly lit, luminance qualitatively closer to the
virtual capture (5.6).

## Milestone M6 — Sync/overlap package (M) — only what M0 attribution justifies

- [ ] Task 6.1 — Decouple the march submit from the WSI acquire semaphore (first real
  swapchain writer waits instead; verify cross-frame HitRecord hazard)
  (`ComputeDispatchNode.cpp:288-293`; audit E2).
- [ ] Task 6.2 — Blit exit barrier: return render target TRANSFER_SRC→GENERAL at frame
  edge (spec-violation fix; red `layout_sync` was best-wall) (`SwapchainBarriers.h:184,
  :100-103` vs `ComputeStageNode.cpp:348`; audit E3).
- [ ] Task 6.3 — Fix the orphaned per-image semaphore re-signal in composite mode
  (`BlitNode.cpp:172-177`; audit E4) + compute-scoped stage masks replacing
  ALL_COMMANDS signals (audit pattern R7).
- [ ] Task 6.4 — A/B with validation layers ON (sync changes never ship unvalidated).
- [ ] Task 6.5 — Expose the widescreen render-scale dial (inventory #12): document
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
incremented on Store (size eviction can never fire).

## Milestone M6b — Hybrid-frame bench: mixed providers + delta modification (S–M)

Rationale: the end-state frame composes procedural baseline + baked voxel deltas;
nothing currently benches or verifies that composition. User-defined acceptance test
(2026-07-16): virtual scene + a visible modification (hole/feature) on one element
delivered via the stored/delta path.

- [ ] Task 6b.1 — v1 "hole in the wall" (today's machinery): virtual Cornell with ONE
  body flipped to PROVIDER_STORED whose bake is the MODIFIED shape (e.g. right wall
  recipe minus a cylinder — `BakeRecipeInstructionsToSdfWorld` bakes arbitrary recipe
  instructions, zero new engine work). New `temp_bench/run_hybrid.bat`.
- [ ] Task 6b.2 — Mixed-provider splits: walls stored + objects procedural, and the
  inverse (`temp_bench/run_mixed.bat`).
- [ ] Task 6b.3 — v2 (if voxel-authoring path is ready per the dormant-work inventory):
  the same hole applied as a RUNTIME voxel edit to the resident stored body, no rebake.

**Gates:** hole/feature visibly correct (light passes through it: shadows + GI respond);
instIdx map correct for all 8 bodies in every variant; per-pass timers within the
sum-of-parts envelope; no provider-boundary artifacts where lighting/shadow rays cross
provider kinds. These variants become STANDING gates for M7/M8 and regression tests for
the delta program (v3, same-body delta-over-procedural, lives there — out of scope here).

## Milestone M7 — Boot parallel bake + serialize bulk path (M)

- [ ] Task 7.1 — Parallelize the 8 independent per-body bakes (`std::async` per body;
  Gaia cross-world thread-safety smoke test FIRST — audit F1 + uncertain-11).
- [ ] Task 7.2 — SerializeSdf bulk entity path (`getBrickEntitiesInto`/`getEntityFast`;
  audit F4) + skip double serialize/mip-bake (F5).
- [ ] Task 7.3 — ~~Wire shader cache~~ MOVED to M2b (pulled forward 2026-07-16).
- [ ] Task 7.4 — Bake-artifact disk cache, design-first (inventory #7 clarification:
  NO bake cache exists anywhere — the 87–190 s bake re-runs every boot; JIT Inc1's
  content-hash cache is write-only recipe-bytecode dedup, unrelated). Key on
  recipe+params+resolution → warm boots at file-load speed. Effort L.

**Gate:** boot < 60 s; serialize output byte-identical (hash compare); tests green.

## Milestone M8 — 3D-texture brick pool prototype + relaxed stepping (L) — the 60+ lever

- [ ] Task 8.1 — Phase-1 prototype: dense per-octree R16F 3D texture for the 5 walls,
  march via `textureLod` hardware trilinear; A/B vs M3's SSBO fast path (audit A6 /
  pattern R1; settles uncertain-6).
- [ ] Task 8.2 — Over-relaxed sphere tracing (ω≈1.4–1.6 + unbounding-sphere overlap
  test) in the march loop (pattern R2).
- [ ] Task 8.3 — Decision doc: phase-2 sparse atlas + aprons go/no-go from measured delta.

**Gate:** measured esvo delta recorded; correctness rig; decision documented.

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
