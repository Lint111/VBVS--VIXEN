# Sampled Lighting Inc4 — DDGI Probe Volume — Plan (2026-07)

**Spec:** `Sampled-Lighting-Design-2026-07.md` §4 (Inc4 line), §5 (engine integration: probe-update as the first genuine parallel sibling pass), §6 open-decision #2 (probe placement), §7 (forward hooks — camera-importance biasing of probe-ray budgets, noted not scheduled).

**Base:** main `a68ca3f9` (Inc0-3 all merged + pushed; Inc3's ReSTIR DI is the direct-lighting term this probe volume complements as the indirect term).

**Scope:** a uniform-grid DDGI (Dynamic Diffuse Global Illumination) probe volume over the scene bounds — probe-update compute pass casts rays through the existing `TraceWorld` traversal, accumulates irradiance + Chebyshev visibility per probe with hysteresis, and the main shade pass gathers probe irradiance for indirect diffuse lighting. This is the FIRST genuinely parallel sibling pass in the pipeline (writes disjoint probe-atlas outputs, never consumes the direct/ReSTIR term — auto-sync should bake it with no barrier against the direct pass). Probe placement: uniform grid first (octree-anchored/cascaded explicitly deferred per the design's own open-decision #2). Denoiser and specular reflections are OUT of scope (Inc5).

## Grounding from codebase investigation (2026-07-12) — two real prerequisites, not just design-doc abstractions

**Prereq A — TraceWorld's include-chain is not yet reusable outside `BodyInstanceRayMarch.comp`.** `TraceWorld(origin, dir, tmin, tmax, out WorldHit)` / `TraceWorldShadow(...)` (`shaders/TraceWorld.glsl:53,286`) already accept arbitrary world-space origin/direction — NOT hardcoded to camera rays, confirmed callable for probe rays as-is. But `TraceWorld.glsl` must be `#include`d after a specific chain of prior includes (BodyInstance struct, `bodyInstances` SSBO, `configs[]`, `traceProceduralBody`, PROVIDER_* defines — `TraceWorld.glsl:12-20`) that today only exists inside `BodyInstanceRayMarch.comp`'s own call site. A probe-update pass, as a genuinely NEW standalone `.comp` shader (not a variant of the march), needs this scene-binding include set extracted into something reusable — this is the direct analog of Inc1's KI-018 (DirectLighting pass-split) and Inc3's KI-023 (geometric reject): a structural prerequisite the increment's own risk profile depends on, done first, isolated on its own gate.

**Prereq B (open architecture question, not yet a blocker) — image-array-hazard support may be needed.** Confirmed: `IMAGE_WRITE` (`ComputeStageNodeConfig.h:84-107`) is a SINGLE slot by deliberate M1 scope decision ("every image-producing pass this codebase has needed so far writes exactly ONE non-swapchain image... generalize IMAGE_WRITE THEN, with its own concrete use case driving the design"). M5's array-hazard surgery (`Resource::hazardConstituents_`, `BufferSyncGathererNode`) is explicitly BUFFER-only by its own documented scope. Standard DDGI needs at minimum an irradiance atlas + a Chebyshev visibility (depth²/depth) atlas as two SEPARATE images updated by the same pass — if M1's single-slot precedent doesn't suffice, this is genuinely new RenderGraph library work, following the exact "generalize when a concrete use case arises" pattern M5 already established for buffers. **Gate this explicitly at M1** rather than discovering it mid-shader-work (the plan's own M1 task below covers it) — do not assume irradiance+visibility can be packed into one image without checking whether that's actually a good idea for probe-atlas layout first (packing IS common in shipped DDGI implementations — R11G11B10 or RGBA16F octahedral atlases sometimes co-pack, but visibility typically wants its own channel/format for the depth²/depth moments — verify against the light-tree's own established "measure vs an external reference, don't assume" discipline).

