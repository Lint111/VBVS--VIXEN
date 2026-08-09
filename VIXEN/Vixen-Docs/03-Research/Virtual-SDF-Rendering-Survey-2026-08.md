# Virtual SDF Rendering — Literature Survey + First-Principles Audit (2026-08)

**Status:** 📐 REFERENCE — external-literature survey, no code claims.
**Scope:** efficient *virtual* (procedural / analytic / CSG-tree) SDF rendering, and an audit of
VIXEN's first-principles virtual-recipe architecture against it.
**Beat boundary:** this doc covers the **SDF/procedural** side. GigaVoxels (2009/2011) and
post-GigaVoxels *voxel* caching are covered by sibling docs; they are referenced here only where
an SDF system explicitly borrows from them.

**Our architecture under audit** (source of truth, not restated in full here):
- `docs/plans/2026-08-04-wavefront-recipe-shading.md` → "W-RT design extensions" (§ user rulings
  2026-08-07) + Status rollup.
- memory `recipe-variant-view-query-generation.md` → recipe I/O contract, m/n/o pruning axes,
  S0/S1 admission ladder, instance binding pass, n±1 residency.

---

## 0. What was actually read

Primary sources read in full or in substantial part (PDFs downloaded and text-extracted, not
summarized second-hand):

| Source | What was read |
|---|---|
| Evans, *Learning from Failure*, SIGGRAPH 2015 Advances | **all 145 slides + speaker notes** (`AlexEvans_SIGGRAPH-2015-sml.pdf`) |
| Aaltonen, *GPU-Based Clay Simulation and Ray-Tracing Tech in Claybook*, GDC 2018 | **all 70 slides** (`Aaltonen_Sebastian_GPU_Based_Clay.pdf`) |
| Wright et al., *Lumen*, SIGGRAPH 2022 Advances | **199-page deck, SDF/traversal/culling sections read**; GI-integration sections skimmed |
| Barbier et al., *Lipschitz Pruning: Hierarchical Simplification of Primitive-Based SDFs*, EG 2025 (CGF) | **full paper** (HAL `hal-05050831`), incl. both procedures + results tables |
| Galin et al., *Segment Tracing Using Local Lipschitz Bounds*, EG 2020 (CGF) | **method + bound-derivation sections** (HAL `hal-02507361`) |
| Keeter, *Massively Parallel Rendering of Complex Closed-Form Implicit Surfaces*, SIGGRAPH 2020 | **full paper** (`keeter_mpr20.pdf`) — tape/slot/tile mechanics + benchmarks |
| *Synchronized-tracing of implicit surfaces*, 2023 (arXiv 2304.09673) | pipeline + A-buffer + bottom-up pruning sections |
| Keinert et al., *Enhanced Sphere Tracing*, 2014 | abstract + contribution summary (secondary sources; paywalled primary) |
| Godot SDFGI | official docs + Godot 4.0 announcement (secondary) |

**Honest gaps:** Keinert 2014 and the Godot SDFGI internals were read via secondary summaries, not
the primary PDFs/source. Claybook's Digital Dragons 2018 variant (which Aaltonen says has *more*
coarse-RT detail than the GDC deck) was **not** obtained — it is video-only. MagicaCSG/womp have no
published technical material; that line of the brief returned nothing citable. Neural SDFs were
deliberately not pursued: nothing found that was practically informative about locality/caching at
our scale, and the brief scoped them "only where informative."

---

## 1. Dreams (Media Molecule) — the closest production ancestor

### 1.1 Mechanism

