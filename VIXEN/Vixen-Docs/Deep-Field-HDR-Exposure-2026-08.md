---
title: Deep-Field HDR Accumulation and Exposure
status: DESIGN-READY
date: 2026-08-10
tags: [hdr, exposure, radiance, deep-field, determinism, gates]
---

# Deep-Field HDR Accumulation and Exposure

## Decision

Adopt a single scene-linear HDR radiance domain from `SpatialReuseShade` through the
accumulation history.  Add exactly one display transform: deterministic exposure plus
tonemap in a new final compute pass, immediately before the existing 8-bit render target
and blit.  The final `rgba8` image remains the image-gate witness; it is no longer a
radiance accumulator.  This is necessary because the current composite has already
measured a second contribution needing roughly `0.251` before it can change an 8-bit
witness at `T=0.015625`; that is incompatible with derived solar/scattering lighting
([Known-Issues.md:1688-1691](../04-Development/Known-Issues.md)).

This is a proposal. “Current” facts below are pinned to engine commit `231e272e`.

## Current radiance inventory

| Stage / resource | Current format and precision | Owner / consequence |
|---|---|---|
| `HitRecord` | 64-B SSBO; geometry/albedo and flags, not a radiance target | March writes one record per render-target pixel (`BuildRenderGraph.cpp:492-501`). Its spare `_pad0[2]` is the stencil home, so HDR must not consume it (`Deep-Field-Policy-Stencil-Grouping-2026-08.md:171-181`). |
| ReSTIR reservoirs A/B and combined reservoir | `ReservoirRecord`, 16 B/pixel SSBO | Selection/reuse state, not accumulated radiance; DirectLighting writes the ping-pong buffers and SpatialReuse consumes the combined record (`DirectLighting.comp:131-160`; `SpatialReuseShade.comp:146-157`). |
| Hit-accum table | 14×32-bit words = 56 B/cell; fixed-point sums | Order-independent hit aggregates, not radiance (`HitAccumulationCommon.glsl:47-59`). |
| `cellRadiance` | `vec4` SSBO = 16 B/live table slot, effectively fp32 | `HitAccumCellShade` stores RGB and uses alpha as a bit-packed key; its EMA is `mix(prev.rgb, Lo, temporalAlpha)` (`HitAccumCellShade.comp:175-188`). This is already HDR-capable and must stay fp32: alpha is not available capacity. |
| Shaded output | `outputImage rgba8` = 4 B/pixel | `SpatialReuseShade` owns output, history, and world-position writes (`BuildRenderGraph.cpp:525-537`). Its direct image format is `rgba8` (the old quantization bite). |
| Radiance history | `historyImage rgba8` = 4 B/pixel | The persistent history is format-matched to `R8G8B8A8_UNORM` output (`AccumulationHistoryNode.h:23-36,91`), and the shader declares it `rgba8` (`SpatialReuseShade.comp:93-103`), so the temporal average is quantized every frame. |
| Geometric history | `worldPosHistoryImage rgba32f` = 16 B/pixel | Already correctly high precision for reprojection (`WorldPosHistoryNode.h:23-37,74`; `SpatialReuseShade.comp:114-120`). No format change. |
| DDGI inputs | irradiance `rgba16f`, visibility `rg16f` | These are HDR-compatible inputs to shading (`SpatialReuseShade.comp:161-168`); do not demote or duplicate them. |
| Present witness | `RenderTargetNode` output is `R8G8B8A8_UNORM`, then `BlitNode` copies it to swapchain | The graph explicitly keeps the presentation-only blit after lighting (`BuildRenderGraph.cpp:553-558`); this is the only intended 8-bit conversion point after this change. |

`SEM_EMISSION` / recipe emission is threaded into cell shading as the representative
recipe's parameter and contributes `albedo * emission` before direct lighting
(`HitAccumCellShade.comp:113-151`). ReSTIR is intentionally still per-pixel while
cell radiance is resolved over it (`HitAccumCellShade.comp:35-38`); HDR must preserve that
composition rather than moving exposure into either branch.

