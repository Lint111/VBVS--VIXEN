# Documentation Index

Comprehensive navigation for 125+ documentation files (~3,200 pages). Project overview, architecture guides, implementation documentation, research papers, and testing infrastructure.

## Quick Start (Essential Reading)

**For first-time contributors, read in order**:

1. [CLAUDE.md](CLAUDE.md) - AI assistant guidelines, communication style, coding standards
2. [memory-bank/projectbrief.md](memory-bank/projectbrief.md) - Project goals, scope, success criteria
3. [memory-bank/systemPatterns.md](memory-bank/systemPatterns.md) - Architecture patterns, component design
4. [memory-bank/activeContext.md](memory-bank/activeContext.md) - Current focus, recent decisions
5. [memory-bank/progress.md](memory-bank/progress.md) - Implementation status, what's done/left

## Memory Bank (Project Context)

Critical files for project understanding and status tracking. Read these first in new sessions.

| File | Purpose | Last Updated |
|------|---------|--------------|
| [memory-bank/projectbrief.md](memory-bank/projectbrief.md) | Project goals, scope, Phase H Week 2 COMPLETE | Ongoing |
| [memory-bank/productContext.md](memory-bank/productContext.md) | Why project exists, design philosophy, learning focus | Ongoing |
| [memory-bank/systemPatterns.md](memory-bank/systemPatterns.md) | Architecture patterns, component design, SlotRole system | Ongoing |
| [memory-bank/techContext.md](memory-bank/techContext.md) | Tech stack (C++23, CMake, Vulkan 1.4), Windows-only, build system | Ongoing |
| [memory-bank/activeContext.md](memory-bank/activeContext.md) | Current work focus, active decisions, recent challenges | Ongoing |
| [memory-bank/progress.md](memory-bank/progress.md) | What's done (Phase 0-G complete), what's in progress (Phase H), roadmap | Ongoing |
| [memory-bank/codeQualityPlan.md](memory-bank/codeQualityPlan.md) | Code review standards, refactoring priority, technical debt management | Ongoing |

## Architecture & Render Graph

Complete documentation for the node-based render graph system. **Start with 00-START-HERE.md**.

### Core Documentation

| File | Purpose |
|------|---------|
| [documentation/GraphArchitecture/00-START-HERE.md](documentation/GraphArchitecture/00-START-HERE.md) | Entry point for graph architecture learning |
| [documentation/GraphArchitecture/README.md](documentation/GraphArchitecture/README.md) | Graph architecture overview, 14 lifecycle hooks |
| [documentation/GraphArchitecture/01-node-system.md](documentation/GraphArchitecture/01-node-system.md) | Node creation, typed node API, In()/Out() slots |
| [documentation/GraphArchitecture/02-graph-compilation.md](documentation/GraphArchitecture/02-graph-compilation.md) | Graph compilation process, DAG validation, dependency resolution |
| [documentation/GraphArchitecture/03-multi-device.md](documentation/GraphArchitecture/03-multi-device.md) | Multi-GPU rendering, cross-device synchronization |
| [documentation/GraphArchitecture/04-caching.md](documentation/GraphArchitecture/04-caching.md) | CashSystem integration, cached resource types |
| [documentation/GraphArchitecture/05-implementation.md](documentation/GraphArchitecture/05-implementation.md) | Low-level implementation details, wiring, execution |
| [documentation/GraphArchitecture/06-examples.md](documentation/GraphArchitecture/06-examples.md) | Complete example graphs, tutorial nodes |
| [documentation/GraphArchitecture/07-cache-aware-batching.md](documentation/GraphArchitecture/07-cache-aware-batching.md) | Optimization: cache-friendly node batching |
| [documentation/GraphArchitecture/08-per-frame-resources.md](documentation/GraphArchitecture/08-per-frame-resources.md) | Triple-buffering, frame-in-flight resource management |
| [documentation/GraphArchitecture/09-loop-system.md](documentation/GraphArchitecture/09-loop-system.md) | Loop nodes, LoopBridge, multi-iteration rendering |

### Architecture & System Design

