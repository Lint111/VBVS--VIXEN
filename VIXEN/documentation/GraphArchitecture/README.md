# RenderGraph Documentation

**Status**: Production-Ready - 15+ nodes implemented, zero validation errors
**Last Updated**: October 31, 2025

## Overview

Graph-based rendering system for Vulkan with compile-time type safety, automatic resource management, and event-driven invalidation.

**Current State**: Fully operational with data-driven pipeline creation, handle-based node access, and dependency-ordered cleanup.

---

## Documentation Structure

### 📋 Quick Start
- **[Quick Reference](render-graph-quick-reference.md)** - Fast overview with code examples

### 📚 Core Documentation

1. **[Node System](01-node-system.md)**
   - Node Type vs Node Instance architecture
   - Type registry and instancing
   - Resource types and schemas
   - Pipeline sharing across instances

2. **[Graph Compilation](02-graph-compilation.md)**
   - Graph construction API
   - Compilation phases (5-phase process)
   - Dependency analysis
   - Resource allocation
   - Pipeline generation

3. **[Multi-Device Support](03-multi-device.md)**
   - Device affinity rules
   - Cross-device transfer nodes
   - Multi-GPU execution
   - Synchronization strategies
   - Resource ownership transfer

4. **[Caching System](04-caching.md)**
   - Multi-level caching strategy
   - Pipeline cache (VkPipelineCache)
   - Descriptor set cache
   - Resource cache
   - Cache key generation
   - Invalidation policies

5. **[Implementation Guide](05-implementation.md)**
   - Directory structure
   - Key classes and responsibilities
   - Migration strategy (10-week plan)
   - Performance considerations
   - Testing strategy

6. **[Usage Examples](06-examples.md)**
   - Simple forward rendering
   - Shadow mapping with multiple lights
   - Multi-GPU rendering
   - Load balancing across GPUs

7. **[Cache-Aware Batching](07-cache-aware-batching.md)**
   - GPU cache hierarchy (L1/L2)
   - Working set calculation
   - Batch creation algorithms
   - Resource sharing optimization
   - Performance analysis

8. **[Per-Frame Resources](08-per-frame-resources.md)** ✅ NEW
   - Ring buffer pattern for CPU-GPU race prevention
   - PerFrameResources helper class
   - Ownership rules and best practices
   - Implementation in DescriptorSetNode, GeometryRenderNode

### 📖 Reference

- **[NodeCatalog.md](NodeCatalog.md)** - Complete catalog of 15+ node types
- **[ResourceVariant-Quick-Reference.md](ResourceVariant-Quick-Reference.md)** - Resource type system
- **[TypedNodeExample.md](TypedNodeExample.md)** - Typed node usage examples
- **[NodeConfig-API-Improvements.md](NodeConfig-API-Improvements.md)** - API improvements
- **[NodeBased_vs_Legacy_Rendering.md](NodeBased_vs_Legacy_Rendering.md)** - Migration guide
- **[PartialCleanupStrategy.md](PartialCleanupStrategy.md)** - Cleanup dependency tracking
- **[ApplicationArchitecture.md](ApplicationArchitecture.md)** - Application integration
- **[ResourceConfig.md](ResourceConfig.md)** - Resource configuration system
- **[ResourceStateManagement.md](ResourceStateManagement.md)** - State tracking
- **[render-graph-quick-reference.md](render-graph-quick-reference.md)** - Quick API reference

---

## Key Concepts

### Node Type vs Node Instance

| Concept | Role | Count | Example |
|---------|------|-------|---------|
| **Node Type** | Template/Definition | 1 per process | `ShadowMapPass` |
| **Node Instance** | Concrete usage | N per scene | `ShadowMap_Light0`, `ShadowMap_Light1` |

### System Architecture

