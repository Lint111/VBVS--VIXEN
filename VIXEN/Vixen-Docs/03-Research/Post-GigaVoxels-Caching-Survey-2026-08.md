# Post-GigaVoxels Caching Survey (2013–2025)

**Status:** 📐 REFERENCE · written 2026-08-07 · scope: sparse-voxel GPU rendering literature
**AFTER** GigaVoxels (2009 paper / 2011 thesis), read specifically for **caching, residency,
and locality** decisions in VIXEN's voxel renderer.

> A sibling digest covers the original GigaVoxels 2009 paper + 2011 thesis. This document
> deliberately does **not** re-derive those; it starts where they stop and asks what the
> field learned in the 13 years since.

---

## Why this survey exists, and the measurement it must answer to

VIXEN's caching design is not yet finalized, and the temptation with a 2009-era reference is
to import its whole architecture. The W-BASE stall decomposition (plan
`docs/plans/2026-08-04-wavefront-recipe-shading.md`, "W-BASE stall decomposition CLOSED",
2026-08-05) makes that a mistake, because it tells us precisely which resource is scarce:

| instrument | reading |
|---|---|
| SM instruction issue | **0.39% of peak** (ALU 0.19 / FMA 0.12) |
| L1TEX | 0.8% |
| DRAM | **0.8% — idle** |
| L2 | 25.2% (the accumulate's global atomics) |
| warps resident | 35.6% |
| warp slots empty while SM active | 53.5% |
| all sync (semaphores/barriers/16 submits) | 0.043 ms |

**Verdict on record: ~99% of the 24 ms frame is busy-waiting on a dependent-load latency
chain** (ESVO pointer-chase + atomic round-trips) at half occupancy. Nothing is bandwidth-
or capacity-bound; the demo working set already fits in L2.

That single fact reorders the entire literature. Most famous voxel-caching work is about
**fitting data that does not fit** — compression, streaming, residency. At demo scale we do
not have that problem. What we have is a **latency** problem, and the papers that speak to it
are a different, smaller set. This survey therefore grades every technique twice: on its own
terms, and against *our* measured bottleneck.

Verdict vocabulary used below:

- **FRAME-SCALE NOW** — attacks dependent-load latency or divergence at demo scale; name the seam.
- **CONTENT-SCALE LATER** — belongs to the streaming arc, mandatory when planets outgrow VRAM.
- **SUPERSEDED** — the field moved past it.
- **N/A** — sound work, wrong problem for us.

---

## 1. The SVDAG family — deduplicated, pointer-free hierarchies

### 1.1 Sparse Voxel DAGs (Kämpe, Sintorn, Assarsson, SIGGRAPH 2013)

**Mechanism.** An SVO's voxels are defined by *paths*, not by node identity — so two subtrees
with identical childmask configurations are interchangeable and can share one instance. The
tree becomes a DAG. Reduction is bottom-up: merge identical leaves (at most 2⁸ = 256 unique
leaf nodes), update parent pointers, repeat one level up until no merges remain; guaranteed
minimal within L iterations (§3). Sorting uses the child-pointer set as a 256-bit key.

**Node layout (§3, Fig. 3).** Childmask and pointers are both 4 bytes; only pointers to
non-empty children are stored, consecutively after the mask. **Node size 8–36 bytes**,
variable with child count. The two lowest levels store 4³ subtrees **pointer-free in a single
64-bit integer** — the majority of nodes, made pointerless.

**Results (§5.2, Table 2).** Node-count reduction of 28× (HAIRBALL, no visible regularity) to
576× (CRYSPONZA). EPICCITADEL at 128K³ = 19 billion voxels in **945 MB**, where an SVO needs
5.1 GB *without counting pointers* (Fig. 1). Traversal at 170–240 MRays/sec on a GTX 680.

**Traversal (§4).** "The main traversal loop closely resembles that of Laine and Karras
[2010a]" — the DAG change "requires only minor changes in code." No decompression step.
Beam optimization at 8×8 pixels, extended to soft shadows.

**On cache behavior — the honest reading.** The paper reports no cache measurements. What it
does establish is that **deduplication is not free for locality**: a DAG node is reached from
many parents, so siblings-in-space are not siblings-in-memory. The SSVDAG paper later states
this explicitly (§1.2 below). Deduplication buys *capacity*, and pays in *scatter* — exactly
the wrong side of our trade.

**Limitation the paper names.** Geometry only. "We do not attempt to compress material or
reflectance properties" (§2) — attributes break the merge, because two subtrees with identical
occupancy but different colors are no longer identical.

> **VERDICT: N/A at frame scale; CONTENT-SCALE LATER, and even then with caution.**
> A DAG solves capacity, which our 0.8%-DRAM frame does not lack, and it worsens
> pointer-chase scatter, which is exactly what we are latency-bound on. It also collides with
> our procedural-SDF direction: recipe-materialized bricks are generated on demand and carry
> per-recipe attributes, so the dedup premise (identical *and* attribute-free subtrees) is
> weak for us. Revisit only if authored (non-procedural) voxel content at extreme resolution
> becomes a shipping requirement.

### 1.2 SSVDAGs — symmetry-aware, variable-bit-rate pointers (Villanueva, Marton, Gobbetti, I3D 2016)

**Mechanism.** Two additions over SVDAG. (a) Merge subtrees identical *under mirror symmetry*
— 8 reflections encoded in 3 bits (X/Y/Z), order-independent and self-inverse; a canonical-form
table of 256 entries maps to 46 canonical representatives (§4.1). (b) Exploit the skewed
reference distribution with **variable-bit-rate pointers**.

**The pointer encoding (§4.2, Fig. 6)** — the transferable part. A 16-bit node header holds
8 children × 2 tag bits:

| tag | meaning | stored |
|---|---|---|
| `00` | null | **0 bytes** |
| `01` | short pointer | **16 bits** = 3 transform bits + 13-bit offset |
| `10` / `11` | long pointer | **32 bits** = 3 transform + 29 offset, tag's low bit supplying the top offset bit |

Level-words are 2 bytes (inner) / 8 bytes (leaf), so a short pointer reaches the first 2¹⁴
bytes of an inner level or 2¹⁶ bytes of the leaf level. Nodes are **reordered per level by
reference count**, most-referenced first, so the hottest nodes fall in the short-pointer
window — measured skew: "the most common 10% of the nodes is referenced by nearly 90% of
pointers at level 14" (Fig. 5). Reordering costs ~4% of conversion time. Node size drops to
**4–34 bytes**.

**Results (§6.3, Table 2).** Powerplant 64K³: **85.8 MB at 0.123 bits/voxel**, vs SVDAG 167.3
MB and SVO 1660.3 MB. Averaged: USSVDAG = 79.6% of SVDAG storage (symmetry alone),
SSVDAG = 52.4% (symmetry + pointer compaction).

**The cache statement (§6.4) — the single most useful sentence in the DAG literature for us.**
Measured slowdowns vs SVDAG: symmetry alone costs **1–2%**, pointer compression costs
**14–16%**. The authors' explanation:

> "the extra computation required for implementing reflections is well hidden by memory
> latency, [while] the more elaborate memory layout of pointer compression is more costly."

That is a clean natural experiment: **ALU work is free under latency hiding; memory-layout
complexity is not.** They also note (§2) why standard tree tricks don't rescue it — page
grouping and relative indexing "are not applicable to our DAGs, since children are scattered
throughout the structure due to sharing."

> **VERDICT: SUPERSEDED as a structure for us — but the §6.4 finding is FRAME-SCALE NOW as a
> design rule.** In a latency-bound kernel, spend ALU freely and guard memory layout jealously.
> This is direct support for W-L1's shared-memory staging (moving a hop from ~200-cycle L2 to
> ~20–30-cycle shared) and direct evidence *against* any scheme that makes a node fetch
> arithmetically cheaper but structurally scattered.

### 1.3 HashDAG — interactive edits on compressed voxels (Careil, Billeter, Eisemann, CGF 2020)

**Mechanism.** Replace the DAG's flat array with a **hash table over node contents**, so an
edit can insert nodes without recompressing. 32-bit pointers at word granularity ⇒ 16 GB
address space, partitioned into per-level regions, each split into fixed-size buckets. A
node's bucket = a P-bit hash (MurmurHash3) of its child mask + child pointers; **lookup =
hash, then linear search within the one bucket**. Nodes never span pages: a node that doesn't
fit the remaining page space moves to a fresh page.

**Parameters (§4 + repo `src/typedefs.h`).** Page size **512** words; **2¹⁶ buckets** on levels
≥10, **1024** near the root; bucket sizes 1024 (top) / 4096 (low) uint32. Measured occupancy at
(64k)³: level-12 buckets hold ~600 words (~120 nodes), std-dev 60, range 327–903 — "less than
half of the maximum bucket size."

**Edits.** Depth-first descent bounded by the edit's volume; modified nodes rebuilt bottom-up;
**old nodes are never mutated**, so a new root per edit gives undo/redo for free. A pre-created
full node at every level makes fills scale sublinearly. Edit cost: **milliseconds** for 10¹⁵-voxel
scenes with multi-million-voxel edits. GC: 25 s for Epic Citadel (64k)³, "currently unoptimized."

**The traversal-cost finding (§3.2, §4) — the part that matters here.** Hashing is an *edit-time*
cost only: "During traversal, however, no hashes need to be computed, limiting the overhead of
the HashDAG to that arising from the extra indirection due to the virtual memory," and because
nodes never straddle pages, "we only need to perform one address translation per node during
traversal." Even so, the measured raytracing overhead is **1.5×–2×** vs a DAG without virtual
memory (Fig. 6), with color resolution adding "overhead of similar magnitude."

> **VERDICT: N/A now; a cautionary data point for any future indirection layer.**
> One extra indirection per node — with the hash cost entirely removed from the trace path —
> still cost 1.5–2×. In a latency-bound traversal, **an indirection level is expensive even when
> its arithmetic is free**, because it lengthens the dependent chain. Any residency scheme we
> build that inserts a page-table hop into the *near-field* path should be assumed to cost on
> this order until measured otherwise. This is a strong argument for our W-L1 direction of
> *shortening* hops rather than *adding* a translation layer to the hot path.

---

## 2. The direct GigaVoxels successors — GVDB and NanoVDB