Dreams' shipping pipeline is **CSG edit list → hierarchical culled evaluator → sparse SDF bricks →
point cloud → splats**. The renderer that shipped is the *fourth* attempt; the **evaluator survived
all four** ("this is building on the evaluator as the only thing that has survived all this
upheaval", slide 108).

**Edit list, not tree.** "we called each primitive an 'edit', we support a simple list, not tree of
CSG edits. and models are made up of anything from 1 to 100,000 edits" (slide 7). CSG trees are
"entirely right leaning, meaning they were a simple list. Simple is good!" (slide 22). Ops are add,
subtract, colour, plus soft blend.

**The evaluator's hierarchical culling** (slides 24–26) — the mechanism most relevant to us:
start with a coarse voxel grid (4×4×4), build "a list of edits that could possibly overlap each
voxel, and then iteratively refin[e] the voxels by splitting them and shortening the lists for
each." Empty and full cells are marked early; boundary cells recurse to a resolution limit.
**Split factor is 4×4×4 in one go**, chosen because it "fits GCN's 64 wide wavefronts and lets us
make coherent scalar branches on primitive type" (slide 26).

**Interval arithmetic + max-norm.** "Because our distance fields are good quality, we can use
interval arithmetic for additional culling" (slide 30). The evaluator works in **max norm**
(`d = max(|x|,|y|,|z|)`) because "the shape of something distance 'd' away from a central origin in
max norm is a cube, which nicely matches the shape of nodes in our hierarchy" (slide 29), citing
UNC's *Efficient Max-Norm Distance Computation and Reliable Voxelization*.

**Soft blend breaks culling** (slide 30) — stated as a first-class problem:
> "Soft min/max needs to revert to hard min/max once distance fields are sufficiently far apart
> (otherwise you can never cull either side)… Need to consider the amount of 'future soft blend'
> when culling, as soft blend increases the range at which primitives can influence the final
> surface."

Their kernel: `soft_min(a,b,r) { float e = max(r-abs(a-b),0); return min(a,b) - e*e*0.25/r; }`,
which "has no effect once abs(a-b) > r" — i.e. **compact blend support by construction**.

**Numbers (slide 33, real models).** 604–53,976 edits; 1M–10M surface voxels; evaluator time
0.009–0.34 s; **culling efficiency 98.3%–99.96%**; "10m - 100m voxels evaluated per second on a
ps4". Dispatch counts for one model: eval 60, sweep 91, points 459, bricker 73 (slide 21). The
"CS of doom" is "a few 3000+ instruction shaders chained together"; the full pipeline is "40+
compute shaders".

**Domain restriction as an enabling decision:** "We had limited the set of edits to exclude domain
deformation or any non-local effects like blur" (slide 22) — non-local ops would destroy the
per-region culling.

### 1.2 The graveyard — each abandoned approach, with the stated reason

Slide 105 is the summary tombstone; reasons below are the speaker notes.

| Engine | Reason abandoned (Evans' words) |
|---|---|
| **1. Polygons** (marching cubes → dual contouring → manifold DC) | "Manifold? Non-Self-Intersecting? Pick one :(" (slide 49). Meshes dense (2M quads for a sphere), mushy edges, slivers; "LOD is still hard (many completely impractical papers)". Hard to tune hard/soft heuristics for UGC. |
| **2. Volumetric Billboards** (screen-aligned slices) | "too much fillrate" — "slicing every cube into 8-16 tiny slices… was going to burn way too much fill rate" (slide 72). |
| **3. GigaVoxels straight** | "single object" — "it focussed on a single large field that (eye) rays were traced through, and I needed… a cloud of rigid voxels models" (slide 66). |
| **4. Brick engine** (rasterize 8³ brick cubes, PS marches inside) | Shipped to artists for ~2 years and *worked*; abandoned for **art direction**, not perf — "why were we paying all this runtime cost (memory & time) to render bricks if they just gave us a poly look?" (slide 106). OIT over the fuzz was never solved: a year of "per pixel atomic bubble sort, front k approximations, depth peeling" (slide 76). |
| **5. Froxel refinement renderer** | "too slow" — "about 4x lower than what I needed for PS4"; "4x-10x depending on my optimism" (slides 103–104). Edge pixels "unboundedly deep" → load-balancing hoops; memory for gigavoxel bricks a struggle. |
| **Final: splats** | Won on *art direction* — the art director "kept pointing at an actual oil painting"; 64-bit atomic-min point splatting, "28.2M point splats… 4.38ms, so that's about 640 million single pixel splats per second" (slide 135). |

The **froxel refinement renderer** (slides 88–91) deserves separate note because it is the closest
Dreams got to our architecture's *shape*: divide the frustum into coarse froxels, "build a list per
froxel of which objects touch it", recursively subdivide, and "as soon as your froxel size matches
the size of gigavoxel prefiltered voxels, you sample the sparse octree of the object (instead of
just using OBBs) to further cull your lists." Evans explicitly names the symmetry: "it's exactly
like the evaluator, except this time we have whole objects stored as gigavoxel trees of bricks
(instead of platonic SDF elements in the evaluator)… and our domain is over froxels, not voxels."
Front-to-back serial processing per pixel gives "perfect, hierarchical occlusion culling."

### 1.3 Audit verdict

**MATCHES-OURS (validation), with one important divergence.**

| Our concept | Dreams twin | Verdict |
|---|---|---|
| Emitter-derived per-boundary primitive-subtree bounds | "building a list of edits that could possibly overlap each voxel… iteratively refining the voxels by splitting them and shortening the lists" | **MATCHES** — same mechanism, different domain (they cull to *voxels* for materialization; we cull to *ray candidates* for direct evaluation) |
| Blends inflate boundaries by influence radius, statically merge partners | "Need to consider the amount of 'future soft blend' when culling"; blend kernel with compact support ("no effect once abs(a-b) > r") | **MATCHES** — and their kernel choice is the *stealable detail*: pick blends that provably vanish at radius r, or culling is unsound |
| Conservative proxy shapes per boundary | max-norm bounds (cube-shaped iso-surfaces matching hierarchy nodes) | **IMPROVES-OURS** — see steal #2 |
| m×n variant selection by cone footprint | Brick-tree "cut" by view distance for constant screen-space brick size (slide 67) | **MATCHES** in spirit (LOD cut selection), ours is finer-grained (per-pixel m×n vs per-brick cut) |
| Virtual/materialized mirror | **DIVERGES** — Dreams *always materializes*. The SDF is "an intermediate representation, we use it to spawn the points at evaluation time" (slide 110). There is no runtime virtual path. | **DIVERGES** |

**The divergence is the crux, and it is defensible.** Dreams materializes because their content is
*static after edit* — a sculpt is authored once and then splatted forever, so paying a 0.09–0.34 s
evaluation once and storing 2M points is obviously right. Our recipes are **instanced and
parameterized** (a procedural ship differs per instance), so materializing every instance is the
thing we are specifically trying to avoid; the virtual path exists to amortize *program* residency
across instances instead of *data* residency per instance. Dreams' own froxel experiment is the
evidence that the virtual-ish direction was viable but *not at 2015 PS4 perf*; we are attempting it
with hardware RT + a decade of hardware.

**Stealable parameters:**
- **4×4×4 split granularity** chosen to fill a 64-wide wavefront and enable *scalar* branches on
  primitive type. Our per-boundary sub-program dispatch has exactly this structure — the bucket
  should be sized so a wave shares one sub-program (we already do this via W-SPLIT buckets;
  confirm the *split factor* is wave-aligned).
- **Culling-efficiency as the standing metric.** Dreams reports 98.3–99.96% vs brute force per
  model. We should report the same number per recipe family: (primitives evaluated) / (primitives
  in full recipe). It is the single number that says whether pruning is working.
- **"Time per prim per block"** as a normalized cost metric (their column) — invariant across model
  complexity, which makes it a regression gate rather than a scene-specific number.
- **Ban non-local operators in the authored recipe vocabulary** (domain deformation, blur). Dreams
  did this deliberately, and it is what makes conservative bounds derivable at all. Our recipe AST
  should reject them at bake, with the error message explaining why.

---

## 2. Claybook (Aaltonen, GDC 2018) — production direct-SDF ray tracing

### 2.1 Mechanism

Claybook ray-traces SDFs at 60 fps on base consoles, but the representation is **fully discretized**:
a `1024×1024×512`, 8-bit signed world volume texture, 586 MB with 5 mips, storing `[-4,+4]` voxels of
distance at 1/32-voxel precision (slide 8). Brushes are "small offline baked volume texture[s]"
(32³–128³); the world SDF is regenerated by combining N brushes with exponential smooth min/max.

**The line that defines their trade:** "**Runtime performance not dependent on brush count**"
(slide 9). Brush count is paid at *generation*, not at *trace*.

**Generation is a per-tile culled brush list** — structurally identical to Dreams' evaluator and to
our pruning: a 64×64×32 dispatch samples each brush at tile center and **culls if `SDF > grid tile
bounds + 4 voxels`**, accepted brushes are compacted into a per-cell list via atomics; then sparse
8×8×8 tile dispatches loop the per-cell brush list and write voxels (slides 12–15). Mips are
generated sparsely with **3 steps of the Eikonal equation in groupshared memory** to re-expand the
±2-voxel band back to ±4 (slide 16).

**Multi-level tracing with footprint termination** (slide 24) — the LOD mechanism:
```
D = volume.SampleLevel(origin + ray*t, mip)
t += worldDistance(D, mip)
D == 1.0 → mip += 2 ;  D <= 0.25 → mip -= 2; D -= halfVoxel
D < pixelConeWidth * t → BREAK      // "Break if surface is inside pixel inner bounding cone → Perfect LOD!"
```

**Cone tracing has an analytic closed form** (slides 26–27), one extra instruction:
`C = sqrt(aperture²+1); A = C/(C-aperture)` precomputed CPU-side, then in-shader `t = (t+D)*A`.

**Coarse cone-trace pre-pass** over **8×8 pixel outer bounding cones** (slide 28) — a cheap
conservative prepass that skips empty space for the whole tile before per-pixel rays.

**Soft shadows**: the demoscene approximation `c = min(c, light_size * SDF(P) / time)` (iq),
improved by **triangulating current and previous samples** to reduce banding, plus jitter + UE4
temporal accumulation (slides 38–39).

**Numbers (slide 43)** — Xbox One base @720p / AMD Vega @4K:
cone-trace pre-pass 0.2 / 0.2 ms; primary & AO rays 1.5 / 1.6 ms; shadow rays 1.7 / 1.9 ms;
material & g-buffer 0.8 / 1.0 ms. **Cache: 8 MB accessed out of 512 MB, 99.85% hit rate** (slide 32).

**Two failures worth recording** (slides 33–34), both directly relevant to us:
- **Overstepping failed**: "Reduces sampling cache locality (random rollback)… SDF(P) more noisy
  with our mipmapped approach… Bloats VGPR count and adds ALU." (This is Keinert-style
  over-relaxation, *rejected in production*.)
- **Wave-ballot load balancing failed**: refilling finished lanes with new rays lost more to
  ray-setup and cache incoherence than it gained; "Coarse cone-trace is simpler and does the job
  better."
- They also considered but had **not** shipped sparse volume: "Only ~10% of mip0 8×8×8 tiles used…
  Measured cost = 13% slower" for the indirection (slide 22).

### 2.2 Audit verdict

**DIVERGES (deliberately, and we are right for our constraints) — with two IMPROVES-OURS items.**

Claybook is the strongest published argument *against* a virtual path: they made brush count free
by materializing, and hit 60 fps on an Xbox One. But their constraint set differs from ours in
three ways that decide it:
1. **One world, one resolution.** A single 1024×1024×512 volume covers the playable space. We have
   an open star system with per-instance procedural content; a single grid at usable resolution is
   not available to us at any memory budget.
2. **Brushes are baked volume textures**, not parametric programs. Instancing a brush with new
   parameters means re-baking. Our recipes are parameterized per instance by design.
3. **586 MB for one level.** Their own future-work slide is sparse virtual texturing — i.e. they
   were heading toward the materialized half of *our* mirror.

So: their "runtime cost independent of brush count" is a real property we do **not** get on the
virtual path, and we should be honest that our virtual path trades exactly that away for
instancing + memory. **Our materialized half already has this property** (a brick costs the same
regardless of the recipe that made it) — which is an argument for our delta-ownership rule being the
right escape hatch: anything that gets edited or hot enough materializes into the brick path and
*joins* Claybook's cost model.

**IMPROVES-OURS #1 — the coarse cone-trace pre-pass (8×8 outer bounding cones), 0.2 ms.**
We have per-pixel m/n selection at tEntry, but no *tile-level conservative skip before the
per-pixel work*. An 8×8 outer-cone prepass would give the candidate-enumeration stage a cheap
per-tile tEntry lower bound, shortening every segment march in the tile. It composes with our
bucketed dispatch (the prepass output is per-tile, buckets are per-recipe).

**IMPROVES-OURS #2 — the analytic cone-trace step (`t = (t+D)*A`), one instruction.**
Our footprint-based n-selection reads featureScale per instance; the *marching* side can adopt the
closed-form widening for free. It also gives a principled termination criterion:
`D < pixelConeWidth * t → BREAK`, which is exactly the "minimum work per pixel" our m×n selection is
trying to achieve, but expressed as a march-termination rather than a variant choice. **These are
complementary, not redundant** — variant choice picks the *program*, cone termination stops the
*march*.

**Stealable parameters:** ±4-voxel narrow band at 8-bit / 1-32 voxel precision; mip step
`+=2/-=2` with `D==1.0` / `D<=0.25` thresholds; `halfVoxel` correction on mip-down;
last-step geometric-series refinement `step = D/(1-(D-D₋₁))` (slide 25); Eikonal ×3 in GSM to
re-expand a downsampled band; **13% measured cost of one indirection level** — a directly usable
prior for our brickmap 2-hop budget.

**Anti-steal (do not adopt):** over-relaxation and wave-ballot rebalancing. Both were measured and
rejected in a shipping 60-fps SDF renderer, for reasons (VGPR bloat, cache incoherence) that apply
to us with *more* force since our bytecode interpreter is already register-hungry (the 32/18-reg
testimony in W-SPLIT).

---

## 3. UE5 Lumen (Wright et al., SIGGRAPH 2022) — the production virtual/materialized mirror

### 3.1 Mechanism

Lumen's software ray tracing runs on **two SDF representations**, and the way it chooses between
them is the closest production analogue to our m/n footprint selection.

**Mesh SDFs (per-asset, "virtual" in the streaming sense):**
- Generated at mesh import (Embree point query; 64 rays/voxel counting backfaces for sign);
  "~0.6ms to build a large 1.5M tri mesh".
- Stored as a **mip-mapped virtual volume texture**: "Sparse 8³ bricks with 0.5 texel border",
  "[-4;+4] voxel distance in 1 byte", page table per mip.
- **Streamed by feedback**: every frame a shader loops all instances, computes the required mip per
  asset, and writes requests; CPU downloads them and streams mips in/out. **Fixed 320 MB brick
  pool** with a linear allocator.
- Two-level structure: bottom = primitives, top = "flat instance descriptor array" — explicitly
  "to leverage instances for storage".

**Global SDF (clipmaps, "materialized"):** 4 sparse clipmaps of 256³, same 8³-brick virtual volume
texture layout, merging all mesh SDFs + heightfields around the camera. **Cached and
incrementally updated**: "we track all scene modifications and build a list of modified bricks on
the GPU. Next we cull all the objects in the scene to the current clipmap and then cull resulting
list to modified bricks." Static bricks are composited into dynamic ones. Far clipmaps update less
often ("time splice"), with per-clipmap LOD settings.

**A quarter-res non-sparse "coarse mip"** accelerates empty-space skipping, used *instead of*
stepping clipmap levels "as our clipmaps have different LOD levels and objects may be missing from
the largest ones."

**The selection rule (our m/n kin):** near-field detail traces are **limited to 2 m**, and rays
continue in the Global SDF beyond that. Detail tracing needs no BVH/grid at all — instead objects
are culled into an **influence froxel grid**: cull to frustum → mark froxels containing geometry →
cull objects to marked froxels with **a rough bounds test then a precise distance-field sample** →
compact into a contiguous per-cell array. At trace time: "Load a single cull grid cell, loop over
all objects in it and ray march them till the last found hit. This results in a very simple and
coherent tracing kernel."

For **directional shadows** (parallel rays, no widening footprint) they cannot rely on the cone,
so they cull objects into a **light-space 2D grid by rasterizing object-oriented bounds**, again
with a fine SDF-sample culling pass in the pixel shader.

**Their abandoned alternatives** (slides ~508–555) are as informative as Dreams':
- **BVH and world-space grids**: built once per frame, reusable across passes — but "performance of
  long incoherent rays wasn't good enough. Software BVH traversal has a quite complex kernel."
  Their resolution was to **stop tracing long rays** rather than to build a better structure.
- **Voxel cone tracing**: "leaky" — merging geometry properties into a volume leaks, especially in
  lower mips.
- **Voxel bit bricks** (1 bit/voxel in an 8³ brick): "surprisingly slow"; even after adding a
  proximity map they "decided to drop voxels and arrived at a Global Distance Field."
- On hardware RT: "overlapping instances in a BVH are an issue as ray needs to visit each one of
  them in order to find the closest hit and we have no ability to change this acceleration
  structure" — i.e. the exact constraint our proxy contract is designed around.

Mesh SDF marching is capped at **64 iterations**, reporting a hit at the current t if exceeded;
normals from 6-sample central differences.

### 3.2 Audit verdict

**MATCHES-OURS on the mirror; IMPROVES-OURS on residency and on the shadow-pass problem.**

| Our concept | Lumen twin | Verdict |
|---|---|---|
| Virtual/materialized mirror over one candidate stream | Mesh SDF (per-asset, streamed) ↔ Global SDF clipmaps (merged, cached) | **MATCHES** — and validates that a production engine keeps *both* rather than choosing |
| Delta ownership: materialized bricks authoritative, can carve virtual | "track all scene modifications → list of modified bricks → cull objects to modified bricks"; static composited into dynamic | **MATCHES** — their static/dynamic brick composite is precisely an ownership rule |
| n±1 residency window per boundary | Per-frame mip-request shader + CPU streaming into a **fixed 320 MB pool**, linear allocator | **IMPROVES-OURS** — see steal #1 |
| Grid bins as search index *and* delta mask (one load, two roles) | Influence froxel grid used for detail traces; modified-brick list for updates | **MATCHES** |
| Footprint-driven m/n selection | 2 m near-field cutoff → Global SDF beyond; per-clipmap LOD | **DIVERGES** — theirs is a hard distance switch, ours is continuous. Ours is better *for our product ruling* (scale-to-the-box wants continuous quality); theirs is far simpler and provably shippable. |
| Output slicing (o-axis): shadow pass = density slice only | Shadow pass uses a **completely different acceleration structure** (light-space 2D grid) because the cone never widens | **IMPROVES-OURS** — see steal #4 |

**The 64-iteration cap with a forced hit** is worth adopting as a policy, not just a number: it
converts an unbounded worst case into a bounded one with a bounded *error*, which is exactly what
our scale-to-the-box frame-time governor needs. Lumen accepts a wrong hit rather than a frame spike.

**Stealable parameters:** 8³ bricks with **0.5-texel border** (border avoids cross-brick filtering
artifacts — our brick path should confirm it has this); [-4,+4] in 1 byte; 4 clipmaps × 256³;
320 MB fixed pool + linear allocator; quarter-res non-sparse coarse mip for empty-space skipping;
64-iteration march cap; two-stage culling (**rough bounds test, then precise SDF sample**) — the
second stage is the one that makes per-cell lists short, and we currently only specify the first.

---

## 4. Sphere-tracing theory line — Hart → Keinert → Galin

### 4.1 Segment Tracing (Galin et al., EG 2020) — the direct theoretical twin

Sphere tracing steps by `s(p) = |f(p)|/λ` with **λ a global Lipschitz bound**; the global bound is
what limits it. Segment Tracing instead computes **λ(e), the local directional Lipschitz bound over
a candidate segment e = [t, t+ε]**, and steps by `|f(t)|/λ(e)`. Because λ(e) ≤ λ, steps are larger
and always safe.

The loop (their Figure 3): propose candidate distance εᵢ → compute local bound λ(e) over that
segment → effective step `s(t,εᵢ) = min(|f(p)|/λ(e), εᵢ)` → **anticipation**: next candidate
`εᵢ₊₁ = κ·s(t,εᵢ)`. The amplification factor κ controls locality-vs-reach; "when κ → ∞, the
computed λ(e) is actually the Lipschitz bound of the entire ray"; **experiments give κ ≈ 2** as the
optimum (their Figure 4).

The bound decomposes over the construction tree exactly as the field does: for skeletal primitives
`f = g∘d`, `λ(e) ≤ |g′∘d∘δ(e)| · ‖∇d∘δ(e)·u‖`, and — the key structural point for us — **"The
structure implicitly implements a bounding volume hierarchy that allows for efficient pruning
during the evaluation."** When λ(e) is hard for some primitive/operator, they fall back to coarser
bounds: `λ(e) ≤ λ(∆)` (whole ray) or `λ(e) ≤ λ(S)` (bounding sphere of the segment) — a graceful
degradation ladder.

The payoff case is **grazing rays**: where the ray is near-orthogonal to ∇f, `|∇d∘δ·u(e)| ≪ 1`, so
the local bound is far tighter exactly where sphere tracing stalls ("Those cases are computationally
intensive for Sphere Tracing as the radius of the spheres… becomes smaller as f(t) drops to 0 near
the surface").

They also bound the distance interval by the **1-Lipschitz inclusion** `d∘δ(e) ⊂ [d(c)-r, d(c)+r]`
where c is the segment center and r = ‖e‖/2 — a cheap conservative interval for any skeleton.

### 4.2 Enhanced Sphere Tracing (Keinert et al. 2014)

Three contributions: **safe over-relaxation** (step ×ω, ω ∈ [1,2), falling back to a normal sphere
step when consecutive unbounding spheres fail to overlap); dynamic self-intersection prevention when
converting signed distance *bounds*; and accelerating the intersection test for convex objects
inside convex bounding volumes. Read via secondary summaries (primary paywalled).

**Note the production counter-evidence:** Claybook measured over-relaxation and rejected it (§2.1) —
"random rollback" kills cache locality and it "bloats VGPR count." Barbier et al. make the same
general point: step-reduction heuristics come "at the cost of increasing single step computations,
mitigating the resulting gain."

### 4.3 Audit verdict

**Segment tracing: MATCHES-OURS — this is the strongest single validation in the survey.**

Our "segment marching over the candidate's [tEntry, tExit]" and their "candidate segment e with a
local bound λ(e)" are the same idea arrived at independently, from opposite directions: they want
*larger safe steps*; we want *exact partial evaluation* (a pruned sub-program is valid within the
segment because other subtrees are unreachable inside it by construction). **These two payoffs
compose** — the segment is simultaneously the pruning domain *and* the step-bound domain, and
nothing in either derivation conflicts.

That composition is the un-taken opportunity: we currently use the segment only for correctness
(pruning validity). Adding λ(e) over the *pruned* sub-program gives larger steps on top, and the
pruned program is cheaper to bound than the full one — the two mechanisms make each other better.

**Stealable parameters:** **κ ≈ 2** (their measured optimum — a directly usable default, not a
tuning starting point); the coarse-bound fallback ladder λ(e) → λ(S) → λ(∆) for operators where the
tight bound is impractical (our emitter can pick per-node at bake and bake the choice in); the
1-Lipschitz interval inclusion `[d(c)-r, d(c)+r]` for cheap conservative bounds on complex skeletons.

**Anti-steal:** over-relaxation (Keinert), on Claybook's measured evidence.

---

## 5. Per-region program pruning — Keeter (2020), Barbier (2025), Synchronized Tracing (2023)

This is the cluster that most directly overlaps our design, and it is worth being blunt: **our
"partial virtual recipes — boundary = sub-program" concept is a known, actively-published research
line.** That is good news (it validates the approach and hands us tuned parameters) and it also
means the *novelty* in our design lies elsewhere (see §8).

### 5.1 Keeter, MPR (SIGGRAPH 2020) — the tape/bytecode precedent

The expression is compiled to "a **linear tape** of operations which can be executed by an
**interpreter** running in a GPU thread." The tape is "an array of clauses" in GPU memory; clause
arguments reference **slots**, and "Treating the evaluator as a primitive virtual machine, slots are
equivalent to machine registers" — slot assignment "is equivalent to the problem of register
allocation." Slot reuse is aggressive: an example graph with nine clauses "uses only two slots."

The render loop is **hierarchical interval-arithmetic tile refinement with tape shortening at each
level**: 64×64 pixel (or 64³ voxel) tiles evaluated with interval arithmetic → inside/outside/
ambiguous; "For each ambiguous tile, we construct a **shortened tape** containing only parts of the
expression which are active in that tile's region"; subdivide with **a high branching factor**
(64→8→pixel), each level using the parent's shortened tape. High branching factor is chosen to map
to warp size and "prevents thread divergence."

**Reduction magnitude: "expression complexity decreases by two orders of magnitude between the
original and reduced expressions."** Benchmarks span a GT 750M (30 FPS class) to Tesla V100;
3D models exceed 40 FPS at 1024³ on a 1080 Ti class part.

Critically for us: MPR **interprets a tape rather than compiling a shader per model**, and the whole
system is designed so that "our evaluation and expression reduction both run efficiently as
massively parallel algorithms, **entirely on the GPU**" — no CPU round-trip, no per-model pipeline
compilation.

### 5.2 Barbier et al., Lipschitz Pruning (EG 2025) — the closest published art to our design

**Problem statement, verbatim:** "Rendering tree-based analytical Signed Distance Fields through
sphere tracing often requires to evaluate many primitives per tracing step, for many steps per pixel
of the end image. This cost quickly becomes prohibitive as the number of primitives that constitute
the SDF grows."

**Solution:** compute, per region of space, a **pruned tree equivalent to the full tree within that
region**. Two traversals:
- *Traversal 1 (post-order)* marks each node **INACTIVE** (doesn't contribute — prune subtree),
  **SKIPPED** (contributes but reduces to a child or its complement), or **ACTIVE**. The pruning
  test is one line: for a binary operator with blend radius k over a region of radius R centered at
  p, **`if |a′ − b′| > k + 2R`** then the operator reduces to one operand and the other subtree is
  inactive. The `2R` term is the Lipschitz inflation of the region — the whole method in one
  inequality.
- *Traversal 2 (pre-order)* computes global state (a node contributes only if active with no
  inactive ancestors), **rewires parents past skipped nodes**, and propagates complementary flags.

**Operator requirement:** each binary op must reduce to one operand (or its complement) when
operands are sufficiently far apart — met by hard union/intersection/difference *and* their smooth
variants with a **quadratic blending kernel φ(d,k) = (1/4k)·max(k−d,0)²** that vanishes at k. They
tabulate (cₐ, c_b, s) per operator: UNION (+1,+1,+1), INTER (+1,+1,−1), SUB (+1,−1,−1).

**Hierarchical scheme:** pruning every cell of a dense grid against the *full* tree doesn't scale
(unknown output size, linear in tree size × cells). Instead, **iteratively subdivide, and prune each
cell against its parent cell's already-pruned tree**. Grid hierarchy in their implementation:
**4³ → 16³ → 64³ → 256³** (4 levels). "at each step the pruned tree becomes smaller, allowing less
memory to be allocated at the next iteration."

**Far-field culling:** if `|f(p)| > C·R` for cell radius R, replace the entire tree with the constant
`sign(d)·(|d| − R)` — a conservative lower bound sharing the same 0-isosurface. **C = 2** in practice
("guarantees that the tracing takes at most 2 steps to cross a far-field cell"). Explicitly credited
as taking "inspiration from the narrow-band optimization commonly used in discrete SDF rendering
[Eva15, Aal18, …]" — i.e. from Dreams and Claybook.

**GPU layout:** three arrays — an **immutable node array** (the input tree, shared, never
duplicated), a **mutable node array** (per-cell pruned trees, double-buffered), each cell owning a
slice written in post-order indexing. Threads reserve slices by atomic on the active-node count
computed in traversal 2.

**Results (laptop RTX 4060, 1920×1080, 1 primary + 1 shadow ray):** speedups **up to two orders of
magnitude vs classical sphere tracing** — the paper's headline figure reaches **×629 on a
6023-node scene**. Pruning reduces to **≈1 active node per cell** for many scenes. Far-field culling
adds **up to ×2** on top. They compare against Keeter's parallel tape reduction and report better
scaling with node count.

**On interval arithmetic:** their Lipschitz criterion has "similar pruning capabilities" to affine
or interval arithmetic (their Figure 8 shows slightly *better*) while requiring **a single SDF
evaluation** instead of an interval interpreter — "without the overhead of interval arithmetic."
This is a direct, measured argument against the Dreams/MPR interval approach for SDFs specifically.

They also explain why **object-centric (bounding-volume) pruning fails for SDFs**: it "quickly
becomes impractical as smooth CSG [operators]… simply cannot be pruned" since SDF primitives have
*global* support. "our Lipschitz criteria is **space centric rather than object centric**."

### 5.3 Synchronized Tracing (2023)

A tile-based pipeline for BlobTrees: rasterize primitive volumes-of-interest into a **low-resolution
per-tile A-buffer** (linked list per screen tile, downsampled), then one **workgroup per screen
tile** processes rays with synchronized traversal so "Threads of a workgroup therefore access to the
blobtree in a coherent fashion." The A-buffer segments the tile frustum into subfrusta, and a
**sparse bottom-up (stackless post-order) tree traversal** prunes the blobtree on the fly, so
"field evaluation complexity [is proportional] to the number of primitive overlaps and not the full
blobtree size." Requires a strengthened *bounded blend* property so blend regions are localized in
space.

### 5.4 Audit verdict

**Our boundary pruning: MATCHES-OURS (independently reinvented, extensively published).
Our S0/S1 ladder: partially DIVERGES — and this is where we differ most interestingly.**

| Our concept | Literature twin | Verdict |
|---|---|---|
| Boundary → pruned sub-program (~200–400 of 1500 instructions) | Barbier per-cell pruned trees (→ ≈1 active node/cell); Keeter shortened tapes (−2 orders of magnitude); Synchronized-tracing bottom-up pruning | **MATCHES** — three independent published systems |
| Segment [tEntry,tExit] makes partial evaluation exact for hard unions | Barbier's `|a′−b′| > k + 2R` region test (2R = region inflation); segment tracing's λ(e) domain | **MATCHES**, but ours is *stated informally* where theirs is a proven inequality — **adopt the inequality** (steal #1) |
| Blends inflate boundaries by influence radius; statically merge partners | Barbier's `+k` term; Dreams' compact-support soft_min; Synchronized-tracing's bounded-blend requirement | **MATCHES** — universal agreement across all three |
| S0 bytecode, interpreted, partial-uploadable | Keeter's tape+slot VM (explicitly "a primitive virtual machine", slots = registers) | **MATCHES** — including the register-allocation framing |
| S1 compiled kernel promoted by hit counters | **APPARENTLY NOVEL** — no surveyed system promotes hot regions to compiled pipelines; all stay interpreted | **DIVERGES** (see below) |
| Hierarchical pruning (coarse → fine, reusing parent's pruned program) | Barbier's 4³→16³→64³→256³ cascade, each level pruning the parent's pruned tree | **IMPROVES-OURS** — steal #2 |
| Far-field: constant-expression replacement | Barbier's `|f(p)| > C·R → const`, C=2 | **IMPROVES-OURS** — steal #5 |

**On S1 (compile hot variants to real pipelines): genuinely divergent, and the literature leans
against us.** Every surveyed system interprets. Keeter's design goal was explicitly to keep
everything on the GPU with no compilation round-trip; Barbier's pruning is *per frame, per cell*,
dynamic enough for animation, which a compilation step could not track. Our S1 bet is different in
kind: our variants are **static families baked at content time** (recipe + boundary + LOD +
output-set), not per-frame spatial cells — so the compile happens at bake or on a slow promotion
path, and the pipeline count is a fixed budget per family. That is a defensible difference (we have
a bake step and a static family space; they have per-frame dynamic cells), and our W-SPLIT
register measurements (32 vs 18 registers) are the empirical case for it. **But it should be
labelled as an unvalidated bet, not as settled design** — and the honest fallback if S1 fails to pay
is that S0-with-hierarchical-pruning is already a published, measured ×100 win.

**Stealable parameters (this section is the richest):**
- **`|a′ − b′| > k + 2R`** — the exact conservative operator-skip test.
- **φ(d,k) = (1/4k)·max(k−d,0)²** — the quadratic blend kernel that makes the test sound.
- **(cₐ, c_b, s) flag table** per operator, with complementary flags stored on the *child*, so
  subtree sign inversion is a flag propagation rather than a rewrite.
- **4³ → 16³ → 64³ → 256³** four-level cascade; **C = 2** far-field constant.
- **Three-array GPU layout**: immutable input nodes (shared) + mutable per-cell pruned slices
  (double-buffered) + per-cell offsets. Our S0 partial-upload story should use exactly this shape.
- **Keeter's high branching factor sized to warp width** to prevent divergence; **slot/register
  allocation via last-use liveness** for the bytecode.
- **≈1 active node per cell** as the target to measure against — if our pruned sub-programs are
  200–400 instructions, that is *three orders of magnitude* more work per evaluation than Barbier
  achieves, which is the single most actionable number in this survey (see §7 steal #1).

---

## 6. Godot SDFGI, and hardware-RT + SDF hybrids

**Godot SDFGI** (Linietsky, Godot 4.0) is a DDGI variant that ray-marches an SDF cascade set instead
of requiring hardware RT — "Since SDFGI uses an SDF to ray-march the scene, it does not require
hardware accelerated ray-tracing." Cascades trade accuracy for distance, same clipmap idea as Lumen
at lower fidelity. **Read via docs/announcement only** (no engine-source reading).

**Audit: N/A to our virtual path** — it is a GI cache over materialized geometry, with no procedural
or per-region program pruning. Its one relevant data point is corroborative: **a second production
engine independently chose cascade/clipmap SDF over voxel cone tracing**, matching Lumen's stated
"voxel cone tracing — leaky" rejection.

**Hardware-RT + SDF hybrids.** The literature here is thin and mostly non-academic. What exists
confirms the mechanism our proxy contract assumes rather than improving on it: procedural (AABB)
geometry in a BLAS invokes an **intersection shader** when the ray hits the box, so hardware culls
only *misses* — the app defines the AABB, the shader defines the shape. A public reference
implementation of exactly our shape exists in LWJGL's Vulkan RT demos (`SdfBricks.java`: bricks as
procedural AABB geometry with an SDF evaluated in the intersection shader). Lumen's team states the
production constraint from the other side: "overlapping instances in a BVH are an issue as ray needs
to visit each one of them in order to find the closest hit and we have no ability to change this
acceleration structure."

**Audit: MATCHES-OURS, and our tiered escalation is better-specified than anything published.**
Our proxy contract's three tiers — (a) emitter-derived multi-AABB decomposition, (b) triangle proxy
hulls where *misses cost zero shader work*, (c) RT-as-broad-phase enumerating (ray, instance,
tEntry) into buckets — is a more complete treatment of the "AABB raises a candidate and you pay
immediately" problem than the surveyed material contains. Tier (b) in particular (trading procedural
AABBs for hardware-intersected triangle hulls to move work off the shader) did not appear in any
source read. Tier (c) is corroborated in spirit by Lumen's decision to abandon BVH traversal for
long rays and use grids for enumeration instead.

Our own W-RT Slice 1 finding — the **generate-min-tracking rule** (`rayQueryGenerateIntersectionEXT`
beyond the committed hit is app UB; NVIDIA implements generate as unconditional replace, so a naive
candidate loop is 97.8% correct and silently corrupts ~2% of rays) — is **not documented in any
source surveyed here**. It is a genuine production-critical finding.

---

## 7. Ranked steal list for the RECIPE / VIRTUAL arcs

Ranked by (expected win) × (cheapness), highest first.

### 1. Adopt Barbier's conservative operator-skip inequality as the pruning *proof*, and measure active-node count against their ≈1
**What:** replace our informal "other subtrees are unreachable inside the segment by construction"
with the explicit test `|a′ − b′| > k + 2R` (R = segment/region radius; k = blend radius), plus the
(cₐ,c_b,s) flag table and the quadratic kernel φ(d,k)=(1/4k)max(k−d,0)² that makes it sound.
**Why it's #1:** it turns a design assertion into a checkable bake-time invariant, it handles smooth
blends *correctly* (our current blend story — "inflate by influence radius and statically merge
partners" — is the right instinct but underspecified), and it comes with the measurement that should
alarm us: they reach **≈1 active node per cell**, while our target is 200–400 instructions per
boundary. Even allowing that our "cells" are boundaries rather than fine grid cells, that gap is the
biggest single perf lever identified in this survey.
**Seam:** the recipe unroller / emitter (bake-time), where boundary sub-programs are cut.

### 2. Hierarchical pruning: prune each level against the parent's already-pruned program
**What:** Barbier's 4³→16³→64³→256³ cascade, where level n+1's input is level n's *output*, not the
full tree. Memory shrinks per level; time shrinks per level.
**Why:** our m-boundary decomposition is currently **one level** (top-level boundaries → sub-programs).
Making it a cascade is the difference between ~5× and ~100× reduction, and it directly feeds the n±1
residency design (a coarser level's pruned program *is* the coarser LOD's program — the n axis and
the pruning cascade may be the same structure, which would collapse two mechanisms into one).
**Seam:** m-axis boundary decomposition + n-axis LOD emission; the "LOD-as-prefix" trick should be
re-examined in light of this (prefix cut vs cascade level are different factorizations of the same
thing).

### 3. Claybook's coarse cone-trace pre-pass (8×8 tiles) + analytic cone step
**What:** an 8×8-pixel outer-bounding-cone prepass producing a per-tile conservative tEntry
(measured at **0.2 ms** at both 720p/XB1 and 4K/Vega), plus the one-instruction analytic cone step
`t = (t+D)*A` with `A = C/(C−aperture)`, `C = sqrt(aperture²+1)`, and the termination criterion
`D < pixelConeWidth * t`.
**Why:** cheap, measured, and composes with (not replaces) our m×n variant selection — variant
choice picks the program, cone termination stops the march. Also gives the frame-time governor a
knob with known cost.
**Seam:** the traversal/shade split (W-SPLIT buckets) — prepass output is per-tile, consumed at
candidate enumeration.

### 4. Segment tracing's λ(e) over the *pruned* sub-program, with κ ≈ 2
**What:** compute the local directional Lipschitz bound over the candidate segment and step by
`|f(t)|/λ(e)`, amplifying the next candidate segment by κ ≈ 2 (their measured optimum). Use the
fallback ladder λ(e) → λ(S) → λ(∆) for operators where the tight bound is impractical, chosen per
node at bake.
**Why:** we already have the segment; we currently use it only for pruning validity. The bound is
*cheaper* on a pruned program than a full one, so #1/#2 make this better, and it attacks grazing
rays — the case both Evans and Aaltonen name as the worst (Evans: "edge pixels are waaaay harder
than surface pixels"; unbounded depth was what killed his refinement renderer).
**Seam:** the per-boundary variant march (segment marching over [tEntry, tExit]).

### 5. Far-field constant-expression culling (C = 2) + Lumen's 64-iteration cap
**What:** where `|f(p)| > C·R`, replace the sub-program with the constant `sign(d)·(|d|−R)`, C = 2.
Separately, cap marching iterations (Lumen: 64) and report a hit at current t on overflow.
**Why:** both are worst-case-bounding, which is what "scale-to-the-box" needs — the far-field
constant is worth **up to ×2** in Barbier's measurements, and the iteration cap converts a frame
spike into a bounded error. Cheapest items on this list.
**Seam:** the o-axis/output-slicing emitter (far-field is a degenerate output slice) and the march
loop.

---

## 8. First-principles audit — our concepts vs the literature

| Our concept | Verdict | Twin in the literature |
|---|---|---|
| **Proxy contract** (per-boundary conservative search geometry feeding TLAS/grid bins) | **Validated-by**, with our tiering apparently novel | Procedural-AABB + intersection-shader model (Vulkan/DXR; LWJGL `SdfBricks` reference impl); Lumen's froxel/light-grid object culling. **Tier (b) triangle proxy hulls — where misses cost zero shader work — appeared in no surveyed source.** |
| **Boundary pruning** (hit boundary k → run only k's pruned sub-program) | **Validated-by** (heavily — 3 independent published systems) | Barbier et al. 2025 per-cell pruned trees (×629, ≈1 node/cell); Keeter 2020 shortened tapes (−2 orders of magnitude); Synchronized-tracing 2023 sparse bottom-up pruning; Dreams' per-voxel edit lists (99.9% culling); Claybook's per-tile brush lists |
| **Segment marching** over candidate [tEntry,tExit] making partial evaluation exact | **Validated-by**, and **improved-by** on the bound | Galin et al. 2020 segment tracing (λ(e) over candidate segments, κ≈2); Barbier's `k + 2R` region inflation is the *rigorous form* of our correctness argument |
| **m axis** (boundary shapes) | **Validated-by** | Dreams' hierarchical edit-list refinement; Barbier's spatial hierarchy; Claybook's brush grid |
| **n axis** (LOD levels, footprint-selected, LOD-as-prefix) | **Partly validated; prefix-encoding NOVEL-IN-DOMAIN, not novel-in-form** — see H1 §8.1 | LOD-by-footprint is standard (Claybook, Lumen, Dreams). Prefix/refinement-stream encoding is **long-established for **geometry**: progressive meshes (Hoppe 1996) are exactly base + truncatable refinement-record stream; Nanite pages coarse-first with always-resident level 0. **Applying it to a program/instruction stream was not found.** Olano's shader LOD is the closest program-side kin and is *branching*, not prefix. |
| **o axis** (per-pass output slicing; shadow pass = density slice only) | **Partly validated — weaker novelty claim than first stated** — see H2 §8.2 | **Unreal compiles a per-(pass × vertex-factory) shader matrix from one material graph**, and opaque depth-only shaders are skipped/shared when a material can't change depth output — i.e. per-pass output specialization from one authored graph is shipping production practice. Our delta is the *third axis* and the derivation from one AST, not output slicing itself. |
| **S0 bytecode** (interpreted, partial-uploadable, free) | **Validated-by** (strongly) | Keeter's tape-of-clauses VM with slots-as-registers and last-use slot reuse; Barbier's mutable node array with per-cell post-order slices |
| **S1 compiled kernel** promoted by hit counters, pipeline count as budget | **Apparently novel — and the literature leans against it** | No surveyed system compiles per-region programs; all interpret, and Keeter's design explicitly avoids CPU round-trips. Our difference (static baked families vs per-frame dynamic cells) makes it *possible* where it wasn't for them, and our 32-vs-18 register measurement is the case for it — but this is an **unvalidated bet**, and S0 + hierarchical pruning is already a measured ×100 fallback. |
| **Instance binding pass** (boundary bounds as *formulas* over param slots, evaluated per instance, dirty-driven) | **Apparently novel — survived the disconfirmation hunt** — see H4 §8.4 | Closest kin: Lumen's per-frame instance loop computing required mip per asset + two-level instance-descriptor array; GPU Work Graphs procedural expansion; RT BLAS refit-on-animation. **All either use fixed per-asset bounds or recompute every frame from geometry; none evaluate baked bounds *expressions over instance parameters* on a dirty-driven schedule.** |
| **Delta ownership** (materialized bricks authoritative in their cells, can carve the virtual field; virtual segments clipped against delta coverage) | **NOVEL, and the hunt sharpened *why*** — see H3 §8.3 | Delta-over-procedural **storage** is standard (Godot Voxel Tools: "we only need to store edited voxels… non-edited regions can be recomputed on the fly"; No Man's Sky's edit buffers). But every such system **destructively materializes the edited block** — "if a block is edited, modifiers cannot affect it." The generator and the edit never coexist at render time. **Our render-time compositing of a live procedural field against materialized delta bricks, with per-segment clipping, is the genuine delta.** |
| **Grid as search index AND delta-ownership mask** (one load, two roles) | **Apparently novel** | Lumen uses separate structures for tracing (froxel cull grid) and updating (modified-brick list). Fusing them is ours. |
| **Scale-to-the-box** (continuous m×n quality; RT as ceiling, DDA as floor) | **Validated-by in parts** | Lumen's software/hardware RT duality and per-clipmap LOD; Godot SDFGI's explicit no-RT-required stance. The *continuity* (vs Lumen's hard 2 m switch) is ours, and is the harder engineering problem. |

**Summary for the user.** Of your eleven design concepts: **five are independently-reinvented known
art** (boundary pruning, segment marching, m axis, S0 bytecode, proxy contract) — strong validation,
and they arrive with tuned constants you can take directly. **Two survive a targeted disconfirmation
hunt as novel** (delta-ownership *render-time* compositing; the instance binding pass). **Two were
downgraded** by that hunt from "apparently novel" to "novel only in domain" (LOD-as-prefix) and
"partly validated" (o-axis output slicing) — see §8.1–8.4. **One is a divergent bet** (S1
compilation). And the single most actionable finding remains §7 item 1: Barbier reaches ≈1 active
node per cell where you are targeting 200–400 instructions per boundary.

### 8.1 H1 — LOD-as-PREFIX bytecode encoding · verdict: **NOVEL IN DOMAIN, NOT IN FORM**

*Hypothesis:* coarse-to-fine instruction ordering so LOD n is an (offset, count) cut of one stream is
novel.

**Disconfirming prior art found — in geometry, not programs.** The prefix/refinement-stream *form*
is thirty years old and canonical:
- **Progressive Meshes (Hoppe, SIGGRAPH 1996)** — a mesh is "a coarse base mesh together with a
  sequence of detail records that indicate how to incrementally refine" it. Truncating the record
  stream at any prefix yields a valid coarser mesh; this is *exactly* our (offset, count_n) cut, and
  the streaming motivation is identical ("the client can quickly display lower resolution LODs
  before the information associated with higher resolution LODs is received").
- **Nanite** allocates clusters "to 128KB pages based on spatial locality **and level in the LOD
  structure**", and "The first page contains the top level(s) of the LOD structure and is **always
  resident**" — coarse-first ordering with a resident prefix, which is also our n±1 residency window
  in a different guise.

**Closest program-side kin, and it diverges:** Olano & Kuehne, *Automatic Shader Level of Detail*
(Graphics Hardware 2003). It generates LOD levels of a *procedural shader* by simplification, but
the runtime representation is a **single shader with `autoLOD` parameter-driven conditionals** —
"threshold levels monotonically increase with each level of simplification and provide a simple means
to choose between levels of detail **within the shader itself**." That is branching, not a prefix
cut, and it carries every level's code at all times (they note "Like a progressive mesh, an LOD
shader contains all levels of detail" — the analogy is drawn explicitly, but the *encoding* is not
adopted).

**The delta:** nobody found applies prefix-truncation encoding to an **instruction stream** where the
truncation is what gets *uploaded* (partial residency of the program itself). Progressive meshes
truncate data; Nanite pages data; Olano branches over a fully-resident program. Our claim should
therefore be narrowed to: *"progressive-mesh-style prefix encoding, applied to the program rather
than the geometry, so LOD residency and program residency are the same mechanism."* That is a real
but **incremental** transfer of a known encoding to a new payload — not an invention. Practical
upside: Hoppe's and Nanite's machinery (geomorph blending across a prefix boundary, always-resident
level-0 page, spatial+level page packing) is directly transferable to our S0 upload path.

### 8.2 H2 — unified 3-axis pruning lattice (boundary × LOD × output) from one AST · verdict: **PARTLY VALIDATED — claim must be narrowed**

*Hypothesis:* the three-axis lattice derived from a single AST is novel.

**Disconfirming prior art, and it is production-scale.** Per-pass output specialization from one
authored graph is **shipping practice in Unreal**: a material compiles into an `FMaterialShaderMap`
organized as a **sparse matrix over (shader pass × vertex factory)** — "Shaders using
`FMeshMaterialShaderType` are pass specific shaders which depend on the material's attributes AND the
mesh type, and therefore must be compiled for each material/vertex factory combination." Each pass
shader accesses only the material inputs it needs, and Unreal explicitly *skips* caching a depth-only
shader when an opaque material's depth output is identical to the default — output-driven variant
elimination in production. Vulkan/Metal **specialization constants** provide the same effect at a
lower level via constant folding + dead-code elimination.

So **two of our three axes exist together in shipping engines** (Unreal: pass × vertex-factory).
Lumen and Claybook both independently confirm the *need* — each hand-builds a separate shadow path
because the cone never widens on parallel light rays.

**The delta, stated honestly:** (a) the **third axis** — spatial boundary pruning — is not part of any
shader-permutation matrix found; permutation systems specialize by *feature/pass/mesh-type*, never by
*region of space*. (b) Unreal's matrix is authored-by-convention (a programmer writes each pass's
shader against the material interface); ours is **derived**, all three axes emitted from one AST with
a bake-time signature check. (c) Nobody combines a spatial axis with a LOD axis and an output axis in
one lattice. Our claim should be *"a pruning lattice whose axes include a spatial one, all three
emitter-derived"* — not "per-pass output slicing is new."

### 8.3 H3 — delta-as-ownership render-time compositing · verdict: **NOVEL — and the hunt made the claim stronger, not weaker**

*Hypothesis:* compositing a live procedural field against materialized edit bricks, with per-segment
clipping and an ownership rule, is novel.

**Prior art exists for the storage model and is widespread — but it stops exactly where we begin.**
- **Godot Voxel Tools** implements delta-over-procedural storage: "we only need to store edited
  voxels (aka 'destructive' editing), while non-edited regions can be recomputed on the fly", with
  unedited blocks regenerated from a `VoxelGenerator` on access. **But editing is terminal for the
  block:** "if a block is edited, modifiers cannot affect it." The docs are explicit that edited and
  generated data "occupy separate pathways rather than being composited at the voxel level", and GPU
  normalmap generation "currently doesn't support edited voxels" and falls back to CPU.
- **No Man's Sky** stores terrain edits as a bounded set of edit buffers (community-documented as 255
  buffers with a visible edit budget) layered over regenerated procedural terrain — again a *save
  format* delta, with the voxel array reloaded from stored data on region re-entry.

**The distinction the brief asked me to check is exactly the load-bearing one.** Every system found
treats the delta as a **data-storage** optimization: the procedural generator is a *cheaper way to
obtain the initial voxel data*, and once an edit lands, the block is materialized and the generator
is out of the picture forever. **None of them evaluates the procedural field and the delta in the
same frame and composites them at render time.** There is no ownership rule because there is no
coexistence — materialization *is* their conflict resolution, applied eagerly at edit time.

**The delta:** ours keeps both live simultaneously — virtual segments clipped against delta coverage,
bricks authoritative in their cells and able to *carve* the virtual field — which is what allows
edits to be sparse and instanced content to stay unmaterialized. Lumen's static→dynamic brick
composite is the nearest mechanical cousin, but both of its operands are materialized data. **Verdict
stands: this is the most novel element of the architecture,** and the reason is now precisely
statable rather than an absence of search hits.

*Caveat:* I could not read the GDC Vault video for McKendrick's NMS talk (video-only, paywalled), so
the NMS characterization rests on community/technical secondary sources. If NMS does composite at
render time, that talk is where it would be said.

### 8.4 H4 — per-instance boundary-formula binding pass · verdict: **NOVEL (survives)**

*Hypothesis:* baking boundary bounds as *formulas over instance params* and evaluating them in a
dirty-driven per-instance pass is novel.

**Candidates checked, all diverge:**
- **Lumen** runs a per-frame shader over all instances computing required mip per asset, feeding
  streaming — closest in *shape* (per-instance derived quantity driving residency), but the bounds
  themselves are fixed per asset and baked at import; the per-instance value is a *LOD selection*,
  not a *geometry bound*.
- **RT BLAS refit** (e.g. `VK_NV_cluster_acceleration_structure` animated-clusters sample) updates
  acceleration structures per instance, but by **recomputing from deformed vertex data every frame**,
  not by evaluating a bounds expression over parameters.
- **GPU Work Graphs procedural generation** (AMD) hierarchically culls procedural content, but uses
  **fixed quantized grid bounds** and is "fully recomputed each frame using work graphs' dynamic
  scheduling. Rather than dirty-driven updates" — the opposite of our dirty-driven binding pass on
  both counts.
- Houdini-style per-instance parameter jitter exists in authoring tools but drives *instantiation*,
  not conservative render-time bounds.

**The delta:** the combination of (i) bounds and feature scales baked as **expressions over named
param slots**, (ii) evaluated **per instance on a dirty schedule** (spawn/param-change, not per
frame), and (iii) **structural variation expressed as masking over a family-shared program set** so
pipeline budget is per-family rather than per-instance — was not found in any surveyed source. The
closest published systems all pick one of: fixed per-asset bounds, or full per-frame recomputation.
**Verdict: novel.** Confidence is moderate rather than high — this sits at the intersection of
procedural-content and RT-acceleration literature, and negative search results are weaker evidence
than the positive hits that settled H1 and H2.

---

## 9. Sources

- Evans, A. *Learning from Failure: A Survey of Promising, Unconventional and Mostly Abandoned Renderers for 'Dreams PS4'*. SIGGRAPH 2015 Advances in Real-Time Rendering. https://advances.realtimerendering.com/s2015/AlexEvans_SIGGRAPH-2015-sml.pdf
- Aaltonen, S. *GPU-Based Clay Simulation and Ray-Tracing Tech in 'Claybook'*. GDC 2018. https://media.gdcvault.com/gdc2018/presentations/Aaltonen_Sebastian_GPU_Based_Clay.pdf
- Wright, D. et al. *Lumen: Real-Time Global Illumination in Unreal Engine 5*. SIGGRAPH 2022 Advances. https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf
- Barbier, W., Sanchez, M., Paris, A., Michel, É., Lambert, T., Boubekeur, T., Paulin, M., Thonat, T. *Lipschitz Pruning: Hierarchical Simplification of Primitive-Based SDFs*. Computer Graphics Forum 44 (EG 2025), e70057. https://hal.science/hal-05050831v1 · https://wbrbr.org/publications/LipschitzPruning/
- Galin, E., Guérin, E., Paris, A., Peytavie, A. *Segment Tracing Using Local Lipschitz Bounds*. Computer Graphics Forum 39(2), 545–554 (EG 2020). https://hal.science/hal-02507361 · code: https://github.com/aparis69/Segment-Tracing
- Keeter, M. *Massively Parallel Rendering of Complex Closed-Form Implicit Surfaces*. SIGGRAPH 2020 / ACM TOG. https://www.mattkeeter.com/research/mpr/keeter_mpr20.pdf · code: https://github.com/mkeeter/mpr
- *Synchronized-Tracing of Implicit Surfaces*. arXiv:2304.09673. https://arxiv.org/pdf/2304.09673
- Keinert, B., Schäfer, H., et al. *Enhanced Sphere Tracing*. STAG 2014. https://www.lgdv.tf.fau.de/publications/enhanced-sphere-tracing/
- Hart, J. C. *Sphere Tracing: A Geometric Method for the Antialiased Ray Tracing of Implicit Surfaces*. The Visual Computer, 1996.
- Godot SDFGI. https://godotengine.org/article/godot-40-gets-sdf-based-real-time-global-illumination/ · https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_sdfgi.html
- Quilez, I. Soft shadows in raymarched SDFs. https://iquilezles.org/articles/rmshadows/
- LWJGL Vulkan RT demo, `SdfBricks.java` (procedural-AABB SDF bricks + intersection shader). https://github.com/LWJGL/lwjgl3-demos/blob/main/src/org/lwjgl/demo/vulkan/raytracing/SdfBricks.java
- Decaudin, P., Neyret, F. *Volumetric Billboards*. (Cited by Evans as the precursor line.)

**Added for the §8.1–8.4 novelty adjudication:**
- Hoppe, H. *Progressive Meshes*. SIGGRAPH 1996; *View-Dependent Refinement of Progressive Meshes*, SIGGRAPH 1997. https://people.eecs.berkeley.edu/~jrs/meshpapers/Hoppe.pdf — base mesh + truncatable refinement-record stream (H1 disconfirming prior art).
- Olano, M., Kuehne, B., Simmons, M. *Automatic Shader Level of Detail*. Graphics Hardware 2003. https://userpages.cs.umbc.edu/olano/papers/aslod.pdf — shader LOD via `autoLOD` parameter-driven conditionals; explicitly draws the progressive-mesh analogy but branches rather than prefix-cuts (H1).
- Karis, B. et al. *Nanite: A Deep Dive*. SIGGRAPH 2021 (via course notes). https://cs418.cs.illinois.edu/website/text/nanite.html — 128KB pages by spatial locality **and LOD level**, always-resident first page (H1).
- Epic Games. *Shader Development in Unreal Engine* (`FMaterialShaderMap`, pass × vertex-factory permutation matrix, depth-only shader sharing). https://dev.epicgames.com/documentation/unreal-engine/shader-development-in-unreal-engine (H2 disconfirming prior art).
- Pettineo, M. *The Shader Permutation Problem, Part 2*. https://therealmjp.github.io/posts/shader-permutations-part2/ — permutation axes, ubershader-vs-specialization tradeoff, specialization constants (H2).
- Zylann. *Godot Voxel Tools* documentation — generators & smooth terrain. https://voxel-tools.readthedocs.io/en/latest/overview/ — destructive delta-over-procedural storage; "if a block is edited, modifiers cannot affect it" (H3 disconfirming prior art, and the reason our claim survives).
- McKendrick, I. *Continuous World Generation in 'No Man's Sky'*. GDC 2017. https://www.gdcvault.com/play/1024265/Continuous-World-Generation-in-No — **video-only, not viewed**; NMS edit-buffer characterization rests on secondary/community sources (H3 caveat).
- AMD GPUOpen. *Work Graphs Mesh Nodes: Procedural Generation*. https://gpuopen.com/learn/work_graphs_mesh_nodes/work_graphs_mesh_nodes-procedural_generation/ — hierarchical per-frame frustum culling on fixed grid bounds, explicitly not dirty-driven (H4).
- nvpro-samples. `vk_animated_clusters` (BLAS refit from deformed vertices per frame). https://github.com/nvpro-samples/vk_animated_clusters (H4).
