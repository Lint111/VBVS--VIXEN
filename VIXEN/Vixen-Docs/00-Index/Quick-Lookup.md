---
title: Quick Lookup Index
aliases: [Index, Lookup, Quick Reference]
tags: [index, navigation, claude]
created: 2025-12-07
---

# Quick Lookup Index

Fast-access reference for Claude Code. Read this file first for any documentation lookup.

---

## Recent Updates

| Date | Topic | File |
|------|-------|------|
| 2026-01-04 | Sprint 6.0.1: Unified Connection System COMPLETE | [[../05-Progress/features/Sprint6.0.1-Unified-Connection-System]] |

---

## Architecture

| Topic | File |
|-------|------|
| System overview | [[../01-Architecture/Overview]] |
| RenderGraph system | [[../01-Architecture/RenderGraph-System]] |
| RenderGraph connection API | [[../05-Progress/features/Sprint6.0.1-Unified-Connection-System]] |
| Vulkan pipeline | [[../01-Architecture/Vulkan-Pipeline]] |
| Type system | [[../01-Architecture/Type-System]] |

---

## Implementation How-To

| Task | File |
|------|------|
| Create a new node | [[../templates/Node-Documentation]] |
| Add logging | [[../Libraries/Logger]] |
| Write tests | [[../04-Development/Testing]] |
| Coding standards | [[../04-Development/Coding-Standards]] |

---

## Libraries

| Library | File |
|---------|------|
| RenderGraph | [[../Libraries/RenderGraph]] |
| SVO | [[../Libraries/SVO]] |
| ShaderManagement | [[../Libraries/ShaderManagement]] |
| EventBus | [[../Libraries/EventBus]] |
| CashSystem | [[../Libraries/CashSystem]] |
| Logger | [[../Libraries/Logger]] |
| ResourceManagement | [[../Libraries/ResourceManagement]] |

---

## Research & Algorithms

| Topic | File |
|-------|------|
| ESVO (Efficient Sparse Voxel Octrees) | [[../03-Research/ESVO]] |
| Voxel ray tracing | [[../03-Research/Voxel-Ray-Tracing]] |
| DXT compression | [[../03-Research/DXT-Compression]] |

---

## Session Context

| File | Purpose |
|------|---------|
| `memory-bank/activeContext.md` | Current focus, recent changes |
| `memory-bank/progress.md` | What's done, what's left |
| [[../05-Progress/Current-Status]] | Detailed status |
| [[../05-Progress/Phase-History]] | Project timeline |

---

## Project Management & Integration

| Topic | File |
|-------|------|
| HacknPlan integration guide | [[../04-Development/HacknPlan-Integration]] |
| Session workflow integration | [[../04-Development/Session-Workflow-Integration]] |
| MCP Sync Engine design | [[../05-Progress/mcp-development/glue-sync-engine]] |

---

## Analysis

| Topic | File |
|-------|------|
| Benchmark data summary | [[../Analysis/Benchmark-Data-Summary]] |
| Data quality report | [[../Analysis/Data-Quality-Report]] |

---

## Node Catalog

### Infrastructure
- WindowNode, DeviceNode, SwapChainNode, FrameSyncNode

### Pipeline
- RenderPassNode, FramebufferNode, GraphicsPipelineNode, ComputePipelineNode, DescriptorSetNode

### Rendering
- GeometryRenderNode, ComputeDispatchNode, PresentNode

### Resources
- DepthBufferNode, TextureLoaderNode, VertexBufferNode, ShaderLibraryNode

### Specialized
- CameraNode, VoxelGridNode, LoopBridgeNode, ConstantNode

---

## Code Paths

| Component | Path |
|-----------|------|
| RenderGraph core | `libraries/RenderGraph/src/Core/` |
| Node implementations | `libraries/RenderGraph/src/Nodes/` |
| Node configs | `libraries/RenderGraph/include/Data/Nodes/` |
| SVO library | `libraries/SVO/` |
| Shaders | `shaders/` |

---

## Search Patterns

For grep searches when file not in index:

```
# Find node implementation
grep "class.*Node.*:" libraries/RenderGraph/

# Find slot definitions
grep "INPUT_SLOT\|OUTPUT_SLOT" libraries/RenderGraph/include/

# Find Vulkan calls
grep "vk[A-Z]" libraries/

# Find ESVO algorithm
grep "ESVO\|traversal\|octree" libraries/SVO/
```
