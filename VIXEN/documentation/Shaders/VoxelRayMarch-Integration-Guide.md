# VoxelRayMarch.comp - Integration Guide

**Created**: November 2, 2025
**Shader**: `Shaders/VoxelRayMarch.comp`
**SPIRV**: `Shaders/VoxelRayMarch.comp.spv`
**Status**: Phase G.3 Complete - Ready for Integration

---

## Overview

`VoxelRayMarch.comp` is a compute shader implementing DDA (Digital Differential Analyzer) voxel ray marching for rendering volumetric voxel data. This shader serves as the **baseline implementation** for research comparing different ray tracing/marching pipeline architectures.

**Research Context**:
- **Baseline Algorithm**: Naive DDA traversal for performance comparison
- **Paper References**: [1] Nousiainen, [5] Voetter
- **Optimization Target**: BlockWalk algorithm (Phase L) - [16] Derin et al.

**Purpose**:
- Phase G: Compute shader pipeline implementation
- Phase L: Baseline for optimization comparisons
- Research: Compute vs Fragment vs Hardware RT performance analysis

---

## Algorithm Summary

### DDA Voxel Traversal

**What it does**: Efficiently traces rays through a voxel grid by stepping from voxel to voxel.

**Key Concept**: Instead of checking every point along the ray, the algorithm only checks **voxel boundaries**.

**Variables**:
- `tMax`: Parametric distance to next voxel boundary (per axis)
- `tDelta`: Distance between voxel boundaries (per axis)
- `step`: Direction to step (+1 or -1 per axis)

**Stepping Logic**:
1. Find the axis with smallest `tMax` (closest boundary)
2. Step along that axis
3. Increment `tMax` by `tDelta` (next boundary is one voxel further)
4. Repeat until hit or max steps

**Reference**: "A Fast Voxel Traversal Algorithm" (Amanatides & Woo, 1987)

---

## Shader Interface

### Includes

The shader uses shared type definitions:
```glsl
#include "SVOTypes.glsl"  // ChildDescriptor helpers, Material struct, octant mirroring
```

### Descriptor Set Layout

**All bindings use implicit set = 0** (single descriptor set):

```glsl
// Binding 0: Output image (storage image, writeonly)
layout(binding = 0) uniform writeonly image2D outputImage;

// Binding 1: ESVO octree buffer (64-bit nodes = uvec2)
layout(std430, binding = 1) readonly buffer ESVOBuffer {
    uvec2 esvoNodes[];  // ChildDescriptor format from SVOTypes.glsl
};

// Binding 2: Brick data buffer (8x8x8 voxels per brick)
layout(std430, binding = 2) readonly buffer BrickBuffer {
    uint brickData[];
};

// Binding 3: Material buffer (Material struct from SVOTypes.glsl)
layout(std430, binding = 3) readonly buffer MaterialBuffer {
    Material materials[];
};

// Binding 4: Ray trace debug buffer (optional, for debugging)
layout(std430, binding = 4) buffer RayTraceBuffer {
    uint traceWriteIndex;
    uint traceCapacity;
    uint _padding[2];
    uint traceData[];  // TraceStep records
};

// Binding 5: Octree configuration UBO (runtime parameters from CPU)
layout(std140, binding = 5) uniform OctreeConfigUBO {
    int esvoMaxScale;       // Always 22 (ESVO normalized space)
    int userMaxLevels;      // log2(resolution) = 7 for 128^3
    int brickDepthLevels;   // 3 for 8^3 bricks
    int brickSize;          // 8 (voxels per brick axis)
    int minESVOScale;       // esvoMaxScale - userMaxLevels + 1 = 16
    int brickESVOScale;     // Scale at which nodes are brick parents = 20
    int bricksPerAxis;      // resolution / brickSize = 16
    int _padding1;
    vec3 gridMin;
    float _padding2;
    vec3 gridMax;
    float _padding3;
    mat4 localToWorld;      // Grid Local [0,1] -> World Space
    mat4 worldToLocal;      // World Space -> Grid Local [0,1]
    float _padding4[16];    // Pad to 256 bytes
} octreeConfig;
```

**Note**: Binding 6 (brickBaseIndex) was REMOVED - brick indices are now read directly from leaf descriptors via `getBrickIndex()` in SVOTypes.glsl.

### Push Constants (Camera)

```glsl
layout(push_constant) uniform PushConstants {
    vec3 cameraPos;
    float time;
    vec3 cameraDir;
    float fov;
    vec3 cameraUp;
    float aspect;
    vec3 cameraRight;
    int debugMode;  // 0=normal, 1-9=debug visualizations
} pc;
```

