---
title: GigaVoxels Techniques Digest (thesis-grounded)
tags: [research, streaming, caching, cone-tracing, lod, wavefront, gigavoxels]
created: 2026-08-07
status: research
supersedes: GigaVoxels-Streaming.md
---

# GigaVoxels Techniques Digest — per-technique, mapped to VIXEN (2026-08)

**What this is.** A per-technique digest of the GigaVoxels line of work, read from primary
sources and mapped against VIXEN's *current measured* architecture. It supersedes
[[GigaVoxels-Streaming]], which summarized only the 2009 conference paper and framed the whole
body of work as "a streaming system for later." That framing is wrong in an important way: about
half the load-bearing ideas in the thesis are **frame-scale locality and LOD discipline**, not
streaming, and those are exactly what our measured bottleneck wants.

## Sources actually read (be precise about this)

| Source | Status | What was read |
|---|---|---|
| **Crassin PhD thesis (2011), 207pp** — *GigaVoxels: A Voxel-Based Rendering Pipeline For Efficient Exploration Of Large And Detailed Scenes* | ✅ **FULL TEXT, complete** | Downloaded from `maverick.inria.fr/Membres/Cyril.Crassin/thesis/CCrassinThesis_EN_Web.pdf` (6.9 MB). Extracted and read: full TOC (151 entries), Ch. 3 (pipeline), Ch. 4 (pre-integrated cone tracing) in full, Ch. 5 (data structure) in full, Ch. 6 (rendering/traversal) in full, Ch. 7 (out-of-core, pp. 117–155) in full, Ch. 8 (soft shadows / DoF / instancing), Ch. 9 (voxel cone-traced GI/AO), Appendix A (GPU characterization). |
| **Crassin et al., I3D 2009** — *GigaVoxels: Ray-Guided Streaming…* | ✅ **FULL TEXT, complete** | Downloaded from `maverick.inria.fr/Publications/2009/CNLE09/CNLE09.pdf`, 8pp, read in full — chiefly to identify what the thesis *superseded* (see "Request buffer" below). |
| **theses.hal.science PDF mirror** | ❌ fetch failed | HAL is behind an Anubis anti-bot gate; returns an HTML denial page, not the PDF. Irrelevant — the Inria copy is the same document. |
| **Richermoz & Neyret 2024, GigaVoxels DP (HPG)** | ⛔ not re-read this session | Already summarized in [[GigaVoxels-Streaming]] from a user-supplied PDF (2026-07-05); that summary is carried forward unchanged below and is *not* re-verified here. |
| OpenGL Insights / GPU Pro chapters | ⛔ not fetched | Not needed — the thesis is a strict superset for every technique in the brief except "beam optimization" (see that entry: it is **not a GigaVoxels technique at all**). |

Section numbers cited below are thesis sections unless marked *(2009)*.

---

## 1. N³-tree + brick pool layout

**(a) What it is.** §5.1–5.2. Space is subdivided by a *generalized* octree ("N³-tree": each node
splits into N³ children, N=2 being a plain octree), and every non-empty, non-constant node carries a
pointer to a **brick** — a small dense M³ voxel grid held in a 3D texture. Children are stored as
contiguous **node tiles** of N³ nodes, so a node needs only *one* child pointer, not eight. A node is
exactly two 32-bit words: word 1 = {terminal bit, 30-bit child-tile index}, word 2 = {flag choosing
constant-value vs brick, 30-bit brick reference}. The two words live in **separate arrays (SoA)**,
because descent and data-fetch touch them in different phases and splitting them improves coalescing
and texture-cache hit rate (§5.2.1). Brick references are 3×10-bit XYZ brick-grid coordinates, not
byte addresses, which is what lets 30 bits address a 2 TB brick pool.

**(b) Verdict: SUPERSEDED — our layout is the same family, arrived at differently.** VIXEN runs
ESVO (Laine–Karras) octrees with 8³ bricks plus a flat `brickGridLookup` grid. The Laine–Karras node
is already the compact "one child pointer + child mask" encoding; our flat brick grid is the *degenerate
N³-tree with N=grid-size at one level*, which is precisely the memory/traversal tradeoff §5.1.3 describes
(large N ⇒ shallow tree ⇒ fewer dependent hops). The composed-traversal ruling — DDA over the flat
grid as leaf traversal, ESVO for data access/mips — is a deliberate divergence *in the same direction*
the thesis argues for.

**(c) Worth stealing.**
- **SoA node split.** §5.2.1 splits child-pointer and data-pointer into separate arrays *for cache
  reasons*. Worth auditing whether our node fetch pulls both halves down one cache line when the
  descent phase only needs one — this is a pure-locality change against a latency-bound frame.
