# Active Context

**Last Updated**: December 3, 2025
**Current Branch**: `claude/phase-i-performance-profiling`
**Status**: Phase I Standalone Components COMPLETE - Ready for Graph Integration

---

## Session Summary

Completed all standalone Profiler components. Ready for RenderGraph integration.

### Completed This Session
- JSON export matching Section 5.2 experimental schema
- SceneInfo data structure (resolution, density, scene_type)
- Bandwidth estimation fallback (when HW counters unavailable)
- BenchmarkRunner framework (test matrix, callbacks, auto-export)
- TestConfiguration validation (power-of-2 res, range checks)
- **53 unit tests passing** (up from 26)

### Files Created This Session
| File | Description |
|------|-------------|
| `libraries/Profiler/include/Profiler/SceneInfo.h` | Scene configuration data structure |
| `libraries/Profiler/src/SceneInfo.cpp` | SceneInfo implementation |
| `libraries/Profiler/include/Profiler/BenchmarkRunner.h` | Benchmark harness framework |
| `libraries/Profiler/src/BenchmarkRunner.cpp` | Test matrix runner |

### Files Modified This Session
| File | Change |
|------|--------|
| `libraries/Profiler/include/Profiler/FrameMetrics.h` | Added `avgVoxelsPerRay`, `bandwidthEstimated`, `PipelineType` enum, validation |
| `libraries/Profiler/src/MetricsExporter.cpp` | Section 5.2 JSON schema export |
| `libraries/Profiler/tests/test_profiler.cpp` | 30+ new tests |

---

## Current Focus: Graph Integration Architecture

### Pipeline Graph Strategy

Create switchable RenderGraph configurations for each pipeline type in the experimental setup:

```
┌─────────────────────────────────────────────────────────────────────┐
│                    BenchmarkGraphFactory                             │
├─────────────────────────────────────────────────────────────────────┤
│  CreateComputeGraph()      → Compute shader ray marching            │
│  CreateFragmentGraph()     → Fragment shader ray marching           │
│  CreateHardwareRTGraph()   → VK_KHR_ray_tracing_pipeline            │
│  CreateHybridGraph()       → Compute primary + HW RT secondary      │
└─────────────────────────────────────────────────────────────────────┘
```

### Reusable SubGraphs (Node Clusters)

Common node patterns extracted into reusable subgraphs:

| SubGraph | Nodes | Purpose |
|----------|-------|---------|
| `SceneSetup` | Camera, Transform, Uniforms | Scene initialization |
| `VoxelDataLoad` | SVO Upload, Brick Cache | Voxel data binding |
| `RayGeneration` | Primary rays from camera | Shared ray setup |
| `OutputPresent` | Tonemap, SwapchainAcquire, Present | Display pipeline |
| `ProfilerHooks` | BeginFrame, EndFrame markers | Profiling integration |

### Graph Configuration Pattern

```cpp
// BenchmarkGraphFactory.h
class BenchmarkGraphFactory {
public:
    enum class PipelineType {
        Compute,        // Compute shader DDA ray marching
        Fragment,       // Fragment shader ray marching
        HardwareRT,     // VK_KHR_ray_tracing_pipeline
        Hybrid          // Compute + HW RT secondary
    };

    static std::unique_ptr<RenderGraph> CreateGraph(
        PipelineType type,
        const SceneInfo& scene,
        const BenchmarkConfig& config
    );

private:
    // SubGraph builders
    static void AddSceneSetupNodes(RenderGraph& graph);
    static void AddVoxelDataNodes(RenderGraph& graph);
    static void AddProfilerNodes(RenderGraph& graph);
    static void AddOutputNodes(RenderGraph& graph);

    // Pipeline-specific ray tracing
    static void AddComputeRayMarch(RenderGraph& graph);
    static void AddFragmentRayMarch(RenderGraph& graph);
    static void AddHardwareRT(RenderGraph& graph);
    static void AddHybridPipeline(RenderGraph& graph);
};
```

### Integration Flow

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│ BenchmarkRun │ ──▶ │ GraphFactory │ ──▶ │ RenderGraph  │
│    Config    │     │  .Create()   │     │  Instance    │
└──────────────┘     └──────────────┘     └──────┬───────┘
                                                  │
        ┌─────────────────────────────────────────┼──────┐
        │                                         ▼      │
        │  ┌───────────┐   ┌───────────┐   ┌───────────┐ │
        │  │  Scene    │──▶│  Voxel    │──▶│   Ray     │ │
        │  │  Setup    │   │  Data     │   │  Trace    │ │
        │  └───────────┘   └───────────┘   └─────┬─────┘ │
        │                                        │       │
        │  ┌───────────┐   ┌───────────┐        │       │
        │  │ Profiler  │◀──│  Output   │◀───────┘       │
        │  │  Export   │   │  Present  │                │
        │  └───────────┘   └───────────┘                │
        └───────────────────────────────────────────────┘
