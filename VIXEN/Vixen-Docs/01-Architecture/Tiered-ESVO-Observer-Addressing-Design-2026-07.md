---
title: Tiered ESVO — Nested-Tree Addressing, Tier-Crossing Traversal & Observer-Relative Rendering
status: Design (promoted from direction 2026-07-05) — Inc1 ✅ SHIPPED 2026-07-07; Inc2 ✅ SHIPPED 2026-07-10 (§3+§5 tier-crossing mechanism + a live, validated single-crossing continuous zoom; 3-tier T2→T1→T0 chaining deferred as a follow-up, see §9); Inc3 M1-M3 ✅ SHIPPED 2026-07-10 (scale-correct hitT/LOD gate, live magnified single crossing, chained hop-loop); Inc3 M4 (Earth-scale demo) mechanism-complete but BLOCKED on a live-render finding — see §9
date: 2026-07-05
updated: 2026-07-10
tags: [architecture, svo, esvo, lod, scale, addressing, skybox, tiered-rendering]
aliases: [Nested ESVO, Tree-of-Trees, Observer Addressing, Recursive ESVO]
related:
  - "[[Sparse-Mip-ESVO-LOD-Direction-2026-07]]"
  - "[[Tiered-ESVO-Inc1-Plan-2026-07]]"
  - "[[Tiered-ESVO-Inc2-Plan-2026-07]]"
  - "[[undertow-vixen-integration-map]]"
  - "libraries/SVO/include/SVOTypes.h"
  - "libraries/SVO/include/LaineKarrasOctree.h"
  - "libraries/SVO/include/ShellOctreeGpu.h"
---

# Tiered ESVO — Nested-Tree Addressing, Tier-Crossing Traversal & Observer-Relative Rendering