| File | Purpose |
|------|---------|
| [documentation/RenderGraph-Architecture-Overview.md](documentation/RenderGraph-Architecture-Overview.md) | High-level RenderGraph design, 6 graph phases + 8 node phases |
| [documentation/GraphArchitecture/ApplicationArchitecture.md](documentation/GraphArchitecture/ApplicationArchitecture.md) | Application-level architecture, integration patterns |
| [documentation/GraphArchitecture/ResourceConfig.md](documentation/GraphArchitecture/ResourceConfig.md) | Resource configuration system, descriptor binding |
| [documentation/GraphArchitecture/ResourceStateManagement.md](documentation/GraphArchitecture/ResourceStateManagement.md) | Resource state transitions, validation |
| [documentation/GraphArchitecture/ResourceHandleVariant-Support.md](documentation/GraphArchitecture/ResourceHandleVariant-Support.md) | ResourceHandle variant system, 29+ Vulkan types |
| [documentation/GraphArchitecture/ResourceVariant-Quick-Reference.md](documentation/GraphArchitecture/ResourceVariant-Quick-Reference.md) | Quick reference for resource variant types |

### Node & Type System

| File | Purpose |
|------|---------|
| [documentation/GraphArchitecture/TypedNodeExample.md](documentation/GraphArchitecture/TypedNodeExample.md) | Step-by-step: creating a typed node with compile-time validation |
| [documentation/GraphArchitecture/PolymorphicSlotSystem-Implementation-Status.md](documentation/GraphArchitecture/PolymorphicSlotSystem-Implementation-Status.md) | SlotRole bitwise flags, Dependency\|Execute roles |
| [documentation/GraphArchitecture/ArrayTypeValidation-Implementation.md](documentation/GraphArchitecture/ArrayTypeValidation-Implementation.md) | Variadic slot arrays, dynamic slot discovery |
| [documentation/GraphArchitecture/NodeBased_vs_Legacy_Rendering.md](documentation/GraphArchitecture/NodeBased_vs_Legacy_Rendering.md) | Comparison: graph-based vs traditional rendering |
| [documentation/GraphArchitecture/NodeConfig-API-Improvements.md](documentation/GraphArchitecture/NodeConfig-API-Improvements.md) | NodeConfig API design, configuration patterns |

### Graph Lifecycle & Compilation

| File | Purpose |
|------|---------|
| [documentation/RenderGraph_Lifecycle_Schema.md](documentation/RenderGraph_Lifecycle_Schema.md) | Complete lifecycle schema with all 14 phases |
| [documentation/GraphArchitecture/GraphCompilable-Architecture.md](documentation/GraphArchitecture/GraphCompilable-Architecture.md) | Graph compilation algorithm, topology analysis |
| [documentation/GraphArchitecture/PartialCleanupStrategy.md](documentation/GraphArchitecture/PartialCleanupStrategy.md) | Efficient resource cleanup, per-node invalidation |
| [documentation/GraphArchitecture/DELIVERY-SUMMARY.md](documentation/GraphArchitecture/DELIVERY-SUMMARY.md) | Graph architecture delivery summary |
| [documentation/GraphArchitecture/DOCUMENTATION-SUMMARY.md](documentation/GraphArchitecture/DOCUMENTATION-SUMMARY.md) | Documentation index for graph subsystem |

## Synchronization & Stuttering (Phase H)

Elimination of animation stuttering during runtime shader recompilation.

| File | Purpose |
|------|---------|
| [documentation/StutterAnalysisAndPlan/README-Documentation-Index.md](documentation/StutterAnalysisAndPlan/README-Documentation-Index.md) | Complete guide to stutter elimination system (3 documents) |
| [documentation/StutterAnalysisAndPlan/QUICK-REFERENCE-Semaphore-Architecture.md](documentation/StutterAnalysisAndPlan/QUICK-REFERENCE-Semaphore-Architecture.md) | 5-min overview of synchronization solution |
| [documentation/StutterAnalysisAndPlan/Synchronization-Semaphore-Architecture.md](documentation/StutterAnalysisAndPlan/Synchronization-Semaphore-Architecture.md) | 1000-line deep dive: frame history, selective sync |
| [documentation/StutterAnalysisAndPlan/IMPLEMENTATION-CHECKLIST-Semaphore-Architecture.md](documentation/StutterAnalysisAndPlan/IMPLEMENTATION-CHECKLIST-Semaphore-Architecture.md) | Daily task tracking for Phase H (4 weeks) |
| [documentation/StutterAnalysisAndPlan/00-START-HERE.md](documentation/StutterAnalysisAndPlan/00-START-HERE.md) | Entry point for stutter analysis |
| [documentation/StutterAnalysisAndPlan/DELIVERY-SUMMARY.md](documentation/StutterAnalysisAndPlan/DELIVERY-SUMMARY.md) | Phase H delivery summary |

