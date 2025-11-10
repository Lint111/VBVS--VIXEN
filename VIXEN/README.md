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
- **Persistent Cache System** - 9 cachers with async save/load, lazy deserialization (SamplerCacher, ShaderModuleCacher, PipelineCacher, etc.)
- **Testing Framework** - 40% coverage, 10 GoogleTest suites, VS Code integration with LCOV visualization
- **Logging System** - ILoggable interface with LOG_TRACE/DEBUG/INFO/WARNING/ERROR macros
- **Lifecycle Hooks** - 14 total hooks (6 graph phases + 8 node phases) for fine-grained control
- **Multi-Rate Loop System** - Fixed timestep accumulator pattern (per-frame, fixed 60Hz, fixed 120Hz)
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
git clone https://github.com/yourusername/VIXEN.git
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
│  (19+ implementations)│
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

### Completed Systems ✅
- **Phase 0-G** - Core rendering infrastructure (synchronization, loops, descriptor binding)
- **Testing Infrastructure** - 40% coverage, 10 test suites
- **Logging System** - ILoggable interface with macro system
- **Variadic Node System** - Dynamic slot discovery
- **Context System** - Phase-specific typed contexts
- **GraphLifecycleHooks** - 14 lifecycle hooks
- **ShaderManagement Phases 0-5** - SPIRV reflection, SDI generation, descriptor automation
- **CashSystem** - Persistent cache with 9 cachers

### Phase H (In Progress - 60% Complete)
- ✅ CameraNode implementation
- ✅ VoxelGridNode implementation
- ✅ VoxelRayMarch.comp shader (DDA traversal)
- ⏳ Sparse voxel octree data structure (pending)
- ⏳ Procedural scene generation (pending)

### Research Roadmap (Phases I-N)
**Target**: May 2026 academic paper submission

| Phase | Description | Duration | Status |
|-------|-------------|----------|--------|
| Phase H | Voxel Infrastructure | 3-4 weeks | 60% ✅ |
| Phase I | Compute Shader Ray Marching | 4-5 weeks | Pending |
| Phase J | Fragment Shader Ray Marching | 3-4 weeks | Pending |
| Phase K | Hardware Ray Tracing | 6-7 weeks | Pending |
| Phase L | Algorithm Variants | 4-5 weeks | Pending |
| Phase M | Automated Testing Framework | 3-4 weeks | Pending |
| Phase N | Data Analysis & Paper Writing | 4-6 weeks | Pending |

**Total**: 28-31 weeks (May 2026 target)

## Node Catalog (19+ Nodes)

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

### Memory Bank
- `memory-bank/projectbrief.md` - Project overview and goals
- `memory-bank/productContext.md` - Design philosophy and vision
- `memory-bank/systemPatterns.md` - Architecture patterns (16+ patterns)
- `memory-bank/techContext.md` - Technology stack and setup
- `memory-bank/activeContext.md` - Current focus and recent changes
- `memory-bank/progress.md` - Implementation status

### Technical Documentation
- `documentation/cpp-programming-guidelins.md` - C++ coding standards
- `documentation/Communication Guidelines.md` - Project communication style
- `documentation/GraphArchitecture/` - 20+ render graph docs
- `documentation/ShaderManagement/` - SPIRV reflection, SDI generation
- `documentation/RayTracing/` - Hardware RT design (~150 pages)
- `documentation/VoxelStructures/` - Octree design, GigaVoxels (~115 pages)
- `documentation/Testing/` - Test scenes, automated testing
- `documentation/Profiling/` - Performance profiling design

### Setup Guides
- `docs/VS_CODE_TESTING_SETUP.md` - Testing framework setup (~800 pages)
- `docs/TEST_COVERAGE.md` - Coverage analysis (~400 pages)
- `docs/CMAKE_BUILD_OPTIMIZATION.md` - Build optimizations (~600 pages)

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

Copyright (c) 2025 [Your Name/Organization]

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

### Short-Term (Phase H, by December 2025)
- Complete sparse voxel octree implementation
- Implement procedural scene generators
- GPU upload integration
- Traversal utilities

### Mid-Term (Phases I-N, by May 2026)
- Implement 4 ray tracing/marching pipelines
- Automated testing framework (180 configurations)
- Performance profiling system
- Research paper submission

### Long-Term (Phases N+1, N+2, by August 2026)
- Hybrid RTX surface-skin pipeline
- GigaVoxels streaming (128× memory reduction)
- Extended research (270 configurations)
- Journal publication

---

**Status**: Active development, research phase, production-quality architecture

**Contact**: [Open an issue for questions or collaboration]