> **Status (2026-07-07).** This promotes the "Observer-relative addressing" section of
> [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] (itself appended 2026-07-05, atop that doc's
> 2026-07-04 nested-tree/tier-math direction) from loose direction into a concrete spec:
> actual struct fields, actual traversal algorithm, actual seam map. **The base mip-sampling
> epic this doc depends on (§9) has since shipped** — [[Sparse-Mip-ESVO-LOD-Direction-2026-07]]'s
> Inc1 (`ae12ba78`) and Inc2 (`2351baff`) are both merged to `main`. **[[Tiered-ESVO-Inc1-Plan-2026-07]]
> now exists**, scoped to §9's own "nearest actionable slice" — `TierAddress` (§4) + address-level sky
> projection (§7 step 1) only, deliberately excluding §3/§5's tier-crossing leaf reference and
> traversal-restart machinery. **Is now COMPLETE** — all 3 milestones (M1-M3) done, each independently
> Opus-validated, plus a final whole-branch review confirming genuine cross-milestone integration and a
> conflict-free merge path against `main` (branch `feat/tiered-esvo-inc1`, 2026-07-07). A
> `SkyProjectionNode` composites synthetic-fixture sky points (direction+magnitude, derived via M1's
> `TierAddress`/`TierMath` and M2's shared-prefix composition/magnitude math) over the existing voxel
> render, live-gate-verified on real GPU with validation layers. All code references below were
> re-verified accurate against current `main` on 2026-07-07 before the plan was written.
>
> **Update (2026-07-10): §3+§5's tier-crossing mechanism (Inc2) is now COMPLETE and live-gate
> proven.** [[Tiered-ESVO-Inc2-Plan-2026-07]] shipped all 5 milestones (`feat/tiered-esvo-inc2`,
> each Opus-validated): `TierRef`/`TierRefTable` CPU plumbing (M1), the `farBit==1` construction
> path (M2), GPU traversal-restart across two independently-resident octrees with a genuine
> real-hardware crossing proof (M3), the screen-space LOD early-out + non-resident-child mip
> fallback (M4), and — the increment's actual deliverable — a live, validated, continuous
> surface-to-orbit zoom through one real tier crossing with no visible pop/seam (M5). M5's
> scripted camera zoom exercised the full composed lifecycle live on real hardware for the first
> time: the child tree starting non-resident, a mid-flight `RequestBrickResidency(true)` grant
> landing while a ray is actively crossing (mip-shade → real child geometry, one-frame flip, no
> garbage), and the screen-space LOD gate declining the crossing again as the camera continued
> pulling back (a gradual, multi-frame shrink-then-decline with no single-frame content pop —
> hand-predicted crossing distance of 40.0 world units matched the observed transition). This
> same live run surfaced and fixed a real, previously-latent bug: `BodyOctreeSceneNode`'s
> `bricksBuffer_`/`configBuffer_` were created without `VK_BUFFER_USAGE_TRANSFER_DST_BIT`, so a
> residency grant landing AFTER the first `Compile()` (exactly M5's mid-flight case — M4's own
> tests only ever toggled residency BEFORE the first upload cycle) hit
> `VUID-vkCmdCopyBuffer-dstBuffer-00120` on real hardware; fixed at the root (both buffers now
> declare the flag) and independently re-confirmed via the project's own existing
> `test_partial_brick_upload`/`test_body_octree_lifetime` gtests, run for the first time this
> session against a real GPU (WSL/Dozen) and green. **The full 3-tier T2→T1→T0 Earth-diameter
> planet chain (the stretch goal) was NOT attempted this increment** — M3/M4's mechanism proof
> covers exactly one crossing; per the plan's own explicit scope line, chaining a second crossing
> is deferred as a follow-up increment rather than blocking M5's single-crossing deliverable (see
> §9's updated sequencing note). §3/§5 are no longer "unscheduled" — see §9.

## 1. Context & Problem

[[Sparse-Mip-ESVO-LOD-Direction-2026-07]] already establishes: (a) per-level mip samples
so a distant/non-resident subtree still shades correctly, and (b) that reaching planetary
and larger scale requires **nested trees** — a leaf of one ESVO is itself the root of
another ESVO, because one tree's traversal-stack depth is fixed (`ESVOTraversalState::scale`
runs 0–22, confirmed in `LaineKarrasOctree.h:252-253` — **23 levels per tree**, i.e.
`2^23 ≈ 8.4×10^6`× linear scale range) and going from a 1cm voxel to a galaxy diameter
spans **~76 octree levels** (`log2(9.46×10^22 cm / 1 cm) ≈ 76.3`). That doc's tier math
(T0 planet / T1 region / T2 bedrock, ~10 effective levels each) already sizes this for a
single planet; **this doc generalizes it upward** — system, galaxy — and specifies the
one piece the parent doc names but does not spec: **what a cross-tree reference actually
is, on the wire, and how traversal follows it.**

This doc adds nothing to the mip-sampling/bandwidth mechanism (unchanged, see parent doc)
and nothing to the reification/delta-log side (that is undertow's `core/`, out of scope
here — see [[undertow-vixen-integration-map]] and undertow
`docs/superpowers/specs/2026-07-05-reification-design.md`). It specifies:

1. A **tier-crossing leaf reference** — the actual bit-layout addition to `ChildDescriptor`.
2. An **address type** — the short hop-chain identifying any cell/object across all tiers,
   consumed by both traversal (VIXEN) and reification (undertow).
3. The **traversal-restart algorithm** at a tier boundary (both directions: descend into a
   child tree, and the LOD-driven early-out that stops before descending).
4. The concrete **sky-projection** consumer (fleet-detection-skybox, the driving use case)
   built on top of (1)–(3).

## 2. Key discovery — the substrate already has three of the four primitives

Nothing below is greenfield; each piece already exists for a different reason and is
being reused, not invented:

| Need | Existing primitive | File |
|---|---|---|
| "This leaf is a far/indirect reference, not a local child" | `ChildDescriptor::farBit` — **already checked in traversal** (`if (currentNode->farBit)`, `LaineKarrasOctree.cpp:161`), but every builder path sets it to `0` today (`SVORebuild.cpp:439,512`). The bit is wired and unused. | `SVOTypes.h:39`, `LaineKarrasOctree.cpp:161` |
| "Remap a shallower tree onto a sub-range of the fixed 0–22 scale space" | `userToESVOScale`/`esvoToUserScale` — already exists so a `maxLevels=8` brick-local tree reuses the same ESVO bit-tricks as a full 23-level tree | `LaineKarrasOctree.h:409-415` |
| "More than one octree instance resident and indexed together" | `ConcatenatedOctrees` — already holds N independent trees (`configs[]`, `nodeCounts[]`, `brickCounts[]`) with per-instance `OctreeConfig`; `BodyInstanceGpu::octreeIndex` already selects one | `ShellOctreeGpu.h:253-268,276-286` |
| "Turn a concrete position into rendered geometry, given a provider kind" | The Stored/Procedural provider dispatch (`ProviderKind`, `evalSDF`) from [[Voxel-Content-Format-Contract-Design-2026-06]] | `SdfRecipes.h:22-25` |

