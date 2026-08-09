---
title: Deep-Field Mip-Accessor Policy — As Implemented
status: active
created: 2026-08-08
tags: [architecture, mip, lod, far-field, traversal, dda, esvo, rt]
---

# Deep-Field Mip-Accessor Policy — as implemented

**Authority:** the spec is undertow-owned —
`docs/superpowers/specs/2026-08-08-deep-field-mip-policy-design.md`, ratified by three
user rulings during the wavefront epoch. Status/evidence live on the ledger —
`docs/plans/2026-08-04-wavefront-recipe-shading.md` (2026-08-08 entries, esp. "USER
RULING: DDA IS THE EXCEPTION" and "USER CORRECTION + Batch 35 result"). This page does
not restate that detail — it says what shipped in this engine tree and where.

## The three regimes (spec, unchanged here)

One footprint→level function, evaluated per ray/instance:

1. **SURFACE** (footprint < voxel size) — exact fine DDA march, binary hit.
2. **MIP HIT** (voxel ≤ footprint < K·cell) — sample the mip ladder at the matched level,
   commit as a hit, no march.
3. **COSMIC** (footprint ≥ K·cell) — transmittance accumulation over coarse cells. Slice 1
   landed (batch 40, see below): flag-gated, byte-exact when off, and on opaque content
   provably degenerates to the mip hit — cross-instance compositing (the actual
   nebula-over-galaxy bar) is the next slice. The anisotropic mip data it will eventually
   consume is baked and waiting (see below) but not yet read at traversal time.

## Division of labour (SHIPPED, batch 35)

The control-flow inversion from the 2026-08-08 user ruling — "dda should only be done
when the ray hit is close enough to have actual detail in view, otherwise always go for
mipmap sampling" — is landed and structural, not a fallback:

- **Entry dispatch decides the regime before any march starts**, at ray/instance entry —
  `shaders/SceneBindings.glsl:2478-2548` (`#ifdef VIXEN_MIP_POLICY` block). It computes
  the entry footprint and, if the policy admits, resolves the mip sample and **returns
  before the DDA march loop begins** (the march loop starts at `:2554`). Mip sampling is
  the default; a march happens only when the footprint says real detail is in view.
- **DDA owns voxel-brick traversal — level 0 by definition.** Asking DDA to resolve
  coarser rungs was a category error (user correction, 2026-08-08): DDA's domain IS the
  brick, so "DDA only samples L0" is correct, not a defect. Coarser levels are the entry
  dispatch's job, never DDA's.
- **RT-composed evaluates per candidate**, so its level distribution naturally spreads
  across rungs (L0/L1/L2 observed). That is a property of its per-candidate mechanism,
  not evidence that composed traversal is "doing better" than DDA — the two are not
  comparable on a level-spread histogram, and that histogram is not an efficiency
  instrument for DDA.

## The anchoring rule (SHIPPED, batch 36; CONFIRMED WORKING, batch 38)

The entry-dispatch footprint was originally anchored on the ray/instance **entry** point
(nearest approach), which undershoots: measured gate LHS collapse 1.86984 → 0.375855
(−80%). An undershot footprint selects too fine a level and wrongly admits rays into the
detail march that should have been mip-sampled outright. The fix anchors the footprint on
the **midpoint** distance instead (`entryMidT`) and is landed —
`recordEntryGateLhs`, `shaders/SceneBindings.glsl:2539`, sits before the admit branch
(`:2542`) so both admitted and rejected rays are sampled (not self-confirming).

**Batch 38 confirmed the fix works, on dda-on, 3/3 boots byte-identical:**
`[EntryGateLhs] min=0.369571 max=1.87239`. Entry LHS max moved **0.375855 → 1.87239
(4.98×)** and now sits at the mid-march ceiling 1.86984 (ratio 1.0014); min barely moves
(1.009×), correct for short spans since `entryMidT >= tEnter`. The earlier "blind
instrument" reading (batch 37) was itself the defect — `recordFarFieldGateOperands` has
only two call sites (the mid-march safety net and the RT twin), neither of which the
entry-dispatch block touches, so the flat 0.366347/0.375855 reading was the SAFETY NET's
unchanged behaviour, not the entry gate's.

**Scope limit (must hold):** the dispatch split 409,500 mip / 9,900 march / 7,200
safety-net (reconciling to `FarFieldCount` 416,700) is **unchanged** by the anchor fix —
footprint SIZING moved, regime ASSIGNMENT did not. The fix works on the quantity it
targets and nothing beyond it.

**Composed has no anchor conclusion — structural, not a defect.** `[EntryGateLhs]` reads
`-nan/0` on all composed boots because composed is a per-candidate RT mechanism that never
routes through the entry-dispatch call site (`march=0` for the same reason).

## Standing regression bars (hold across any change to this path)

- **Flag-off identity:** `87473180f7b4e603` (byte/PNG-level, not a weaker hash-of-hash
  check — a prior "correction" to a different hash was independently re-verified wrong
  and rejected).
- **DDA census:** 414/420, mean 244.3071, max 332.
- **DDA policy-on frame parity:** 0 px delta vs baseline over the full 500×500, byte-level.
- **D=612 parity:** the closed far-field arc's brick-rung ESVO/DDA/RT-composed pixel
  parity at D=612/811/1207wu — the regime-2 special case that any full-ladder
  implementation must not regress.
- Near bodies (genuine detail, small footprint) must still take the exact march — the
  ruling explicitly keeps them there; this is not a blanket coarsen.

Entry-dispatch split counts from the batch-35 landing (reconciles exactly against
`FarFieldCount`): 409,500 entry-mip / 9,900 entry-march / 7,200 safety-net /
416,700 total.

### The `emptyEntry` characterization (batch 39/40) — `march=9900` never meant "detail in view"

`recordPolicyEntryDispatch(false)` (`SceneBindings.glsl:2601`) sat **outside** the
`if (entryPolicyAdmits)` block, so the `march` bucket conflated two populations: rays
genuinely rejected at entry (real detail in view) and rays **admitted** whose entry cell
turned out unbricked ("empty entry"). The fix added an additive third counter,
`emptyEntry`, without moving `mip`/`march`/`FarFieldCount` (sync verified CRLF-normalized
MD5 across all five files; struct grew 480→496 bytes, both new fields seeded 0).

**Result (batch 39, reproduced byte-identically batch 40, 3/3 boots):**
`[PolicyEntryDispatch] mip=409500 march=9900 emptyEntry=9900`. `emptyEntry == march`
**exactly** ⇒ in this scene/config, **100% of the "march" population is
admitted-but-empty-entry-cell; ZERO rays are genuinely detail-regime-rejected at entry.**
This is a characterization (two independent boots, promoted from batch-39's single-boot
observation), not yet a general claim beyond this scene/config.

**Reconciliation — two denominators, do not mix them (validator, batch 40):**
`FarFieldCount 416700 = mip 409500 + safety 7200` exactly, but `mip + march = 419400 ≠
416700` (the −2700 gap = `march 9900 − safety 7200`): march rays are **not** a subset of
`FarFieldCount`. composed-on reconciles differently, as `far = mip + 0` — a structurally
different formula (composed never routes through the entry-dispatch site at all, per the
anchoring-rule section above).

## The two-state boot alternation (validator finding, batch 40)

Frame-hash comparison (not census) revealed that nominally-identical boots draw from
exactly **two** frame states per config, not one: the default-scene identity/dda-on legs
alternate between MD5 `87473180f7b4e603` and `9f5ea513…`. `cmp` proved
dda-on-3 ≡ identity-1 and dda-on-1 ≡ identity-2 **byte-identical**, while their counters
differ absolutely (409500/9900/9900 vs 0/0/0 — configs genuinely differ via the ENGAGED
log line). Consequence: **dda-on ≡ identity at full byte-exactness** — the policy resolves
409,500 rays through the mip path and lands the exact image the incumbent produces (the
regime-2 parity bar holding at full strength, not a regression). The two states differ in
1,089 px; census `count` reads 414 on both (stable but coarse — frame hashes are the
sharper parity instrument going forward). See Known-Issues.md for the flake-family note.

## Cost

**NOT-CERTIFIABLE — root cause narrowed, not resolved (batch 39/40, supersedes the
earlier "concurrent GPU workload" framing below).** The "another app" contention theory
was a controller misattribution: `nvidia-smi` confirmed the GPU idle at P8/12 W with only
`explorer.exe` on the device — Steam-alive ≠ GPU-busy. The real picture, reconciled with
commit `cf3a30fd` ("W-BASE stall decomposition closed"): **the frame is
COMPUTE-LATENCY-BOUND, not pacing-bound and not contended.** GPU 98.8% busy while SM issue
= 0.39% (L2 25.2%, warps resident 35.6%) — a dependent-load latency chain inside the ESVO
traversal shader (pointer-chase + L2 atomic round-trips) backpressures the frame's
`vkWaitForFences` (`FrameSyncNode.cpp:148-149`, ~20 ms). The 210 MHz/P8 clock is
latency-bound-but-occupied, not idle: under a 3000-frame load boot the clock touched 960
MHz (P3) for exactly one sample (46% of max) while utilization read 4%, then fell back —
there is no clock plateau to warm into. Present mode is already IMMEDIATE (uncapped;
`VulkanSwapChain.cpp:380-408`), so pacing/vsync is not the lever either.

**Even so, within-leg variance still exceeds any between-leg effect**: identity-1 2.80 ms
vs identity-3 3.99 ms, same config, same binary. The march-AVOIDED win this dispatch
inversion exists to deliver remains **unmeasured, not disproven**; the next probe is
per-boot clock stamping + fence-wait jitter, not more boots or GPU warm-up (both tried and
ruled out as the fix). cf3a30fd's lever ranking stands for where the compute-latency
budget actually goes: W-RT ray query > B2 shared-mem premerge > kernel splitting.

<details>
<summary>Superseded framing (kept for history — do not cite)</summary>

Originally recorded as "not certifiable — a concurrent GPU workload (another app) was
confirmed running on the machine, producing up to 165× within-config spread." The
concurrent-workload claim was a controller instrument defect (`tasklist /FI` blank-output
trap, see Known-Issues.md) and is retracted; the compute-latency-bound framing above
supersedes it.
</details>

## Regime 3 (COSMIC) — slice 1 CLOSED (batch 40 landing + post-batch-40 close-out)

**Code landed, gated additively.** `pc.cosmicK` push constant + `VIXEN_REGIME3_K` env;
`VIXEN_REGIME3` env → define splice, gated on `VIXEN_MIP_POLICY`; accumulation walk lives
inside the `entryPolicyAdmits` block (`C += T·cov·color; T ×= (1−cov)`; early-out at
`T<0.02`; falls through to the ordinary march if nothing sampled). Loop bounded three
independent ways (64-cell budget, grid-bounds break, span break — no GPU hang risk).
`TraceBufferHeader` grew 496→512 bytes (`regime3EntryCount`/`regime3EarlyOutCount`),
GLSL↔C++ field-for-field, md5-sync-verified across all 7 touched files.

**3-leg smoke, ALL PASS (`gates/batch40_regime3_smoke.bat`):**
- **`r3-off`** (flag unset): frame hash `87473180` = known incumbent state;
  `[PolicyEntryDispatch] 409500/9900/9900` and `[FarFieldCount] 416700` unchanged;
  `[Regime3] entry=0`. Byte-exact bar holds on the unflagged variant.
- **`r3-on`** (default K): ENGAGED line prints; all counters identical to `r3-off`; frame
  hash unchanged. Default K admits nothing in this scene — flag-on is image-neutral until
  K reaches scene footprints (sanity check, not a null result).
- **`r3-onk`** (K=0.2, forced reachable) — **the walk fires and its numbers reconcile:**
  - `[Regime3] entry=419400 earlyOut=416700` — entry = 409500+9900 = every
    policy-admitted ray (mip AND empty-entry populations both promoted to the walk), not
    inert.
  - `[FarFieldCount] n=0` — the walk fully replaces the single mip-hit commit, by design.
  - `earlyOut=416700` equals the OLD `FarFieldCount` exactly (409500 entry + 7200 safety):
    on opaque bricks coverage≈1 ⇒ the first cell gives `C=color, T→0` ⇒ early-out after
    ONE cell ⇒ **the accumulation degenerates to the mip hit**, confirming the theory's
    opaque-limit prediction by counter identity.
  - Frame hash `9f5ea513` — the OTHER known incumbent state from the two-state
    alternation above ⇒ image byte-identical to the incumbent (modulo that alternation).
    Cosmic accumulation over opaque content reproduces the same picture, as it must; the
    regime only diverges on translucent/sparse content, which this scene has none of.
  - **Open reconciliation question (not a blocker):** residue `mip=0 march=5400
    emptyEntry=2700` — 2700 walks sampled nothing and fell through (419400−416700), but
    `march=5400 ≠ 2700`; suspect is safety-net re-entry counts under the walk. Flagged for
    the next instrument pass, not yet root-caused.

**Slice 1 complete:** code + instruments + reachability + byte-exact flag-off +
opaque-limit parity. Still needed for regime 3 to be MEANINGFUL: a scene with
translucent/sparse coarse content, the sky-sphere cache, anisotropy consumption,
hysteresis.

### Sparse-body divergence attempt (batch 41 / 41-v2) — bake bug found+fixed; cross-instance compositing is genuinely out of scope for slice 1

A 7th body (scattered spherical shell, kind-6 raw-eval lambda into `BakeSdfWorld`,
env-gated `VIXEN_SPARSE_BODY=1`, default off) was added to exercise sparse coarse content
against regime 3. Default-off gate proven clean (`[BrickDataHash]` byte-identical to
baseline; frame `87473180` ∈ known state set).

**Root cause found (batch 41): `SdfBake.h:176-201` dilates the occupied brick mask by ONE
brick.** The mask correctly kept 151/512 bricks (29.5%, on target), but gaps exactly one
brick wide were sealed by the dilation: **502/512 = 98.0% dense post-bake** (pool bytes
6,168,576 B = 98.8% of a dense body's 6,242,304 B). The scene never had coverage<1 — the
mip nodes correctly reported the truth about wrongly-dense bake data. Two instrument
lessons carried to Known-Issues.md: pool BYTES is the bake-sparsity truth instrument
(unconditional, no `VIXEN_NODE_LOG` needed); a "< 0.9936" threshold criterion was vacuous
because the real baseline (0.993562) rounds into it — thresholds need full precision or
explicit strict-vs-baseline framing.

**Fix (batch 41-v2): mask cells widened 1→4 bricks**, keep-rate 40%→25% (compensating for
the skirt re-inflating kept regions by ~(6/4)²). **Confirmed by pool bytes:** sparse-on
41,201,664 B − sparse-off 37,453,824 B = 3,747,840 B = **60.0%** of a dense body (down from
98.8%), matching the predicted (6/4)²×25% ≈ 56%.

**But: counters bit-identical to the pre-fix (98%-dense) run** — `entry=420000
earlyOut=417300 march=5400 emptyEntry=2700` on both bakes, same ~0.62× uniform per-channel
dimming (1091 px total diff, no hue shift), zero body4 magenta either way.

**Resolution — the missing cross-instance bleed-through is BY DESIGN, not a bug.**
`SceneBindings.glsl:2583-2587`'s own banner: *"This slice does NOT composite over deeper
content (out of scope) — it commits the accumulated color as the hit."* The walk is
per-instance: it accumulates within the sparse body's own grid, then commits `accumColor`
as an opaque hit — residual transmittance T is discarded (renders black), never applied to
content behind. The uniform dimming IS the slice-1 walk operating exactly as written. The
batch-41 brief's demand for cross-instance bleed-through as a success bar was a brief
defect against code that explicitly scopes that out — the next regime-3 slice (below) is
where it belongs.

**Open instrument question, unresolved:** `walkCov = clamp(walkSdf.y,0,1)` (from
`readMipSample(SEM_SDF)`) reads ≈0.62 on BOTH the 98%-dense and 60%-dense bakes —
suggesting the coverage source measures in-band voxel fraction *within* occupied bricks
(constant for a given shell thickness), not brick-level occupancy. If so, regime 3's
density proxy may be blind to bake-level sparsity entirely. This is a hypothesis backed by
counter identity across two different bakes, not yet independently measured — needs a
dedicated walkCov/walkSampledLevel probe before the next slice leans on it. See
Known-Issues.md.

**Next regime-3 slice (now precisely scoped):** cross-instance compositing — carry
residual T out of the walk and blend over deeper content (the actual nebula-over-galaxy
bar) instead of committing an opaque hit — plus the walkCov source audit.

## Anisotropic coarse mips (regime-3 groundwork, baked ahead of its consumer)

The GigaVoxels-steal 6-axis directional-coverage encoding for coarse mips (spec's
"Anisotropic coarse mips" section) is implemented and self-checked, ahead of any regime-3
consumer:

- `libraries/SVO/include/MipAnisoPool.h` — `MipAnisoSample` (6-float POD, one per axis),
  `MipAnisoPool`, `BakeMipAnisoPool`.
- Wired additively into `ShellOctreeGpu.h`'s `Octree`/`SerializedOctree`
  (`mipAnisoPool` + `mipAnisoPoolBases`), populated only for coarse interior nodes above
  a threshold level — zero cost where it doesn't apply.
- `libraries/SVO/tests/test_mip_aniso_pool.cpp` — bake-time self-checks green: an
  axis-aligned slab shows strong single-axis asymmetry, a solid cube is isotropic, nodes
  at/above the threshold level are untouched, and the serialized byte size matches
  `nodeCount × channelCount × sizeof(MipAnisoSample)`.

The regime-3 (COSMIC) accumulator landed (slice 1, above) but does not yet read this
pool — anisotropy consumption is a named follow-on, not part of slice 1's scope.

## See also

- [[Sparse-Mip-ESVO-LOD-Direction-2026-07]] — the original per-level sample /
  fallback-on-miss mechanism this policy formalizes and supersedes as the dispatch rule.
- [[GigaVoxels-Techniques-Digest-2026-08]] — source of the anisotropic-coverage steal.
- [[Measurement-Discipline-2026-08]] — the evidence rules (seeded probes, state-set
  hashing, pool-bytes-as-truth, count-vs-mean/max, box-lock pattern, brief-bar rule) this
  epoch's findings above were produced under.
- `docs/superpowers/specs/2026-08-08-deep-field-mip-policy-design.md` (undertow) — the
  spec.
- `docs/plans/2026-08-04-wavefront-recipe-shading.md` (undertow) — the ledger, evidence
  and batch-by-batch history.
