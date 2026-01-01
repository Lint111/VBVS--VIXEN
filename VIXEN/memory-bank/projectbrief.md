# Project Brief

## Project Name
VIXEN - Vulkan Interactive eXample Engine

## Overview
A learning-focused Vulkan graphics engine implementing a **graph-based rendering architecture** on Windows. The project has evolved from chapter-based Vulkan fundamentals to a production-quality render graph system with compile-time type safety, resource variant architecture, and event-driven invalidation.

**STATUS (January 2026)**: VIXEN has entered **PRODUCTION PHASE** with a comprehensive 11-sprint development roadmap totaling 1,422 hours of planned work. Research paper complete (V4), now tracked in HacknPlan project 230809 with 107 tasks across boards 651780-651790.

## Current Focus: Production Development
The core architecture is a **node-based render graph** where rendering operations are modular, composable nodes connected into a directed acyclic graph (DAG). Active development follows the **Production-Roadmap-2026.md** master document.

### Key Achievements
1. **Variant Resource System** - 29+ Vulkan types with macro-based registry, zero-overhead type safety
2. **Typed Node API** - `In()`/`Out()` methods with compile-time slot validation
3. **EventBus Integration** - Decoupled invalidation cascade (WindowResize → SwapChainInvalidated → Framebuffer rebuild)
4. **Clean Build State** - Zero warnings, professional codebase quality
5. **Protected API Enforcement** - Nodes use only high-level typed API, graph manages low-level wiring
6. **Testing Infrastructure** - 40% coverage, 10 test suites, VS Code integration with LCOV visualization
7. **Logging System** - ILoggable interface with LOG_*/NODE_LOG_* macros, namespace-independent
8. **Variadic Node System** - Dynamic slot discovery, ConnectVariadic API for runtime slot arrays
9. **Context System** - Phase-specific typed contexts (SetupContext, CompileContext, ExecuteContext, CleanupContext)
10. **Lifecycle Hooks** - 6 graph phases + 8 node phases = 14 total hooks for fine-grained control
11. **SlotRole Bitwise Flags** - Dependency | Execute combined roles for flexible descriptor binding
12. **Phase G Complete** - Descriptor binding refactored, generalized architecture, zero validation errors

## Goals

### Core Engine Goals
1. **Production-Quality Architecture** - Industry-standard render graph patterns (comparable to Unity HDRP, Unreal RDG)
2. **Type Safety First** - Compile-time validation eliminates runtime type errors
3. **Extensibility** - Adding new node types requires minimal boilerplate
4. **Performance** - Zero-overhead abstractions, cache-friendly compilation
5. **Learning Platform** - Document architectural decisions for educational value

### Research Goals (COMPLETE - December 2025)
6. **Comparative Analysis** - COMPLETE: 1,125 tests across 6 GPUs comparing 4 pipelines
7. **Academic Contribution** - Paper draft V4 complete, awaiting feedback (Sprints 2.1-2.3)
8. **Reproducible Results** - Automated 180-configuration test matrix COMPLETE
9. **Open Source Research** - All code, data, and analysis tools publicly available

**Research Paper Status**: V4 draft complete with 1,125 benchmark tests across 6 GPUs. HW RT achieves 2.1-3.6x speedup over compute. 3 of 5 original hypotheses contradicted by data.

### Production Goals (NEW - January 2026)
10. **Timeline Execution System** - Multi-dispatch, wave scheduling, graph-in-graph composition (Q2-Q4 2026)
11. **Multi-GPU Support** - Distribute work across multiple GPUs with automatic load balancing
12. **GaiaVoxelWorld Physics** - 100M voxel physics simulation with VR integration (Q1 2026 - Q1 2027)
13. **Infrastructure Hardening** - Memory safety, build optimization, CashSystem robustness

## Scope
### Completed Systems ✅
- **Core RenderGraph Infrastructure** - Graph compilation, execution, topology analysis
- **Resource Variant System** - Type-safe `std::variant` replacing manual type-punning (29+ types)
- **Typed Node Base Classes** - `TypedNode<Config>` with macro-generated storage
- **EventBus** - Queue-based event system for node invalidation, auto-incrementing message types
- **Node Implementations** - 19+ nodes (Window, SwapChain, DepthBuffer, RenderPass, FrameSync, LoopBridge, BoolOp, etc.)
- **Build System** - Modular CMake libraries with incremental compilation, PCH support
- **Testing Framework** - GoogleTest, 40% coverage, VS Code integration, LCOV visualization
- **Logging Infrastructure** - ILoggable interface, LOG_* macros, subsystem integration
- **Variadic Nodes** - VariadicTypedNode base class, dynamic slot arrays
- **Context System** - Phase-specific typed contexts (Setup/Compile/Execute/Cleanup)
- **Lifecycle Hooks** - GraphLifecycleHooks with 14 hooks (6 graph + 8 node phases)
- **ShaderManagement** - SPIRV reflection, SDI generation, descriptor automation (Phases 0-5)
- **CashSystem** - 9 cachers with persistent cache, lazy deserialization, async save/load
- **Synchronization** - Frame-in-flight, two-tier sync (fences/semaphores), present fences
- **Phase G Complete** - SlotRole bitwise flags, deferred descriptor binding, generalized architecture

### In Progress 🔨 (Production Sprints)
- **Sprint 4: Resource Manager Integration** - Board 651780 (15 tasks, 192h)
- **Sprint 5: CashSystem Robustness** - Board 651783 (21 tasks, 148h)
- See `Vixen-Docs/05-Progress/Production-Roadmap-2026.md` for complete sprint breakdown

### Completed Research Phases ✅
- **Phase K: Hardware Ray Tracing** - COMPLETE (December 2025)
- **Phase L: Data Pipeline** - COMPLETE (1,125 tests collected)
- **Research Paper V4** - COMPLETE (awaiting feedback)
- **Sprints 1.1, 1.2, 2.1** - COMPLETE (Infrastructure Hardening + Benchmark Data)
- **Sprint 2.2** - DEFERRED (Hybrid Pipeline not required for initial publication)
- **Sprint 2.3** - AWAITING FEEDBACK (Paper submission pending review)

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
