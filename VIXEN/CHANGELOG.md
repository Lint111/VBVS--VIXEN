# Changelog

All notable changes to VIXEN will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2025-11-10

### Overview
First public release of VIXEN - Vulkan Interactive eXample Engine. This release represents the completion of Phase G, featuring a production-quality graph-based rendering architecture with compile-time type safety and comprehensive infrastructure systems.

**Status**: Phase G Complete (SlotRole System & Descriptor Binding Refactor)
**Research**: Phase H (Voxel Infrastructure) in progress on separate branch

### Added

#### Core Architecture
- **Graph-Based Rendering System** - Node-based render graph with directed acyclic graph (DAG) execution
- **Typed Node API** - `TypedNode<Config>` with compile-time `In()`/`Out()` slot validation
- **Resource Variant System** - 29+ Vulkan types with macro-based registry, zero-overhead abstractions
- **Event-Driven Invalidation** - EventBus with cascade recompilation (WindowResize → SwapChainInvalidated → Framebuffer rebuild)
- **Protected API Enforcement** - Nodes use high-level typed API, graph manages low-level wiring

#### Infrastructure Systems
- **Persistent Cache System** - 9 cachers with async save/load (SamplerCacher, ShaderModuleCacher, PipelineCacher, etc.)
- **Testing Framework** - 40% coverage, 10 GoogleTest suites, VS Code integration with LCOV visualization
- **Logging System** - ILoggable interface with LOG_TRACE/DEBUG/INFO/WARNING/ERROR macros
- **Lifecycle Hooks** - 14 total hooks (6 graph phases + 8 node phases) for fine-grained control
- **Multi-Rate Loop System** - Fixed timestep accumulator (per-frame, fixed 60Hz, fixed 120Hz)
- **Frame-in-Flight Synchronization** - CPU-GPU pacing with two-tier sync (fences + semaphores)

#### Shader Management (Phases 0-5)
- **SPIRV Reflection** - Automatic descriptor layout generation from shader reflection
- **SDI Generation** - Type-safe UBO struct definitions with content-hash UUID system
- **Data-Driven Pipelines** - Zero hardcoded shader assumptions, all from reflection
- **Descriptor Automation** - Pool sizing, layout creation, binding from SPIRV metadata
- **Push Constants** - Automatic extraction and propagation
- **Vertex Format Extraction** - Dynamic vertex input from SPIRV reflection

#### Node Catalog (19+ Nodes)
- **Core**: WindowNode, DeviceNode, SwapChainNode, RenderPassNode, FramebufferNode
- **Pipeline**: GraphicsPipelineNode, ComputePipelineNode, DescriptorSetNode
- **Resources**: CommandPoolNode, VertexBufferNode, DepthBufferNode, TextureLoaderNode, ShaderLibraryNode
- **Execution**: GeometryRenderNode, ComputeDispatchNode, PresentNode, FrameSyncNode
- **Utility**: ConstantNode, LoopBridgeNode, BoolOpNode

#### Build System
- **CMake Modular Architecture** - 7 libraries (Logger, VulkanResources, EventBus, ShaderManagement, ResourceManagement, RenderGraph, CashSystem)
- **Build Optimizations** - Ccache/sccache support, precompiled headers, Ninja generator, unity builds
- **Testing Infrastructure** - GoogleTest integration, CTest support, coverage reporting
- **Trimmed Build Mode** - Header-only Vulkan build without full SDK

#### Documentation
- **Memory Bank** - 6 files documenting project context, patterns, progress (~200 pages)
- **Architecture Documentation** - 30+ files covering graph system (~800 pages)
- **Component Documentation** - ShaderManagement, CashSystem, EventBus (~330 pages)
- **Standards** - C++23 coding guidelines, communication style, smart pointer guide
- **Documentation Index** - Comprehensive index of 90+ files organized by topic

### Changed

#### Phase G: SlotRole System & Descriptor Binding Refactor
- **SlotRole Bitwise Flags** - Combined `Dependency | Execute` roles for flexible descriptor binding
- **Deferred Descriptor Binding** - Execute phase instead of Compile phase
- **DescriptorSetNode Generalization** - Removed hardcoded MVP/rotation/UBO logic, reduced from ~230 lines to ~80 lines
- **NodeFlags Enum** - Consolidated state management pattern (replaces scattered bool flags)
- **Per-Frame Descriptor Sets** - Generalized binding infrastructure

