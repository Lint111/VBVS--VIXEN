# M4b — Sparse-Mip Dormancy Audit (Baked-Perf Pipeline)

Date: 2026-07-17. Scope: confirm why the shipped Sparse-Mip ESVO LOD path
(mip pools + `MipFallback.glsl`) never engages on the baked Cornell demo, and
whether it is safe to wake for secondary rays only. All line numbers are
`fix/baked-perf-pipeline` HEAD `f23a8d01` in this worktree.

## 1. Mip pools ARE baked + uploaded on the Cornell baked path

`BuildRenderGraph.cpp:3394-3395` (VIXEN_DDGI_CORNELL_BAKED_DEMO block) calls
`Vixen::SVO::BakeAndAttachMipPool` for the light body and
`Vixen::SVO::ConcatenateSdfWithMips(octreesForCat)` at line 3439 for all 8
bodies — the same concatenation entry point `MipBake.h` uses to compute both
`mipPoolBase` (line 378) and `brickLookupBase` (line 380) as exact prefix
sums over each octree's own node/channel/brick counts, in the SAME function.
Mip data is genuinely resident in the GPU buffer this frame; this is not a
"pool exists but empty" situation.

## 2. Two independent mechanisms suppress mip-fallback for ALL rays today

**(a) Forced eager residency.** `BuildRenderGraph.cpp:3489`
`bodyScene->RequestBrickResidency(true)` sets `brickResident=1` on every
octree config for this demo (comment at 3468-3488 explains why: lazy/mip-only
residency was causing incoherent primary hits, root-caused and fixed
2026-07-15). This makes every `octreeConfig.brickResident == 0u` gate
(`SceneBindings.glsl:1008`, `1546`) permanently false — the "streaming grace"
mip-fallback trigger never fires for ANY ray type, primary or secondary.

**(b) LOD screen-space gate uses a primary-ray-calibrated coefficient.**
The OTHER mip trigger — `pc.raySizeCoef > 0.0 && footprint >= scale_exp2`
(`SceneBindings.glsl:1071-1072` for shading rays, `1573-1574` for any-hit
shadow rays) — is NOT dead code and NOT unconnected for shadow rays: I
verified `directLightingPushConstantGatherer` (line 4561) and
`spatialReusePushConstantGatherer` (line 5170) both already wire field 8
(`raySizeCoef`) to the SAME live `raySizeCoefNode` primary rays use
(`2*tan((fovRad/height)*0.5)`, ~5.6e-4 at 45°/1440p). Since that coefficient
is calibrated for "when does a NODE's screen footprint fall below 1 pixel
from the CAMERA," and Cornell is a room-scale, single-tier scene (no
tier-crossing, `childScale==1.0` boundary per
`tiered-esvo-observer-address-direction`), no node's footprint from the
camera ever goes sub-pixel before the leaf level is reached — so this gate
is armed but structurally never trips, for primary OR secondary rays. This
is the real dormancy mechanism for shadow rays: not "disconnected," but
"using a per-pixel error threshold that is correct for primary rays and far
too strict for shadow/probe rays, which don't need pixel-accurate geometry."

**`probeUpdatePushConstantGatherer` (line 595) never wires fields 8/9 at
all** — only `instanceCount` (field 10, line 5501) is connected. Verified
`PushConstantGathererNode::PackPushConstantData`
(`PushConstantGathererNode.cpp:414-415`) zero-fills the whole buffer before
packing connected fields, so this is safe-by-default (raySizeCoef=0 →
`pc.raySizeCoef > 0.0` gate short-circuits false, LOD off) rather than a
garbage-read landmine — but it does mean probe rays run 100% full-detail
today, with no coarse path reachable at all.

## 3. `mipHasCoverage` (M4 Task 4.2) is exactly the hook Task 4b.2 needs

`MipFallback.glsl:84-87` already provides the any-hit occlusion-only mip
read (coverage bit only, no color fetch), consumed at both shadow-ray mip
sites (`SceneBindings.glsl:1551`, `1576`). No new shader function is needed
for shadow-ray coarse marching — only a coarser `raySizeCoef` fed into the
existing gates.

## 4. `brickLookupBase` (M1) exact-prefix addressing covers the MIP read side — CONFIRMED SAFE