### Memory Layout (Binding 5 - OctreeConfigUBO)

**OctreeConfigUBO** - 256 bytes total (std140 layout):
```cpp
struct OctreeConfig {
    int32_t esvoMaxScale;       // Offset 0
    int32_t userMaxLevels;      // Offset 4
    int32_t brickDepthLevels;   // Offset 8
    int32_t brickSize;          // Offset 12
    int32_t minESVOScale;       // Offset 16
    int32_t brickESVOScale;     // Offset 20
    int32_t bricksPerAxis;      // Offset 24
    int32_t _padding1;          // Offset 28
    glm::vec3 gridMin;          // Offset 32 (vec3 = 12 bytes)
    float _padding2;            // Offset 44
    glm::vec3 gridMax;          // Offset 48
    float _padding3;            // Offset 60
    glm::mat4 localToWorld;     // Offset 64 (mat4 = 64 bytes)
    glm::mat4 worldToLocal;     // Offset 128
    float _padding4[16];        // Offset 192 (64 bytes)
    // Total: 256 bytes
};
```

**Alignment Rules (std140)**:
- `int`: 4-byte alignment
- `vec3`: 16-byte alignment (rounds up to vec4)
- `mat4`: 16-byte alignment per column
- Total size padded to 256 bytes for GPU alignment

### Workgroup Configuration

```glsl
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
```

**Dispatch Dimensions**:
```cpp
uint32_t groupCountX = (screenWidth + 7) / 8;   // Ceiling division
uint32_t groupCountY = (screenHeight + 7) / 8;
vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
```

**Example** (1920×1080):
- Groups X: (1920 + 7) / 8 = 240
- Groups Y: (1080 + 7) / 8 = 135
- Total workgroups: 240 × 135 = 32,400
- Total threads: 32,400 × 64 = 2,073,600 (one per pixel)

---

## Integration with ComputePipelineNode (Phase G.1)

### Step 1: Create Compute Pipeline

```cpp
// ComputePipelineNode::CompileImpl()

// 1. Load shader module
VkShaderModule shaderModule = shaderModuleCacher->GetOrCreate("VoxelRayMarch.comp.spv");

// 2. Create pipeline layout (descriptor set layout + push constants)
VkPipelineLayoutCreateInfo layoutInfo = {
    .setLayoutCount = 1,
    .pSetLayouts = &descriptorSetLayout,  // Auto-generated from SPIRV-Reflect
    .pushConstantRangeCount = 0
};
vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);

// 3. Create compute pipeline
VkComputePipelineCreateInfo pipelineInfo = {
    .stage = {
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = shaderModule,
        .pName = "main"
    },
    .layout = pipelineLayout
};
vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
```

### Step 2: Create Descriptor Sets

```cpp
// VoxelGridNode::CompileImpl()

// Allocate descriptor set
VkDescriptorSetAllocateInfo allocInfo = {
    .descriptorSetCount = 1,
    .pSetLayouts = &descriptorSetLayout
};
vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);

// Update descriptors (6 bindings total)
VkDescriptorImageInfo outputImageInfo = {
    .imageView = outputImageView,
    .imageLayout = VK_IMAGE_LAYOUT_GENERAL
};

VkDescriptorBufferInfo esvoBufferInfo = {
    .buffer = esvoNodesBuffer,
    .offset = 0,
    .range = VK_WHOLE_SIZE
};

VkDescriptorBufferInfo brickBufferInfo = {
    .buffer = brickDataBuffer,
    .offset = 0,
    .range = VK_WHOLE_SIZE
};

VkDescriptorBufferInfo materialBufferInfo = {
    .buffer = materialBuffer,
    .offset = 0,
    .range = VK_WHOLE_SIZE
};

VkDescriptorBufferInfo traceBufferInfo = {
    .buffer = rayTraceBuffer,
    .offset = 0,
    .range = VK_WHOLE_SIZE
};

VkDescriptorBufferInfo configUboInfo = {
    .buffer = octreeConfigBuffer,
    .offset = 0,
    .range = sizeof(OctreeConfig)  // 256 bytes
};

VkWriteDescriptorSet writes[6] = {
    { // Binding 0: Output image
        .dstSet = descriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &outputImageInfo
    },
    { // Binding 1: ESVO nodes
        .dstSet = descriptorSet,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &esvoBufferInfo
    },
    { // Binding 2: Brick data
        .dstSet = descriptorSet,
        .dstBinding = 2,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &brickBufferInfo
    },
    { // Binding 3: Materials
        .dstSet = descriptorSet,
        .dstBinding = 3,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &materialBufferInfo
    },
    { // Binding 4: Ray trace debug buffer
        .dstSet = descriptorSet,
        .dstBinding = 4,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &traceBufferInfo
    },
    { // Binding 5: Octree config UBO
        .dstSet = descriptorSet,
        .dstBinding = 5,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &configUboInfo
    }
};

vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);
```

