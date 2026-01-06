# VIXEN - Vulkan Interactive eXample Engine

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.4.321.1-red.svg)](https://www.vulkan.org/)

A production-quality Vulkan graphics engine featuring a **graph-based rendering architecture** with compile-time type safety, event-driven invalidation, and a comprehensive caching system. Currently being extended as a **voxel ray tracing research platform** for comparative pipeline analysis targeting academic publication (May 2026).

## Key Features

### Graph-Based Rendering Architecture
- **Node-Based Render Graph** - Modular, composable rendering operations connected into a directed acyclic graph (DAG)
- **Compile-Time Type Safety** - `TypedNode<Config>` API with `In()`/`Out()` methods eliminates runtime type errors
- **Variant Resource System** - 29+ Vulkan types with macro-based registry, zero-overhead abstractions
- **Event-Driven Invalidation** - Decoupled node communication (WindowResize → SwapChainInvalidated → Framebuffer rebuild)
- **Protected API Enforcement** - Nodes use high-level typed API, graph manages low-level wiring

### Advanced Infrastructure
- **Resource Management** - Budget-aware allocation with VMA integration, intrusive refcounting, frame-scoped lifetimes
- **Persistent Cache System** - 9 cachers with async save/load, lazy deserialization (SamplerCacher, ShaderModuleCacher, PipelineCacher, etc.)
- **Testing Framework** - 156+ passing tests, GoogleTest suites across all libraries
- **Logging System** - Hierarchical logger with per-node debug output (NODE_LOG_* macros)
- **Lifecycle Hooks** - 14 total hooks (6 graph phases + 8 node phases) for fine-grained control
- **Unified Connection System** - Single Connect() API with C++20 concepts for Direct, Variadic, and Accumulation connections
- **Frame-in-Flight Synchronization** - CPU-GPU pacing with two-tier sync (fences + semaphores)

### Shader Management
- **SPIRV Reflection** - Automatic descriptor layout generation from shader reflection
- **SDI Generation** - Type-safe UBO struct definitions with content-hash UUID system
- **Data-Driven Pipelines** - Zero hardcoded shader assumptions, all from reflection
- **Descriptor Automation** - Pool sizing, layout creation, binding from SPIRV metadata
- **Push Constants** - Automatic extraction and propagation

### Research Capabilities (Voxel Ray Tracing)
- **4 Pipeline Architectures** - Compute shader, fragment shader, hardware RT, hybrid implementations
- **Sparse Voxel Octree** - Hybrid pointer-based + brick map with 9:1 compression
- **Test Matrix** - 180 configurations (4 pipelines × 5 resolutions × 3 densities × 3 algorithms)
- **Performance Profiling** - VkQueryPool timestamps, bandwidth monitoring, CSV export
- **Procedural Scenes** - Cornell Box (10% density), Cave System (50%), Urban Grid (90%)

## Quick Start

### Prerequisites
- **Windows 10/11** (x64)
- **Visual Studio 2022+** with C++ support (MSVC compiler)
- **CMake 3.21+**
- **Vulkan SDK 1.4.321.1** (install to `C:/VulkanSDK/1.4.321.1`)

### Build Instructions

```bash
# Clone repository
git clone https://github.com/lioryaari/VIXEN.git
cd VIXEN

# Generate build files
cmake -B build

# Build Debug configuration
cmake --build build --config Debug

# Build Release configuration
cmake --build build --config Release

# Run executable
./binaries/VIXEN.exe
```

### Alternative: Visual Studio

```bash
# Generate VS solution
cmake -B build

# Open in Visual Studio
start build/VIXEN.sln
```

## Architecture Overview

### Render Graph System

```
┌─────────────────────────────────────────┐
│          RenderGraph                    │  ← Graph orchestrator
│  - Compilation phases                   │
│  - Resource ownership                   │
│  - Execution ordering                   │
└──────────────┬──────────────────────────┘
               │
    ┌──────────┼──────────┬────────────────────┐
    │          │          │                    │
    ▼          ▼          ▼                    ▼
┌────────────────┐  ┌──────────┐  ┌──────────────────┐  ┌──────────┐
│  NodeInstance  │  │ Resource │  │  EventBus        │  │ Topology │
│  (Base class)  │  │ (Variant)│  │  (Invalidation)  │  │  (DAG)   │
└───────┬────────┘  └──────────┘  └──────────────────┘  └──────────┘
        │
        ▼
┌───────────────────────┐
│ TypedNode<ConfigType> │  ← Template with compile-time type safety
└───────┬───────────────┘
        │
        ▼
┌───────────────────────┐
│  Concrete Nodes       │  ← SwapChainNode, FramebufferNode, PipelineNode
│  (30+ implementations)│
└───────────────────────┘
```

### Key Design Patterns

#### 1. Typed Node Pattern
```cpp
// Config defines slots with compile-time types
struct MyNodeConfig {
    INPUT_SLOT(ALBEDO, VkImage, SlotMode::SINGLE);
    OUTPUT_SLOT(FRAMEBUFFER, VkFramebuffer, SlotMode::SINGLE);
};

// Node uses typed API
class MyNode : public TypedNode<MyNodeConfig> {
    void Compile() override {
        VkImage albedo = In(MyNodeConfig::ALBEDO);  // Compile-time type check
        Out(MyNodeConfig::FRAMEBUFFER, myFB);       // Compile-time type check
    }
};
```

#### 2. Resource Variant Pattern
```cpp
// Single macro registration
REGISTER_RESOURCE_TYPE(VkImage, ImageDescriptor)

// Auto-generates type-safe access
template<typename T>
T Resource::GetHandle() { return std::get<T>(handleVariant); }
```

#### 3. Event-Driven Invalidation
```cpp
// Node subscribes to events
void SwapChainNode::Setup() override {
    eventBus->Subscribe(EventType::WindowResize, this);
}

// Node handles event, marks dirty, cascades
void SwapChainNode::OnEvent(const Event& event) override {
    if (event.type == EventType::WindowResize) {
        SetDirty(true);
        eventBus->Emit(SwapChainInvalidatedEvent{});  // Cascade
    }
}
```

## Current Status

**Branch**: `production/sprint-6-timeline-foundation` | **Tests**: 156+ passing

### Recently Completed (2026 Q1) ✅
- **Sprint 4: Resource Manager Integration** - Budget-aware allocation, VMA/DirectAllocator, SharedResource refcounting
- **Sprint 5: CashSystem Robustness** - Memory safety, TLAS lifecycle, staging buffer pool, 62 new tests
- **Sprint 5.5: Pre-Allocation Hardening** - EventBus queue, command pools, deferred destruction ring buffers
- **Sprint 6.0.1: Unified Connection System** - Single Connect() API with C++20 concepts, 102 connection tests

### In Progress 🟢
- **Sprint 6 Phase 1: MultiDispatchNode** - Multi-pass compute shader support for Timeline system
- **Phase 2-3: TaskQueue & WaveScheduler** - Wave-based parallel execution

### 2026 Roadmap
- **Q1-Q2**: Timeline Execution System (parallel execution, multi-GPU, automatic synchronization)
- **Q2-Q4**: GaiaVoxelWorld Physics (cellular automata, soft body, GPU procedural generation)
- **Research**: Paper draft complete, awaiting feedback for May 2026 submission

See [Production-Roadmap-2026.md](Vixen-Docs/05-Progress/Production-Roadmap-2026.md) for full roadmap (1,440h tracked).

## Node Catalog (30+ Nodes)

### Core Rendering Nodes
- **WindowNode** - Window surface management
- **DeviceNode** - Vulkan device selection and creation
- **SwapChainNode** - Presentation swapchain management
- **RenderPassNode** - Render pass creation
- **FramebufferNode** - Framebuffer management
- **GraphicsPipelineNode** - Graphics pipeline creation
- **ComputePipelineNode** - Compute pipeline creation
- **DescriptorSetNode** - Descriptor set allocation and binding

### Resource Nodes
- **CommandPoolNode** - Command buffer allocation
- **VertexBufferNode** - Vertex data management
- **DepthBufferNode** - Depth/stencil buffer
- **TextureLoaderNode** - Texture loading and upload
- **ShaderLibraryNode** - SPIRV compilation and reflection

### Execution Nodes
- **GeometryRenderNode** - Geometry rendering pass
- **ComputeDispatchNode** - Generic compute shader dispatch
- **PresentNode** - Swapchain presentation
- **FrameSyncNode** - Frame-in-flight synchronization

### Utility Nodes
- **ConstantNode** - Constant value outputs
- **LoopBridgeNode** - Multi-rate loop integration
- **BoolOpNode** - Boolean logic operations
- **CameraNode** - View/projection matrix generation
- **VoxelGridNode** - 3D voxel grid data structure

## Research Focus: Voxel Ray Tracing

### Research Question
How do different Vulkan ray tracing/marching pipeline architectures affect rendering performance, GPU bandwidth utilization, and scalability for data-driven voxel rendering?

### Four Pipeline Architectures
1. **Compute Shader Ray Marching** - Full GPU parallelism, custom traversal algorithms
2. **Fragment Shader Ray Marching** - Rasterization pipeline, different GPU utilization
3. **Hardware Ray Tracing** - VK_KHR_acceleration_structure, BLAS/TLAS for voxels
4. **Hybrid Pipeline** - RTX for fast initial hit + ray marching for materials

### Test Matrix (180 Configurations)
- **4 pipelines** × **5 resolutions** (512², 1024², 1920×1080, 2560×1440, 3840×2160)
- **3 densities** (10%, 50%, 90%)
- **3 algorithms** (DDA, Empty Space Skip, BlockWalk)

### Test Scenes
- **Cornell Box** (64³, 10% density) - Baseline, simple lighting
- **Perlin Noise Cave** (128³, 50% density) - Organic structures
- **Urban Grid** (256³, 90% density) - Worst-case stress test

### Performance Metrics
- Frame time (CPU)
- GPU time (timestamps)
- Bandwidth (R/W)
- VRAM usage
- Ray throughput
- Voxel traversal efficiency

### Expected Contributions
- First comprehensive Vulkan-specific comparison
- Reproducible results with open-source implementation
- Academic paper (May 2026)
- Extended research with hybrid RTX pipeline (August 2026)

## Documentation

**📚 Primary Documentation**: [Vixen-Docs/](Vixen-Docs/) Obsidian vault with 90+ organized documents

### Quick Links

**Start Here**:
- [Quick Lookup Index](Vixen-Docs/00-Index/Quick-Lookup.md) - Fast navigation
- [Architecture Overview](Vixen-Docs/01-Architecture/Overview.md) - System design
- [RenderGraph System](Vixen-Docs/01-Architecture/RenderGraph-System.md) - Core architecture

**Implementation**:
- [Create a Node](Vixen-Docs/templates/Node-Documentation.md) - Node template
- [Logging Guide](Vixen-Docs/Libraries/Logger.md) - Hierarchical logging
- [Testing Guide](Vixen-Docs/04-Development/Testing.md) - Test framework

**Libraries**:
- [Overview](Vixen-Docs/Libraries/Overview.md) - 17 libraries with dependencies
- [RenderGraph](Vixen-Docs/Libraries/RenderGraph.md) - 30+ nodes
- [ResourceManagement](Vixen-Docs/Libraries/ResourceManagement.md) - Budget-aware allocation
- [CashSystem](Vixen-Docs/Libraries/CashSystem.md) - AS caching

**Progress**:
- [Roadmap 2026](Vixen-Docs/05-Progress/Production-Roadmap-2026.md) - 1,440h plan
- [Sprint 6.0.1](Vixen-Docs/05-Progress/features/Sprint6.0.1-Unified-Connection-System.md) - Latest

**Build**: [CLAUDE.md](CLAUDE.md) - Commands, troubleshooting

## Performance Characteristics

### Current Capacity
- **Node Count**: 100-200 nodes per graph
- **Build Time**: Clean 60-90s, Incremental 5-10s (with optimizations)
- **Cache Performance**: CACHE HIT confirmed for shaders, pipelines, samplers
- **Frame Rate**: Target 60 FPS with 4 frames in flight

### Optimization Features
- **Precompiled Headers** - 2-3× build speedup
- **Persistent Cache** - 9 cachers with lazy deserialization
- **Zero-Overhead Abstractions** - std::variant, templates, no virtual dispatch in hot paths
- **Stateful Command Buffers** - Only re-record when dirty

## Contributing

This project is currently in research phase focusing on voxel ray tracing comparative analysis. Contributions welcome after May 2026 publication.

For questions or collaboration inquiries, please open an issue.

## License

MIT License - see [LICENSE](LICENSE) file for details.

Copyright (c) 2025 Lior Yaari

## Acknowledgments

### Research Bibliography
Based on 24 research papers covering voxel rendering, ray tracing, sparse voxel octrees, and GPU optimization techniques. Key references:

- [1] Nousiainen - Voxel rendering baseline
- [5] Voetter - Vulkan volumetric rendering
- [6] Aleksandrov - SVO baseline architecture
- [16] Derin - BlockWalk empty space skipping
- [22] Molenaar - SVDAG compression

Full bibliography available in project documentation.

### Technologies
- **Vulkan** - Khronos Group
- **SPIRV-Reflect** - Shader reflection library
- **GoogleTest** - Testing framework
- **glslang** - GLSL to SPIRV compiler

## Roadmap

### 2026 Q1: Infrastructure Hardening (✅→🟢)
- ✅ Sprint 4: Resource Manager (192h)
- ✅ Sprint 5: CashSystem Robustness (104h)
- ✅ Sprint 5.5: Pre-Allocation (16h)
- ✅ Sprint 6.0.1: Unified Connections (126h)
- 🟢 Sprint 6: Timeline Foundation (212h, in progress)

### 2026 Q2-Q4: Timeline Execution
- MultiDispatchNode, wave-based parallel execution
- Graph-in-graph composable pipelines
- Automatic synchronization
- Multi-GPU distribution

### 2026-2027: GaiaVoxelWorld Physics
- Cellular automata, soft body physics
- GPU procedural generation, VR integration

### Research
- ✅ Paper draft complete
- ⏳ Awaiting feedback for May 2026 submission

---

**Status**: Active development, research phase, production-quality architecture

**Contact**: [Open an issue for questions or collaboration]