## Shader Management System

Shader compilation, SPIRV reflection, SDI generation, descriptor automation.

| File | Purpose |
|------|---------|
| [documentation/ShaderManagement/README.md](documentation/ShaderManagement/README.md) | Shader system overview |
| [documentation/ShaderManagement/01-architecture.md](documentation/ShaderManagement/01-architecture.md) | Architecture: compilation pipeline, descriptor automation |
| [documentation/ShaderManagement/ShaderLibraryArchitecture.md](documentation/ShaderManagement/ShaderLibraryArchitecture.md) | Shader library design, reflection system |
| [documentation/ShaderManagement/ShaderManagement-README.md](documentation/ShaderManagement/ShaderManagement-README.md) | Detailed shader management guide |
| [documentation/ShaderManagement/ShaderManagement-Integration-Plan.md](documentation/ShaderManagement/ShaderManagement-Integration-Plan.md) | Integration checklist for shader system |
| [documentation/ShaderManagement/Shader-PreCompilation-Workflow.md](documentation/ShaderManagement/Shader-PreCompilation-Workflow.md) | Pre-compilation workflow, SPIRV reflection, SDI generation |

### Shader Implementations

| File | Purpose |
|------|---------|
| [documentation/Shaders/VoxelRayMarch-Integration-Guide.md](documentation/Shaders/VoxelRayMarch-Integration-Guide.md) | Compute shader integration for voxel ray marching |
| [documentation/Shaders/FragmentRayMarch-Integration-Guide.md](documentation/Shaders/FragmentRayMarch-Integration-Guide.md) | Fragment shader integration for voxel rendering |

## Caching System (CashSystem)

Cache-aware resource management with persistent storage and lazy deserialization.

| File | Purpose |
|------|---------|
| [documentation/CashSystem/README.md](documentation/CashSystem/README.md) | CashSystem overview, 9 cacher types |
| [documentation/CashSystem/01-architecture.md](documentation/CashSystem/01-architecture.md) | Architecture: persistent cache, async save/load |
| [documentation/CashSystem/02-usage-guide.md](documentation/CashSystem/02-usage-guide.md) | How to implement cachers for specific types |
| [documentation/CashSystem/Cacher-Implementation-Template.md](documentation/CashSystem/Cacher-Implementation-Template.md) | Template for new cacher implementation |
| [documentation/CashSystem/CashSystem-Migration-Status.md](documentation/CashSystem/CashSystem-Migration-Status.md) | Migration status, completed types |
| [documentation/CashSystem/Phase-A-Cacher-Implementation-Plan.md](documentation/CashSystem/Phase-A-Cacher-Implementation-Plan.md) | Phase A implementation checklist |

## Event Bus System

Decoupled event-driven communication for node invalidation and system notifications.

| File | Purpose |
|------|---------|
| [documentation/EventBus/README.md](documentation/EventBus/README.md) | EventBus overview, queue-based event system |
| [documentation/EventBus/EventBusArchitecture.md](documentation/EventBus/EventBusArchitecture.md) | Architecture: message queues, auto-incrementing types |
| [documentation/EventBus/AutoMessageTypeSystem.md](documentation/EventBus/AutoMessageTypeSystem.md) | Auto message type generation system |
| [documentation/EventBus/EventBus-ResourceManagement-Integration.md](documentation/EventBus/EventBus-ResourceManagement-Integration.md) | Integration with resource management system |

## Voxel Rendering & Ray Tracing (Phase H)

Voxel infrastructure, ray marching, octree structures, hybrid rendering.

### Infrastructure & Design

| File | Purpose |
|------|---------|
| [documentation/PhaseH-VoxelInfrastructure-Plan.md](documentation/PhaseH-VoxelInfrastructure-Plan.md) | Phase H plan: CameraNode, VoxelGridNode, octree, procedural generation |
| [documentation/VoxelRayTracingResearch-TechnicalRoadmap.md](documentation/VoxelRayTracingResearch-TechnicalRoadmap.md) | Research roadmap, 4-pipeline comparison, May 2026 publication target |

