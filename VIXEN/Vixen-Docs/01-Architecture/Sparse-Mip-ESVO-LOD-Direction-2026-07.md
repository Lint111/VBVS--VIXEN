# Sparse-Mip ESVO LOD — Direction (2026-07)

**Status:** ✅ INC1 SHIPPED (2026-07-06, on `feat/sparse-mip-esvo-inc1`, not yet merged to main) — see [[Sparse-Mip-ESVO-LOD-Inc1-Plan-2026-07]] for the full milestone-by-milestone implementation record (M1-M5, all gates green, Opus-validated). This direction doc's original bandwidth framing was an estimate ("the upper ~4 levels of node data plus their samples render a distant object for almost free, with the brick pool never resident" — the "Why it fits now" section, retained below unmodified as historical rationale); **M5's live A/B measurement (Task 12) replaces that estimate with real numbers**: N=16 real `BodyOctreeSceneNode` trees (3 shell-octree kinds each, `kShellDepth=6`/64³ grid), comparing (a) every tree's bricks requested+uploaded via `BatchedUploader` (the pre-Inc1-equivalent "every body's bricks get uploaded" cost) vs (b) every tree staying mip-only (Inc1's real on-demand trigger behavior for a body that never crosses the resolvability/frustum/occlusion gate): **26,759,424 bytes (~25.5 MB) uploaded in condition (a) vs exactly 0 bytes in condition (b), reproduced identically across 3 independent runs** — a 100% reduction, not just "almost free" — and a **31-51ms vs 0.18-0.23ms wall time (≈170-220x faster, WSL2/Dozen scheduling jitter on the baseline side)** for the residency-service phase across all 16 trees. Full no-regression sweep (every built gtest binary — 144 real ELF test executables spanning `binary`/`Procedural`/`Stored-SDF`/multi-channel/recipe-pool and the rest of the engine, run directly per KI-014's documented workaround): **133/144 passed**; all 11 failures independently confirmed pre-existing/unrelated to this increment (asset-path issues in UI tests, known SVO-core issues predating this branch, and one flaky async-completion race in this increment's own M2 test `test_partial_brick_upload` — reproduces intermittently, ~1-in-3 runs, with zero code changes between runs, so it is a real pre-existing timing race in `UploadBrickPool`/`PollBrickUploadCompletion`, not something M5 introduced; worth a follow-up fix, not a blocker for this gate). See the Inc1 plan's M5 Progress Log for the full failure-by-failure accounting. Next: nested tree-of-trees (T0/T1/T2 planetary tiers) is [[Tiered-ESVO-Observer-Addressing-Design-2026-07]]'s scope, explicitly not Inc1's (§0). Original framing preserved below unedited except this banner.

**Status (historical, pre-Inc1):** DIRECTION (user-proposed 2026-07-04, mid widescreen-perf program). Not scheduled; natural next epic after the widescreen close-out — it is the real fix for the deferred "many-bodies bandwidth" items (Widescreen findings ranks 4/5/6/10) and the enabler for undertow's 60-300-body path and provider-LOD (AR#41/48).

## The idea (user's mechanism)

Add a filtered **value sample per tree level** to the ESVO, maintained when the tree is updated — a sparse-mipmap-like structure. Upload the samples as **another dataset following the same sparse assignment order as the brick data**. During traversal: check for brick data; if it does not exist (not resident), look up the mip sample **at the same ordinal in the other dataset**. Far from a tree, the mip data dominates naturally — the upper ~4 levels of node data plus their samples render a distant object for almost free, with the brick pool never resident.

## Why it fits now

