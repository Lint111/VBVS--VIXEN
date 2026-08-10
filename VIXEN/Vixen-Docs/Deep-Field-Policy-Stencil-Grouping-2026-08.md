---
title: Deep-Field Policy Stencil & Grouped Handling — Code-and-Measurement Revision
status: Partial — E1 census and E2 group-scoped cull shipped; stencil materialization and grouping are DESIGN-FUTURE
date: 2026-08-09
tags: [architecture, stencil, grouping, dispatch, regime, virtual, materialized, composite, traversal, deep-field]
aliases: [Policy Stencil, Regime Stencil, Group Dispatch, Source-Axis Stencil]
related:
  - "[[Deep-Field-Residency-Unification-2026-08]]"
  - "[[Deep-Field-Mip-Accessor-Policy-2026-08]]"
  - "[[Lazy-Procedural-Delta-Baseline-Design-2026-07]]"
  - "C:\\GitHub\\undertow-winbuild\\perf\\e1-slice0\\report.md"
  - "C:\\GitHub\\undertow-winbuild\\perf\\e2-slice1\\report.md"
  - "shaders/TraceWorld.glsl"
  - "shaders/HitRecord.glsl"
---

# Deep-Field Policy Stencil & Grouped Handling

> **DESIGN-FUTURE direction:** materialize per-pixel policy information in a stencil, then group
> downstream handling by that policy. The proposed byte has two orthogonal axes: a footprint regime
> and independently testable VIRTUAL/MATERIALIZED source bits. No production stencil byte, tile
> reduction, or source-gated evaluator dispatch exists yet; the E1 fields are probe-only and are not
> serialized into `HitRecord` (`shaders/TraceWorld.glsl:103-105`; `shaders/SceneBindings.glsl:494-517`).

## 0. Evidence contract and current result

This third revision distinguishes three kinds of statements:

- **SHIPPED** means the cited source is present in the engine tree inspected for this revision.
- **MEASURED** means the number is reported by
  `C:\GitHub\undertow-winbuild\perf\e1-slice0\report.md` or
  `C:\GitHub\undertow-winbuild\perf\e2-slice1\report.md`.
- **DESIGN-FUTURE** means a proposal, prerequisite, prediction, or acceptance gate; it is not a
  description of production behavior.

The current result is **PARTIAL**. The image-inert E1 composition census is shipped behind
`VIXEN_COMPOSITION_COUNTERS`, with nine primary bins, eighteen shadow-wave bins, and one E2
relaxed-ray bin (`shaders/SceneBindings.glsl:494-521`). The E2 blend-interval group relaxation is
also shipped (`shaders/TraceWorld.glsl:569-599`). Stencil storage, tile reduction, grouped dispatch,
and secondary-wave partitioning remain **DESIGN-FUTURE**.

## 1. What the shipped slices establish

### 1.1 E1 composition census — SHIPPED and MEASURED

`TraceWorld` currently carries `footprintRegime` and `sourceMask` only in its transient `WorldHit`
probe fields (`shaders/TraceWorld.glsl:103-105`). The primary march records them after a hit
(`shaders/BodyInstanceRayMarch.comp:238-242`), and the probe maps the three regime values and three
source classes into counters (`shaders/SceneBindings.glsl:494-510`). CPU readback preserves the
same `[regime][source]` shape (`libraries/RenderGraph/include/Debug/RayTraceBuffer.h:217-226`;
`libraries/RenderGraph/src/Debug/RayTraceBuffer.cpp:664-684`).

The measured 300-frame census is:

| Scene | Surface `(V,M,mixed)` | Mip `(V,M,mixed)` | Cosmic `V / M / mixed` | Evidence |
|---|---:|---:|---:|---|
| control | `(0,0,0)` | `(0,0,0)` | `0 / 417,300 / 0` | **MEASURED:** E1 report, boot table, lines 46-51 |
| overlap | `(0,0,0)` | `(0,0,0)` | `0 / 430,500 / 0` | **MEASURED:** E1 report, boot table, lines 46-51 |

The overlap adds **13,200 cosmic/materialized classifications over 300 frames**, or 44/frame and
3.16% relative to the control count (**MEASURED:** E1 report, lines 62-66). The probe-on repeats
are byte-identical (**MEASURED:** E1 report, lines 48-51 and 67).

