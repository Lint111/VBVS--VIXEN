# VIXEN

**Vulkan-based Industrial-grade eXperimental ENgine for Voxel Ray Tracing**

A production-quality research platform for voxel rendering using modern Vulkan 1.3, featuring a graph-based rendering architecture with compile-time type safety.

---

## Overview

VIXEN is a high-performance voxel rendering engine built on Vulkan 1.3, designed for both production use and voxel ray tracing research. The engine features a sophisticated graph-based rendering architecture comparable to Unity HDRP, Unreal RDG, and Frostbite FrameGraph.

**Key Characteristics:**
- **Graph-Based Rendering**: DAG architecture with automatic dependency resolution
- **Type Safety**: Compile-time type checking via C++23 templates and concepts
- **Research Platform**: ESVO (Efficient Sparse Voxel Octrees) ray tracing implementation
- **Production Quality**: Comprehensive testing, budget-aware resource management, extensive documentation

---

## Key Features

### RenderGraph System
- **Node-based architecture** with 30+ specialized node types (Device, SwapChain, ComputeDispatch, etc.)
- **Unified connection system** supporting Direct, Variadic, and Accumulation connections
- **Compile-time slot validation** via `TypedNode<Config>` template pattern
- **Automatic resource lifetime management** with graph-owned resources
- **Event-driven invalidation** through EventBus for decoupled node communication

### Resource Management
- **Budget-aware allocation** with soft/hard memory limits (DeviceBudgetManager)
- **VMA integration** with DirectAllocator fallback for maximum compatibility
- **Intrusive refcounting** (SharedResource) with deferred destruction
- **GPU memory aliasing** support for transient resource optimization
- **Frame-scoped resource management** (LifetimeScope)

### Voxel Rendering
- **ESVO ray tracing** with compute shader implementation
- **SVO (Sparse Voxel Octree)** data structures optimized for GPU traversal
- **Multi-pipeline comparison** (4 rendering pipelines benchmarked)
- **Procedural voxel generation** with stable delta storage

### Development Infrastructure
- **156+ passing tests** across all libraries (Google Test)
- **Hierarchical logging system** with per-node debug output
- **CMake-based build system** with modern target-based dependencies
- **Comprehensive documentation** (90+ Obsidian docs in `Vixen-Docs/`)

---

## Architecture

### Three Core Pillars

1. **Compile-Time Type Safety**: Template-based node system eliminates runtime type errors
2. **Clear Resource Ownership**: Graph owns resources, nodes access via raw pointers
3. **Event-Driven Invalidation**: EventBus decouples nodes for maintainability

### System Diagram

```
Application Layer
  └─ VulkanGraphApplication
      └─ RenderGraph (orchestrator)
          ├─ NodeRegistry (node types)
          ├─ EventBus (communication)
          ├─ NodeInstance (base class)
          │   └─ TypedNode<Config> (type-safe wrapper)
          │       └─ Concrete Nodes (30+ implementations)
          ├─ Resource System
          │   ├─ ResourceVariant (type-safe variant)
          │   ├─ SharedResource (refcounting)
          │   └─ ResourceBudgetManager (allocation tracking)
          └─ Vulkan Backend
              ├─ VulkanDevice
              ├─ SwapChain
              ├─ Pipeline Management
              └─ Descriptor Sets
```

### Library Structure (17 Libraries)

**Foundation:**
- `Core` - Fundamental types and utilities
- `Logger` - Hierarchical logging with per-component debug output

**Communication:**
- `EventBus` - Event-driven node communication with pre-allocated queue
- `ResourceManagement` - Memory budgets, lifetime scopes, state tracking

**Vulkan:**
- `VulkanResources` - Vulkan object wrappers and RAII
- `CashSystem` - Acceleration structure caching and TLAS management
- `ShaderManagement` - SPIR-V reflection and shader bundle system

**Rendering:**
- `RenderGraph` - Core graph system with 30+ node types

**Voxel:**
- `SVO` - Sparse Voxel Octree implementation
- `VoxelData` - Voxel data structures
- `VoxelComponents` - Component-based voxel system
- `GaiaArchetypes` - ECS archetype system
- `GaiaVoxelWorld` - Voxel world management

**Tools:**
- `Profiler` - Performance metrics and benchmarking
- `MemoryManager` - (deprecated, migrated to ResourceManagement)

See [Vixen-Docs/Libraries/Overview.md](Vixen-Docs/Libraries/Overview.md) for detailed dependency graph.

---

## Technology Stack

- **Language**: C++23 (MSVC 2022)
- **Graphics API**: Vulkan 1.3
- **Build System**: CMake 3.22+
- **Platform**: Windows 11 (primary), Linux (planned)
- **Testing**: Google Test
- **Dependencies**:
  - Vulkan SDK 1.3+
  - GLM (mathematics)
  - STB (image loading)
  - SPIRV-Reflect (shader reflection)
  - nlohmann/json (configuration)
  - Intel TBB (parallel algorithms)
  - Gaia ECS (voxel components)

