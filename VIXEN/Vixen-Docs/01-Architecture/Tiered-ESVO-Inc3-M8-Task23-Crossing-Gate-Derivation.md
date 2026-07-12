---
title: Tiered ESVO Inc3 — M8 Task 23 — tier-crossing LOD gate re-derivation (camera-anchored, world-unit-correct, knob-free)
status: derivation for the Task 23 engine fix; supersedes the coefficient-override approach (Task 20/21) and the construct-at-depth approach (Task 22, proven impossible)
depends: Tiered-ESVO-Inc3-Plan-2026-07.md (M8 Task 20/22 Progress Log), Tiered-ESVO-Observer-Addressing-Design-2026-07.md §9
---

# The structural problem, restated precisely

Both LOD gates in `shaders/BodyInstanceRayMarch.comp` read the same push-constant
`pc.raySizeCoef` (correct: `2*tan(fov/height/2)`, per-world-distance cone spread,
`RaySizeCoefNode.cpp:31`). The gates themselves:

- Ordinary (non-leaf) gate: `tv_max*coef + bias >= state.scale_exp2` — Laine & Karras §4.4.
- Tier-crossing gate (pre-Task-23): `tv_max*coef + bias >= childScale*state.scale_exp2`.

Task 20 proved `tv_max` is the current node's exit-t, **floored by the node's own chord**
for any ray traversing its interior — it does not shrink with camera proximity. Task 22
proved the `2^-depth` factor cancels identically from both sides of the gate, so no
construction depth can flip it. Result: at `childScale=2^-10` the crossing gate is
**permanently declined** on any body big enough to read as a planet (267× at the M8 demo's
scale), and shrinking the shared coefficient to force it starves the ordinary gate
(mip-only body vanishes below coef≈2.5e-5 — Task 20 validator's root cause).

## Why the shared coefficient was never the defect

The pixel-footprint model is `footprint(D) = D*coef + bias`, where **D is the distance
from the camera to the thing whose resolvability is being decided**.

- For the ordinary gate, the "thing" is the node itself. Using `tv_max` (node exit-t) is
  fine there: the gate fires when `D ≈ size/coef ≈ 600×size`, so the ≤1-node-chord error
  between entry-t, exit-t, and true camera distance is a ≤0.2% relative error.
- For the crossing gate, the "thing" is the **child tree's content**, which is `childScale`×
  smaller than the hosting leaf. At `childScale=2^-10` the correct firing distance
  (`≈ cs·se/coef`) is far **smaller** than the hosting leaf's own chord — so the chord floor
  in `tv_max` dominates and the gate's LHS never reaches the firing range, at any distance,
  at any depth. **The broken quantity is the gate's distance argument, not the coefficient.**

A second push-constant coefficient can never fix this: the needed threshold varies per-leaf
and per-ray (chord vs. proximity), not per-frame. The fix is to evaluate the ONE principled
coefficient at the RIGHT distance.

# Units audit (all verified against shipped code, this worktree)

1. `t` inside `traverseOctreeInstancedOnce` is parametric in `rayDirLocal`
   (`hitPos12 = coef.normOrigin + rayDirLocal*state.t_min`, :567/:892;
   `initRayCoefficients` biases are built from `d = mat3(worldToLocal)*rayDir`).
2. `t = 0` at `rayStartWorld` — the **cube entry point**, not the camera
   (`initRayCoefficients(rayDir, rayStartWorld)`, :736). `tEntryWorld` (camera→cube-entry
   arc, :733) is added to outputs only (`hitT = tEntryWorld + state.t_min`), never to gate
   inputs. So the shipped gates are entry-anchored — camera-distance-independent whenever
   the camera is outside the octree cube. (This is why every recorded "handoff distance"
   in M6/M7 was actually a projected-size vanishing point, not a gate flip: for cs=0.25
   scenes the crossing gate never declined at any tested distance, and for cs=2^-10 it
   always declined. The gate has never yet functioned as a distance-driven handoff live.)
3. At hop 0, `t` is in **true world units**: TraceWorld.glsl's own invScale comment block
   proves `t` computed with `instDir = rayDir/renderScale` against a `localToWorld` that
   bakes `kWorldGridSize` comes out in world-distance units, and the traversal adds
   `tEntryWorld` (a genuine world arc) to it.
4. One unit of the CURRENT tree's local [1,2) frame spans `1/length(rayDirLocal)` t-units
   along the ray (position advances by `rayDirLocal` per unit t — definitional).
5. Across a crossing, the remap contract (`childLocal = (parentLocal−childOrigin)/childScale
   + 1.5`) means one child-local unit physically spans `childScale`× one parent-local unit,
   **independent of the child octree's own `cfg.localToWorld`** (which the wrapper only uses
   for a round-trip that cancels: `worldToLocal·localToWorld ≡ I`).
6. From (4)+(5): threading `tLocalUnitWorld` (this tree's local unit in true-world units;
   hop 0 init `1/length(rayDirLocal)`; per hop `*= childScale`) gives, at every hop:
   `kPhys = length(rayDirLocal)*tLocalUnitWorld` = true-world distance per t-unit — and
   `kPhys ≡ 1` at hop 0 by construction, for any instance/renderScale convention.

# The corrected gate (Task 23)

