# RenderGraph Library

Data-flow graph system for Vulkan resource management and rendering pipeline execution.

## Purpose

Provides a node-based architecture where:
- Nodes produce/consume typed resources via slots
- Compilation phase validates connections and creates Vulkan objects
- Execution phase records commands to command buffers
- Per-frame resources handled automatically for frames-in-flight

## Core Concepts

### Nodes
Self-contained units with input/output slots. Each node implements:
- `CompileImpl()` - Create Vulkan resources (pipelines, buffers, etc.)
- `ExecuteImpl()` - Record commands to command buffer

### Slots
Typed connection points for data flow:
- **Input slots**: Receive data from other nodes
- **Output slots**: Provide data to other nodes

### Graph Lifecycle
1. **Build**: Create nodes and connect slots
2. **Compile**: Validate connections, create Vulkan objects
3. **Execute**: Per-frame command recording

## Key Node Types

### Resource Nodes
| Node | Purpose |
|------|---------|
| DeviceNode | Vulkan device creation |
| SwapChainNode | Swapchain management |
| CommandPoolNode | Command buffer allocation |
| FrameSyncNode | Frame synchronization (fences/semaphores) |

### Pipeline Nodes
| Node | Purpose |
|------|---------|
| ComputePipelineNode | Compute shader pipeline |
| GraphicsPipelineNode | Graphics shader pipeline |
| ShaderLibraryNode | Shader module caching |
| DescriptorSetNode | Descriptor set management |

### Render Nodes
| Node | Purpose |
|------|---------|
| VoxelGridNode | ESVO voxel ray marching (compute) |
| GeometryRenderNode | Traditional mesh rendering |
| RenderPassNode | Render pass begin/end |
| PresentNode | Swapchain presentation |

### Utility Nodes
| Node | Purpose |
|------|---------|
| CameraNode | Camera matrices and input |
| InputNode | Keyboard/mouse input handling |
| ConstantNode | Static value providers |
| BoolOpNode | Conditional logic |

## Directory Structure

```
libraries/RenderGraph/
├── include/
│   ├── Core/              # Graph infrastructure
│   │   ├── NodeInstance.h
│   │   ├── NodeType.h
│   │   ├── GraphTopology.h
│   │   ├── GPUPerformanceLogger.h
│   │   └── ...
│   ├── Data/              # Type definitions
│   │   ├── ResourceTypes.h
│   │   └── Nodes/         # Node configs
│   └── Nodes/             # Node headers
├── src/
│   ├── Core/
│   └── Nodes/
└── README.md
```

## Usage Example

```cpp
// Build graph
auto device = graph.AddNode<DeviceNode>();
auto swapchain = graph.AddNode<SwapChainNode>();
auto voxelGrid = graph.AddNode<VoxelGridNode>();
auto present = graph.AddNode<PresentNode>();

// Connect slots
graph.Connect(device, "Device", swapchain, "Device");
graph.Connect(swapchain, "Images", voxelGrid, "OutputImage");
graph.Connect(voxelGrid, "Output", present, "Image");

// Compile and run
graph.Compile();
while (running) {
    graph.Execute(currentFrameIndex);
}
```

## Performance Logging

Nodes can integrate GPU timing via GPUPerformanceLogger:

```cpp
gpuLogger_ = std::make_shared<GPUPerformanceLogger>("NodeName", device, framesInFlight);
gpuLogger_->BeginFrame(cmdBuffer, frameIndex);
gpuLogger_->RecordDispatchStart(cmdBuffer, frameIndex);
// ... dispatch ...
gpuLogger_->RecordDispatchEnd(cmdBuffer, frameIndex, width, height);
```

## Related Documentation

- [DOCUMENTATION_INDEX.md](../../DOCUMENTATION_INDEX.md) - Full documentation index
- [RenderGraph.md](../../documentation/RenderGraph/RenderGraph.md) - Detailed architecture
- [GPUPerformanceSystem.md](../../documentation/VulkanResources/GPUPerformanceSystem.md) - Timing system
