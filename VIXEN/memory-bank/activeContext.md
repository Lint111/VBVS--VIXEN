# Active Context

**Last Updated**: December 3, 2025
**Current Branch**: `claude/phase-i-performance-profiling`
**Status**: Phase I Graph Integration IN PROGRESS - BenchmarkGraphFactory Complete

---

## Session Summary

Implemented BenchmarkGraphFactory with reusable subgraph builders for switchable pipeline configurations.

### Completed This Session
- **BenchmarkGraphFactory** - Complete factory for building benchmark graphs
  - `InfrastructureNodes`, `ComputePipelineNodes`, `RayMarchNodes`, `OutputNodes` structs
  - `BuildInfrastructure()` - Instance, window, device, swapchain, cmdPool, frameSync
  - `BuildComputePipeline()` - ShaderLib, descriptors, pipeline, dispatch
  - `BuildRayMarchScene()` - Camera, voxelGrid, input nodes
  - `BuildOutput()` - Present node
  - `ConnectComputeRayMarch()` - Wires all subgraphs together
  - `BuildComputeRayMarchGraph()` - High-level convenience method
- **64 unit tests passing** (up from 53)

### Files Created This Session
| File | Description |
|------|-------------|
| `libraries/Profiler/include/Profiler/BenchmarkGraphFactory.h` | Factory with node handle structs |
| `libraries/Profiler/src/BenchmarkGraphFactory.cpp` | Subgraph builders + connection logic |

### Files Modified This Session
| File | Change |
|------|--------|
| `libraries/Profiler/CMakeLists.txt` | Added BenchmarkGraphFactory, RenderGraph dependency |
| `libraries/Profiler/tests/test_profiler.cpp` | Added 11 BenchmarkGraphFactory tests |

---

## Current Focus: Profiler Hook Wiring

### Next Steps (Priority Order)

1. **Wire ProfilerGraphAdapter hooks** - Connect lifecycle hooks to BenchmarkGraphFactory graphs
2. **Integrate with BenchmarkRunner** - Use factory in test execution loop
3. **End-to-end benchmark test** - Run actual benchmark with metrics export

### Existing Hook Infrastructure

GraphLifecycleHooks already exists in RenderGraph:
```cpp
// RenderGraph::GetLifecycleHooks()
hooks.RegisterFrameHook(FramePhase::Begin, callback);
hooks.RegisterFrameHook(FramePhase::End, callback);
hooks.RegisterNodeHook(NodeLifecyclePhase::PreExecute, callback);
hooks.RegisterNodeHook(NodeLifecyclePhase::PostExecute, callback);
```

ProfilerGraphAdapter already implements callbacks:
```cpp
ProfilerGraphAdapter adapter;
adapter.OnFrameBegin(cmdBuffer, frameIndex);
adapter.OnFrameEnd(frameIndex);
adapter.OnDispatchBegin();
adapter.OnDispatchEnd(width, height);
```

---

## Phase I Progress Summary

### Standalone Components ✅ COMPLETE

| Component | Status | Description |
|-----------|--------|-------------|
| ProfilerSystem | ✅ | Singleton orchestrator |
| RollingStats | ✅ | Sliding window percentiles |
| DeviceCapabilities | ✅ | GPU info capture |
| MetricsCollector | ✅ | VkQueryPool timing + VRAM |
| MetricsExporter | ✅ | CSV + JSON (Section 5.2) |
| BenchmarkConfig | ✅ | JSON config loader + validation |
| SceneInfo | ✅ | Resolution, density, type |
| BenchmarkRunner | ✅ | Test matrix framework |
| Bandwidth Estimation | ✅ | Fallback calculation |

### Graph Integration ✅ COMPLETE

| Component | Status | Description |
|-----------|--------|-------------|
| BenchmarkGraphFactory | ✅ | Factory class with Build* methods |
| InfrastructureNodes | ✅ | Device, window, swapchain, sync |
| ComputePipelineNodes | ✅ | Shader, descriptors, dispatch |
| RayMarchNodes | ✅ | Camera, voxelGrid |
| OutputNodes | ✅ | Present |
| ConnectComputeRayMarch | ✅ | Subgraph wiring |

### Hook Wiring ⏳ IN PROGRESS

| Task | Status | Description |
|------|--------|-------------|
| Wire ProfilerGraphAdapter | ⏳ | Connect to graph lifecycle |
| Integrate BenchmarkRunner | ⏳ | Factory in test loop |
| End-to-end test | ⏳ | Full benchmark run |

