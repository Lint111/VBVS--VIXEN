---
title: Deep-Field Residency Unification — design spec + bandwidth-instrumentation audit
status: draft
created: 2026-08-09
tags: [architecture, mip, lod, residency, bandwidth, cache, far-field, esvo]
---

# Deep-Field Residency Unification

**Scope.** Answers B49-T2's four questions with file:line evidence, against engine
`e7dbe524`. Read-only analysis — no code changes. Builds on the shipped Sparse-Mip
Inc1/Inc2 residency trigger (`InstanceWantsBrickResidency`, 68.8% measured mixed-scene
upload reduction) and the certified deep-field mip-accessor policy (regimes 1/2/3,
[[Deep-Field-Mip-Accessor-Policy-2026-08]]).

**⚠️ Naming collision, resolved up front:** this doc's "regime 1/2/3" (mip-policy
footprint classification) and `regime_of.py`'s "LOW/HIGH" (per-boot GPU-latency
classification used by the cost-certification sweep) are **unrelated vocabularies that
happen to share the word "regime."** `regime_of.py` classifies measurement conditions
(contention/latency), not scene content. Do not conflate them — this doc uses "regime
1/2/3" exclusively for the footprint→level classification.

---

## A. Instrumentation gap

**Claim:** the benchmark scene uploads ~37.45MB wholesale and `boot_bytes_uploaded`/
`steady_state_bytes_uploaded` read zero because the benchmark path bypasses the
instrumented uploader entirely.

**The instrumented path** (what the CSV columns actually measure):
- `PerfCsvWriter::RecordFrame` (`application/main/include/PerfCsvWriter.h:44-46`) takes
  `bootBytesUploaded`/`steadyStateBytesUploaded` as plain `uint64_t` params — no
  computation, just a passthrough into the `Row` struct (`PerfCsvWriter.h:58-59`).
- Its one call site, `VulkanGraphApplication.cpp:1587`, sources those params from
  `bodyScene->BootBytesUploaded()` / `bodyScene->SteadyStateBytesUploaded()`
  (`VulkanGraphApplication.cpp:1581-1585`).
- Those two accessors (`BodyOctreeSceneNode.h:203-204`) return `bootBytesUploaded_` /
  `steadyStateBytesUploaded_`, two fields (`BodyOctreeSceneNode.h:335-336`) that are
  **only ever written in one place**: `BodyOctreeSceneNode::UploadBrickPool`
  (`BodyOctreeSceneNode.cpp:1344-1348`) — first call latches `bootBytesUploaded_`,
  every later call accumulates into `steadyStateBytesUploaded_`.
- `UploadBrickPool` itself is gated on `residencyRequested_` (`BodyOctreeSceneNode.cpp:1310`)
  and routes through `device->Upload(...)` (`BodyOctreeSceneNode.cpp:1322`) →
  `BatchedUploader` (`ResourceManagement/include/Memory/BatchedUploader.h`), which is the
  only upload path this codebase actually instruments (`totalBytesUploaded_.fetch_add`,
  `BatchedUploader.cpp:267`).

**The benchmark scene's actual path** (what moves the ~37.45MB):
- `BodyOctreeSceneNode::CreateOctreeBuffers` (`BodyOctreeSceneNode.cpp:898`), called from
  `CompileImpl` (`BodyOctreeSceneNode.cpp:420`) and `Rematerialize`
  (`BodyOctreeSceneNode.cpp:1155`) — i.e. every Compile/boot, unconditionally.
- Every buffer it populates — `nodesBuffer_` (`:909-912`), conditionally `bricksBuffer_`
  (`:931-935`, only when `residencyRequested_` true at Compile time), `materialsBuffer_`
  (`:937-940`), `configBuffer_` (`:996-999`), `sdfBuffer_`/channelPool (`:1009-1012`),
  `brickLookupBuffer_` (`:1014-1017`), `mipPoolBuffer_` (`:1025-1028`),
  `tierRefTableBuffer_` (`:1037-1040`), `occupancyGridBuffer_` (`:1050-1053`) — goes
  through the free function `CreateHostBuffer` (`BodyOctreeSceneNode.cpp:70-127`), which
  is a raw `vkCreateBuffer` + `vkMapMemory`/`memcpy`/`vkUnmapMemory`
  (`BodyOctreeSceneNode.cpp:119-125`). **It never calls `BatchedUploader` or touches
  `bootBytesUploaded_`/`steadyStateBytesUploaded_` at all** — there is no
  instrumentation hook in `CreateHostBuffer` or its call sites.
