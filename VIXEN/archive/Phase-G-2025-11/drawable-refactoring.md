# VulkanDrawable Refactoring Strategy

## Overview

The monolithic `VulkanDrawable` class must be decomposed into composable RenderGraph nodes. This document defines the refactoring strategy and node architecture for descriptor management, pipeline creation, and command buffer recording.

---

## Current Architecture (Legacy)

### VulkanDrawable Responsibilities

```
VulkanDrawable (MonolithicClass)
├── Descriptor Management
│   ├── CreateDescriptorSetLayout()
│   ├── CreateDescriptorPool()
│   ├── CreateDescriptorSet()
│   └── Descriptor binding definitions
├── Pipeline Management
│   ├── CreatePipelineLayout()
│   └── Push constant ranges
├── Vertex Input
│   ├── CreateVertexBuffer()
│   ├── CreateVertexIndex()
│   ├── Vertex binding descriptions (viIpBind)
│   └── Vertex attribute descriptions (viIpAttr)
├── Uniform Buffer Management
│   ├── CreateUniformBuffer()
│   ├── MVP matrix updates (in Update())
│   └── Persistent memory mapping
└── Command Recording
    ├── RecordCommandBuffer()
    ├── InitViewports()
    ├── InitScissors()
    └── Draw call recording with binding
```

### Problems

1. **Single Responsibility Violation**: 15 public methods, <200 lines requirement violated
2. **Tight Coupling**: Direct dependencies on VulkanRenderer, VulkanShader, VulkanPipeline
3. **Non-Reusable**: Hard to mix/match descriptor configurations with different pipelines
4. **Draw-Time Abstraction**: Conflates resource creation (compile-time) with draw execution (runtime)

---

## New Architecture (RenderGraph Nodes)

### Node Hierarchy

```
Pipeline Assembly Phase:
  DescriptorSetNode (TypeID: 200)
    └─ Creates descriptor pools, layouts, and allocates sets
    └─ Depends on: ShaderLibraryNode, TextureLoaderNode

  GraphicsPipelineNode (TypeID: 201)
    └─ Creates graphics pipeline with all state
    └─ Depends on: ShaderLibraryNode, RenderPassNode, DescriptorSetNode
    
  FramebufferNode (TypeID: 202)
    └─ Creates framebuffers for rendering
    └─ Depends on: DepthBufferNode, RenderPassNode, SwapChainNode

Geometry Phase:
  VertexBufferNode (TypeID: 103)
    └─ Uploads mesh vertex data
    └─ Depends on: (None - standalone buffer management)

  GeometryRenderNode (TypeID: 210)
    └─ Records and executes draw commands
    └─ Depends on: VertexBufferNode, GraphicsPipelineNode, FramebufferNode

Runtime Phase:
  UniformUpdateNode (TypeID: 211)
    └─ Updates uniform buffers per-frame (MVP, etc)
    └─ Depends on: DescriptorSetNode (to know buffer layout)

  PresentNode (TypeID: 212)
    └─ Synchronization and swapchain presentation
    └─ Depends on: GeometryRenderNode, SwapChainNode
```

---

## DescriptorSetNodeConfig

### Purpose

Encapsulates descriptor set, layout, and pool creation. Replaces `VulkanDrawable::CreateDescriptorSet*()` methods.

### Design

**NodeType ID**: 200  
**Category**: Pipeline Assembly  

#### Input Ports

```cpp
struct DescriptorSetNodeConfig {
    // From ShaderLibraryNode
    struct {
        // Bindings used by shaders (queried from spirv reflection)
        std::vector<VkDescriptorSetLayoutBinding> bindings;
    } shaderMetadata;

    // From TextureLoaderNode (if needed)
    struct {
        bool includeTexture;  // Determines binding[1] requirements
        // Future: texture metadata for binding info
    } resourceConfig;
};
```

#### Output Ports

```cpp
struct DescriptorSetNodeOutput {
    VkDescriptorPool                      pool;           // Manages allocation
    std::vector<VkDescriptorSetLayout>   layouts;         // Define structure
    std::vector<VkDescriptorSet>         sets;            // Actual allocations
};
```

#### Compile() Logic

1. Analyze binding requirements from inputs
2. `vkCreateDescriptorSetLayout()` - Infer from shader reflection
3. `vkCreateDescriptorPool()` - Size pool based on binding types
4. `vkAllocateDescriptorSets()` - Allocate sets
5. `vkUpdateDescriptorSets()` - Bind resources (UBOs, samplers, images)

