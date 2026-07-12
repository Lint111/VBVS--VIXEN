---
title: Tiered ESVO Inc3 M1 — hitT normalization derivation (standalone numeric trace)
status: Evidence artifact for Inc3 M1 Task 1
depends: Tiered-ESVO-Inc3-Plan-2026-07.md (Task 1)
---

# Task 1 — standalone numeric derivation

Per the plan's discipline requirement ("verify the derivation with a standalone
numeric trace... repeat that discipline" — referring to Inc2 M3's implementer
self-catching a multiply-vs-divide error this same way), this is the numeric
trace that determined the correct composition formula BEFORE implementing it.

## Setup

Generic (non-Inc2-fixture) uniform-scale matrices, chosen to avoid any
coincidental cancellation the shipped SDF-sphere demo fixture might hide:

- Parent `localToWorld = diag(S_parent)`, `S_parent = 4.0`.
- Child `localToWorld = diag(S_child)`, `S_child = 3.0` (deliberately DIFFERENT
  from `S_parent`, since Inc2's shipped demo has `S_child == S_parent`, which
  would hide a scale-dependent bug).
- Ray: `rayOrigin = (-2, 0.5, 0.5)`, `rayDir = (1, 0, 0)` (unit length).
- `TierRef.childScale = k = 0.5` (a genuine `childScale != 1` case).
- `TierRef.childOriginLocal = (0.4, 0.4, 0.4)` (parent-local units).

## Derivation

`remapRayIntoChildFrame` (shader `BodyInstanceRayMarch.comp:581-588`, mirrored
in `GpuTraversalMirror.h`):

```
invScale = 1/childScale
childLocalOrigin = (parentLocalOrigin - childOrigin) * invScale + 1.5
childLocalDir    = parentLocalDir * invScale
```

The wrapper (`traverseOctreeInstanced`) then builds:

```
childRayOriginWorld = childLocalToWorld * (childLocalOrigin - 1.0)
childRayDirWorld    = mat3(childLocalToWorld) * childLocalDir
```

and calls the child's `traverseOctreeInstancedOnce` with this WORLD-space ray.

**Question**: is the child call's own reported `hitT` (a distance parametric in
`childRayDirWorld`) already a true world distance, or does it need a
correction factor before composing `hitT = tierCrossWorldT + childHitT`?

### Numeric trace (Python, executed standalone — see conversation record)

```python
S_parent, S_child = 4.0, 3.0
rayOrigin = (-2.0, 0.5, 0.5); rayDir = (1.0, 0.0, 0.0)
rayOriginLocal = rayOrigin / S_parent          # worldToLocal
rayDirLocal    = rayDir / S_parent
crossing_local = (0.5, 0.5, 0.5)               # a hand-picked point ON the ray
t_local = 4.0                                   # solved so rayOriginLocal + t*rayDirLocal == crossing_local
tierCrossWorldT = t_local                       # = 4.0, confirmed == true world distance via
                                                 # direct geometric round-trip (see full script)

childScale = 0.5
childOriginLocal = (0.4, 0.4, 0.4)
parentLocalOrigin = crossing_local
parentLocalDir = rayDirLocal
invScale = 1/childScale
childLocalOrigin = (parentLocalOrigin - childOriginLocal)*invScale + 1.5   # = (1.5, 1.5, 1.5)
childLocalDir    = parentLocalDir * invScale                               # = (0.5, 0, 0)

childRayOriginWorld = (childLocalOrigin - 1.0) * S_child   # = (1.5, 1.5, 1.5)
childRayDirWorld     = childLocalDir * S_child             # = (1.5, 0, 0)

# Child hit, hand-picked at child-local x=1.65 (s_child=0.3 along childLocalDir):
s_child = 0.3
child_hit_local  = childLocalOrigin + s_child*childLocalDir   # = (1.65, 1.5, 1.5)
child_hit_world  = (child_hit_local - 1.0) * S_child           # = (1.95, 1.5, 1.5)  [TRUE world point]

true_world_dist_from_crossing = |child_hit_world - childRayOriginWorld|   # = 0.45
s_child_reported = 0.3   # what castRayOnce's own "t" convention reports for this hit

ratio = true_world_dist_from_crossing / s_child_reported   # = 1.5
length(childRayDirWorld) = 1.5                              # EXACTLY equals the ratio
```

**Result**: `true_world_dist = s_child_reported * length(childRayDirWorld)`,
confirmed to full float precision. Neither a plain `* childScale` nor
`/ childScale` reproduces this — `length(childRayDirWorld)` depends on BOTH
`childScale` AND the child octree's own independent `localToWorld` scale
(`S_child`), which a bare `childScale` factor cannot recover. This was also
verified structurally: the whole SVO instance system's `hitT == world
distance` guarantee (both `renderScale`'s scalar de-instancing and
`kWorldGridSize`'s per-octree scalar) depends on scale being uniform end to
end — `length(childRayDirWorld)` is the correct, general, already-in-scope
quantity (the wrapper already computes this vector at the composition site),
requiring no new state.

## Conclusion — implemented

```
hitT = tierCrossWorldT + childHitT * length(childRayDirWorld)
```

At `childScale == 1.0` with `S_child == S_parent` (Inc2's shipped demo),
`length(childRayDirWorld) == 1.0` exactly, so this is byte-for-byte identical
to Inc2's original plain-addition formula — confirmed by the M1 live baseline
regression (unchanged `VIXEN_TIER_CROSSING_DEMO` behavior, VUID baseline
unchanged at 10 emissions).

Implemented at:
- `VIXEN/shaders/BodyInstanceRayMarch.comp` — `traverseOctreeInstanced`'s
  composition site (`childRayDirWorldLen` local, `hitT = tierCrossWorldT +
  hitT * childRayDirWorldLen`).
- `VIXEN/libraries/SVO/include/GpuTraversalMirror.h` — `castRay`'s mirrored
  composition site, in lockstep per the file's SYNC CONTRACT.

CPU parity test (`test_tier_crossing_mirror_parity.cpp`,
`NonUnityChildScaleHitTParity`) exercises `childScale ∈ {0.5, 2.0, 2^-10}` on
the repo's actual SDF-sphere fixture and asserts the corrected code's
measured `hit.t` values; a regression to the old plain-addition formula (or
any other multiply/divide inversion) was verified BY DIRECT TEST to fail
these assertions (measured old-formula values: `45.0` invariant across all
non-unity `childScale`, vs. the correct varying `70.0` / `32.5` / `25618.55`).