**The gap is narrow and specific:** `farBit` has no defined "indirect" payload format yet
— nothing says *which* other tree, or how to remap the ray into it. `ConcatenatedOctrees`
holds sibling trees (same tier, e.g. 3 body-kind shells) but has no notion of **parent/child
tier** — no tree today points at another tree as *its own leaf content*. This doc defines
that payload and that pointer.

## 3. The tier-crossing leaf reference

### 3.1 Bit layout (extends `ChildDescriptor`, `SVOTypes.h:36-115`)

`ChildDescriptor` is a `static_assert`ed 8 bytes; no field is added or widened (traversal
stride stays untouched, matching the parent doc's non-goal on widening the 8-byte node).
Instead, the existing **brick-mode `contourPointer`/`contourMask` reuse** (already
context-dependent — "Contour mode: offset to contour values / Brick mode: index into
sparse brick array", `SVOTypes.h:43-47`) gains a third interpretation, selected by
`farBit`:

```
farBit == 0, leafMask bit set  → existing brick-mode leaf (today's behavior, unchanged)
farBit == 1, leafMask bit set  → TIER-CROSSING leaf (NEW):
                                    contourPointer (24 bits) = index into a new
                                      per-instance TierRefTable (not a brick index)
                                    contourMask    (8 bits)  = child-tree's root scale
                                      hint (0-22) — lets the traversal restart size
                                      its stack push without a second indirection
```

`farBit` was already being read (`LaineKarrasOctree.cpp:161`) and always false — this is
additive, zero-cost on every existing tree (bit stays 0), and the existing
`hasBrick()`/`getBrickIndex()` accessors are unaffected for `farBit==0` nodes.

### 3.2 `TierRef` — the indirect payload

```cpp
// New. Parallel array on ConcatenatedOctrees (one per registered cross-tier edge),
// analogous to how configs[]/nodeCounts[] already parallel the node/brick pools.
struct TierRef {
    uint32_t childOctreeIndex;   // index into ConcatenatedOctrees::configs[] — the
                                 // child tree, uploaded/resident like any sibling tree
    float    childOriginLocal[3];// child tree's [1,2)-space origin, expressed in the
                                 // PARENT tree's local [1,2) frame (parent-local → child-local
                                 // is a single scale+offset — never a flattened world matrix,
                                 // see §4)
    float    childScale;         // linear scale of child tree's unit cube, in parent-local units
};
```

`childOctreeIndex` indexing into the *same* `ConcatenatedOctrees::configs[]` that sibling
body-kind trees already use means a tier-crossing traversal restart is mechanically
identical to today's "switch which `OctreeConfig` this ray is walking" — no new resource
type, no new descriptor-set binding shape.

### 3.3 Why this stays float32-safe (precision)

Each `TierRef` expresses its child origin in the **parent's own local frame** — never in
world space, never as an accumulated matrix. This is the same discipline the parent doc's
"each tier traverses in its own local [1,2) frame... only the CPU-side chain of tier
origins needs double/fixed-point" already states; this section makes it structural: the
data format has **no field capable of holding a flattened world-to-leaf transform** — only
adjacent-tier-to-adjacent-tier — so there is nothing to accumulate error into even if a
future implementation tried. A ray crossing N tiers composes N of these local hops, each
individually well-conditioned regardless of what absolute scale the tiers represent.

## 4. The address type

An object's or point's full address is the ordered list of tier-crossing hops from some
agreed root down to it:

```cpp
// Not GPU-resident — a small CPU-side identity, cheap to store/compare/serialize.
// 4-5 entries typical (galaxy-cell → system → orbit/planet-cell → region → brick),
// derived from the tier math in Sparse-Mip-ESVO-LOD-Direction-2026-07 §"Concrete tier math".
struct TierAddress {
    std::vector<uint32_t> hops;  // hops[i] = which child octant/TierRef was taken at tier i
};
```

- **Shared-prefix = shared ancestor** — a fleet and the observer with a common `hops[0..k]`
  share tier k's frame; direction/distance between them only needs to compose the hops
  *below* the common prefix, not the whole chain (bounds the local-frame composition depth
  to what's actually divergent, not the full address length).
- **This is what traversal already produces as a side effect.** The sequence of
  `ChildDescriptor`s / `TierRef`s a ray descends through *is* the address — no separate
  bookkeeping structure duplicates what the `CastStack` (`LaineKarrasOctree.h:378-402`)
  already tracks per-ray. `TierAddress` is the **object-persistent** form of the same
  sequence (stored on a body/fleet across frames), not a new traversal mechanism.
- **This is the one shared contract with undertow.** Reification
  (`(base state, delta log, t) → concrete state`, undertow
  `docs/superpowers/specs/2026-07-05-reification-design.md`) needs the same `TierAddress`
  to decide simulation fidelity; VIXEN needs it to know which `ConcatenatedOctrees` slot
  and which `TierRef` chain to render from. The struct above is the proposed shape of that
  contract — final field types should be reconciled with whatever `Undertow.View` schema
  codegen emits (see [[undertow-vixen-integration-map]]), not hand-duplicated.

## 5. Tier-crossing traversal

### 5.1 Descend (ray enters a child tree)

When traversal hits a `farBit==1` leaf:
1. Look up `TierRef` via `contourPointer` into the current tree's `TierRefTable`.
2. Transform the ray (origin + direction) from the current tree's local `[1,2)` frame into
   the child's, using `TierRef::childOriginLocal`/`childScale` — a single scale+offset, per
   §3.3.
3. Push a **traversal restart**: re-enter the standard ESVO iterative traversal
   (`executeAdvancePhase`/`executePopPhase`, `LaineKarrasOctree.h:494-505`) against
   `configs[childOctreeIndex]`, at the child's own scale 0, with a **fresh** `CastStack`
   sized to the child's own depth (bounded per-ray state — this doc does not require
   growing `MAX_STACK_DEPTH`, since only one tier's stack is live at a time; the
   parent-tier stack is parked, not merged with the child's).
4. On exiting the child tree (ray leaves its `[1,2)` bounds), pop back to the parent's
   parked stack and resume at the parent's next `POP`/`ADVANCE`, exactly as if the
   `farBit` leaf had been an ordinary voxel miss.

This is a **traversal restart, not a recursive descent within one stack** — matches the
parent doc's "cross-tier descent is a traversal restart into the child tree (bounds
per-ray state)."

### 5.2 LOD early-out (the common case — most rays never cross)

Before doing (5.1), check the child's angular footprint against `raySizeCoef`
(`RaySizeCoefNode`, already computes the LOD cone-spread constant from render-target
height) the same way an ordinary leaf-vs-continue-subdividing decision is made today. If
the footprint is sub-pixel, shade from the **parent tier's mip sample at this node**
(per [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] §"Sample placement") and never cross into
the child tree at all. This is the mechanism that makes "4-5 tiers deep" cheap in
practice: a ray only ever pays for a tier-crossing restart when something in that child
tree is large enough on screen to matter, which is also exactly the residency-need signal
(§5.3).

### 5.3 Residency

A `TierRef`'s child tree does not need to be GPU-resident to exist as an address — per
[[Sparse-Mip-ESVO-LOD-Direction-2026-07]]'s residency policy (mip tier pinned, brick tier
evictable), a `farBit` leaf whose child is not currently loaded is just another "miss,
serve the parent's mip sample" case (§5.2), identical in shape to a non-resident brick
today. No new residency state machine — this reuses the existing sentinel-miss pattern at
one tier higher.

## 6. Procedural base vs. persisted delta (scope boundary with undertow)

Per [[undertow-vixen-integration-map]] and the reification doc: **VIXEN never stores "the
universe."** A `TierRef`'s child tree is expected to be populated on demand by evaluating
`(TierAddress, seed) → base geometry` — the existing recipe/kernel-codegen bake-on-demand
path (`SetBakeRecipe`, per [[kernel-codegen-framework-direction]]), generalized to be keyed
by address instead of assumed-single-body. Any *persisted* divergence (a mined voxel, a
destroyed module) arrives from undertow as a reified `ConcreteState` and is baked the same
way an edited recipe is baked today — VIXEN does not know or care whether the content it's
baking is "pure procedural" or "procedural + delta," because reification already resolved
that distinction before the state reaches the View contract. **This is a hard scope line:**
this doc does not specify a delta format, a persistence format, or a cache-eviction policy
for tier content — those are either undertow's reification concern or an orthogonal VIXEN
streaming-budget concern already covered by the parent mip-direction doc.

## 7. Driving use case — observation-post fleet detection

Concrete exercise of §3-§5, matching the scenario that motivated this doc: player spots a
fleet's deceleration burn on the night sky from an adjacent sector, zooms in (observation
post / telescope), and watches it resolve continuously from a point of light into real
geometry.