**Good news, confirmed with zero new plumbing needed:**
- The mip-cut light-tree (`LightTree.h`, `LightTreeBufferNode`, `shaders/Generated/LightTreeBuffer.glsl`) is ALREADY a generic GPU-resident SSBO any compute shader can bind — Inc3's `DirectLighting.comp` already does. A probe-update pass can bind the SAME SSBO for read access with no new plumbing, exactly the design doc's "feeds ReSTIR + feeds DDGI" claim, now grounded. Caveat: `worldPos` in the buffer is grid-local, not world-space — any NEW caller (including probe-update) must do the same per-instance world-transform `BuildRenderGraph.cpp`'s existing `VIXEN_RESTIR_GATE_DEMO` block already does (not automatic, but a known, copyable pattern).
- `[GpuStruct]` config-codegen (schema→C++/GLSL→node→binding→drift-guard-test) is proven and directly reusable — `ReservoirConfig`'s 8-file pipeline (`codegen/config-schemas/ReservoirConfig.cs` → `.g.h` → `.glsl` → `ReservoirConfigNode` → drift-guard test) is the exact template for a new `ProbeGridConfig`.
- Gate/demo-scene infrastructure has a direct precedent: `VIXEN_RESTIR_GATE_DEMO` (`BuildRenderGraph.cpp:1923-2044`) bakes a scene, wires it live, and a per-tick readback hook (`VulkanGraphApplication.cpp:500-544`) does numeric verification at a fixed frame count chosen to exceed convergence — the design's own DDGI gate requirement ("leak scenes (thin walls), edit-loop... frame budget held") follows the identical shape: a new env var, a new scene-baking block (this time WITH occluder/thin-wall geometry for the leak test), a new readback hook.
- KI-019 (`GPUQueryManager` isolated-dispatch timing) is confirmed STILL OPEN as of Inc3 M7 — Inc4's own cost measurement will use the same CPU `FrameTimer` A/B substitute (env-var on/off lever, full-frame wall-clock delta) as Inc1-3, not a new method.
- Zero exploratory DDGI/probe/Chebyshev/octahedral code exists anywhere in the tree (confirmed via broad grep) — Inc4 starts from zero, nothing to reconcile or avoid duplicating.

## Milestone Map

