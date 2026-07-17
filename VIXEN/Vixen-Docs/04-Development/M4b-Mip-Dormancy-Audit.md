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
