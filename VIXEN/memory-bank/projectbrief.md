# Project Brief

## Project Name
VIXEN - Vulkan Interactive eXample Engine

## Overview
A learning-focused Vulkan graphics engine implementing a **graph-based rendering architecture** on Windows. The project has evolved from chapter-based Vulkan fundamentals to a production-quality render graph system with compile-time type safety, resource variant architecture, and event-driven invalidation.

**NEW (November 2025)**: VIXEN is now being extended as a **voxel ray tracing research platform** for comparative pipeline analysis, targeting academic paper publication (May 2026).

## Current Focus: RenderGraph System
The core architecture is a **node-based render graph** where rendering operations are modular, composable nodes connected into a directed acyclic graph (DAG).

### Key Achievements
1. **Variant Resource System** - 25+ Vulkan types with macro-based registry, zero-overhead type safety
2. **Typed Node API** - `In()`/`Out()` methods with compile-time slot validation
3. **EventBus Integration** - Decoupled invalidation cascade (WindowResize → SwapChainInvalidated → Framebuffer rebuild)
4. **Clean Build State** - Zero warnings, professional codebase quality
5. **Protected API Enforcement** - Nodes use only high-level typed API, graph manages low-level wiring

## Goals

### Core Engine Goals
1. **Production-Quality Architecture** - Industry-standard render graph patterns (comparable to Unity HDRP, Unreal RDG)
2. **Type Safety First** - Compile-time validation eliminates runtime type errors
3. **Extensibility** - Adding new node types requires minimal boilerplate
4. **Performance** - Zero-overhead abstractions, cache-friendly compilation
5. **Learning Platform** - Document architectural decisions for educational value

### Research Goals (NEW - November 2025)
6. **Comparative Analysis** - Measure performance differences between 4 ray tracing/marching pipelines
7. **Academic Contribution** - Publish findings in graphics/rendering conference (May 2026 target)
8. **Reproducible Results** - Automated testing framework for 180 configuration matrix
9. **Open Source Research** - All code, data, and analysis tools publicly available

**Research Question**: How do different Vulkan pipeline architectures (compute shader ray marching, fragment shader ray marching, hardware ray tracing, hybrid) affect rendering performance, GPU bandwidth utilization, and scalability for data-driven voxel rendering?

**Bibliography**: 24 research papers compiled in `C:\Users\liory\Docs\Performance comparison on rendering methods for voxel data.pdf` covering voxel rendering, ray tracing, sparse voxel octrees, and GPU optimization techniques.

## Scope
### Completed Systems ✅
- **Core RenderGraph Infrastructure** - Graph compilation, execution, topology analysis
- **Resource Variant System** - Type-safe `std::variant` replacing manual type-punning
- **Typed Node Base Classes** - `TypedNode<Config>` with macro-generated storage
- **EventBus** - Queue-based event system for node invalidation
- **Node Implementations** - 15+ nodes (Window, SwapChain, DepthBuffer, RenderPass, etc.)
- **Build System** - Modular CMake libraries with incremental compilation

### In Progress 🔨
- **Node Expansion** - Adding GeometryRender, Present, Pipeline nodes
- **Graph Wiring** - Connecting nodes for complete rendering pipeline
- **Architectural Refinement** - Addressing review recommendations (validation, encapsulation)

### Future Enhancements 🚀
- **Parallelization** - Wave-based execution for multi-threaded rendering
- **Resource Aliasing** - Transient resource memory optimization
- **Compiled Execution** - Devirtualized hot path for 1000+ node graphs
- **Memory Budget System** - Allocation tracking and priority-based eviction

## Target Platform
- **Primary**: Windows 10/11 (x64)
- **Build System**: CMake 3.21+ with modular library structure
- **Compiler**: MSVC (Visual Studio 2022+, C++23 required)
- **Graphics API**: Vulkan 1.4.321.1

## Success Criteria
### Architecture ✅
1. ✅ Compile-time type safety throughout resource system
2. ✅ Clean resource ownership model (graph owns, nodes access)
3. ✅ Zero-overhead abstractions (no runtime type penalties)
4. ✅ Event-driven invalidation (decoupled node communication)
5. ✅ Protected API enforcement (nodes use typed API only)

### Code Quality ✅
1. ✅ Zero warnings in RenderGraph library
2. ✅ RAII throughout (smart pointers, no raw `new/delete`)
3. ✅ Const-correctness on member functions
4. ✅ Professional documentation standards

### Functionality (Next Phase)
1. ⏳ Complete rendering pipeline (geometry render → present)
2. ⏳ Event-driven window resize handling
3. ⏳ Node catalog with 20+ production-ready nodes
4. ⏳ Example scenes demonstrating graph capabilities

## Non-Goals
- Cross-platform support (Windows-only acceptable)
- Real-time ray tracing (deferred to future versions)
- Material editing UI (focus on architecture)
- Mobile/console ports

## Design Principles
1. **Radical Conciseness** - Documentation must be brief, precise, actionable
2. **Type Safety First** - Compile-time checking preferred over runtime validation
3. **Single Responsibility** - Classes <200 instructions, functions <20 instructions (relaxed for Vulkan)
4. **Composition Over Inheritance** - Prefer interfaces and templates to deep hierarchies
5. **Documentation as Code** - Architecture documented in memory-bank/, not external wikis

## Constraints
- **Vulkan SDK**: 1.4.321.1 (validation layers required)
- **C++ Standard**: C++23 (concepts, ranges, std::variant required)
- **Coding Guidelines**: cpp-programming-guidelins.md (PascalCase classes, camelCase methods)
- **Platform-Specific Code**: Acceptable for Win32 window management (`VK_USE_PLATFORM_WIN32_KHR`)