- **The cutoff machinery already exists**: `raySizeCoef` (honest per-extent since widescreen M4, recomputed through the recompile cascade) tells each ray the level where its cone footprint exceeds voxel size. What the format lacks is anything to shade with at an interior stop — attributes live only in leaf bricks (Inc3 SoA channel pool). Per-level samples fill exactly that hole.
- **Two triggers, one read path**: (1) footprint cutoff = deliberate LOD; (2) brick residency miss = streaming grace (shade coarse, refine when bricks arrive — no holes, no stalls). Both land on `mip[level][ordinal]`.
- **Update-path synergy**: samples refresh bottom-up along dirty paths during materialization/edits (O(depth) per edit) — the same maintenance shape destructible ships need; initial fill is nearly free during bake's bottom-up build.
- **Residency policy falls out**: mip dataset is tiny (interior nodes ≈ 1/7 of leaf count × sample bytes) → **pinned per registered tree**; bricks become the evictable tier under the budgeted octree pool (recipe pipeline). Every tree is always at least mip-renderable → many trees per frame without bandwidth cliffs.

## Design decisions to make (spec-time)

1. **Sample placement**: parallel per-level SoA pool indexed by level-local node ordinal in the existing serialization order (NOT widening the 8-byte ESVO node — traversal stride stays untouched). Read-by-semantic like the Inc3 channel pool.
2. **Existence check**: sentinel in the existing leaf→brick offset mapping (`INVALID_OFFSET` = not resident → mip fallback). One indirection, divergence-tolerant. Verify spare-bit vs sentinel cost in the real node/offset layout.
3. **Filtering semantics per channel**: color/roughness = weighted mean (by child coverage); **SDF must NOT be mean-filtered** — conservative min-magnitude / coverage-occupancy semantics per level (Inc3 lesson: "solid = Density>0" is a binary-voxel assumption; the same trap recurs here).
4. **v1 = hard switch** at fallback; v2 nicety: fractional-LOD lerp between adjacent level samples to hide refinement pop.
5. **Format versioning** rides the existing single-source codegen (VRC container + `[GpuStruct]`/kernel framework) — no hand-mirroring.
6. **Memory/bandwidth budget math** in the spec: samples/tree, pinned-set size at N trees, expected far-view working set vs today.

## Planetary scale (the vision this buys)

"A true spaceship approach: a planet going from a small dot, to a large body in orbit, to a detailed surface when we land" (user, 2026-07-04) — with no impostor/billboard handoff, because dot/orbit/surface are the SAME tree at different residency depths: dot = top levels pinned (KB); approach = progressive residency descent; landing = brick tier streamed in around the landing region only (the per-node sentinel fallback makes partial in-tree residency free — everything else keeps serving mip samples).

Two spec-time realities:
1. **Depth + precision**: planet-at-1cm is ~40 levels — past practical single-tree depth and past float32 (which dies ~10^7 m, i.e. at orbital distances). Shape that preserves the design: **nested trees** (tree-of-trees — the planet's deep leaves are themselves trees; the same-ordinal mip dataset applies at every nesting tier; a non-resident SUBTREE is just another miss served by the parent tier's sample) + camera-relative / floating-origin traversal transforms.
2. **Residency becomes spatial within a tree** for landing: brick-tier budget selects by frustum+distance inside the tree, not per-tree all-or-nothing. Policy knob, not a format change — the fallback already tolerates arbitrary per-node miss patterns.

## Nested trees = recursive chunking, sharpened (user Q 2026-07-04)

Yes — tree-of-trees is recursive chunking, with two upgrades over classic chunk grids: (1) every tier is RENDERABLE (mip samples), not just an index — an unloaded region shades coarse instead of popping/holing; (2) only bottom-tier trees own bricks ("bedrock"), so the bandwidth ceiling is `#resident bottom trees × brick working set` — bounded by residency policy, independent of world size (world growth only grows the tiny pinned mip tiers). Sharpenings: bedrock is sparser than it sounds — uniform regions (solid interior / empty) need NO bricks, their mip sample is exact; and bedrock can be PROCEDURAL — bottom trees generated on demand via the recipe/kernel pipeline (recipe + edit overlay) so the real limit is min(generation, transfer) per region. Mechanics: ~10 levels per tier keeps the 23-entry traversal stack and per-tier ordinal indexing comfortable; cross-tier descent is a traversal restart into the child tree (bounds per-ray state).