- The header comment at `BodyOctreeSceneNode.cpp:366-368` (repeated verbatim in
  `ResidencyDefault.h:13-15`) says this explicitly: residency laziness governs **only**
  the binary `concatenated_.bricks` blob; "the channelPool, nodes, mips, lookup tables,
  and both shell-cache slots still upload whole at Compile — their laziness is a future
  increment's paged pool, not this milestone."
- The 37,453,824 B figure (37.45MB) is independently corroborated as the dense-body
  channel-pool size in `Deep-Field-Mip-Accessor-Policy-2026-08.md:277` ("sparse-off
  37,453,824 B"), matching the brief's "~37.45MB wholesale" claim — that pool is the
  `sdfBuffer_`/channelPool upload at `BodyOctreeSceneNode.cpp:1009-1012`, one of the
  uninstrumented `CreateHostBuffer` calls above.
- **This exact gap is independently documented by the test that measured the 68.8%
  figure itself:** `test_bandwidth_ab_measurement.cpp:9-17` (the Inc1 M5 gate) states in
  its own file banner that the initial-residency `CreateOctreeBuffers` path "writes
  `bricksBuffer_` via a direct host-visible memcpy... NOT via `BatchedUploader::Upload`
  — so `BatchedUploaderStats` cannot observe that path at all," and that its measured
  68.8%-class figures are only ever exercised via the ON-DEMAND streaming path
  (`RequestBrickResidency(true)` called after the tree already exists mip-only), never
  the boot-time wholesale path. This test independently confirms the divergence point
  identified below, from the opposite direction (a test author debugging why their
  instrumentation read zero).

**Exact divergence point:** `BodyOctreeSceneNode.cpp:1009` (`CreateHostBuffer(device,
sdfSize, ..., sdfBuffer_, ...)`) — and identically its seven siblings at `:909, :931,
:937, :996, :1014, :1025, :1037, :1050` — versus `BodyOctreeSceneNode.cpp:1324`
(`device->Upload(...)` inside `UploadBrickPool`), the only call in this file that feeds
the instrumented counters. Same file, same class, two entirely separate upload
mechanisms; only one is counted.

**Minimal wiring to make the columns real** (three options, ranked by invasiveness):

1. **Cheapest — count bytes at the `CreateHostBuffer` call sites without routing them
   through `BatchedUploader`.** Add a `uint64_t` out-param or a member accumulator
   bumped by each of the 8 `CreateOctreeBuffers` calls (`:909-1053`), folded into
   `bootBytesUploaded_` the same way `UploadBrickPool` already does (first Compile
   latches boot, any later Compile/Rematerialize accumulates steady-state). This makes
   the CSV columns literally true (every byte actually DMA'd/mapped to the GPU is
   counted) without changing the upload mechanism itself. Smallest diff; does not touch
   `CreateHostBuffer`'s signature.
2. **Route the wholesale buffers through `BatchedUploader` too.** Would make
   `BatchedUploaderStats.totalBytesUploaded` (`BatchedUploader.cpp:308`) the single
   source of truth for all GPU uploads, at the cost of touching 8 call sites' buffer
   creation to go through the async upload+poll machinery `UploadBrickPool` already
   uses (host-visible buffers today are populated synchronously at Compile;
   `BatchedUploader` uploads are async and require the same poll-completion pattern as
   `PollBrickUploadCompletion`, `BodyOctreeSceneNode.cpp:1350+`). Larger diff, and not
   obviously beneficial today since these buffers are populated once at Compile, not
   re-toggled per frame like brick residency.
3. **Do nothing structural, just log-scrape.** The perf log already prints every
   buffer's size (`BodyOctreeSceneNode.cpp:1055-1063`, the `"Created octree buffers
   (...)"` line) and the sweep driver already regex-extracts `pool=NNNNB` from the log
   (`~/scripts/sweep-cert-resumable.sh:108`, `poolB=$(grep -oE 'pool=[0-9]+B' ...)`).
   This is how bandwidth ground-truth is obtained TODAY, sidestepping the broken CSV
   column. Not a fix to the instrumentation gap — a documented workaround already in
   production use — but relevant context for D below.

**Recommendation:** option 1. It is a same-file, same-struct addition (no new class, no
async plumbing) and directly repairs the two dead CSV columns the brief names, without
conflating "uploaded via BatchedUploader" (a mechanism) with "uploaded to the GPU" (the
thing the column name promises).

---

## B. Unification — one footprint→regime function, two consumers

**The premise doesn't hold as stated: today there is no single "regime classification"
to unify residency against, because the render-side policy and the residency trigger use
two structurally different footprint formulas, not two call sites of the same one.**

**Render-side (shader, world-space footprint):**
`SceneBindings.glsl:2602-2613` (entry dispatch, `VIXEN_MIP_POLICY`):
```
entryMidT         = 0.5 * (tEnter + gridT.y)                      // midpoint anchor, batch-36 fix
entryWorldDist     = entryMidT * instRenderScale
entryCellWorldSize = ((1.0 / bpaF) / entryDirLen) * instRenderScale
entryAdmitFootprint = entryCellWorldSize / kEntryBrickSize          // kEntryBrickSize = 8.0
entryGateLhs       = entryWorldDist * pc.raySizeCoef + pc.raySizeBias
entryPolicyAdmits  = pc.raySizeCoef > 0 && entryGateLhs >= entryAdmitFootprint     // regime 2 threshold
regime3Admits      = entryPolicyAdmits && entryGateLhs >= pc.cosmicK * entryCellWorldSize  // regime 3 threshold
```
Per-ray, per-instance, per-frame, evaluated on GPU, in GLSL. Two thresholds
(`entryAdmitFootprint`, `pc.cosmicK * entryCellWorldSize`) select among regimes 1
(neither admits)/2 (`entryPolicyAdmits` only)/3 (`regime3Admits`).

**Residency-side (CPU, screen-space resolvability + capability):**
Two entirely separate functions, neither sharing a term with the shader formula above:
- `InstanceWantsBrickResidency` (`ResidencyTrigger.h:27-54`): frustum test
  (`SphereIntersectsFrustum`) AND `minResolvableLevel(distance, fovRadians,
  screenHeightPx, leafSize_m, pxThreshold) <= brickTierLevel` — a **screen-space pixel
  coverage** criterion (px threshold), not a world-space footprint. One production
  caller: `VulkanGraphApplication.cpp:4100`, the per-frame camera-driven residency
  re-check.
- `DeriveResidencyDefault` (`ResidencyDefault.h:53-62`): a **content-capability** gate
  (is every octree mip-baked?), not a distance/footprint gate at all — governs only the
  boot-time default before any camera exists. One production caller:
  `BodyOctreeSceneNode::DeriveResidencyDefaultIfUnset` (`BodyOctreeSceneNode.cpp:419`).

**Inventory of every current call site:**

| function | file:line | consumer | axis |
|---|---|---|---|
| entry-dispatch footprint (regime 1/2/3) | `SceneBindings.glsl:2602-2626` | GPU shader traversal | world-space footprint (distance × raySizeCoef + bias vs cell size) |
| `InstanceWantsBrickResidency` | `ResidencyTrigger.h:27`, called `VulkanGraphApplication.cpp:4100` | CPU per-frame residency re-check | screen-space resolvable level (px threshold) + frustum |
| `DeriveResidencyDefault` | `ResidencyDefault.h:53`, called `BodyOctreeSceneNode.cpp:419` | CPU boot-time default (once, pre-camera) | content capability (mip-baked or not) |

**Why unification is not a drop-in today:** the shader's footprint is a *world-space
linear distance metric* (`raySizeCoef·dist + raySizeBias` vs `cell_size/8`); the
residency trigger's is a *screen-space angular/pixel metric* (`minResolvableLevel`,
which folds in FOV and screen height — see `ResolvableLevel.h`, not reproduced here).
These are dimensionally different tests that happen to correlate (both roughly track
"is this thing far/small on screen") but are not the same function evaluated twice.
`DeriveResidencyDefault` is a third, orthogonal axis (capability, not distance) that
cannot be subsumed into either.