1. **Sky projection (far, no tier-crossing yet).** For every candidate object at or above
   the observer's tier (ancestor cells: system → galaxy), compute direction
   `normalize(objectLocal − observerLocal)` composed only through the shared-prefix
   ancestor (§4) — never a flattened world coordinate. Render as a point/disk in the
   skybox: direction + apparent magnitude (brightness falloff + optional light-delay term
   for the burn's staleness — a physically-motivated detection floor, not required for v1).
   This step needs **no tier-crossing traversal at all** — it's evaluated at the address
   level, not ray-marched.
2. **Zoom (LOD continuity, §5.2).** As FOV narrows / magnification increases, the fleet's
   angular footprint grows; once it crosses the `raySizeCoef` pixel-footprint threshold,
   the sky-point handoff becomes a real tier-crossing traversal (§5.1) into the fleet's own
   tree — same mechanism an ordinary planet-approach LOD transition uses, no separate
   impostor/billboard system.
3. **Resolve (near).** Standard ESVO ray-march inside the fleet's tree (ship geometry,
   whatever provider kind it declares) — unchanged rendering path from here down.

No new rendering system is required for step 1 beyond "evaluate direction+magnitude from
an address and composite into the skybox target" — a small, bolt-on node consuming
`TierAddress` data, not a new traversal mode.