These are the two systems that inherited GigaVoxels' brick-pool lineage and are closest kin to
our coarse-grid + brick design. Their traversal (HDDA) is essentially the composed-traversal
ruling arrived at independently.

### 2.1 GVDB Voxels (Hoetzlein, HPG 2016)

**Topology.** A VDB configuration vector gives log2 resolution per level: `<5,4,3>` = 32³ top
divisions, 16³ mid, **8³ leaf bricks** (§4.1). The spec subsumes others: `<1,1,1,1,1>` is an
octree, `<9,4>` a two-level hierarchy with a 512³ index and 16³ bricks. **This is our design
space, stated as a parameter.**

**Memory pools (§4.2, Fig. 3).** Two pool groups per level. P0 holds fixed node records; P1
holds child lists. **Node header padded to 40 bytes**:

| field | bytes |
|---|---|
| Level | 1 |
| Position (index space) | 12 |
| Value (brick position in atlas) | 12 |
| Parent (index in P0ᵢ₊₁) | 4 |
| Child list (index in P1ᵢ) | 8 |
| bitmask | res(i)³/8 |

Child lists are sized `K·res(i)³` with K ≈ ½–⅓ of maximum, because "VDB topology is already
so compact — requiring less than 1% (average 5 MB) of memory compared to the atlas." 500k leaf
bricks ⇒ ~20 MB of topology. Child location is by **64-bit popcount on the bitmask** — the
same popcount-indexing idiom the brickmap thesis uses for colors.

**Bricks live in a 3D texture atlas**, explicitly "for efficient hardware trilinear filtering
and texture cache access" (§4.1). **Apron voxels** (§4.3) are handled by enlarging the atlas,
populated by a kernel using an inverse atlas→world mapping — GVDB adds them because OpenVDB's
per-voxel cache-efficient iterators "do not map well to GPU parallelism where we would like
interior voxels of a brick to perform the same amount of work per thread as boundary voxels."

**HDDA traversal (§5, Algorithm 1).** Branchless 3D-DDA (Amanatides) with mask/comparison
operators instead of branches. Descend on an active bit, restart the DDA at the child with the
current t; ascend when `t >= tExit[lev]`. Crucially: **one set of DDA variables reused at all
levels** — "Simply unrolling each tree level with multiple sets of DDA variables is ineffective
as **kernel register pressure becomes a limiting factor**." A **short stack of exit points**
only, restored on step-up; the root is never revisited until traversal ends.

**Brick-size result (§6.1, Fig. 8).** The tradeoff, measured: "Small bricks achieve the best
occupancy but place a burden on rendering as the DDA spends more time in entry and exit and
less time doing useful work. Large bricks achieve the best rendering performance but at the
cost of occupancy." Recommendation: **64³ or higher once data exceeds 2048³**.