- **⚠️ The brick-size data contradicts our 8³ choice.** §5.3 measures storage: **4³ corner-centered
  bricks are optimal** for both test scenes (Sponza: 68.5 MB bricks + 1.9 MB octree = 6.88% of a dense
  512³ pyramid). §6.3.2 measures speed on the same axis: **4³ = 141 FPS, 8³ = 128 FPS, 16³ = 110 FPS** —
  smaller bricks are *faster too*. Reason (§6.3.2 / Fig. 6.16): with small bricks cost is dominated by
  octree traversal (75% at 4³), with large bricks by brick marching (83% at 16³, much of it stepping
  through empty voxels inside the brick). Ch. 9 consequently uses 3³ corner-centered.
- **Corner-centered vs node-centered voxels** (§5.1.4). Node-centered needs a 1-voxel replicated border
  on every side; at 8³ that is a **1.95× memory multiplier** (Fig. 5.6), at 4³ node-centered it is 3.38×.
  Corner-centered halves the border cost (8³ → 1.42×) at the price of needing a 3³-Gaussian MIP filter
  instead of a box filter. Our 8³ bricks pay the border tax; whether we pay 1.95× or 1.42× is worth
  checking.
- **Brick-neighbor interpolation constraint** (§5.1.4, Fig. 5.7): if any voxel on a brick's boundary is
  non-empty, the *neighbor node at the same LOD must also have a brick*, or interpolation near the
  boundary is wrong. Relaxable to "neighbor may be one level coarser" with imperceptible artifacts. This
  is a real correctness trap for any mip-tier work with a perfect-parity gate.

---

## 2. Ray-guided request buffer + usage buffer (the residency core)

**(a) What it is.** §7.3.3. The central trick, and it is much simpler than the 2009 paper's version.
Two flat GPU arrays: a **request buffer** sized 1:1 with the page table (same layout, so entry *i* of
the page table maps to slot *i*), and a **usage buffer** sized 1:1 with the data pool. A ray needing an
absent page writes **the current frame's timestamp** into its request slot; a ray *using* a resident page
writes the timestamp into that page's usage slot. Because every writer writes the *same* value, there is
**no race and no atomic** — concurrent identical writes are benign, and the scheme is **self-deduplicating
by construction** (many rays wanting the same page produce one flagged slot). Using timestamps rather
than booleans also means **the buffers never need clearing**: "flagged this pass" ⇔ "stamp == current
pass". A stream compaction then turns the sparse flagged buffer into a compact request list, and the
compaction's *output length* is exactly the number of pages to load.

For comparison, the *2009 paper* did this the hard way: rays wrote up to 12 node indices into 3 MRTs,
exploited a 2×2 spatial-coherence pattern plus a temporally shifted 48-element FIFO window to get more
nodes out, then ran a neighborhood de-duplication pass and a **HistoPyramid** stream reduction before
reading back to the CPU, which owned a mirrored tree. The thesis discards all of it. When the
`GigaVoxels-Streaming` note said the thesis is "materially simpler and more precise," this is the delta.

**Cost, measured** (§7.5.1): interacting with both cache interfaces from inside the ray-casting kernel
adds only **5% to render time**; cache management (LRU + request handling) is **4% of frame time** and
**constant** regardless of churn.

**(b) Verdict: CONTENT-SCALE LATER — but the *format* is the thing to copy, and it costs nothing to
adopt early.** VIXEN's data fits VRAM at demo scale, so there is nothing to page. The plan already names
the W3 cell table as the future request signal. What matters now is that when we do build it, we build
*this* version and not the 2009 one.

**(c) Worth stealing.**
- **The exact format**: request buffer 1:1 with the page table; a `uint32` frame timestamp, not a bool;
  no atomics; no clear-per-frame. This is the single highest-value paragraph in the thesis.
- **The one-bit multiplexing trick** (§7.3.8): node-cache and brick-cache requests share *one* request
  buffer, discriminated by a single bit, because a given ray issues one kind or the other, never both in
  a pass. Conflicts cost a one-pass delay, which they explicitly accept to avoid an atomic. Halves the
  buffer and the memory traffic.
- **Priority variant** (§7.3.3, §7.3.5): swap the timestamp for an *atomic counter* of how many rays
  requested the page; sort the compacted list by it and serve only the top *n* per frame. This gives a
  hard per-frame streaming budget with the highest-screen-footprint pages served first. Note the honest
  tradeoff they state: counters *require* atomics and *require* clearing both buffers each pass — so it
  costs real performance and is "not used uppermost." Adopt only when a budget cap is actually needed.
- **Two-pass compaction optimization** (§7.3.8): one cheap compaction over the whole shared buffer to
  extract all valid requests, *then* the per-cache compaction over that much smaller buffer. They report
  stream compaction was "one of the most costly operations" in cache management — this halves it twice.

---

## 3. LRU / page management (fully GPU-side)