`MipBake.h:350-380`: `setMipPoolBase`/`setBrickLookupBase` are computed by
the identical accumulation pattern in the identical function
(`ConcatenateSdfWithMips`), and `mipPoolBase +=
s.nodeCount * s.channelCount` (line 378) / `brickLookupBase += ...` (line
380) run back-to-back per body. The M1 addressing bug (Task 1.1, landed
2026-07-15, referenced at `BuildRenderGraph.cpp:3243-3245`: "bodies 5/6/7
vanish from the instIdx map") was in this exact function and is already
fixed+validated — `readMipSample`'s `octreeConfig.mipPoolBase` base
(`MipFallback.glsl:52`) is written by the same corrected code. No separate
addressing bug found for the mip-pool read side.

## 5. M5 (`traceBoundsMin/Max`) and M5b (non-unit `instDir` fix) compose safely with the mip path

Both fixes sit strictly UPSTREAM of `traverseOctreeInstanced`/
`traverseOctreeInstancedAnyHit` (`SceneBindings.glsl`), where all
mip-fallback logic lives:
- M5's tight-bounds cull (`TraceWorld.glsl:307-309`, `551-553`) is a
  per-instance AABB pre-filter in the instance-iteration loop, entirely
  before octree descent begins. A ray that survives the cull enters
  traversal exactly as before; mip-fallback logic is untouched.
- M5b's `instDir` normalization fix happens during ESVO ray setup, before
  `traverseOctreeInstanced` is called — the mip code always operates on an
  already-corrected unit-length local ray direction.

No interaction hazard found; both audits are clear.

## Conclusion / plan for Task 4b.2

Wake the dormant path for secondary rays ONLY, without touching shared
shader code, by exploiting the fact that DirectLighting/SpatialReuse/
ProbeUpdate already have (or can be given) their OWN push-constant
gatherers, independent from the primary raymarch gatherer:

- `directLightingPushConstantGatherer` and `spatialReusePushConstantGatherer`
  currently mirror the primary `raySizeCoefNode` value 1:1 (lines 4561,
  4972, 5170). Re-point their field-8 connection at a NEW, coarser
  shadow-specific coefficient (a small multiplier over the primary value, or
  a second `RaySizeCoefNode`/`ConstantNode`) so shadow rays hit the
  screen-space LOD gate (`SceneBindings.glsl:1573`) well before primary rays
  would.
- `probeUpdatePushConstantGatherer` needs NEW connections for fields 8/9
  (currently absent) using the same or a probe-specific coarse coefficient,
  so `ProbeUpdate.comp`'s own `TraceWorldShadow` call
  (`ProbeUpdate.comp:241`) gets a nonzero `pc.raySizeCoef`.
- Primary gatherer (`pushConstantGatherer`, line 4561's `if` branch already
  distinct from the other two) is NOT touched — same_path parity is
  structurally guaranteed by construction, not just by convention.
- `RequestBrickResidency(true)` (line 3489) stays as-is — the "streaming
  grace" `brickResident==0u` gate is out of scope for M4b (that's a
  residency-management change, not an LOD-policy change); only the
  screen-space coefficient gate is being exercised.

## Task 4b.2 implementation notes

Implemented exactly as planned above: a new `secondaryRaySizeCoefConstant`
`ConstantNode` (default `0.05`, ~90x the primary ray's live coefficient at
45deg/1440p; tunable via `VIXEN_SECONDARY_RAY_SIZE_COEF`), wired into field 8
of `directLightingPushConstantGatherer`, `spatialReusePushConstantGatherer`
(replacing their prior `raySizeCoefNode` connection), and newly into
`probeUpdatePushConstantGatherer` (fields 8 AND 9, both previously
unconnected). The tier-crossing debug override
(`VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE`) still takes precedence over the
new constant when active, same as it did over `raySizeCoefNode` before —
unchanged precedence, just a different non-override source. The primary
gatherer's connection (`pushConstantGatherer` -> `raySizeCoefNode`) is
byte-for-byte unmodified.

Verified `PushConstantGathererNode::PackPushConstantData` (line 436) treats
an out-of-range field index as a logged warning + skip, not an error/crash —
so wiring fields 8/9 into `probeUpdatePushConstantGatherer` is safe even if
`ProbeUpdate.comp`'s reflected push-constant struct had turned out not to
include them (it does: `TraceWorldShadow` is a real, unconditionally-reached
call in that shader, so glslang keeps `raySizeCoef`/`raySizeBias` live
through dead-code elimination).

## Task 4b.3 — A/B results (fix/baked-perf-pipeline, this worktree)

Two independent fresh `run_parity_check.bat` passes (each internally
re-running both `run_baked.bat` and `run_virtual.bat`, warm per the
project's warm-run convention — a prior `run_baked.bat` launch was done
first and discarded). Frames 31-160 excluding 150/151, per
`compare_parity.py`'s own convention.

