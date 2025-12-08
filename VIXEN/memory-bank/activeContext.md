# Active Context

**Last Updated**: December 7, 2025
**Current Branch**: `claude/phase-k-hardware-rt`
**Status**: Phase K IN PROGRESS - Hardware RT Pipeline Working

---

## Latest Session (2025-12-07 - Session 2)

**Brick Index Mismatch Bug Investigation & Fix Attempt**

### Issue Identified
Compressed RTX rendering showed incorrect colors (some tests pure black, others partial color loss). Root cause: **brick index mismatch** between:
- VoxelAABBConverterNode: Uses simple ZYX grid order (`brickZ * bricksPerAxis² + brickY * bricksPerAxis + brickX`)
- LaineKarrasOctree: Uses **Morton-sorted insertion order** for brick indices

### Changes Made This Session

| Task | Files | Description |
|------|-------|-------------|
| Fixed localVoxelIdx order | `VoxelAABBConverterNode.cpp:201-209` | Changed from ZYX to XYZ to match SVORebuild.cpp |
| Added BRICK_GRID_LOOKUP_BUFFER | `VoxelGridNodeConfig.h:89-95, 150-154` | New output slot (index 7) for brick grid lookup |
| Lookup buffer creation | `VoxelGridNode.cpp:440-476` | Creates CPU-side lookup from brickGridToBrickView |
| Lookup buffer cleanup | `VoxelGridNode.cpp:840-851` | Proper resource cleanup |
| Added input slot | `VoxelAABBConverterNodeConfig.h:118-125` | BRICK_GRID_LOOKUP_BUFFER input slot |
| Wired connection | `BenchmarkGraphFactory.cpp:1270-1273` | VoxelGrid → VoxelAABBConverter connection |

### Technical Details

**Local Voxel Index Fix (XYZ order to match compression):**
```cpp
// Must match SVORebuild.cpp: x = voxelLinearIdx & 7, y = (>>3)&7, z = (>>6)&7
// So: voxelLinearIdx = x + y*8 + z*64
uint32_t localVoxelIdx = localX +
                        localY * BRICK_SIZE +
                        localZ * BRICK_SIZE * BRICK_SIZE;
```

**Brick Grid Lookup Buffer:**
- Size: `bricksPerAxis³ * sizeof(uint32_t)`
- Maps grid coord `(brickX, brickY, brickZ)` to Morton-sorted brick index
- 0xFFFFFFFF for empty bricks
- Populated from `octreeData->root->brickGridToBrickView`

### Known Issue (Deferred)
VoxelAABBConverterNode still needs to **consume** the lookup buffer to get correct brick indices. Current implementation computes grid-based brick index which doesn't match Morton-sorted octree order.

**TODO:** Have VoxelAABBConverterNode use the lookup buffer data when generating brick mappings.

---

## Previous Session (2025-12-07 - Session 1)

**Compressed RTX implementation complete!** Brick mapping buffer now enables true DXT-compressed color access in RTX shaders.

### Quick Reference
- **Completed:** Brick mapping buffer implementation for compressed RTX
- **RTX Status:** VoxelRT_Compressed.rchit uses binding 8 for `(brickIndex, localVoxelIdx)` lookup
- **sdi_tool:** Added `-I`/`--include` flags for #include directive resolution

### Current Descriptor Bindings (VoxelRT_Compressed)
| Binding | Resource | Type |
|---------|----------|------|
| 0 | outputImage | STORAGE_IMAGE |
| 1 | topLevelAS | ACCELERATION_STRUCTURE_KHR |
| 2 | aabbBuffer | STORAGE_BUFFER |
| 3 | materialIdBuffer | STORAGE_BUFFER |
| 5 | octreeConfig | UNIFORM_BUFFER |
| 6 | compressedColors | STORAGE_BUFFER |
| 7 | compressedNormals | STORAGE_BUFFER |
| 8 | brickMapping | STORAGE_BUFFER ✅ |

---

### Completed Previous Session (December 6, 2025)

**Documentation Maintenance:**
- Comprehensive inventory of all documentation files (90+ markdown files)
- Obsidian vault (Vixen-Docs/) audited and synchronized
- Updated Current-Status.md with Phase J completion
- Updated Home.md with current phase status
- Updated DOCUMENTATION_INDEX.md with December 6 changes
- Updated memory-bank/projectbrief.md with recent completions
- Verified all wikilinks and Mermaid diagrams in vault

### Completed December 5, 2025

