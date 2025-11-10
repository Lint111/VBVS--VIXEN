# VIXEN Documentation Index

**Complete guide to all documentation in the VIXEN project.**

Welcome to the VIXEN documentation. This index organizes all documentation by topic and difficulty level to help you find what you need.

---

## 🚀 Quick Start

**New to VIXEN? Start here:**

1. **[README.md](README.md)** - Project overview, features, build instructions
2. **[memory-bank/projectbrief.md](memory-bank/projectbrief.md)** - High-level goals and scope
3. **[memory-bank/productContext.md](memory-bank/productContext.md)** - Why VIXEN exists, design philosophy
4. **[documentation/GraphArchitecture/00-START-HERE.md](documentation/GraphArchitecture/00-START-HERE.md)** - Introduction to graph-based rendering

---

## 📚 Core Documentation

### Memory Bank (Project Context)
**Essential project knowledge - read these first**

| File | Description |
|------|-------------|
| [projectbrief.md](memory-bank/projectbrief.md) | Project goals, scope, success criteria |
| [productContext.md](memory-bank/productContext.md) | Problem statement, research goals, design philosophy |
| [systemPatterns.md](memory-bank/systemPatterns.md) | 16+ architecture patterns (Typed Node, Resource Variant, EventBus, etc.) |
| [techContext.md](memory-bank/techContext.md) | Technology stack, build system, development environment |
| [activeContext.md](memory-bank/activeContext.md) | Current focus (Phase H - Voxel Infrastructure), recent changes |
| [progress.md](memory-bank/progress.md) | Implementation status, completed systems, roadmap |

---

## 🏗️ Architecture Documentation

### Graph-Based Rendering System
**Core rendering architecture - node-based render graph with compile-time type safety**

| File | Description | Level |
|------|-------------|-------|
| [00-START-HERE.md](documentation/GraphArchitecture/00-START-HERE.md) | Introduction to graph architecture | Beginner |
| [README.md](documentation/GraphArchitecture/README.md) | Graph system overview | Beginner |
| [01-node-system.md](documentation/GraphArchitecture/01-node-system.md) | NodeInstance base class, lifecycle | Intermediate |
| [02-graph-compilation.md](documentation/GraphArchitecture/02-graph-compilation.md) | Compilation phases, topology | Intermediate |
| [03-multi-device.md](documentation/GraphArchitecture/03-multi-device.md) | Multi-GPU support | Advanced |
| [04-caching.md](documentation/GraphArchitecture/04-caching.md) | Resource caching strategies | Intermediate |
| [05-implementation.md](documentation/GraphArchitecture/05-implementation.md) | Implementation details | Advanced |
| [06-examples.md](documentation/GraphArchitecture/06-examples.md) | Usage examples | Beginner |
| [07-cache-aware-batching.md](documentation/GraphArchitecture/07-cache-aware-batching.md) | Batching optimizations | Advanced |
| [08-per-frame-resources.md](documentation/GraphArchitecture/08-per-frame-resources.md) | Frame-in-flight resource management | Intermediate |
| [09-loop-system.md](documentation/GraphArchitecture/09-loop-system.md) | Multi-rate update loops | Intermediate |
| [TypedNodeExample.md](documentation/GraphArchitecture/TypedNodeExample.md) | Creating custom nodes | Beginner |
| [ResourceConfig.md](documentation/GraphArchitecture/ResourceConfig.md) | Slot configuration API | Intermediate |
| [NodeConfig-API-Improvements.md](documentation/GraphArchitecture/NodeConfig-API-Improvements.md) | Advanced config patterns | Advanced |

### Quick References
| File | Description |
|------|-------------|
| [render-graph-quick-reference.md](documentation/GraphArchitecture/render-graph-quick-reference.md) | API quick reference |
| [ResourceVariant-Quick-Reference.md](documentation/GraphArchitecture/ResourceVariant-Quick-Reference.md) | Resource variant system |
| [QUICK-REFERENCE-Semaphore-Architecture.md](documentation/GraphArchitecture/QUICK-REFERENCE-Semaphore-Architecture.md) | Synchronization primitives |

---

## 🎨 Shader Management

### SPIRV Reflection & Automation
**Data-driven shader compilation, descriptor generation, type-safe UBO structs**

| File | Description | Level |
|------|-------------|-------|
| [README.md](documentation/ShaderManagement/README.md) | Shader system overview | Beginner |
| [01-architecture.md](documentation/ShaderManagement/01-architecture.md) | SPIRV reflection architecture | Intermediate |
| [ShaderManagement-Integration-Plan.md](documentation/ShaderManagement/ShaderManagement-Integration-Plan.md) | 6-phase integration roadmap | Intermediate |
| [Shader-PreCompilation-Workflow.md](documentation/ShaderManagement/Shader-PreCompilation-Workflow.md) | Offline shader compilation | Intermediate |
| [ShaderLibraryArchitecture.md](documentation/ShaderManagement/ShaderLibraryArchitecture.md) | Shader library node design | Advanced |

---

## 💾 Caching System (CashSystem)

### Persistent Resource Caching
**9 cachers with async save/load, lazy deserialization**

| File | Description | Level |
|------|-------------|-------|
| [README.md](documentation/CashSystem/README.md) | CashSystem overview | Beginner |
| [01-architecture.md](documentation/CashSystem/01-architecture.md) | Cacher architecture | Intermediate |
| [02-usage-guide.md](documentation/CashSystem/02-usage-guide.md) | Using cachers in code | Beginner |
| [Phase-A-Cacher-Implementation-Plan.md](documentation/CashSystem/Phase-A-Cacher-Implementation-Plan.md) | Implementation roadmap | Advanced |
| [CashSystem-Migration-Status.md](documentation/CashSystem/CashSystem-Migration-Status.md) | Migration tracking | Reference |