## 8. Seam map (files that would change, if scheduled)

| File | Role | Change |
|---|---|---|
| `libraries/SVO/include/SVOTypes.h` (`ChildDescriptor`) | leaf descriptor | no field added/widened; `farBit==1` + leaf bit defines the tier-crossing interpretation of `contourPointer`/`contourMask` (§3.1) |
| `libraries/SVO/src/SVORebuild.cpp`, `SVOBuilder.cpp` | tree construction | gains a path that sets `farBit=1` + writes a `TierRefTable` entry (today: `farBit` always `0`) |
| `libraries/SVO/include/LaineKarrasOctree.h`/`.cpp` (`executeAdvancePhase`/`executePopPhase`, `CastStack`) | traversal | add the tier-crossing branch (§5.1): ray remap + traversal restart against a different `OctreeConfig`; LOD early-out (§5.2) reuses `raySizeCoef` |
| `libraries/SVO/include/ShellOctreeGpu.h` (`ConcatenatedOctrees`) | multi-tree container | add `TierRef[]` / `TierRefTable` as a new parallel pool, same shape as `configs[]`/`nodeCounts[]` |
| `libraries/RenderGraph/include/Nodes/RaySizeCoefNode.h` | LOD constant | consumed, not modified — §5.2 reuses it as-is |
| new: sky-projection node (name TBD) | skybox | consumes `TierAddress` data for far objects, composites direction+magnitude into the skybox render target (§7 step 1) |
| (undertow, external) `Undertow.View` schema | sim↔render contract | gains whatever columns carry `TierAddress` + reified per-object state — coordinated via [[undertow-vixen-integration-map]], not owned here |

## 9. Sequencing