---

## Building

### Prerequisites

- Windows 11 / Windows 10
- Visual Studio 2022 (MSVC v143)
- CMake 3.22+
- Vulkan SDK 1.3+ (C:/VulkanSDK/ or environment variable)
- Git

### Quick Build

```bash
# Clone repository
git clone <repository-url>
cd VIXEN

# Configure with CMake
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build (parallel, 16 threads)
cmake --build build --config Debug --parallel 16

# Run tests
cd build/libraries/RenderGraph/tests/Debug
./test_*.exe --gtest_brief=1
```

### Build Targets

- **Libraries**: All 17 static libraries (built automatically)
- **Tests**: Per-library test executables (156+ tests)
- **Application**: `VulkanGraphApplication` (main executable)
- **Benchmarks**: `VixenBenchmark` (performance analysis)

See [CLAUDE.md](CLAUDE.md) for detailed build commands and troubleshooting.

---

## Project Structure

```
VIXEN/
├── libraries/              # 17 static libraries
│   ├── RenderGraph/        # Core graph system (largest)
│   ├── SVO/                # Voxel octree implementation
│   ├── ResourceManagement/ # Budget-aware allocation
│   ├── CashSystem/         # Acceleration structure caching
│   └── ...                 # 13 more libraries
├── application/            # VulkanGraphApplication executable
├── shaders/                # GLSL compute/fragment shaders
├── Vixen-Docs/             # Obsidian vault (90+ docs)
│   ├── 00-Index/           # Quick lookup index
│   ├── 01-Architecture/    # System design docs
│   ├── 02-Implementation/  # How-to guides
│   ├── 03-Research/        # ESVO, algorithms, papers
│   ├── 04-Development/     # Standards, testing, logging
│   ├── 05-Progress/        # Session notes, roadmap
│   └── Libraries/          # Per-library documentation
├── tests/                  # Integration tests
├── tools/                  # Python utilities (data analysis)
├── .claude/                # Claude Code configuration
│   └── skills/             # Project-specific skills
├── CMakeLists.txt          # Root CMake configuration
├── CLAUDE.md               # AI assistant instructions
└── README.md               # This file
```

---

## Documentation

VIXEN uses an **Obsidian vault** ([Vixen-Docs/](Vixen-Docs/)) as the primary documentation source.

### Quick Access

- **Start Here**: [Quick-Lookup.md](Vixen-Docs/00-Index/Quick-Lookup.md) - Fast navigation index
- **Architecture**: [Overview.md](Vixen-Docs/01-Architecture/Overview.md) - System design
- **RenderGraph**: [RenderGraph-System.md](Vixen-Docs/01-Architecture/RenderGraph-System.md) - Core graph architecture
- **Libraries**: [Libraries/Overview.md](Vixen-Docs/Libraries/Overview.md) - Library catalog
- **Roadmap**: [Production-Roadmap-2026.md](Vixen-Docs/05-Progress/Production-Roadmap-2026.md) - Development plan

### Documentation Organization

| Folder | Content |
|--------|---------|
| `00-Index/` | Quick lookup, navigation |
| `01-Architecture/` | System design, patterns |
| `02-Implementation/` | How-to guides, tutorials |
| `03-Research/` | ESVO, algorithms, papers |
| `04-Development/` | Standards, testing, workflows |
| `05-Progress/` | Session notes, sprint tracking |
| `Libraries/` | Per-library documentation |
| `templates/` | Documentation templates |

---

## Current Status

**Branch**: `production/sprint-6-timeline-foundation`
**Phase**: Sprint 6.0.1 - Unified Connection System (COMPLETE)
**Status**: ✅ Infrastructure hardening complete, Timeline system foundation in progress

### Recently Completed

**Sprint 5.5: Pre-Allocation Hardening** (16h, ✅ COMPLETE)
- `PreAllocatedQueue<T>` ring buffer for EventBus
- Command buffer pool sizing API
- Deferred destruction pool with ring buffer
- Allocation tracker instrumentation

**Sprint 6.0.1: Unified Connection System** (126h, ✅ COMPLETE)
- Single `Connect()` API for all connection types (Direct, Variadic, Accumulation)
- C++20 concepts for type-safe slot resolution
- ConnectionRule registry with extensible modifier system
- 102 passing tests for connection infrastructure

### In Progress

**Sprint 6: Timeline Foundation** (212h estimated)
- Phase 0: Unified Connection System (✅ COMPLETE)
- Phase 1: MultiDispatchNode (56h, 🟢 IN PROGRESS)
- Phase 2: TaskQueue System (72h, ⏳ PLANNED)
- Phase 3: WaveScheduler (84h, ⏳ PLANNED)

### Test Status