**(a) What it is.** §7.3.4. An **LRU page list** — one 32-bit page reference per page in the pool,
ordered most-recently-used first. Sorting it every frame would be prohibitive, so instead they
**incrementally re-sort with two order-preserving stream compactions**: compact the used entries into
U⁺, compact the unused into U⁻, concatenate U⁺ ∥ U⁻. Because each compaction preserves relative order,
U⁻ keeps the oldest at the tail — so concatenation *is* the sort, in two linear parallel passes. To
insert *n* new pages you take the last *n* entries; they are the true LRU victims, and their addresses
are fetched in parallel by the loading threads.

Eviction requires an **invalidation pass** (§7.3.7), because a recycled page may still be referenced by
page-table entries: step 1 flags each recycled page (writing the flag *into the usage buffer* using a
reserved value of zero, so invalidation costs **no extra memory**); step 2 runs one thread per page-table
entry, nulling any entry pointing at a flagged page. Must run *after* production, because the producer is
allowed to decline to produce or to alias an existing page.

**Measured**: GPU-side LRU management is **1.7×–27.5× faster than CPU-side**, the advantage growing with
pool size (§7.5.4). Total management overhead is **13.84 MB of bookkeeping for 448 MB of managed data —
3.08%** (§7.3.8), for a pool of 110592 8³ bricks (432 MB) + 262144 node tiles (16 MB).

**(b) Verdict: CONTENT-SCALE LATER (the streaming arc).** No eviction pressure at demo scale. Recorded
so that when planets outgrow VRAM we do not reinvent a CPU-mirrored LRU — which is the failure mode the
thesis spends §7.1.1 dismantling, and which the *2009 paper itself* still had.

**(c) Worth stealing.** The two-compaction incremental sort (it is the whole trick — no sort primitive
needed); the reserved-zero invalidation flag riding in the usage buffer; and the sizing datapoint —
**~3% bookkeeping overhead** is the budget to hold ourselves to.

---

## 4. Brick marching + empty/constant-space skipping

**(a) What it is.** §6.1.5–6.1.6. Inside a brick, plain regular-grid ray marching with hardware trilinear
fetches. Step size is a *fraction of the interpolated voxel size*: with anisotropic pre-integrated voxels
you step exactly one voxel (the pre-integration length matches), with isotropic voxels you must step
**d = ⅓·V** and correct opacity for the shorter path, α′ = 1−(1−α)^(Δ′ₓ/Δₓ) (§4.6.3). Marching stops on
leaving the node, on opacity saturation, or when the needed LOD leaves the range spanned by the two
currently-held bricks. Empty/constant nodes are not marched at all: the constant value is **analytically
integrated** over the entry→exit distance and skipped in one step.

Two skipping refinements: (i) **the coarse brick is sampled first** — if the parent-level sample is fully
transparent, a step of at least one *coarse* voxel is provably safe, which is how they defend large bricks
against wasted texture fetches; (ii) optionally store distance-to-nearest-non-empty per voxel and skip by
it, proximity-clouds style — free when the content is already an SDF.

**The interpolation trap (§6.1.6, Fig. 6.9):** you may **not** skip a constant node cleanly. Its parent's
brick contains that constant value, but a sample taken in the parent near the child's boundary interpolates
it against a *neighbouring* non-constant voxel. So a border of **half a parent-brick voxel** must still be
sampled, blending the constant against the parent brick. Skipping it produces visible artifacts. Likewise
the big skip step must not jump past the LOD range the constant node covers.

**(b) Verdict: FRAME-SCALE NOW — partly SUPERSEDED, one correctness item live.** Our DDA leaf traversal
over the flat grid already *is* empty-space skipping, and our recipes are SDFs, so the proximity-cloud
variant (ii) is native rather than an add-on. The coarse-first-sample trick (i) is a direct argument for
the far-field mip tier: it says the coarse level pays for itself as a *skip oracle*, not just as an
antialiasing source.

**(c) Worth stealing.** The **half-voxel border rule** is the one to act on: it is exactly the class of
defect that a perfect-parity gate catches and that costs a day to diagnose. If the in-flight far-field mip
tier skips or coarsens at a constant/empty boundary, check this before blaming the descent. Also worth
noting the ⅓-voxel step rule and the opacity power-correction — our stepping constant should be a
*derived* function of voxel size with a named justification, not a tuned magic number.

---

## 5. Pre-filtered mipmaps + pre-integrated cone tracing

**(a) What it is.** Ch. 4, the theoretical heart. Aliasing is framed correctly: a pixel is a **cone**, not
a ray, so the fix is not more rays but a representation that can be *pre-integrated over a footprint*.
Surfaces (B-reps, not linearly filterable) are replaced by a **density distribution** whose per-voxel
quantities *are* linearly filterable: pre-integrated transparency T and in-scattered energy Q, stored in a
3D MIP pyramid at a length-of-integration *l* tied to voxel size (s = l², §4.3). A cone is then traced with
**one ray**, varying the sampled MIP level so each sample's pre-integrated footprint matches the cone's
cross-section at that distance (§4.3.2) — reconstruction is quadrilinear (trilinear + across-level).

