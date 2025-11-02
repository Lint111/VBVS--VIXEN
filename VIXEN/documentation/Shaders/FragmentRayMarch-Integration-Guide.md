# Fragment Shader Ray Marching - Integration Guide

**Phase**: J (Fragment Shader Pipeline)
**Purpose**: Second pipeline variant for comparative analysis vs compute shader
**Status**: Preparation complete (shaders validated)
**Date**: November 2, 2025

---

## Overview

This guide documents the **fragment shader ray marching pipeline** for voxel rendering. Unlike the compute shader approach (Phase G), this pipeline uses the traditional rasterization path with a fullscreen triangle.

### Architectural Differences: Fragment vs Compute

| Aspect | Fragment Shader (This) | Compute Shader (Phase G) |
|--------|------------------------|--------------------------|
| **Pipeline Stage** | Rasterization (graphics) | Compute (general-purpose) |
| **Invocation** | Per-pixel (via rasterizer) | Per-workgroup (explicit dispatch) |
| **Parallelism** | Hardware-managed | Software-managed (workgroup size) |
| **Memory Writes** | Framebuffer attachment | Storage image (imageStore) |
| **Render Pass** | Required | Not required |
| **Early-Z Optimization** | Available (depth test) | Not applicable |
| **Workgroup Shared Memory** | Not available | Available (16-32 KB) |
| **Performance Profile** | Better for screen-aligned work | Better for non-screen work |

**Research Value**: Different GPU utilization patterns, memory access patterns, and bandwidth characteristics.

---

## Algorithm Summary

### Ray Marching Core (Identical to Compute)

Both shaders use the **Amanatides & Woo DDA algorithm** for voxel traversal:

1. **Ray Generation**: Unproject screen UV to world space ray
2. **Grid Intersection**: Ray-AABB test with voxel grid bounds
3. **DDA Traversal**: Step through voxels along ray direction
4. **Hit Detection**: Sample 3D texture at voxel position (alpha > 0.5 = solid)
5. **Shading**: Calculate normal from hit position, apply diffuse lighting

**Key Difference**: Fragment shader receives UV from vertex shader (rasterizer), compute shader calculates from `gl_GlobalInvocationID`.

---

## Shader Files

### 1. Vertex Shader: `Shaders/Fullscreen.vert`

**Purpose**: Generate fullscreen triangle covering entire viewport

**Technique**: **Fullscreen Triangle** (not quad)
- Single triangle with vertices at (-1,-1), (3,-1), (-1,3) in NDC
- Covers entire screen with one primitive (GPU-friendly)
- No diagonal seam (quad has 2 triangles = potential seam)
- No vertex buffer needed (vertices hardcoded in shader)

```glsl
#version 460

layout(location = 0) out vec2 fragUV;

const vec2 POSITIONS[3] = vec2[](
    vec2(-1.0, -1.0),  // Bottom-left
    vec2( 3.0, -1.0),  // Bottom-right (off-screen)
    vec2(-1.0,  3.0)   // Top-left (off-screen)
);

const vec2 UVS[3] = vec2[](
    vec2(0.0, 0.0),
    vec2(2.0, 0.0),
    vec2(0.0, 2.0)
);

void main() {
    gl_Position = vec4(POSITIONS[gl_VertexIndex], 0.0, 1.0);
    fragUV = UVS[gl_VertexIndex];
}
```

**Integration Notes**:
- No vertex input bindings required (uses `gl_VertexIndex`)
- Always draw 3 vertices: `vkCmdDraw(cmd, 3, 1, 0, 0)`
- No index buffer needed

---

### 2. Fragment Shader: `Shaders/VoxelRayMarch.frag`

**Purpose**: Ray march through voxel grid, output color to framebuffer

**Inputs**:
- `fragUV` (location 0): UV coordinates from vertex shader [0,1]

**Outputs**:
- `outColor` (location 0): Final pixel color (RGBA)

**Descriptor Sets**:

**Set 0, Binding 0**: Camera uniform buffer
```glsl
layout(set = 0, binding = 0) uniform CameraData {
    mat4 invProjection;   // Inverse projection matrix
    mat4 invView;         // Inverse view matrix
    vec3 cameraPos;       // World-space camera position
    uint gridResolution;  // Voxel grid size (32/64/128/256/512)
} camera;
```

**Set 0, Binding 1**: Voxel data (3D texture)
```glsl
layout(set = 0, binding = 1) uniform sampler3D voxelGrid;
```

**Voxel Data Format**:
- **RGBA8** or **RGBA16F** 3D texture
- **RGB**: Voxel color/albedo
- **Alpha**: Occupancy (>0.5 = solid, <=0.5 = empty)

---

## Integration Steps (Phase J)

### Step 1: Graphics Pipeline Creation

**Key Differences from Compute Pipeline**:
- Requires vertex shader + fragment shader (not single compute shader)
- Requires render pass and framebuffer
- Uses color attachment (not storage image)
- Uses `vkCmdDraw` (not `vkCmdDispatch`)

```cpp
// Shader modules
VkShaderModule vertShader = LoadShader("Fullscreen.vert.spv");
VkShaderModule fragShader = LoadShader("VoxelRayMarch.frag.spv");

VkPipelineShaderStageCreateInfo stages[2] = {
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertShader,
        .pName = "main"
    },
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragShader,
        .pName = "main"
    }
};

// Vertex input state (EMPTY - no vertex buffers)
VkPipelineVertexInputStateCreateInfo vertexInput = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = 0,    // No bindings
    .vertexAttributeDescriptionCount = 0   // No attributes
};

// Input assembly (TRIANGLE_LIST)
VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .primitiveRestartEnable = VK_FALSE
};

// Viewport and scissor (dynamic state recommended)
VkPipelineViewportStateCreateInfo viewportState = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1,
    .scissorCount = 1
};

// Rasterization state
VkPipelineRasterizationStateCreateInfo rasterizer = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_NONE,  // Fullscreen triangle, no culling
    .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    .lineWidth = 1.0f
};

// Multisample state (disabled for baseline)
VkPipelineMultisampleStateCreateInfo multisampling = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable = VK_FALSE
};

// Depth/stencil state (disabled - no depth buffer for fullscreen)
VkPipelineDepthStencilStateCreateInfo depthStencil = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .depthTestEnable = VK_FALSE,
    .depthWriteEnable = VK_FALSE,
    .stencilTestEnable = VK_FALSE
};

// Color blend state (no blending - direct write)
VkPipelineColorBlendAttachmentState colorAttachment = {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
};

VkPipelineColorBlendStateCreateInfo colorBlending = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .logicOpEnable = VK_FALSE,
    .attachmentCount = 1,
    .pAttachments = &colorAttachment
};

// Dynamic state (viewport + scissor)
VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
VkPipelineDynamicStateCreateInfo dynamicState = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = 2,
    .pDynamicStates = dynamicStates
};

// Graphics pipeline
VkGraphicsPipelineCreateInfo pipelineInfo = {
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount = 2,
    .pStages = stages,
    .pVertexInputState = &vertexInput,
    .pInputAssemblyState = &inputAssembly,
    .pViewportState = &viewportState,
    .pRasterizationState = &rasterizer,
    .pMultisampleState = &multisampling,
    .pDepthStencilState = &depthStencil,
    .pColorBlendState = &colorBlending,
    .pDynamicState = &dynamicState,
    .layout = pipelineLayout,
    .renderPass = renderPass,
    .subpass = 0
};

vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
```

---

### Step 2: Render Pass Setup

**Attachment**: Single color attachment (output image)