**Proposed design — one function, honest about its two real inputs:**

```
Regime FootprintRegime(worldDist, cellWorldSize, raySizeCoef, raySizeBias, cosmicK)
    footprint = worldDist * raySizeCoef + raySizeBias
    if raySizeCoef <= 0 || footprint < cellWorldSize/8:  return Regime::Surface   // regime 1
    if footprint < cosmicK * cellWorldSize:              return Regime::MipHit    // regime 2
    return Regime::Cosmic                                                        // regime 3
```

This is exactly the shader's existing arithmetic (`SceneBindings.glsl:2609-2626`),
extracted to a pure function — the same "dependency-free function" pattern already used
by `ResidencyTrigger.h`/`ResolvableLevel.h`/`ResidencyDefault.h` (no node/GPU types,
unit-testable standalone). It would live in a new
`libraries/SVO/include/FootprintRegime.h`, mirroring those three files' placement, and
GLSL would need a hand-synced twin (as `SceneBindings.glsl` already is for other CPU-side
formulas — this codebase has no C#→GLSL or C++→GLSL transpiler for this class of pure
function; the sync discipline is the same manual md5-verified parity this epoch already
practices, per `Deep-Field-Mip-Accessor-Policy-2026-08.md:200`).

**The residency decision becomes a consumer of `FootprintRegime`, not a parallel
formula:** replace `InstanceWantsBrickResidency`'s `minResolvableLevel <= brickTierLevel`
screen-space test with a call to `FootprintRegime(distance, cellWorldSize, ...) ==
Regime::Surface` (regime 1 ⇒ bricks resident) evaluated CPU-side with the same
`raySizeCoef`/`raySizeBias`/`cosmicK` push-constants the shader already receives per
frame (`pc.raySizeCoef`, `pc.raySizeBias`, `pc.cosmicK` — already uniform, already
frame-current). Regimes 2/3 ⇒ `RequestBrickResidency(false)` (mip-only, bricks
evictable/never-uploaded, matching the brief's stated goal exactly).

**What does NOT unify:** `DeriveResidencyDefault`'s capability gate stays separate — it
answers "can this tree be lazy at all" (a one-time content-shape question), not "should
it be resident right now" (a per-frame distance question). Folding it into
`FootprintRegime` would conflate a boot-time content precondition with a live-camera
decision; keep them composed (`DeriveResidencyDefaultIfUnset` still runs first and only
gates whether the per-frame regime check is allowed to go lazy at all), not merged.

**Where it lives / who consumes it:**
- **Definition:** `libraries/SVO/include/FootprintRegime.h` (new, header-only, pure
  function — CPU C++ side).
- **GLSL twin:** the existing block in `SceneBindings.glsl:2602-2626`, refactored to
  read as the same three-line comparison, hand-synced (no new mechanism — matches how
  `ResidencyTrigger.h`'s `minResolvableLevel`/frustum logic already has no GLSL
  counterpart today, i.e. this introduces the FIRST CPU/GPU-synced pure function in this
  family, which is new coordination cost worth naming as a risk, not hiding).
- **Consumer 1 (existing):** the shader entry dispatch — unchanged behavior, just
  extracted to name the shared function.
- **Consumer 2 (new):** `InstanceWantsBrickResidency` (or its caller,
  `VulkanGraphApplication.cpp:4100`), replacing the screen-space `minResolvableLevel`
  test with the world-space `FootprintRegime` test.
- **One source of truth for the threshold constants** (`raySizeCoef`, `raySizeBias`,
  `cosmicK`, the `/8` brick-size divisor): today `kEntryBrickSize = 8.0` is a shader
  local (`SceneBindings.glsl:2608`) and `kResidencyLeafSizeM`/`kResidencyPxThreshold`
  are separate CPU constants (`VulkanGraphApplication.cpp`, referenced at the
  `InstanceWantsBrickResidency` call site `:4100-4101`) — unification requires these
  become ONE set of named constants (push-constant-sourced or a shared header), not two
  independently-tuned threshold pairs that could silently drift apart.

---

## C. Cache policy — GPU-LRU vs pinning, revisited

**Current state: there is no GPU-LRU in production.** `SVOStreaming.h` declares a full
streaming-manager interface (`ISVOStreamingManager`, `BrickResidency` enum with
`Loading`/`Evicting` states, an `LRU eviction` config block —
`evictThreshold`/`distanceEvictFactor`/`maxFramesBeforeEvict`/`maxEvictsPerFrame`,
`SVOStreaming.h:109-118`) modeled explicitly on Nanite/SVT/MegaTexture patterns
(`SVOStreaming.h:22-25`) — but grepping the engine tree found **zero concrete
implementations or production callers** of this interface. It is a designed-but-unbuilt
seam, not dead code from a removed feature.

**What actually ships today is binary pinning:** `BodyOctreeSceneNode` tracks exactly one
bit of residency state per tree (`residencyRequested_`/`brickPoolUploaded_`, no
per-brick granularity — the file's own comment at `BodyOctreeSceneNode.cpp:1308-1309`
scopes this explicitly: "§0 scope is per-tree binary 'not requested'/'fully uploaded'
with no GPU-memory-reclaim requirement this increment"). Once resident, bricks are never
individually evicted; the whole tree's brick pool is populated or it isn't.

**The new regime counters as usage feedback:** `[PolicyEntryDispatch]`
(`mip`/`march`/`emptyEntry`, `RayTraceBuffer.h:169-175`, read back via
`ReadPolicyEntryDispatchStats`, printed at `VoxelGridNode.cpp:614`) and `[Regime3]`
(`entry`/`earlyOut`, `RayTraceBuffer.h:178-183`) are **per-frame GPU counters, reset
each frame** (no persistent accumulation across frames is described anywhere in the
struct comments) — they report "how many rays this frame resolved via mip vs march vs
cosmic," not "which bricks were actually touched" or "how long has brick X been
resident." They are a regime-mix signal, not a per-resource usage/recency signal an LRU
needs.

**Recommendation: keep binary pinning, do not build the GPU-LRU, for now.** Rationale:
1. The counters show the regime split is dominated by mip/cosmic paths in the
   deep-field case by construction — "MIP HIT (regime 2)/COSMIC (regime 3)" scenes are
   exactly the ones where bricks should never be resident at all (per this brief's own
   framing: "regimes 2/3 ⇒ mip-only, bricks evictable/never-uploaded"). An LRU only pays
   for itself once there is a genuine WORKING SET of bricks larger than can stay
   permanently resident while still being revisited — i.e. regime-1 (SURFACE) content
   that moves in and out of frustum/resolvability repeatedly. Section B's binary
   pin/evict decision (resident iff regime 1) already handles the common case an LRU
   would otherwise be built for.
2. `SVOStreaming.h`'s interface is unimplemented and unintegrated; standing it up is a
   multi-week subsystem (an actual per-brick allocator + eviction scheduler +
   load/unload state machine), not a policy tweak — disproportionate to the bandwidth
   win B49-T2 is chasing (the measured 68.8% reduction already comes from the binary
   pin/evict decision at tree granularity, not per-brick LRU).
3. Binary pinning's failure mode (thrash at the frustum/resolvability boundary) is
   already named and mitigated: `FrustumCull.h:30` documents hysteresis specifically to
   stop "camera back and forth near the frustum boundary" from thrashing upload/evict —
   the same protection an LRU's `maxFramesBeforeEvict` would provide, already present
   without the LRU's complexity.

**What measurement would falsify this recommendation:** if, once section A's
instrumentation lands, `steadyStateBytesUploaded_` (the accumulator across
residency-toggle events) grows unboundedly over a long play session in a scene with many
regime-1 bodies moving in and out of view — i.e. the SAME tree's bricks get re-uploaded
repeatedly because binary pin/evict has no memory of "this was resident 2 seconds ago,
don't re-fetch it, just re-flag it" — that is direct evidence the working set exceeds
what pinning can hold efficiently and per-brick LRU (keeping recently-evicted brick data
in a CPU-side pool instead of re-uploading from source) would pay for itself. Watch
`steadyStateBytesUploaded_` growth rate vs session length as the falsifying signal, not
a synthetic benchmark — this is exactly the kind of curve the newly-wired bandwidth
column (section A) makes visible for the first time.

---

## D. Measurement plan — the three-axis table's bandwidth column

**Once section A is wired**, `boot_bytes_uploaded`/`steady_state_bytes_uploaded` in the
perf CSV become real per-frame cumulative counters instead of always-zero. The
three-axis table (performance / bandwidth / efficiency=perf-per-byte) gets its bandwidth
axis directly from these columns — no new CSV schema needed, only real values in
existing columns.

**Baseline vs projected:**
- **Baseline (today, wholesale upload):** ~37.45MB per boot (37,453,824 B, the dense
  channel-pool figure corroborated in
  `Deep-Field-Mip-Accessor-Policy-2026-08.md:277` and matching the brief). Once section
  A's option-1 wiring lands, this becomes the measured `bootBytesUploaded_` for a
  regime-2/3-heavy scene with residency NOT unified (today's behavior: mips+lookup+config
  always upload wholesale regardless of regime).
- **Projected (after section B's unification ships):** the brief's own anchor is "≥68.8%
  reduction analog" — the measured Sparse-Mip Inc1/Inc2 mixed-scene figure already
  achieved for the brick-blob-only case. Applying the SAME regime-1-only-residency rule
  to the wholesale buffers this doc's section A newly makes visible (channelPool,
  mipPool, brickLookup, tierRefTable, occupancyGrid — currently NEVER gated on
  residency at all, per `BodyOctreeSceneNode.cpp:366-368`) is a distinct, larger
  opportunity than the brief's anchor number describes: today's 68.8% figure is
  brick-blob-only; the channel/mip/lookup buffers are a SEPARATE wholesale cost this
  spec's B doesn't yet claim a number for, because they are not bricks — the mip data
  must stay resident for regime 2/3 to sample it at all (that is the entire point of the
  deep-field mip policy). **The only bandwidth win available on the wholesale buffers is
  NOT uploading data for octrees/instances that are never in frustum or never
  resolvable at any level** (today `CreateOctreeBuffers` runs unconditionally for every
  octree in `concatenated_`, even fully-occluded or off-screen ones) — a frustum/
  visibility gate on `CreateOctreeBuffers` itself, which is a DIFFERENT lever than the
  regime-1-vs-2/3 brick gate this doc's B unifies. Flagged as an open question below,
  not claimed as delivered by this design.

**Sweep-driver columns (already present, currently fed garbage):**
`~/scripts/sweep-cert-resumable.sh:108-114` already computes:
- `poolB` — regex-scraped from the boot log's `"pool=NNNNB"` line
  (`BodyOctreeSceneNode.cpp:1055-1063`'s `NODE_LOG_INFO`), i.e. it ALREADY gets a
  correct bandwidth ground-truth today, via log-scraping (section A's workaround-3),
  independent of the broken CSV column.
- `ssBpf` (steady-state bytes-per-frame) — computed via `python3` from
  `steady_state_bytes_uploaded` in the per-boot CSV (`sweep-cert-resumable.sh:109-113`),
  which is **currently always `NA` or 0** because the column is dead (section A's gap).
  Once A lands, this becomes a real per-frame steady-state bandwidth signal with zero
  changes to the sweep script — it already parses the right field name.
- **`bootUploadB`** the brief names is NOT currently a sweep-driver column — the closest
  existing one is `poolB` (log-scraped, boot-time only, not per-frame). Adding a true
  `bootUploadB` TSV column (from the now-real `boot_bytes_uploaded` CSV field, first
  non-zero row) is a one-line addition to the driver's per-boot `printf` (currently 9
  tab-separated fields, `sweep-cert-resumable.sh:114`) — mechanical, once A ships.

**Efficiency (perf-per-byte):** with `metric` (the existing conserved
`esvo_traverse_shade_ms + shadow_visibility_wave_ms` sum, `regime_of.py:8`) and a real
`ssBpf`, efficiency is `metric / ssBpf` (ms per byte/frame) or its inverse
(bytes-per-ms) — a derived column, no new instrumentation, computable in the same
`python3` block that already produces `ssBpf`.

---

## Implementation slice list (smallest-first, independently bootable/verifiable)

1. **A-wire:** add a `uint64_t` accumulator bumped at each of `CreateOctreeBuffers`'s 8
   `CreateHostBuffer` call sites (`BodyOctreeSceneNode.cpp:909-1053`), folded into
   `bootBytesUploaded_`/`steadyStateBytesUploaded_` the same way `UploadBrickPool`
   already does. Verify: boot with `VIXEN_PERF_CSV` set, confirm
   `boot_bytes_uploaded` in the CSV is non-zero and ≈37.45MB on the benchmark scene (no
   behavior change, log line at `:1055` unaffected).
2. **D-wire (mechanical, depends on 1):** add the `bootUploadB` TSV column to
   `sweep-cert-resumable.sh`'s per-boot `printf` (`:114`), sourced from the now-real CSV
   field's first row. Verify: one sweep boot, confirm the TSV has a non-`NA`
   `bootUploadB` matching the log-scraped `poolB` (cross-check against the existing
   ground truth).
3. **B-extract (no behavior change):** factor `SceneBindings.glsl:2602-2626`'s
   arithmetic into a named, commented three-line block (or a GLSL function) matching the
   proposed `FootprintRegime` shape, still inline, still identical output. Verify:
   flag-off/on byte-exact bars from `Deep-Field-Mip-Accessor-Policy-2026-08.md:81-88`
   still hold (frame hash `87473180f7b4e603` unchanged, DDA census 414/420 unchanged).
4. **B-CPU-twin (new file, no wiring yet):** write `FootprintRegime.h`
   as a standalone header, unit-tested against the shader's known counter reconciliations
   (`mip=409500 march=9900` etc. from the design doc) using hand-computed world-space
   distances for the benchmark scene's known camera/instance positions. No production
   caller yet — pure function correctness only.
5. **B-wire (residency consumer):** replace `InstanceWantsBrickResidency`'s
   `minResolvableLevel`-based test at `VulkanGraphApplication.cpp:4100` with
   `FootprintRegime(...) == Regime::Surface`, using the same `pc.raySizeCoef`/
   `raySizeBias`/`cosmicK` the shader receives. Verify: `[ResidencyGateDemo]` log
   (`VulkanGraphApplication.cpp:4113-4120`, `VIXEN_RESIDENCY_GATE_DEMO`-gated) shows
   equivalent or better residency transitions vs the pre-change screen-space test on the
   existing residency test scenes (`test_residency_trigger.cpp`,
   `test_occlusion_gate.cpp`).
6. **C-measure (depends on 1):** instrument a long-session `steadyStateBytesUploaded_`
   growth-rate probe (session length vs cumulative steady-state bytes) on a scene with
   several regime-1 bodies crossing the frustum/resolvability boundary repeatedly — the
   falsification test named in section C. Only proceed to any LRU work if this shows
   unbounded growth.

---

## Non-goals

- Building `SVOStreaming.h`'s `ISVOStreamingManager`/per-brick LRU (section C's
  recommendation is explicitly against this, pending the falsification measurement).
- Routing the wholesale buffers (`channelPool`/`mipPool`/`brickLookup`/`tierRefTable`/
  `occupancyGrid`) through `BatchedUploader` (section A's option 2) — not needed for
  correct instrumentation, only for a different upload mechanism.
- A frustum/visibility gate on `CreateOctreeBuffers` for off-screen octrees (named as an
  open question in D, not a designed slice here — it's a different axis than
  regime-1-vs-2/3 brick residency).
- Anisotropic mip / regime-3 cross-instance compositing / sky-sphere caching — all
  explicitly out of scope per `Deep-Field-Mip-Accessor-Policy-2026-08.md`'s own
  "next slice" notes; this doc does not touch regime-3's unfinished pieces.
- Changing `DeriveResidencyDefault`'s capability-gate semantics (section B keeps it
  separate by design).

## Open questions for the controller

1. **Is the "≥68.8% reduction analog" the right anchor for D's projected number**, given
   section D's finding that the wholesale-buffer opportunity (channelPool/mipPool/etc.)
   is a distinct, currently-unquantified lever from the brick-blob residency the 68.8%
   figure measured? Should this doc's slice list include a frustum-visibility gate on
   `CreateOctreeBuffers` as a follow-on slice, or is that explicitly out of scope for
   B49-T2?
2. **GLSL/C++ sync mechanism for `FootprintRegime`** (slice 3/4 above): this is the
   first CPU/GPU pure-function pair in this family with no existing transpiler. Is
   hand-sync (matching this epoch's existing md5-verified-parity discipline) acceptable,
   or does this warrant a small script/test that diffs the two implementations'
   constants (`kEntryBrickSize`, `cosmicK` threshold) to prevent silent drift?
3. **Threshold-constant unification** (section B's last point): `kResidencyLeafSizeM`/
   `kResidencyPxThreshold` (CPU, screen-space) and `kEntryBrickSize`/`pc.cosmicK`
   (GPU, world-space) are independently tuned today. Once `InstanceWantsBrickResidency`
   moves to `FootprintRegime`, the screen-space constants become dead — confirm no other
   caller depends on `minResolvableLevel`/`ResolvableLevel.h` before removal (not
   audited in this pass; `ResidencyTrigger.h`'s blast radius per codegraph shows 2
   callers total, both under `/tests/`, plus the one production caller named above —
   worth a fresh grep at implementation time, not stale-quoted here).