**Test Suite: 64 tests passing**

---

## BenchmarkGraphFactory Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                    BenchmarkGraphFactory                             │
├─────────────────────────────────────────────────────────────────────┤
│  BuildInfrastructure()     → InfrastructureNodes                    │
│  BuildComputePipeline()    → ComputePipelineNodes                   │
│  BuildRayMarchScene()      → RayMarchNodes                          │
│  BuildOutput()             → OutputNodes                            │
│  ConnectComputeRayMarch()  → Wires all subgraphs                    │
│  BuildComputeRayMarchGraph() → High-level convenience               │
└─────────────────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────────────────┐
│  BenchmarkGraph struct                                               │
│  ├── InfrastructureNodes infra                                      │
│  ├── ComputePipelineNodes compute                                   │
│  ├── RayMarchNodes rayMarch                                         │
│  └── OutputNodes output                                             │
└─────────────────────────────────────────────────────────────────────┘
```

### Node Handle Structs

```cpp
struct InfrastructureNodes {
    NodeHandle instance, window, device, swapchain, commandPool, frameSync;
    bool IsValid() const;
};

struct ComputePipelineNodes {
    NodeHandle shaderLib, descriptorGatherer, pushConstantGatherer;
    NodeHandle descriptorSet, pipeline, dispatch;
    bool IsValid() const;
};

struct RayMarchNodes {
    NodeHandle camera, voxelGrid, input;
    bool IsValid() const;
};

struct OutputNodes {
    NodeHandle present, debugCapture;
    bool IsValid() const;
};
```

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
5. **Fragment pipeline**: BuildFragmentRayMarchGraph() not yet implemented

---

## Profiler Library Structure (Updated)

```
libraries/Profiler/
├── CMakeLists.txt
├── include/Profiler/
│   ├── ProfilerSystem.h        # Singleton orchestrator
│   ├── RollingStats.h          # Sliding window statistics
│   ├── DeviceCapabilities.h    # GPU info capture
│   ├── FrameMetrics.h          # Data structures + PipelineType enum
│   ├── MetricsCollector.h      # VkQueryPool timing + VRAM
│   ├── MetricsExporter.h       # CSV/JSON output (Section 5.2)
│   ├── BenchmarkConfig.h       # JSON config loading + validation
│   ├── SceneInfo.h             # Scene metadata structure
│   ├── BenchmarkRunner.h       # Test matrix harness
│   ├── BenchmarkGraphFactory.h # Graph factory with subgraph builders
│   └── ProfilerGraphAdapter.h  # RenderGraph bridge
├── src/
│   ├── ProfilerSystem.cpp
│   ├── RollingStats.cpp
│   ├── DeviceCapabilities.cpp
│   ├── MetricsCollector.cpp
│   ├── MetricsExporter.cpp
│   ├── BenchmarkConfig.cpp
│   ├── SceneInfo.cpp
│   ├── BenchmarkRunner.cpp
│   └── BenchmarkGraphFactory.cpp
└── tests/
    └── test_profiler.cpp       # 64 unit tests
```

---

## Todo List - Implementation Status

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

### Graph Integration ✅ COMPLETE
- [x] BenchmarkGraphFactory header + node structs
- [x] BuildInfrastructure() - device, window, swapchain
- [x] BuildComputePipeline() - shader, descriptors, dispatch
- [x] BuildRayMarchScene() - camera, voxel grid
- [x] BuildOutput() - present node
- [x] ConnectComputeRayMarch() - subgraph wiring
- [x] BuildComputeRayMarchGraph() - full assembly

### Hook Wiring ⏳ IN PROGRESS
- [ ] Wire ProfilerGraphAdapter to graph lifecycle hooks
- [ ] Integrate BenchmarkRunner with BenchmarkGraphFactory
- [ ] End-to-end benchmark test

### Future Pipelines
- [ ] BuildFragmentRayMarchGraph()
- [ ] BuildHardwareRTGraph()
- [ ] BuildHybridGraph()

### Deferred
- [ ] VK_KHR_performance_query (hardware bandwidth)
- [ ] gpu_utilization_percent (vendor-specific)
- [ ] Nsight Graphics validation
- [ ] Shader counters (avg_voxels_per_ray)

---

**End of Active Context**