This is **downstream of** [[Sparse-Mip-ESVO-LOD-Direction-2026-07]]'s base epic (mip
sampling, single-planet T0/T1/T2 nesting) — that epic proves nested-tree traversal restart
and per-tier mip fallback at one scale hop (planet → region → bedrock) before this doc's
generalization (arbitrary tier count, cross-tree `TierRef`, address-level sky projection)
is attempted. **The base epic has shipped** (Inc1 `ae12ba78` + Inc2 `2351baff`, both merged
to `main`). The nearest actionable slice, **§7 step 1 alone** (address-level sky projection
for static content — `TierAddress` + a sky-projection node, not §5's tier-crossing traversal
restart, since a point-of-light skybox entry doesn't require ray-marching into the referenced
tree), now has a **✅ COMPLETE implementation**: [[Tiered-ESVO-Inc1-Plan-2026-07]] (written and
shipped 2026-07-07, branch `feat/tiered-esvo-inc1`). §3/§5 (the tier-crossing leaf reference and
traversal-restart machinery) remain unscheduled — that Plan's own §0 explicitly excluded them,
matching this section's sequencing; they are the natural next increment once nested-tree work is
actually prioritized. **Nested-tree work is now prioritized (user request 2026-07-07)** —
[[Tiered-ESVO-Inc2-Plan-2026-07]] scoped §3+§5 directly: `TierRef`/`TierRefTable`, the `farBit==1`
construction path, GPU traversal-restart across two independently-resident octrees, and a live,
continuous surface-to-orbit zoom demonstration through at least one real tier crossing (a genuine
Earth-diameter-scale 3-tier T2→T1→T0 chain was a stretch goal within that plan, not a hard
requirement). **✅ SHIPPED 2026-07-10** — all 5 milestones complete, each Opus-validated
(`feat/tiered-esvo-inc2`). §3+§5's tier-crossing mechanism (traversal-restart, LOD early-out,
residency reuse) is proven live on real hardware for a SINGLE crossing, including the dynamic
mid-flight residency transition (child tree starts non-resident, a grant lands while a ray is
actively crossing, the crossing transitions mip-shade → real geometry with no garbage) and a
continuous camera zoom through the LOD-gate boundary with no visible pop/seam. **The 3-tier
T2→T1→T0 chain was NOT attempted** — Inc2's own scope explicitly allowed shipping the
single-crossing proof alone if chaining surfaced unexpected complexity, and the increment's
time budget went to the single-crossing live-gate discipline (M3's camera-authority bug fix,
M5's mid-flight-residency buffer-usage-flag bug fix) rather than the stretch goal. **Chaining a
second crossing (T1 region → T0 planet, reusing the identical M3/M4 mechanism) is the natural
next increment** — per the design doc's own repeated point, N-tier crossing is the SAME
mechanism repeated, not new per-tier engineering, so this is expected to be straightforward
composition work, not a new mechanism design. One concrete prerequisite carried forward from
Inc2's own validator addenda: both this increment's crossings kept `childScale==1.0` throughout
(explicit scope boundary); a scale-magnified tier (the real planet-scale case, where child and
parent trees are NOT the same physical scale) needs (a) a per-child-scale `hitT` normalization
fix at the crossing-composition site (the additive `hitT = tierCrossWorldT + childHitT` is only
exact at `childScale==1.0`) and (b) the LOD gate's inequality generalized to
`>= childScale*scale_exp2` (currently gates on the parent leaf's own footprint, exactly correct
only at `childScale==1.0`) — both flagged explicitly in Inc2's M3/M4 validator addenda as
named, scoped prerequisites for whichever increment first builds a non-unity-scale tier.

**Update (2026-07-10): [[Tiered-ESVO-Inc3-Plan-2026-07]] M1-M3 ✅ SHIPPED, M4 mechanism-complete
but BLOCKED on a live-render finding.** M1 (scale-correct hitT normalization — a MULTIPLY by
`length(childRayDirWorld)`, not a bare childScale term — + LOD gate generalized to
`childScale*scale_exp2`) and M2 (a live, magnified single crossing at `childScale=0.25`,
predicted-and-measured 3.93× apparent-size ratio) landed the two prerequisites this section
named. M3 generalized the traversal-restart wrapper to a bounded hop loop (`MAX_TIER_HOPS=5`)
and live-gated a genuine T2→T1→T0 chain at `childScale=1.0` (unity/near-unity scale). **M4
(the epic gate: Earth-scale, `childScale=2^-10` per hop, ~2^-20 total ratio) implemented the
full mechanism** — a k-invariant `childOriginLocal` placement technique (verified in CPU parity
tests down to the real 2^-10 ratio) correctly keeps every hop's `tEntryWorld` term ~0 even
under ~1024×/~1,048,576× cumulative-length amplification, and a scripted zoom + widened camera
orbit-distance floor were built for the live demonstration. **However, live captures could not
produce the expected distinctly-colored child geometry at the crossing for ANY tested non-unity,
non-near-unity `childScale` value (0.25, 0.5, 2^-10) in this environment** — including at Inc3
M2's own original, previously-Opus-validated commit checked out standalone, ruling out a
regression introduced by M3/M4's own work. Only `childScale==1.0` (Inc2's original case) and
`childScale` in roughly `{0.7-0.9}` (visibly shrinking, but real, colored slivers) render
correctly in this session's testing; scales below roughly 0.5 render as background/miss at the
crossing instead of the child's own surface. Since `GpuTraversalMirror.h` only models the
binary-DDA leaf-hit path (not the live SDF march `handleLeafHitInstancedSdf`/`marchBrickSdf`),
CPU parity tests cannot exercise or catch this — the mechanism (hop composition, hitT
normalization, LOD gate) is independently proven correct on the CPU side at 2^-10, but the
**live SDF-march behavior at non-unity/small childScale is an open, unresolved finding**,
carried forward as the concrete blocker for the next increment (or a resumed M4) before the
Earth-scale zoom can be called genuinely seamless end-to-end. See
[[Tiered-ESVO-Inc3-Plan-2026-07]]'s M4 Progress Log entry for the full investigation trail.