They are candid about the approximations: T evolves *exponentially* along the ray and between MIP levels,
so linear interpolation is formally wrong in both — accepted for hardware-filtering speed, "negligible in
most cases." The model rests on a **decorrelation hypothesis** (§4.4): densities within a beam must be
roughly uncorrelated at scales larger than a sub-path. It is violated by long aligned structures — a wall
seen edge-on — which **over-estimates opacity**, making filtered objects look slightly fat. Bounded by
roughly one pixel for thin cones; **grows with cone aperture**, which is the standing warning for every
wide-cone use (DoF, AO, GI).

Shading is factored *out* of the pre-integration (§4.5): store material color (linear, filters freely) and
a **normal distribution function** instead of a normal — as a **Toksvig lobe**, mean vector D with variance
recovered from its shortened length, σ² = (1−|D|)/|D|. RGB is stored opacity-weighted (αC) so interpolation
does not bleed transparent colors. At shade time the NDF, the BRDF lobe, and the **view-cone lobe**
(σᵥ = cos ψ, ψ = cone aperture, §4.5.3) are convolved together.

The **isotropic vs anisotropic** split (§4.6.3–4.6.4): plain averaging into a parent voxel causes the
"two red-green walls" problem — two opaque differently-colored walls average to a semi-transparent
blend, and a half-filled 2³ block averages to half-transparent regardless of view direction. Fix: store
**6 directional channels** (one per major axis), built by integrating in depth then averaging the 4
transverse values; at render time interpolate the 3 channels nearest the view direction. Costs only
**1.5× memory**, because directional values are needed *only* for non-leaf levels.