At the crossing decision site, with `ref = tierRefTable[absoluteTierRefIdx]`:

```
tChild          = clamp( dot(childOriginLocal − coef.normOrigin, rayDirLocal)
                          / dot(rayDirLocal, rayDirLocal),  state.t_min, tv_max )
kPhys           = length(rayDirLocal) * tLocalUnitWorld
worldDistToChild = tWorldBase + (tEntryWorld + tChild) * kPhys
childWorldSize  = ref.childScale * state.scale_exp2 * tLocalUnitWorld
decline (mip-shade this leaf)  iff  worldDistToChild*coef + bias >= childWorldSize
```

- `tChild`: the along-ray t of the child cube's center (`childOriginLocal` is in the same
  unmirrored parent-local [1,2) space as `coef.normOrigin` — the proven M5 remap inputs),
  clamped to the leaf's own [entry, exit] span. This replaces the chord-floored `tv_max`.
- `worldDistToChild`: true camera→child distance, composed exactly like the shipped
  `hitT = tEntryWorld + t` convention, made camera-anchored (fix for audit point 2) and
  hop-correct via `tWorldBase` (accumulated physical distance to this hop's entry).
- `childWorldSize`: the RHS keeps M1 Task 2's validated semantics ("the child's finest
  resolvable detail" = `childScale × leaf size`) but expresses it in world units via
  `tLocalUnitWorld`, so LHS·coef (a world footprint) and RHS (a world size) finally
  compare like with like. At hop 0 this equals `cs·se·W` where `W` = world edge of the
  tree's [1,2) cube (48wu for the M8 Earth demo).
- `raySizeCoef == 0.0` still disables the gate entirely (ablation knob preserved).

Wrapper threading (two floats down into `traverseOctreeInstancedOnce`, updated per hop):

```
hop 0:  tWorldBase = 0.0;  tLocalUnitWorld = 1.0/length(rayDirLocal)
crossing:  tWorldBase += tierCrossWorldT * (length(curRayDirLocal)*tLocalUnitWorld)
           tLocalUnitWorld *= ref.childScale
```

The shipped hitT composition (`runningHitT`, `cumulativeDirLen`) is untouched.

## Predicted behavior at the real coefficient (0.00157080 @ 45°FOV/500px), no override

M8 Earth demo (renderScale=4.8 → W=48wu, cs=2^-10, root-level leaf se=0.25):

- hop 0 (T0→T1) fires below `D0 = cs·se·W/coef = 0.011719/0.0015708 = 7.4605 wu`
  (camera→T1-region). T1 region world size `cs·W = 0.046875wu` subtends
  `0.046875/7.4605/0.0015708 ≈ 4.0 px` at handoff — appears at the resolvability limit
  and grows continuously (≈30px at 1wu, ≈300px at 0.1wu). Seamless by construction.
- hop 1 (T1→T2) fires below `D1 = D0·cs = 7.286e-3 wu`; T2 (`cs²·W = 4.578e-5wu`) is
  likewise ≈4.0px at ITS handoff (same self-similar law — the signature of a correct
  scale-invariant gate), ≈29px at 1e-3wu.
- Ordinary body rendering: **untouched** (the non-leaf gate at :983-984 is not modified;
  scenes with no farBit leaf in the ray's path are byte-identical).

Existing demos at their recorded capture distances (verified numerically before build):
unity (RHS=12wu-equiv vs LHS≈0.37) — crossing still fires; chain @R=4.8 (hop0 RHS=3,
hop1 RHS=0.75 vs LHS≈0.37) — both fire; observable @R=0.1 static d=10 (hop0 RHS=0.0625
vs 0.0157) — fires; observable hop1 RHS=0.015625 — flips at d≈9.95wu, i.e. the T2 cyan
now genuinely mip-declines just beyond the distance where it measured 1px anyway.

# Companion semantic fix: child-miss falls back to the leaf's own mip, not whole-ray sky

The shipped wrapper returns a whole-ray MISS when a crossing was taken and the child tree
missed. For cs≪1 that turns the hosting leaf into a giant sky hole around a tiny child
(the recorded "notch"), and makes the gate handoff a hard pop (mip-shade → sky in one
frame). The design doc's own §5.3 semantics ("just another miss, serve the parent's mip
sample" — already implemented for the residency and LOD early-outs) is applied to the
child-miss case too: on child miss (or hop-budget exhaustion), shade the DEEPEST parked
crossing leaf from its own mip sample (`shadeFromMipSample`), **only if that leaf has mip
coverage** — a leaf with no baked mip (or a genuinely empty region at unity scale) still
misses to sky, which keeps every mip-less crossing scene byte-identical to today.
hitT for the fallback uses the shipped composition (`runningHitT +
tierCrossWorldT*cumulativeDirLen`), identical to what the decline path would have produced.

# Mirror lockstep

`GpuTraversalMirror.h` runs exclusively with `raySizeCoef==0` (LOD structurally disabled —
its own documented convention, M1 Task 2 precedent), so the gate change does not port as
code; the mirror's Task-9/Task-10 divergence comments are updated to describe the new gate
and the child-miss mip fallback (same category as the existing "mirror is a brick-hit-test
oracle, not a shading oracle" residency note). The wrapper's hitT composition — which the
mirror DOES port — is unchanged.
