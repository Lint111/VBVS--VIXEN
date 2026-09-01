# VIXEN Documentation Index

**Complete guide to all documentation in the VIXEN project.**

Welcome to the VIXEN documentation. This index organizes documentation by topic and difficulty
level to help you find what you need.

> **Status (2026-09):** VIXEN is evolving from a voxel ray-tracing research platform into a
> reusable, moddable **game render engine** (consumed by the *Undertow* game), with an
> **SDF/Recipe/CSG** procedural-content codegen system in active development. Voxel authoring Inc1,
> compiler-emitted SIMD4 materialization, and raster-proxy B1 are landed; data-movement and
> multicore-dispatch direction work is current. The research track
> is feature-complete. The **canonical** docs are the Obsidian vault at
> [`VIXEN/Vixen-Docs/`](VIXEN/Vixen-Docs/); the `documentation/` links below are legacy reference,
> partially superseded by the vault.

---

## 🚀 Quick Start

**New to VIXEN? Start here:**

1. **[README.md](README.md)** - Project overview, features, build instructions
2. **[memory-bank/projectbrief.md](VIXEN/memory-bank/projectbrief.md)** - High-level goals and scope
3. **[memory-bank/productContext.md](VIXEN/memory-bank/productContext.md)** - Why VIXEN exists, design philosophy
4. **[RenderGraph system](VIXEN/Vixen-Docs/01-Architecture/RenderGraph-System.md)** - Introduction to graph-based rendering

---

## 📚 Core Documentation

### Memory Bank (Project Context)
**Essential project knowledge - read these first**

| File | Description |
|------|-------------|
| [projectbrief.md](VIXEN/memory-bank/projectbrief.md) | Project goals, scope, success criteria |
| [productContext.md](VIXEN/memory-bank/productContext.md) | Problem statement, research goals, design philosophy |
| [systemPatterns.md](VIXEN/memory-bank/systemPatterns.md) | 16+ architecture patterns (Typed Node, Resource Variant, EventBus, etc.) |
| [techContext.md](VIXEN/memory-bank/techContext.md) | Technology stack, build system, development environment |
| [activeContext.md](VIXEN/memory-bank/activeContext.md) | Current focus and recent changes |
| [progress.md](VIXEN/memory-bank/progress.md) | Implementation status, completed systems, roadmap |

---

## 🏗️ Architecture Documentation

### Graph-Based Rendering System
**Core rendering architecture - node-based render graph with compile-time type safety**

| File | Description | Level |
|------|-------------|-------|
| [RenderGraph system](VIXEN/Vixen-Docs/01-Architecture/RenderGraph-System.md) | Node graph, lifecycle, and compilation | Beginner–Advanced |
| [RenderGraph library](VIXEN/Vixen-Docs/Libraries/RenderGraph.md) | Library overview and API surface | Beginner–Advanced |
| [RenderGraph implementation](VIXEN/Vixen-Docs/02-Implementation/Overview.md) | Engine implementation notes | Advanced |

### Quick References
| File | Description |
|------|-------------|
| [RenderGraph library](VIXEN/Vixen-Docs/Libraries/RenderGraph.md) | API quick reference |
| [Vulkan resources](VIXEN/Vixen-Docs/Libraries/VulkanResources.md) | Resource and synchronization primitives |

---

## 🎨 Shader Management

### SPIRV Reflection & Automation
**Data-driven shader compilation, descriptor generation, type-safe UBO structs**

| File | Description | Level |
|------|-------------|-------|
| [Shader management library](VIXEN/Vixen-Docs/Libraries/ShaderManagement.md) | Shader system overview | Beginner–Advanced |
| [Shader implementation](VIXEN/Vixen-Docs/02-Implementation/Shaders.md) | SPIR-V reflection and shader implementation | Intermediate |

---

## 💾 Caching System (CashSystem)

### Persistent Resource Caching
**9 cachers with async save/load, lazy deserialization**

| File | Description | Level |
|------|-------------|-------|
| [CashSystem library](VIXEN/Vixen-Docs/Libraries/CashSystem.md) | Cache architecture and usage | Beginner–Advanced |

---

## 📡 Event System (EventBus)

### Decoupled Node Communication
**Event-driven invalidation, cascade recompilation**

| File | Description | Level |
|------|-------------|-------|
| [EventBus library](VIXEN/Vixen-Docs/Libraries/EventBus.md) | Event-driven invalidation and messaging | Beginner–Advanced |