## Concrete tier math for the 1 cm planetary configuration (user Q 2026-07-04)

Earth-scale body (Ø ~12,700 km) at 1 cm = 2^30-2^31 fine cells across → ~30-31 effective levels. Three tiers, ~10 effective levels each:

| Tier | Spans | Levels | Leaf cell | Role |
|---|---|---|---|---|
| T0 planet | 12,700 km | 10 | ~12.4 km | ALWAYS pinned; its mip samples are the planet-from-orbit |
| T1 region | ~12.4 km | 10 | ~12 m | mip sets stream by proximity; leaves spawn T2 trees |
| T2 bedrock | ~12 m | ~7 node levels + 8^3 brick | ~1 cm | bricks ONLY here — this tier IS today's ESVO-leaf→brick format, unchanged |

Key numbers: T0 surface-intersecting leaves ≈ 3-4M (5.1e8 km^2 / 154 km^2) → tens of MB pinned. The 1 cm bedrock shell (~1e18 voxels planet-wide) is NOT storable and doesn't need to be: T2 trees are PROCEDURAL (recipe + edits overlay, bake-on-demand via the kernel/recipe pipeline), generated near the camera, evicted freely; only edited regions persist. Working sets: orbit = T0 mips (MBs); approach = facing-hemisphere T1 mips (MBs); landed = concentric residency rings (1 cm bricks within ~50 m ≈ a few hundred T2 trees, tens-hundreds MB, policy-tunable). Precision: each tier traverses in its own local [1,2) frame (ESVO already does), float32-safe per tier; only the CPU-side chain of tier origins needs double/fixed-point (undertow sim space); cross-tier restart re-anchors precision. Edits at 1 cm cost O(30): T2 brick write + bottom-up mip refilter + one leaf-sample refresh per ancestor tier. Residency-rule refinement: "mip tier pinned per tree" = pinned for T0 only; T1/T2 mip sets stream by proximity (T0 samples cover anything whose T1 isn't resident).

## Clone-aware data (content-addressed dedup — user proposal 2026-07-04)

Make the format data-clone aware: hash content at bake/stream time; if identical data is already GPU-resident, REFERENCE it instead of storing/uploading a copy. Split by tier:
- **Bricks: YES** — the leaf→offset indirection already exists, so dedup is structural: content hash → resident-hash table (hash→offset+refcount) → hit: reuse offset, skip upload; miss: allocate+upload+register. SVDAG-family results: 10-100× on geometry-like channels. Duplicates cost a 4-byte ref instead of a KB transfer.
- **Mip samples: NO** — their efficiency IS the implicit ordinal addressing (no indirection) and payloads are ~8 B; a reference layer costs more than it saves.

Locked consequences: (1) **per-CHANNEL dedup** — the SoA read-by-semantic pools hash independently (brick = tuple of channel refs); occupancy/geometry dedup best, SDF-of-canonical-surfaces well, color worst — separation keeps good channels unhostaged; (2) **copy-on-write on edit** of any refcount>1 brick (correct destructibles semantic anyway; pool refcounts double as eviction bookkeeping); (3) **recipe-level cloning is the bigger planetary win** — same recipe + same canonical inputs → identical T2 trees; dedup at tree level short-circuits GENERATION, not just storage (tree-sharing already proven live by BodyInstanceRayMarch's many-instances-one-octree). 128-bit content hash (collision risk a non-thought at planet scale); hash cost is noise vs the saved upload. v2+ (explicit non-goal now): symmetry-canonical dedup (rotation/mirror matching).

## Non-goals (this direction)

Mip-BRICK sets (2×2×2 filtered mini-bricks per level) — heavier; per-node samples first. Contours/DAG dedup — orthogonal.