> **VERDICT: FRAME-SCALE NOW — three specific transfers.**
> (1) The **one-DDA-variable-set, register-pressure** finding is directly on our critical path:
> our stall decomposition names the register ceiling (specialized kernels' 32-reg testimony) as
> a lever, and GVDB independently found per-level DDA state unaffordable. Our composed traversal
> should keep a single DDA state + short exit stack, not per-level state.
> (2) The **brick-size curve** is the tuning input for W-L1's staging budget: bigger bricks =
> fewer entry/exit transitions = shorter dependent chains, at an occupancy cost. Our 8³ bricks
> sit at the small end of GVDB's curve; if staging makes a brick fetch cheap, a larger
> brick-cluster staging unit is the lever the curve predicts.
> (3) **Apron voxels for parallel uniformity** — worth pricing if/when brick-local filtering or
> neighbor stencils enter the shade kernels, because the alternative (branchy boundary handling)
> costs exactly the divergence our wavefront epoch exists to remove.

### 2.2 NanoVDB (Museth, SIGGRAPH 2021 Talks)

*Read from the canonical header, `nanovdb/NanoVDB.h` v32.9.2 — it specifies the layout more
precisely than the two-page talk. The talk text itself was not reachable (ACM 403); no measured
ReadAccessor speedup or render benchmark is quoted here for that reason.*

**Single contiguous, position-independent buffer** (header "Memory layout", L67–121):

```
[GridData(672B)][TreeData(64B)]---[RootData][N × Root::Tile]---[Internal<5>]---[Internal<4>]---[Leaf<3>]---[blind data]
```

**Branching 5,4,3 confirmed** (L4635–4645): upper internal 32³ tiles, lower internal 16³, **leaf
8³ = 512 voxels** — the same leaf brick size as ours and as GVDB's default.

**Byte sizes.** GridData 672 B, TreeData 64 B. Masks dominate internal nodes: upper node carries
**two 4096-byte bitmasks** (value + child, 32768 bits each), lower node two 512-byte masks; the
**leaf has only a 64-byte value mask, no child mask**. Tiles are a union of `ValueT` or
`int64_t child`, where child is a **self-relative byte offset** ("child-pointer = Tile::child +
this") — which is what makes the buffer memcpy-able to GPU without pointer fixups.

**Ordering is breadth-first and it is a declared flag, not a convention:**
`GridFlags::IsBreadthFirst = 1 << 5` (L333), the default constructor argument (L1968), queryable
via `isBreadthFirst()`, and combined into
`isSequential<NodeT>() { return NodeT::FIXED_SIZE && this->isBreadthFirst(); }` (L2299) — i.e.
breadth-first + fixed node size ⇒ **each level is a flat indexable array**. (The header states no
prose cache claim for this; the benefit is structural.)

**Alignment: 32 bytes**, `NANOVDB_DATA_ALIGNMENT 32`, with a `static_assert` on every node struct
and an explicit warning that a misaligned buffer is UB — "Normally this is not a concern on GPUs,
because they use 256 byte aligned allocations, but the same cannot be said about the CPU."

**ReadAccessor — the caching mechanism worth stealing (L5252–5402).** The default caches **all
three levels** (`ReadAccessor<BuildT,0,1,2>`): 3 cached node pointers + 3 keys, 68 bytes total
(44 with the single-key option). A hit is a masked compare against the cached node's origin:

```cpp
bool isCached(const CoordType& ijk) const {
  return (ijk[0] & int32_t(~NodeT::MASK)) == mKeys[NodeT::LEVEL][0] && ... ;
}
```

`get<OpT>` probes **leaf → lower → upper → root** — bottom-up from the *most local* cached node —
and only falls back to a full root-down descent on a miss, repopulating the cache on the way down.
Header guidance: "since a ReadAccessor caches previous access patterns it is by design not
thread-safe, so use one instantiation per thread (it is very light-weight)."

**HDDA (`math/HDDA.h`).** The DDA is constructed with a **`dim`** and snaps to that level's grid:
`mVoxel = RoundDown(pos) & ~(dim-1)`. A level change is `update(ray, dim)`, which early-outs if
`dim` is unchanged and otherwise **re-snaps the same DDA state** — ascending/descending is a
re-snap, **not a stack push**. The descend decision comes from `acc.getDim(ijk, ray)`, which
returns the dim of the deepest node containing ijk, descending only through set child-mask bits
and caching each node on the way. Empty/tile regions return a large dim (4096/128/8) so the DDA
takes one giant step; only inside a leaf does dim reach 1.

> **VERDICT: FRAME-SCALE NOW — the highest-value structural steal in this survey.**
> The **ReadAccessor is a per-thread inline cache for a pointer-chase**, and a pointer-chase is
> precisely our measured bottleneck. It exploits the fact that consecutive samples along a ray
> (or across a bucket of rays hitting the same brick) overwhelmingly resolve inside the *same*
> nodes, so the common case costs a masked compare against a register-resident key instead of a
> root-down chain of dependent loads. Under W-L1's bucket-by-brick key, coherence is *higher*
> than NanoVDB's assumed case, because the bucket is built to guarantee it.
> The **stackless re-snap HDDA** is independently the same shape as our composed traversal
> (DDA as leaf traversal, ESVO as data access) and confirms the register discipline GVDB found:
> no per-level state, no stack push on level change.
> **32-byte alignment** and **breadth-first-by-level ⇒ flat indexable arrays** are cheap layout
> disciplines to adopt for any node array we author.

---

## 3. Brickmap-style two-level grids — the closest production kin

### 3.1 van Wingerden, "Real-time Ray tracing and Editing of Large Voxel Scenes" (Utrecht MSc, 2015)

This is the closest published analogue to our coarse `brickGridLookup` + 8³ brick design, and
it is the most complete published specification of a GPU-request streaming loop for voxels.

**Brickmap (§3.1.1, Fig. 10).** Brick **8×8×8**, "empirically determined a good size," equal in
all dimensions because fine detail "can be random in nature." Layout is exactly **71 bytes**:
`solid mask 512 bits | color pointer 32 bits | LOD color 24 bits`. **1 bit per voxel occupancy,
one flat 512-bit mask — no sub-brick hierarchy.** Colors live separately, DXT1, ~4 bits/voxel,
max 256 bytes per brick. **Color index = popcount of set bits below the voxel's bit index**
(§4.1) — the same popcount-indexing idiom as GVDB's child lookup.

**Brickgrid cell — 32 bits, three states (§3.1.2, Fig. 11):**

| state | encoding |
|---|---|
| loaded | 32-bit brickmap pointer |
| empty | null pointer |
| **unloaded** | **24-bit LOD color + 8-bit flags** |

Two runtime flags matter: *unloaded* and *already-requested*. Individual bit positions are not
given in the thesis.

**Third layer for empty space (§3.4).** One bit per cell above the brickgrid, node size **4×4×4
brickgrid cells** (one bit covering 64 pointers), for a **2048:1** memory ratio. The author's own
noted weakness: perf degrades on larger brickgrids and "having multiple extra layers above the
brickgrid can fix this problem" — not implemented.

**Traversal (§3.3).** 3D-DDA at *every* level, top bit-grid → brickgrid → brickmap. **No stack** —
explicitly cited as the advantage over SVOs, where the stack "is a bottleneck for SVOs on the
GPU" (§5.4.1). No cache measurements are offered.

**The streaming loop, in full (§3.2.1, §4.2)** — the reference implementation of GPU-requested
residency for voxels:

1. The **entire brickgrid is resident** from the start; brickmap pointers begin as unloaded.
2. On hitting an unloaded cell during the trace: append the brick index to a **feedback array**,
   shade with the 24-bit LOD color, and **set the requested flag on that pointer so it is not
   added again** — dedup is a per-cell flag *in the grid itself*, not a sort or hash of the list.
   If the feedback array is full, abort *without* setting the flag, so the brick is re-requested
   later.
3. **A ray hitting a non-resident brick TERMINATES and shades with the LOD color** — not skipped
   as empty, not treated as an untextured hit. Same path as out-of-LOD-range.
4. CPU reads back, fetches/generates each brick, runs a visibility test (delete voxels fully
   enclosed by opaque neighbours), DXT1-compresses, batches uploads.
5. **Feedback and unpack buffers are both 256 bricks per frame.**
6. GPU brick slots are a **simple CPU-tracked object pool**, O(1) alloc/dealloc; colors use a
   buddy-style allocator with levels from 16 to 256 bytes. **There is no GPU-side LRU** — GPU
   bricks are freed only on edit. LRU exists only at the disk-cache tier (chunks of 16³ bricks,
   LZ4, **1000-chunk LRU list**, ~1:10 compression).
7. Latency is never given a frame count; only "within seconds while never heavily impacting the
   frame rate."

**Numbers (§5, HD 7970, 1068×986).** Brickgrid static cost **64 MB**, top layer 0.03 MB. Landscape
8096×8096×256: 64.5–90.3 MB GPU, 104–164 MRays/s. Hairball 2048³: 64.9–112.1 MB vs **ESVO's 1157
MB** — a 10× memory win on the irregular scene. **Streaming cost: 104.3 MRays/s idle → 75.8
MRays/s while streaming = ~27% frame drop** (§5.5). The author's own structural conclusion: the
64 MB static brickgrid **dominates** — "for some viewpoints that occlude most of the environment
almost all data is taken up by static memory."

**Successor: `stijnherfst/BrickMap` (CUDA).** A different 32-bit cell budget: **12-bit index +
3 flag bits (`brick_loaded`/`brick_unloaded`/`brick_requested`) + 8-bit 2×2×2 LOD + 9 unused**,
over superchunks of 16³ = 4096 bricks. The width was chosen for a reason worth recording:
**32-bit because there are no hardware 16-bit atomics** — the request-dedup flag must be
atomically settable from the trace kernel. Three LOD levels (8³, 2³, 1³) gave "no big difference
in frametimes, but the number of chunks requested to be streamed did decrease significantly."
Emergent property: **only surface bricks of a superchunk are ever loaded, since rays don't
penetrate the interior.**

> **VERDICT: CONTENT-SCALE LATER — and it is the template to copy when we get there.**
> Concretely stealable when the streaming arc opens: the **three-state 32-bit cell** (our
> `brickGridLookup` is already the right shape); **dedup by a requested-flag in the grid cell**
> rather than sorting a request list; **the LOD-color fallback so a miss never stalls a ray**;
> and the **256-requests-per-frame** budget as a starting order of magnitude.
> Two warnings carry over now: (a) the **static grid dominates memory**, so our coarse grid's
> size — not the bricks — will set the floor; (b) **atomics availability dictates cell width**,
> which is a layout constraint to respect rather than discover.
> The **no-stack, DDA-at-every-level** structure is already our composed-traversal ruling; this
> is independent production-grade confirmation.

### 3.2 Teardown (Dennis Gustafsson)

**Representation.** Per-object voxel volumes, **8-bit palette index per voxel** — "any voxel
volume can have up to 255 different materials and the representation per voxel is just a single
byte to save memory" ("The Spraycan", 2020-12-03). A palette entry carries color + roughness +
emissiveness + reflectivity + physical material type; palettes pack into a 256-wide texture.
**Index 0 = empty, so the palette fetch *is* the occupancy test.** The world is "thousands of
individual voxel volumes"; larger levels ≈ half a billion voxels ("Teardown quicksave").

**Mips are occupancy, not min-max.** In his pre-Teardown prototype (2018-10-17) he packs
**8 neighbouring voxels per byte, octree fashion, one bit per octant**: "If the byte is zero it
means there are no voxels in any octant, and this can be exploited later to speed up
raytracing." 100×100×25 m at 5 cm = 2 G voxels, **292 MB including two mip levels** vs 2 GB
naive. RenderDoc analysis of the shipped game finds the same shape: a shared world texture
1252×128×1252 with three mips, each texel packing 2×2×2 voxels into one 8-bit uint.

**Traversal.** Both supercover DDA and a cheaper fixed-step march, chosen per use case — fixed
steps are "not water tight," so supercover where exactness matters, fixed-step for AO and
volumetrics; coarse-to-fine, starting at a coarse mip and dropping finer on intersection.
Third-party capture identifies three variants (SuperSparse / Sparse / Dense) differing in how
aggressively the mip is promoted on a miss and demoted on a hit.

**Performance (his own number, 2018 prototype):** AO + lighting + fog + reflections with ten
lights in **~9 ms at 1080p on a GTX 1080**.

**Notably absent: any streaming system.** Teardown is fully resident. No explicit cache-behavior
statement from Gustafsson was found; the closest are the memory-packing rationale and a note
that small objects are **merged into larger volumes for performance**.

> **VERDICT: FRAME-SCALE NOW as an existence proof and a mip-policy reference.**
> The **hit-demotes / miss-promotes mip walk** is the cheap adaptive-LOD policy our far-field
> footprint-selected mip tier is a stricter version of (ours gates on perfect parity, which
> Teardown never needed). The "merge small objects into larger volumes" note is the production
> echo of GVDB's brick-size curve and of W-L1's bucket-by-brick thesis: **fewer, larger
> coherent units beat many small ones**, because per-unit entry/exit is the cost.
> The absence of streaming in a shipped, well-regarded voxel game at half-a-billion-voxel scale
> is itself evidence for our sequencing — residency is a content-scale problem, not a
> prerequisite for a good-looking, fast voxel renderer.

---

## 4. Nanite — request-driven residency, done at production scale

Triangle-domain, but the modern reference for usage-based residency, and the machinery
transfers to bricks almost unchanged. The SIGGRAPH 2021 slides are conceptual on streaming
("Just like virtual texturing we request data based on demand"); the concrete parameters below
come from UE source (5.1 mirror) and the 5.4.4 cvar dump.

**Page geometry** (`NaniteDefinitions.h`):

| constant | value |
|---|---|
| `NANITE_STREAMING_PAGE_GPU_SIZE` | **128 KB** |
| `NANITE_ROOT_PAGE_GPU_SIZE` | **32 KB** (a *different* constant — the folklore "128 KB root page" is wrong) |
| `NANITE_MAX_PAGE_DISK_SIZE` | 256 KB (2× GPU size) |
| `NANITE_MAX_CLUSTERS_PER_PAGE` | 256 |
| `NANITE_MAX_CLUSTER_TRIANGLES` | 128 |
| `NANITE_MAX_STREAMING_REQUESTS` | **262,144 per frame** |

**Pool sizing** (cvars, 5.4.4 defaults): `r.Nanite.Streaming.StreamingPoolSize` **512 MB**
("Does not include memory used for root pages") ⇒ 4096 page slots;
`NumInitialRootPages` **2048** (⇒ ~64 MB always-resident, growable);
`MaxPendingPages` **128**; `MaxPageInstallsPerFrame` **128** ("Limiting this can limit the
overhead of streaming"); `AsyncCompute` **1**. Epic warns: "If the pool is not large enough to
fit all the data needed for a view, cache thrashing can occur where streaming never settles
even for a static view."

**The request mechanism — requests are a byproduct of culling, not a separate pass**
(`NaniteClusterCulling.usf`):

```c
if( bVisible && !bWasOccluded && HierarchyNodeSlice.bLeaf )
    RequestPageRange(RuntimeResourceID, StartPageIndex, NumPages,
                     NaniteView.StreamingPriorityCategory, StreamingPriority);
```

Four properties worth naming. (1) **A request is a page *range***, not a single page.
(2) Slot allocation is **one wave-coalesced atomic**, `WaveInterlockedAddScalar_`, not one per
lane — the same atomic-coalescing idiom Laine et al. measured at +40% (§5 below).
(3) **Priority is packed into a single sortable uint**: a 2-bit per-view category in the high
bits (so shadow and primary views rank differently) with the float priority's mantissa below.
(4) **HZB-occluded geometry emits no request** — you never stream what you cannot see.

**Dedup is CPU-side**, a linear-probed `FRequestsHashTable` that **merges by max priority**;
selection heapifies by priority and recursively pulls in dependency pages so a page never
arrives without its parents.

**Latency.** `MaxStreamingReadbackBuffers = 4u` — a 4-deep readback ring. The exact
request→resident frame count is not stated by Epic; the ring bounds it at 4 and the pipeline
implies ≥2–3.

**Eviction: LRU, constrained.** Intrusive linked list, insert at front, evict from the back,
with the source rule: "Only remove leaf nodes. Make sure to never delete a node that was added
this frame or is a dependency for a pending page registration." A parent is never evicted out
from under a resident child.

**What renders on a miss — the elegant part.** Uninstalling a page sets
`NANITE_CLUSTER_FLAG_LEAF` on the parent's clusters and its child reference to `0xFFFFFFFF`.
Traversal then sees a leaf, **stops descending, draws the coarser resident ancestor, and emits
the request in the same pass**. There is no miss path and no stall — the DAG cut is simply
shallower. A shader comment confirms the LOD metric is corrected for it: "MinLODError needs to
also reflect leafness caused by streaming cut."

**Nanite voxels (UE 5.7)** are worth flagging as convergent evolution: the Nanite Builder
"voxelizes them into clusters of, at most, 128 4×4×4 voxel bricks" (8192 voxels/cluster), and
**voxels are chosen by the same simplification-error metric as any other LOD rung** — so they
inherit the page/streaming machinery unchanged. Measured: one tree's UASSET 3.5 GB → ~29 MB;
streaming memory for one view ~36 MB → ~2.7 MB. Normals are stored as a **distribution**, not a
single vector. No voxel-specific streaming constants or `r.Nanite.Voxel*` cvars were found.

> **VERDICT: CONTENT-SCALE LATER — the reference architecture for our streaming arc; two ideas
> are FRAME-SCALE NOW.**
> For the streaming arc, adopt wholesale: **requests emitted by the traversal pass itself**
> (our W3 cell table is already the natural request signal, as the plan records);
> **request-a-range not a page**; **priority as one sortable uint with a per-view category**
> (primary vs probe/shadow waves must not compete equally); **CPU-side dedup merging by max
> priority**; **LRU constrained so a parent outlives its children**; a **bounded installs-per-frame**
> budget; and above all the **miss policy — never stall, just render the coarser resident
> ancestor and request**. That last one composes exactly with our far-field footprint-selected
> mip: a non-resident brick is simply a *coarser footprint*, which our mip tier already knows
> how to sample. That is a genuinely clean fit and should be the design's spine.
> Frame-scale now: **wave-coalesced atomic allocation** for any queue write, and
> **occlusion-gated requests** (don't pay for what the HZB already killed).

---

## 5. Wavefront scheduling and warp coherence — the papers about *our* bottleneck

### 5.1 Megakernels Considered Harmful (Laine, Karras, Aila, HPG 2013)

Our epoch already follows the thesis, so this section mines the details rather than the
conclusion.

**Structure (§4.2).** A pool of **1M paths** always alive; each iteration advances every path by
one segment. Path state is in **global memory, SoA, 212 bytes/path = 212 MB**. Three stages —
logic, material, ray cast — communicating through **preallocated fixed-size queues (4 MB each)**
with an atomically-incremented counter. Enlarging the pool 1M→8M buys only ~5% for 1.7 GB, so
they keep 1M.

**The two implementation findings that are not in the abstract:**

- **SoA vs AoS (§4.3): 80% total speedup**, and per-kernel: logic +147%, new-path +790%,
  materials +68%. Ray cast is unaffected (it doesn't touch path state). The mechanism is stated
  plainly: threads in the logic kernel operate on consecutive path indices, so each state access
  becomes "a contiguous read/write of 32 32-bit memory words, aligned to a 1024-bit boundary."
- **Atomic coalescing (§4.4): 40% total speedup.** Rather than each lane doing its own atomic
  increment, a warp-wide **ballot** builds a mask, one atomic is issued per warp, and lanes take
  consecutive slots. Two distinct wins: serialized atomics are avoided, *and* coherence improves
  because lanes from one warp land in consecutive queue entries instead of being interleaved with
  other warps'. The **ray cast kernels sped up 32% from this alone**, "attributed entirely to
  improved ray coherence."

**Results (Table 1, Tesla K20).** CARPAINT +36% (a *single* material — the win comes purely from
low-register ray-cast kernels and instruction-cache fit), CITY +79%, CONFERENCE **+221%**.
Thread utilization in CONFERENCE: megakernel **23%** → wavefront **53%**.

**Why the megakernel loses (§4.1)** — four mechanisms, all of which we should recognize:
divergent path termination; material divergence serializing over all materials in a warp;
subtle divergence from materials that skip shadow rays; **high register usage from material
hot-spots, which "decreases the number of threads that can remain resident and thereby hurts the
latency hiding capability. Ray casts suffer from this especially badly, as they perform
relatively many memory accesses compared to math operations"**; and instruction-cache overrun,
since the i-cache is shared across all warps on an SM.

> **VERDICT: FRAME-SCALE NOW — two concrete gaps in our shipped implementation.**
> The register/occupancy mechanism is *literally our measured state*: warps resident 35.6%,
> warp slots empty 53.5%, and the plan's own lever list names kernel splitting for occupancy.
> Laine et al. quantify why that matters most for the memory-heavy kernel — which is ours.
> Two details to check ourselves against: (1) **is every queue write wave-coalesced via ballot?**
> That was worth 40% overall and 32% on the ray-cast kernel *for coherence alone*. Nanite
> independently uses the same idiom. (2) **is our hit/queue state SoA?** Worth 80% there. Our
> hop record (`{position, hitDirection, pathDistance, recipeId, throughput, anchor}`, 32–48 B)
> is exactly the kind of struct that wants to be columnar — and our View contract is already
> SoA-by-design, so the idiom is native to this codebase.

### 5.2 Understanding the Efficiency of Ray Traversal on GPUs (Aila & Laine, HPG 2009 + 2012 addendum)

Predates our window, but it is the **methodological precedent for our exact symptom**, and its
conclusion is the one we independently reached.

The measured gap: packet traversal has maximally coherent memory access yet ran **1.7–2.4× off**
a simulated upper bound. The falsification is the elegant part — per-ray (*less* coherent)
traversal was *closer* to the bound, "contradicting the idea of memory bandwidth being a major
issue," and incoherent AO/diffuse rays were "not any further from the simulated numbers than
primary rays, even though they should hit the memory bandwidth limits much sooner" (§3.2).

The actual cause was **work-distribution starvation**: "the execution time of individual rays
vary wildly, and that may cause starvation issues in work distribution when long-running rays or
warps keep distribution slots hostage." The fix was **persistent threads** — launch exactly enough
threads to fill the machine and pull work from a global pool via an atomic counter — worth
**1.5–2.2×**, landing within 10–20% of the theoretical bound for all ray types (§3.4).

**Two negative results that save us work.** (a) **Work queues with ballot/prefix-sum lane
repacking** were "very slow on GTX285 in practice"; the authors' verdict is that they pay
"in cases where the primary bottleneck is low SIMD efficiency" (§5.2). (b) **Wide trees**
(branching 4–32, a ray spread across adjacent lanes) were "significantly slower than binary trees
in all of our test cases" (§5.3).

Independent 2024 reproduction (`dubiousconst282/VoxelRT`): wave-intrinsic postponement of inner
traversal and un-nested DDA loops both "showed limited success"; group-shared memory for the
ancestor stack gave **+9% in Tree64 but a 120% slowdown in ESVO**. Its clocks-per-iteration table
is the latency-bound fingerprint in one image — PlainDDA 391.7 iters/ray at **67.8 clocks/iter**
vs Tree64 19.9 iters/ray at **170.7 clocks/iter**: hierarchical methods buy ~20× fewer iterations
at ~2.5× the cost each, because each iteration *is* a dependent pointer chase.

> **VERDICT: FRAME-SCALE NOW — the strongest single lever in this survey, plus two guardrails.**
> Aila & Laine's diagnostic chain is ours: high apparent occupancy, idle DRAM, far below the
> issue bound. Their answer was neither layout nor lane-repacking but **persistent threads with an
> atomic work-pull**, worth 1.5–2.2×. Our wavefront already has queues; what is not obviously
> present is the *persistent* consumer that keeps every SM fed regardless of per-ray cost
> variance — and our 53.5% empty-warp-slots reading is the signature of exactly the starvation
> they describe.
> The guardrails matter as much: **do not** reach for subgroup lane-repacking (three independent
> negative results, and it only pays when SIMD efficiency is the measured bottleneck — ours is
> latency), and **do not** widen the tree's branching factor across lanes.
> The clocks/iteration framing also sharpens W-L1's claim: staging cuts the *per-hop* cost of the
> expensive-iteration regime, which is the correct axis for a hierarchical traversal.

### 5.3 ESVO beam optimization (Laine & Karras, I3D 2010) — the coherence lever that did work

Included because it is the coherence technique with published *positive* measurements in the
voxel domain. A coarse conservative distance image over **4×4 or 8×8 pixel blocks**, rays cast at
block corners, per-ray start = min of the 4 corners. Measured (Mrays/s, contours → +beam):
SIBENIK-D 2048×1536 **43.6 → 60.9 (+40%)**, CITY 1024×768 **89.1 → 106.0 (+19%)**, FAIRY +3%,
**HAIRBALL 0%**. The gain is entirely a function of how much cheap empty space precedes the first
hit; on incoherent geometry it vanishes.

ESVO's layout facts are also worth recording against ours: child descriptor **64 bits**;
**page headers at every 8 KB boundary** so one can be found by clearing the low bits of any
descriptor pointer; and — the load-bearing locality mechanism — far pointers "can be made very
rare by **sorting child descriptors in an approximate depth-first order within each block**."
**Note this is depth-first sibling grouping, not Morton.**

> **VERDICT: partially shipped, partially FRAME-SCALE NOW.**
> Our composed traversal already skips the empty near-field via RT/DDA, which is beam
> optimization's benefit by another route. The transferable item is the layout one:
> **approximate depth-first ordering within a fixed-size block** is the published answer to
> shortening a pointer chase, and it is the ordering discipline to apply to our node array
> alongside NanoVDB's breadth-first-per-level flat arrays. (These are not in conflict:
> breadth-first *between* levels for flat indexing, depth-first *within* a block for chase
> locality.)

---

## 6. Cache-layout specifics — Morton, alignment, and a negative result

**Morton/Z-order on a linear buffer is the weakest lever in this survey**, and the evidence is
consistent enough to state plainly.

- **The hardware already does it.** Real texture swizzling is *nested tiling* sized against the
  cache line — "64-byte cache lines, you might decide to chop up 32bpp textures into tiles of 4×4
  pixels" — with row-major indexing at the tile level, not pure bit-interleaved Morton, because
  interleaving is "somewhat awkward in software" but "relatively easy and cheap to do in
  hardware" (Giesen, 2011). Pure Morton is the limit case of a family the hardware already
  occupies.
- **The systems that had the choice chose 3D textures**, explicitly for the cache: GigaVoxels
  used 3D textures for both traversal and brick sampling because of "3D texture caches on the
  GPU"; GVDB stores bricks in a 3D texture atlas "for efficient hardware trilinear filtering and
  texture cache access."
- **No source found measures manual Morton on a linear buffer beating a 3D texture.** The one
  strong volume-rendering result located (Ikeda et al., up to 2.22×; texture cache hit rate
  58.8% → 71.8% at oblique angles) attributes the gain to **viewpoint-adaptive thread-block
  shaping and transposed thread indexing** — thread mapping, not data layout. *(Abstract-level
  only; the body was not read.)*
- **Where Morton did earn its place: ray→lane assignment.** Aila & Laine assigned *rays to warps*
  in Morton order. Laine et al. 2013 name the absence of exactly this as a reason their ray-cast
  utilization trailed Aila & Laine's: "our rays are not sorted in any fashion, whereas in the
  previous work they were assigned to threads in a Morton-sorted order."

**Alignment and node-layout facts worth adopting regardless:** NanoVDB's **32-byte alignment with
static_asserts**; ESVO's **8 KB page-header boundary trick**; Nanite's **`GPU_PAGE_HEADER_SIZE 16`**.

**Hardware sparse-residency granularity (Vulkan spec)** — relevant the moment we consider
hardware-managed residency instead of hand-rolled: all standard 3D sparse block shapes are
**64 KB**, i.e. 8-bit → 64×32×32, 16-bit → 32×32×32, 32-bit → 32×32×16, 128-bit → 16×16×16.
Gated on `sparseResidencyImage3D` + `residencyStandard3DBlockShape`. Note HashDAG's authors named
Vulkan sparse resources as their intended fix for exactly the indirection overhead they measured.

> **VERDICT: Morton-for-brick-data = SUPERSEDED / not worth a slice. Morton-for-ray-ordering =
> FRAME-SCALE NOW, and it is already half-built.** Our W-SPLIT bucketing *is* a ray-reordering
> mechanism; widening the key to brick (W-L1) makes it a spatial one. That is the version of
> "Morton" the literature actually supports.
> Alignment/padding disciplines are free and should be adopted. **64 KB** is the number to design
> against if hardware sparse residency ever enters the streaming arc.

---

## 7. Hardware-RT-era voxel work (2019–2025)

The definitive measured comparison is **Hansson Söderlund, Evans & Akenine-Möller, JCGT 11(3),
2022** (NVIDIA, RTX 3090, path tracing) — four traversal methods × five intersection methods over
SDF scenes. The two that matter for us:

- **SVS** — one AABB per surface-intersecting voxel, BVH in DXR, **traversal on RT hardware**,
  custom intersection shader.
- **SBS** — one AABB per **7×7×7-voxel brick** (8³ values), RT hardware to the brick, then
  **3D DDA inside the intersection shader**. This is our composed traversal, published.

| | Cheese ms | Goblin ms | Cheese MB | Goblin MB |
|---|---|---|---|---|
| GST (software sphere-trace) | 47.9 | 82.9 | 177.3 | 177.3 |
| **SVS** (HW RT, AABB/voxel) | **17.1** | **16.1** | **209.0** | 152.2 |
| **SBS** (HW RT, AABB/brick + DDA) | 32.8 | 30.9 | **24.6** | **18.0** |
| SVO (software) | 40.7 | 54.8 | 45.0 | 32.6 |

Hardware RT wins outright on speed — SVS is ~1.9× faster than SBS and ~2.4× faster than SVO. But
**AABB-per-voxel costs 8.5× the memory of AABB-per-brick** (209.0 vs 24.6 MB). The authors'
own tradeoff ruling: "In a memory-constrained scenario, SBS is likely the winning method because
it uses the least memory and is faster than SVO." Software sphere-tracing wins only in the
coherent-primary-ray regime.

**Two findings that bear directly on our rulings.** (1) Tighter boxes help the hardware: "SBS
creates tighter bounding boxes around the SDF surface, which allows for better utilization of
hardware-accelerated ray tracing." (2) A **negative locality result** worth taking seriously:
"We speculated that SBS might be faster than SVS due to better use of locality because a block of
7×7×7 voxels is read at a time, but we cannot see any such evidence."

Our own measurements sit consistently in this landscape: W-RT Slice 1 measured RT at 1.38–1.53
ns/ray vs software 2.93–3.17, and W-BRICKMAP Slice 1 measured two-level DDA at **0.96–1.08
ns/ray — faster than hardware RT on the demo-scale search**. That is not a contradiction of JCGT;
it is the expected shape at small scale, where BVH build/traversal overhead is not yet amortized.

Also relevant: the software 64-tree line (`dubiousconst282`, 2024) reports `SvtNode64` at
**12 bytes** (1-bit IsLeaf, 31-bit ChildPtr, 64-bit ChildMask), ~0.19 bytes/voxel asymptotic,
Bistro at 0.62 B/voxel vs ESVO's 1.02 — and, more usefully, an optimization ladder measured in
cycles/ray at 4K: naive 16,903 → **ancestor memoization 8,896 (nearly 2×)** → bitmask coalescing
7,052 → ray-octant mirroring 6,358.

> **VERDICT: confirms our composed-traversal ruling; one FRAME-SCALE NOW steal.**
> SBS *is* our architecture (RT to the brick, DDA inside) and the memory column vindicates the
> brick-granularity choice by 8.5×. The user ruling that RT is an optional enhancement rather
> than a dependency is well-supported: software two-level DDA already beats RT at our scale.
> The steal is **ancestor memoization — nearly 2× on its own**, and it is the same mechanism as
> NanoVDB's ReadAccessor arriving from a different direction. Two independent sources converging
> on "cache the ancestor chain per thread" is the strongest signal in this survey.
> The negative locality result is a caution for W-L1's *justification*: brick-block reads did not
> by themselves buy SBS anything. W-L1's claim must rest on the **shared-memory hop-latency cut
> under a bucket that guarantees reuse**, not on "we read a brick at a time" — the latter is
> measured not to be enough.

---

## 8. The virtual-texturing lineage — the general usage-based-residency pattern

**Sparse Virtual Textures (Barrett, 2008)** established the loop and, more importantly, the
*miss policy*. Page management: "List all pages required / All mipmap pages / Drop high-res pages
to fit memory / Lock pages already present / Download pages / LRU discard," downloading from
lowest-resolution first. Page size is presented as an unresolved tradeoff — smaller pages mean
more padding and page-table traffic but finer granularity — with the recommendation left as
literally "Guess: 256, 128."

The architectural point is the one that recurs everywhere since: **a graphics page fault cannot
block.** "VM approach is to block… Not an option for SVT"; instead "Graphics can tolerate wrong
output" ⇒ substitute a coarser resident mip. His reason is precisely our situation — pixel
processors are "already massively parallel to tolerate memory latency," so a stall wastes the one
mechanism keeping the machine fed.

**UE Streaming Virtual Texturing** confirms the modern shape: feedback is "written while
rendering a frame … describing which resource tiles were requested," and "after the render frame
is finished, the feedback buffer is read back using **double or triple buffering to avoid
blocking**" ⇒ 2–3 frames of latency; the pool "acts as a **least recently used cache**." Commonly
cited defaults `r.VT.TileSize=128`, `r.VT.TileBorderSize=4`, `r.vt.FeedbackFactor=16`,
`r.VT.MaxUploadsPerFrame=64` come from third-party config dumps rather than Epic docs and should
be treated as unverified.

Note the **feedback factor**: the request buffer is written at **1/16th** resolution, not per
pixel. Requests are a statistical sample of need, not an exhaustive record — which is what makes
readback cheap.

> **VERDICT: CONTENT-SCALE LATER — three parameters and one law.**
> The law: **never stall on a miss; substitute coarser and request.** This is the same policy
> Nanite, the brickmap thesis, and SVT all independently converge on, and for us it composes with
> the far-field mip tier at zero design cost.
> The parameters: **subsample the feedback buffer** (1/16 is the shipping precedent — we do not
> need every ray to vote); **double/triple-buffer the readback** so it never blocks (2–3 frames);
> **bound uploads per frame** (UE 64, Nanite 128, brickmap 256).

---

## Ranked steal list for the caching design

Ordered by expected value against the **measured** bottleneck (dependent-load latency, 0.39% SM
issue, idle DRAM, 53.5% empty warp slots), not by how famous the technique is.

### 1. Per-thread ancestor memoization on the traversal chain — NanoVDB ReadAccessor + 64-tree
**Seam: composed traversal (ESVO data-access role); compounds with W-L1.**
Cache the last-visited node per level in registers (NanoVDB: 3 levels, 68 bytes, or 44 with the
single-key variant) and probe **leaf → lower → upper → root**, falling back to a root-down descent
only on a miss. A hit is a masked compare against a register-resident key
(`(ijk & ~NodeT::MASK) == mKey`), which **replaces a dependent-load chain with an ALU compare** —
and SSVDAG's §6.4 finding is that ALU is free under latency hiding while memory layout is not.
Two independent sources converge here: NanoVDB ships it as the default accessor, and the 64-tree
work measures **nearly 2×** from it standalone (16,903 → 8,896 cycles/ray). Under W-L1's
bucket-by-brick key the hit rate should *exceed* both, since the bucket is constructed to
guarantee the coherence the accessor assumes.
*Why it ranks first: it attacks the dependent chain directly, costs registers not bandwidth
(and we have DRAM headroom, not register headroom — so measure occupancy against the 32-reg
specialized-kernel budget), and it is the one technique with two independent positive measurements.*

### 2. Persistent-thread work-pull for the wave consumers — Aila & Laine
**Seam: bucketed dispatch / the wavefront queues.**
Launch exactly enough threads to fill the machine and pull work from the global queue via an
atomic counter, instead of sizing dispatches to the work. Measured **1.5–2.2×**, closing to within
10–20% of the theoretical bound. Our **53.5% empty-warp-slots-while-SM-active** is the textbook
signature of the starvation this fixes — long-running rays holding distribution slots hostage
while other slots idle. The plan's per-recipe bucketed dispatch already provides the queues; what
is missing is the persistent consumer.
*Caveat to respect: this is orthogonal to lane repacking, which is separately measured to be a
loser for us (see below).*

### 3. W-L1 brick staging, justified on hop latency rather than block reads — GVDB + SSVDAG + JCGT
**Seam: W-L1 directly.**
The lever is sound and the measurement supports it (L2 ~200 cyc vs shared ~20–30 cyc = 5–10× per
hop), but the survey sharpens both the justification and the parameters:
- **Justify on reuse, not on block size.** JCGT explicitly failed to find a locality win from
  reading a 7³ block at a time ("we cannot see any such evidence"). Staging pays because the
  bucket key *guarantees* many rays reuse the staged brick — so the A/B must report **reuse per
  staged brick**, or the result will be uninterpretable.
- **Watch the register/occupancy budget, not just the shared-memory budget.** GVDB found per-level
  DDA state "ineffective as kernel register pressure becomes a limiting factor" and solved it by
  reusing **one DDA variable set** at all levels with only a short exit stack. Keep that
  discipline; the 32-reg specialized kernels are what makes staging affordable at all.
- **Brick size is a tunable with a known curve.** GVDB: small bricks maximize occupancy but the
  DDA "spends more time in entry and exit and less time doing useful work." If staging makes a
  brick fetch cheap, the curve predicts a *larger* staging unit (brick-cluster) wins. Teardown's
  "merge small objects into larger volumes for performance" is the production echo.
- **Known risk with a measured precedent:** shared-memory staging of traversal state produced
  **+9% in one structure and a 120% slowdown in ESVO** in the 2024 reproduction. Gate accordingly
  and keep the flag-off path byte-exact, per standing practice.

### 4. Wave-coalesced atomics + SoA on every queue write — Laine 2013, confirmed by Nanite
**Seam: bucketing / queue machinery / W3 accumulation.**
Two cheap, well-measured disciplines to audit our shipped queues against:
- **Ballot-coalesced atomic slot allocation** — one atomic per warp, lanes take consecutive slots.
  Worth **40% overall** in the wavefront paper, and **32% on the ray-cast kernel from improved
  coherence alone**. Nanite uses exactly this idiom (`WaveInterlockedAddScalar_`) for streaming
  requests. Our L2 is at 25.2% from accumulate atomics, so this is aimed at the one non-idle
  memory counter we have — and it composes with the queued B2 shared-memory pre-merge rather than
  competing with it.
- **SoA for hop/queue records** — worth **80% total / +147% on the state-heavy kernel**. Our
  32–48 B hop record is the exact shape that wants to be columnar, and SoA is already this
  codebase's native idiom via the View contract.

### 5. The residency architecture, adopted whole when the streaming arc opens — Nanite + brickmap + SVT
**Seam: content-scale streaming (the W3 cell table is the request signal).**
Three sources converge on one design; take it rather than re-deriving:
- **Requests are emitted by the traversal pass itself**, gated on visible-and-not-occluded, as a
  **range** `(resource, startPage, numPages)` with a **priority packed into one sortable uint**
  carrying a per-view category (primary vs probe/shadow must not compete equally).
- **Never stall on a miss.** Render the coarser resident ancestor and request — Nanite via a leaf
  flag, brickmap via a 24-bit LOD color, SVT via mip substitution. For us this is nearly free:
  **a non-resident brick is just a coarser footprint**, and the far-field footprint-selected mip
  tier already knows how to sample that. Design the miss path as a footprint clamp.
- **Dedup cheaply**: a *requested* flag in the grid cell (brickmap — no sort, but note it forces
  the cell width to an atomically-writable size) or CPU-side hash merging by max priority
  (Nanite). **Subsample the feedback** (UE writes it at 1/16 resolution).
- **Double/triple-buffered readback** (2–3 frames; Nanite's ring is 4), **bounded installs per
  frame** (64/128/256 across the three systems), **LRU constrained so a parent is never evicted
  under a resident child**.
- **Budget the static grid, not the bricks.** The brickmap thesis's own conclusion is that its
  64 MB brickgrid dominated memory. Our coarse grid will set the floor.

**Explicitly not recommended**, on the strength of three independent negative results: subgroup
lane-repacking / wave-intrinsic postponement of divergent traversal (Aila & Laine measured it slow
and scoped it to *SIMD-efficiency-bound* workloads — ours is latency-bound; the 2024 voxel
reproduction found "limited success"), wide branching factors across lanes (Aila & Laine:
"significantly slower in all of our test cases"), and manual Morton swizzling of brick data on a
linear buffer (no source measures it beating a 3D texture; the hardware already tiles against the
cache line).

---

## Changes vs GigaVoxels-2011 thinking

Where the field moved, so our caching behavior is finalized against 2025 understanding.

**1. "Cache miss" stopped meaning "stall" and became "render coarser."**
GigaVoxels' loop is request → wait → refine, with subdivision throttled to one request per ray.
Every modern system instead makes the miss *invisible*: Nanite marks the parent a leaf and draws
the resident ancestor in the same pass that requests the child; the brickmap thesis terminates the
ray on the brick's 24-bit LOD color; SVT substitutes a coarser mip because blocking "is not an
option." The residency system became a **quality dial rather than a correctness dependency**. For
us this is the single most consequential update: our footprint-selected mip tier is already a
coarser-representation mechanism, so residency should be expressed as a *footprint clamp* — which
also means the streaming arc can land without touching the near-field hot path.

**2. Requests became a byproduct of culling, not a separate pass — and are visibility-gated.**
GigaVoxels' feedback needed MRT encoding, a screen-space neighbourhood dedup mask, and
HistoPyramid compaction to keep readback affordable. Nanite emits requests inline from the culling
traversal it was already running, dedups on the CPU by max priority, and — the idea that did not
exist in 2011 — **suppresses requests for HZB-occluded geometry entirely**. The 2009 pipeline
would happily stream what a later depth test discards.

**3. Priority became explicit, typed, and per-view.**
2011 had a request and an LRU timestamp. 2025 has a sortable priority uint with a **per-view
category**, so a shadow or probe wave cannot evict what the primary view needs, plus deadline-aware
prefetch ("Prioritize requests closer to the deadline higher") and a dependency rule that a page
never arrives without its parents. Directly relevant to us: our probe grid, shadow wave, and
primary rays would otherwise compete as equals for residency.

**4. Compression and streaming split into separate, sometimes opposed, concerns.**
2011 treated "make it smaller" and "make it stream" as one goal. The DAG line (2013–2020) made
geometry 10–500× smaller with **no streaming at all** — and paid for it in scatter: SSVDAG's
pointer compression costs 14–16% traversal, HashDAG's single extra indirection costs 1.5–2×.
Meanwhile Teardown shipped half a billion voxels **fully resident** with no streaming system.
The modern reading: **compression buys capacity and costs latency**, so a latency-bound renderer
should not adopt it reflexively. This directly supports the plan's re-scoping of usage-based
residency to the content-scale arc.

**5. The measured bottleneck moved from bandwidth to latency and occupancy — and the field
published the diagnostic for telling them apart.**
2009–2011 designs were built against a bandwidth-scarce mental model. Aila & Laine's falsification
(coherent access *further* from the bound than incoherent access ⇒ not bandwidth) and the
wavefront paper's register/occupancy analysis relocated the problem to **latency hiding and warp
residency**. The 2024 clocks-per-iteration data makes it concrete: hierarchical traversal buys
~20× fewer iterations at ~2.5× the cost each. Our own three-instrument decomposition landed in
exactly this place independently — which is the reassuring part, and the reason the steal list
above leads with ancestor memoization and persistent threads rather than with anything
GigaVoxels-shaped.

**6. Two-level grids beat deep octrees for the leaf regime, and the winning traversal is
stackless.**
2011's centre of gravity was the deep N³-tree. What shipped since — brickmap, Teardown, GVDB,
NanoVDB, and JCGT's SBS — is consistently **shallow: a coarse grid or short tree over fixed-size
bricks, with a DDA inside and no traversal stack** (NanoVDB re-snaps one DDA state on level
change; GVDB keeps only a short exit stack; the brickmap thesis names the SVO stack "a bottleneck
for SVOs on the GPU"). Our composed-traversal ruling (RT = traversal, DDA = leaf traversal,
ESVO = data access/mips) is the 2025 consensus shape, independently derived.

**7. Hardware ray tracing became a real option for voxels — with a memory cliff, and it is
optional at our scale.**
No 2011 design could assume RT hardware. JCGT 2022 measures AABB-per-voxel as fastest but at
**8.5× the memory** of AABB-per-brick, making per-brick the memory-constrained choice — which is
our architecture. Our own measurements show software two-level DDA (0.96–1.08 ns/ray) beating
hardware RT (1.38–1.53) at demo scale, so the standing ruling that RT is an optional enhancement
rather than a dependency is well-founded and should survive the scaled-scene crossover
measurement.

---

## Sources

**Read in full (PDF text extracted):**
[Kämpe et al., High Resolution Sparse Voxel DAGs, SIGGRAPH 2013](https://www.cse.chalmers.se/~uffe/HighResolutionSparseVoxelDAGs.pdf) ·
[Villanueva et al., SSVDAGs, I3D 2016](https://albertojaspe.net/publications/2016-I3D-ssvdags.pdf) ·
[Careil et al., Interactively Modifying Compressed Sparse Voxel Representations, CGF 2020](https://raw.githubusercontent.com/Phyronnaz/HashDAG/master/ModifyingCompressedVoxels-main.pdf) ·
[Hoetzlein, GVDB, HPG 2016](https://ramakarl.com/pdfs/2016_Hoetzlein_GVDB.pdf) ·
[van Wingerden, Real-time Ray tracing and Editing of Large Voxel Scenes, Utrecht 2015](https://studenttheses.uu.nl/handle/20.500.12932/20460) ·
[Laine, Karras, Aila, Megakernels Considered Harmful, HPG 2013](https://research.nvidia.com/sites/default/files/pubs/2013-07_Megakernels-Considered-Harmful/laine2013hpg_paper.pdf) ·
[Aila & Laine, Understanding the Efficiency of Ray Traversal on GPUs, HPG 2009](https://research.nvidia.com/sites/default/files/pubs/2009-08_Understanding-the-Efficiency/aila2009hpg_paper.pdf) ·
[Laine & Karras, Efficient Sparse Voxel Octrees, I3D 2010](https://research.nvidia.com/sites/default/files/pubs/2010-02_Efficient-Sparse-Voxel/laine2010i3d_paper.pdf) ·
[Hansson Söderlund, Evans, Akenine-Möller, JCGT 11(3) 2022](https://jcgt.org/published/0011/03/06/paper-lowres.pdf) ·
[Karis et al., Nanite, SIGGRAPH 2021](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf) ·
[Crassin et al., GigaVoxels 2009](https://maverick.inria.fr/Publications/2009/CNLE09/CNLE09.pdf) ·
[Crassin et al., Voxel Cone Tracing 2011](https://research.nvidia.com/sites/default/files/pubs/2011-09_Interactive-Indirect-Illumination/GIVoxels-pg2011-authors.pdf) ·
[Kraaijeveld et al., CGF 2025](https://arxiv.org/pdf/2510.09081) ·
[Barrett, Sparse Virtual Textures 2008](https://silverspaceship.com/src/svt/)

**Read as source/documentation:**
[NanoVDB.h v32.9.2](https://raw.githubusercontent.com/AcademySoftwareFoundation/openvdb/master/nanovdb/nanovdb/NanoVDB.h) + `nanovdb/math/HDDA.h` ·
UE 5.1 mirror: [NaniteDefinitions.h](https://raw.githubusercontent.com/Wabi-Studios/UnrealEngine/master/Engine/Shaders/Shared/NaniteDefinitions.h), [NaniteStreamingManager.cpp](https://raw.githubusercontent.com/Wabi-Studios/UnrealEngine/master/Engine/Source/Runtime/Engine/Private/Rendering/NaniteStreamingManager.cpp), [NaniteClusterCulling.usf](https://raw.githubusercontent.com/Wabi-Studios/UnrealEngine/master/Engine/Shaders/Private/Nanite/NaniteClusterCulling.usf) ·
[UE 5.4.4 cvar dump](https://indxzero.github.io/ue544cvarwiki/) ·
[Nanite Technical Details](https://dev.epicgames.com/documentation/unreal-engine/nanite-technical-details) ·
[Nanite Foliage (voxels)](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-foliage) ·
[Vulkan sparse resources](https://docs.vulkan.org/spec/latest/chapters/sparsemem.html) ·
[UE Streaming Virtual Texturing](https://dev.epicgames.com/documentation/en-us/unreal-engine/streaming-virtual-texturing-in-unreal-engine)

**Read as blog/repo (production + community):**
[Gustafsson, From screen space to voxel space](https://blog.voxagon.se/2018/10/17/from-screen-space-to-voxel-space.html) · [The Spraycan](https://blog.voxagon.se/2020/12/03/the-spraycan.html) · [Teardown quicksave](https://blog.voxagon.se/2020/11/18/teardown-quicksave.html) ·
[acko.net, Teardown Frame Teardown](https://acko.net/blog/teardown-frame-teardown/) ·
[dubiousconst282, voxel ray tracing guide 2024](https://dubiousconst282.github.io/2024/10/03/voxel-ray-tracing/) + [VoxelRT](https://github.com/dubiousconst282/VoxelRT) ·
[stijnherfst/BrickMap](https://github.com/stijnherfst/BrickMap) ·
[Giesen, texture tiling and swizzling](https://fgiesen.wordpress.com/2011/01/17/texture-tiling-and-swizzling/) ·
[elopezr, A Macro View of Nanite](https://www.elopezr.com/a-macro-view-of-nanite/) · [Tricky Bits](https://trickybitsblog.github.io/2024/04/20/nanite.html)

**Could not fetch (stated so the gaps are visible):** the NanoVDB SIGGRAPH 2021 Talk text (ACM
403; `ken.museth.org` has no NanoVDB entry) — layout facts above come from the canonical header
instead, which is more precise, but **no measured ReadAccessor speedup or NanoVDB render
benchmark is available**, so steal #1's magnitude rests on the 64-tree's independent ~2×.
No readable UE ≥5.5 source mirror (Nanite constants are 5.1-verified, cvar defaults 5.4.4).
No Brian Karis technical talk on Nanite voxels (5.7 docs only). Ikeda et al. on Morton/cache
locality: abstract-level only. Aokana (arXiv 2505.02017, SVDAG + LOD + streaming): abstract only.

---

# Addendum: prior art for PROGRAM residency (2026-08-07)

*Requested follow-up. The survey above treats data residency; this addendum asks whether the
plan's **program**-residency proposal has prior art.*

**The proposal under test** (plan §"Admission ladder", items 3–4): recipe variant families over
m boundary shapes × n LOD levels, with a per-cell **admission ladder — S0 bytecode (free,
interpreted, partial-uploadable) → S1 compiled kernel, promoted by hit counters** — and a
**residency window n±1** with hysteresis, where "pipeline count is the budget, not memory."
That is: treat program variants the way GigaVoxels treats bricks.

The question is whether that is novel. Answer: **the mechanism is mature on the CPU side and
unbuilt on the GPU side.** Three ingredients — an interpreted fallback tier, usage-counted
promotion, and a bounded window with eviction — each ship somewhere in graphics, but no GPU
system combines them. Every graphics system found promotes on **first use** and **never evicts**.

## The closest kin, and the delta from each

### Dolphin's ubershaders (2017) — closest in *shape*, and the delta is exact

The only shipped graphics system whose fallback tier is a genuine **GPU-side interpreter of the
program semantics**. Their own section heading: *"Write an Interpreter for the GameCube/Wii
Rendering Pipeline within Shaders and Run it on the Host Graphics Card"* — an interpreter for
the TEV unit, precompiled at game start, because the hardware it emulates has *"no preloading of
the TEV configurations whatsoever."*

**Hybrid mode** is our S0→S1 handover, one tier down: *"whenever a new pipeline configuration
appears, Dolphin will use the already compiled Ubershaders to immediately render the effect
without stuttering while still compiling the specialized shader in the background. Once the
specialized shader is done, Dolphin will then hand the objects rendering through the Ubershader
over to these newly generated specialized shaders."* Mechanically (`VertexManagerBase.cpp`,
`ShaderCache.cpp`): per draw with changed state, `GetPipelineForUidAsync(uid)` is polled;
on miss it calls `QueuePipelineCompile(uid, COMPILE_PRIORITY_ONDEMAND_PIPELINE)` and returns
empty, and the draw falls back to `GetUberPipelineForUid`. Granularity is one `GXPipelineUid`.
Variants persist to disk across runs via per-stage `LinearDiskCache`.

**The two deltas, both verified at source** (`ShaderCache.cpp` master, 1473 lines — grep for
`evict|lru|hotness|use_count|hit_count|prune` returns **0 hits**; the only `.clear()` calls are
wholesale teardown on config change):

1. **Promotion is first-use, not counted.** One sighting of a UID enqueues the compile. There is
   no hit count, no threshold, no hotness test — the `COMPILE_PRIORITY_*` values are queue
   ordering, not hotness.
2. **There is no eviction and no bound.** Residency is a plain `std::map<GXPipelineUid, …>` plus
   an append-only disk log. Dolphin is **compile-once-keep-forever**.

The consequence is the precise thing a counter + window would fix: **Dolphin's compiled set is
the cumulative set of every configuration ever seen, not the current working set.** That is
tolerable for an emulator with a fixed game and a 15%-overlapping UID space; it is not tolerable
for procedurally generated m×n recipe variant families, where the cumulative set is unbounded by
construction.

*(RPCS3's shader interpreter is the same story with an extra hop — a real GLSL RSX-bytecode
interpreter, with un-built variants aliased onto over-general prebuilt pipelines and promoted on
first draw via `CACHED_PIPE_UNOPTIMIZED` → `CACHED_PIPE_RECOMPILING`. That boolean sits exactly
where a counter would go. No eviction.)*

### Pipeline caching and pre-warming — the opposite axis

The industry's answer to variant cost is **ahead-of-time**, not demand-driven:

- **Vulkan `VkPipelineCache`** — *"contents … are managed by the implementation"*; the spec
  defines **no eviction policy and no size bound** (grep of the pipelines chapter for
  `evict|prune`: zero). Khronos concedes the gap in the `VK_KHR_pipeline_binary` announcement:
  *"The VkPipelineCache API provides no control over the lifetime of the binary objects that it
  contains."*
- **Fossilize / Steam Deck** — fully predictive: `.foz` files of `Vk*CreateInfo` + SPIR-V,
  collected from other users' sessions, replayed ahead of play purely to populate the driver
  cache. Uniform hardware makes it work. No fallback tier, no counters, no bound.
- **UE PSO precaching** — AOT at asset `PostLoad`. Its fallback is *not* an interpreter
  (`r.PSOPrecache.ProxyCreationDelayStrategy`: skip the draw, substitute the default material, or
  block). It has the only count-capped window found in an engine —
  `r.PSOPrecache.KeepInMemoryUntilUsed` with `KeepInMemoryGraphicsMaxNum` **8192** /
  `ComputeMaxNum` **4096** — but the motivation is **inverted**: it retains *not-yet-used* PSOs to
  defeat the NVIDIA driver's eviction. A staging window, not a working set.
- **Unity** — build-time stripping + AOT warmup; first use of an unwarmed variant is *"a visible
  stall."* Release is by **refcount**, not recency. Its one true LRU is at the wrong granularity
  and off by default: *"Unity removes the least recently used decompressed chunk from memory"* —
  a **chunk of many variants**, capped by a setting whose default is 0 = no limit, and which
  *"only has an effect on shaders Unity has not yet loaded."*
- **D3D12** — `ID3D12PipelineLibrary` is append-only. `ID3D12ShaderCacheSession` is genuinely
  bounded (`MaximumInMemoryCacheSizeBytes`, `MaximumInMemoryCacheEntries` default 128, entries
  *"temporarily stored in memory, until evicted by newer entries"*) but that is
  **insertion-recency over a blob cache**, not use-recency over live pipelines.

**DXR forecloses the idea deliberately:** *"It wasn't deemed worth the effort or complexity to
support incremental deletion, i.e. DeleteFromStateObject()."*

### Shader LOD / material LOD — nearly orthogonal

Worth stating because it is the obvious place to look and it is **not** the same thing. Unity's
manual disclaims the association people assume:

> *"Although this technique is named after the LOD feature for rendering meshes, there are
> important differences: **shader LOD does not relate to distance from the Camera, and Unity does
> not calculate shader LOD automatically.**"*

Selection is an authored integer, applied **globally** (`Shader.globalMaximumLOD`) or per shader
asset — never per-object. UE's `r.MaterialQualityLevel` is likewise a global scalability cvar
(Epic: *"Quality switches… will only be all on or off globally"*, and *"This feature is not for
distance LOD as you cannot have two quality levels at the same time"*); distance-driven cheapening
in UE is asset substitution (per-LOD material slots, HLOD proxy baking).

So shader LOD is a **quality axis chosen by a static function**, where the cheap tier is a
*visually different* shader. Our m×n selection resembles it superficially but differs on both
axes: ours is **per-pixel from cone footprint**, and the LOD-as-prefix-cut construction means the
tiers are the *same* program truncated, not different programs. The research line
(Olano 2003, Pellacini 2005, Sitthi-Amorn 2011, He et al. SIGGRAPH Asia 2015 — the most relevant
of the set) builds LOD ladders **offline** and selects by distance; Shader Components (2017)
explicitly declines the residency question: *"our design leaves the responsibility for caching and
lookup up variants to the engine."*

### Tiered JIT — where the mechanism actually lives

The CPU lineage is the honest comparator, and it has all three ingredients.

**HotSpot.** Tier 0 interpreter always resident (`Method::clear_code()`: *"Revert to using the
interpreter and clear out the nmethod"*). Promotion counts invocations **and loop back-edges**:
`Tier3InvocationThreshold` 200, `Tier3CompileThreshold` 2000, `Tier4InvocationThreshold` 5000,
`Tier4CompileThreshold` 15000. Thresholds are **scaled by pressure** — they rise with compiler
queue length and *exponentially* with code-cache fullness, i.e. the system compiles less eagerly
as residency tightens, before evicting anything. (Note `-XX:CompileThreshold` is **inert** under
tiered compilation.)

Eviction is real and pressure-gated. Post-JDK-20 the sweeper is gone, replaced by GC epochs +
nmethod entry barriers — *calling* a method refreshes its epoch, which is the usage signal:

```cpp
return CodeCache::previous_completed_gc_marking_cycle() > _gc_epoch + 2 * CodeCache::cold_gc_count();
```

with the comment *"reduce code cache pressure and get rid of nmethods that don't seem to be all
that relevant any longer."* With no pressure, `_cold_gc_count = INT_MAX` — *"No code cache
pressure; don't age code"* — so nothing is evicted however cold it is.

**⭐ The single most transferable detail in this addendum: reheating is free and deliberately
uncharged.** `Method*` metadata — invocation counters and the profile data — is independent of the
compiled nmethod and **survives eviction**, so a flushed method resumes interpreting with counters
intact and re-crosses the threshold almost immediately. Critically, age-based eviction uses
`Reason_tenured`/`Reason_age`, which are **excluded from the recompilation cutoffs**
(`PerMethodRecompilationCutoff` 400). That exclusion is exactly what makes unlimited
demote/re-promote cycling safe rather than a path to permanent deoptimization.

**V8** budgets by `invocation_count_for_* × bytecode_length` (Maglev 400, TurboFan 3000), so a
bigger function needs proportionally more work to tier up — directly relevant to variants whose
instruction counts differ by an order of magnitude across the m×n family. V8 also **evicts the
bytecode itself by age**: `flush_code_based_on_time` defaults true, so the live policy is
`bytecode_old_time` **180 seconds** (not the 6-GC figure), age reset on each execution.

**.NET is the informative negative**: counter-driven promotion (`TC_CallCountThreshold` 30) but
**no interpreter tier and no eviction** — its own design doc says *"There is no unified mechanism
for lifetime management of different code versions,"* listed as future work.

### One counter-driven thing in graphics — and its authors flag our loop as unbuilt

**NVIDIA AZP/Zeroploit** (Stephenson & Rangan, arXiv 2011.10550, 2020), prototyped on a real
GeForce driver branch, value-profiles shader scalars using **global-memory atomic counters** and
JITs specialized regions behind a runtime `VOTE.ALL` guard selecting *"either the specialized
region, or the default fallback region."* But the fallback is **intra-shader** (both paths in one
binary, not two resident programs), the counters are per-*variable* and collected **offline**, and
there is no variant cache or eviction. The authors name *"an adaptive compilation strategy based
on continuous online profiling"* as **future work** — NVIDIA flagging the loop as unbuilt.

The only place usage counters govern *program residency* is a **Qualcomm patent** (US9530245B2):
two tiers (on-chip instruction memory ⟷ system memory), driver tracking *"the last time that the
shader program was accessed or executed and/or the frequency,"* with LFU, LRU, and a hybrid. But
every program there is the same fully-compiled kind — a program *cache*, not a *tiering* system.

## Verdict

| system | interpreted fallback? | promotion trigger | bounded window + eviction? |
|---|---|---|---|
| **Dolphin Hybrid** | **yes** (GPU TEV interpreter) | **first use** | **no** — monotonic |
| RPCS3 | **yes** (GLSL bytecode interp) | **first use** | no |
| radeonsi non-monolithic→optimized | no (machine code) | first use, never stalls | no (caps + degrades) |
| Cycles Metal generic→specialized | no | load-time | no |
| VkPipelineCache / Fossilize | no | AOT / predictive | no policy defined |
| UE PSO precaching | no (default material) | AOT at load | count cap, **inverted** purpose |
| Unity | no | AOT + first-use stall | LRU over *chunks*, off by default |
| NVIDIA AZP | intra-shader fallback | **offline value profile** | no |
| **HotSpot / V8** | **yes** | **counter (+ back-edges)** | **yes, pressure-gated** |

**The delta, stated plainly: the proposal is a faithful import of tiered JIT into GPU program
variants, and that import has not been made.** Dolphin owns the interpreted tier and half of the
promotion story; UE owns a weak, inverted version of the window; HotSpot and V8 own the whole
mechanism but for CPU methods. Nobody joins a hotness counter to a bounded window with demotion
back to an interpreted tier on the GPU — and the two closest graphics systems both grow their
compiled set as the *cumulative* set of everything ever seen, which is exactly the failure mode a
procedurally generated m×n variant family cannot afford.

So: **novel as an assembly, not as a mechanism.** The honest framing for the design doc is not
"we invented usage-driven program residency" but "we are importing tiered JIT — a 25-year-old,
well-understood mechanism — into GPU program variants, where it has not previously been applied,
because procedural recipe families make the variant space unbounded in a way emulator UID spaces
are not."

## What to steal, concretely

1. **Counters must survive demotion, and demotion must not count against re-promotion.** HotSpot's
   `Reason_tenured` exclusion is the load-bearing detail — without it, cycling across the n±1
   window boundary would permanently strand a variant in S0. Our hysteresis addresses the popping;
   this addresses the *accounting*.
2. **Scale the promotion threshold by pressure before evicting.** HotSpot raises thresholds
   exponentially as the code cache fills, and evicts nothing while pressure is absent
   (`_cold_gc_count = INT_MAX`). Since the plan's budget is *pipeline count*, the analogue is:
   promote freely below the pipeline budget; raise the hit threshold as it fills; evict only above
   a watermark.
3. **Budget by program size, not just hit count.** V8 multiplies its counter by bytecode length.
   Our variants differ by an order of magnitude in instruction count (a ~200–400-instruction
   boundary prefix vs a 1500-instruction recipe), so a flat hit threshold would over-promote cheap
   variants and under-promote expensive ones.
4. **The fallback must be correct on its own, never merely a placeholder.** OptiX states this as
   an invariant for bound values: *"No module should rely on the value being specialized in order
   to work correctly."* This is what makes demotion safe, and it is the program-side twin of the
   data-side "never stall on a miss" law in §8.
5. **Never stall on a pending promotion.** radeonsi's trick is worth copying verbatim: on an
   unsignalled compile fence it does not block — it zeroes the `opt` bit in a local key copy and
   re-selects the generic variant. Combined with our fail-soft invariant, that keeps a compile
   hitch off the frame path by construction.

**Sources for this addendum:** [Dolphin ubershaders](https://web.archive.org/web/2023/https://dolphin-emu.org/blog/2017/07/30/ubershaders/) + [ShaderCache.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/ShaderCache.cpp) (eviction/counter absence verified by grep at master, 1473 lines) ·
[RPCS3 VKShaderInterpreter](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/VK/VKShaderInterpreter.cpp) ·
[Vulkan pipeline cache spec](https://docs.vulkan.org/spec/latest/chapters/pipelines.html) + [VK_KHR_pipeline_binary announcement](https://www.khronos.org/blog/bringing-explicit-pipeline-caching-control-to-vulkan) ·
[UE PSO precaching](https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine) ·
[Unity shader loading](https://docs.unity3d.com/6000.2/Documentation/Manual/shader-loading.html) + [Shader LOD](https://docs.unity3d.com/2021.3/Documentation/Manual/SL-ShaderLOD.html) ·
[D3D12_SHADER_CACHE_SESSION_DESC](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_shader_cache_session_desc) ·
HotSpot [compilationPolicy.cpp](https://raw.githubusercontent.com/openjdk/jdk/master/src/hotspot/share/compiler/compilationPolicy.cpp) / [nmethod.cpp](https://raw.githubusercontent.com/openjdk/jdk/master/src/hotspot/share/code/nmethod.cpp) / [compiler_globals.hpp](https://raw.githubusercontent.com/openjdk/jdk/master/src/hotspot/share/compiler/compiler_globals.hpp) + [JDK-8290025](https://bugs.openjdk.org/browse/JDK-8290025) ·
V8 [flag-definitions.h](https://raw.githubusercontent.com/v8/v8/main/src/flags/flag-definitions.h) / [tiering-manager.cc](https://raw.githubusercontent.com/v8/v8/main/src/execution/tiering-manager.cc) ·
.NET [code-versioning.md](https://raw.githubusercontent.com/dotnet/runtime/main/docs/design/features/code-versioning.md) ·
[Stephenson & Rangan, AZP](https://arxiv.org/pdf/2011.10550) ·
[Qualcomm US9530245B2](https://patents.google.com/patent/US9530245) ·
[He et al., Rapid Automatic Shader LOD, SIGGRAPH Asia 2015](http://graphics.cs.cmu.edu/projects/lodgen/lodgen.pdf) ·
[Shader Components 2017](https://d1qx31qr3h6wln.cloudfront.net/publications/he17_shadercomp.pdf)

**Not found / unverified:** a measured ubershader-vs-specialized slowdown factor, specialized
compile time in ms, or per-game variant count (the Dolphin blog is qualitative on all three);
vendor confirmation of the NVIDIA driver-cache eviction policy that UE's `KeepInMemoryUntilUsed`
exists to fight; reported LRU-over-PSO in yuzu/shadPS4 (search-summary only, not source-checked);
"adaptive shader compilation with an eviction policy" as an academic topic — no such literature.