The virtual and mixed axes are **unmeasured, no virtual content exists in test scenes yet**. Their
zero bins do not demonstrate a virtual-only skip, a mixed-source execution path, or a performance
benefit (**MEASURED:** E1 report, lines 48-51 and 64-66).

### 1.2 Source-mask meaning — SHIPPED probe semantics

For the E1 probe, source means **hit-candidate participation**, not evaluator visitation and not
final-winner-only provenance. The procedural bit is ORed only after `pHit`
(`shaders/TraceWorld.glsl:365`), the stored/materialized bit is ORed only after `instHit`
(`shaders/TraceWorld.glsl:691`), and the accumulated mask is published after the instance loop
(`shaders/TraceWorld.glsl:735-745`). The primary census consumes that published mask only when
`TraceWorld` returns a hit (`shaders/BodyInstanceRayMarch.comp:238-242`).

**DESIGN-FUTURE:** a materialized stencil must retain this exact hit-candidate-participation
definition unless a separately measured experiment deliberately changes it. Writer sites belong
after successful primary-function candidate hits; evaluator entry and shadow-function entry are
not equivalent producer sites. The virtual and mixed stencil behavior is **unmeasured, no virtual
content exists in test scenes yet**.

### 1.3 Footprint regime authority — prerequisite remains DESIGN-FUTURE

There is no production `FootprintRegime` type or shared classifier. The E1 probe says its numeric
values are local until that production symbol exists (`shaders/SceneBindings.glsl:494-496`), while
stored-path classifications still repeat inline math (`shaders/TraceWorld.glsl:500-515` and
`shaders/SceneBindings.glsl:2632-2642`).

**DESIGN-FUTURE prerequisite:** extract and parity-test the single classifier specified by
[[Deep-Field-Residency-Unification-2026-08]] before stencil materialization or additional wave call
sites. This document intentionally does not republish the formula; doing so would create another
hand-maintained authority.

### 1.4 Primary launch extent — SHIPPED

The command recorder derives the live render-target extent, ceiling-divides each dimension by the
8×8 workgroup size, and dispatches those values (`libraries/RenderGraph/src/Nodes/ComputeDispatchNode.cpp:423-431,489-490`).
At 500×500 this code launches 63×63 groups, and the shader rejects only lanes outside the image
(`shaders/BodyInstanceRayMarch.comp:217-221`).

`BuildRenderGraph` still writes and logs floor-divided `DISPATCH_X/Y` parameters
(`application/main/source/graph/BuildRenderGraph.cpp:1853-1862`), but those values are not the
execution authority used by `vkCmdDispatch`. The shipped launch covers all 500×500 valid lanes, and
no floor-to-ceiling dispatch edit belongs in a stencil slice.

## 2. Group-scoped composite cull — SHIPPED

### 2.1 Implemented behavior

The front-to-back entry test first computes `entryBehindCurrentBest`
(`shaders/TraceWorld.glsl:557-570`). Under `VIXEN_REGIME3_COMPOSITE`, it relaxes the reject only when
the current winner is cosmic and its residual transmittance is strictly inside
`(1e-6, 0.999999)` (`shaders/TraceWorld.glsl:571-586`). Composite-off retains the original reject
(`shaders/TraceWorld.glsl:587-599`). The downstream consumer uses the same strict interval
(`shaders/BodyInstanceRayMarch.comp:244-264`).

The census counts a primary ray at most once even when more than one behind candidate is admitted
(`shaders/TraceWorld.glsl:168-172,580-584`), stores the count in probe slot 27
(`shaders/SceneBindings.glsl:513-516`), and prints it as `[CompositionRelaxed]`
(`libraries/RenderGraph/src/Nodes/VoxelGridNode.cpp:645-666`).

### 2.2 Measured result and orchestrator ruling

| Leg | Frame MD5 | Relaxed rays | `[CompositeBlend]` | Evidence |
|---|---|---:|---|---|
| control-floor2 | `3951c2c5df508d3db0045a7bc7fe1d71` | 0 | `blends=0 behindMax=0` | **MEASURED:** E2 report, lines 68-75 |
| control-composite | `c957de521268607ba0ac75d9adefe6f1` | 8,400 / 300 = **28/frame** | `blends=84300 behindMax=0.633939` | **MEASURED:** E2 report, lines 68-75 |
| overlap-floor2 | `5e9426528170fde067b7c76059945003` | 0 | `blends=0 behindMax=0` | **MEASURED:** E2 report, lines 68-75 |
| overlap-composite, repeat 1 and 2 | `8ceddca0337356372e586ba7f877b0c2` | 8,400 / 300 = **28/frame** | `blends=84300 behindMax=0.633939` | **MEASURED:** E2 report, lines 68-75 |

