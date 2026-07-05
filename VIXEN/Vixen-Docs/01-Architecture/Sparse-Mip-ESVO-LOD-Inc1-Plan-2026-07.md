# Sparse-Mip ESVO LOD — Increment 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or the
> post-brainstorm-context-manager pipeline to implement this plan milestone-by-milestone. Steps use
> checkbox (`- [ ]`) syntax. **Each milestone's GPU/shader work is verified by the controller**
> (Windows ninja build + live gate); CPU work is gtest-verified. See the Stored-SDF-Provider-Inc2
> plan's "Critical project facts" for the worktree-build protocol (sandbox-off, `cmake --preset
> vixen-ninja` from the worktree, artifact+test ground-truth, never overlap builds). **Live-run gate
> is authoritative for GPU/render work** — static review has repeatedly passed runtime bugs on this
> project (auto-sync P4 precedent); every milestone below that touches the shader or the upload path
> ends in an actual `VIXEN.exe` run, not just a clean compile.

**Goal:** Ship the base epic from [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] — a per-level filtered
value sample added to the ESVO, read as the fallback whenever a leaf's brick is not resident (either
because the ray's LOD cutoff says "don't bother" or because the brick genuinely hasn't been
uploaded/generated yet) — **plus the partial/streamed upload mechanism** the mip fallback requires to
pay off: today's octree buffers are single monolithic host-visible SSBOs uploaded whole at
`CreateOctreeBuffers` time (`BodyOctreeSceneNode.cpp:419-480`); this increment adds the ability to
upload **only the node array + mip pool** for a tree (cheap, KB-scale) while leaving its brick pool
empty/unpopulated until something is actually close enough to need it. "Close enough" is a derived
quantity, not a tuned constant — the finest octree level worth having brick-resident is a closed-form
function of the tree's distance from the camera **and the active camera's FOV** (zoom/telescope), not
distance alone (§ M4/Task 10: `minResolvableLevel(d, fov, screenHeightPx)`), the same
screen-space-footprint reasoning `RaySizeCoefNode` already applies per-ray, applied once per-tree to
gate residency instead of per-pixel to gate traversal depth. **Nested tree-of-trees (T0/T1/T2
planetary tiers, cross-tree `TierRef`) is explicitly NOT this increment** — see §0 scope.

**Architecture:** Add a parallel per-level SoA sample pool to `ConcatenatedOctrees`/`SerializeSdf`,
filled bottom-up during bake (mirrors how `SerializeSdf` already walks the octree to emit
nodes/bricks). Extend `ChildDescriptor`'s existing brick-mode sentinel check
(`hasBrick()`/`INVALID_BRICK_INDEX`, `SVOTypes.h:102-104`) so a **not-yet-resident** brick is
indistinguishable, at traversal time, from "no brick data at this leaf" — both fall back to
`mip[level][ordinal]`. Wire brick uploads through `ResourceManagement::BatchedUploader` (staged,
offset-targeted, async — already built, currently unused by `BodyOctreeSceneNode`) instead of the
current whole-buffer `CreateHostBuffer` path, so a tree's brick pool can be allocated at full size but
populated incrementally, on demand, per brick.

**Tech Stack:** C++23, GLSL compute (std430 SSBO), glm, GoogleTest, CMake ninja preset, Vulkan 1.3.

**Prior art:** Crassin, Neyret, Lefebvre, Eisemann (2009), "GigaVoxels: Ray-Guided Streaming for
Efficient and Detailed Voxel Rendering" (I3D 2009) — the compressed conference announcement, read in
full from the author's own PDF (`maverick.inria.fr/Publications/2009/CNLE09/CNLE09.pdf`) 2026-07-05.
Superseded, for Plan purposes, by **Crassin's PhD thesis** (2011, "GigaVoxels: A Voxel-Based Rendering
Pipeline For Efficient Exploration Of Large And Detailed Scenes," 207pp — the full treatment; Chapter 7
"Out-of-core data management," pp.117-155, is the load-bearing chapter) and a direct 2024 follow-on,
Richermoz & Neyret, "GigaVoxels DP: Starvation-Less Render and Production for Large and Detailed
Volumetric Worlds Walkthrough" (HPG 2024, hal-04654692) — both user-supplied 2026-07-05, read in full.
This corrects and deepens the earlier vault-summary-only read
(`Vixen-Docs/03-Research/GigaVoxels-Streaming.md`), which only reflects the 2009 paper.

**Core mechanism match (thesis Ch.7, more precise than the 2009 paper's version):** an octree node
stores either a brick pointer or a constant value; on a cache miss the ray falls back to a coarser
level instead of stalling; both the node pool and brick pool are managed as **two instances of one
generic GPU cache** (page table + data pool, §7.3.1-7.3.2), each with an LRU replacement policy
maintained **entirely on the GPU** (§7.3.4) — no CPU-mirrored structure, unlike prior art the thesis
explicitly critiques (Gobbetti et al. [GMAG08] requires a CPU-side clone structure updated every frame).
This Plan's `hasBrick()`/`INVALID_BRICK_INDEX` → `mip[level][ordinal]` fallback (§ Architecture) and
`BatchedUploader`-based on-demand population (M2) are the same shape as this cache-miss/fallback core.

**The request mechanism, corrected (thesis §7.3.3, simpler than the 2009 paper's HistoPyramid scheme):**
NOT a per-ray output list requiring stream-compaction-of-lists as I first read from the 2009 paper alone
— the thesis's mature version uses a **flat request buffer sized 1:1 with the page table**. Each page
table slot has a corresponding request-buffer slot; a ray needing that page writes the **current
frame's timestamp** into the slot (not a boolean, not an atomic-append). Because every ray that needs
the same page writes the *same* value, no atomic operation and no uniqueness pass is needed — the
buffer is self-deduplicating by construction (§7.3.3, "Data requests on the page table"). A subsequent
GPU stream-compaction pass (§7.3.5) extracts only the flagged (current-timestamp) slots into a compact
request list for the producer. Usage tracking for the LRU (§7.3.4) uses the identical trick with a
separate usage buffer. **This is the mechanism to cite going forward, not the 2009 paper's HistoPyramid
description** — it is the more mechanically simple and more directly portable-to-VIXEN version.

**The churn/synchronization problem, precisely diagnosed (this is the actual answer to "would GigaVoxels
reduce bandwidth churn"):** the thesis states its central claim directly — "The most important
characteristic of our approach compared to previous methods is that it places the control of the whole
paging and caching scheme on the GPU, removing the need for costly communications and synchronization
between the CPU and the GPU" (§7.1.2). Two *measured* comparisons quantify this (§7.5.3-7.5.4, GTX480 +
Core2 Duo E6850):
- **Transfer rate, GPU kernel-fetch vs. CPU-triggered `cudaMemcpyToArray` copies**: GPU streaming reaches
  ~half the theoretical 8GB/s PCIe bandwidth at scale (e.g. ~4.1GB/s at 172 bricks of 18³ voxels);
  CPU-triggered copies top out at **1/40th** of theoretical bandwidth for small (18³) bricks and **1/5th**
  for larger (66³) bricks, and the CPU approach's relative disadvantage *grows* with brick count — the
  opposite of what you'd want as scene complexity increases.
- **LRU management cost, GPU vs. CPU**, at 64B pages: GPU approach is **1.7×-27.5× faster**, with the
  advantage growing as managed-pool size grows (64MB→1MB tested range).
- The 2024 follow-on paper diagnoses the *remaining* churn even in the GPU-cache design: alternating
  discrete render/production passes (even GPU-driven ones) still stalls whenever a pass's rays finish at
  different times — "a lot of idle time... at the end of a frame — the 'tail' regime, which can sometimes
  represent more than half of the total time" — fixed via CUDA Dynamic Parallelism (a ray hitting a miss
  launches its own production kernel directly, no pass boundary at all), measured at **1.1×-4.4× speedup,
  average 2.1×**, with the *largest* gains specifically on high-disocclusion/high-churn scenes (moving
  backward through a wall: up to 4.4×) — i.e., confirms this exact class of fix helps most precisely
  where churn is worst.

**So: yes, unambiguously, "incorporating GigaVoxels" (the actual GPU-native cache+request mechanism, not
just the mip-fallback idea Inc1 already borrows) would reduce bandwidth churn measurably** — the
question from earlier in this conversation is answered with real numbers, not just "it depends." The
reason it's still not simply "build it now":
- **CUDA-specific mechanism, not directly portable — but not a dead end (verified 2026-07-05, corrects
  an earlier flat "no Vulkan equivalent" claim in this doc).** Both the thesis's cache and the 2024
  paper's dynamic-parallelism scheduler rely on CUDA-specific capabilities (direct system-memory-mapped
  kernel fetches; CUDA Dynamic Parallelism, i.e. GPU kernels launching kernels) with no *safe,
  cross-vendor* Vulkan equivalent today:
  - **`VK_EXT_device_generated_commands`** (standardized 2024) is real and usable now — GPU writes
    indirect-dispatch/draw arguments to a buffer, executed without a CPU round-trip to inspect/record
    them. Close to what Inc2's request-buffer/LRU trick actually needs (GPU decides, CPU doesn't inspect
    contents first) — does NOT require kernels launching kernels.
  - **Vulkan Work Graphs** (`VK_AMDX_shader_enqueue`) is the true analog to CUDA Dynamic
    Parallelism — GPU nodes recursively enqueuing further GPU nodes (`maxExecutionGraphDepth`, min 32) —
    exactly the "ray hits a miss → launches its own production kernel" pattern the 2024 paper uses.
    **But it is AMD-only and experimental** (`AMDX` prefix, not `EXT`/`KHR`; no confirmed NVIDIA support
    as of this check) — not viable to architect a cross-vendor VIXEN feature around yet. Worth
    rechecking periodically for standardization, not ruling out permanently.
  - **The portable workaround, if the dynamic-parallelism fix is ever pursued before Work Graphs
    standardizes: persistent-thread / megakernel scheduling** — the technique the 2024 paper's own
    §2.2 names as the pre-dynamic-parallelism alternative ("the persistent thread strategy settles the
    dependent tasks as producers-consumers connected via FIFO arrays," citing Nanite as a shipping
    example). Concretely: one compute dispatch sized to fill actual GPU occupancy (not one thread per
    task), each thread loops pulling work items off a GPU-resident atomic-counter ring buffer until
    empty — including items pushed onto that same queue by other threads mid-dispatch. This
    genuinely approximates "kernel launches kernel" using only atomics + a ring buffer in plain
    portable compute-shader code, no vendor extension required — it is what Nanite actually ships on
    today, not Work Graphs. The real cost: ray-continuation and brick-production are different,
    differently-parallel work domains (the 2024 paper itself flags this: "a rendering warp can touch
    several bricks while a brick can be requested by several rays") — a persistent-thread emulation
    likely needs two cooperating queues (one per domain) rather than the single recursive call a true
    dynamic-parallelism/work-graph primitive gives for free, so expect coarser granularity or more
    hand-built scheduling logic than the paper's version.
- **The request/LRU mechanism (thesis §7.3, CUDA-portability aside) is buildable in Vulkan compute** —
  the flat-request-buffer-plus-timestamp trick has no CUDA-specific dependency; it's a plain SSBO write
  pattern. This is the part worth actually planning for Inc2 (below). The dynamic-parallelism scheduling
  fix (2024 paper) is the part that's architecturally hardest to port and should be evaluated separately,
  later, only if VIXEN's own two-pass structure shows the same tail-regime stalling this fix targets.
- Also narrower in ambition than either source: this Plan targets bandwidth/upload-cost reduction across
  many in-memory-resident bodies, not their out-of-core (disk→system-memory→GPU) tier for datasets that
  cannot fit in memory at all (thesis example: a 4096³ Visible Human dataset, 256GB on disk) — VIXEN's
  octree buffers already fit in host-visible memory today; there is no disk tier in scope here.

**Inc2 candidate (not scoped, not started): adopt the thesis's flat request-buffer + GPU-side LRU,
replacing Inc1's per-tree formula with per-brick, ray-observed demand.** This directly closes the
over-request gap already noted in §0 (a large partially-visible/partially-occluded body currently
requests its *whole* brick tier once any part crosses the resolvability threshold) — residency would
instead be driven by which bricks rays actually touched this frame, self-deduplicated by the
timestamp-write trick (§7.3.3), with GPU-side LRU eviction (§7.3.4) replacing Task 10's
distance/FOV/frustum/occlusion estimate entirely for the brick tier. Concretely this needs: (a) a
request buffer + usage buffer sized to the brick page table, written by `BodyInstanceRayMarch.comp`'s
existing leaf-hit path (the loop already knows which brick it needed); (b) a GPU stream-compaction pass
extracting flagged slots (verify whether VIXEN has an existing stream-compaction primitive before
building the thesis's exact scheme — this is a well-known GPU primitive, likely available via an
existing library rather than hand-rolled); (c) GPU-side LRU list maintenance (§7.3.4, sorted by
timestamp) rather than CPU-side residency bookkeeping; (d) CPU-side consumption of the compacted
request list still feeds the same `BatchedUploader` plumbing Inc1 already builds (M2) — Inc2 changes
WHAT triggers a residency request and WHERE eviction decisions are made, not HOW a request is fulfilled.
**Deliberately not the 2024 paper's dynamic-parallelism scheduling fix** — that requires GPU
kernels launching kernels, a genuine Vulkan-portability problem, and should only be evaluated if Inc2's
simpler request-buffer adoption still shows the same render/production pass-boundary stalling the 2024
paper diagnoses. **Not worth building now**: per Task 10's CPU-vs-GPU note, Inc1's coarser per-tree
formula is measurably sufficient at current/near-term tree counts (tens to a few hundred bodies); this
Inc2 candidate is motivated specifically by large, partially-occluded/partially-visible single bodies (a
planet, per the parent direction doc's T0/T1/T2 tier math) — i.e. it's a natural companion to the
nested-tree epic ([[Tiered-ESVO-Observer-Addressing-Design-2026-07]]), not an Inc1-scale concern. Decide
whether to build this once M5's bandwidth measurement (Task 12) is in and nested-tree work is actually
scheduled, not before.

**If ever pursued: gate through the existing `CapabilityGraph`, not a bespoke detection path (user
insight 2026-07-05) — this turns "requires a rare/experimental extension" from a hard blocker into a
device-capability-gated feature tier.** VIXEN already has the exact mechanism this needs, generic and
already wired into every node: `libraries/VulkanResources/include/CapabilityGraph.h`'s
`DeviceExtensionCapability`/`DeviceFeatureCapability` nodes (checked against the physical device's
actual `vkGetPhysicalDeviceFeatures2`-queried support) compose via `CompositeCapability`, and every
`NodeType` already declares a `requiredCapabilities` field (`NodeType.h`, `DeviceCapabilityFlags`)
consulted at graph-build time. No new capability-detection system needed — register
`DeviceExtensionCapability("VK_EXT_device_generated_commands")` and (if/when it standardizes past
`AMDX`) the work-graphs extension the same way every other optional Vulkan feature in this codebase
already is, and let the residency/scheduling node's `requiredCapabilities` pick the variant:
Work-Graphs-driven scheduling if available → `device_generated_commands`-based request/LRU if that's
present but Work Graphs isn't → the persistent-thread/megakernel emulation (no extension dependency at
all) → Inc1's plain per-tree CPU formula as the universal floor. Users on capable hardware get the
bandwidth benefit automatically, with zero user-facing setting and no per-feature detection code beyond
the standard `requiredCapabilities` declaration — exactly the shape every other optional feature in this
render graph already follows.

**Design of record:** [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] (direction + design decisions §1-6).
**Reuses:** `SerializeSdf`/`ConcatenateSdf` (`ShellOctreeGpu.h`), `ChildDescriptor` brick-mode sentinel
(`SVOTypes.h`), `RaySizeCoefNode` (LOD cutoff), `ResourceManagement::BatchedUploader` (staged async
upload — exists, not currently used by the octree path).

---

## §0. Scope

**In scope for Inc1:**
- Per-level mip sample pool: SoA storage, bake-time bottom-up fill, GPU upload, shader fallback read.
- SDF-safe filtering semantics (conservative min-magnitude, not mean) and color/roughness mean
  filtering.
- The **partial-upload mechanism**: allocate a brick pool's buffer at full capacity but populate
  brick data lazily via `BatchedUploader`, so a tree can be "mip-only resident" (nodes + mip samples
  uploaded, zero bricks uploaded) and still render correctly from arbitrary distance.
- A residency policy simple enough to prove the mechanism: **per-tree binary** (a tree's bricks are
  either "not requested" or "fully uploaded"), not per-brick frustum/distance selection within a
  single tree. Spatial-within-a-tree brick residency (needed for planetary landing) is explicitly
  deferred (§0 out-of-scope) — proving the sentinel-fallback mechanism at tree granularity is the
  gate for that later, harder policy.

**Out of scope for Inc1** (per [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] and
[[Tiered-ESVO-Observer-Addressing-Design-2026-07]], both explicitly sequenced downstream of this):
- Nested trees / tree-of-trees / `TierRef` / cross-tree traversal restart.
- Fractional-LOD lerp between adjacent mip levels (v2 nicety per the direction doc).
- Clone-aware/content-hash dedup.
- Any undertow-side reification or delta-log work.

**Why partial upload belongs in THIS increment and not later:** the mip sample's entire value
proposition — "a distant object renders for almost free, brick pool never resident" — is not
observable without a way to *not* upload the brick pool. Shipping mip sampling alone, on top of
today's whole-buffer upload, would prove the shader-side fallback logic but not the bandwidth claim
that motivates the epic. The two are one gate.

---

## Milestone Map

- **M1 — Mip sample bake + SoA serialize** (Tasks 1-3) · gate: `test_mip_sample_bake` gtest green.
- **M2 — Brick-pool partial allocation + `BatchedUploader` wiring** (Tasks 4-6) · gate:
  `test_partial_brick_upload` gtest green (CPU-observable: buffer allocated, brick region unwritten
  until requested) + controller-run `VIXEN.exe` link.
- **M3 — Shader fallback read (existence check + filtering dispatch)** (Tasks 7-9) · gate: shader
  compiles + lavapipe offscreen render test (mip-only tree renders a recognizable silhouette with
  zero bricks uploaded).
- **M4a — Resolvable-level formula** (Task 10, part 1: `minResolvableLevel` function + its two-scenario
  test) · gate: unit test green, pure CPU math, no GPU/render dependency. Split out of the original
  single Task 10 2026-07-05 — see "Milestone re-split" note below.
- **M4b — Frustum + occlusion gates** (Task 10, part 2: frustum containment reuse, hysteresis margin,
  the two-tier occlusion mechanism — GPU per-ray `bestT` reject + optional CPU-side residency
  occlusion gate) · gate: the GPU per-ray fix's iteration-count unit test green; the CPU-side gate's
  unit test green if built this increment (scope-decided per its own note, may defer to Inc2). Depends
  on M4a's formula existing (the combined trigger references `minResolvableLevel`), but is a distinct
  mechanism with its own test surface — not folded into M4a.
- **M4c — Trigger wiring + live gate** (Task 10, part 3: the combined `RequestBrickResidency` call +
  capability-graph gating note + Task 11's live gate) · gate: `VIXEN.exe` live run, three-scenario test
  (distance/zoom/orientation-driven) from Task 10's closing bullets, no stall/hitch, no regression.
  Depends on M4a + M4b both landing first — this is the integration milestone, not a fourth independent
  mechanism.
- **M5 — Gate + verify: bandwidth claim** (Task 12) · controller/interactive · live A/B: N mip-only
  trees vs N fully-resident trees, measure actual bytes uploaded + frame time; no-regression suite.

**Milestone re-split note (2026-07-05):** the original Milestone Map had a single M4 (Tasks 10-11).
Task 10 grew, across design discussion, from "wire a residency trigger" into four largely-independent
sub-mechanisms (resolvability formula, frustum containment, a two-tier occlusion gate, and
capability-graph-gated scheduling) — by line count, Task 10 alone (`### Task 10` through the bullet
list preceding `### Task 11`) is larger than Tasks 1-9 combined. Handing that to one implementer as a
single milestone risks both context overload for a Sonnet-medium worker and an unfocused validator
pass. Re-split into M4a/M4b/M4c below, each sized to the skill's target (1-3 tasks, one clear gate).
The task text itself (Task 10's sub-bullets) is unchanged — only the milestone boundaries around it are
new; implementers should treat each lettered milestone as covering the correspondingly-labeled part of
Task 10's existing bullet list, not rewrite the task.

### Progress Log

*(empty — not started)*

---

## M1 — Mip sample bake + SoA serialize

### Why this is first
Every other milestone depends on the sample pool existing and being correctly filled; it has no
dependency on the upload-path changes (M2) and can be built/tested purely CPU-side first, same
sequencing as Stored-SDF Inc2's M1 (bake) before M3 (GPU integration).

### Task 1 — `MipSample` type + per-channel filtering semantics
- [ ] Define `MipSample` (or per-channel variants) mirroring the existing multi-channel SoA shape
  (`VoxelChannelFormat.h`'s `SemanticId`/`FieldKind`) — read-by-semantic like the Inc3 channel pool,
  not a new parallel format.
- [ ] Implement filtering per [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] §"Design decisions" point 3:
  - `SEM_COLOR`/`SEM_ROUGHNESS`/etc: weighted mean by child coverage (a `min(childCount,8)`-weighted
    average, coverage = fraction of the 8 child octants that are non-empty).
  - `SEM_SDF` (`FK_DISTANCE`): **conservative min-magnitude**, not mean — take the child sample with
    the smallest `|distance|` (closest to the surface), propagated with the correct sign. Explicitly
    test that a mean-filtered SDF at a level would produce a false-positive/negative surface crossing
    that min-magnitude avoids (this is the Inc3-lesson regression case: write a bake fixture where a
    naive mean *would* misplace the surface and assert the min-magnitude result doesn't).
- [ ] Unit test: `test_mip_sample_filter.cpp` — pure math, no octree needed. Verify color-mean and
  SDF-min-magnitude on hand-constructed child-sample fixtures (uniform fill, half-empty, and the
  adversarial SDF case above).

### Task 2 — Bottom-up bake-time fill
- [ ] In the bake path (wherever `SerializeSdf`/octree construction already walks bottom-up to build
  `ChildDescriptor`s — verify exact hook via `codegraph_explore "SVORebuild bottom-up octree build
  hierarchy"` before writing, do not assume the entry point without checking current code), add a
  post-pass (or fold into the existing pass) that computes each interior node's `MipSample` from its
  children's samples/leaf values, one level at a time, root-ward.
- [ ] Ordinal indexing: per-level SoA pool indexed by the **same level-local node ordinal as the
  existing serialization order** (direction doc §"Design decisions" point 1) — do not invent a new
  addressing scheme; verify against how `SerializeSdf` already assigns node order and reuse it
  directly.
- [ ] Unit test: `test_mip_sample_bake.cpp` — bake a small multi-level test octree (reuse the Inc2/Inc3
  bake fixtures), assert every interior level's samples are present and match Task 1's filter applied
  to the actual child data (not just "non-zero" — exact value assertions).

### Task 3 — `SerializedOctree`/`ConcatenatedOctrees` carries the mip pool
- [ ] Add `mipPool` (bytes) + `mipPoolBase`-per-octree (mirrors `poolBrickBase` for the existing
  channel pool, `ShellOctreeGpu.h:181-187`) to `SerializedOctree` and `ConcatenatedOctrees`.
- [ ] `ConcatenateSdf` appends each octree's `mipPool` and stamps the per-octree base offset, same
  pattern as the existing `channelPool`/`brickGridLookup` append loop (`ShellOctreeGpu.h:754-757`).
- [ ] Extend `OctreeConfig` tail with `mipPoolBase` (verify free byte range before picking an offset —
  the tail is documented as bytes ≥200 in the 432-byte std140 struct; confirm current occupancy via
  `codegraph_explore "OctreeConfig tail bytes used formatId bricksPerAxis poolBrickBase"` before
  claiming a byte range, do not guess).
- [ ] Unit test: `test_soa_mip_serialize.cpp` (mirrors `test_soa_sdf_serialize.cpp`) — round-trip a
  baked octree through `ConcatenateSdf`, assert the mip pool bytes match the bake-time samples at the
  expected ordinal offsets.

**Gate:** `test_mip_sample_filter`, `test_mip_sample_bake`, `test_soa_mip_serialize` all green.

---

## M2 — Brick-pool partial allocation + `BatchedUploader` wiring

### Why this is the real new mechanism
`CreateOctreeBuffers` (`BodyOctreeSceneNode.cpp:419-480`) currently creates every octree buffer
host-visible and writes the full CPU-side byte vector at creation time — there is no notion of
"allocate this buffer's capacity now, populate its contents later." `ResourceManagement::BatchedUploader`
(`BatchedUploader.h/.cpp`) already provides exactly the missing piece — `Upload(srcData, size,
dstBuffer, dstOffset)`, staged through a pooled ring of staging buffers, async, completion-tracked —
but nothing in the octree path calls it today. This milestone wires it in.

### Task 4 — Brick buffer allocated at full capacity, uploaded empty
- [ ] Change `CreateOctreeBuffers`'s brick-buffer creation: allocate `bricksBuffer_` sized for the
  tree's **full** brick count (from `ConcatenatedOctrees::brickCounts`/the per-channel stride math in
  `VoxelChannelFormat.h`), but do NOT populate it from `concatenated_.bricks.data()` unconditionally —
  gate initial population behind a per-octree "residency request" flag that defaults to **false** (no
  bricks requested = mip-only tree).
- [ ] Verify the existing `hasBrick()`/`INVALID_BRICK_INDEX` sentinel (`SVOTypes.h:102-104`) already
  means "this leaf has no brick" — confirm (do not assume) that a brick-pool region left
  **unwritten-but-allocated** is safely distinguishable from **populated** at the shader side; if the
  current sentinel lives in `ChildDescriptor.contourPointer` (CPU-side, always correct regardless of
  GPU buffer contents) this is already sufficient and no new GPU-side "is this brick uploaded" flag is
  needed — verify this claim against current code before building a redundant flag.
- [ ] Unit test: `test_partial_brick_upload.cpp` — create a tree's buffers with residency-request=false,
  assert node buffer + mip pool are populated, brick buffer is allocated (correct size) but the CPU
  mirror / readback shows it untouched (or: assert `hasBrick()` on every leaf still correctly reports
  false until a brick is actually uploaded, if the sentinel is descriptor-side not buffer-side —
  resolve this ambiguity as part of writing the test, it's the crux of the milestone).

### Task 5 — Wire `BatchedUploader` for brick population
- [ ] Replace `CreateOctreeBuffers`'s direct `concatenated_.bricks.data()` write with a
  `BatchedUploader::Upload(brickBytes, size, bricksBuffer_, dstOffset)` call for the bricks that ARE
  being populated at a given time (initially: all-or-nothing per §0 scope, so this is "upload
  everything via BatchedUploader instead of the host-visible direct-write path" as the mechanical
  first step — enables per-brick granularity later without re-architecting).
- [ ] Determine (verify against current code, do not assume) whether `BodyOctreeSceneNode` currently
  has access to a `BatchedUploader` instance, or whether one needs to be constructed/threaded in —
  check `VulkanDevice`/render-graph-wide uploader ownership via `codegraph_explore "BatchedUploader
  construction ownership where is it instantiated"` before assuming a global exists.
- [ ] Unit test/controller gate: `VIXEN.exe` links and boots with the existing default scene rendering
  unchanged (no-regression — this task swaps the upload mechanism, not the data).

### Task 6 — Residency-request API on `BodyOctreeSceneNode`
- [ ] Add `RequestBrickResidency(octreeIndex, bool resident)` (name TBD at implementation time) —
  mirrors the existing `SetBakeRecipe`/`SetRecipePool` dirty-flag pattern (`BodyOctreeSceneNode.h:89-99`):
  stash the request, mark dirty, `ExecuteImpl` performs the actual `BatchedUploader` call next frame
  (do not upload synchronously inside the setter — matches the established pattern in this file).
- [ ] Unit test: call `RequestBrickResidency(idx, true)` on a mip-only tree, tick `ExecuteImpl`, assert
  bricks are now populated (readback or the `hasBrick()` check from Task 4).

**Gate:** `test_partial_brick_upload` green + `VIXEN.exe` links (controller-run) with no visual
regression on the existing default scene.

---

## M3 — Shader fallback read

### Task 7 — Existence check at leaf hit
- [ ] In `BodyInstanceRayMarch.comp`'s leaf hit-test (`handleLeafHitInstancedSdf`/
  `handleLeafHitInstanced`, per the M6 redesign in Stored-SDF Inc2), add the check: if
  `hasBrick()`/the equivalent GPU-side condition indicates no resident brick at this leaf, **do not
  march** — instead read `mip[level][ordinal]` for this node using the level/ordinal already tracked
  by `ESVOTraversalState` (`scale`, and whatever ordinal-tracking already exists in the traversal —
  verify exact field names via `codegraph_explore` before writing, per the direction doc's point 2
  "verify spare-bit vs sentinel cost in the real node/offset layout").
- [ ] Shade using the mip sample directly (v1 = hard switch, no lerp, per direction doc point 4) —
  produces a flat-shaded or coarse-normal representation of that node's extent, not an iso-surface
  march.

### Task 8 — LOD-cutoff fallback (the other trigger)
- [ ] Before attempting to descend further, check `raySizeCoef`'s existing footprint-vs-voxel-size
  test; if the cutoff says stop, read the mip sample at the current level **without checking brick
  residency at all** — this is the "deliberate LOD" trigger from the direction doc's "two triggers,
  one read path," distinct from Task 7's "streaming grace" trigger, but landing on the identical
  `mip[level][ordinal]` read.
- [ ] Verify both triggers share one code path (per the direction doc's explicit "both land on
  `mip[level][ordinal]`") rather than accidentally forking into two shader branches that could drift.

### Task 9 — Lavapipe render gate
- [ ] Offscreen render test (mirrors the Stored-SDF M6 lavapipe gate style): a tree with brick
  residency-request left at `false` (never uploaded, per M2) renders a recognizable silhouette from
  its mip samples alone — assert non-trivial pixel coverage (fillRatio-style assertion, matching the
  Inc2 M6 precedent that a silhouette-only check isn't sufficient by itself — also assert the shape is
  roughly correct, not just "some pixels are lit").
- [ ] No-regression: existing binary/Procedural/Stored bodies with residency-request=true (or bypassing
  the new check entirely) render identically to pre-Inc1.

**Gate:** shader compiles, lavapipe offscreen render shows correct mip-only silhouette, no-regression
suite green.

---

## M4a — Resolvable-level formula

> Part 1 of Task 10 (see "Milestone re-split note" above). Pure CPU math — no GPU/render dependency,
> buildable and testable independently of M4b/M4c.

### Task 10 — Resolvable-level formula (derives the trigger; zoom/FOV is a first-class input, not distance-only)

**The trigger is a closed-form calculation over (distance, FOV), not a tuned magic-number
threshold, and not a distance-only check.** A node at octree level L has linear size
`leafSize_m * 2^L` (`leafSize_m` = 0.01 for the 1cm-voxel configuration; level 0 = leaf/finest,
increasing L = coarser, matching `LaineKarrasOctree.h`'s convention where the root sits at
`ESVO_MAX_SCALE`). One pixel subtends `theta_px = fovRadians / screenHeightPx` radians — this is
exactly what `RaySizeCoefNode` already computes, and **narrowing FOV (zooming, a telescope) shrinks
`theta_px`**, since the same screen height now spans fewer degrees. A node at distance `d` is
resolvable (subtends `>= pxThreshold` pixels) when `nodeSize/d >= pxThreshold * theta_px`. Solving
for the **finest (smallest, lowest-L) level still resolvable** at a given `(d, theta_px)`:

```
minResolvableLevel(d, fovRadians, screenHeightPx) =
    ceil( log2( d * pxThreshold * fovRadians / screenHeightPx / leafSize_m ) )
```

Any level *below* `minResolvableLevel` is sub-pixel and wasted to have brick-resident; any level at
or above it is fine. **This formula takes FOV as an input alongside distance for exactly the
telescope/observation-post case:** zooming in (narrower `fovRadians`) shrinks `theta_px`, which
*decreases* `minResolvableLevel` — correctly meaning finer detail becomes worth resolving, with no
special-casing for "zoomed" vs "normal" view; a telescope is just a smaller `fovRadians` value fed
into the same formula used for the normal camera. **Concretely** (1080p/60°FOV/1px threshold, verify
against the project's actual `RaySizeCoefNode` constants before hardcoding): a fleet at 500m needs
level ≥5.6 resolvable under normal 60° FOV (bricks, at level ~3, are well below that — not
resolvable, correctly stay mip-only); the same fleet at the same 500m under a 2° telescope FOV needs
only level ≥0.7 (bricks now clearly resolvable — the trigger correctly requests brick residency
purely from the FOV change, camera position untouched). This is the direct answer to "does zooming
automatically update the upload requirements": **yes, by construction, because FOV is a term in the
same formula distance is** — there is no separate telescope-mode code path.
- [ ] Implement `minResolvableLevel(distance, fovRadians, screenHeightPx, leafSize_m, pxThreshold)` as
  a small pure function (CPU-side, called once per tree per residency re-check — NOT per-ray; the
  per-ray version of this same math is what `RaySizeCoefNode`/the shader traversal already does at
  finer grain via the LOD cutoff from M3/Task 8). Unit test against the worked values above (or the
  project's actual constants once verified) — this is pure math, no GPU needed. Include a fixed-distance,
  varying-FOV test case (the telescope scenario) alongside the fixed-FOV, varying-distance case, since
  the two axes are independent inputs to the same formula and both need coverage, not just distance.

**M4a gate:** `minResolvableLevel` unit test green (both distance-driven and FOV-driven cases).

---

## M4b — Frustum + occlusion gates

> Part 2 of Task 10. Depends on M4a's `minResolvableLevel` existing (the combined trigger references
> it), but the frustum/occlusion mechanisms themselves are independent of the formula's internals —
> buildable once M4a's function signature is fixed, without waiting on M4a's own gate to fully land if
> parallelizing, though this Plan runs milestones sequentially per the context-manager pipeline.

- [ ] **Frustum containment is a second, independent gate — not folded into the distance/FOV formula.**
  `minResolvableLevel` answers "how much detail is worth resolving," but a tree can be close and
  well within resolvable range while sitting entirely outside the view frustum (behind the camera, or
  off to the side beyond the FOV cone) — that's a direction/containment test, not a distance test, and
  distance alone cannot detect it. Residency requires **both**: (a) the tree's bounding volume
  intersects the camera frustum (reuse whatever frustum test the render graph already has for
  culling — verify via `codegraph_explore "frustum cull bounding volume camera"` before writing a new
  one), and (b) `minResolvableLevel(d, fov, h) <= brickLevel`. A tree failing (a) needs zero bricks
  regardless of (b) — full trim, same as being out of resolvable range, and for Inc1 this is the
  correct behavior for ANY out-of-frustum tree, not just distant ones (a body 5m away but directly
  behind the camera should be exactly as brick-empty as one 10km away).
- [ ] **Hysteresis margin on the frustum test**, not a hard edge: expand the culling frustum slightly
  (a small angular/distance pad) for the *residency* check specifically (distinct from the render
  frustum used for actual draw culling) — otherwise panning the camera back and forth near the
  boundary re-triggers upload/evict every frame. Pick a pad wide enough that a normal look-around does
  not thrash; this is a tunable, not a formula, and should be a named constant, not inlined.
- [ ] **Occlusion — a fourth, independent gate. Two distinct mechanisms at two different
  granularities; do not conflate them (correction 2026-07-05, after finding the existing per-ray AABB
  cull).** Frustum containment answers "is it within the view cone"; resolvability answers "is it big
  enough to matter"; neither answers "is something else already in front of it."
  1. **Already exists, per-ray, GPU-side:** `BodyInstanceRayMarch.comp:741-742` —
     `rayAABBIntersection(localRayOrigin, localRayDir, vec3(0.0), vec3(1.0))`, `if (gridT.y < 0.0)
     continue;` — inline in the per-pixel instance loop, skips full ESVO traversal for any instance
     whose bounding box a ray doesn't even pass through. This is a **ray-miss test, not an occlusion
     test**: it says nothing about whether a CLOSER instance already blocks this ray's view of a
     FARTHER one that the ray does pass through. It costs nothing extra to reuse/extend (see below) but
     does not by itself solve the residency problem.
  2. **Missing, and this is the actual gap for the CPU-side residency decision (confirmed by search,
     `codegraph_explore "occlusion query visibility test AABB ray frustum culling"`, 2026-07-05: no
     occlusion-query/HZB/beam infrastructure exists anywhere):** a body directly behind a planet passes
     BOTH the existing per-ray AABB test (a ray can still intersect its bounding box before or after
     hitting the planet) AND frustum containment AND resolvability, yet needs zero bricks — the
     planet's already-resident geometry blocks every pixel it could show. Nothing today prevents
     requesting residency for it.
- [ ] **Cheapest correct fix reuses the existing per-ray loop instead of adding a separate CPU-side
  beam pre-pass.** The instance loop (`BodyInstanceRayMarch.comp:667-782`) already tracks `bestT`
  (nearest hit so far across all instances processed this ray). Sort instances **front-to-back**
  (by distance from camera, computed CPU-side when building the per-frame instance list — cheap, one
  sort per frame, not per-pixel) and extend the existing AABB reject at line 741 to also check
  `gridT.x > bestT` (this instance's nearest possible entry point is already farther than something
  already hit this ray) — reject in that case too, alongside the existing `gridT.y < 0.0` miss check.
  This gives real occlusion-aware early-out **inside the mechanism that already exists**, at zero added
  infrastructure, for the per-ray traversal-skip case. **This does not by itself gate CPU-side brick
  residency** (§ still needed below) — it only saves GPU traversal work for instances that already have
  bricks; it doesn't stop non-visible instances from having bricks uploaded in the first place.
- [ ] **CPU-side residency occlusion gate — separate from the GPU per-ray fix above, genuinely new,
  scope-decide at implementation time.** For the "should we upload bricks for tree X at all" decision,
  the cheap version: for each frustum-passing, resolvable candidate, test its bounding-volume center
  (and a few sample points) against a **coarse depth estimate** built from already brick-resident
  trees only (e.g. one representative ray per resident tree toward the candidate's direction; if it
  reports a hit closer than the candidate's own distance, treat the candidate as occluded). Deliberately
  approximate — a full per-pixel occlusion query is out of scope; this reuses the front-to-back
  ordering from the GPU fix above as its input, not a new render pass. Decide at implementation time
  whether this ships in Inc1: if M5's bandwidth measurement (Task 12) shows frustum+resolvability alone
  deliver the claimed win on the initial test scenes (no heavily-occluding geometry), defer this to
  Inc2; if the scenes include occluders (moon behind planet, ships behind a station) and bandwidth
  doesn't improve as expected, build this before declaring Inc1 done.
- [ ] Unit test for the GPU per-ray fix (front-to-back + `gridT.x > bestT` reject): a synthetic
  three-body line-up (camera → occluder → occluded target) asserts the occluded target's traversal is
  skipped (not just its hit discarded) once the occluder's hit is recorded — verify via iteration-count
  instrumentation (`DebugRaySample`/`dbg.iterationCount`, already present in the shader) showing the
  occluded instance did zero traversal iterations, not just "no visible pixel difference."
- [ ] Unit test for the CPU-side residency gate (if built this increment): same three-body line-up,
  occluder already brick-resident, asserts the occluded target's residency is NOT requested despite
  passing frustum + resolvability; moving the occluder aside (or letting the target emerge past it)
  triggers residency request once unoccluded.
- [ ] **CPU is the right home for the residency gate at Inc1's scale — do not move it to compute/mesh
  shaders now (checked 2026-07-05, not a guess).** The gate (frustum + `minResolvableLevel` + coarse
  occlusion samples) runs once per candidate tree per re-check, not per-pixel; at the tree counts this
  increment and undertow's near-term target actually involve (tens to a few hundred bodies, per
  `BodyOctreeSceneNode.cpp:666`'s existing `3*64` instance cap and the 60-300-body path noted in
  [[undertow-vixen-integration-map]]), the whole per-frame CPU cost is sub-millisecond, single-threaded
  — a GPU compute dispatch would add more round-trip/readback latency than the work itself takes to
  run on CPU. **Revisit only if the nested-tree epic** ([[Tiered-ESVO-Observer-Addressing-Design-2026-07]],
  explicitly out of scope for Inc1 per §0) **pushes resident-tree counts up by orders of magnitude** —
  that is the one condition under which this calculus would flip, and it is a future increment's
  concern, not this one's.

**M4b gate:** GPU per-ray fix's iteration-count unit test green; CPU-side residency occlusion gate's
unit test green if built this increment (scope-decide per its own note above — may legitimately defer
to Inc2, in which case this gate is "N/A, deferred" rather than a failure).

---

## M4c — Trigger wiring + live gate

> Part 3 of Task 10 + all of Task 11. The integration milestone — ties M4a's formula and M4b's gates
> together into the actual `RequestBrickResidency` call, notes the capability-graph gating path for any
> future GPU-scheduling variant, and runs the live render gate. Depends on M4a + M4b both landing.

- [ ] Wire `RequestBrickResidency(idx, frustumContains(idx) && minResolvableLevel(d, fov, h) <= brickLevel && !occluded(idx))`
  as the actual trigger (brick tier at `~kShellDepth` per `BodyOctreeSceneNode.h:118`; `occluded(idx)`
  is the Inc1-optional gate above — defaults to `false`/always-pass if not built this increment, so the
  formula degrades gracefully to the frustum+resolvability-only trigger), re-evaluated whenever camera
  distance, FOV, **or orientation** changes materially — orientation matters because it's what moves a
  tree in/out of the frustum independent of distance or zoom, and also changes what occludes what.
- [ ] Unit/controller test: **three** scenarios, not two — (a) camera moves toward a stationary body at
  fixed FOV/orientation (distance-driven), (b) camera stays fixed while FOV narrows
  (telescope/zoom-driven), and (c) camera stays fixed distance/FOV but **rotates** so a body moves from
  in-frustum to out-of-frustum and back (orientation-driven, the case distance/FOV alone can't cover) —
  assert bricks populate/trim correctly in all three, with no visible pop/hole during in-frustum
  transitions and no thrash (repeated request/evict within a few frames) when panning near the
  hysteresis margin in scenario (c). Eviction on any axis (zooming out, moving away, or panning out of
  frustum) uses the same combined gate symmetrically — actual GPU memory reclaim on eviction is a
  separate concern from the trigger itself; verify whether "stop requesting" alone is sufficient for
  Inc1 or whether freeing already-uploaded brick memory is needed for the bandwidth claim in M5 —
  decide based on what M5's measurement actually needs.

### Task 11 — Live gate
- [ ] `VIXEN.exe` run: place N bodies at far distance (mip-only), confirm they render (coarse but
  correct) with zero brick uploads; move camera close to one, confirm it resolves to full detail; move
  away, confirm behavior is stable (no crash/leak on repeated request/de-request cycles).

**Gate:** live run clean, 0 syncval, no crash across repeated distance transitions.

---

## M5 — Gate + verify: bandwidth claim

### Task 12 — A/B measurement
- [ ] Live A/B per the direction doc's own framing ("Memory/bandwidth budget math... samples/tree,
  pinned-set size at N trees, expected far-view working set vs today"): measure actual bytes
  uploaded + frame time for N far-away bodies under (a) pre-Inc1 behavior (full brick upload per body)
  vs (b) Inc1 mip-only-until-close. Record real numbers, not the direction doc's estimates — this is
  the whole point of the increment and needs measured, not projected, evidence.
- [ ] No-regression suite: full existing test suite (binary/Procedural/Stored/multi-channel/recipe-pool)
  green.
- [ ] Update [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] status banner to reflect Inc1 shipped, with the
  measured bandwidth numbers in place of the estimates.

**Gate:** live-run bandwidth improvement demonstrated with real numbers; full no-regression suite
green.

---

## Notes for implementers

- **Do not start nested-tree work.** If M4/M5 tempts extending toward `TierRef`/cross-tree traversal
  (because "the mip fallback would also cover a non-resident child tree"), that is correct per the
  direction doc but is explicitly [[Tiered-ESVO-Observer-Addressing-Design-2026-07]]'s scope, not
  this plan's. Land Inc1 first; it is the dependency, not a subset to merge in ad hoc.
- **Every "verify against current code" bullet above is deliberate**, not filler — this plan was
  written by reading the current struct layouts and upload path directly (`ChildDescriptor`,
  `ConcatenatedOctrees`, `OctreeConfig` tail, `BatchedUploader`, `BodyOctreeSceneNode`), but several
  exact hook points (bottom-up bake pass location, `OctreeConfig` free-byte range, `BatchedUploader`
  ownership) were not fully traced in the design pass that produced this plan and must be confirmed
  by whoever implements each task, not assumed from this doc's prose.