The current cosmic path composites in linear-looking shader values as
`C += T*cov*color; T *= (1-cov)` and stops at `T < 0.02`
(`Deep-Field-Mip-Accessor-Policy-2026-08.md:192-200`). It is therefore upstream of the
display transform. C3 is only one retained second candidate, not a general volume stack
(`Deep-Field-Policy-Stencil-Grouping-2026-08.md:147-154`), which is another reason not to
hide its residual energy with early `rgba8` writes.

## Format ladder and allocation cost

All costs below are bytes per render pixel, excluding Vulkan alignment/allocator overhead.
At the standing 500×500 witness, one B/pixel is 250,000 B (0.238 MiB).

| Slice | Change | Increment | Cumulative | Why this order |
|---|---|---:|---:|---|
| 0 | No format change: keep HitAccum `cellRadiance` fp32 and world-position history fp32. Reuse `HitAccumParams.camPos.w` only for its existing EMA alpha. | 0 B | 0 B | Reuse-first: `cellRadiance.a` is a packed key, and existing fp32 resources have distinct semantics. |
| 1 | Add `sceneRadianceImage`, `rgba16f`, written by `SpatialReuseShade` instead of `outputImage`; add `sceneRadianceHistory`, `rgba16f`, replacing the current `rgba8` history. | 8 + 4 = 12 B | 12 B | This is the smallest identity-safe HDR accumulation seam: current-frame radiance and temporal average cannot quantize. `rgba16f` supplies signed/large-range RGB and a spare alpha for validity/weight; do not use RGB9E5/E5B9 because its shared exponent is unsuitable for an alpha/history contract and dark scattering beside sunlit values. |
| 2 | Add `ExposureTonemap.comp`, reading scene radiance and writing the existing 4-B `R8G8B8A8_UNORM` render target. Retire the old 4-B `rgba8` history allocation. | already counted | 12 B | One output allocation remains: this replaces, rather than adds beside, the legacy history. |
| 3 | Optional metering reduction: 1/16-resolution `r16f` log-luminance image plus deterministic reductions. | 0.00390625 B/pixel | 12.00390625 B | Only after Slice 1 gates are green; a CPU/readback histogram is forbidden because it changes timing and driver ordering. |

At 500×500, Slice 1 adds 3,000,000 B (2.86 MiB): 2,000,000 B for the two
`rgba16f` images minus the retired 1,000,000-B `rgba8` history? More plainly, it replaces
one 1,000,000-B `rgba8` history with two 2,000,000-B `rgba16f` resources, a net
3,000,000 B. The existing 1,000,000-B `rgba8` display target remains. `rgba32f` is
reserved for a demonstrated fp16 overflow/precision failure; using it for both HDR images
would cost a further 8 B/pixel (1.91 MiB at 500×500) without serving the first slice.

## Exposure operator: one home, deterministic inputs

`ExposureTonemap.comp` is the sole owner of display conversion. Its graph position is:

```
march → ReSTIR/SRS + cell-radiance resolve → sceneRadianceHistory (linear HDR)
      → ExposureTonemap (exposure, ACES-fitted curve, sRGB/UNORM encode)
      → existing R8G8B8A8 render target → BlitNode → present
```

It must run after all radiance contributions, including the accumulation/cosmic composite,
and before `BlitNode`; no shading, reservoir, history, or mip accessor may apply exposure.
The current graph already isolates the blit after lighting (`BuildRenderGraph.cpp:525-558`),
so the named implementation seam is
`application/main/source/graph/BuildRenderGraph.cpp`, new
`shaders/ExposureTonemap.comp`, and new RenderGraph node/config beside
`AccumulationHistoryNode`.

Default control is derived auto-exposure, not authored scene light. Meter the post-accumulation
HDR image’s log2 luminance using fixed 16×16 workgroups, fp32 local sums/counts, a fixed
row-major reduction tree, fixed percentile/EV constants, and an integer frame counter for a
fixed adaptation curve. Exclude non-finite and zero luminance by a specified `epsilon`.
The reduction must not use floating atomics or unordered subgroup reductions. The result is a
single scalar `EV100`; user controls are only `autoExposureEnabled`, `exposureCompensationEV`,
and `pinnedEV100`. A pinned value bypasses metering and adaptation. This keeps the thesis:
the star and transmittance determine scene radiance; exposure merely maps it to a human witness.