The overlap composite is the C3 demonstration: it deterministically diverges and reaches
`behindMax=0.633939` (**MEASURED:** E2 report, lines 83-95). The control-composite movement is also
the intended fix operating on that scene's own farther candidates, not a failed control; the
orchestrator explicitly ruled E2-T1 **DONE** and accepted both new references (**MEASURED/RULING:**
E2 report, lines 118-130).

The measured relaxation is **28 rays/frame**, or **0.0112% of 250,000 primary rays/frame**
(**MEASURED:** E2 report, lines 86-87). This bounds affected rays only. It does not bound extra
instance traversals, because one relaxed ray may admit multiple later intersecting instances
(`shaders/TraceWorld.glsl:168-172`). It also does not imply a timing ceiling: the two composite
timing repeats differ by about 10%, so the report attributes no timing delta to the relaxation
(**MEASURED:** E2 report, lines 88-95).

The virtual-axis effect of this cull is **unmeasured, no virtual content exists in test scenes yet**.

### 2.3 Composition ceiling — SHIPPED limitation

The composite build carries exactly one `secondT`/`secondColor` pair
(`shaders/TraceWorld.glsl:158-167`), retains the nearest losing hit
(`shaders/TraceWorld.glsl:720-731`), and exports that single color
(`shaders/TraceWorld.glsl:735-751`). **DESIGN-FUTURE:** deeper transparency or volume stacks require
an explicit termination/transmittance budget and additional storage or queueing; the shipped C3 fix
does not establish a general multilayer solution.

## 3. Per-pixel stencil storage — DESIGN-FUTURE

### 3.1 Proposed byte

**DESIGN-FUTURE:** use one byte with bits 0-2 for a footprint-regime value, bit 3 for VIRTUAL, bit 4
for MATERIALIZED, and bits 5-7 reserved. VIRTUAL and MATERIALIZED are independent flags so a mixed
pixel can set both. The source semantics are the hit-candidate-participation semantics in §1.2.

**DESIGN-FUTURE:** reserve regime values only after the shared `FootprintRegime` authority exists.
Surface, mip-hit, and cosmic are the only numeric values exercised by the E1 probe
(`shaders/SceneBindings.glsl:494-504`); volumetric, translucent, and transparent have no shipped
producer or consumer.

### 3.2 Candidate home and required preservation fix

`HitRecord` declares 60 bytes of fields and uses a 64-byte runtime-array stride
(`libraries/RenderGraph/tests/Nodes/test_hitrecord_sdi_parity.cpp:41-72,124-131`). Its `_pad0[2]`
word is currently owned by shadow visibility bits 0-4 (`shaders/HitRecord.glsl:46-57`). The analytic
shadow phase stores only bits 0-3 (`shaders/ShadowVisibilityWave.comp:197-203`), and the reservoir
phase preserves bits 0-3 while setting bit 4 (`shaders/ShadowVisibilityWave.comp:133-146`). Both
current stores erase bits 8-15.

**DESIGN-FUTURE:** if the stencil byte occupies `_pad0[2]` bits 8-15, the analytic writer must
read-modify-write only its owned low bits and the reservoir writer must preserve every non-owned
bit. Update both ownership comments and add a parity/readback gate. This changes no declared
`HitRecord` size or 64-byte stride, but it is a prerequisite—not shipped behavior.

### 3.3 Materialization and tile reduction

**DESIGN-FUTURE:** write the winning regime plus the accumulated hit-source mask into the selected
stencil byte during the primary record write. Reduce each 8×8 tile into a regime-presence mask and
an OR of source flags. At 500×500, the shipped 63×63 launch in §1.4 implies 3,969 tile entries;
the buffer shape and reduction algorithm remain unimplemented.

**DESIGN-FUTURE:** use the tile aggregate only to skip an evaluator whose source bit is absent. A
tile lacking MATERIALIZED may skip the materialized portion; a tile lacking VIRTUAL may skip the
virtual portion; mixed tiles run both. This optimization is **unmeasured, no virtual content exists
in test scenes yet**. E1's all-materialized stored scenes can validate the classification plumbing
but cannot validate the virtual skip or its performance.