### Step 3: Record Dispatch Commands

```cpp
// ComputeDispatchNode::ExecuteImpl()

vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

// Dispatch workgroups
uint32_t groupCountX = (screenWidth + 7) / 8;
uint32_t groupCountY = (screenHeight + 7) / 8;
vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
```

### Step 4: Synchronization

```cpp
// After dispatch, before reading outputImage

VkImageMemoryBarrier barrier = {
    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,  // Or VK_ACCESS_TRANSFER_READ_BIT
    .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
    .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    .image = outputImage,
    .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1
    }
};

vkCmdPipelineBarrier(
    cmd,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,      // srcStage
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,     // dstStage (if presenting)
    0,
    0, nullptr,
    0, nullptr,
    1, &barrier
);
```

---

## RenderGraph Node Structure (Phase G)

### Recommended Node Architecture

**Option 1: Single ComputeRayMarchNode** (Simpler)
```
ComputeRayMarchNode
├─ Inputs:
│  ├─ DEVICE (VkDevice)
│  ├─ CAMERA_BUFFER (VkBuffer - CameraData UBO)
│  ├─ VOXEL_GRID (VkImageView - 3D texture)
│  └─ VOXEL_SAMPLER (VkSampler)
├─ Outputs:
│  └─ OUTPUT_IMAGE (VkImageView - rgba8)
└─ Internal:
   ├─ VkPipeline (compute)
   ├─ VkPipelineLayout
   └─ VkDescriptorSet
```

**Option 2: Modular Nodes** (More flexible)
```
ComputePipelineNode → Creates VkPipeline from shader
      ↓
ComputeDispatchNode → Binds descriptors, dispatches workgroups
```

**Recommendation**: Option 1 for initial implementation (Phase G), Option 2 when generalizing (Phase L).

---

## Camera Data Setup

### Projection Matrix

```cpp
// In CameraNode or similar
glm::mat4 projection = glm::perspective(
    glm::radians(45.0f),          // FOV
    (float)width / (float)height, // Aspect ratio
    0.1f,                         // Near plane
    100.0f                        // Far plane
);

glm::mat4 invProjection = glm::inverse(projection);
```

### View Matrix

```cpp
glm::mat4 view = glm::lookAt(
    cameraPos,           // Camera position (eye)
    cameraPos + forward, // Look-at target
    glm::vec3(0, 1, 0)   // Up vector
);

glm::mat4 invView = glm::inverse(view);
```

### Update UBO

```cpp
CameraData cameraData = {
    .invProjection = invProjection,
    .invView = invView,
    .cameraPos = cameraPos,
    .gridResolution = 128  // Match voxel grid size
};

// Map and update uniform buffer
void* data;
vkMapMemory(device, cameraBufferMemory, 0, sizeof(CameraData), 0, &data);
memcpy(data, &cameraData, sizeof(CameraData));
vkUnmapMemory(device, cameraBufferMemory);
```

---

## Voxel Grid Setup (Phase H)

### Option A: 3D Texture (Current)

```cpp
// Create 3D texture (128³ example)
VkImageCreateInfo imageInfo = {
    .imageType = VK_IMAGE_TYPE_3D,
    .format = VK_FORMAT_R8_UNORM,  // 8-bit grayscale
    .extent = { 128, 128, 128 },
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
};

vkCreateImage(device, &imageInfo, nullptr, &voxelGridImage);

// Create sampler (linear filtering)
VkSamplerCreateInfo samplerInfo = {
    .magFilter = VK_FILTER_LINEAR,
    .minFilter = VK_FILTER_LINEAR,
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
};

vkCreateSampler(device, &samplerInfo, nullptr, &voxelSampler);
```

### Option B: Storage Buffer (Phase H - Octree)

**Shader modification required**:
```glsl
// Replace binding 2
layout(set = 0, binding = 2) buffer VoxelBuffer {
    uint octreeNodes[];  // Linearized octree
} voxelBuffer;
```

**Traversal changes**: See Phase H octree design document.

---

## Output Image Setup

### Create Storage Image