### Voxel Structures

| File | Purpose |
|------|---------|
| [documentation/VoxelStructures/OctreeDesign.md](documentation/VoxelStructures/OctreeDesign.md) | Octree data structure design for voxel storage |
| [documentation/VoxelStructures/GigaVoxels-CachingStrategy.md](documentation/VoxelStructures/GigaVoxels-CachingStrategy.md) | GigaVoxels-style caching strategy for massive datasets |
| [documentation/VoxelStructures/ESVO-Integration-Guide.md](documentation/VoxelStructures/ESVO-Integration-Guide.md) | Efficient Sparse Voxel Octree integration |
| [documentation/VoxelStructures/ECS-Octree-Integration-Analysis.md](documentation/VoxelStructures/ECS-Octree-Integration-Analysis.md) | Entity Component System integration with octree |

### Ray Tracing

| File | Purpose |
|------|---------|
| [documentation/RayTracing/HardwareRTDesign.md](documentation/RayTracing/HardwareRTDesign.md) | Hardware ray tracing pipeline design |
| [documentation/RayTracing/HybridRTX-SurfaceSkin-Architecture.md](documentation/RayTracing/HybridRTX-SurfaceSkin-Architecture.md) | Hybrid ray tracing with surface-skin optimization |

## Build System & Environment

Build configuration, CMake setup, development environment.

| File | Purpose |
|------|---------|
| [documentation/BuildSystem/CMAKE_BUILD_OPTIMIZATION.md](documentation/BuildSystem/CMAKE_BUILD_OPTIMIZATION.md) | CMake optimization, precompiled headers, incremental builds |
| [documentation/BuildSystem/BuildingWithoutVulkanSDK.md](documentation/BuildSystem/BuildingWithoutVulkanSDK.md) | Building without Vulkan SDK, fallback paths |
| [documentation/ArchitecturalReview-CurrentState.md](documentation/ArchitecturalReview-CurrentState.md) | Current architecture state, recent reviews |

## Testing & Quality Assurance

Testing frameworks, test coverage, validation strategies.

| File | Purpose |
|------|---------|
| [documentation/Testing/VS_CODE_TESTING_SETUP.md](documentation/Testing/VS_CODE_TESTING_SETUP.md) | VS Code integration for running GoogleTest |
| [documentation/Testing/TEST_COVERAGE.md](documentation/Testing/TEST_COVERAGE.md) | Test coverage metrics, LCOV visualization |
| [documentation/Testing/TEST_COVERAGE_SUMMARY.md](documentation/Testing/TEST_COVERAGE_SUMMARY.md) | Summary of test coverage (40% overall) |
| [documentation/Testing/TESTING_PROGRESS_SUMMARY.md](documentation/Testing/TESTING_PROGRESS_SUMMARY.md) | Testing progress and roadmap |
| [documentation/Testing/TestScenes.md](documentation/Testing/TestScenes.md) | Test scene definitions, procedural generation |
| [documentation/RenderGraph/COMPREHENSIVE_TEST_PLAN.md](documentation/RenderGraph/COMPREHENSIVE_TEST_PLAN.md) | Test plan for render graph system |

## Code Standards & Guidelines

Coding conventions, communication style, best practices.

| File | Purpose |
|------|---------|
| [documentation/Standards/Communication Guidelines.md](documentation/Standards/Communication%20Guidelines.md) | **MANDATORY**: Radical conciseness, no sycophantic language, facts over process |
| [documentation/Standards/cpp-programming-guidelins.md](documentation/Standards/cpp-programming-guidelins.md) | C++23 coding standards, RAII, const-correctness, class/function size limits |
| [documentation/Standards/smart-pointers-guide.md](documentation/Standards/smart-pointers-guide.md) | Smart pointer usage guidelines (unique_ptr, shared_ptr, weak_ptr) |

## Type System & Migrations

Type safety implementation, resource variant architecture, migration guides.

| File | Purpose |
|------|---------|
| [documentation/Type-System-Migration-Guide.md](documentation/Type-System-Migration-Guide.md) | Step-by-step migration to variant-based type system |
| [documentation/Type-System-Final-Summary.md](documentation/Type-System-Final-Summary.md) | Summary of type system implementation (29+ types, zero runtime overhead) |
| [documentation/Migration-To-Zero-Overhead-System.md](documentation/Migration-To-Zero-Overhead-System.md) | Migration to zero-overhead abstractions |