| metric (ms) | pre-M4b baseline | M4b run 1 | M4b run 2 |
|---|---|---|---|
| `probe_update_ms` | 65.03 | 31.01 | 28.85 |
| `spatial_reuse_ms` | 8.01 | 3.71 | 3.90 |
| `whole_frame_gpu_span_ms` | 135.11 | 103.42 | 101.31 |
| `esvo_traverse_shade_ms` (primary, unchanged code) | 61.97 | 68.58 | 68.42 |
| `direct_lighting_ms` | ~0.0002 | ~0.0002 | ~0.0002 |

`probe_update_ms` and `spatial_reuse_ms` both dropped by roughly half,
reproducibly across two independent runs — the two ray types M4b's mip
policy targets. `esvo_traverse_shade_ms` (primary ray march, byte-identical
code path) floats within normal cross-session variance (per
`baked-sdf-perf-rootfix-validation` memory note: absolutes float,
ratios/deltas are what's meaningful) — consistent with same_path staying
hash-equal. `direct_lighting_ms` stayed at its pre-existing near-zero value
(that pass's own dispatch is already gated by M4 Task 4.3's no-op guard in
this scene, per prior milestones — its shadow rays run inside
`SpatialReuseShade.comp`, not `DirectLighting.comp`, which is why that
pass's OWN ms figure is unaffected here).

**Parity**: both runs — `same_path`: `hash_equal=True`, `cells_differing=0/625`,
`oob_ok=True`, all 8 bodies present. `cross_path` (ENFORCED): PASS both
runs, `cell_agreement=99.0%` (unchanged from the pre-M4b 99.04%),
`mean_abs_luminance_delta` 4.70/4.71 (pre-M4b baseline: 4.79 -- within
noise), `p99_luminance_delta` 40.77/40.41 (pre-M4b: 40.0 -- within noise).
No regression against either fence.

**Leak/over-darkening check**: visually compared `temp_bench/baked/
hud_capture_150.png` against `temp_bench/virtual/hud_capture_150.png`
(ground truth). Cornell box geometry fully coherent — red/green side walls,
back wall, floor, ceiling, ceiling light, sphere, and box all present and
correctly shaded; no visible light leaks or over-darkening in the closed
box from the coarser shadow/probe mip level. Sphere/box shading-crispness
differences between baked and virtual are the pre-existing, already-fenced
baked-vs-virtual lighting divergence (same magnitude as before M4b), not a
new artifact.

**8 bodies / OOB**: confirmed both runs — `run_oob: "0/183540"`,
`run_bodies_present: ["0".."7"]`.

**Tests**: `BuildRenderGraph.cpp` compiles only into the application targets
(`VixenApp`/`VIXEN` in `application/main/CMakeLists.txt`) — confirmed via
`grep` that no RenderGraph test executable's `CMakeLists.txt` compiles this
file (the one hit in `libraries/RenderGraph/tests/CMakeLists.txt` is a
comment, not a source-list entry). No test rebuild/rerun required for this
change; existing test state (including the KI'd reds) is unaffected by
construction.

**Gate verdict**: probe_update + spatial_reuse GPU ms measurably and
reproducibly down; same_path hash-equal; cross_path ENFORCED PASS; no
visible leaks/over-darkening; 8 bodies; OOB=0. Task 4b.3 PASSES.

## Task 4b.4 (stretch) — deferred, not attempted

Footprint-driven mip selection for PRIMARY rays via the existing
`RaySizeCoefNode`/`raySizeCoef` term is the SAME mechanism already live for
primary rays today (`SceneBindings.glsl:1071-1072`,
`OctreeTraversal-ESVO.glsl:213`) — calibrated for genuine sub-pixel screen-
space error, which is the architecturally correct threshold for primary
rays and is why it structurally never trips in this room-scale, single-tier
Cornell scene (matches the plan's own expectation: "Expected small in
room-scale Cornell; the payoff is tiered scenes"). Making primary rays use
a coarser threshold would require either a second, primary-specific
coefficient (risking the same_path hash-equal invariant the instant it's
non-default) or genuine level-blending to avoid visible popping at the
transition distance — the plan's own stretch-task language calls for
exactly this to be "DOCUMENT and defer" rather than half-landed. Deferred
to a future increment (tiered/orbit-scale scenes, where the payoff is
actually large per the plan, are the natural place to pick this back up)
with an OFF-by-default knob design, not attempted in this milestone.