---

## 📡 Event System (EventBus)

### Decoupled Node Communication
**Event-driven invalidation, cascade recompilation**

| File | Description | Level |
|------|-------------|-------|
| [README.md](documentation/EventBus/README.md) | EventBus overview | Beginner |
| [EventBusArchitecture.md](documentation/EventBus/EventBusArchitecture.md) | Architecture details | Intermediate |
| [AutoMessageTypeSystem.md](documentation/EventBus/AutoMessageTypeSystem.md) | Auto-incrementing message types | Intermediate |
| [EventBus-ResourceManagement-Integration.md](documentation/EventBus/EventBus-ResourceManagement-Integration.md) | RM integration | Advanced |

---

## 🔬 Research: Voxel Ray Tracing

### Academic Research Platform
**4 pipeline architectures, 180-config test matrix, May 2026 publication target**

### Overview & Planning
| File | Description | Level |
|------|-------------|-------|
| [ResearchPhases-ParallelTrack.md](documentation/ResearchPhases-ParallelTrack.md) | Research roadmap (Phases H-N) | Overview |

### Pipeline Architectures
| File | Description | Level |
|------|-------------|-------|
| [VoxelRayMarch-Integration-Guide.md](documentation/Shaders/VoxelRayMarch-Integration-Guide.md) | Compute shader ray marching (DDA) | Beginner |
| [FragmentRayMarch-Integration-Guide.md](documentation/Shaders/FragmentRayMarch-Integration-Guide.md) | Fragment shader ray marching | Intermediate |
| [HardwareRTDesign.md](documentation/RayTracing/HardwareRTDesign.md) | Hardware ray tracing (VK_KHR) | Advanced |
| [HybridRTX-SurfaceSkin-Architecture.md](documentation/RayTracing/HybridRTX-SurfaceSkin-Architecture.md) | Hybrid RTX + ray marching (Phase N+1) | Advanced |

### Data Structures
| File | Description | Level |
|------|-------------|-------|
| [OctreeDesign.md](documentation/VoxelStructures/OctreeDesign.md) | Sparse voxel octree (baseline) | Intermediate |
| [ECS-Octree-Integration-Analysis.md](documentation/VoxelStructures/ECS-Octree-Integration-Analysis.md) | ECS-optimized octree (Phase N+1) | Advanced |
| [GigaVoxels-CachingStrategy.md](documentation/VoxelStructures/GigaVoxels-CachingStrategy.md) | Streaming voxel architecture | Advanced |

### Testing & Optimization
| File | Description | Level |
|------|-------------|-------|
| [TestScenes.md](documentation/Testing/TestScenes.md) | Cornell Box, Cave, Urban Grid | Intermediate |
| [PerformanceProfilerDesign.md](documentation/Profiling/PerformanceProfilerDesign.md) | Metrics collection, CSV export | Intermediate |
| [BibliographyOptimizationTechniques.md](documentation/Optimizations/BibliographyOptimizationTechniques.md) | 24-paper literature review | Reference |

---

## 🛠️ Build System & Tools

### CMake, Testing, Coverage
**Production-quality build infrastructure**

| File | Description | Level |
|------|-------------|-------|
| [CMAKE_BUILD_OPTIMIZATION.md](documentation/BuildSystem/CMAKE_BUILD_OPTIMIZATION.md) | Build optimizations (Ccache, PCH, Ninja) | Intermediate |
| [BuildingWithoutVulkanSDK.md](documentation/BuildSystem/BuildingWithoutVulkanSDK.md) | Trimmed build mode (headers only) | Advanced |
| [TESTING_PROGRESS_SUMMARY.md](documentation/Testing/TESTING_PROGRESS_SUMMARY.md) | Test coverage status | Reference |
| [COMPREHENSIVE_TEST_PLAN.md](documentation/RenderGraph/COMPREHENSIVE_TEST_PLAN.md) | RenderGraph test suite | Advanced |

---

## 📖 Coding Standards

### C++ Guidelines & Best Practices
**Project coding standards and style**

| File | Description | Level |
|------|-------------|-------|
| [cpp-programming-guidelins.md](documentation/Standards/cpp-programming-guidelins.md) | C++23 coding standards | Essential |
| [Communication Guidelines.md](documentation/Standards/Communication Guidelines.md) | Documentation style | Essential |
| [smart-pointers-guide.md](documentation/Standards/smart-pointers-guide.md) | RAII and memory management | Beginner |

---

## 📦 Component Documentation

### Individual Subsystems
| File | Description |
|------|-------------|
| [ApplicationArchitecture.md](documentation/GraphArchitecture/ApplicationArchitecture.md) | VulkanGraphApplication design |
| [NodeBased_vs_Legacy_Rendering.md](documentation/GraphArchitecture/NodeBased_vs_Legacy_Rendering.md) | Migration from legacy code |
| [Synchronization-Semaphore-Architecture.md](documentation/GraphArchitecture/Synchronization-Semaphore-Architecture.md) | Vulkan sync primitives |
| [RenderGraph_Lifecycle_Schema.md](documentation/RenderGraph_Lifecycle_Schema.md) | Lifecycle hook system |
| [GraphCompilable-Architecture.md](documentation/RenderGraph/GraphCompilable-Architecture.md) | Compilable interface |

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

**Last Updated**: November 2025 (Phase H - Voxel Infrastructure 60% complete)