## Research & References

Research papers, optimization techniques, bibliography.

| File | Purpose |
|------|---------|
| [documentation/Optimizations/BibliographyOptimizationTechniques.md](documentation/Optimizations/BibliographyOptimizationTechniques.md) | Bibliography: 24+ papers on voxel rendering, ray tracing, GPU optimization |
| [documentation/Profiling/PerformanceProfilerDesign.md](documentation/Profiling/PerformanceProfilerDesign.md) | Performance profiler design for benchmarking |
| [documentation/ResearchPhases-ParallelTrack.md](documentation/ResearchPhases-ParallelTrack.md) | Research phases for voxel ray tracing analysis |

## Historical Archive

Previous phases, architectural reviews, experiments, and obsolete documentation.

### Archive Directory

| Directory | Purpose |
|-----------|---------|
| [documentation/archive/2025-12-01-cleanup/](documentation/archive/2025-12-01-cleanup/) | Old build logs and temp files from Nov 2025 |

**Note**: Historical archive files from Phases 0-G were not preserved during earlier cleanup operations. Current project state is fully documented in:
- [documentation/ArchitecturalPhases-Checkpoint.md](documentation/ArchitecturalPhases-Checkpoint.md) - Complete phase history
- [memory-bank/progress.md](memory-bank/progress.md) - Implementation status
- [documentation/ArchitecturalReview-CurrentState.md](documentation/ArchitecturalReview-CurrentState.md) - Current architecture state

## Main Documentation Files

| File | Purpose |
|------|---------|
| [documentation/README.md](documentation/README.md) | Documentation directory overview |
| [CLAUDE.md](CLAUDE.md) | **READ FIRST**: AI assistant guidelines, memory bank, build system, architecture overview, coding standards |

## Navigation Guide

### By Role

**Project Manager**:
- Start: [memory-bank/projectbrief.md](memory-bank/projectbrief.md)
- Then: [memory-bank/progress.md](memory-bank/progress.md)
- Reference: [PhaseH-VoxelInfrastructure-Plan.md](documentation/PhaseH-VoxelInfrastructure-Plan.md)

**Software Architect**:
- Start: [memory-bank/systemPatterns.md](memory-bank/systemPatterns.md)
- Then: [documentation/GraphArchitecture/00-START-HERE.md](documentation/GraphArchitecture/00-START-HERE.md)
- Reference: [memory-bank/codeQualityPlan.md](memory-bank/codeQualityPlan.md)

**C++ Developer (New)**:
- Start: [CLAUDE.md](CLAUDE.md)
- Then: [documentation/Standards/Communication Guidelines.md](documentation/Standards/Communication%20Guidelines.md)
- Then: [documentation/Standards/cpp-programming-guidelins.md](documentation/Standards/cpp-programming-guidelins.md)
- Finally: [memory-bank/projectbrief.md](memory-bank/projectbrief.md)

**C++ Developer (Render Graph)**:
- Start: [documentation/GraphArchitecture/00-START-HERE.md](documentation/GraphArchitecture/00-START-HERE.md)
- Then: [documentation/GraphArchitecture/TypedNodeExample.md](documentation/GraphArchitecture/TypedNodeExample.md)
- Reference: [documentation/GraphArchitecture/06-examples.md](documentation/GraphArchitecture/06-examples.md)

**C++ Developer (Voxel/Ray Tracing)**:
- Start: [documentation/PhaseH-VoxelInfrastructure-Plan.md](documentation/PhaseH-VoxelInfrastructure-Plan.md)
- Then: [documentation/VoxelStructures/OctreeDesign.md](documentation/VoxelStructures/OctreeDesign.md)
- Then: [documentation/Shaders/VoxelRayMarch-Integration-Guide.md](documentation/Shaders/VoxelRayMarch-Integration-Guide.md)

**QA/Tester**:
- Start: [documentation/Testing/VS_CODE_TESTING_SETUP.md](documentation/Testing/VS_CODE_TESTING_SETUP.md)
- Then: [documentation/RenderGraph/COMPREHENSIVE_TEST_PLAN.md](documentation/RenderGraph/COMPREHENSIVE_TEST_PLAN.md)
- Reference: [documentation/Testing/TestScenes.md](documentation/Testing/TestScenes.md)