#### Invalidation Triggers

- `ShaderReloadEvent` → Recompile if bindings changed
- `TextureLoadEvent` → Update image descriptor in set
- `UniformBufferResizedEvent` → Reallocate/rebind

---

## GraphicsPipelineNodeConfig

### Purpose

Encapsulates graphics pipeline creation. Replaces `VulkanPipeline::CreatePipeline()` logic.

### Design

**NodeType ID**: 201  
**Category**: Pipeline Assembly  

#### Input Ports

```cpp
struct GraphicsPipelineNodeConfig {
    // From ShaderLibraryNode
    struct {
        VkShaderModule vertModule;
        VkShaderModule fragModule;
        // Entry point names (typically "main")
    } shaders;

    // From DescriptorSetNode
    struct {
        VkPipelineLayout pipelineLayout;  // Contains descriptor layouts + push constants
    } descriptorInfo;

    // From RenderPassNode
    struct {
        VkRenderPass renderPass;
        uint32_t subpass;  // Usually 0
    } renderPass;

    // User Config
    struct {
        VkViewport viewport;
        VkRect2D scissor;
        bool enableDepthTest;
        bool enableDepthWrite;
        // Rasterization state
        VkCullModeFlags cullMode;
        VkFrontFace frontFace;
        VkPolygonMode polygonMode;
    } state;

    // Vertex Input (from mesh metadata)
    struct {
        VkVertexInputBindingDescription    binding;
        std::vector<VkVertexInputAttributeDescription> attributes;
    } vertexInput;
};
```

#### Output Ports

```cpp
struct GraphicsPipelineNodeOutput {
    VkPipeline pipeline;           // The graphics pipeline handle
    VkPipelineLayout layout;       // Reference to input layout
};
```

#### Compile() Logic

1. Validate all shader modules present
2. Build `VkPipelineShaderStageCreateInfo` array (vert, frag)
3. Create `VkPipelineVertexInputStateCreateInfo` from vertex data
4. Assemble all pipeline state structures
5. `vkCreateGraphicsPipelines()` with cache
6. Store pipeline handle in output

#### Invalidation Triggers

- `ShaderReloadEvent` → Recreate pipeline
- `RenderPassChangedEvent` → Recreate (compatibility change)
- `DescriptorSetLayoutChangedEvent` → Update layout compatibility

---

## About VulkanDrawable

### Current Role (Problems)

**Draw-time abstraction** that mixes:
- **Compile-time** resource creation (buffers, sets, pipeline)
- **Runtime** execution (semaphores, command recording, rendering)

This violates the RenderGraph model where nodes are:
1. **Created once** during graph construction
2. **Compiled** to validate dependencies and create GPU resources
3. **Executed** every frame without recompilation

### Proposed Removal Strategy

#### Phase 1: Extract Resource Methods (Now)

1. ✅ **DescriptorSetNodeConfig** - Takes `CreateDescriptorSet*()` logic
2. ✅ **GraphicsPipelineNodeConfig** - Takes `CreatePipelineLayout()` logic
3. ✅ **VertexBufferNode** - Takes `CreateVertexBuffer()` logic (already exists)

#### Phase 2: Extract Execution Methods (Next)

1. **GeometryRenderNode** - Takes `RecordCommandBuffer()` logic
   - Inputs: Pipeline, vertex buffers, descriptor sets, framebuffer
   - Outputs: Command buffer handles
   - Executes: `vkCmdBindPipeline()`, `vkCmdBindVertexBuffers()`, `vkCmdDraw()`

2. **UniformUpdateNode** - Takes `Update()` logic (MVP updates)
   - Inputs: Uniform buffer from DescriptorSetNode
   - Outputs: (None - updates in-place)
   - Executes: Memory mapping, memcpy, vkFlushMappedMemoryRanges()

3. **PresentNode** - Takes `Render()` logic (frame submission)
   - Inputs: Command buffers from GeometryRenderNode
   - Outputs: Semaphore signals
   - Executes: Swapchain acquire, queue submit, present

#### Phase 3: Keep/Deprecate

- ❌ **VulkanDrawable** itself - Will be **removed**
- ✅ **VulkanDrawable::SetTexture()** - Migrate to TextureBinding node or merge into DescriptorSetNode

---

## Vertex Input Data

### Current Approach (in VulkanDrawable)

