# Active Context

**Last Updated**: December 3, 2025
**Current Branch**: `claude/phase-i-performance-profiling`
**Status**: Phase II IN PROGRESS - Export Complete (131 Tests Passing)

---

## Session Summary

Completed export of compute ray march graph from VulkanGraphApplication to BenchmarkGraphFactory. Added ConnectVariadic resource wiring and shader builder registration.

### Completed This Session
- **WireVariadicResources()** - New method in BenchmarkGraphFactory
  - Wires descriptor bindings (0-5) for VoxelRayMarch.comp
  - Wires push constant bindings (camera data fields)
  - Uses hardcoded binding indices matching VoxelRayMarchNames.h
  - Proper SlotRole assignment (Dependency, Execute, Debug)
- **RegisterVoxelRayMarchShader()** - Shader builder registration
  - Registers ShaderBundleBuilder callback for VoxelRayMarch.comp
  - Supports compressed variant (useCompressed flag)
  - Configures include paths for shader preprocessing
- **BuildComputeRayMarchGraph() Updated** - Now includes full wiring
  - Calls WireVariadicResources after typed connections
  - Calls RegisterVoxelRayMarchShader for shader loading
- **5 New Unit Tests** for export functionality (126 -> 131)
  - Null graph handling
  - Binding layout documentation tests
  - Push constant layout documentation tests

### Files Modified This Session
| File | Change |
|------|--------|
| `libraries/Profiler/include/Profiler/BenchmarkGraphFactory.h` | Added WireVariadicResources, RegisterVoxelRayMarchShader declarations |
| `libraries/Profiler/src/BenchmarkGraphFactory.cpp` | Implemented variadic wiring + shader registration |
| `libraries/Profiler/CMakeLists.txt` | Added ShaderManagement dependency |
| `libraries/Profiler/tests/test_profiler.cpp` | Added 5 new export tests (126 -> 131) |

### Binding Layout (VoxelRayMarch.comp)
**Descriptor Set 0:**
- Binding 0: outputImage (storage image)
- Binding 1: esvoNodes (SSBO)
- Binding 2: brickData (SSBO)
- Binding 3: materials (SSBO)
- Binding 4: traceWriteIndex (SSBO, debug)
- Binding 5: octreeConfig (UBO)

**Push Constants (64 bytes):**
- 0: cameraPos (vec3)
- 1: time (float)
- 2: cameraDir (vec3)
- 3: fov (float)
- 4: cameraUp (vec3)
- 5: aspect (float)
- 6: cameraRight (vec3)
- 7: debugMode (int32)

---

## Current Focus: Phase II Vulkan Integration

### VulkanIntegration API

```cpp
// Option 1: One-liner initialization from compiled graph
if (VulkanIntegrationHelper::InitializeProfilerFromGraph(graph, "main_device")) {
    ProfilerSystem::Instance().StartTestRun(config);
    // ... render frames ...
    ProfilerSystem::Instance().EndTestRun();
}

// Option 2: RAII scoped integration
{
    ScopedProfilerIntegration profiler(graph);
    if (profiler.IsValid()) {
        ProfilerSystem::Instance().StartTestRun(config);
        // ... render frames ...
        ProfilerSystem::Instance().EndTestRun();
    }
} // Profiler automatically shutdown

// Option 3: High-level batch runner
size_t passed = VulkanIntegrationHelper::RunBenchmarkSuite(
    graph,
    BenchmarkConfigLoader::GetQuickTestMatrix(),
    "benchmarks/results",
    [&]() { return graph->RenderFrame() == VK_SUCCESS; }
);

// Option 4: Manual handle extraction
VulkanHandles handles = VulkanIntegrationHelper::ExtractFromGraph(graph);
if (handles.IsValid()) {
    ProfilerSystem::Instance().Initialize(handles.device, handles.physicalDevice);
}
```

### Hook Wiring API

```cpp
// Wire profiler hooks to graph lifecycle
ProfilerGraphAdapter adapter;
BenchmarkGraphFactory::WireProfilerHooks(graph, adapter, benchGraph);

// In render loop:
adapter.SetFrameContext(cmdBuffer, frameIndex);
adapter.OnFrameBegin();
// ... node execution ...
adapter.OnDispatchEnd(dispatchWidth, dispatchHeight);
adapter.OnFrameEnd();
```

### BenchmarkRunner Integration

