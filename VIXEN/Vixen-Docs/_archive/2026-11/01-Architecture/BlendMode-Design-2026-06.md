---
title: BLEND_MODE — alpha blending for graphics pipelines (AR#32)
aliases: [BLEND_MODE, alpha blending, MakeColorBlendAttachment, AR#32 design]
tags: [architecture, design, rendergraph, cashsystem, presentation-layer, AR32]
created: 2026-06-14
status: DONE — implemented + merged to main (2026-06-14)
related:
  - "[[RenderGraph]]"
  - "[[CashSystem]]"
  - "[[Maturation-Backlog-2026-06]]"
---

# BLEND_MODE — alpha blending for graphics pipelines (AR#32)

A `BLEND_MODE` string parameter on `GraphicsPipelineNode`, mirroring the existing `CULL_MODE`
pattern, replacing a hardcoded `blendEnable = VK_FALSE`. Unblocks transparent geometry, UI-over-3D,
and glow/fire effects. Source: AR#32 / Decision #2 in the game-renderer review.

## Problem

`GraphicsPipelineNode` (the sole graph producer of graphics pipelines) built its pipeline **two ways,
both hardcoding no-blend**:

1. **Live path:** `CreatePipelineWithCache` → `PipelineCacher::Create` (`pipeline_cacher.cpp`),
   `colorBlendAttachment.blendEnable = VK_FALSE`.
2. **Fallback** (cacher absent / threw): local `CreatePipeline` → `BuildColorBlendState` →
   `CreateColorBlendAttachment(VK_FALSE)`.

`PipelineCreateParams` doubles as the **pipeline cache key** (`PipelineCacher::ComputeKey`). So a
correct fix must thread blend through *both* code paths *and* the cache key — otherwise two pipelines
differing only in blend mode collide on one cached `VkPipeline` (first-built mode silently wins).

(The RML UI interface, `VixenRmlRenderInterface`, blends via its own independent pipeline and is
unaffected.)

## Design (mirrors CULL_MODE; single source of truth)

1. **Recipe (one place):** `RenderGraph::NodeHelpers::MakeColorBlendAttachment(const std::string&)` in
   `VulkanStructHelpers.h` → a full `VkPipelineColorBlendAttachmentState`. Throws on an unknown mode
   (matches the `Parse*` convention in `EnumParsers.h`). The node computes it **once** in `CompileImpl`
   and stores it in a member; both the cacher params and the manual fallback consume that one value.
2. **Thread through the cacher:** `PipelineCreateParams` (and `PipelineWrapper`, for parity) gain a
   `VkPipelineColorBlendAttachmentState colorBlendAttachment`, **defaulted to opaque / write-RGBA** so
   every existing caller is byte-identical. `pipeline_cacher.cpp` uses `ci.colorBlendAttachment` instead
   of the hardcode. CashSystem only stores a Vulkan struct — no new dependency on RenderGraph.
3. **Cache-key correctness:** `ComputeKey` appends the 8 blend fields
   (`blendEnable` + 6 factor/op fields + `colorWriteMask`), so distinct blend states never collide.
4. **Config:** `GraphicsPipelineNodeConfig::BLEND_MODE = "blendMode"`, default `"None"`.

> **Critical default:** a zero-init `VkPipelineColorBlendAttachmentState{}` has `colorWriteMask = 0`
> (writes nothing). Both new fields and the node member are explicitly initialized to
> `blendEnable = VK_FALSE` + RGBA write mask so "unset" == today's opaque behavior. A unit test guards
> this (`PipelineBlendKey.DefaultParamsAreOpaqueWriteRGBA`).

## Blend modes

| Mode                 | color src / dst                 | alpha src / dst                 | Use |
|----------------------|----------------------------------|----------------------------------|-----|
| `None` (default)     | — (blend disabled)               | —                                | opaque; preserves prior behavior |
| `Alpha`              | `SRC_ALPHA` / `ONE_MINUS_SRC_ALPHA` | `ONE` / `ONE_MINUS_SRC_ALPHA` | standard straight-alpha "over" |
| `PremultipliedAlpha` | `ONE` / `ONE_MINUS_SRC_ALPHA`    | `ONE` / `ONE_MINUS_SRC_ALPHA`    | src already premultiplied by alpha |
| `Additive`           | `SRC_ALPHA` / `ONE`              | `ONE` / `ONE`                    | alpha-weighted additive — glow/fire/particles |
| `Multiply`           | `DST_COLOR` / `ZERO`             | `DST_ALPHA` / `ZERO`             | modulate / darken |

All modes write all color channels; `colorBlendOp` / `alphaBlendOp` are `ADD`.

## Files

- `libraries/RenderGraph/include/NodeHelpers/VulkanStructHelpers.h` — `MakeColorBlendAttachment`.
- `libraries/RenderGraph/include/Data/Nodes/GraphicsPipelineNodeConfig.h` — `BLEND_MODE` const + doc.
- `libraries/RenderGraph/include/Nodes/GraphicsPipelineNode.h` / `src/Nodes/GraphicsPipelineNode.cpp` —
  parse `BLEND_MODE` → `blendAttachment` member → params + fallback.
- `libraries/CashSystem/include/PipelineCacher.h` — blend field on params + wrapper.
- `libraries/CashSystem/src/pipeline_cacher.cpp` — `ComputeKey` + `Create` use the blend field.

## Testing

- `test_blend_mode` (RenderGraph, 8 tests) — each mode's exact factors; unknown throws; all modes write
  RGBA; `BLEND_MODE` constant value. Pure, no device.
- `test_pipeline_blend_key` (CashSystem, 4 tests) — distinct blend → distinct cache key; same blend →
  same key; default params are opaque/write-RGBA.
- Regression: full build green; all CashSystem suites pass; app smoke clean (0 VK_ERROR/VUID,
  ~27k render events in 15s) — opaque path unchanged.

## Out of scope / follow-ups

- Per-attachment / MRT blend (single attachment today, matching the rest of the pipeline).
- `logicOp` blending, dual-source blending, custom blend constants.
- Consolidating the dead duplicate `Data/EnumParsers.h` (live copy is `NodeHelpers/EnumParsers.h`).
