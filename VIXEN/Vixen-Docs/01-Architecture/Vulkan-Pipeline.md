---
title: Vulkan Pipeline Architecture
aliases: [Vulkan Pipeline, GPU Pipeline, Vulkan Rendering]
tags: [architecture, vulkan, pipeline, gpu]
created: 2025-12-06
related:
  - "[[Overview]]"
  - "[[RenderGraph-System]]"
  - "[[../02-Implementation/Shaders|Shaders]]"
---

# Vulkan Pipeline Architecture

VIXEN's Vulkan integration follows a node-based approach where each Vulkan operation is encapsulated in a graph node.

---

## 1. Render Pipeline Flow

```mermaid
graph TD
    subgraph Initialization
        W[WindowNode] --> |HWND| SC
        D[DeviceNode] --> |VkDevice| SC[SwapChainNode]
    end

    subgraph Resources
        SC --> |Images| FB[FramebufferNode]
        RP[RenderPassNode] --> FB
        DB[DepthBufferNode] --> FB
    end

    subgraph Pipeline
        SL[ShaderLibraryNode] --> GP[GraphicsPipelineNode]
        DS[DescriptorSetNode] --> GR[GeometryRenderNode]
        GP --> GR
        FB --> GR
    end

    subgraph Presentation
        GR --> P[PresentNode]
        FS[FrameSyncNode] --> GR
        FS --> P
    end

    style W fill:#e74c3c
    style D fill:#e74c3c
    style SC fill:#3498db
    style FB fill:#27ae60
    style GP fill:#9b59b6
    style P fill:#f39c12
```

---

## 2. Frame-in-Flight Synchronization

```mermaid
sequenceDiagram
    participant CPU
    participant Fence as VkFence
    participant GPU
    participant Present

    CPU->>Fence: Wait (Frame N-2)
    Fence-->>CPU: Signaled
    CPU->>GPU: Submit Frame N
    GPU->>Present: Render Complete
    Present->>Fence: Signal (Frame N)

    Note over CPU,Present: MAX_FRAMES_IN_FLIGHT = 4
```

### 2.1 Two-Tier Synchronization

| Type | Purpose | Indexing |
|------|---------|----------|
| **Fences** | CPU-GPU sync | Per-flight (4) |
| **imageAvailable Semaphores** | Acquire sync | Per-flight (4) |
| **renderComplete Semaphores** | Present sync | Per-image (3) |

---

## 3. Swapchain Management

```mermaid
graph LR
    subgraph SwapChain [Swapchain - 3 Images]
        I0[Image 0]
        I1[Image 1]
        I2[Image 2]
    end

    subgraph Views [Image Views]
        V0[View 0]
        V1[View 1]
        V2[View 2]
    end

    subgraph Framebuffers
        F0[FB 0]
        F1[FB 1]
        F2[FB 2]
    end

    I0 --> V0 --> F0
    I1 --> V1 --> F1
    I2 --> V2 --> F2
```

### 3.1 Swapchain Invalidation

```mermaid
graph TD
    WR[Window Resize] --> SCI[SwapChain Invalidated]
    SCI --> FB[Framebuffer Dirty]
    SCI --> DB[DepthBuffer Dirty]
    FB --> RC[Recompile]
    DB --> RC
```

---

## 4. Descriptor Set Management

### 4.1 Descriptor Layout

```cpp
// Auto-generated from SPIRV reflection
VkDescriptorSetLayoutBinding bindings[] = {
    {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
    {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}
};
```

### 4.2 Per-Frame Descriptor Sets

```mermaid
graph LR
    subgraph Pool [Descriptor Pool]
        DS0[Set Frame 0]
        DS1[Set Frame 1]
        DS2[Set Frame 2]
    end

    subgraph UBOs [Uniform Buffers]
        UBO0[UBO Frame 0]
        UBO1[UBO Frame 1]
        UBO2[UBO Frame 2]
    end

    DS0 --> UBO0
    DS1 --> UBO1
    DS2 --> UBO2
```

---

## 5. Pipeline Creation

### 5.1 Graphics Pipeline