```cpp
VkAttachmentDescription colorAttachment = {
    .format = VK_FORMAT_R8G8B8A8_UNORM,  // Or swapchain format
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,  // Fullscreen write, no need to clear
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
};

VkAttachmentReference colorRef = {
    .attachment = 0,
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
};

VkSubpassDescription subpass = {
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1,
    .pColorAttachments = &colorRef
};

VkRenderPassCreateInfo renderPassInfo = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &colorAttachment,
    .subpassCount = 1,
    .pSubpasses = &subpass
};

vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);
```

---

### Step 3: Descriptor Set Layout (Same as Compute)

```cpp
VkDescriptorSetLayoutBinding bindings[2] = {
    // Binding 0: Camera uniform buffer
    {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT  // Fragment access only
    },
    // Binding 1: Voxel grid sampler
    {
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    }
};

VkDescriptorSetLayoutCreateInfo layoutInfo = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = 2,
    .pBindings = bindings
};

vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);
```

---

### Step 4: Command Buffer Recording

```cpp
// Begin render pass
VkRenderPassBeginInfo renderPassBegin = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = renderPass,
    .framebuffer = framebuffer,
    .renderArea = {{0, 0}, {width, height}}
};

vkCmdBeginRenderPass(cmd, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

// Bind pipeline
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

// Bind descriptor sets
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                        0, 1, &descriptorSet, 0, nullptr);

// Set dynamic viewport and scissor
VkViewport viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
VkRect2D scissor = {{0, 0}, {width, height}};
vkCmdSetViewport(cmd, 0, 1, &viewport);
vkCmdSetScissor(cmd, 0, 1, &scissor);

// Draw fullscreen triangle (3 vertices, no vertex buffer)
vkCmdDraw(cmd, 3, 1, 0, 0);

vkCmdEndRenderPass(cmd);
```

---

### Step 5: Camera Data Upload (Same as Compute)

```cpp
struct CameraData {
    glm::mat4 invProjection;
    glm::mat4 invView;
    glm::vec3 cameraPos;
    uint32_t gridResolution;
};

// Calculate inverse matrices
glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

CameraData cameraData = {
    .invProjection = glm::inverse(projection),
    .invView = glm::inverse(view),
    .cameraPos = cameraPos,
    .gridResolution = 128  // Match 3D texture dimensions
};

// Upload to uniform buffer
memcpy(uniformBufferMapped, &cameraData, sizeof(CameraData));
```

---

## Performance Profiling Hooks (Phase I Integration)

### Timestamp Queries

```cpp
// Before render pass
vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, 0);

// Begin render pass
vkCmdBeginRenderPass(cmd, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

// ... draw calls ...

vkCmdEndRenderPass(cmd);

// After render pass
vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 1);
```

### Metrics to Collect (Phase I)

For comparison with compute shader:
- **GPU Time**: Fragment shader execution time
- **Bandwidth**: Memory read/write (via VK_KHR_performance_query)
- **Fragment Invocations**: Total pixels processed (via pipeline statistics)
- **Rasterization Efficiency**: Fragments vs pixels (measure overdraw)

**Expected Differences**:
- Fragment shader may have higher bandwidth (framebuffer writes)
- Compute shader may have better cache locality (workgroup shared memory)
- Fragment shader benefits from rasterizer early-Z (if depth buffer used)

---

## Test Configuration Matrix (Phase M)

Fragment shader will be tested across same configurations as compute shader:

| Parameter | Values | Count |
|-----------|--------|-------|
| **Resolution** | 512×512, 1024×1024, 2048×2048, 4096×4096, 8192×8192 | 5 |
| **Voxel Density** | 10%, 50%, 90% | 3 |
| **Algorithm** | Baseline DDA, Empty Skip, BlockWalk | 3 |

**Total**: 5 × 3 × 3 = **45 configurations per pipeline**

**Comparison**: Fragment vs Compute (90 tests total for these two pipelines)

---

## RenderGraph Node Design (Phase J Implementation)

### FragmentRayMarchNode