## Witness and gate contract

1. **Identity gates:** flag-off must leave the old graph byte-identical. While HDR is enabled,
   capture an HDR canonical hash from `sceneRadianceHistory` after clearing/normalizing unused
   alpha, and capture the final `rgba8` PNG/byte MD5 after a *pinned* EV and fixed tonemap
   constants. Neither hash substitutes for the other.
2. **KI-050 discipline:** acceptance compares each leg against the complete permitted
   two-hash state set, never a convenient one hash. The mip-policy document proves two valid
   nominal boot states and requires byte-level parity (`Deep-Field-Mip-Accessor-Policy-2026-08.md:119-130`);
   wholesale-admission S1 repeats the same stored-control two-hash requirement
   (`Deep-Field-Wholesale-Admission-2026-08.md:159-182`).
3. **Auto-exposure legs:** byte identity is not expected when scene luminance changes. Require
   identical HDR hash and EV for repeated pinned-scene boots; for adaptive runs require a
   fixed EV sequence plus HDR relative-error/SSIM thresholds declared before capture. The
   final 8-bit PNG remains the user-facing gate, but its comparison is against the pinned
   exposure reference.
4. **Quantization witness:** record both HDR max/mean luminance and final-code histogram.
   A change that is below one `rgba8` code but nonzero in HDR is evidence, not a failed
   lighting contribution. The gate explicitly reports it rather than rounding it away.

## Deep-field interaction

The format change does not change `FootprintRegime`, the per-pixel policy stencil, or
wholesale admission. The stencil byte is still proposed in `HitRecord._pad0[2]` bits 8–15
and needs writer preservation (`Deep-Field-Policy-Stencil-Grouping-2026-08.md:169-188`);
HDR images are separate resources. Regime-2/3 must keep the mip pool ready as the wholesale
ladder already requires (`Deep-Field-Wholesale-Admission-2026-08.md:167-175`).

For cosmic/accumulative groups, coverage and transmittance arithmetic remains linear before
exposure. Store group color/transmittance intermediates in fp16 at minimum, promote the
accumulating `C` and `T` temporaries to fp32 in the shader, and emit fp16 scene radiance only
after the final group composite. This prevents a bright solar sample from sharing an 8-bit
step with a faint behind contribution. It neither claims nor implements the missing
multi-layer storage that C3 documents.

## Slice ladder and gates

| Slice | Implementation files | Predeclared gate / Windows measurement |
|---|---|---|
| 1 — HDR seam | `shaders/SpatialReuseShade.comp`; `libraries/RenderGraph/{include/Nodes/AccumulationHistoryNode.h,src/Nodes/AccumulationHistoryNode.cpp}`; new `SceneRadianceNode`; `application/main/source/graph/BuildRenderGraph.cpp`; `shaders/ExposureTonemap.comp` | Flag-off legacy two-hash byte set unchanged; HDR-on with pinned `EV100=0` has stable HDR hash and final PNG hash across N≥3 Windows boots; no validation errors. This is implementation-ready. |
| 2 — deterministic meter | new meter/reduction shader and node/config; graph wiring | Fixed test scene produces identical EV and HDR hash N≥3; motion/reset sequence has predeclared EV frames; measure GPU pass time and image allocation bytes on Windows. |
| 3 — sun/cosmic divergence | `SceneBindings.glsl`, `TraceWorld.glsl`, cosmic composite sites, sun scene fixture | Show a nonzero HDR delta even where final pinned 8-bit delta is zero; then show a visible pinned-exposure witness. Preserve C3 and policy-off two-hash sets. Report fp16 saturation/underflow counters. |
| 4 — quality/perf promotion | gate scripts and performance ledger | For expected non-bit-exact adaptive exposures: HDR relative error ≤ 1e-3 on deterministic fixtures, final pinned-witness SSIM ≥ 0.999 and zero unexpected changed pixels outside the declared ROI. Record 50th/95th GPU ms and total memory on Windows; do not substitute WSL. |

The first slice deliberately does not change radiance math, reservoirs, stencil ownership, or
cosmic policy. It only separates HDR accumulation from the irreversible 8-bit witness, so it
can retain the existing identity contract before real solar flux is introduced.