#### Previous Phase Completions
- **Phase 0.1-0.7**: Synchronization infrastructure (per-frame resources, frame-in-flight, command buffers, loops, present fences)
- **Phase A**: Persistent cache with lazy deserialization
- **Phase B**: Encapsulation via INodeWiring interface
- **Phase C**: Event processing validation
- **Phase F**: Bundle-first organization refactor

### Technical Details

#### Performance Characteristics
- **Node Capacity**: 100-200 nodes per graph
- **Build Time**: Clean 60-90s, Incremental 5-10s (with optimizations)
- **Cache Performance**: CACHE HIT confirmed for shaders, pipelines, samplers
- **Frame Rate**: Target 60 FPS with 4 frames in flight
- **Test Coverage**: 40% coverage across 10 test suites

#### Platform Support
- **OS**: Windows 10/11 (x64)
- **Compiler**: MSVC (Visual Studio 2022+, C++23 required)
- **Graphics API**: Vulkan 1.4.321.1
- **Build System**: CMake 3.21+

#### Design Patterns
- Typed Node Pattern - Compile-time slot validation
- Resource Variant Pattern - Zero-overhead type safety
- Graph-Owns-Resources Pattern - Clear lifetime management
- EventBus Invalidation Pattern - Decoupled node communication
- Handle-Based Access Pattern - O(1) node lookups
- Cleanup Dependency Pattern - Auto-detected dependency-ordered destruction
- Two-Semaphore Synchronization Pattern - GPU-GPU sync without CPU stalls
- Split SDI Architecture Pattern - Generic interface sharing with shader-specific convenience

### Fixed
- Zero Vulkan validation errors
- Descriptor binding issues (moved from Compile to Execute phase)
- Semaphore indexing per Vulkan validation guide
- MessageType collision bug (DeviceMetadataEvent vs CleanupRequestedMessage)
- SPIRV reflection vertex format extraction (removed hardcoded vec4 bug)
- Pipeline layout variable shadowing bug

### Security
- MIT License
- No sensitive data in repository
- RAII throughout (smart pointers, no raw new/delete)
- Const-correctness on member functions

### Known Limitations
- **Manual Descriptor Setup** - Some nodes still create descriptor layouts manually (automated in Phases 4-5)
- **Single-Threaded Execution** - No wave-based parallelism yet
- **No Memory Aliasing** - No transient resource optimization
- **Virtual Dispatch Overhead** - ~2-5ns per call (acceptable <200 nodes)

### Notes
- **Research Track**: Phase H (Voxel Infrastructure) in progress on `claude/phase-h-voxel-infrastructure` branch
- **Target Platform**: Windows-only acceptable for v0.1
- **Code Quality**: Zero warnings in RenderGraph library, professional codebase quality

---

## Future Roadmap

### Phase H: Voxel Infrastructure (In Progress - 60% Complete)
- Sparse voxel octree data structure
- Procedural scene generation (Cornell Box, Cave, Urban Grid)
- GPU upload integration
- VoxelRayMarch compute shader with DDA traversal

### Phase I-N: Research Execution (May 2026 Target)
- 4 ray tracing/marching pipelines (compute shader, fragment shader, hardware RT, hybrid)
- 180-configuration test matrix
- Automated testing framework
- Performance profiling system
- Academic paper submission

### Long-Term Extensions (August 2026 Target)
- Hybrid RTX surface-skin pipeline
- GigaVoxels streaming architecture
- Extended research (270 configurations)
- Journal publication

---

## Acknowledgments

Based on 24 research papers covering voxel rendering, ray tracing, sparse voxel octrees, and GPU optimization techniques.

### Technologies
- Vulkan - Khronos Group
- SPIRV-Reflect - Shader reflection library
- GoogleTest - Testing framework
- glslang - GLSL to SPIRV compiler

---

**Full documentation**: See [DOCUMENTATION_INDEX.md](DOCUMENTATION_INDEX.md) for complete guide to 90+ documentation files.

**Repository**: https://github.com/lioryaari/VIXEN

**License**: MIT License - Copyright (c) 2025 Lior Yaari