---

## 🔬 Research: Voxel Ray Tracing *(heritage — feature-complete)*

### Origin: Academic Research Platform
**4 pipeline architectures, 180-config test matrix. Research feature-complete and paper drafted;
the reusable voxel/RT systems (SVO, ray-marching, hardware RT, profiling) carry forward into the
game-engine track.**

### Overview & Planning
| File | Description | Level |
|------|-------------|-------|
| [Research overview](VIXEN/Vixen-Docs/03-Research/Overview.md) | Research roadmap and status | Overview |

### Pipeline Architectures
| File | Description | Level |
|------|-------------|-------|
| [Ray marching](VIXEN/Vixen-Docs/02-Implementation/Ray-Marching.md) | Compute shader ray marching | Beginner–Advanced |
| [Hardware RT](VIXEN/Vixen-Docs/03-Research/Hardware-RT.md) | Hardware ray tracing (VK_KHR) | Advanced |
| [Hybrid RTX](VIXEN/Vixen-Docs/03-Research/Hybrid-RTX-SurfaceSkin.md) | Hybrid RTX + ray marching | Advanced |

### Data Structures
| File | Description | Level |
|------|-------------|-------|
| [SVO system](VIXEN/Vixen-Docs/02-Implementation/SVO-System.md) | Sparse voxel octree and streaming | Intermediate–Advanced |
| [ECS octree research](VIXEN/Vixen-Docs/03-Research/ECS-Octree-Integration.md) | ECS-optimized octree | Advanced |
| [GigaVoxels research](VIXEN/Vixen-Docs/03-Research/GigaVoxels-Streaming.md) | Streaming voxel architecture | Advanced |

### Testing & Optimization
| File | Description | Level |
|------|-------------|-------|
| [Testing](VIXEN/Vixen-Docs/04-Development/Testing.md) | Test scenes and test workflow | Intermediate |
| [Profiler library](VIXEN/Vixen-Docs/Libraries/Profiler.md) | Metrics collection and profiling | Intermediate |
| [Optimization bibliography](VIXEN/Vixen-Docs/03-Research/Optimization-Bibliography.md) | Research literature review | Reference |

---

## 🛠️ Build System & Tools

### CMake, Testing, Coverage
**Production-quality build infrastructure**

| File | Description | Level |
|------|-------------|-------|
| [Build system](VIXEN/Vixen-Docs/04-Development/Build-System.md) | Build configuration and workflow | Intermediate |
| [Testing](VIXEN/Vixen-Docs/04-Development/Testing.md) | Test workflow and coverage status | Reference |
| [RenderGraph test plan](VIXEN/Vixen-Docs/04-Development/RenderGraph-Test-PDB-Consolidation-Plan-2026-07.md) | RenderGraph test suite | Advanced |

---

## 📖 Coding Standards

### C++ Guidelines & Best Practices
**Project coding standards and style**

| File | Description | Level |
|------|-------------|-------|
| [Coding standards](VIXEN/Vixen-Docs/04-Development/Coding-Standards.md) | C++ coding standards and documentation style | Essential |

---

## 📦 Component Documentation

### Individual Subsystems
| File | Description |
|------|-------------|
| [RenderGraph system](VIXEN/Vixen-Docs/01-Architecture/RenderGraph-System.md) | VulkanGraphApplication and lifecycle design |
| [RenderGraph system](VIXEN/Vixen-Docs/01-Architecture/RenderGraph-System.md) | Node graph and synchronization design |
| [Vulkan resources](VIXEN/Vixen-Docs/Libraries/VulkanResources.md) | Vulkan resource and synchronization primitives |
| [RenderGraph system](VIXEN/Vixen-Docs/01-Architecture/RenderGraph-System.md) | Lifecycle and compilable-interface design |

---

## 🗄️ Archive (Historical)

### Legacy Documentation
**Kept for reference, superseded by newer docs**

Located in `documentation/archive/`:
- `render-graph-architecture.md` - Original graph design (superseded by GraphArchitecture/)
- `Cleanup-Architecture.md` - Old cleanup system (superseded by dependency-ordered cleanup)
- `CashSystem-Integration.md` - Initial integration notes (superseded by CashSystem/README.md)
- `EventBus-Implementation-Checklist.md` - Old checklist (completed)
- Various migration and refactoring notes from October-November 2025

