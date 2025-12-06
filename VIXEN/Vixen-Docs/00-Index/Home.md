---
title: VIXEN Documentation Home
aliases: [Home, Index, Dashboard, Start Here]
tags: [index, navigation, home]
created: 2025-12-06
---

# VIXEN Documentation

**Vulkan Interactive eXample Engine** - A graph-based Vulkan rendering engine with voxel ray tracing research capabilities.

> [!info] Current Status
> **Phase J COMPLETE** - Fragment shader pipeline with push constants working. 1,700 Mrays/sec achieved.

---

## Quick Navigation

```mermaid
graph LR
    A[Home] --> B[Architecture]
    A --> C[Implementation]
    A --> D[Research]
    A --> E[Development]
    A --> F[Progress]

    B --> B1[RenderGraph]
    B --> B2[Vulkan Pipeline]
    B --> B3[Type System]

    C --> C1[SVO System]
    C --> C2[Ray Marching]
    C --> C3[Shaders]

    D --> D1[ESVO Algorithm]
    D --> D2[Papers]

    E --> E1[Build System]
    E --> E2[Testing]

    style A fill:#4a9eff
    style B fill:#ff9f43
    style C fill:#26de81
    style D fill:#a55eea
    style E fill:#fd9644
    style F fill:#20bf6b
```

---

## Sections

### [[01-Architecture/Overview|Architecture]]
Core system design and component relationships.
- [[01-Architecture/RenderGraph-System|RenderGraph System]] - Node-based rendering pipeline
- [[01-Architecture/Vulkan-Pipeline|Vulkan Pipeline]] - GPU resource management
- [[01-Architecture/Type-System|Type System]] - Compile-time safety with variants
- [[01-Architecture/EventBus|EventBus]] - Decoupled communication

### [[02-Implementation/Overview|Implementation]]
Detailed implementation guides and code references.
- [[02-Implementation/SVO-System|SVO System]] - Sparse Voxel Octree with ESVO
- [[02-Implementation/Ray-Marching|Ray Marching]] - GPU ray traversal
- [[02-Implementation/Compression|DXT Compression]] - 5.3:1 memory reduction
- [[02-Implementation/Shaders|Shaders]] - GLSL shader documentation

### [[03-Research/Overview|Research]]
Academic references and algorithm documentation.
- [[03-Research/ESVO-Algorithm|ESVO Algorithm]] - Laine & Karras 2010
- [[03-Research/Voxel-Papers|Voxel Papers]] - Bibliography of 24+ papers
- [[03-Research/Pipeline-Comparison|Pipeline Comparison]] - 4-way performance analysis

### [[04-Development/Overview|Development]]
Build system, testing, and coding standards.
- [[04-Development/Build-System|Build System]] - CMake configuration
- [[04-Development/Testing|Testing]] - GoogleTest framework
- [[04-Development/Coding-Standards|Coding Standards]] - C++23 guidelines
- [[04-Development/Profiling|Profiling]] - GPU performance measurement

### [[05-Progress/Overview|Progress]]
Project status and roadmap tracking.
- [[05-Progress/Current-Status|Current Status]] - Active work
- [[05-Progress/Roadmap|Roadmap]] - Future phases
- [[05-Progress/Phase-History|Phase History]] - Completed milestones

---

## Key Metrics

| Metric | Value | Target |
|--------|-------|--------|
| GPU Throughput | 1,700 Mrays/sec | >200 Mrays/sec |
| DXT Compression | 5.3:1 ratio | 5:1 ratio |
| Test Coverage | 40% | 40% |
| Nodes Implemented | 19+ | 20+ |
| Shader Variants | 4 (compute/fragment x compressed/uncompressed) | 6 |

---

## Technology Stack

| Component | Technology |
|-----------|------------|
| Graphics API | Vulkan 1.4 |
| Language | C++23 |
| Build System | CMake 4.0+ |
| Platform | Windows (Win32) |
| Compiler | MSVC (VS 2022+) |
| Testing | GoogleTest |

---

## Getting Started

1. **New to the project?** Start with [[01-Architecture/Overview|Architecture Overview]]
2. **Want to build?** See [[04-Development/Build-System|Build System]]
3. **Working on voxels?** Read [[02-Implementation/SVO-System|SVO System]]
4. **Research focus?** Check [[03-Research/ESVO-Algorithm|ESVO Algorithm]]

---

## File Quick Links

> [!tip] Source Files
> - `C:\cpp\VBVS--VIXEN\VIXEN\CLAUDE.md` - Project instructions
> - `C:\cpp\VBVS--VIXEN\VIXEN\memory-bank\` - Context files
> - `C:\cpp\VBVS--VIXEN\VIXEN\libraries\` - Core libraries

---

*Last updated: 2025-12-06*