## 10. Rejected alternatives

- **Widen `ChildDescriptor` to carry a dedicated tier-ref field** — rejected in favor of
  reusing the existing `farBit`+brick-mode-reuse pattern (§3.1): keeps the node at 8 bytes,
  keeps traversal stride unchanged, matches the parent doc's explicit non-goal on widening
  the node for mip samples (same reasoning applies here).
- **A single accumulated world-space transform carried per-ray across tier crossings** —
  rejected (§3.3): composing per-hop parent-local→child-local transforms keeps every
  intermediate value well-conditioned regardless of recursion depth; a flattened
  world-to-leaf matrix would accumulate multiplicative error across tiers for no benefit,
  since nothing downstream needs the flattened form.
- **Growing `MAX_STACK_DEPTH` to hold all tiers' stacks simultaneously** — rejected (§5.1):
  a traversal restart parks the parent stack and uses a fresh child stack; tiers are never
  concurrently live beyond "parent parked, child active," so the existing per-tree stack
  depth is sufficient.

## 11. Open decisions

- **`TierRefTable` residency/upload shape** — parallel pool on `ConcatenatedOctrees` (§3.2)
  is the sketch; exact upload/binding (own SSBO vs. tail of an existing one) is a spec-time
  call, same category of decision the parent doc defers for mip samples.
- **`TierAddress` wire format with undertow** — §4's struct is a proposal; the authoritative
  shape should be reconciled against whatever `Undertow.View` schema-codegen would emit,
  once undertow's reification primitive has a concrete per-entity address representation.
- **Whether the sky-projection node lives in `RenderGraph` proper or as a new small
  library** — deferred to whenever §7/§9's "nearest actionable slice" is actually scheduled.

## Related

- [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] — the base epic (mip sampling, bandwidth,
  T0/T1/T2 single-planet nesting) this doc generalizes upward and depends on
- [[undertow-vixen-integration-map]] — the sim↔render seam; `TierAddress` is the shared
  contract with undertow's reification primitive
- [[kernel-codegen-framework-direction]] — the bake-on-demand mechanism a `TierRef`'s child
  tree content would use for procedural base geometry
- [[Voxel-Content-Format-Contract-Design-2026-06]] — the provider-kind dispatch a resolved
  tier-crossing leaf ultimately renders through