```

---

## Phase I Progress Summary

### Standalone Components ✅ COMPLETE

| Component | Status | Tests |
|-----------|--------|-------|
| ProfilerSystem singleton | ✅ | Core orchestrator |
| RollingStats | ✅ | Sliding window percentiles |
| DeviceCapabilities | ✅ | GPU info capture |
| MetricsCollector | ✅ | VkQueryPool timing |
| MetricsExporter | ✅ | CSV + JSON (Section 5.2) |
| BenchmarkConfig | ✅ | JSON config loader |
| SceneInfo | ✅ | Resolution, density, type |
| BenchmarkRunner | ✅ | Test matrix framework |
| Config Validation | ✅ | Input validation |
| VRAM Tracking | ✅ | VK_EXT_memory_budget |
| Bandwidth Estimation | ✅ | Fallback calculation |

**Test Suite: 53 tests passing**

### Integration Tasks (Next Phase)

| Task | Description | Priority |
|------|-------------|----------|
| BenchmarkGraphFactory | Create switchable graph configs | HIGH |
| SubGraph extraction | Reusable node clusters | HIGH |
| Profiler hook wiring | Connect to NodeInstance lifecycle | HIGH |
| Scene extractors | Pull data from VoxelGridNode | MEDIUM |
| Shader counters | avg_voxels_per_ray atomics | MEDIUM |
| Nsight validation | Compare estimates vs actual | LOW |

---

## Reference Sources

### Experimental Setup (Assignment 3)
- **Document**: `C:\Users\liory\Downloads\Experimental Setup - Assignment 3 2ad1204525ac81e5996df013045cc57d.md`
- **Research Question**: How do different Vulkan ray tracing pipeline architectures affect performance, bandwidth, and scalability?
- **Pipelines**: Hardware RT, Compute Shader, Fragment Shader, Hybrid
- **Tests**: Resolution scaling (32³-512³), Density variation (10-90%), Ray complexity, Dynamic updates
- **Key JSON Schema**: Section 5.2 defines target export format for benchmark data

### ESVO Implementation Paths
- Paper: `C:\Users\liory\Downloads\source-archive\efficient-sparse-voxel-octrees\`
- Key files: `trunk/src/octree/Octree.cpp` (castRay), `trunk/src/octree/build/Builder.cpp`

### Papers
- Laine & Karras (2010): "Efficient Sparse Voxel Octrees"
- Amanatides & Woo (1987): "A Fast Voxel Traversal Algorithm" (DDA)

---

## Known Limitations

1. **Single-brick octrees**: Fallback code path for `bricksPerAxis=1`
2. **Axis-parallel rays**: Require `computeCorrectedTcMax()` filtering
3. **VK_KHR_performance_query**: May not be available on all GPUs (estimation fallback implemented)
4. **Hardware RT**: VK_KHR_ray_tracing_pipeline not yet implemented

---

## Profiler Library Structure (Updated)

```
libraries/Profiler/
├── CMakeLists.txt
├── include/Profiler/
│   ├── ProfilerSystem.h       # Singleton orchestrator
│   ├── RollingStats.h         # Sliding window statistics
│   ├── DeviceCapabilities.h   # GPU info capture
│   ├── FrameMetrics.h         # Data structures + PipelineType enum
│   ├── MetricsCollector.h     # VkQueryPool timing + VRAM
│   ├── MetricsExporter.h      # CSV/JSON output (Section 5.2)
│   ├── BenchmarkConfig.h      # JSON config loading + validation
│   ├── SceneInfo.h            # Scene metadata structure
│   ├── BenchmarkRunner.h      # Test matrix harness
│   └── ProfilerGraphAdapter.h # RenderGraph bridge
├── src/
│   ├── ProfilerSystem.cpp
│   ├── RollingStats.cpp
│   ├── DeviceCapabilities.cpp
│   ├── MetricsCollector.cpp
│   ├── MetricsExporter.cpp
│   ├── BenchmarkConfig.cpp
│   ├── SceneInfo.cpp
│   └── BenchmarkRunner.cpp
└── tests/
    └── test_profiler.cpp      # 53 unit tests
```

---

## Todo List - Metrics Implementation Status

### Standalone Components ✅ COMPLETE
- [x] frame_time_ms, fps, p99, stddev
- [x] gpu_time_ms (VkQueryPool)
- [x] ray_throughput_mrays, primary_rays_cast
- [x] vram_usage_mb, vram_budget_mb
- [x] gpu_name, driver_version, vram_gb
- [x] JSON export (Section 5.2 schema)
- [x] SceneInfo data structure
- [x] Bandwidth estimation fallback
- [x] BenchmarkRunner framework
- [x] Config validation

### Integration Tasks ⏳ NEXT
- [ ] BenchmarkGraphFactory (switchable pipelines)
- [ ] SubGraph extraction (reusable node clusters)
- [ ] Wire profiler to NodeInstance hooks
- [ ] Scene extractors (VoxelGridNode → SceneInfo)
- [ ] Shader counters (avg_voxels_per_ray)

### Deferred
- [ ] VK_KHR_performance_query (hardware bandwidth)
- [ ] gpu_utilization_percent (vendor-specific)
- [ ] Nsight Graphics validation

---

**End of Active Context**