```cpp
// Data-driven pipeline creation from SPIRV reflection
void GraphicsPipelineNode::CompileImpl() {
    auto shaderBundle = In(SHADER_DATA_BUNDLE);

    // Auto-extract vertex format from reflection
    auto vertexInputs = BuildVertexInputsFromReflection(shaderBundle);

    // Auto-create descriptor layout if not provided
    if (!In(DESCRIPTOR_SET_LAYOUT)) {
        descriptorSetLayout = DescriptorSetLayoutCacher::GetOrCreate(
            device, shaderBundle);
    }

    // Create pipeline via cacher
    VkPipeline pipeline = PipelineCacher::GetOrCreate(device, params);

    Out(PIPELINE, pipeline);
    Out(PIPELINE_LAYOUT, pipelineLayout);
}
```

### 5.2 Compute Pipeline

```cpp
void ComputePipelineNode::CompileImpl() {
    auto shaderBundle = In(SHADER_DATA_BUNDLE);

    // Similar pattern - auto descriptor layout, cached pipeline
    VkPipeline pipeline = ComputePipelineCacher::GetOrCreate(device, params);

    Out(PIPELINE, pipeline);
}
```

---

## 6. Push Constants

### 6.1 Camera Data (64 bytes)

```glsl
layout(push_constant) uniform PushConstants {
    vec3 cameraPos;     // 0
    float time;         // 12
    vec3 cameraDir;     // 16
    float fov;          // 28
    vec3 cameraUp;      // 32
    float aspect;       // 44
    vec3 cameraRight;   // 48
    int debugMode;      // 60
};
```

### 6.2 Push Constant Wiring

```cpp
// In GeometryRenderNode::Execute()
vkCmdPushConstants(
    commandBuffer,
    pipelineLayout,
    VK_SHADER_STAGE_FRAGMENT_BIT,
    0,
    sizeof(PushConstantData),
    &pushConstantData
);
```

---

## 7. GPU Timing

### 7.1 Timestamp Query

```cpp
class GPUTimestampQuery {
    VkQueryPool timestampPool_;
    float timestampPeriod_;  // Nanoseconds per tick

public:
    void WriteTimestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage, uint32_t query);
    std::optional<double> GetElapsedMs(VkDevice device, uint32_t start, uint32_t end);
    double CalculateMraysPerSec(double elapsedMs, uint32_t width, uint32_t height);
};
```

### 7.2 Performance Logging

```cpp
class GPUPerformanceLogger {
    std::array<double, 60> dispatchTimes_;  // Rolling window
    std::array<double, 60> mraysPerSec_;

public:
    void RecordFrame(double dispatchMs, double mrays);
    void MaybeLog(uint32_t frameNumber);  // Every 120 frames
};
```

---

## 8. Memory Management

### 8.1 Buffer Types

| Type | Usage | Flags |
|------|-------|-------|
| Staging | CPU-visible upload | HOST_VISIBLE, HOST_COHERENT |
| Device | GPU-only access | DEVICE_LOCAL |
| Uniform | Small, frequent updates | HOST_VISIBLE |
| Storage | Large read/write | DEVICE_LOCAL |

### 8.2 Image Formats

| Usage | Format |
|-------|--------|
| Color Attachment | VK_FORMAT_B8G8R8A8_SRGB |
| Depth Attachment | VK_FORMAT_D32_SFLOAT |
| Storage Image | VK_FORMAT_R8G8B8A8_UNORM |
| DXT Compressed | VK_FORMAT_BC1_RGB_UNORM_BLOCK |

---

## 9. Code References

| Component | Location |
|-----------|----------|
| SwapChainNode | `libraries/RenderGraph/src/Nodes/SwapChainNode.cpp` |
| FramebufferNode | `libraries/RenderGraph/src/Nodes/FramebufferNode.cpp` |
| GraphicsPipelineNode | `libraries/RenderGraph/src/Nodes/GraphicsPipelineNode.cpp` |
| DescriptorSetNode | `libraries/RenderGraph/src/Nodes/DescriptorSetNode.cpp` |
| GPUTimestampQuery | `libraries/VulkanResources/include/GPUTimestampQuery.h` |
| FrameSyncNode | `libraries/RenderGraph/src/Nodes/FrameSyncNode.cpp` |

---

## 10. Related Pages

- [[Overview]] - Architecture overview
- [[RenderGraph-System]] - Graph system details
- [[../02-Implementation/Shaders|Shaders]] - Shader documentation
- [[../04-Development/Profiling|Profiling]] - Performance measurement
