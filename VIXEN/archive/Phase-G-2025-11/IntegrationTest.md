# Render Graph Integration Test

## Overview

This integration test demonstrates the complete render graph system by chaining all 11 nodes together to replicate the VulkanRenderer workflow.

## Complete Node Pipeline

```
┌─────────────────────────────────────────────────────────────────┐
│                    RENDER GRAPH PIPELINE                         │
└─────────────────────────────────────────────────────────────────┘

Phase 1: Resource Management
├─ [SwapChainNode]      → Manages presentation surface
├─ [TextureLoaderNode]  → Loads texture assets
└─ [DepthBufferNode]    → Creates depth/stencil buffer

Phase 2: Pipeline Assembly  
├─ [ShaderNode]         → Loads/compiles vertex & fragment shaders
├─ [RenderPassNode]     → Defines render pass with attachments
└─ [VertexBufferNode]   → Uploads geometry to GPU

Phase 3: Pipeline Creation
├─ [DescriptorSetNode]  → Creates descriptor layouts & sets
└─ [GraphicsPipelineNode] → Assembles complete graphics pipeline

Phase 4: Rendering
├─ [FramebufferNode]    → Creates framebuffers from attachments
├─ [GeometryRenderNode] → Records draw commands
└─ [PresentNode]        → Presents rendered image
```

## Usage Example

```cpp
#include "RenderGraph/RenderGraphIntegrationTest.h"

// Initialize Vulkan resources (device, swapchain, etc.)
VulkanDevice* device = ...;
VulkanSwapChain* swapchain = ...;
uint32_t width = 800;
uint32_t height = 600;

// Create integration test
auto test = std::make_unique<RenderGraphIntegrationTest>(
    device, 
    swapchain, 
    width, 
    height
);

// Build the complete render graph
test->BuildGraph();

// Compile (validate topology, allocate resources)
test->Compile();

// Render loop
while (running) {
    test->RenderFrame();
}

// Cleanup
test->Cleanup();
```

## Graph Construction Steps

### 1. Register Node Types
All 11 node types are registered with the graph's node type registry.

### 2. Create Node Instances
One instance of each node type is created with a unique name:
- `swapchain_main`
- `texture_main`
- `depth_main`
- `shader_main`
- `renderpass_main`
- `vertexbuffer_main`
- `descriptorset_main`
- `pipeline_main`
- `framebuffer_main`
- `render_main`
- `present_main`

### 3. Configure Parameters
Each node is configured with appropriate parameters:

**SwapChainNode:**
- width, height
- Wraps existing VulkanSwapChain

**TextureLoaderNode:**
- filePath: "Assets/textures/sample.png"
- uploadMode: "Optimal"

**DepthBufferNode:**
- width, height
- format: "D32"

**ShaderNode:**
- vertexShaderPath: "Shaders/Draw.vert"
- fragmentShaderPath: "Shaders/Draw.frag"
- autoCompile: true

**RenderPassNode:**
- colorFormat, depthFormat
- loadOp: "Clear", storeOp: "Store"
- layout: "Undefined" → "PresentSrc"

**VertexBufferNode:**
- vertexCount: 36 (cube)
- vertexStride: sizeof(VertexWithUV)
- useTexture: true

**DescriptorSetNode:**
- uniformBufferSize: 256 (MVP matrix)
- useTexture: true

**GraphicsPipelineNode:**
- enableDepthTest/Write: true
- cullMode: "Back"
- topology: "TriangleList"

**FramebufferNode:**
- width, height
- framebufferCount: swapchain image count
- includeDepth: true

**GeometryRenderNode:**
- vertexCount: 36
- clearColor: (0, 0, 0, 1)

**PresentNode:**
- waitForIdle: true

### 4. Establish Connections
Nodes are connected via Set methods to form the complete pipeline:

```cpp
// Pipeline assembly
pipelineNode->SetShaderStages(...);
pipelineNode->SetRenderPass(...);
pipelineNode->SetVertexInput(...);
pipelineNode->SetDescriptorSetLayout(...);

// Framebuffer creation
framebufferNode->SetRenderPass(...);
framebufferNode->SetColorAttachments(...);
framebufferNode->SetDepthAttachment(...);

// Rendering
renderNode->SetPipeline(...);
renderNode->SetFramebuffers(...);
renderNode->SetDescriptorSets(...);
renderNode->SetVertexBuffer(...);

// Presentation
presentNode->SetSwapchain(...);
presentNode->SetQueue(...);
```

## Compilation

The `Compile()` method performs:
1. **Topology Validation** - Checks for cycles, validates connections
2. **Resource Allocation** - Allocates GPU resources for each node
3. **Pipeline Generation** - Creates Vulkan pipelines
4. **Execution Order** - Topologically sorts nodes

## Frame Rendering

Each frame follows this sequence:

1. **Acquire Image** - Get next swapchain image
2. **Record Commands** - Record draw commands to command buffer
3. **Submit** - Submit command buffer with semaphore synchronization
4. **Present** - Queue presentation operation

## Benefits Over Monolithic Renderer

✅ **Composability** - Nodes can be rearranged, added, or removed
✅ **Separation of Concerns** - Each node has one clear responsibility
✅ **Testability** - Individual nodes can be tested in isolation
✅ **Reusability** - Nodes can be shared across different graphs
✅ **Flexibility** - Easy to add post-processing, multi-pass rendering
✅ **Clarity** - Graph structure makes dependencies explicit

## Integration with VulkanRenderer

This test demonstrates **renderer parity** - all capabilities of the monolithic VulkanRenderer are now available through the composable node system. Existing code can gradually migrate to the graph-based approach while maintaining compatibility.

## Next Steps

1. **Add more node types** for advanced features:
   - ComputeShaderNode
   - PostProcessNode
   - ShadowMapNode
   - MultipassNode

2. **Optimize execution** with parallel node execution where possible

3. **Add graph templates** for common rendering patterns

4. **Implement hot-reload** for shaders and textures

5. **Add visual graph editor** for runtime inspection

## Performance Considerations

- **Compilation overhead** - Graph compilation happens once, not per-frame
- **Parameter updates** - Use Set methods to update parameters without recompilation
- **Resource caching** - Pipeline cache reduces recreation cost
- **Command buffer reuse** - Command buffers can be pre-recorded and reused

## Conclusion

This integration test proves the render graph system can fully replicate VulkanRenderer behavior while providing a cleaner, more modular architecture for future expansion.
