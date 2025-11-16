# Legacy Vulkan Wrappers - Migration Guide

**Archived**: 2025-11-16
**Reason**: Replaced by modern CashSystem cachers and RenderGraph nodes

---

## Archived Files

### VulkanPipeline.cpp/.h

**Replaced by**: `CashSystem/PipelineCacher.h`
**Used in RenderGraph**: `GraphicsPipelineNode`, `ComputePipelineNode`

**Migration Example**:
```cpp
// OLD (legacy):
VulkanPipeline pipeline;
pipeline.CreatePipeline(drawable, shader, config, &handle, renderPass, device);

// NEW (RenderGraph):
auto pipelineNode = graph->CreateNode<GraphicsPipelineNode>("pipeline");
pipelineNode->SetShader("shaders/myshader.spv");
pipelineNode->SetRenderPass(renderPassNode);
// CashSystem automatically caches pipelines internally
```

**Key Features Replaced**:
- Pipeline cache management → CashSystem handles caching
- Dynamic state setup → GraphicsPipelineNode configurable
- Vertex input binding → VertexBufferNode handles vertex data

---

### VulkanDescriptor.cpp/.h

**Replaced by**: `CashSystem/DescriptorCacher.h` + `CashSystem/DescriptorSetLayoutCacher.h`
**Used in RenderGraph**: `DescriptorSetNode`, `DescriptorResourceGathererNode`

**Migration Example**:
```cpp
// OLD (legacy):
class MyDrawable : public VulkanDescriptor {
    VulkanStatus CreateDescriptorSetLayout(bool useTexture) override {
        // Manual descriptor layout creation
    }
};

// NEW (RenderGraph):
auto descNode = graph->CreateNode<DescriptorSetNode>("descriptors");
descNode->AddUniformBuffer(uboNode, 0, VK_SHADER_STAGE_VERTEX_BIT);
descNode->AddTexture(textureNode, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
// CashSystem handles descriptor allocation, layout creation, and caching
```

**Key Features Replaced**:
- Descriptor pool management → CashSystem allocates from centralized pools
- Descriptor set layout creation → DescriptorSetLayoutCacher handles layouts
- Descriptor set updates → DescriptorResourceGathererNode automates updates

---

## Still Active Files (VulkanResources Library)

### VulkanShader.cpp/.h

**Location**: `source/VulkanResources/`
**Purpose**: Runtime VkShaderModule wrapper for RenderGraph
**Used by**: ShaderLibraryNode, ConstantNode, ShaderManagement

**Why Not Archived**:
- ShaderManagement compiles GLSL/HLSL to SPIR-V
- VulkanShader wraps the final VkShaderModule handles
- RenderGraph nodes consume VulkanShader objects as typed resources
- Provides shader stage configuration (vertex, fragment, compute)

**Usage**:
```cpp
// ShaderManagement compiles shader
auto spirv = ShaderCompiler::CompileGLSL("shader.vert", shaderc_vertex_shader);

// VulkanShader wraps VkShaderModule
VulkanShader shader;
shader.BuildShaderModuleWithSPV(spirv.data(), spirv.size(), ...);

// RenderGraph consumes
pipelineNode->SetShaderStages(shader.shaderStages, shader.stagesCount);
```

---

### VulkanSwapChain.cpp/.h

**Location**: `source/VulkanResources/`
**Purpose**: Swapchain management with implicit conversions for RenderGraph
**Used by**: 15 RenderGraph nodes (SwapChainNode, FramebufferNode, RenderPassNode, etc.)

**Why Not Archived**:
- Core presentation infrastructure (surface, swapchain, image views)
- Provides implicit conversion operators for seamless node connections
- Handles platform-specific surface creation (Win32)
- Manages present modes (IMMEDIATE, MAILBOX, FIFO)

**Key Features**:
- Surface creation: `CreateSurface(instance, hwnd, hinstance)`
- Swapchain lifecycle: `CreateSwapChainColorImages()`, `DestroySwapChain()`
- Image view management: `CreateColorImageView()`
- Implicit conversions: `operator VkImageView()`, `operator VkSwapchainKHR()`

**Usage in RenderGraph**:
```cpp
auto swapChainNode = graph->CreateNode<SwapChainNode>("swapchain");
auto framebufferNode = graph->CreateNode<FramebufferNode>("framebuffer");

// SwapChainNode internally manages VulkanSwapChain
// Implicit conversions allow direct node connections
framebufferNode->Connect(swapChainNode); // VulkanSwapChain converts to VkImageView
```

---

### wrapper.cpp/.h

**Location**: `source/VulkanResources/`
**Purpose**: Command buffer utilities (CommandBufferMgr) + ReadFile helper
**Used by**: TextureLoader (staging buffer operations)

**Why Not Archived**:
- `CommandBufferMgr` provides simple one-shot command submission
- Used by TextureLoader for texture upload staging operations
- `ReadFile()` utility for loading files into memory

**Key Functions**:
```cpp
// Command buffer helpers
CommandBufferMgr::AllocateCommandBuffer(device, cmdPool, &cmdBuf);
CommandBufferMgr::BeginCommandBuffer(cmdBuf);
// ... record commands ...
CommandBufferMgr::EndCommandBuffer(cmdBuf);
CommandBufferMgr::SubmitCommandBuffer(queue, &cmdBuf);

// File loading utility
void* data = ReadFile("texture.png", &size);
```

