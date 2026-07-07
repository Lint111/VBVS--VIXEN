# Changelog

All notable changes to VIXEN will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Sparse-Mip ESVO LOD** (Inc1 + Inc2) — per-level filtered mip samples on the ESVO so a
  distant/non-resident subtree still shades correctly, plus partial/streamed brick-pool uploads
  gated by screen-space resolvability, frustum containment, and a CPU-side occlusion test against
  already-resident trees. Measured ~170-220x bandwidth reduction at the mechanism's all-or-nothing
  extreme endpoint, and a realistic 3.2x/68.8% reduction on a mixed near/far scene through the live
  residency trigger. See `Vixen-Docs/01-Architecture/Sparse-Mip-ESVO-LOD-{Direction,Inc1-Plan,
  Inc2-Plan}-2026-07.md`.
- **Tiered ESVO — observer-relative addressing, Inc1** — `TierAddress` (a short hop-chain identity
  spanning voxel-cm to galaxy scale across 5 tiers) and a `SkyProjectionNode` that composites
  direction+magnitude sky points over the existing voxel render, evaluated at the address level with
  no ray-marching. First slice of the nested-tree/observer-addressing epic supporting an
  observation-post fleet-detection mechanic; the tier-crossing traversal-restart machinery remains a
  future increment. See `Vixen-Docs/01-Architecture/Tiered-ESVO-{Observer-Addressing-Design,
  Inc1-Plan}-2026-07.md`.

### Documentation
- Rewrote the repository `README.md` and `VIXEN/README.md` to reflect the pivot from voxel
  ray-tracing research platform to reusable, moddable game render engine (for *Undertow*) with the
  SDF/Recipe/CSG procedural-content codegen system. De-duplicated the two READMEs (root = overview,
  engine = build/layout quick reference).
- Corrected `VIXEN/DOCUMENTATION_INDEX.md` (removed false "Phase K/L COMPLETE" status) and refreshed
  the root `DOCUMENTATION_INDEX.md`; both now point to `Vixen-Docs/` as canonical.
- `libraries/README.md` now documents all 14 libraries (was 6).

### Changed
- Archived finished/superseded docs (July 2026 cleanup): session transcripts, feature proposals,
  MCP-dev notes, and completed Sprint4–6.5 logs moved to `Vixen-Docs/_archive/2026-07/`; root
  planning/review docs and the resolved deletion-incident post-mortems moved to
  `VIXEN/archive/2026-07-cleanup/`.

### Removed
- Deleted loose build spew from the repo root: `bash.exe.stackdump`, stray `comp.spv` / `rchit.spv`.
- Untracked regeneratable SPIR-V build copies under `generated/` and stale Python bytecode; the
  source-referenced SDI headers (`generated/sdi/*.h`) and runtime shader assets (`shaders/*.spv`)
  remain tracked. Added `.gitignore` rules for `*.stackdump`, `generated/*.spv`, and `__pycache__/`.

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
