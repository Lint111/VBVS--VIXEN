# Render Graph Node Architecture - Renderer Parity Implementation

## Goal
Replicate current VulkanRenderer functionality through composable, single-responsibility nodes.

---

## Current Renderer Analysis

### Initialization Sequence (from VulkanRenderer::Initialize)
1. **CreatePresentationWindow** - Window management
2. **SwapChain::Initialize** - Surface + swapchain creation
3. **CreateCommandPool** - Command buffer management
4. **CreateDepthImage** - Depth buffer allocation ✅ DONE
5. **CreateVertexBuffer** - Geometry upload
6. **CreateRenderPass** - Render pass definition
7. **CreateFrameBuffer** - Framebuffer assembly
8. **CreateShaders** - Shader loading/compilation
9. **TextureLoader::Load** - Texture loading ✅ DONE
10. **CreateDescriptors** - Descriptor sets for uniforms/textures
11. **CreatePipelineStateManagement** - Graphics pipeline assembly

### Rendering Loop (from VulkanRenderer::Render)
1. **SwapChain::AcquireNextImage** - Get next swapchain image
2. **Drawable::Render** - Record draw commands
3. **SwapChain::Present** - Present to screen

---

## Node Design Principles

###

 1. **Single Responsibility**
Each node does ONE thing well.

### 2. **Clear Inputs/Outputs**
Explicit dependencies through resource connections.

### 3. **Stateless Execution**
Nodes don't store frame state - only configuration.

### 4. **Lifecycle Separation**
- **Setup**: One-time initialization
- **Compile**: Resource creation (on graph compile/resize)
- **Execute**: Per-frame work (command recording)
- **Cleanup**: Resource destruction

---

## Node Catalog

### ✅ IMPLEMENTED

#### 1. TextureLoaderNode
- **Type ID**: 100
- **Inputs**: None
- **Outputs**: Texture image (SHADER_READ_ONLY_OPTIMAL)
- **Parameters**: filePath, uploadMode, generateMipmaps
- **Responsibility**: Load texture from disk, upload to GPU
- **Lifecycle**: Load in Compile, no Execute work

