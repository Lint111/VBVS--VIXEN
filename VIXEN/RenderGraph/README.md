# RenderGraph Static Library

**Graph-based rendering architecture** for Vulkan with node system, automatic dependency tracking, and resource management.

## Structure

```
RenderGraph/
├── CMakeLists.txt          # Library build configuration
├── Core/                   # Graph infrastructure
│   ├── GraphTopology.cpp
│   ├── NodeInstance.cpp
│   ├── NodeType.cpp
│   ├── NodeTypeRegistry.cpp
│   └── RenderGraph.cpp
├── Nodes/                  # Concrete node implementations
│   ├── CommandPoolNode.cpp
│   ├── DepthBufferNode.cpp
│   ├── DescriptorSetNode.cpp
│   ├── DeviceNode.cpp
│   ├── FramebufferNode.cpp
│   ├── GeometryRenderNode.cpp
│   ├── GraphicsPipelineNode.cpp
│   ├── PresentNode.cpp
│   ├── RenderPassNode.cpp
│   ├── ShaderLibraryNode.cpp
│   ├── SwapChainNode.cpp
│   ├── TextureLoaderNode.cpp
│   ├── VertexBufferNode.cpp
│   └── WindowNode.cpp
└── include/RenderGraph/    # Public headers
    ├── GraphTopology.h
    ├── NodeInstance.h
    ├── NodeLogging.h
    ├── NodeType.h
    ├── NodeTypeRegistry.h
    ├── RenderGraph.h
    ├── Resource.h          # Template-based type-safe resources
    ├── ResourceConfig.h
    ├── ResourceTypes.h
    ├── TypedNodeInstance.h
    └── Nodes/              # Node type headers & configs
        ├── CommandPoolNode.h
        ├── CommandPoolNodeConfig.h
        ├── DepthBufferNode.h
        ├── ...
```

## Targets

### RenderGraphCore
**Core graph infrastructure** - topology, nodes, resources

- **Dependencies**: VulkanResources, Logger, EventBus, ResourceManagement
- **Standard**: C++23
- **Type**: Static library
- **Headers**: Exported via `RenderGraph/include`

### RenderGraphNodes
**Concrete node implementations** - all specialized rendering nodes

- **Dependencies**: RenderGraphCore, ShaderManagement
- **Standard**: C++23
- **Type**: Static library
- **Unity Build**: Enabled (4 files per batch)
- **Auto-discovery**: Add new `.cpp` files in `Nodes/` - no CMake changes needed!

## Usage

### In Parent CMakeLists.txt
```cmake
# Include RenderGraph as subdirectory
add_subdirectory(RenderGraph)

# Link against targets
target_link_libraries(YourTarget PRIVATE
    RenderGraphCore
    RenderGraphNodes
)
```

### In Your Code
```cpp
#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/Nodes/SwapChainNode.h"
#include "RenderGraph/Nodes/PresentNode.h"

// Use the graph
Vixen::RenderGraph::RenderGraph graph;
auto swapchain = graph.AddNode("swapchain", swapchainType, device);
// ...
```

## Key Features

### 1. Type-Safe Resources
`Resource<T>` template provides compile-time type safety:
```cpp
Resource<VkImage>* imageRes = ...;
Resource<VkBuffer>* bufferRes = ...;
IResource* basePtr = imageRes;  // Polymorphic base
```

### 2. Node Type vs Instance
- **NodeType**: Template/definition (e.g., `SwapChainNodeType`)
- **NodeInstance**: Concrete usage (e.g., `swapchain_main`, `swapchain_offscreen`)
- One type, many instances

### 3. EventBus Integration
Nodes emit events for invalidation cascades:
```cpp
WindowResize → SwapChainNode::OnEvent() → marks dirty → recompiles
```

### 4. Automatic Dependency Tracking
Graph topology analyzes connections, determines execution order

## Build Configuration

### Compiler Flags
- **MSVC**: `/FS` flag for PDB synchronization (parallel builds)
- **RenderGraphCore**: `VS_GLOBAL_UseMultiToolTask "false"` to prevent PDB contention

### Include Paths
- **Project headers**: `${CMAKE_SOURCE_DIR}/include` (Headers.h, etc.)
- **RenderGraph headers**: `${CMAKE_CURRENT_SOURCE_DIR}/include`

### Adding New Nodes
1. Create `YourNode.h` in `include/RenderGraph/Nodes/`
2. Create `YourNode.cpp` in `Nodes/`
3. **Done!** CMake auto-discovers via `file(GLOB ...)`

## Dependencies Graph

```
RenderGraphNodes
    └─> RenderGraphCore
        ├─> VulkanResources
        ├─> Logger
        ├─> EventBus
        └─> ResourceManagement
    └─> ShaderManagement
```

## Architecture Notes

### Resource System
- **Header-only templates**: `Resource<T>` defined entirely in `Resource.h`
- **Old Resource.cpp**: Removed (obsolete non-template version)
- **Polymorphic base**: `IResource` for type-erased storage
- **Type-safe downcast**: `resource->As<VkImage>()`

### Node Lifecycle
1. **Created**: Instance allocated
2. **Compiled**: Resources allocated, pipelines built
3. **Dirty**: Invalidated, needs recompilation
4. **Executing**: Recording command buffers

### Graph Compilation Phases
1. **PropagateDeviceAffinity**: Assign devices to nodes
2. **AnalyzeDependencies**: Build dependency graph
3. **AllocateResources**: Create Vulkan resources
4. **GeneratePipelines**: Compile graphics pipelines
5. **BuildExecutionOrder**: Topological sort for execution

## Troubleshooting

### Build Errors: "Cannot open Resource.cpp"
**Cause**: Resource.cpp was deleted (now header-only templates)
**Fix**: Run `cmake -B build` to regenerate build files

### Include Errors: "Cannot find RenderGraph/..."
**Cause**: Include paths not configured
**Fix**: Ensure `target_link_libraries` includes `RenderGraphCore` or `RenderGraphNodes`

### PDB File Locking
**Cause**: Parallel compilation writing to same PDB
**Fix**: Already configured with `/FS` flag in CMakeLists.txt

## References

- **Project Docs**: `../documentation/EventBusArchitecture.md`
- **Graph Guide**: `../documentation/GraphArchitecture/render-graph-quick-reference.md`
- **Coding Standards**: `../documentation/cpp-programming-guidelins.md`
