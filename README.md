# VIXEN — Vulkan Interactive eXample Engine

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.4.321.1-red.svg)](https://www.vulkan.org/)

A production-quality **Vulkan render-graph engine** with compile-time type safety, event-driven
invalidation, and a persistent caching system. VIXEN began as a voxel ray-tracing research
platform and is now being extended into a **reusable, moddable game render engine** — consumed
as a library by the game **Undertow** — built around an **SDF/Recipe procedural-content
codegen** pipeline.

> **Direction (2026).** The comparative voxel ray-tracing research (the platform's origin) is
> feature-complete and its paper drafted. Active development has pivoted to the game-engine-library
> track: an instantiable engine boundary, offscreen/multi-view render targets, a recoverable error
> model, and the Recipe/SDF/CSG content system. See
> [`Architecture-Review-Game-Renderer-2026-06-12`](VIXEN/Vixen-Docs/01-Architecture/Architecture-Review-Game-Renderer-2026-06-12.md)
> for the gap analysis driving this work.

The engine lives under [`VIXEN/`](VIXEN/); this file is the repository overview.

## Key Features

### Graph-Based Rendering Architecture
- **Node-Based Render Graph** — modular, composable operations connected into a directed acyclic graph (DAG)
- **Compile-Time Type Safety** — `TypedNode<Config>` API with `In()`/`Out()` methods eliminates runtime type errors
- **Variant Resource System** — 29+ Vulkan types via a macro-based registry, zero-overhead abstractions
- **Event-Driven Invalidation** — decoupled cascade (WindowResize → SwapChainInvalidated → Framebuffer rebuild)
- **Protected API Enforcement** — nodes use the high-level typed API; the graph manages low-level wiring

### Advanced Infrastructure
- **Persistent Cache System** — async save/load with lazy deserialization (shaders, pipelines, samplers, …)
- **Testing Framework** — GoogleTest suites across libraries (SVO, VoxelData, GaiaVoxelWorld, RenderGraph, …) with LCOV coverage
- **Logging System** — `ILoggable` interface with `LOG_*` / `NODE_LOG_*` macros
- **Lifecycle Hooks** — 14 total (6 graph phases + 8 node phases) for fine-grained control
- **Multi-Rate Loop System** — fixed-timestep accumulator (per-frame, 60 Hz, 120 Hz)
- **Frame-in-Flight Synchronization** — CPU-GPU pacing with two-tier sync (fences + semaphores)

### Shader Management
- **SPIR-V Reflection** — automatic descriptor-layout generation from reflection
- **SDI Generation** — type-safe UBO struct definitions with a content-hash UUID system
- **Data-Driven Pipelines** — no hardcoded shader assumptions; everything derived from reflection
- **Descriptor Automation** — pool sizing, layout creation, and binding from SPIR-V metadata
- **Build-Time & Runtime Compilation** — `shader_tool` offline path plus runtime glslang

### Procedural Content: SDF / Recipe / CSG *(active)*
- **Recipe Container** — a cross-repo content format: the **Undertow** authoring side (writer)
  emits recipes that the VIXEN **reader** consumes to drive procedural generation
- **SDF Kernel Codegen** — signed-distance-field operations compiled into evaluation kernels
- **CSG Operations** — Union / Subtract / Intersect (and smooth variants) over SDF primitives
- **Octree Memory Pool** — dynamic-N, memory-budgeted pool with bake-to-pool workflow feeding the SVO

### Voxel & Ray Tracing (heritage / reusable)
- **Sparse Voxel Octree** — Laine–Karras-based SVO with brick maps and compression
- **Ray-marching & Hardware RT** — compute, fragment, and `VK_KHR_acceleration_structure` pipelines
- **Performance Profiling** — VkQueryPool timestamps, bandwidth monitoring, CSV export

## Quick Start

### Prerequisites
- **Windows 10/11 (x64)** with Visual Studio 2022+ (MSVC), *or* **Linux/WSL** (GCC/Clang)
- **CMake 3.21+** (Ninja recommended)
- **Vulkan SDK 1.4.321.1** — auto-provisioned by the build if not found
  (`cmake/ProvisionVulkan.cmake`); a manual install to `C:/VulkanSDK/1.4.321.1` also works

### Build

```bash
# From the engine directory
cd VIXEN

# Configure (Ninja auto-selected if available; Vulkan SDK auto-provisioned if missing)
cmake -B build

# Build
cmake --build build --config Debug --parallel 16      # or --config Release
```

Header-only / SDK-less environments can configure with `-DVULKAN_TRIMMED_BUILD=ON`.

### Visual Studio

```bash
cd VIXEN && cmake -B build && start build/VIXEN.sln
```

## Architecture Overview

```
┌─────────────────────────────────────────┐
│              RenderGraph                 │  ← orchestrator: compilation phases,
│                                          │    resource ownership, execution order
└──────────────┬───────────────────────────┘
    ┌──────────┼──────────┬────────────────────┐
    ▼          ▼          ▼                    ▼
┌────────────┐ ┌────────┐ ┌────────────────┐ ┌──────────┐
│NodeInstance│ │Resource│ │   EventBus     │ │ Topology │
│ (base)     │ │(variant)│ │(invalidation) │ │  (DAG)   │
└─────┬──────┘ └────────┘ └────────────────┘ └──────────┘
      ▼
┌──────────────────────┐
│ TypedNode<ConfigType>│  ← compile-time typed slots
└─────┬────────────────┘
      ▼
┌──────────────────────┐
│  Concrete Nodes      │  ← SwapChain / Framebuffer / Pipeline / TraceRays / … (30+)
└──────────────────────┘
```

### Key Design Patterns

```cpp
// 1) Typed Node — compile-time-checked slots
struct MyNodeConfig {
    INPUT_SLOT(ALBEDO, VkImage, SlotMode::SINGLE);
    OUTPUT_SLOT(FRAMEBUFFER, VkFramebuffer, SlotMode::SINGLE);
};
class MyNode : public TypedNode<MyNodeConfig> {
    void Compile() override {
        VkImage albedo = In(MyNodeConfig::ALBEDO);   // type-checked
        Out(MyNodeConfig::FRAMEBUFFER, myFB);        // type-checked
    }
};

// 2) Resource Variant — one macro registers a type-safe handle
REGISTER_RESOURCE_TYPE(VkImage, ImageDescriptor)

// 3) Event-Driven Invalidation — subscribe, mark dirty, cascade
eventBus->Subscribe(EventType::WindowResize, this);
eventBus->Emit(SwapChainInvalidatedEvent{});
```

## Libraries

14 static libraries under [`VIXEN/libraries/`](VIXEN/libraries/) — foundation (logger, EventBus,
Core, ResourceManagement, CashSystem), Vulkan/rendering (VulkanResources, ShaderManagement,
RenderGraph, Profiler), and voxel/procedural (VoxelData, SVO, VoxelComponents, GaiaVoxelWorld,
GaiaArchetypes). See the [libraries README](VIXEN/libraries/README.md) for one-line descriptions.

## Documentation

- **[Documentation Index](VIXEN/DOCUMENTATION_INDEX.md)** — catalog of active docs by topic
- **[`Vixen-Docs/`](VIXEN/Vixen-Docs/)** — the canonical Obsidian vault (architecture, implementation, research, progress)
  - Architecture: [`Vixen-Docs/01-Architecture/`](VIXEN/Vixen-Docs/01-Architecture/)
  - Libraries: [`Vixen-Docs/Libraries/`](VIXEN/Vixen-Docs/Libraries/)
  - Research: [`Vixen-Docs/03-Research/`](VIXEN/Vixen-Docs/03-Research/)
- **[`memory-bank/`](VIXEN/memory-bank/)** — session/context state
- Finished/superseded material lives under [`VIXEN/archive/`](VIXEN/archive/) and
  [`Vixen-Docs/_archive/`](VIXEN/Vixen-Docs/_archive/)

## Status

- **Engine core:** production-quality render graph, shader management, caching, profiling — stable
- **Game-engine-library track:** in progress — engine facade, render-target abstraction, error model, moddable API surface
- **SDF/Recipe/CSG content system:** in active development (registry, octree pool, live render gates)
- **Voxel ray-tracing research:** feature-complete; paper drafted

## License

MIT License — see [LICENSE](LICENSE). Copyright © 2025–2026 Lior Yaari.

## Acknowledgments

Built on **Vulkan** (Khronos), **SPIRV-Reflect**, **glslang**, **GoogleTest**, and **Gaia ECS**.
Voxel/ray-tracing work draws on published research in sparse voxel octrees, SVDAG compression, and
GPU traversal (full bibliography in [`Vixen-Docs/03-Research/`](VIXEN/Vixen-Docs/03-Research/)).
