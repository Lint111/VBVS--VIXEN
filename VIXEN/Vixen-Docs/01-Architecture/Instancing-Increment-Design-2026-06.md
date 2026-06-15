---
title: Many-entity draw path — instancing increment (AR#31, increment 1)
aliases: [InstanceBufferNode, instancing, gl_InstanceIndex, AR#31 increment]
tags: [architecture, design, rendergraph, presentation-layer, AR31]
created: 2026-06-14
status: DONE — general Draw.vert/Draw.frag path, 64 textured instanced cubes, 0 VK errors (merged to main 2026-06-15)
related:
  - "[[RenderGraph]]"
  - "[[Maturation-Backlog-2026-06]]"
  - "[[BlendMode-Design-2026-06]]"
---

# Many-entity draw path — instancing increment (AR#31, increment 1)

The first real step toward many-entity rendering: one mesh, **N instances** via hardware instancing,
each with its own transform from an SSBO indexed by `gl_InstanceIndex`. Rendered in an **isolated
env-var demo graph** so the live voxel-compute path is untouched. Source: AR#31, scope chosen
"instancing increment first" (2026-06-14).

## Context (what exists today)

- **Raster path is dormant.** The live app runs **compute-only** (voxel ray-marching); the entire
  graphics-pipeline branch in `VulkanGraphApplication.cpp` is disabled inside `/* */` blocks
  (`BuildRenderGraph`, ~lines 640–657 nodes, 698–756 params). The voxel path writes the swapchain.
- **One mesh, hardcoded.** `VertexBufferNode` uploads a static 36-vertex cube (`Core/MeshData.h`).
- **`GeometryRenderNode` already supports instancing** — `RecordDrawCall` issues
  `vkCmdDraw(vertexCount, instanceCount, …)` (`GeometryRenderNode.cpp:514-520`); `INSTANCE_COUNT` is a
  parameter. The draw primitive is done; what's missing is the per-instance data + a shader that reads it.
- **`Draw.vert`** uses a single-MVP UBO (`mat4 mvp`, binding 0) — no per-instance data.
- **Isolated alternate graphs already exist:** `VIXEN_UI_DEMO` env var → `BuildUIGraph()`
  (`VulkanGraphApplication.cpp:526, 617-622`) builds a separate graph leaving voxel untouched. This is
  the re-light pattern to mirror.
- Wiring uses the `ConnectionBatch` API (`batch.Connect(src, OUT, dst, IN)`).

## Decisions

| # | Decision | Choice |
|---|----------|--------|
| Scope | how much to build | **Instancing increment**: one mesh, N instances, re-lit just enough to see N cubes. Defer multi-mesh draw-lists + `vkCmdDrawIndirect`. |
| Data path | per-instance transforms | **SSBO indexed by `gl_InstanceIndex`** (not instance vertex attributes) — the path GPU-driven culling/indirect later builds on. |
| Source | where transforms come from | **New `InstanceBufferNode`** — a dedicated producer node (the reusable seam for future per-frame transforms/culling), not inline. |
| Re-light | how to render | **`BuildInstancingDemoGraph()`** gated by `VIXEN_INSTANCING_DEMO`, mirroring `BuildUIGraph` — voxel path untouched. |

## Design

### 1. `InstanceBufferNode` (new producer node)

A `TypedNode<InstanceBufferNodeConfig>` that allocates a storage buffer of N `mat4` model matrices and
outputs it for the descriptor set. Mirror the **`RenderTargetNode`** producer pattern (AR#28:
`libraries/RenderGraph/{include/Data/Nodes/RenderTargetNodeConfig.h, include/Nodes/RenderTargetNode.h,
src/Nodes/RenderTargetNode.cpp}`) for structure, FR-7 lifecycle (`ctx.reason`), and registration.

- **Inputs:** `VULKAN_DEVICE (VulkanDevice*)`.
- **Outputs:** `INSTANCE_BUFFER (VkBuffer)`; `INSTANCE_COUNT (uint32_t)`.
- **Parameters:** `GRID_DIM (uint32_t, default 8)` → N = GRID_DIM³ (or GRID_DIM² for a planar grid —
  implementer picks the simplest visible layout); `SPACING (float, default 2.0)`.
- **Allocation:** one `VkBuffer` with `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`, host-visible+coherent (simplest
  for a static upload), sized `N * sizeof(mat4)`. Use the real memory-type selection (mirror
  `RenderTargetNode::CreateTarget` / `NodeHelpers` `FindMemoryType` — **not** a `memoryTypeIndex=0`
  placeholder). Fill with a grid of translation matrices (`glm::translate`), map/memcpy/unmap once at
  compile.
- **Lifecycle (FR-7):** create once; persist across recompile; free only on `FinalTeardown`.

### 2. Shader (`shaders/Draw.vert` + the `VixenBenchmark/shaders/Draw.vert` copy)

Add the instance SSBO and apply the per-instance model:

```glsl
#version 450
layout (std140, binding = 0) uniform bufferVals { mat4 mvp; } myBufferVals;          // proj*view
layout (std430, binding = 1) readonly buffer Instances { mat4 model[]; } instances;   // AR#31

layout(location = 0) in vec4 pos;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec2 outUV;

void main() {
    outUV = inUV;
    gl_Position = myBufferVals.mvp * instances.model[gl_InstanceIndex] * pos;
    gl_Position.y = -gl_Position.y;
    gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
}
```

`Draw.frag` unchanged. (Whoever fills the `mvp` UBO in the demo graph supplies **proj·view**, since the
model is now per-instance. For the increment the existing camera/MVP source is acceptable as long as the
cubes are visible; correctness of the exact proj·view split is an implementation detail of the demo graph.)

> **Implementation note (as-built, 2026-06-15 — general path):** The demo now drives the **general
> `Draw.vert` + `Draw.frag`** path (the throwaway `InstancingDemo.*` shaders were deleted). The two
> original blockers were fixed:
> - **Binding collision** → instance SSBO moved to **binding 2** (0 = MVP UBO, 1 = `Draw.frag`'s
>   `sampler2D`, 2 = instance transforms). No cross-stage collision.
> - **Missing MVP-UBO producer** → new **`MvpUniformNode`** (proj·view UBO at binding 0).
>
> Two adjacent subsystems turned out to be **stubs/incomplete** and were implemented as root-cause fixes
> (not band-aids):
> - **`CashSystem::TextureCacher` was a stub** (`CreateFallbackTexture` made a 1×1 CPU pixel, created no
>   VkImage) — so `TextureLoaderNode` was non-functional for *any* texture, even a valid file. Now it does
>   real staging upload (`TextureLoader::LoadFromMemory` raw-bytes path + a transient command pool), and
>   generates a default checkerboard when `FILE_PATH` is empty. This also fixes the file path.
> - **The combined-image-sampler descriptor path was incomplete** — a `sampler2D` needs view+sampler in
>   one binding, but two connections to one gatherer binding collapse (last-writer-wins). Added a
>   `TextureLoaderNode TEXTURE_SAMPLER_PAIR` (`ImageSamplerPair`) output so one connection satisfies the
>   binding, taught the gatherer/`CanBePersistent` to accept it, and fixed `Resource::GetDescriptorHandle`
>   to prefer a non-null `ImageSamplerPair` before the lone `VkImageView` (guarded so plain image-view
>   bindings — e.g. the voxel storage image — are unaffected; verified regression-clean).

### 3. Descriptor wiring (as-built)

Three reflection-driven descriptors on the general shaders, all via `DescriptorResourceGathererNode` →
`DescriptorSetNode`:
- **binding 0** — MVP uniform buffer ← `MvpUniformNode.MVP_BUFFER`
- **binding 1** — combined image sampler ← `TextureLoaderNode.TEXTURE_SAMPLER_PAIR` (one
  `ImageSamplerPair` handle satisfies the view+sampler binding)
- **binding 2** — instance transforms SSBO ← `InstanceBufferNode.INSTANCE_BUFFER`

Each connected with `SlotRole::Dependency | SlotRole::Execute`. This was the main integration risk and
required completing the combined-image-sampler path (see the implementation note above).

### 4. `GeometryRenderNode`

No draw-loop change needed: set `INSTANCE_COUNT = N` (from `InstanceBufferNode`'s `INSTANCE_COUNT`
output, or the demo graph sets the param). The SSBO arrives via the descriptor set.

### 5. Re-light: `BuildInstancingDemoGraph()`

New method gated by `getenv("VIXEN_INSTANCING_DEMO")` at the top of `BuildRenderGraph()` (beside the
`VIXEN_UI_DEMO` check). Mirror `BuildUIGraph()` structure. Wire the raster chain using the disabled
blocks + the UI graph as templates:

`Instance→Device→Window→SwapChain→CommandPool`, `RenderPass`, `Framebuffer`, `DepthBuffer`,
`VertexBuffer` (cube), `InstanceBufferNode`, `ShaderLibrary` (Draw.vert/frag), `DescriptorResourceGatherer`,
`DescriptorSet`, `GraphicsPipeline`, `FrameSync`, `GeometryRender`, `Present`. Set
`GeometryRenderNode INSTANCE_COUNT = N`.

## Testing

- **Unit:** `InstanceBufferNode` config (slots/params/defaults) + the transform-grid generation (N =
  GRID_DIM^k; matrices are the expected translations) — pure where possible, mirroring
  `test_render_target_node.cpp`.
- **App smoke:** run with `VIXEN_INSTANCING_DEMO=1` → 0 VK_ERROR/VUID; draw recorded with
  `instanceCount == N (>1)`; render activity > 0. (Assert via logs.)
- **Regression:** default run (no env var) is byte-identical voxel-compute behavior.

## Out of scope (later AR#31 increments)

- Heterogeneous multi-mesh **draw lists** and `vkCmdDrawIndirect` (GPU-driven).
- GPU culling; per-frame dynamic transforms (those build on this SSBO seam).
- Replacing the live voxel path with raster (this stays an isolated demo graph).