| Milestone | Tasks | Model | Where |
|---|---|---|---|
| M1 | **[PREREQ]** Extract TraceWorld's scene-binding include chain into a reusable form; resolve the image-atlas-slot question (single packed atlas vs image-array-hazard generalization) — DECIDE + implement whichever the investigation supports, gate byte-identical | Sonnet impl / Opus validate | worktree |
| M2 | `ProbeGridConfig` [GpuStruct] (grid origin/spacing/counts, ray-per-probe budget, hysteresis rate, enable flag) + `ProbeAtlasNode`(s) (persistent images, mirrors `AccumulationHistoryNode`'s survive-across-frames pattern) | Sonnet impl / Opus validate | worktree |
| M3 | Probe-update compute pass: cast `raysPerProbe` rays per probe through `TraceWorld` + the light-tree SSBO for direct-lit ray radiance, accumulate into the irradiance atlas with hysteresis blend | Sonnet impl / Opus validate | worktree |
| M4 | Chebyshev visibility (depth²/depth moments) atlas + leak-test gate scene (thin-wall occluders) — validate NO light leaking through geometry before wiring probes into the shade pass | Sonnet impl / Opus validate | worktree |
| M5 | Wire probe irradiance gather into the main shade pass (indirect diffuse term, added to ReSTIR's direct term) — confirm the fan-out/fan-in auto-sync shape (§5): probe-update and direct/ReSTIR passes both read HitRecord/scene, write disjoint outputs, no barrier between them | Sonnet impl / Opus validate | worktree |
| M6 | Edit-loop responsiveness gate (delta content fades in within the hysteresis window) + probe-ray-budget real-GPU bench (the design's own flagged pass-2 open question — bench BEFORE finalizing grid density) + full live gate | Sonnet impl / Opus validate | worktree |
| M7 | Measurement (CPU FrameTimer A/B, same KI-019 substitute) + docs close-out + final whole-diff review | Sonnet impl / Opus validate | worktree |

Prereq (M1) first, isolated on its own byte-identical gate — same discipline as Inc1/Inc3's own prereq milestones. M3/M4 are the highest-risk milestones (probe-ray tracing correctness, leak prevention) — live-gated. M5 is the fan-out/fan-in integration proof point the design doc specifically calls out as architecturally significant for this pipeline. M6's real-GPU probe-budget bench directly answers the design's own explicitly-flagged "pass-2 open question" before the grid is finalized — do not skip or guess at grid density first.

## Tasks

### Task 1 (M1) — [PREREQ] Reusable TraceWorld include chain + image-atlas slot decision
- Extract the scene-binding include chain currently only assembled inside `BodyInstanceRayMarch.comp`'s call site (BodyInstance struct, `bodyInstances` SSBO, `configs[]`, `traceProceduralBody`, PROVIDER_* defines — see `TraceWorld.glsl:12-20` for the exact list) into a form a genuinely NEW standalone `.comp` shader (the future probe-update pass) can also pull in. Follow the Inc1 M1 precedent for how `TraceWorld`/`SceneBindings.glsl` were themselves extracted from the monolithic march — mirror that shape, don't invent a new one.
- Resolve the image-atlas question from the grounding section above: investigate whether DDGI's irradiance + Chebyshev-visibility atlases can reasonably co-pack into ONE image (checking real DDGI implementation precedent, not just assuming) or genuinely need two separate `IMAGE_WRITE`-shaped slots simultaneously. If two are needed and M1's single-slot precedent doesn't suffice, generalize following M5's exact "concrete use case drives the design" pattern (the array-hazard buffer generalization is the template, adapted for images) — this is real RenderGraph library surgery if needed, treat it with the same rigor/regression-gating M5 applied to its own buffer generalization (do not break the shipped single-IMAGE_WRITE users: DirectLighting/SpatialReuseShade from Inc3).
- **Gate:** byte-identical to the current baseline `fde9c268…` (pure refactor + scaffolding, zero visual delta — no probe pass exists yet to change output). Live-gate any RenderGraph library surgery (syncval clean, no regression on Inc3's shipped IMAGE_WRITE/array-hazard users) if Prereq B required a generalization.

### Task 2 (M2) — ProbeGridConfig + persistent probe atlas node(s)
- `ProbeGridConfig` `[GpuStruct]` via the exact Inc0-3 codegen precedent (`ReservoirConfig`'s 8-file pipeline is the template): grid origin (world-space), probe spacing, grid counts (X/Y/Z), rays-per-probe budget, hysteresis rate, enable flag. Next free binding per the current live census.
- Persistent probe atlas image(s) (per M1's resolved slot shape) — `ProbeAtlasNode` mirroring `AccumulationHistoryNode`'s "persistent, survives recompile, NOT the RenderTargetNode ring" pattern (history must survive across frames for hysteresis exactly as accumulation's history does for EWMA).
- **Gate:** drift-guard test for `ProbeGridConfig` (pure C++ layout, same honest scaffolding-only scoping `ReservoirConfig` used before M4's shader consumer existed). `enabled=0` reproduces the pre-DDGI baseline byte-identically.

### Task 3 (M3) — Probe-update compute pass: ray cast + irradiance accumulate
- New standalone probe-update `.comp` shader (using M1's reusable include chain) as its own `ComputeStageNode` pass. Per probe, cast `raysPerProbe` rays (start with a fixed/deterministic direction set — e.g. spherical Fibonacci or a fixed octahedral sample pattern; a full importance-sampled scheme is NOT required for this milestone) through `TraceWorld`, sample direct-lit radiance at each hit (reuse the light-tree SSBO + `DirectLighting.comp`'s shading math where sensible — don't reimplement a second lighting model).
- Accumulate into the irradiance atlas with the hysteresis blend rate from `ProbeGridConfig`.
- **Gate:** live render — probes visibly light a simple test scene (no formal numeric gate yet, that's M4's leak-test); confirm the auto-sync hazard on the new probe-update pass's write is correctly declared (whichever slot shape M1 resolved).

### Task 4 (M4) — Chebyshev visibility atlas + leak-test gate
- Second atlas (or second channel-set of the M1-resolved slot shape): depth²/depth moments per probe-ray-direction, enabling Chebyshev's-inequality-based visibility test at gather time (standard DDGI leak mitigation).
- **Leak-test gate scene**: thin-wall occluder geometry (per the design's own explicit gate requirement "leak scenes (thin walls)") — follow the `VIXEN_RESTIR_GATE_DEMO` precedent shape (new env var, new scene-baking block in `BuildRenderGraph.cpp`, new readback hook in `VulkanGraphApplication.cpp`) but building occluder geometry instead of emissive content.
- **Gate:** live — confirm NO light leaks through the thin wall with visibility-tested gather; confirm it DOES leak (as a negative control) if the Chebyshev test is disabled, proving the test scene is actually sensitive to the mechanism being validated (an ablation gate, per the recipe-epic's "vary exactly one factor" lesson referenced in the design doc's §5 Verification bullet).

### Task 5 (M5) — Wire into the shade pass; prove the fan-out/fan-in shape
- Add probe irradiance gather (trilinear across the 8 nearest probes, Chebyshev-weighted) to the main shade pass as the indirect diffuse term, summed with Inc3's ReSTIR direct term.
- **Architecturally significant per the design doc's own §5**: confirm the probe-update pass and the direct/ReSTIR pass are genuinely declared as disjoint-output siblings (both read HitRecord/scene, neither consumes the other's output) so auto-sync bakes them with NO barrier between — this is the FIRST time this pipeline exercises that shape. Live-gate the scheduler's actual baked edges (mirroring M5-Inc3's own rigor verifying the fan-in demo's edge topology) — don't just trust it "looks disjoint," inspect the baked SyncEdges.
- **Gate:** live render — indirect term visibly adds bounce lighting; `enabled=0` (on `ProbeGridConfig`) still reproduces the pre-DDGI baseline; syncval clean, confirm zero unexpected barrier between the two sibling passes (or a barrier IS present and that's flagged as a finding needing explanation, not silently accepted).

### Task 6 (M6) — Edit-loop gate + probe-ray-budget GPU bench + full live gate
- **Edit-loop responsiveness**: modify scene content (add/remove emissive geometry) live, confirm probe irradiance converges to the new state within the hysteresis window (per the design's own explicit gate requirement) — not instantly (hysteresis is deliberate smoothing) but bounded.
- **Probe-ray-budget bench, the design's own flagged pass-2 open question**: measure real-GPU cost at varying `raysPerProbe` values BEFORE finalizing grid density/ray budget for production use — this is explicitly called out in the design doc as needing real-GPU data before sizing, not a guess.
- **Frame budget held**: confirm the probe-update pass's cost fits sanely within the 16.6ms budget alongside Inc1-3's already-measured costs (~240ns/shadow-ray + ~1.2ms accumulation + ~5.4ms ReSTIR) — order-of-magnitude, per the established KI-019-constrained measurement discipline, not a precise budget commitment.
- **Gate:** all of the above live; full-stack re-verification (byte-identity escape hatch, no new VUIDs beyond the established census, both apps boot/close clean).

### Task 7 (M7) — Measurement + docs + close-out
- CPU FrameTimer A/B (`ProbeGridConfig.enabled=1` vs `0`), same KI-019 substitute methodology as Inc1-3, run twice if variance is comparable to the delta (the now-standing Inc2 M5 lesson).
- Docs: design doc §4 Inc4 → DONE with date+SHAs+cost; §6 open-decision #2 (probe placement) updated with what was actually decided/shipped (uniform grid, confirm octree-anchored/cascaded genuinely stays deferred); §6 #4 frame-budget split gains the fourth data point. CHANGELOG. Commit this plan doc onto the branch (untracked in the main checkout until now, like Inc0-3).
- **Gate (Opus validator + final whole-diff review):** all prior gates re-run from a fresh rebuild; byte-identity independently re-derived; leak-test + edit-loop gates reproduced; the full stack live-gated.

## M1 Findings (2026-07-12, implementer pass)

**Part A — DONE, no extraction needed.** Investigation found `SceneBindings.glsl` (Sampled Lighting Inc3 M1 / KI-018) already IS the reusable form the plan called a prerequisite: it bundles the full scene-binding chain (BodyInstance struct, `bodyInstances`/`esvoNodes`/`brickData`/etc. SSBOs, `configs[]`, PROVIDER_* defines, all traversal helpers) and already `#include`s `TraceWorld.glsl` at its own tail (`SceneBindings.glsl:1288`). Three shaders already consume it identically (`BodyInstanceRayMarch.comp`, `DirectLighting.comp`, `SpatialReuseShade.comp`). Added a throwaway smoke-test consumer (`shaders/ProbeUpdateSmokeTest.comp` + `libraries/ShaderManagement/tests/test_probe_update_smoke.cpp`, mirroring `test_hlsl_ingestion.cpp`'s standalone-executable-with-no-Vulkan-device pattern) that `#include`s `SceneBindings.glsl` from a fourth, genuinely new call site and calls both `TraceWorld`/`TraceWorldShadow` with a non-camera-anchored origin/direction — compiled + ran green through the real `ShaderBundleBuilder`/glslang pipeline (with real `#include` resolution), proving reusability is not just plausible but demonstrated. M3's probe-update pass can `#include "SceneBindings.glsl"` verbatim, same as the three existing consumers.

**Part B — DONE (2026-07-12, second pass, user-authorized).** Researched RTXGI-reference DDGI atlas layout: irradiance and visibility/Chebyshev-moment data are stored as separate images with DIFFERENT per-probe texel resolutions (irradiance ~6×6 to 10×10 texels/probe incl. border; visibility ~16×16 to 18×18 texels/probe incl. border) — visibility needs finer angular sampling than irradiance because occlusion/leak-prevention accuracy is the whole mechanism DDGI's reputation risk depends on. Channel-packing into one `IMAGE_WRITE`-shaped image is not viable at any fidelity level — structural DDGI property, not a shortcut. User authorized the full generalization, mirroring M5's buffer surgery exactly.

Before writing any code, verified directly (not assumed) that `Resource::hazardConstituents_` (`CompileTimeResourceSystem.h:786-790`) and `ResourceAccessTracker::AddNode` (`ResourceAccessTracker.cpp:71-169`) are genuinely resource-type-agnostic already — zero `ResourceType`/`VkBuffer`-specific logic anywhere in the tracker or `FrameSyncScheduler` (grepped both, zero hits). This meant NO tracker/scheduler changes were needed — only new gathering (`ImageSyncGathererNode`/`ImageSyncGathererNodeConfig`, mirroring `BufferSyncGathererNode` exactly) + a new `IMAGE_WRITE_ARRAY` input slot (index 19, purely additive — `INPUTS` 19→20, existing `IMAGE_WRITE` at index 18 and every other slot untouched) + a barrier-recording loop in `ComputeStageNode::RecordComputeCommands` using the SAME per-target logic `IMAGE_WRITE` already had (`imageWriteLayouts_` was already `std::unordered_map<VkImage,...>`, already multi-entry-capable, zero type changes). Chose the additive/opt-in scoping option (leave `IMAGE_WRITE` completely alone) rather than migrating existing consumers onto the array path, per the reduced-blast-radius option M5's own worker used.

**Regression gates (real GPU, `VIXEN_VULKAN_VALIDATION=1`):** RenderGraph test suite all green (`test_buffer_sync_gatherer_node`, `test_barrier_types`, `test_frame_sync_node`+`_timeline`, `test_frame_sync_scheduler`, `test_pass_group_node_smoke`/`schedule`, `test_compute_dispatch_node`, `test_hitrecord_sdi_parity`). `VIXEN_RESTIR_GATE_DEMO` live run: steady ~100 FPS, only pre-existing documented KI-024 noise (unrelated `test_dispatch` demo pipeline — confirmed zero pixel impact, not touched by this change). `VIXEN_FANIN_DEMO` live run: ~10k frames, zero errors, zero VUIDs, stable ~1000 FPS — confirms no regression on the multi-submit fan-in sync topology this generalization sits directly alongside. Full `build.bat all`: 0 failures. Commit `4007c6ca`.

## M3 Findings (2026-07-12, implementer pass)

**DONE.** New standalone `shaders/ProbeUpdate.comp` (`ComputeStageNode` "probe_update",
commit `f9453b76`), wired but NOT added to the default-live path beyond `ProbeGridConfig`'s
own `probeGridEnabled` gate (same discipline M2 used for its own scaffolding).

**Ray sample pattern:** spherical Fibonacci lattice (Keinert et al. 2015's closed-form
mapping) — chosen over a stratified/QMC scheme because it needs no per-tick RNG state to stay
deterministic (required for hysteresis to converge against a STABLE sample set rather than
chasing fresh per-tick noise) while still being near-uniform for the realistic raysPerProbe
range (tens to low hundreds).

**Shading reuse:** does NOT pull in `DirectLighting.comp`'s RIS/reservoir machinery
(`ReservoirCombine.glsl`'s `reservoirUpdate`/`reservoirCombine`) — the plan's own Task 3 scopes
probe rays to a single deterministic sample, not spatiotemporal reuse. Instead reuses the SAME
light-tree SSBO (binding 24) + `TraceWorldShadow` `SceneBindings.glsl` gives every consumer,
via a single-candidate WRS draw (the M=1 degenerate case of `DirectLighting.comp`'s own
`reservoirBuildFromLightTree` inner loop, re-derived locally rather than instantiating a full
`ReservoirRecord` state machine for a one-shot pick). `pcgHash`/`rngNextFloat` are re-declared
locally (mirroring `ReservoirCombine.glsl`'s own copy) rather than including that file, since
none of its reservoir-update/-combine primitives are needed here.

**Atlas-write correctness (the real risk this milestone flagged):** the plan's per-(probe,ray)
naive shape would have every ray invocation racing to write the SAME probe's atlas texel block
concurrently. Resolved architecturally: ONE WORKGROUP PER PROBE (`gl_WorkGroupID.x` = probe
index, `gl_LocalInvocationID.x` = ray index, `local_size_x` = a fixed
`PROBE_UPDATE_MAX_RAYS_PER_PROBE=256` ceiling — comfortably inside the core-spec-guaranteed
1024-invocation minimum), workgroup-shared-memory tree reduction sums all raysPerProbe samples,
and ONLY invocation 0 performs the hysteresis blend + atlas write for that probe. Matches
standard DDGI's batch-then-blend update shape exactly, not an approximation. Atlas texel-block
indexing (`texelsPerProbe = imageSize(atlas).x / (countX*countY)`) is derived from
`imageSize()` at runtime rather than duplicating M2's `kProbeIrradianceTexelsPerProbe`/
`kProbeVisibilityTexelsPerProbe` constants as a second source of truth — confirmed against
`BuildRenderGraph.cpp`'s own atlas-layout comment (columns sweep X then Y, rows tile Z) rather
than re-deriving independently.

**Gate results (real GPU, Windows-native, `VIXEN_VULKAN_VALIDATION=1`):**
- `VIXEN_PROBE_GRID_CONFIG_ENABLED=1` + `VIXEN_RESTIR_GATE_DEMO=1` (emissive scene, so the
  light-tree cut is non-empty and probe rays have something to sample): 13,370 frames, steady
  ~125 FPS, clean shutdown via `taskkill`. VUID census: the same 8 pre-existing documented
  types (vkUpdateDescriptorSets-03047, vkResetFences-01123, vkQueueSubmit2-03868/03875,
  vkCmdDraw-09600, vkCmdDispatch-08114, vkBeginCommandBuffer-00049,
  vkAcquireNextImageKHR-01779, PresentInfoKHR-MissingAcquireWait) at the same
  duplicated-message-limit-of-10-per-type ceiling — zero NEW VUID types introduced by the new
  pass.
- Default (`probeGridEnabled=0`, no env vars): boots, renders, closes clean; same VUID
  baseline. `ProbeGridConfigNode`'s own `probeGridEnabled=0` escape hatch (checked first in
  `main()`) makes the new pass a no-op dispatch when disabled.
- RenderGraph auto-sync regression suite (`test_buffer_sync_gatherer_node`,
  `test_barrier_types`, `test_frame_sync_node`, `test_frame_sync_scheduler`,
  `test_pass_group_schedule`, `test_hitrecord_sdi_parity`): all green, re-run post-change.
- Re-ran one of the 7 codegen `*_check` drift-guard targets that failed mid-`build.bat all`
  (`reservoirconfig_check`) standalone and it passed clean — confirmed those 7 failures were the
  known parallel-`dotnet run`-vs-shared-`CodegenTool~`-output-directory file-lock race (multiple
  `*_check` targets building the SAME shared Yeroket tool concurrently under `ninja -k 0`'s
  full-parallelism default), not a regression from this milestone's changes — none of the 7
  failed structs (`LightingConfig`/`ReservoirConfig`/`ReservoirRecord`/`OctreeConfig`/
  `Callables`/`ViewHud`/`ViewHudMarkup`) is `ProbeGridConfig`, which this milestone did not
  touch.

**Sync-hazard verification:** structural, not scheduler-trace-instrumented (this app has no
default-enabled SyncEdge-dump logging path at runtime — that level of introspection lives in
the RenderGraph unit tests, which already cover the array-hazard-expansion mechanism this pass
relies on via `FrameSyncArrayHazard.*`). `probeUpdateGatherer`'s descriptor bindings never touch
HitRecord, the reservoir ping-pong buffers, or the render-target images — only the same
read-only scene SSBOs + light-tree cut every other pass already reads (read-after-read is not a
hazard, the established precedent throughout this program) plus its own atlas
`IMAGE_WRITE_ARRAY`. No connection exists between `probeUpdateNode` and
`directLightingNode`/`spatialReuseNode`'s sync slots, so no edge can be baked between them by
construction.

**Deviations from the plan:** did not attempt a true octahedral per-ray-direction texel mapping
within a probe's atlas block this milestone (M3 writes one averaged value uniformly across the
whole texel block per probe) — flagged in-shader as M4/M5's own scope (per-direction gather is
what the block's texel resolution anticipates, not needed for M3's "probes visibly light a
scene" gate). The visibility atlas write is explicitly basic/placeholder (depth, depth² moments,
no Chebyshev test applied yet) per the plan's own "your call, don't force it" M3 note — chose to
write it since the same ray-cast pass already has the hit distance in hand at zero extra
ray-cast cost.

## Self-Review

- **Why the TraceWorld-reusability prereq first?** Every prior increment's own prereq milestone (Inc1 KI-018, Inc3 KI-023) isolated a structural blocker on a byte-identical gate before the increment's actual novel logic — Inc4's structural blocker is that no standalone (non-march) compute shader can currently pull in the scene-binding chain `TraceWorld` needs. Isolating it first means the probe-ray-casting logic (M3) lands on a proven scaffold, not tangled with a plumbing bug.
- **Why resolve the image-atlas slot question at M1, not discover it mid-M3/M4?** M1's own IMAGE_WRITE and M5's own buffer-array-hazard generalization were both driven by hitting the wall mid-milestone and escalating (the M5 architecture saga this session). Doing the investigation upfront here, with the actual DDGI atlas-count question explicit in the plan, aims to avoid repeating that costly mid-milestone discovery pattern — though if the investigation is wrong and a real blocker still surfaces at M3/M4, escalate exactly as M5 did (stop, ask, don't guess).
- **Why is M5 (the shade-pass wiring) flagged architecturally significant?** The design doc's own §5 explicitly calls out DDGI as "the first genuine parallel sibling to the direct/shadow pass" — this is a real structural first for the pipeline's auto-sync scheduler, not just another consumer wiring. Verifying the actual baked SyncEdge topology (not just "it renders correctly") is the same rigor Inc3 M5's fan-in-demo verification applied to its own novel sync shape.
- **Why bench probe-ray budget at M6 before "finalizing" anything?** The design doc explicitly flags this as the deciding pass-2 question, not something to guess at during initial implementation — sizing the grid/ray-budget on real-GPU data (not an assumed number) avoids the same trap Inc1's 240ns/ray anchor explicitly warned against (order-of-magnitude, sparse-scene-derived, not a precise constant to build hard budgets on without re-deriving for denser content).
- **Deliberate non-scope:** octree-anchored/cascaded probe placement (uniform grid only, per design's own open-decision #2 — revisit when Tiered-ESVO's scale ambitions actually meet lighting); denoiser (Inc5); specular/VNDF reflections (Inc5); camera-importance biasing of probe-ray budgets (§7 forward hook, noted not scheduled — the naive "bias by inverse direct-shadow value" is explicitly flagged as the WRONG map in the design doc, a correct importance scheme needs its own research pass); any SPPM-style photon layer (later/optional).
- **Biggest risks:** (1) the image-atlas slot question — if co-packing genuinely doesn't work for real DDGI quality and a full image-array-hazard generalization is needed, that's real core-scheduler surgery with the same regression-gating stakes M5's buffer generalization had (all shipped IMAGE_WRITE consumers from Inc3 must remain unaffected). (2) Leak-test correctness (M4) — DDGI's whole reputation risk is light leaking through thin geometry; the ablation-gate discipline (prove the test scene is actually sensitive to the mechanism, not just "looks fine") is the mitigation. (3) The fan-out/fan-in sync shape (M5) being the pipeline's first real exercise of genuinely-parallel-sibling-passes — if the scheduler doesn't actually bake the disjoint-no-barrier shape correctly, that's a correctness (not just performance) risk worth inspecting directly, not assuming from "it compiled."

## Execution Handoff

Ready for `post-brainstorm-context-manager`: milestones sequential M1→M7, Sonnet implementers / Opus validators per milestone + final Opus whole-diff review, isolated worktree, in-tree destructive/git tier pre-blessed at setup. Real-GPU (Windows-native) gates; watch long builds/renders by polling — active foreground loops only, NEVER a passive/background wait mid-task (this instruction must be restated verbatim in every implementer/validator brief per the project-rules/post-brainstorm-context-manager skill updates made this session). Live-gate authoritative for every sync-touching milestone (M1 if RenderGraph surgery is needed, M3, M4, M5). M1's image-atlas-slot investigation may reveal the prereq is smaller or larger than scoped here — if smaller (single packed atlas suffices), fold the saved scope into more thorough M3/M4 ray-sampling-pattern work; if larger (real hazard-mechanism surgery needed), treat it with M5-Inc3's exact level of rigor and don't compress the timeline to compensate.