---

## 📊 Documentation Metrics

### By Category
- **Memory Bank**: 6 files (~200 pages)
- **Graph Architecture**: 30+ files (~800 pages)
- **Shader Management**: 5 files (~150 pages)
- **CashSystem**: 5 files (~100 pages)
- **EventBus**: 4 files (~80 pages)
- **Research (Voxel)**: 8 files (~660 pages)
- **Build System**: 3 files (~700 pages)
- **Standards**: 3 files (~100 pages)
- **Testing**: 2 files (~400 pages)
- **Archive**: 20+ files (historical)

**Total**: ~90+ active documentation files, ~3,200 pages

---

## 🎯 Recommended Reading Paths

### For New Contributors
1. README.md
2. memory-bank/projectbrief.md
3. memory-bank/systemPatterns.md
4. documentation/GraphArchitecture/00-START-HERE.md
5. documentation/Standards/cpp-programming-guidelins.md
6. documentation/GraphArchitecture/TypedNodeExample.md

### For Understanding Architecture
1. memory-bank/systemPatterns.md (16+ patterns)
2. documentation/GraphArchitecture/01-node-system.md
3. documentation/GraphArchitecture/02-graph-compilation.md
4. documentation/EventBus/EventBusArchitecture.md
5. documentation/ShaderManagement/01-architecture.md
6. documentation/CashSystem/01-architecture.md

### For Research Context
1. memory-bank/productContext.md
2. documentation/ResearchPhases-ParallelTrack.md
3. documentation/VoxelStructures/OctreeDesign.md
4. documentation/Testing/TestScenes.md
5. documentation/Profiling/PerformanceProfilerDesign.md
6. documentation/Optimizations/BibliographyOptimizationTechniques.md

### For Implementation Work
1. documentation/GraphArchitecture/TypedNodeExample.md
2. documentation/GraphArchitecture/ResourceConfig.md
3. documentation/CashSystem/02-usage-guide.md
4. documentation/ShaderManagement/ShaderManagement-Integration-Plan.md
5. documentation/Standards/cpp-programming-guidelins.md

---

## 🔍 Finding Documentation

### By Topic
Use this index to find documentation by topic. Press `Ctrl+F` (or `Cmd+F` on Mac) to search for keywords.

### By Component
- **RenderGraph**: `documentation/GraphArchitecture/` and `documentation/RenderGraph/`
- **Shaders**: `documentation/ShaderManagement/` and `documentation/Shaders/`
- **Caching**: `documentation/CashSystem/`
- **Events**: `documentation/EventBus/`
- **Research**: `documentation/VoxelStructures/`, `documentation/RayTracing/`, `documentation/Testing/`
- **Build**: `documentation/BuildSystem/`

### By File Type
- **README.md files**: Overview documents in each subdirectory
- **00-START-HERE.md**: Entry points for complex topics
- **Quick Reference**: Fast lookups for APIs and patterns
- **Implementation Plans**: Roadmaps and checklists
- **Architecture docs**: Deep dives into system design

---

## 📝 Documentation Standards

All documentation follows the **Communication Guidelines** (see `documentation/Standards/Communication Guidelines.md`):

- **Radical Conciseness** - Maximum signal, minimum noise
- **Structured Format** - Lists, tables, code blocks over prose
- **Technical Focus** - Facts and implementation details
- **Professional Tone** - No marketing language or superlatives

---

## 🤝 Contributing

When adding new documentation:
1. Follow the Communication Guidelines
2. Update this index file
3. Add a README.md to new subdirectories
4. Use descriptive filenames (e.g., `VoxelRayMarch-Integration-Guide.md`)
5. Include difficulty level tags: Beginner, Intermediate, Advanced

---

## 📧 Questions?

- **Issues**: Open an issue on GitHub
- **Architecture questions**: See `memory-bank/systemPatterns.md`
- **Build problems**: See `documentation/BuildSystem/`
- **Research inquiries**: See `memory-bank/productContext.md`

---

**Last Updated**: September 2026 — game-engine-library track active; SDF/Recipe/CSG in development;
voxel ray-tracing research feature-complete. Canonical docs: [`VIXEN/Vixen-Docs/`](VIXEN/Vixen-Docs/).

> Note: legacy `documentation/` and `memory-bank/` content is stored under `VIXEN/`; the links
> above are rooted accordingly. Use the canonical vault above for current architecture/status.