**Usage in TextureLoader**:
```cpp
// TextureHandling/Loading/TextureLoader.cpp
VkCommandBuffer stagingCmd;
CommandBufferMgr::AllocateCommandBuffer(..., &stagingCmd);
CommandBufferMgr::BeginCommandBuffer(stagingCmd);
// Copy texture data to GPU
vkCmdCopyBufferToImage(...);
CommandBufferMgr::EndCommandBuffer(stagingCmd);
CommandBufferMgr::SubmitCommandBuffer(queue, &stagingCmd);
```

---

### VulkanApplicationBase.cpp/.h

**Location**: `source/` (main application level)
**Purpose**: Base application class for VulkanGraphApplication
**Used by**: VulkanGraphApplication, main.cpp

**Why Not Archived**:
- Core application architecture (abstract base class)
- Handles Vulkan instance initialization
- Defines application lifecycle: Initialize(), Prepare(), Update(), Render(), DeInitialize()
- VulkanGraphApplication inherits from it

**Class Hierarchy**:
```
VulkanApplicationBase (abstract)
    ├── Initialize() - Vulkan core setup
    ├── Prepare() - Pure virtual
    ├── Update() - Pure virtual
    ├── Render() - Pure virtual
    └── DeInitialize() - Cleanup
           ↑
           │
    VulkanGraphApplication (concrete)
        ├── Prepare() - Build RenderGraph
        ├── Update() - Update camera, inputs
        └── Render() - Execute RenderGraph
```

---

## Architecture Comparison

### Legacy (Pre-RenderGraph)

```
VulkanApplication
    ├── VulkanRenderer
    │   ├── VulkanPipeline (ARCHIVED)
    │   ├── VulkanDescriptor (ARCHIVED)
    │   ├── VulkanShader
    │   └── VulkanSwapChain
    └── VulkanDrawable (inherits VulkanDescriptor)
```

**Problems**:
- Manual resource management (no caching)
- Tight coupling between rendering components
- Difficult to extend or modify rendering pipeline
- No automatic resource lifetime tracking

### Modern (RenderGraph + CashSystem)

```
VulkanGraphApplication (inherits VulkanApplicationBase)
    └── RenderGraph
        ├── CashSystem (resource caching layer)
        │   ├── PipelineCacher (replaces VulkanPipeline)
        │   ├── DescriptorCacher (replaces VulkanDescriptor)
        │   ├── ShaderModuleCacher
        │   ├── RenderPassCacher
        │   └── SamplerCacher
        ├── Nodes (declarative graph construction)
        │   ├── GraphicsPipelineNode (uses PipelineCacher)
        │   ├── DescriptorSetNode (uses DescriptorCacher)
        │   ├── SwapChainNode (wraps VulkanSwapChain)
        │   ├── ShaderLibraryNode (wraps VulkanShader)
        │   └── 40+ other nodes
        └── VulkanResources (low-level wrappers)
            ├── VulkanShader (VkShaderModule wrapper)
            ├── VulkanSwapChain (swapchain lifecycle)
            ├── wrapper (command buffer utilities)
            ├── VulkanDevice
            ├── VulkanInstance
            └── TextureHandling
```

**Benefits**:
- Automatic resource caching (CashSystem)
- Declarative graph construction (RenderGraph)
- Automatic resource lifetime tracking
- Easy pipeline reconfiguration
- Node-based modularity

---

## Build System Changes

**Before**:
```cmake
# source/VulkanResources/CMakeLists.txt (lines 24-30)
list(APPEND VULKAN_RESOURCES_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../VulkanDescriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../VulkanPipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../VulkanShader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../VulkanSwapChain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrapper.cpp
)
```

**After**:
```cmake
# source/VulkanResources/CMakeLists.txt (lines 24-29)
# Note: VulkanDescriptor.cpp and VulkanPipeline.cpp archived (replaced by CashSystem)
list(APPEND VULKAN_RESOURCES_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../VulkanShader.cpp      # Runtime VkShaderModule wrapper
    ${CMAKE_CURRENT_SOURCE_DIR}/../VulkanSwapChain.cpp   # Swapchain management
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrapper.cpp           # CommandBufferMgr utilities
)
```

---

## Migration Checklist

If you need to restore legacy functionality:

1. **Pipeline Creation**:
   - ❌ Don't use VulkanPipeline
   - ✅ Use GraphicsPipelineNode + CashSystem PipelineCacher
   - See: `RenderGraph/src/Nodes/GraphicsPipelineNode.cpp`

2. **Descriptor Management**:
   - ❌ Don't inherit from VulkanDescriptor
   - ✅ Use DescriptorSetNode + CashSystem DescriptorCacher
   - See: `RenderGraph/src/Nodes/DescriptorSetNode.cpp`

3. **Shader Loading**:
   - ✅ VulkanShader still active (wraps VkShaderModule)
   - ✅ Use ShaderManagement for compilation
   - See: `ShaderManagement/include/ShaderCompiler.h`

4. **Swapchain Setup**:
   - ✅ VulkanSwapChain still active
   - ✅ Use SwapChainNode in RenderGraph
   - See: `RenderGraph/src/Nodes/SwapChainNode.cpp`

---

## Questions or Issues?

If you encounter problems migrating from legacy Vulkan wrappers:

1. Check RenderGraph examples: `RenderGraph/tests/`
2. Review CashSystem cachers: `CashSystem/include/CashSystem/`
3. Search for node usage: `grep -r "GraphicsPipelineNode" RenderGraph/src/`
4. Consult documentation: `documentation/RenderGraph/`

**Last Updated**: 2025-11-16