```
┌──────────────────────────────────────────────┐
│         Node Type Registry                   │
│  (ShadowMapPass, GeometryPass, etc.)        │
└─────────────┬────────────────────────────────┘
              │ instantiate
              ▼
┌──────────────────────────────────────────────┐
│         Render Graph                         │
│  ┌────────┐  ┌────────┐  ┌────────┐        │
│  │Instance│  │Instance│  │Instance│  ...   │
│  └────────┘  └────────┘  └────────┘        │
└─────────────┬────────────────────────────────┘
              │ compile
              ▼
┌──────────────────────────────────────────────┐
│      Pipeline Instance Groups                 │
│  (Shared pipelines + Variants)               │
└─────────────┬────────────────────────────────┘
              │ execute
              ▼
┌──────────────────────────────────────────────┐
│         Vulkan Commands                      │
│  (Per-device command buffers)                │
└──────────────────────────────────────────────┘
```

---

## Core Features

### ✅ Dynamic Pipeline Compilation
Replace static resource allocation with runtime graph compilation

### ✅ Reusable Components
- Node types define process templates
- Unlimited instances per type
- Automatic pipeline sharing when compatible

### ✅ Resource Optimization
- Transient resource aliasing (30-50% memory reduction)
- Automatic barrier insertion
- Smart caching at multiple levels

### ✅ Multi-Device Support
- First-class multi-GPU architecture
- Automatic cross-device transfer
- Device affinity propagation
- Parallel execution per device

### ✅ Intelligent Caching
- Pipeline cache (shared across compatible instances)
- Descriptor set pooling
- Resource output caching
- LRU eviction

---

## Workflow

### 1. Setup (Once)
```cpp
NodeTypeRegistry registry;
RegisterBuiltInNodeTypes(registry);
```

### 2. Build Graph (Per Scene)
```cpp
RenderGraph graph(device, &registry);
auto scene = graph.AddNode("GeometryPass", "MainScene");
auto shadow = graph.AddNode("ShadowMapPass", "Shadow_Light0");
graph.ConnectNodes(scene, 0, shadow, 0);
```

### 3. Compile & Execute
```cpp
graph.Compile();  // Analyzes instances, creates pipelines
graph.Execute(commandBuffer);
```

---

## Migration Path

**Current State:** Static resource management requiring manual updates across multiple areas

**Phase 1:** Foundation (Weeks 1-2)
- Implement base node and graph classes
- Resource abstraction

**Phase 2:** Node Types (Weeks 3-4)
- Port existing operations to node types
- Type registry and factory

**Phase 3:** Resource Management (Weeks 5-6)
- Resource allocator
- Transient aliasing

**Phase 4:** Caching (Week 7)
- Multi-level cache implementation

**Phase 5:** Optimization (Week 8)
- Multi-threading
- Performance tuning

**Phase 6:** Integration (Weeks 9-10)
- Replace current renderer
- Migration tools

**Target State:** Dynamic graph-based system with automatic optimization

---

## Performance Targets

| Metric | Target |
|--------|--------|
| Runtime Overhead | < 5% vs static |
| Memory Reduction | 30-50% (transient aliasing) |
| Compilation Time | < 100ms for typical scene |
| Cache Hit Rate | > 90% for stable scenes |

---

## Getting Started

1. **Read the [Quick Reference](render-graph-quick-reference.md)** for a rapid overview
2. **Study [Node System](01-node-system.md)** to understand types vs instances
3. **Review [Usage Examples](06-examples.md)** for practical patterns
4. **Follow [Implementation Guide](05-implementation.md)** to begin coding

---

## Additional Resources

### Academic References
- "FrameGraph: Extensible Rendering Architecture in Frostbite" (GDC 2017)
- "Render Graphs and Vulkan — a deep dive" (Khronos)

### Similar Systems
- Frostbite FrameGraph
- Unity Scriptable Render Pipeline
- Unreal Engine 5 Render Dependency Graph

---

**Version:** 1.0
**Status:** Design Complete - Ready for Implementation
**Last Updated:** 2025-10-18