**(b) Verdict: FRAME-SCALE NOW — this is the theory under the in-flight far-field mip tier, and it
predicts our parity risk.** Our `raySizeCoef`/`raySizeBias` cone-LOD function is exactly §4.3.2's
"vary the MIP level along the ray by footprint," and the plan already reuses it for both traversal and
accumulation-cell selection — the thesis's "one LOD policy, two consumers" principle, independently
derived. Notably the wavefront plan *also* independently derived Toksvig variance-carry ("an averaged
direction vector shortens, and |Σdir|/count is a variance measure — feed it as a roughness widen"). That
is §4.5.2 verbatim, which is a good sign for the design and means the citation is now available.

**(c) Worth stealing.**
- **The perfect-parity gate is theoretically at risk from the decorrelation hypothesis, not from
  arithmetic.** §4.4 says a *correct* implementation still over-estimates opacity on aligned structures,
  and the error **scales with cone aperture**. If far-field parity fails on edge-on planar content
  specifically, that is expected physics of the model, not a bug in the descent — decide the ruling on
  that basis rather than chasing it.
- **Interpolate *two* bricks, not one** (§4.3.1, §6.1.4): continuous LOD requires the current node's brick
  *and its parent's*, so the descent must **retain the parent** at every step. Our footprint-selected mip
  tier should hold both or accept level-quantization popping.
- **αC opacity-weighted color storage** — cheap, prevents a whole artifact class.
- **The 6-channel anisotropic voxel at 1.5× memory** is the known fix if the far-field tier shows the
  red-green-walls artifact (blocky half-transparent silhouettes at distance). Non-leaf levels only.
  **BAKED, 2026-08-08:** `libraries/SVO/include/MipAnisoPool.h` — 6-axis directional coverage per
  (node, channel), additive into `ShellOctreeGpu.h` (coarse interior nodes only, zero cost elsewhere),
  bake-time self-checks green (slab asymmetry, cube isotropy, threshold-level gating, serialized
  byte size — `test_mip_aniso_pool.cpp`). No traversal-time consumer yet — awaits the deep-field
  policy's regime 3 (COSMIC accumulation); see
  [[../01-Architecture/Deep-Field-Mip-Accessor-Policy-2026-08]].
- **σᵥ = cos ψ** as the view-cone lobe when convolving with roughness.

---

## 6. Cone-aperture soft shadows and AO (the shadow-wave mapping)

**(a) What it is.** §8.3.1 and §9.5. A shadow is a second cone, not a second ray. Its apex is at the light,
its radius is set by the **impact volume** — the intersection of the *primary* cone with the surface, which
is a volume, not a point — so a cone containing both the light and that volume approximates the area-light
integral. The LOD along the shadow trace is driven by the *shadow* cone's radius, not the pixel cone's.
Accumulate opacity front-to-back, stop at saturation. The headline property, stated twice: **blurrier is
cheaper**, because a wider cone reads coarser MIP levels — the exact inverse of the multisampling
cost curve, and the same reason DoF (§8.3.2) gets *faster* as it defocuses.

AO (§9.5) is the same primitive fanned out: A(p) = (1/N)·Σᵢ Vc(p, Ωᵢ) over **~5 cones** on the hemisphere,
accumulating occlusion only, with a distance falloff **f(r) = 1/(1+λr)** so nearby occluders dominate.
Full GI (§9.7) adds one **tight cone in the mirror direction whose aperture is derived from the specular
exponent**, which is how glossy reflections come out of the same machinery. Both use deferred shading so
the cones are only traced for visible fragments.

**(b) Verdict: FRAME-SCALE NOW — plugs directly into the existing shadow wave.** We already have the
W1a shadow wave (`ShadowRayTrace.comp`, one thread per fixed slot, `invalid ⇔ tmax≤tmin`), and we already
have the cone-LOD function. The missing piece is that our shadow rays are *rays* at mip 0, so they pay full
near-field traversal cost for a result that is going to be blurred anyway. Giving each shadow ray an
aperture and letting it read coarser mips is a **latency reduction on the exact axis we are bound on** —
fewer, shallower dependent hops per shadow query — and it is nearly free to wire because the shadow wave
is already a separate kernel with its own LOD freedom.

**(c) Worth stealing.**
- **Aperture from the impact volume**, not from a constant. The primary cone footprint at the hit is
  already computed by our cone-LOD function; the shadow cone's radius should be seeded from it, which
  makes softness automatically correct with distance.
- **~5 hemisphere cones for AO**, and **f(r) = 1/(1+λr)** as the falloff. Concrete starting parameters.
- **Specular cone aperture derived from the roughness/specular exponent** — one cone, glossy reflection,
  no stochastic noise. Relevant to the recipe shading kernels.
- The **smooth-not-noisy** argument (§4.7): cone tracing's error is a *bias*, stochastic sampling's is
  *variance*, and for perception bias beats noise. Directly relevant to how much ReSTIR noise we should
  be willing to trade away in the far field.

---

## 7. GPU producers — procedural bricks on demand

**(a) What it is.** §7.4. Requests are answered by a user-defined **GPU producer**: a kernel launched with
**one thread group per requested page**, so pages are filled in parallel *and* the threads within a group
cooperate on one page via shared memory (§7.3.6). Three kinds: **loading** (from host memory), **procedural**
(generated on the GPU from a function — fractals, or voxelizing a mesh already in VRAM), and **mixed**
(load then amplify, e.g. add Perlin noise to a scanned dataset). The producer is handed exactly two things:
*what* (page-table index) and *where* (pool index from the LRU list), and it **returns** the index actually
used — which lets it (i) decline to produce, reporting "this whole brick is one constant value" so a
constant is stored in the node instead of a brick, or (ii) **alias an existing page**, which is how brick
and subtree **instancing** works (§8.1–8.2: thousands of instanced voxel trees, a Menger sponge implemented
as a *recursive pointer* in the octree — infinite resolution from one node).

For procedural producers the page index carries no spatial meaning (the pool is arbitrarily ordered), so a
**localization buffer** supplies it: a **localization code** of 3×10 bits (one 10-bit sequence per axis,
bit *n* = the child chosen on that axis at level *n*) plus an 8-bit **depth**. Stored **per node-tile**, not
per node — the missing last bit is recoverable from the node's address within the tile, an 8× saving.

Two performance findings: GPU-pulled transfers (CUDA zero-copy, coalesced, from inside the producer) reach
**~half of theoretical PCIe bandwidth**, versus **1/40th** (18³ bricks) to **1/5th** (66³ bricks) for
CPU-issued per-brick copies (§7.5.3) — small copies are dominated by driver latency and texture-format
conversion. And because memory latency dwarfs ALU, **decompression inside the producer is effectively
free** (§7.4.3) — the arithmetic hides entirely under the transfer.

**(b) Verdict: FRAME-SCALE NOW *and* CONTENT-SCALE LATER — the highest-fit technique in the whole thesis
for our recipe architecture.** Our recipes are procedural SDFs that will supply derived proxy geometry
across an m-boundary × n-LOD × o-output variant family. That *is* a procedural producer: a function from
(spatial extent, LOD) to a brick. The producer interface is the missing formalization of the recipe→brick
materialization seam, and it comes with the two features we will certainly need — **"this brick is
constant, store a value instead"** (empty-space compaction falls out of recipe evaluation for free) and
**page aliasing for instancing** (repeated content — a forest, a station module, a fractal — costs one
brick).

**(c) Worth stealing.**
- **The producer contract**: (what, where) in, **actual-index-used** out, with the two escape hatches
  (null = "constant, no brick needed"; other-index = "alias this existing page"). Copy this signature.
- **One workgroup per brick, cooperating via shared memory.** This is the same shape as W-L1's staging
  discipline, on the production side. Group size chosen to share destination-address computation across
  the page.
- **Localization code: 3×10 bits + 8-bit depth, stored per node-tile.** A compact, proven encoding for
  "which region of space does this page represent" — needed the moment recipes generate bricks on demand.
- **Mixed producers**: load coarse + amplify procedurally is precisely "authored planet + recipe detail."
- **Decompression is free under transfer latency** — an argument for storing recipe inputs compressed.

---

## 8. Out-of-core / host streaming

**(a) What it is.** §7.4.4. Deliberately the *least* developed part of the thesis — Crassin states plainly
that disk out-of-core "was not the priority" and that the real problem of the era is virtualizing **video**
memory, not disk. The disk tier is a second cache in system memory with a simple **FIFO** policy (not LRU).
Mechanism: a node-tile loaded from host memory has null child/brick pointers, and its localization info
holds its *host* address. When a child is requested, the producer checks host memory; if present it loads
it; if absent it **does nothing at all** — and the request simply recurs next frame. Meanwhile the request
batch is also read back to the CPU each pass (small, cheap), which triggers a fully **asynchronous** disk
load. Nothing blocks; the frame degrades to coarser data and converges. Demonstrated on 64 GB and 256 GB
datasets at interactive rates.

A neat storage trick (§7.4.3): for the host-memory tier, a page-table entry that is *not* resident stores
**the host address in place of the null pointer**, with one flag bit distinguishing the two. Source location
tracking at **zero** memory overhead.

**(b) Verdict: CONTENT-SCALE LATER — the streaming arc, and mostly *not* our problem shape.** Our content
is procedural: the "disk" is a recipe evaluation, so the tier that matters is producer throughput, not I/O.
The transferable idea is the **failure discipline**, not the storage.

**(c) Worth stealing.** The **"producer declines, request recurs"** protocol is the right backpressure
model for scale-to-the-box: on a weak iGPU, the producer serves what its budget allows and silently drops
the rest; rays fall back to coarser mips; quality converges when motion stops. That is *exactly* the
"smooth degradation" product requirement, expressed as a protocol rather than a heuristic — and it needs
no scheduler, only that missing data has a defined coarse fallback. Also steal the **address-in-place-of-null**
trick if we ever need a residency tier.

---

## 9. Multi-pass refinement, update strategies, and LOD transition quality

**(a) What it is.** §7.2. Because production is deferred to a separate pass, a ray hitting missing data has
two options, and the choice is a **quality/latency dial**:
- **Real-time strategy** — one render + one update pass per frame; rays *never* stall, they use whatever
  coarser brick exists. Priority goes to **bricks over subdivisions**, guaranteeing a coarse brick always
  exists so a complete image is always producible. Cost: slower convergence, because coarse bricks get
  loaded that the final resolution won't need.
- **Quality-first** — multiple interleaved render/update passes per frame; rays **suspend** into a state
  buffer and resume next pass. Priority goes to **subdivision over bricks**, reaching correct resolution
  faster. In 2009 this suspend/resume was implemented with MRT state + Z-cull to mask completed rays.
- **Balanced** — N quality-first passes then one real-time pass, N chosen from the remaining frame budget.

Refinement is strictly **top-down, one level per pass**, which is what guarantees occluded nodes are never
built (§7.2.1). Sequence cost (§7.5.1, Mandelbulb): rendering 56% of frame at 1× speed / 34% at 4×; cache
management a *constant* 4% / 2.5%; node loading 1.5%; **brick production 30% at 1× and 57% at 4×** — i.e.
production, not management and not rendering, is what a fast camera costs you. Cache hit rate is excellent:
**2.6% misses with a 512 MB brick cache, 5.9% with 128 MB** (§7.5.2) — a 4× smaller cache barely more than
doubles the miss rate, which is the strongest single argument that ray-guided demand loading does not
thrash.

**(b) Verdict: CONTENT-SCALE LATER for the paging, FRAME-SCALE NOW for the *dial*.** The
progressive-refinement machinery presupposes something to page. But the **budget dial itself** — "N quality
passes then one guaranteed-complete pass, N from remaining frame time" — is the shape of our
scale-to-the-box requirement, and it is a scheduling policy we could express against the recipe producer
without any residency system.

**(c) Worth stealing.**
- **"Bricks before subdivisions" as the real-time invariant.** The reason is subtle and general: it
  guarantees a *coarse-but-complete* representation always exists, so there is always something to fall
  back to. Any LOD scheme with a smooth-degradation requirement wants this invariant.
- **The 2.6%/5.9% miss-rate datapoint** — the number to beat, and evidence that no predictive prefetching
  is needed. They note explicitly they use **no prefetching whatsoever** and still get this.
- **The cost split** (management constant and tiny; production dominant under motion) tells us where to
  put the budget knob when we build it: on the producer, not the cache.

---

## 10. Megakernel vs split passes — the thesis contradicts our W-SPLIT

**(a) What it is.** §6.1.3, and *(2009)* §5.1 says the same. They **tried** splitting octree descent and
brick marching into two kernels communicating through video memory, explicitly to reduce register pressure
and improve branch coherence — and it was **slower** than the single big kernel. Stated reason: the
round-trip of per-ray state through memory costs hundreds of cycles of latency, and keeping that state in
registers beat the occupancy gain. This was measured on SM4-generation hardware (G80/GTX280 era).

**(b) Verdict: NOT APPLICABLE as a conclusion — but it is a real warning, and it names the exact cost the
wavefront plan must keep paying attention to.** Our W-SPLIT went the other way and shipped bit-exact, and
the plan's own inventory notes the gain ("traversal carries no shading state, shading no traversal state —
occupancy improves in both") along with the risk ("wavefront trades ALU coherence for VRAM traffic; on
bandwidth-poor GPUs it can LOSE"). The thesis is a 2011 datapoint for that risk *on hardware where L2 was
small and the register file was the only fast storage*. Two things changed: modern GPUs have large L2 and
much better memory-level parallelism, and our splits are motivated by **per-recipe specialization**
(compiling a thin kernel per recipe), which is a code-divergence win the thesis never had available.

**(c) Worth stealing.** Not a technique, a **measurement discipline**: they split, measured, and reverted.
Our plan's demand for honest A/B on both a recipe-diverse *and* a simple scene is the same instinct. Worth
recording that the one prior team to try this split found it lost, so the burden of proof stays on the
split — which is exactly what the plan's gates already impose.

---

## 11. Beam optimization

**(a) What it is.** A low-resolution pre-pass renders a conservative distance image; the full-resolution
pass then starts each ray at the distance its neighborhood already proved empty, skipping the empty run
from the near plane.

**(b) Verdict: NOT A GIGAVOXELS TECHNIQUE — the brief attributes it to the wrong source.** It appears
nowhere in the thesis (searched the full extracted text: no "beam optimization", no "pre-pass"/"prepass",
no low-resolution depth pre-pass) and nowhere in the 2009 paper. It is **Laine & Karras (2010), *Efficient
Sparse Voxel Octrees*** — the ESVO paper — where it is a named optimization. The GigaVoxels equivalents of
the same idea are different mechanisms: rasterizing a **proxy surface** to supply ray origins and exit
points (§6.1, and §6.2 for depth-buffer-bounded rays in mixed rasterized scenes).

**(c) Verdict against us: SUPERSEDED — we already have it, from the correct source.** Our **B1 depth proxy
+ HiZ tile pyramid** is shipped and default-on, and it is precisely the screen-anchored conservative-start
pyramid. The thesis's contribution here is only the reminder in §6.2 that when compositing with rasterized
geometry, seeding each ray's accumulated RGBA *from the existing color buffer* lets opacity saturate — and
terminate — immediately, which is free occlusion culling for any raster content we composite against.

---

## 12. Anisotropic texture-cache behavior (Appendix A) — an underrated finding

**(a) What it is.** §A.1. They benchmarked 1000 texture fetches/thread of ray-casting-shaped access through
a 512³ RGBA8 volume, sweeping view direction. Results are **strongly anisotropic**: with trilinear filtering
on a 3D texture, the same work costs **12.20 ms** along the best axis order and **50.00 ms** along the worst
— a **4.1× spread purely from ray direction**, because cache lines are not cubical. Also: **3D textures beat
layered-2D by 1.74×** on average with full trilinear, and manual Z interpolation in the kernel is far worse
(52.79 vs 30.36 ms avg). §A.2 separately reverse-engineers G80 fragment scheduling: 16×16 screen tiles bound
to texture processors, warps assembled from a per-TP FIFO, and **one slow fragment stalls every subsequent
tile on that TP** while other TPs drain — the original "one bad ray poisons the warp" measurement.

**(b) Verdict: FRAME-SCALE NOW — this is a direct lead on our measured bottleneck.** We measured 0.39% SM
issue, L2 at 25%, DRAM idle: the frame is waiting on *dependent load latency*, and Appendix A says a
meaningful fraction of voxel-fetch latency is **a function of traversal direction relative to the memory
layout**. That is a knob we have never turned. It also independently motivates W-L1: if cache-line geometry
is anisotropic and hurts by up to 4×, then staging a brick into shared memory removes the dependence on
cache-line geometry entirely.

**(c) Worth stealing.** The **experiment**, not the numbers (they are Fermi-era). A directed A/B of our
march cost against camera orientation over a fixed scene is a cheap measurement that would either surface a
several-× locality effect or rule it out — and it fits the standing measurement protocol without new
instrumentation. If the effect reproduces, Morton/swizzled brick-pool storage becomes a candidate lever.

---

## 13. GigaVoxels DP (Richermoz & Neyret 2024) — carried forward, not re-verified

Carried from [[GigaVoxels-Streaming]] (user-supplied PDF, 2026-07-05; **not re-read this session**):
even the GPU-native cache design retains a starvation "tail regime" from alternating discrete render and
production passes, which "can sometimes represent more than half of the total time." Their fix is **CUDA
Dynamic Parallelism** — a ray that misses launches its own production kernel inline, with no CPU round-trip
and no pass boundary. Measured **1.1×–4.4×, average 2.1×**, largest on high-disocclusion scenes. The authors
flag Vulkan portability as unsolved.

**Verdict: NOT APPLICABLE (portability), but the *diagnosis* is FRAME-SCALE relevant.** We are Vulkan, and
there is no ray-query-callable dynamic parallelism. But "the tail of a pass is where the GPU idles" is
exactly the shape of our own W-BASE finding (busy-wait at 0.39% SM issue). Our structural answer to pass-tail
starvation is the bucketed indirect dispatch, which fills the tail with *other buckets' work* rather than
launching nested kernels — arguably the more portable solution to the same problem.

---

## Ranked steal list — the 5 highest-leverage items for VIXEN

1. **Cone-aperture shadow/AO cones in the existing shadow wave** (§8.3.1, §9.5). Give each shadow ray an
   aperture seeded from the primary hit's cone footprint and let it read coarser mips. It attacks the
   measured bottleneck directly — fewer and shallower *dependent* hops per shadow query — costs no new
   buffers (the wave and the cone-LOD function both already exist and the wave already has independent LOD
   freedom), and the thesis's headline property is that **blurrier is strictly cheaper**, which no
   ray-based shadow method can offer. Start with ~5 hemisphere cones and f(r)=1/(1+λr) for AO.

2. **The GPU producer contract, adopted now as the recipe→brick seam** (§7.4, §7.3.6). Formalize recipe
   brick materialization as *(what, where) → actual-index-used* with the two escape hatches: null meaning
   "this brick is a constant, store a value not a brick" (free empty-space compaction straight out of SDF
   evaluation) and other-index meaning "alias an existing page" (instancing — a forest costs one brick).
   This is the highest-fit technique in the thesis for our architecture, it is *frame*-scale useful before
   any streaming exists, and defining it now means the eventual streaming arc plugs in rather than
   retrofits. Take the 3×10-bit + 8-bit-depth per-node-tile localization code with it.

3. **The brick-size and centering evidence, re-tested against our 8³** (§5.3, §6.3.2, Fig. 6.16). The
   thesis measures **4³ faster than 8³ faster than 16³** (141/128/110 FPS) *and* 4³ corner-centered the most
   compact, with the cost decomposition explaining why (small bricks ⇒ traversal-dominated, large ⇒
   marching-dominated with wasted empty steps). Ch. 9 went all the way to 3³. Our 8³ was inherited from
   Laine–Karras, not measured. Given a latency-bound frame this is a cheap, high-information A/B — and the
   corner-centered variant separately cuts the border memory multiplier from 1.95× to 1.42×.

4. **The direction-anisotropy measurement** (§A.1). A **4.1× spread in texture-fetch cost purely from ray
   direction** is a latency effect we have never measured, on the exact axis we are bound on. One directed
   A/B — march cost vs camera orientation on a fixed scene, within the standing protocol, no new
   instrumentation — either surfaces a multi-× locality lever (pointing at swizzled brick storage, and
   independently strengthening W-L1's shared-memory staging, which makes cache-line geometry irrelevant)
   or cleanly rules it out.

5. **The flat timestamped request-buffer format, banked for the streaming arc** (§7.3.3, §7.3.8). When
   W3's cell table becomes the request signal, build *this*: request buffer 1:1 with the page table, a
   `uint32` frame timestamp instead of a bool — **no atomics** (identical concurrent writes are benign),
   **self-deduplicating**, **never cleared**. Plus the one-bit trick to share one buffer between node and
   brick requests. It measured at 5% added render cost and ~3% memory overhead, and it is the thing the
   2009 paper got wrong (MRT node-lists + HistoPyramid + CPU-mirrored tree) and the thesis got right — so
   recording the *right* version now is what stops us reimplementing the wrong one from the older, more
   widely-cited source.

**Deliberately not on the list:** LRU/eviction (nothing to evict at demo scale), disk out-of-core (our
"disk" is a recipe), beam optimization (already shipped as B1, and not a GigaVoxels technique), and the
megakernel-beats-split finding (2011 hardware, opposite motivation from our per-recipe specialization —
but it stands as a reason to keep the burden of proof on the split, which the plan's gates already do).

## Related

- [[GigaVoxels-Streaming]] — the 2009-only note this supersedes; keep for the 2024 DP summary
- [[../01-Architecture/Sparse-Mip-ESVO-LOD-Inc1-Plan-2026-07]] — our independently-derived sentinel/fallback design
- `docs/plans/2026-08-04-wavefront-recipe-shading.md` (undertow) — W-L1 lever, far-field mip tier, composed traversal