#### 2. DepthBufferNode
- **Type ID**: 101
- **Inputs**: None
- **Outputs**: Depth image (DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
- **Parameters**: width, height, format (D32/D24S8/D16)
- **Responsibility**: Create depth/stencil buffer
- **Lifecycle**: Create in Compile, no Execute work

---

### 🔨 TO IMPLEMENT

#### 3. SwapChainNode
- **Type ID**: 102
- **Pipeline Type**: Transfer
- **Inputs**: None (manages OS surface internally)
- **Outputs**:
  - [0] Current swapchain image (COLOR_ATTACHMENT_OPTIMAL)
  - [1] Image available semaphore
  - [2] Render finished semaphore
- **Parameters**:
  - width: uint32_t
  - height: uint32_t
  - vsync: bool
  - preferredFormat: std::string ("BGRA8_SRGB", etc.)
- **Responsibility**:
  - Create/manage VkSwapchainKHR
  - Acquire next image (Execute phase)
  - Handle resize events
  - Manage presentation semaphores
- **Special**: Persistent across frames, stateful

#### 4. VertexBufferNode
- **Type ID**: 103
- **Pipeline Type**: Transfer
- **Inputs**: None
- **Outputs**:
  - [0] Vertex buffer
  - [1] Index buffer (optional)
- **Parameters**:
  - meshData: custom type (vertices, indices, attributes)
  - usage: std::string ("Static", "Dynamic", "Streaming")
- **Responsibility**:
  - Allocate vertex/index buffers
  - Upload mesh data to GPU
  - Handle staging buffers
- **Lifecycle**: Upload in Compile, no Execute work

#### 5. RenderPassNode
- **Type ID**: 104
- **Pipeline Type**: Graphics
- **Inputs**: None
- **Outputs**:
  - [0] RenderPass handle (opaque)
- **Parameters**:
  - colorFormat: VkFormat
  - depthFormat: VkFormat
  - loadOp: std::string ("Load", "Clear", "DontCare")
  - storeOp: std::string ("Store", "DontCare")
  - samples: uint32_t (MSAA)
- **Responsibility**:
  - Define render pass with attachments
  - Configure load/store operations
  - Define subpass dependencies
- **Lifecycle**: Create in Compile, reuse in Execute

#### 6. FramebufferNode
- **Type ID**: 105
- **Pipeline Type**: Graphics
- **Inputs**:
  - [0] RenderPass (from RenderPassNode)
  - [1] Color attachment (from SwapChainNode or other)
  - [2] Depth attachment (from DepthBufferNode) - optional
- **Outputs**:
  - [0] Framebuffer handle (opaque)
- **Parameters**:
  - width: uint32_t
  - height: uint32_t
- **Responsibility**:
  - Assemble framebuffer from attachments
  - Validate attachment compatibility
- **Lifecycle**: Create in Compile, reuse in Execute

#### 7. ShaderNode
- **Type ID**: 106
- **Pipeline Type**: Graphics
- **Inputs**: None
- **Outputs**:
  - [0] Vertex shader module
  - [1] Fragment shader module
  - [2] (Optional) Geometry shader module
- **Parameters**:
  - vertexPath: std::string
  - fragmentPath: std::string
  - geometryPath: std::string (optional)
  - compileFromGLSL: bool (vs. load .spv)
- **Responsibility**:
  - Load/compile shader modules
  - Cache compiled SPIR-V
- **Lifecycle**: Load in Compile, no Execute work

#### 8. DescriptorSetNode
- **Type ID**: 107
- **Pipeline Type**: Graphics
- **Inputs**:
  - [0] Texture (from TextureLoaderNode) - optional
  - [1] Uniform buffer - optional
  - [N] Additional bindings...
- **Outputs**:
  - [0] Descriptor set
  - [1] Descriptor set layout
- **Parameters**:
  - bindings: array of {type, stage, count}
- **Responsibility**:
  - Create descriptor set layout
  - Allocate descriptor sets
  - Update bindings
- **Lifecycle**: Create layout in Compile, update sets in Execute

#### 9. GraphicsPipelineNode
- **Type ID**: 108
- **Pipeline Type**: Graphics
- **Inputs**:
  - [0] Vertex shader (from ShaderNode)
  - [1] Fragment shader (from ShaderNode)
  - [2] RenderPass (from RenderPassNode)
  - [3] Descriptor set layout (from DescriptorSetNode)
- **Outputs**:
  - [0] Pipeline
  - [1] Pipeline layout
- **Parameters**:
  - vertexInputState: custom (attributes, bindings)
  - rasterizationState: custom (cull mode, polygon mode, etc.)
  - depthStencilState: custom (depth test, depth write, etc.)
  - blendState: custom (blend enable, blend factors, etc.)
  - dynamicState: array of dynamic states
- **Responsibility**:
  - Assemble graphics pipeline
  - Configure fixed-function stages
  - Handle pipeline cache
- **Lifecycle**: Create in Compile, reuse in Execute

#### 10. GeometryRenderNode
- **Type ID**: 109
- **Pipeline Type**: Graphics
- **Inputs**:
  - [0] Framebuffer (from FramebufferNode)
  - [1] RenderPass (from RenderPassNode)
  - [2] Pipeline (from GraphicsPipelineNode)
  - [3] Descriptor set (from DescriptorSetNode)
  - [4] Vertex buffer (from VertexBufferNode)
  - [5] Index buffer (from VertexBufferNode) - optional
- **Outputs**:
  - [0] Rendered image (same as framebuffer color attachment)
- **Parameters**:
  - clearColor: glm::vec4
  - vertexCount: uint32_t
  - instanceCount: uint32_t
  - useIndexBuffer: bool
- **Responsibility**:
  - Begin render pass
  - Bind pipeline
  - Bind descriptor sets
  - Bind vertex/index buffers
  - Issue draw calls
  - End render pass
- **Lifecycle**: Record commands in Execute

#### 11. PresentNode
- **Type ID**: 110
- **Pipeline Type**: Graphics
- **Inputs**:
  - [0] Rendered image (from GeometryRenderNode)
  - [1] Render finished semaphore (from SwapChainNode)
- **Outputs**: None (presents to screen)
- **Parameters**: None
- **Responsibility**:
  - Queue present operation
  - Handle presentation errors
  - Signal semaphores
- **Lifecycle**: Execute phase only

---

## Data Flow Example - Complete Rendering

```
[TextureLoaderNode]
      ↓ (texture)
      ↓
[DepthBufferNode]   [SwapChainNode]
      ↓ (depth)       ↓ (swapchain image + semaphores)
      ↓               ↓
[VertexBufferNode]  [RenderPassNode] ────→ [FramebufferNode]
      ↓ (verts)       ↓ (renderpass)         ↓ (framebuffer)
      ↓               ↓                       ↓
[ShaderNode]        [DescriptorSetNode] ←── (texture)
      ↓ (shaders)     ↓ (descriptors)
      ↓               ↓
      └──→ [GraphicsPipelineNode]
                  ↓ (pipeline)
                  ↓
           [GeometryRenderNode] ←── (all inputs)
                  ↓ (rendered image)
                  ↓
           [PresentNode] ←── (semaphores)
                  ↓
            (Display!)
```

---

## Implementation Priority

### Phase 1: Core Resources (DONE)
- ✅ TextureLoaderNode
- ✅ DepthBufferNode

### Phase 2: Pipeline Assembly (NEXT)
- ShaderNode - Simple, no dependencies
- RenderPassNode - Simple, no dependencies
- VertexBufferNode - Needs mesh data structure

### Phase 3: Pipeline Creation
- DescriptorSetNode - Depends on ShaderNode
- GraphicsPipelineNode - Depends on ShaderNode + RenderPassNode

### Phase 4: Rendering
- FramebufferNode - Depends on RenderPassNode
- GeometryRenderNode - Depends on all above
- SwapChainNode - Complex, stateful
- PresentNode - Depends on SwapChainNode

---

## Helper Structures Needed

### MeshData (for VertexBufferNode)
```cpp
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    VertexInputDescription inputDescription;
};
```

### VertexInputDescription
```cpp
struct VertexInputDescription {
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
};
```

### PipelineStateDescription (for GraphicsPipelineNode)
```cpp
struct PipelineStateDescription {
    VkPipelineRasterizationStateCreateInfo rasterization;
    VkPipelineDepthStencilStateCreateInfo depthStencil;
    VkPipelineColorBlendStateCreateInfo colorBlend;
    std::vector<VkDynamicState> dynamicStates;
};
```

---

## Testing Strategy

### Unit Tests (per node)
- Create node in isolation
- Set parameters
- Compile
- Verify outputs created

### Integration Test (full pipeline)
- Build complete graph (all 11 nodes)
- Compile graph
- Execute 1 frame
- Verify image rendered
- Compare with current VulkanRenderer output

---

## Migration Path

### Current State
```cpp
VulkanRenderer renderer;
renderer.Initialize();    // Does everything
renderer.Render();         // Renders frame
```

### Target State
```cpp
NodeTypeRegistry registry;
// Register all node types...

RenderGraph graph(&device, &registry);
// Build graph with nodes...
graph.Compile();

// Render loop
graph.Execute(commandBuffer);
```

### Transition
1. Implement all nodes
2. Create RenderGraphBuilder helper class
3. RenderGraphBuilder::BuildFromRenderer(VulkanRenderer&)
4. Test side-by-side
5. Switch to graph-based system
6. Deprecate VulkanRenderer

---

*Next: Implement Phase 2 nodes (Shader, RenderPass, VertexBuffer)*