```cpp
VkImageCreateInfo imageInfo = {
    .imageType = VK_IMAGE_TYPE_2D,
    .format = VK_FORMAT_R8G8B8A8_UNORM,
    .extent = { screenWidth, screenHeight, 1 },
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
};

vkCreateImage(device, &imageInfo, nullptr, &outputImage);

// Transition to GENERAL layout for compute shader writes
VkImageMemoryBarrier barrier = {
    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
    .image = outputImage,
    // ... subresourceRange
};

vkCmdPipelineBarrier(cmd,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0, 0, nullptr, 0, nullptr, 1, &barrier);
```

### Display Output

**Option 1: Full-screen quad** (copy to swapchain)
```glsl
// Fragment shader
layout(set = 0, binding = 0) uniform sampler2D computeOutput;
out vec4 fragColor;
void main() {
    fragColor = texture(computeOutput, inUV);
}
```

**Option 2: Direct blit** (VkImage → swapchain)
```cpp
VkImageBlit blitRegion = {
    .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
    .srcOffsets = { {0,0,0}, {width, height, 1} },
    .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
    .dstOffsets = { {0,0,0}, {width, height, 1} }
};

vkCmdBlitImage(cmd, outputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               1, &blitRegion, VK_FILTER_NEAREST);
```

---

## Performance Profiling Integration (Phase I)

### Timestamp Queries

```cpp
// Before dispatch
vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, 0);

vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

// After dispatch
vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, 1);

// Later: Read back results
uint64_t timestamps[2];
vkGetQueryPoolResults(device, queryPool, 0, 2, sizeof(timestamps), timestamps,
                      sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

float gpuTimeMs = (timestamps[1] - timestamps[0]) * timestampPeriod / 1e6f;
```

### Bandwidth Calculation

**Theoretical read bandwidth**:
```
Voxels sampled per ray = avg_steps (e.g., 100)
Total rays = screen_width × screen_height
Bytes per sample = 1 byte (R8_UNORM)

Total bytes read = rays × avg_steps × 1
Bandwidth (GB/s) = total_bytes / gpu_time_seconds / 1e9
```

**Actual measurement**: Use VK_KHR_performance_query (Phase I).

---

## Testing & Validation Checklist

### Phase G Testing

- [ ] Shader compiles without errors (`glslangValidator`)
- [ ] SPIRV validation passes (`spirv-val`)
- [ ] Descriptor layout extracted correctly (`spirv-reflect`)
- [ ] ComputePipelineNode creates pipeline successfully
- [ ] Dispatch executes without validation errors
- [ ] Output image shows rendered voxels (visual test)
- [ ] Camera movement updates view correctly
- [ ] Different voxel resolutions work (32³, 64³, 128³)

### Phase I Testing (Performance)

- [ ] Timestamp queries return reasonable values (<50ms)
- [ ] Frame time matches expected performance
- [ ] Bandwidth measurements collected
- [ ] CSV export works for 300+ frames
- [ ] Metrics align with manual calculations

### Phase L Testing (Optimization)

- [ ] BlockWalk shows measurable performance improvement
- [ ] Empty space skipping reduces bandwidth
- [ ] Optimization metrics match research expectations

---

## Known Limitations & Future Work

### Current Limitations

1. **Fixed MAX_STEPS = 512**: May miss distant voxels in large grids
   - **Solution**: Make configurable via push constants (Phase G.1)

2. **No early ray termination optimizations**: Traverses full path even if occluded
   - **Solution**: Add empty space skipping (Phase L.1)

3. **Simple grayscale voxels**: No color/material attributes
   - **Solution**: Extend to RGB + material ID in Phase H

4. **3D texture sampling**: Memory inefficient for sparse data
   - **Solution**: Octree buffer traversal (Phase H)

5. **Naive DDA traversal**: Not optimal for coherence
   - **Solution**: BlockWalk algorithm (Phase L.2)

### Future Enhancements (Phase L)

**L.1: Empty Space Skipping**
```glsl
// Add coarse occupancy grid (16³ blocks)
layout(set = 0, binding = 3) buffer BlockOccupancy {
    uint blocks[];  // Bit per 16³ block
};

// Skip empty blocks during traversal
if (blockIndex < blockCount && !isBlockOccupied(blockIndex)) {
    skipDistance = calculateBlockSkipDistance();
    voxelPos += skipDirection * skipDistance;
}
```

**L.2: BlockWalk Traversal** (Paper [16])
```glsl
// Group voxels into coherent blocks
// Traverse blocks first, then voxels within blocks
// Reduces divergence, improves memory coherence
```