**Fragment Pipeline Implementation:**
Fixed multiple issues to enable VoxelRayMarch.frag rendering through the graphics pipeline.

| Fix | File | Description |
|-----|------|-------------|
| Config auto-load | `BenchmarkCLI.cpp:577-594` | Searches exe-relative paths for config file |
| Node type registration | `BenchmarkRunner.cpp:341-345` | Added RenderPassNodeType, FramebufferNodeType, GraphicsPipelineNodeType, GeometryRenderNodeType |
| DescriptorSet connections | `BenchmarkGraphFactory.cpp:836-841` | Added SWAPCHAIN_IMAGE_COUNT, IMAGE_INDEX connections |
| Framebuffer connections | `BenchmarkGraphFactory.cpp:856-867` | Added Device/RenderPass/SwapChain → Framebuffer |
| Validation error fix | `RenderGraph.cpp:620-622` | Improved error message with node type and slot name |
| Skip Invalid slots | `DescriptorResourceGathererNode.cpp:400-403` | Skip SlotState::Invalid during validation |
| Push constant support | `GeometryRenderNodeConfig.h:159-171` | Added PUSH_CONSTANT_DATA (slot 15), PUSH_CONSTANT_RANGES (slot 16) |
| SetPushConstants() | `GeometryRenderNode.cpp:373-400` | vkCmdPushConstants call for camera data |
| Push constant wiring | `BenchmarkGraphFactory.cpp:948-952` | PushConstantGatherer → GeometryRenderNode |

**Result:** Fragment pipeline renders voxels with proper camera control via push constants.

---

### Previous Session (December 4, 2025)

**Shader Refactoring (Code Quality):**
Created 6 shared GLSL include files to eliminate ~1300 lines of duplicate code between VoxelRayMarch.comp and VoxelRayMarch_Compressed.comp.

| Include File | Size | Content |
|-------------|------|---------|
| RayGeneration.glsl | 3.8KB | `getRayDir()`, `rayAABBIntersection()`, coord transforms |
| CoordinateTransforms.glsl | 5.7KB | ESVO/grid/brick space conversions, octant mirroring |
| ESVOCoefficients.glsl | 4.2KB | `RayCoefficients` struct, `initRayCoefficients()` |
| ESVOTraversal.glsl | 16.3KB | PUSH/ADVANCE/POP phases, `StackEntry`, `TraversalState` |
| TraceRecording.glsl | 8.1KB | Debug capture infrastructure, `DebugRaySample` |
| Lighting.glsl | 1.8KB | `computeLighting()`, shading helpers |

**Size Reduction:**
- VoxelRayMarch.comp: 80KB → 20KB (**75% smaller**)
- VoxelRayMarch_Compressed.comp: 56KB → 22KB (**60% smaller**)

**What's Shared (via includes):**
- ESVO traversal algorithm (PUSH/ADVANCE/POP phases)
- Ray coefficient initialization
- Coordinate space transformations
- Debug trace recording
- Lighting calculations

**What's Shader-Specific (kept in .comp):**
- Buffer bindings and UBOs
- Brick DDA (`marchBrickFromPos` vs `marchBrickFromPosCompressed`)
- `handleLeafHit` (different brick data access patterns)
- `traverseOctree()` main loop (calls shader-specific handleLeafHit)

**Previous Session - VoxelDataCache (Performance Optimization):**
Added caching system to avoid regenerating the same voxel scene multiple times during benchmark runs.

```cpp
// Cache stores VoxelGrid by (sceneType, resolution) key
const VoxelGrid* VoxelDataCache::GetOrGenerate(sceneType, resolution, params);
VoxelDataCache::Clear();           // Free memory
VoxelDataCache::GetStats();        // Returns (hits, misses)
VoxelDataCache::SetEnabled(bool);  // Enable/disable caching
```

**Verified Working:**
- ✅ Multiple scenes: cornell (23%), noise (53%), tunnels (94%), cityscape (28%)
- ✅ Multiple shaders: VoxelRayMarch.comp, VoxelRayMarch_Compressed.comp
- ✅ Multiple resolutions: 64³, 128³, 256³
- ✅ Incremental export: Each test exports JSON immediately after completion
- ✅ Cache hits/misses: Second request for same (scene, resolution) uses cache

**Cache Output Example:**
```
[VoxelDataCache] MISS: Generating cornell @ 64^3...
[VoxelDataCache] Generated cornell @ 64^3, density=23.3429%
[VoxelDataCache] HIT: cornell @ 64^3 (hits=1, misses=1)
```