```cpp
struct {
    VkVertexInputBindingDescription binding;      // binding[0] stride
    VkVertexInputAttributeDescription attr[2];    // position, color/uv
} viIpBind, viIpAttr;
```

### New Approach (in VertexBufferNode)

```cpp
struct VertexBufferNodeOutput {
    VkBuffer buffer;
    VkDeviceMemory memory;
    
    // Vertex input metadata
    struct {
        VkVertexInputBindingDescription binding;
        std::vector<VkVertexInputAttributeDescription> attributes;
    } vertexInputState;
};
```

**Consumer**: GraphicsPipelineNodeConfig receives this as input → uses in pipeline creation.

---

## Uniform Buffer & Frame Updates

### Current (VulkanDrawable)

```cpp
struct UniformData {
    VkBuffer buf;
    VkDeviceMemory mem;
    void* pData;                          // Persistent mapping
    std::vector<VkMappedMemoryRange> ranges;
};

// Updated in Update(deltaTime)
void VulkanDrawable::Update(float dt) {
    MVP = computeTransform(dt);
    memcpy(pData, &MVP, sizeof(MVP));
    vkFlushMappedMemoryRanges(...);
}
```

### New (UniformUpdateNode)

```cpp
struct UniformUpdateNodeInput {
    VkBuffer uniformBuffer;               // From DescriptorSetNode
    VkDeviceMemory memory;
    std::vector<VkMappedMemoryRange> ranges;
    
    float deltaTime;                      // From RenderGraph context
};

// Executed every frame by node
void UniformUpdateNode::Execute() {
    glm::mat4 MVP = computeTransform(input.deltaTime);
    memcpy(mappedPtr, &MVP, sizeof(MVP));
    vkFlushMappedMemoryRanges(device, ranges.size(), ranges.data());
}
```

---

## Push Constants

### Current (VulkanDrawable::RecordCommandBuffer)

```cpp
enum ColorFlag { RED, GREEN, BLUE, YELLOW, MIXED };
float mixerValue = 0.3f;
unsigned pushConstants[2] = { YELLOW, /* bitcast float */ };

vkCmdPushConstants(*cmdDraw, pipelineLayout, 
                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, 
                   sizeof(pushConstants), pushConstants);
```

### New (in PushConstantDataNode - future)

```cpp
struct PushConstantConfig {
    struct {
        uint32_t colorIndex;
        float mixerValue;
    } data;
};

// Stored in GeometryRenderNode::Execute()
vkCmdPushConstants(cmd, layout, 
                   VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                   sizeof(data), &data);
```

---

## Migration Roadmap

### Immediate (Phase 2.1: DescriptorSetNode + GraphicsPipelineNode)

1. Create `include/RenderGraph/Nodes/DescriptorSetNodeConfig.h`
2. Create `include/RenderGraph/Nodes/GraphicsPipelineNodeConfig.h`
3. Implement both node types
4. Register in NodeTypeRegistry
5. Update VertexBufferNode to output vertex input metadata
6. **Keep VulkanDrawable** for backward compatibility, but mark methods as deprecated

### Short-term (Phase 2.2: GeometryRenderNode)

1. Create GeometryRenderNode to handle command recording
2. Migrate `RecordCommandBuffer()` logic
3. Integrate with PresentNode for frame submission

### Medium-term (Phase 2.3: UniformUpdateNode)

1. Create UniformUpdateNode for per-frame updates
2. Subscribe to RenderGraph's frame-update events
3. Handle MVP matrix transformations

### Long-term (Phase 3: Full Deprecation)

1. Remove VulkanDrawable entirely
2. All functionality in specialized graph nodes
3. Old rendering loop → RenderGraph execution

---

## Design Principles Applied

✅ **Single Responsibility**: Each node type has one purpose  
✅ **Composability**: Nodes can be mixed/matched (e.g., different pipelines, descriptor layouts)  
✅ **Reusability**: Descriptor sets usable by multiple pipelines  
✅ **Loose Coupling**: Nodes communicate via EventBus  
✅ **RAII**: Resources owned by nodes, destroyed in destructors  
✅ **Testability**: Each node can be tested independently  

---

## References

- `documentation/GraphArchitecture/node-system.md` - Node base classes
- `documentation/GraphArchitecture/graph-compilation.md` - Compilation pipeline
- `memory-bank/systemPatterns.md` - Architecture patterns
- Existing nodes: `VertexBufferNode`, `TextureLoaderNode`, `DepthBufferNode`