**Researcher (Voxel Ray Tracing)**:
- Start: [documentation/VoxelRayTracingResearch-TechnicalRoadmap.md](documentation/VoxelRayTracingResearch-TechnicalRoadmap.md)
- Then: [documentation/Optimizations/BibliographyOptimizationTechniques.md](documentation/Optimizations/BibliographyOptimizationTechniques.md)
- Then: [documentation/ResearchPhases-ParallelTrack.md](documentation/ResearchPhases-ParallelTrack.md)

### By Topic

**Render Graph System**:
1. [documentation/GraphArchitecture/00-START-HERE.md](documentation/GraphArchitecture/00-START-HERE.md)
2. [documentation/RenderGraph-Architecture-Overview.md](documentation/RenderGraph-Architecture-Overview.md)
3. [documentation/RenderGraph_Lifecycle_Schema.md](documentation/RenderGraph_Lifecycle_Schema.md)
4. [documentation/GraphArchitecture/ResourceHandleVariant-Support.md](documentation/GraphArchitecture/ResourceHandleVariant-Support.md)

**Type Safety & Resource Management**:
1. [documentation/Type-System-Migration-Guide.md](documentation/Type-System-Migration-Guide.md)
2. [documentation/Type-System-Final-Summary.md](documentation/Type-System-Final-Summary.md)
3. [documentation/GraphArchitecture/ResourceHandleVariant-Support.md](documentation/GraphArchitecture/ResourceHandleVariant-Support.md)
4. [documentation/GraphArchitecture/ResourceVariant-Quick-Reference.md](documentation/GraphArchitecture/ResourceVariant-Quick-Reference.md)

**GPU Synchronization & Stutter Elimination**:
1. [documentation/StutterAnalysisAndPlan/README-Documentation-Index.md](documentation/StutterAnalysisAndPlan/README-Documentation-Index.md)
2. [documentation/StutterAnalysisAndPlan/QUICK-REFERENCE-Semaphore-Architecture.md](documentation/StutterAnalysisAndPlan/QUICK-REFERENCE-Semaphore-Architecture.md)
3. [documentation/StutterAnalysisAndPlan/Synchronization-Semaphore-Architecture.md](documentation/StutterAnalysisAndPlan/Synchronization-Semaphore-Architecture.md)
4. [documentation/StutterAnalysisAndPlan/IMPLEMENTATION-CHECKLIST-Semaphore-Architecture.md](documentation/StutterAnalysisAndPlan/IMPLEMENTATION-CHECKLIST-Semaphore-Architecture.md)

**Shader Management & SPIRV Reflection**:
1. [documentation/ShaderManagement/README.md](documentation/ShaderManagement/README.md)
2. [documentation/ShaderManagement/01-architecture.md](documentation/ShaderManagement/01-architecture.md)
3. [documentation/ShaderManagement/Shader-PreCompilation-Workflow.md](documentation/ShaderManagement/Shader-PreCompilation-Workflow.md)
4. [documentation/Shaders/VoxelRayMarch-Integration-Guide.md](documentation/Shaders/VoxelRayMarch-Integration-Guide.md)

**Caching System (CashSystem)**:
1. [documentation/CashSystem/README.md](documentation/CashSystem/README.md)
2. [documentation/CashSystem/01-architecture.md](documentation/CashSystem/01-architecture.md)
3. [documentation/CashSystem/02-usage-guide.md](documentation/CashSystem/02-usage-guide.md)
4. [documentation/GraphArchitecture/04-caching.md](documentation/GraphArchitecture/04-caching.md)

**Event Bus & Decoupled Communication**:
1. [documentation/EventBus/README.md](documentation/EventBus/README.md)
2. [documentation/EventBus/EventBusArchitecture.md](documentation/EventBus/EventBusArchitecture.md)
3. [documentation/EventBus/AutoMessageTypeSystem.md](documentation/EventBus/AutoMessageTypeSystem.md)