**Inputs**:
- `CAMERA_BUFFER` (VkBuffer*) - Camera uniform data
- `VOXEL_GRID` (VkImageView*) - 3D texture with voxel data
- `SAMPLER` (VkSampler*) - Texture sampler (linear/nearest)
- `RENDER_PASS` (VkRenderPass*) - Compatible render pass
- `FRAMEBUFFER` (VkFramebuffer*) - Output framebuffer
- `COMMAND_BUFFER` (VkCommandBuffer*) - Recording target

**Outputs**:
- `OUTPUT_IMAGE` (VkImage*) - Rendered result
- `COMPLETION_SEMAPHORE` (VkSemaphore*) - GPU sync primitive

**Node Type**: `GeometryRender` (reuses graphics pipeline infrastructure)

**Lifecycle**:
1. **Compile**: Create graphics pipeline, descriptor sets
2. **Execute**: Record render pass with fullscreen triangle draw
3. **Cleanup**: Destroy pipeline, descriptor sets

---

## Validation Checklist

- [x] `Fullscreen.vert` compiles to valid SPIRV ✅
- [x] `VoxelRayMarch.frag` compiles to valid SPIRV ✅
- [x] Both shaders pass spirv-val ✅
- [x] Descriptor bindings match compute shader (same camera/voxel data) ✅
- [x] DDA algorithm identical to compute version (ensures fair comparison) ✅
- [ ] Integration with RenderGraph (Phase J implementation)
- [ ] Performance profiling (Phase I + J)
- [ ] Comparison with compute shader results (Phase M)

---

## Known Differences vs Compute Shader

### Advantages (Fragment Shader)
1. **Rasterizer hardware**: Automatic viewport transform, interpolation
2. **Render pass integration**: Direct framebuffer output (no image copy)
3. **Early-Z optimization**: Potential for depth-based culling (if depth buffer added)
4. **Industry standard**: Familiar pattern for graphics programmers

### Disadvantages (Fragment Shader)
1. **No workgroup shared memory**: Cannot cache voxel data across threads
2. **Render pass overhead**: Must begin/end render pass (barriers/clears)
3. **Framebuffer attachment**: Output format constrained by render pass
4. **Less flexibility**: Cannot write to arbitrary storage images

### Performance Prediction

**Hypothesis**: Fragment shader will be **comparable or slightly slower** than compute shader for voxel ray marching due to:
- Lack of workgroup shared memory for voxel caching
- Additional render pass overhead
- Framebuffer write penalties

**Research Value**: Quantifying this difference across resolutions/densities is the goal of Phase J.

---

## References

**Shader Files**:
- `Shaders/Fullscreen.vert` - Fullscreen triangle vertex shader
- `Shaders/VoxelRayMarch.frag` - Fragment shader ray marching
- `Shaders/VoxelRayMarch.comp` - Compute shader equivalent (Phase G)

**Documentation**:
- `VoxelRayMarch-Integration-Guide.md` - Compute shader integration (Phase G reference)
- `PerformanceProfilerDesign.md` - Profiling system (Phase I)
- `VoxelRayTracingResearch-TechnicalRoadmap.md` - Full research plan

**Research Papers** (from bibliography):
- [1] Nousiainen - "Performance comparison on rendering methods for voxel data" (baseline comparison)
- [5] Voetter - "Volumetric Ray Tracing with Vulkan" (compute vs fragment discussion)

---

## Next Steps (Phase J Implementation)

After Phase F completion:

1. **Create FragmentRayMarchNode** class (similar to ComputeRayMarchNode)
2. **Implement graphics pipeline creation** (vertex + fragment shaders)
3. **Integrate with RenderGraph** (connect to FramebufferNode, PresentNode)
4. **Add profiling hooks** (timestamp queries, bandwidth counters)
5. **Run benchmark suite** (45 configurations, compare with compute results)
6. **Analyze performance delta** (identify bottlenecks, architectural differences)

**Estimated Time**: 1-2 weeks (Phase J in roadmap)

**Deliverable**: Comparative analysis document with performance charts (fragment vs compute)