- **Total Tests**: 156+ passing (as of 2026-01-06)
- **ResourceManagement**: 138 tests
- **RenderGraph**: 102 tests (connection system)
- **CashSystem**: 62 tests
- **Coverage**: ~85% for critical paths

---

## Roadmap Highlights

### 2026 Q1: Infrastructure Hardening
- ✅ Resource Manager Integration (Sprint 4)
- ✅ CashSystem Robustness (Sprint 5)
- ✅ Pre-Allocation Hardening (Sprint 5.5)
- ✅ Unified Connection System (Sprint 6.0.1)
- 🟢 Timeline Foundation (Sprint 6)

### 2026 Q2-Q4: Timeline Execution System
- MultiDispatchNode for multi-pass compute
- Wave-based parallel execution
- Graph-in-graph composable pipelines
- Automatic synchronization (barriers/semaphores)
- Multi-GPU distribution

### 2026-2027: GaiaVoxelWorld Physics
- Cellular automata (100M voxels/second)
- Soft body physics (Gram-Schmidt solver)
- GPU procedural generation
- Skin Width SVO optimization
- VR integration (90 FPS target)

### Research Publication
- 📝 Paper draft complete (4-pipeline comparison)
- ⏳ Awaiting feedback before submission
- Target: ACM SIGGRAPH / IEEE Visualization

See [Production-Roadmap-2026.md](Vixen-Docs/05-Progress/Production-Roadmap-2026.md) for complete roadmap (1,440h tracked).

---

## Development Workflow

### Project Rules

VIXEN enforces strict development standards via the `project-rules` skill:

- **Communication**: Maximum signal, minimum noise - no filler phrases
- **Engineering**: Fix root causes, not symptoms - no quick hacks
- **Obsidian-First**: Check documentation vault before code search
- **Logging**: Use hierarchical Logger system, never `std::cout`
- **HacknPlan**: All work tracked in project management system

See [.claude/skills/project-rules/](-.claude/skills/project-rules/) for full rule definitions.

### Branch Strategy

- `main` - Stable production branch
- `production/sprint-X-*` - Sprint-specific feature branches
- Merge to `main` after sprint completion and testing

### Testing Requirements

- All new features must include tests
- Tests must pass before merge
- Integration tests for multi-component features
- Performance tests for critical paths

### Documentation Requirements

- Update Obsidian docs for new features
- Add architectural decisions to `01-Architecture/`
- Update roadmap for completed work
- Session summaries in `05-Progress/`

---

## Contributing

VIXEN is currently a research project. External contributions are not yet accepted, but the codebase is structured for future open-source release.

### Code Style

- C++23 features encouraged (concepts, ranges, etc.)
- Modern CMake (target-based, no global variables)
- RAII resource management (no manual cleanup)
- Prefer `std::expected` over exceptions for error handling
- Absolute paths in tools (Windows: `C:\...` format)

### Commit Messages

Format: `type(scope): description`

Types: `feat`, `fix`, `refactor`, `docs`, `test`, `perf`
Scopes: Library names (`RenderGraph`, `SVO`, `CashSystem`)

Example: `feat(RenderGraph): Add MultiDispatchNode for multi-pass compute`

All commits include co-authorship footer:
```
🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

---

## License

**Proprietary** - All rights reserved. Not licensed for public use or redistribution.

---

## Project Management

- **Issue Tracking**: HacknPlan (Project 230809)
- **Sprint Boards**: 12 active sprints (651780-651790)
- **Time Tracking**: Logged per task via HacknPlan API
- **Design Elements**: Linked to Obsidian vault via glue layer

---

## Performance Characteristics

| Aspect | Current | Target (Timeline System) |
|--------|---------|--------------------------|
| Threading | Single-threaded | Wave-based parallel |
| Node Capacity | 100-200 | 500+ |
| Virtual Dispatch | ~2-5ns per call | Devirtualized for 1000+ nodes |
| Resource Aliasing | None | Transient memory optimization |
| Frame Time | 16ms (60 FPS) | 11ms (90 FPS for VR) |

---

## Links

- **Documentation**: [Vixen-Docs/00-Index/Quick-Lookup.md](Vixen-Docs/00-Index/Quick-Lookup.md)
- **Architecture**: [Vixen-Docs/01-Architecture/Overview.md](Vixen-Docs/01-Architecture/Overview.md)
- **Roadmap**: [Vixen-Docs/05-Progress/Production-Roadmap-2026.md](Vixen-Docs/05-Progress/Production-Roadmap-2026.md)
- **Build Instructions**: [CLAUDE.md](CLAUDE.md)

---

## Acknowledgments

- **Vulkan SDK**: Khronos Group
- **ESVO Algorithm**: Based on research by Laine & Karras (NVIDIA)
- **Gaia ECS**: Richard Biely
- **Development**: Powered by Claude Sonnet 4.5 AI assistant

---

*Last Updated: 2026-01-06*
*Version: Sprint 6.0.1 Complete*