**Voxel Rendering & Ray Tracing**:
1. [documentation/PhaseH-VoxelInfrastructure-Plan.md](documentation/PhaseH-VoxelInfrastructure-Plan.md)
2. [documentation/VoxelRayTracingResearch-TechnicalRoadmap.md](documentation/VoxelRayTracingResearch-TechnicalRoadmap.md)
3. [documentation/VoxelStructures/OctreeDesign.md](documentation/VoxelStructures/OctreeDesign.md)
4. [documentation/RayTracing/HardwareRTDesign.md](documentation/RayTracing/HardwareRTDesign.md)

**Build System & Environment**:
1. [CLAUDE.md](CLAUDE.md) - Section "Build System"
2. [documentation/BuildSystem/CMAKE_BUILD_OPTIMIZATION.md](documentation/BuildSystem/CMAKE_BUILD_OPTIMIZATION.md)
3. [documentation/BuildSystem/BuildingWithoutVulkanSDK.md](documentation/BuildSystem/BuildingWithoutVulkanSDK.md)

**Testing & Quality**:
1. [documentation/Testing/VS_CODE_TESTING_SETUP.md](documentation/Testing/VS_CODE_TESTING_SETUP.md)
2. [documentation/Testing/TEST_COVERAGE.md](documentation/Testing/TEST_COVERAGE.md)
3. [documentation/RenderGraph/COMPREHENSIVE_TEST_PLAN.md](documentation/RenderGraph/COMPREHENSIVE_TEST_PLAN.md)

**Code Standards**:
1. [documentation/Standards/Communication Guidelines.md](documentation/Standards/Communication%20Guidelines.md) - **MANDATORY**
2. [documentation/Standards/cpp-programming-guidelins.md](documentation/Standards/cpp-programming-guidelins.md)
3. [documentation/Standards/smart-pointers-guide.md](documentation/Standards/smart-pointers-guide.md)

## Document Statistics

| Category | Count | Pages | Purpose |
|----------|-------|-------|---------|
| Memory Bank | 7 | 50 | Project context, status, patterns |
| Render Graph | 20+ | 600 | Node system, compilation, execution |
| Synchronization | 7 | 200 | GPU sync, stutter elimination |
| Shader Management | 6 | 150 | Compilation, SPIRV, descriptors |
| Caching (CashSystem) | 6 | 150 | Cache-aware resource management |
| Event Bus | 4 | 100 | Decoupled communication |
| Voxel Rendering | 6 | 150 | Octrees, ray marching, structures |
| Ray Tracing | 2 | 50 | Hardware RT, hybrid approaches |
| Testing | 5 | 100 | Coverage, test plans, setup |
| Code Standards | 3 | 75 | Guidelines, best practices |
| Build System | 3 | 50 | CMake, Vulkan SDK, optimization |
| Type System | 3 | 75 | Variants, safety, migration |
| Research | 3 | 75 | Bibliography, profiling, roadmap |
| Archive | 1 | 10 | December 2025 cleanup |
| **Total** | **~85** | **~2,200** | Complete project documentation |

## Using This Index

1. **Find by role**: Check "Navigation Guide > By Role" for your position
2. **Find by topic**: Check "Navigation Guide > By Topic" for technical subjects
3. **Jump to archive**: Search "Historical Archive" for older phases and experiments
4. **Understand structure**: Check "Document Statistics" for scope of each area

## Key Principles

**Radical Conciseness**: Every file title and description is brief, actionable
**Single Source of Truth**: Each topic has primary docs; others reference
**Versioned History**: Archive preserves all previous phases for reference
**Role-Based Navigation**: Find what you need in seconds based on your job
**Linked References**: Cross-references maintain coherence across 125+ files

---

**Last Updated**: December 2, 2025
**Status**: Week 2 GPU Integration COMPLETE | 1,700 Mrays/sec achieved | Week 3 DXT Compression PENDING
**Scope**: ~85 files, ~2,200 pages, 14 major topic areas

**Recent Changes (December 2, 2025)**:
- Removed 40+ orphaned archive file references (files never existed)
- Archived old build logs from temp/ directory
- Deleted large debug logs (vixen_debug.log, vixen_run.log)
- Deleted orphaned CashSystem- file
- Updated PhaseH-VoxelInfrastructure-Plan.md with Week 2 completion status
- Updated ArchitecturalPhases-Checkpoint.md with current progress
- Fixed stale "IN PROGRESS" statuses to reflect completed work