### Files Modified This Session
| File | Change |
|------|--------|
| `shaders/RayGeneration.glsl` | **NEW** - Ray setup, AABB intersection, scale mappings |
| `shaders/CoordinateTransforms.glsl` | **NEW** - Space conversions, octant mirroring |
| `shaders/ESVOCoefficients.glsl` | **NEW** - RayCoefficients struct + init |
| `shaders/ESVOTraversal.glsl` | **NEW** - PUSH/ADVANCE/POP phases, state structs |
| `shaders/TraceRecording.glsl` | **NEW** - Debug capture infrastructure |
| `shaders/Lighting.glsl` | **NEW** - Shading functions |
| `shaders/VoxelRayMarch.comp` | Refactored to use includes (80KB → 20KB) |
| `shaders/VoxelRayMarch_Compressed.comp` | Refactored to use includes (56KB → 22KB) |

**Previous Session Files:**
| File | Change |
|------|--------|
| `libraries/RenderGraph/include/Data/SceneGenerator.h:398-478` | Added VoxelDataCache class |
| `libraries/RenderGraph/src/Data/SceneGenerator.cpp:116-232` | VoxelDataCache implementation |
| `libraries/RenderGraph/src/Nodes/VoxelGridNode.cpp:128-149` | Use cache in CompileImpl |

### Test Results
```
$ ./binaries/vixen_benchmark.exe --config ./application/benchmark/benchmark_config.json --render -i 100 -w 10

Output: test_results_cache/
  COMPUTE_64_CORNELL_VOXELRAYMARCH.COMP_RUN1.json
  COMPUTE_64_CORNELL_VOXELRAYMARCH_COMPRESSED.COMP_RUN2.json
  COMPUTE_64_NOISE_VOXELRAYMARCH.COMP_RUN3.json
  ... (18 test files exported incrementally)
```

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
5. ~~**Fragment pipeline**: BuildFragmentRayMarchGraph() documented in stub~~ **RESOLVED** ✅ (December 5, 2025)
6. **Shader counters**: Stubs defined, require GLSL query integration in Phase II
7. **Window resolution bug**: Changing render resolution in benchmark config doesn't resize window, only shifts render origin. Likely issue in WindowNode or swapchain configuration - window size stays fixed while compute dispatch dimensions change.

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

### Future Pipelines
- [x] BuildFragmentRayMarchGraph() - **COMPLETE** ✅ Full implementation with push constants
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
- [x] vixen_benchmark executable wired to real framework
  - [x] --headless mode (compute-only, synthetic metrics)
  - [x] --render mode (RenderGraph with window)
  - [x] BenchmarkRunner integration
  - [x] VulkanContext headless initialization
- [x] VoxelDataCache - Caches voxel grids by (scene, resolution) for benchmark speedup
- [x] Incremental export - Each test exports JSON immediately after completion
- [x] Multi-scene support verified (cornell, noise, tunnels, cityscape)
- [x] Multi-shader support verified (VoxelRayMarch.comp, VoxelRayMarch_Compressed.comp)
- [x] Shader refactoring - Created 6 shared GLSL include files, reduced duplication 60-75%

**Next Steps (Pipeline Expansion):**
- [x] BuildFragmentRayMarchGraph() - **COMPLETE** ✅ Full implementation with push constants
- [x] **VoxelRayMarch_Compressed.frag** - **COMPLETE** ✅ (December 6, 2025)
  - Created compressed fragment shader matching VoxelRayMarch_Compressed.comp
  - Wired compressed buffer bindings (6-7) in BenchmarkGraphFactory
- [x] BuildHardwareRTGraph() - **COMPLETE** ✅ VK_KHR_ray_tracing_pipeline implementation (December 7, 2025)
- [ ] BuildHybridGraph() - Combine compute + fragment approaches
- [ ] GPU integration test (requires running Vulkan application)
- [ ] GLSL shader counter queries
- [ ] VK_KHR_performance_query (hardware bandwidth)
- [ ] gpu_utilization_percent (vendor-specific)
- [ ] Nsight Graphics validation

**Shader Variants for Comparative Study:**
| Pipeline | Uncompressed | Compressed |
|----------|--------------|------------|
| Compute | ✅ VoxelRayMarch.comp | ✅ VoxelRayMarch_Compressed.comp |
| Fragment | ✅ VoxelRayMarch.frag | ✅ VoxelRayMarch_Compressed.frag |
| Hardware RT | ✅ VoxelRT.rgen/rmiss/rchit/rint | ⏳ Compressed variant |

---

**End of Active Context**