## 4. Secondary waves — DESIGN-FUTURE

No destination-policy queue partition is shipped. The queued shadow request is exactly 32 bytes and
contains only `{origin,tmin,dir,tmax}` (`shaders/ShadowRayQueue.glsl:12-20`). `ProbeGather` originates
work from DDGI probe positions and writes that request plus a separate payload
(`shaders/ProbeGather.comp:161-208`); it does not originate from a screen pixel or fetch a pixel
`HitRecord`. `ProbeApply` consumes result and payload buffers (`shaders/ProbeApply.comp:35-46,172-193`).

Therefore these items are **DESIGN-FUTURE prerequisites**:

1. Define provenance separately for pixel-origin, probe-origin, and world-origin producers. Only a
   producer that actually reads a pixel `HitRecord` may inherit its launch stencil.
2. Define cone coefficient/bias, accumulated path distance, and policy-result storage in the request
   or a companion result ABI. The shipped 32-byte request has none of those fields
   (`shaders/ShadowRayQueue.glsl:17-20`).
3. Trace/classify before destination-keyed compaction. Destination regime is a traversal result and
   cannot be used as a pre-trace partition key unless a separate conservative key is defined.
4. Treat recipe bucketing only as architectural precedent for compaction and indirect dispatch; it
   is not a reusable implementation of policy-wave partitioning.
5. Measure non-empty wave populations before sizing partitions. E1's stored control and overlap
   scenes measured zero provider-reaching queued and visibility-wave entries
   (**MEASURED:** E1 report, lines 46-51 and 66). The identity leg later measured non-zero derived
   visibility entries but still no virtual or mixed content (**MEASURED:** E2 report, line 70).

All virtual/mixed secondary-wave behavior is **unmeasured, no virtual content exists in test scenes
yet**.

## 5. Extensibility boundary — DESIGN-FUTURE

The one-byte layout can represent future regime values and additional source flags without growing
`HitRecord`; that is a **DESIGN-FUTURE layout capacity claim**, not evidence that a new behavior is an
enum-only change. Every new regime still requires an amendment to the shared classifier, producer
semantics, consumer policy, cull rule, and measurement matrix. Every new source flag still requires
real producer sites and evaluator gates.

No reserved regime inherits the shipped composite cull exception. The exception is specifically
the conjunction implemented at `shaders/TraceWorld.glsl:571-586` and consumed at
`shaders/BodyInstanceRayMarch.comp:244-264`.

## 6. Slice order and acceptance gates

| Slice | State | Evidence / gate |
|---|---|---|
| E1 composition census | **SHIPPED, MEASURED** | Probe implementation at `shaders/SceneBindings.glsl:494-521`; measured tables in E1 report lines 42-67. |
| E2 group-scoped cull | **SHIPPED, MEASURED, ORCHESTRATOR-ACCEPTED** | Implementation at `shaders/TraceWorld.glsl:569-599`; accepted references and 28 rays/frame in E2 report lines 68-95 and 118-130. |
| Shared `FootprintRegime` extraction/parity | **DESIGN-FUTURE prerequisite** | Current inline duplicates at `shaders/TraceWorld.glsl:500-515` and `shaders/SceneBindings.glsl:2632-2642` must be replaced by one authority. |
| Stencil byte + shadow-word preservation | **DESIGN-FUTURE** | Preserve current shadow ownership at `shaders/HitRecord.glsl:46-57` and fix the erasing stores at `shaders/ShadowVisibilityWave.comp:133-146,197-203`. |
| Tile reduction + evaluator skips | **DESIGN-FUTURE, unmeasured** | Must preserve the shipped 63×63 primary extent (`ComputeDispatchNode.cpp:423-431,489-490`); virtual/mixed acceptance scenes do not exist. |
| Secondary-wave provenance/ABI/classification/compaction | **DESIGN-FUTURE, unmeasured** | Current request lacks policy state (`shaders/ShadowRayQueue.glsl:12-20`); current DDGI producer is probe-origin (`shaders/ProbeGather.comp:161-208`). |

The next schedulable implementation slice is not “group everything.” It is the shared classifier
prerequisite followed by primary stencil materialization and word-preservation tests. Virtual-source
skip work remains blocked on a scene containing virtual content, because that axis is currently
**unmeasured, no virtual content exists in test scenes yet**.