```cpp
BenchmarkRunner runner;
runner.SetDeviceCapabilities(deviceCaps);
runner.SetTestMatrix(matrix);
runner.SetRenderDimensions(1920, 1080);
runner.StartSuite();

while (runner.BeginNextTest()) {
    // Create graph for current test config
    auto graph = runner.CreateGraphForCurrentTest(renderGraph);

    // Render loop with profiler callbacks
    runner.GetAdapter().SetFrameContext(cmdBuffer, frameIndex);
    runner.GetAdapter().OnFrameBegin();
    // ... execute graph ...
    runner.GetAdapter().OnFrameEnd();

    runner.RecordFrame(metrics);
    if (runner.IsCurrentTestComplete()) {
        runner.FinalizeCurrentTest();
        runner.ClearCurrentGraph();
    }
}
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
| FragmentPipelineNodes | ✅ | Fragment shader, render pass, attachments |
| OutputNodes | ✅ | Present |
| ConnectComputeRayMarch | ✅ | Subgraph wiring |
| BuildComputeRayMarchGraph | ✅ | Full compute pipeline assembly |
| BuildFragmentRayMarchGraph | ✅ | Full fragment pipeline assembly (stub) |
| BuildHardwareRTGraph | ✅ | Full hardware RT assembly (stub) |

### Hook Wiring ✅ COMPLETE

| Task | Status | Description |
|------|--------|-------------|
| WireProfilerHooks() | ✅ | Connect ProfilerGraphAdapter to graph lifecycle |
| BenchmarkRunner integration | ✅ | Graph factory + adapter in test loop |
| Hook wiring tests | ✅ | 25 new tests for hook wiring |
| End-to-end benchmark tests | ✅ | 10 new tests for full profiler stack |
| Fragment/Hardware RT stubs | ✅ | Documented stub builders + FragmentPipelineNodes |
| Shader counters | ✅ | ShaderCounters struct in FrameMetrics.h |

**Test Suite: 116 tests passing** (89 -> 116, +27 new tests this session)

### Known Optimization (Deferred)

**Node-specific hook registration** - Current `GraphLifecycleHooks` broadcasts to ALL nodes, then filters by name inside callback (O(N) per hook per frame). Same pattern used in `TypedConnection::ConnectVariadic`.

Future improvement: Extend `GraphLifecycleHooks` with node-filtered registration:
```cpp
void RegisterNodeHook(phase, NodeHandle nodeHandle, callback, debugName);  // O(1) dispatch
```

See TODO in `BenchmarkGraphFactory.cpp:594-602`.

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
4. **Hardware RT**: VK_KHR_ray_tracing_pipeline documented in stub, requires Phase II implementation
5. **Fragment pipeline**: BuildFragmentRayMarchGraph() documented in stub, requires Phase II implementation
6. **Shader counters**: Stubs defined, require GLSL query integration in Phase II

---

## Profiler Library Structure (Final)

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
│   ├── BenchmarkRunner.h       # Test matrix harness + graph mgmt
│   ├── BenchmarkGraphFactory.h # Graph factory + hook wiring
│   ├── ProfilerGraphAdapter.h  # RenderGraph bridge
│   └── VulkanIntegration.h     # NEW: Phase II - VkDevice extraction
├── src/
│   ├── ProfilerSystem.cpp
│   ├── RollingStats.cpp
│   ├── DeviceCapabilities.cpp
│   ├── MetricsCollector.cpp
│   ├── MetricsExporter.cpp
│   ├── BenchmarkConfig.cpp
│   ├── SceneInfo.cpp
│   ├── BenchmarkRunner.cpp
│   ├── BenchmarkGraphFactory.cpp
│   └── VulkanIntegration.cpp  # NEW: Phase II
└── tests/
    └── test_profiler.cpp       # 131 unit/integration tests
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

### Hook Wiring ✅ COMPLETE
- [x] Wire ProfilerGraphAdapter to graph lifecycle hooks
- [x] Integrate BenchmarkRunner with BenchmarkGraphFactory
- [x] Integration tests (25 new tests)

### Future Pipelines ✅ STUBS COMPLETE
- [x] BuildFragmentRayMarchGraph() - Fragment builder + FragmentPipelineNodes struct
- [x] BuildHardwareRTGraph() - Hardware RT stub with VK_KHR_ray_tracing_pipeline docs
- [ ] BuildHybridGraph() (Phase II)

### Phase I Deferred Tasks ✅ COMPLETE
- [x] End-to-end benchmark tests (10 new tests)
- [x] Shader counters struct (avg_voxels_per_ray, cache_line_hits)
- [x] Fragment pipeline documentation

### Phase II - Vulkan Integration (IN PROGRESS)
- [x] VulkanIntegration.h/.cpp - Vulkan handle extraction from RenderGraph
- [x] VulkanHandles struct - device, physicalDevice, queue, framesInFlight
- [x] VulkanIntegrationHelper - ExtractFromGraph, InitializeProfilerFromGraph
- [x] ScopedProfilerIntegration - RAII wrapper for profiler lifecycle
- [x] 10 new unit tests (116 -> 126 total)
- [x] WireVariadicResources() - Descriptor bindings + push constant wiring
- [x] RegisterVoxelRayMarchShader() - Shader builder registration
- [x] BuildComputeRayMarchGraph() fully wired with ConnectVariadic
- [x] 5 new export tests (126 -> 131 total)
- [ ] GPU integration test (requires running Vulkan application)
- [ ] VK_KHR_ray_tracing_pipeline (Hardware RT implementation)
- [ ] GLSL shader counter queries
- [ ] VK_KHR_performance_query (hardware bandwidth)
- [ ] gpu_utilization_percent (vendor-specific)
- [ ] Nsight Graphics validation
- [ ] BuildHybridGraph() implementation

---

**End of Active Context**