**L.3: Adaptive Sampling**
```glsl
// Increase step size for distant voxels (LOD)
float distanceToCamera = length(rayOrigin + t * rayDir - cameraPos);
float adaptiveStep = mix(1.0, 4.0, smoothstep(10.0, 100.0, distanceToCamera));
```

---

## Research Integration Notes

### Test Configurations

**Resolution Variants**:
- 32³: groupCount = (1920/8) × (1080/8) = 32,400 workgroups
- 64³: Same dispatch, different `gridResolution` uniform
- 128³: Same dispatch, larger voxel texture
- 256³: Same dispatch, may hit VRAM limits (test Phase H octree)
- 512³: Requires octree (3D texture = 512³ × 1 byte = 134 MB)

**Density Variants** (Phase H):
- Sparse (10-30%): Generate procedural scenes with empty space
- Medium (40-60%): Mixed solid/empty regions
- Dense (70-90%): Mostly filled voxel grid

**Algorithm Variants** (Phase L):
- Baseline: Current DDA implementation
- Empty-skip: Block occupancy grid optimization
- BlockWalk: Coherent block traversal

### Comparison Points

**Compute vs Fragment** (Phase J):
- Same ray generation logic
- Fragment shader samples 3D texture instead of compute dispatch
- Compare bandwidth, frame time, occupancy

**Compute vs Hardware RT** (Phase K):
- Compute: Manual DDA traversal
- RT: BLAS/TLAS with custom AABB intersection
- Compare build time, trace time, memory usage

---

## Quick Reference: File Locations

**Shader Source**:
- `Shaders/VoxelRayMarch.comp` - GLSL source
- `Shaders/VoxelRayMarch.comp.spv` - Compiled SPIRV

**Node Implementations** (Phase G):
- `RenderGraph/include/Nodes/ComputePipelineNodeConfig.h`
- `RenderGraph/src/Nodes/ComputePipelineNode.cpp`
- `RenderGraph/include/Nodes/ComputeDispatchNodeConfig.h`
- `RenderGraph/src/Nodes/ComputeDispatchNode.cpp`

**Documentation**:
- `documentation/VoxelRayTracingResearch-TechnicalRoadmap.md` - Full research plan
- `documentation/ResearchPhases-ParallelTrack.md` - Week 1-3 parallel tasks
- `documentation/Shaders/VoxelRayMarch-Integration-Guide.md` - This document

**Research Papers**:
- Paper [1]: Nousiainen - Performance comparison baseline
- Paper [5]: Voetter - Volumetric ray tracing with Vulkan
- Paper [16]: Derin et al. - BlockWalk optimization target

---

## Example: Complete Integration (Pseudocode)

```cpp
// Phase G.1: Minimal working example

// 1. Setup (once)
VkShaderModule shader = LoadShader("VoxelRayMarch.comp.spv");
VkDescriptorSetLayout descLayout = ExtractDescriptorLayout(shader);
VkPipelineLayout pipelineLayout = CreatePipelineLayout(descLayout);
VkPipeline pipeline = CreateComputePipeline(shader, pipelineLayout);

VkImage outputImage = CreateStorageImage(1920, 1080, VK_FORMAT_R8G8B8A8_UNORM);
VkImage voxelGrid = Load3DTexture("voxel_128.raw", 128, 128, 128);
VkBuffer cameraUBO = CreateUniformBuffer(sizeof(CameraData));

VkDescriptorSet descSet = AllocateDescriptorSet(descLayout);
UpdateDescriptors(descSet, outputImage, cameraUBO, voxelGrid);

// 2. Per-frame update
CameraData camera = {
    .invProjection = glm::inverse(projMatrix),
    .invView = glm::inverse(viewMatrix),
    .cameraPos = cameraPosition,
    .gridResolution = 128
};
UpdateBuffer(cameraUBO, &camera);

// 3. Render
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSet, 0, nullptr);
vkCmdDispatch(cmd, 240, 135, 1);  // (1920/8) × (1080/8)

// 4. Display
BlitImageToSwapchain(outputImage, swapchainImage);
```

---

## Contact & Maintenance

**Created By**: Agent 2 (Research Thread)
**Date**: November 2, 2025
**Phase**: G.3 (Compute Shader Preparation)
**Status**: Complete, ready for Phase G.1 integration

**Next Steps**:
1. Implement ComputePipelineNode (Phase G.1)
2. Implement storage resource types (Phase G.2)
3. Integrate this shader into node (Phase G.3 integration)
4. Add timestamp queries (Phase G.4)

**Questions/Issues**: See `VoxelRayTracingResearch-TechnicalRoadmap.md` for full context.
