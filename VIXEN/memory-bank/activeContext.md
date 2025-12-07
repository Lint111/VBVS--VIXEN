# Active Context

**Last Updated**: December 7, 2025
**Current Branch**: `claude/phase-k-hardware-rt`
**Status**: Phase K IN PROGRESS - Hardware RT Pipeline Working

---

## Latest Session (2025-12-07)

**RTX pipeline scene switching fixed!** All scenes (cornell, noise, tunnels, cityscape) now render correctly in hardware RT mode.

### Quick Reference
- **Next Task:** Implement proper compressed RTX (requires brick index mapping from VoxelAABBConverterNode)
- **RTX Status:** Working for all scenes; compressed variant uses material fallback (renders identically to uncompressed)

---

## Session Summary

### Completed This Session

| Fix | File | Description |
|-----|------|-------------|
| Scene switching | `BenchmarkGraphFactory.cpp` | `ConfigureHardwareRTParams` now passes scene type to `VoxelAABBConverterNode` |
| Compressed shader fallback | `VoxelRT_Compressed.rchit` | Falls back to `materialIdBuffer` lookup (same as uncompressed) |
| SDI regeneration | Used `sdi_tool.exe` | Regenerated VoxelRTNames.h with correct binding names |

### Root Cause: Scene Switching Bug
`VoxelAABBConverterNode` defaulted to `"cornell"` scene type because `ConfigureHardwareRTParams` didn't set the `scene_type` parameter. Fixed by adding `SceneInfo` parameter to the function.

### Root Cause: Compressed Shader Artifacts
**Problem:** Compressed RTX shader tried to compute `brickIndex` from spatial position, but:
1. RTX AABBs are sparse (only solid voxels), not a dense grid
2. `compressedColors[brickIndex * 32 + blockIdx]` requires brick-to-primitive mapping
3. `VoxelAABBConverterNode` only outputs `materialIds[primitiveID]`, not brick indices

**Current Workaround:** Compressed shader uses material-based coloring (same as uncompressed):
```glsl
uint matID = materialIdBuffer.materialIds[gl_PrimitiveID];
vec3 baseColor = getMaterialColor(matID);
```

---

## Handoff: Implementing Proper Compressed RTX

### The Problem
For compressed RTX to work, the shader needs `(brickIndex, localVoxelIdx)` per primitive to access:
- `compressedColors[brickIndex * 32 + blockIdx]`
- `compressedNormals[brickIndex * 32 + blockIdx]`

But `VoxelAABBConverterNode` only outputs `materialIds[]`.

### Solution Options

**Option 1: Add Brick Mapping Buffer** (Recommended)
Modify `VoxelAABBConverterNode` to output additional buffer:
```cpp
struct VoxelBrickMapping {
    uint16_t brickIndex;      // Index into compressed buffer arrays
    uint16_t localVoxelIdx;   // Position within brick (0-511)
};
// Output: VkBuffer brickMappingBuffer - one entry per AABB primitive
```

Shader access:
```glsl
layout(binding = 8) readonly buffer BrickMappingBuffer {
    uvec2 brickMapping[];  // Packed (brickIndex, localVoxelIdx)
};

void main() {
    uvec2 mapping = brickMapping[gl_PrimitiveID];
    uint brickIndex = mapping.x;
    int voxelLinearIdx = int(mapping.y);
    vec3 color = getCompressedVoxelColor(brickIndex, voxelLinearIdx);
}
```

**Option 2: Store Voxel Coordinates**
Output `(x, y, z)` per primitive and compute brick index in shader. Requires 12 bytes/primitive vs 4 bytes for Option 1.

**Option 3: Keep Material Fallback**
Compressed RTX renders same as uncompressed. Simpler but no memory benefit.

### Files to Modify for Option 1
1. `VoxelAABBConverterNodeConfig.h` - Add `BRICK_MAPPING_BUFFER` output slot
2. `VoxelAABBConverterNode.cpp` - Generate brick mapping during AABB extraction
3. `BenchmarkGraphFactory.cpp` - Wire new buffer to descriptor gatherer
4. `VoxelRT_Compressed.rchit` - Use brick mapping for compressed buffer access

### Current Descriptor Bindings (VoxelRT)
| Binding | Resource | Type |
|---------|----------|------|
| 0 | outputImage | STORAGE_IMAGE |
| 1 | topLevelAS | ACCELERATION_STRUCTURE_KHR |
| 2 | aabbBuffer | STORAGE_BUFFER |
| 3 | materialIdBuffer | STORAGE_BUFFER |
| 5 | octreeConfig | UNIFORM_BUFFER |
| 6 | compressedColors | STORAGE_BUFFER (compressed only) |
| 7 | compressedNormals | STORAGE_BUFFER (compressed only) |
| 8 | brickMapping | STORAGE_BUFFER (TODO - compressed only) |

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
