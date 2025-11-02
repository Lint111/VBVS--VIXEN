# Technical Roadmap: Voxel Ray Tracing Research Integration

**Date**: November 2, 2025
**Status**: Planning Complete
**Research Goal**: Compare Vulkan ray tracing/marching pipelines for voxel rendering
**Timeline**: 18-24 weeks (post-Phase F completion)

---

## Executive Summary

This document outlines the technical implementation roadmap for integrating voxel ray tracing research capabilities into the VIXEN framework. The roadmap builds incrementally on completed infrastructure (Phases 0-F) and defers non-essential features (graph editor, advanced UI) to focus exclusively on research requirements.

**Core Research Question**: How do different Vulkan ray tracing and ray marching pipeline architectures affect rendering performance, GPU bandwidth utilization, and scalability for data-driven voxel rendering?

**Testing Matrix**: 4 pipelines × 5 resolutions × 3 densities × 3 algorithm variants = 180 configurations

---

## Current Infrastructure Status

### ✅ Completed (Ready for Research)
1. **RenderGraph System** - Node-based pipeline with compile-time type safety
2. **Resource Variant System** - Type-safe Vulkan resource management
3. **Cache System** - Performance metrics tracking (ShaderModuleCacher, PipelineCacher)
4. **SPIRV Reflection** - Automatic descriptor layout generation
5. **Multi-Rate Loop System** - Fixed timestep support for deterministic timing
6. **Synchronization** - Frame-in-flight, semaphores, fences
7. **Event System** - Invalidation cascade for dynamic scene updates
8. **CMake Build System** - Modular library structure

### 🔄 In Progress
- **Phase F** - Slot task system for array processing (16-21h remaining)

### ❌ Missing (Required for Research)
Critical gaps identified in 6 major phases (G-L) outlined below.

---

## Implementation Phases

### Phase F: Slot Task System (IN PROGRESS) ⚡
**Duration**: 16-21 hours
**Status**: Planning complete, ready for implementation

**Deliverables**:
- Slot metadata consolidation (SlotScope, SlotNullability, SlotMutability)
- Resource budget manager (device capability tracking)
- Three-tier lifecycle (Node → Task → Instance)
- Budget-based parallelism (hybrid allocation)

**Relevance to Research**: Enables parallel voxel loading/processing with automatic resource scaling.

**DO NOT INTERRUPT** - Complete Phase F before starting research phases.

---

### Phase G: Compute Shader Pipeline (RESEARCH FOUNDATION) 🎯
**Duration**: 2-3 weeks
**Priority**: HIGHEST - Simplest ray marching implementation
**Dependencies**: Phase F complete

#### Objectives
1. Validate profiling methodology with simplest pipeline
2. Establish baseline metrics before tackling hardware RT complexity
3. Test timestamp query accuracy and overhead

#### Implementation Tasks

**G.1: ComputePipelineNode** (8-10h)
```cpp
// Files to create:
RenderGraph/include/Nodes/ComputePipelineNodeConfig.h
RenderGraph/src/Nodes/ComputePipelineNode.cpp
CashSystem/include/CashSystem/ComputePipelineCacher.h
CashSystem/src/ComputePipelineCacher.cpp
```

**Key Features**:
- VkComputePipeline creation and caching
- Descriptor set layout auto-generation (reuse ShaderManagement)
- Push constant support
- Workgroup size extraction from SPIRV reflection

**G.2: Storage Resource Support** (4-6h)
```cpp
// Extend ResourceVariant.h:
REGISTER_RESOURCE_TYPE(VkBufferView, BufferViewDescriptor)
// Storage buffer/image binding slots
```

**Add to ResourceVariant**:
- `VkBufferView` (storage buffer views)
- Storage image layouts (VK_IMAGE_LAYOUT_GENERAL)

**G.3: Compute Ray Marching Implementation** (8-12h)
```cpp
// Shaders to create:
Shaders/VoxelRayMarch.comp  // Simple DDA ray marching

// Node to create:
RenderGraph/include/Nodes/ComputeRayMarchNodeConfig.h
RenderGraph/src/Nodes/ComputeRayMarchNode.cpp
```

**Shader Features**:
- Screen-space ray generation (inverse projection matrix)
- DDA voxel traversal algorithm
- Output to storage image (VK_FORMAT_R8G8B8A8_UNORM)
- Basic voxel lookup (3D texture sampling)

**G.4: Timestamp Query Integration** (6-8h)
```cpp
// Files to create:
RenderGraph/include/Nodes/TimestampQueryNodeConfig.h
RenderGraph/src/Nodes/TimestampQueryNode.cpp
```

**Key Features**:
- VkQueryPool creation (VK_QUERY_TYPE_TIMESTAMP)
- Begin/End query wrapping around compute dispatch
- Result readback with proper synchronization
- Double-buffered query pools (per-frame rotation)

**Success Criteria**:
- ✅ Compute shader renders voxel cube to screen
- ✅ Timestamp queries measure dispatch time (<0.1ms overhead)
- ✅ Manually adjustable voxel resolution (32³, 64³, 128³)
- ✅ Zero validation errors

---

### Phase H: Voxel Data Infrastructure 📦
**Duration**: 3-4 weeks
**Priority**: HIGH - Required for all pipelines
**Dependencies**: Phase G complete (storage resource support)

#### Objectives
1. Implement sparse voxel octree (SVO) data structure
2. Create voxel data generation/loading system
3. Support multiple density levels (sparse, medium, dense)

#### Implementation Tasks

**H.1: Octree Data Structure** (10-14h)
```cpp
// Files to create:
ResourceManagement/include/VoxelStructures/SparseVoxelOctree.h
ResourceManagement/src/VoxelStructures/SparseVoxelOctree.cpp
```

**Features**:
- Morton code-based spatial indexing
- Depth-limited octree (configurable max depth)
- Empty node pruning (memory efficiency)
- Serialization/deserialization (cache support)

**H.2: Voxel Data Generator** (8-12h)
```cpp
// Files to create:
ResourceManagement/include/VoxelGenerator.h
ResourceManagement/src/VoxelGenerator.cpp
```

**Generator Types**:
- Geometric primitives (sphere, cube, cylinder)
- Perlin noise terrain (organic shapes)
- Density-controlled generation (10%, 40%, 70%, 90% fill)
- Procedural scene templates (sparse architectural, dense organic)

**H.3: GPU Voxel Buffer Upload** (6-8h)
```cpp
// Files to create:
RenderGraph/include/Nodes/VoxelBufferNodeConfig.h
RenderGraph/src/Nodes/VoxelBufferNode.cpp
```

**Features**:
- Octree linearization (GPU-friendly flat buffer)
- Staging buffer upload (large dataset support)
- 3D texture alternative (for fragment shader pipeline)
- Dynamic voxel update support (for Phase K)

**H.4: Voxel Traversal Utilities** (4-6h)
```cpp
// Shaders to create:
Shaders/Include/VoxelTraversal.glsl  // Shared utilities

// Functions:
- OctreeTraverse(ray, octreeBuffer) -> hitT
- DDAMarch(ray, voxelGrid3D) -> hitT
- EmptySpaceSkip(ray, blockGrid) -> skipDist
```

**Success Criteria**:
- ✅ Octree stores 256³ voxel grid efficiently (<50MB for sparse)
- ✅ GPU buffer upload completes in <100ms
- ✅ Compute ray marching uses octree traversal
- ✅ Test scenes: sphere, terrain, architectural

---

### Phase I: Performance Profiling System 📊
**Duration**: 2-3 weeks
**Priority**: HIGH - Required for metrics collection
**Dependencies**: Phase G complete (timestamp queries)

#### Objectives
1. Automated per-frame metric collection
2. Statistical analysis (min/max/mean/stddev/percentiles)
3. CSV export for external analysis
4. GPU bandwidth monitoring via performance counters

#### Implementation Tasks

**I.1: Performance Profiler Core** (8-12h)
```cpp
// Files to create:
RenderGraph/include/Core/PerformanceProfiler.h
RenderGraph/src/Core/PerformanceProfiler.cpp
```

**Features**:
- Per-frame metric aggregation (frame time, GPU time, draw calls)
- Rolling statistics (configurable window size)
- Percentile calculation (1st, 50th, 99th)
- Memory-efficient circular buffer (fixed memory footprint)

**I.2: GPU Performance Counter Integration** (8-12h)
```cpp
// Files to create:
RenderGraph/include/Core/GPUCounters.h
RenderGraph/src/Core/GPUCounters.cpp
```

**Metrics**:
- Memory bandwidth (read/write GB/s) via VK_KHR_performance_query
- VRAM usage (VK_EXT_memory_budget)
- GPU utilization percentage (vendor-specific extensions)
- Ray throughput (derived: rays/sec = pixel_count / frame_time)

**Platform Support**:
- NVIDIA: Nsight Systems integration hooks (optional)
- AMD: RGP markers (optional)
- Fallback: Timestamp-based estimates

**I.3: CSV Export System** (4-6h)
```cpp
// Files to create:
RenderGraph/include/Core/MetricsExporter.h
RenderGraph/src/Core/MetricsExporter.cpp
```

**Output Format**:
```csv
frame,timestamp_ms,frame_time_ms,gpu_time_ms,bandwidth_read_gb,bandwidth_write_gb,vram_mb,rays_per_sec
0,0.0,16.7,14.2,23.4,8.1,2847,124000000
1,16.7,16.8,14.3,23.5,8.2,2847,123800000
```

**I.4: Benchmark Configuration System** (6-8h)
```cpp
// Files to create:
tests/Benchmarks/BenchmarkConfig.h
tests/Benchmarks/BenchmarkConfig.cpp
```

**Configuration Schema**:
```cpp
struct BenchmarkConfig {
    uint32_t voxelResolution;       // 32, 64, 128, 256, 512
    float densityPercent;           // 10, 40, 70, 90
    std::string sceneType;          // "sparse_architectural", "dense_organic"
    uint32_t warmupFrames;          // Discard first N frames
    uint32_t measurementFrames;     // Collect metrics for N frames
    std::string outputPath;         // CSV file path
};
```

**Success Criteria**:
- ✅ Profiler collects 300+ frames with <1% overhead
- ✅ CSV export includes all required metrics
- ✅ Bandwidth measurements within 5% of Nsight Graphics
- ✅ Configuration files drive test execution

---

### Phase J: Fragment Shader Ray Marching 🎨
**Duration**: 1-2 weeks
**Priority**: MEDIUM - Second pipeline implementation
**Dependencies**: Phase H complete (voxel data)

#### Objectives
1. Traditional rasterization-based ray marching
2. Comparison baseline for compute/RT pipelines
3. Validate shared voxel data structure

#### Implementation Tasks

**J.1: Fragment Ray March Pipeline** (8-12h)
```cpp
// Files to create:
RenderGraph/include/Nodes/FragmentRayMarchNodeConfig.h
RenderGraph/src/Nodes/FragmentRayMarchNode.cpp

// Shaders:
Shaders/FullscreenQuad.vert  // Trivial passthrough
Shaders/VoxelRayMarchFrag.frag  // Ray marching logic
```

**Shader Features**:
- Screen-space ray generation (same as compute)
- 3D texture sampling (voxel data as VkImage)
- DDA traversal in fragment shader
- Early-Z optimization (depth prepass optional)

**J.2: 3D Texture Support** (4-6h)
```cpp
// Extend TextureLoaderNode for 3D textures
// Add 3D sampler support to SamplerCacher
```

**Success Criteria**:
- ✅ Fragment shader renders same scene as compute pipeline
- ✅ Profiler measures fragment shader GPU time
- ✅ Bandwidth metrics collected
- ✅ Visual parity with compute shader output

---

### Phase K: Hardware Ray Tracing Pipeline ⚡
**Duration**: 4-5 weeks
**Priority**: HIGH - Core research pipeline
**Dependencies**: Phase H complete (voxel data)

#### Objectives
1. VK_KHR_ray_tracing_pipeline implementation
2. BLAS/TLAS acceleration structure management
3. Custom AABB intersection shaders for voxels

#### Implementation Tasks

**K.1: Ray Tracing Extension Setup** (4-6h)
```cpp
// Modify DeviceNode to enable extensions:
VK_KHR_ray_tracing_pipeline
VK_KHR_acceleration_structure
VK_KHR_deferred_host_operations  // For async BLAS builds

// Add feature flags:
VkPhysicalDeviceRayTracingPipelineFeaturesKHR
VkPhysicalDeviceAccelerationStructureFeaturesKHR
```

**K.2: Acceleration Structure Node** (12-16h)
```cpp
// Files to create:
RenderGraph/include/Nodes/AccelerationStructureNodeConfig.h
RenderGraph/src/Nodes/AccelerationStructureNode.cpp
CashSystem/include/CashSystem/AccelerationStructureCacher.h
```

**Features**:
- BLAS creation for voxel AABBs (one per voxel or hierarchical)
- TLAS creation (scene-level instances)
- Compaction support (reduce memory footprint)
- Dynamic rebuild for voxel modification (Phase L.3)

**K.3: Ray Tracing Pipeline Node** (14-18h)
```cpp
// Files to create:
RenderGraph/include/Nodes/RayTracingPipelineNodeConfig.h
RenderGraph/src/Nodes/RayTracingPipelineNode.cpp
CashSystem/include/CashSystem/RayTracingPipelineCacher.h

// Shaders:
Shaders/RayGen.rgen       // Ray generation
Shaders/VoxelAABB.rint    // AABB intersection test
Shaders/VoxelHit.rchit    // Closest hit shading
Shaders/Miss.rmiss        // Miss shader
```

**Shader Features**:
- Ray generation from camera (same logic as compute)
- Custom AABB intersection (voxel bounds testing)
- Closest hit shader (voxel color lookup)
- Shader binding table (SBT) management

**K.4: Ray Tracing Dispatch Node** (6-8h)
```cpp
// Files to create:
RenderGraph/include/Nodes/RayTracingDispatchNodeConfig.h
RenderGraph/src/Nodes/RayTracingDispatchNode.cpp
```

**Features**:
- vkCmdTraceRaysKHR invocation
- Shader binding table setup
- Output image resource management

**Success Criteria**:
- ✅ Hardware RT renders voxel scene correctly
- ✅ BLAS/TLAS build times measured
- ✅ Ray tracing performance metrics collected
- ✅ Dynamic BLAS rebuild works (for Phase L)

---

### Phase L: Pipeline Variants & Optimization 🚀
**Duration**: 3-4 weeks
**Priority**: HIGH - Research comparison completeness
**Dependencies**: Phases G, J, K complete (all base pipelines)

#### Objectives
1. Implement traversal optimizations for each pipeline
2. Add hybrid pipeline (compute + hardware RT)
3. Enable dynamic scene updates

#### Implementation Tasks

**L.1: Empty Space Skipping** (6-8h)
```cpp
// Shaders to modify:
Shaders/VoxelRayMarch.comp (compute)
Shaders/VoxelRayMarchFrag.frag (fragment)
Shaders/VoxelAABB.rint (hardware RT)

// Add block-level occupancy grid (coarse 8³ or 16³ blocks)
// Skip empty blocks during traversal
```

**L.2: Block-Based Traversal (BlockWalk)** (10-14h)
```cpp
// Implement hierarchical traversal (research paper inspiration)
// Two-level structure: coarse blocks + fine voxels
// Reduces ray-voxel intersection tests by 60-80%
```

**L.3: Dynamic Scene Updates** (8-12h)
```cpp
// Files to create:
RenderGraph/include/Nodes/VoxelUpdateNodeConfig.h
RenderGraph/src/Nodes/VoxelUpdateNode.cpp
```

**Features**:
- Low-frequency updates (1-10 voxels/frame)
- High-frequency updates (100-1000 voxels/frame)
- Triggers:
  - Compute/fragment: 3D texture update (vkCmdCopyBufferToImage)
  - Hardware RT: BLAS rebuild (partial or full)
- Measure update cost vs rendering cost

**L.4: Hybrid Pipeline** (12-16h)
```cpp
// Files to create:
RenderGraph/include/Nodes/HybridRayPipelineNodeConfig.h
RenderGraph/src/Nodes/HybridRayPipelineNode.cpp

// Strategy:
// 1. Compute shader: Primary rays + coarse traversal
// 2. Hardware RT: Fine detail or secondary rays (shadows, AO)
```

**Success Criteria**:
- ✅ Each optimization shows measurable bandwidth reduction
- ✅ Dynamic updates work for all pipelines
- ✅ Hybrid pipeline demonstrates performance advantage
- ✅ All algorithm variants implemented for comparison

---

### Phase M: Automated Testing Framework 🤖
**Duration**: 3-4 weeks
**Priority**: CRITICAL - Enables research execution
**Dependencies**: All pipeline phases (G, J, K, L) complete

#### Objectives
1. Automated test matrix execution (180 configurations)
2. Headless rendering mode (no window/interaction required)
3. Result aggregation and statistical analysis

#### Implementation Tasks

**M.1: Benchmark Runner** (10-14h)
```cpp
// Files to create:
tests/Benchmarks/BenchmarkRunner.h
tests/Benchmarks/BenchmarkRunner.cpp
tests/Benchmarks/main.cpp
```

**Features**:
- JSON test matrix specification (pipelines × resolutions × densities)
- Sequential test execution with proper cleanup
- Progress reporting (current test N/180)
- Automatic graph reconfiguration per test

**M.2: Headless Mode** (6-8h)
```cpp
// Modify WindowNode:
// - Optional offscreen rendering (VK_NULL_HANDLE surface)
// - Use VkImage instead of swapchain for output
// - Disable PresentNode in headless mode
```

**M.3: Camera Path System** (8-10h)
```cpp
// Files to create:
RenderGraph/include/Nodes/CameraPathNodeConfig.h
RenderGraph/src/Nodes/CameraPathNode.cpp
```

**Features**:
- Predefined camera trajectories (orbit, flythrough)
- Deterministic timing (lockstep with fixed timestep loop)
- Keyframe interpolation
- Recording/playback support

**M.4: Result Aggregator** (8-12h)
```cpp
// Files to create:
tests/Benchmarks/ResultAggregator.h
tests/Benchmarks/ResultAggregator.cpp
```

**Features**:
- Merge CSV files from 180 tests
- Generate summary statistics per configuration
- Output comparison matrices (pipeline A vs B at resolution X)
- Export to JSON for visualization tools

**M.5: Test Matrix Configuration** (4-6h)
```json
// tests/Benchmarks/test_matrix.json
{
  "pipelines": ["compute", "fragment", "hardware_rt", "hybrid"],
  "resolutions": [32, 64, 128, 256, 512],
  "densities": [0.2, 0.5, 0.8],
  "algorithms": ["baseline", "empty_skip", "blockwalk"],
  "scenes": ["sphere", "terrain", "architectural"],
  "frames": {
    "warmup": 60,
    "measurement": 300
  }
}
```

**Success Criteria**:
- ✅ Automated execution of all 180 configurations
- ✅ Headless rendering works (no user interaction)
- ✅ Results exported to structured format
- ✅ Total test time <8 hours (with parallelization)

---

### Phase N: Research Execution & Analysis 📈
**Duration**: 2-3 weeks
**Priority**: FINAL - Actual research work
**Dependencies**: Phase M complete

#### Objectives
1. Execute full test matrix
2. Validate metric accuracy
3. Analyze results
4. Generate visualizations

#### Tasks

**N.1: Pilot Testing** (4-6h)
- Run subset of test matrix (20 configurations)
- Validate metric accuracy against manual measurements
- Debug any anomalies

**N.2: Full Test Execution** (6-8h wall-clock time)
- Execute all 180 configurations
- Monitor for failures/crashes
- Collect ~54GB of CSV data (180 tests × 300 frames × 1KB/frame)

**N.3: Data Validation** (4-6h)
- Check for outliers (statistical anomalies)
- Verify bandwidth measurements (compare to Nsight)
- Ensure consistent scene rendering across pipelines

**N.4: Statistical Analysis** (8-12h)
- Performance scaling curves (resolution vs throughput)
- Bandwidth utilization efficiency
- Pipeline comparison matrices
- Optimization technique impact

**N.5: Visualization** (8-12h)
- Multi-line graphs (performance across resolutions)
- Heatmaps (bandwidth usage patterns)
- Scatter plots (throughput vs bandwidth)
- Bar charts (optimization impact)

**Success Criteria**:
- ✅ All 180 tests complete successfully
- ✅ Metrics align with research proposal
- ✅ Clear performance trends identified
- ✅ Results support or refute hypotheses

---

## Timeline Summary

| Phase | Duration | Cumulative | Dependencies | Priority |
|-------|----------|-----------|--------------|----------|
| F (Slot Tasks) | 16-21h | 3 weeks | None | HIGH |
| G (Compute Pipeline) | 2-3 weeks | 6 weeks | F | HIGHEST |
| H (Voxel Data) | 3-4 weeks | 10 weeks | G | HIGH |
| I (Profiling) | 2-3 weeks | 13 weeks | G | HIGH |
| J (Fragment Shader) | 1-2 weeks | 15 weeks | H | MEDIUM |
| K (Hardware RT) | 4-5 weeks | 20 weeks | H | HIGH |
| L (Optimizations) | 3-4 weeks | 24 weeks | G,J,K | HIGH |
| M (Automation) | 3-4 weeks | 28 weeks | G,J,K,L | CRITICAL |
| N (Research) | 2-3 weeks | 31 weeks | M | FINAL |

**Total Timeline**: 28-31 weeks (~7 months)
**With scope reduction**: 20-24 weeks (~5-6 months) - skip hybrid pipeline, reduce resolution levels

---

## Explicitly Deferred Features

These features are NOT required for research and will be postponed:

### ❌ Graph Editor
- Visual node editor UI
- Drag-and-drop connection editing
- Real-time graph visualization
**Rationale**: Research uses programmatic graph construction only.

### ❌ Material System
- PBR material editor
- Texture streaming
- Material variants
**Rationale**: Voxel rendering uses simple solid colors.

### ❌ Advanced UI
- ImGui integration (beyond basic profiler overlay)
- Scene inspection tools
- Real-time parameter tweaking
**Rationale**: Automated tests use fixed parameters.

### ❌ Asset Pipeline
- Model importers (GLTF, OBJ)
- Texture compression pipeline
- Asset hot-reload
**Rationale**: Procedural voxel generation only.

### ❌ Advanced Lighting
- Shadow mapping
- Ambient occlusion (except as optional secondary rays)
- Global illumination
**Rationale**: Research focuses on primary ray throughput.

---

## Phase Ordering Rationale

### Why Compute First (Phase G)?
1. **Simplest pipeline** - validates profiling before complexity
2. **No RT extensions** - reduces initial learning curve
3. **Immediate visual output** - confirms correctness early
4. **Foundation for hybrid** - reusable code

### Why Voxel Data Early (Phase H)?
1. **All pipelines need it** - critical path dependency
2. **Octree complexity** - time-consuming, parallelize with other work
3. **Testing foundation** - enables pipeline validation

### Why Hardware RT Late (Phase K)?
1. **Most complex** - BLAS/TLAS, SBT, custom intersection
2. **Depends on voxel data** - can't start until Phase H done
3. **Not on critical path** - compute/fragment can proceed in parallel

### Why Automation Last (Phase M)?
1. **All pipelines must work first** - can't automate broken code
2. **Requires stable APIs** - avoids rework
3. **Research depends on it** - but implementation doesn't

---

## Risk Mitigation

### Risk: Phase F Takes Longer Than Expected
**Mitigation**: Phase F is bounded (21h max). If exceeded, reassess scope.

### Risk: Hardware RT Extension Unavailable
**Mitigation**: Research can proceed with compute + fragment only (fallback plan).

### Risk: Bandwidth Metrics Inaccurate
**Mitigation**: Early validation in Phase I against Nsight Graphics.

### Risk: Test Execution Time Too Long
**Mitigation**: Reduce frame count (300 → 150) or parallelize on multiple GPUs.

### Risk: VRAM Exhaustion at 512³ Resolution
**Mitigation**: Octree compression (SVO), reduce max resolution to 256³.

---

## Success Metrics

### Technical Milestones
- ✅ Phase G complete → Compute ray marching renders voxel cube
- ✅ Phase H complete → 256³ octree loads in <100ms
- ✅ Phase I complete → Profiler collects 60fps metrics
- ✅ Phase K complete → Hardware RT renders same scene as compute
- ✅ Phase M complete → Automated test runs unattended

### Research Milestones
- ✅ 180 configurations tested successfully
- ✅ Bandwidth measurements validated against external tools
- ✅ Performance trends support or refute hypotheses
- ✅ Results publishable (conference paper quality)

---

## Documentation Updates

Upon phase completion, update:

**Memory Bank**:
- `memory-bank/activeContext.md` - Current phase status
- `memory-bank/progress.md` - Completed phases checklist

**Technical Docs**:
- `documentation/VoxelRayTracing/PhaseG-ComputePipeline.md`
- `documentation/VoxelRayTracing/PhaseH-VoxelData.md`
- (Continue for each phase)

**Research Docs**:
- `research/TestMatrix.md` - Configuration specification
- `research/ResultsAnalysis.md` - Findings and conclusions

---

---

## Phase N+1: Hybrid RTX Surface-Skin Pipeline (ADVANCED EXTENSION) 🚀

**Duration**: 5-7 weeks
**Priority**: OPTIONAL - Advanced research extension
**Dependencies**: Phases G, J, K complete (baseline comparisons established)

**Research Document**: `documentation/RayTracing/HybridRTX-SurfaceSkin-Architecture.md` (~110 pages)

### Concept

**Innovation**: Combine RTX hardware for fast initial surface intersection with ray marching for complex material handling.

**Key Technique**: Extract "surface skin buffer" containing only voxels at material boundaries or with empty neighbors → 5× data reduction.

### Implementation Tasks

**N+1.1: Surface Skin Extraction (CPU Pre-Process)** (8-12h)
```cpp
// Files to create:
VoxelProcessing/include/SurfaceSkinExtractor.h
VoxelProcessing/src/SurfaceSkinExtractor.cpp

// Key algorithm:
SurfaceSkinBuffer ExtractSurfaceSkin(const VoxelGrid& grid) {
    // For each voxel:
    // 1. Check if has empty neighbors → boundary
    // 2. Check if neighbors have different material IDs → transition
    // 3. Check if any neighbor is non-opaque → affects light
    // Output: Sparse buffer (typically 20% of full grid)
}
```

**Deliverables**:
- Surface voxel detection algorithm
- Normal calculation (gradient-based + dominant face fallback)
- Material classification system (opaque/reflective/refractive/volumetric)

**N+1.2: Virtual Geometry Generation** (12-16h)
```cpp
// Greedy meshing optimization
std::vector<VirtualQuad> GreedyMesh(const SurfaceSkinBuffer& skin) {
    // 1. Group voxels by normal direction + material
    // 2. Merge coplanar adjacent quads into rectangles
    // 3. Convert to triangles for RTX BLAS
    // Result: 10M triangles (vs 54M naïve) for urban scene
}
```

**Deliverables**:
- Quad generation from surface voxels
- Greedy meshing (rectangle merging)
- Triangle conversion for RTX BLAS

**N+1.3: RTX BLAS with Triangle Geometry** (6-8h)
```cpp
// Build acceleration structure with standard triangles (not AABBs)
VkAccelerationStructureKHR CreateSurfaceSkinBLAS(
    const std::vector<Triangle>& triangles
) {
    // Use VK_GEOMETRY_TYPE_TRIANGLES_KHR (faster than AABB)
    // Native hardware acceleration, no custom intersection shader
}
```

**Deliverables**:
- Triangle-based BLAS creation
- Material metadata buffer (per-triangle)
- TLAS with single instance

**N+1.4: Hybrid Ray Tracing Shaders** (16-20h)
```glsl
// Ray generation: RTX first hit → material continuation
void main() {
    // Step 1: Trace to surface skin (RTX hardware)
    traceRayEXT(surfaceSkinTLAS, ...);

    // Step 2: Material-specific handling
    if (mat.isOpaque) {
        finalColor = ShadeOpaque();
    } else if (mat.isRefractive) {
        finalColor = MarchRefractiveVolume();  // Glass
    } else if (mat.isVolumetric) {
        finalColor = MarchVolumetricMedia();   // Fog
    }
}
```

**Deliverables**:
- Hybrid ray generation shader (.rgen)
- Closest hit shader (.rchit) - records hit position + material
- Opaque material shading
- Refractive volume marching (glass, water)
- Volumetric media marching (fog, smoke with scattering)

**N+1.5: Integration & Benchmarking** (8-12h)
- Create HybridRTXNode (RenderGraph integration)
- Run test suite (45 configurations - Cornell, Cave, Urban × resolutions × densities)
- Compare with baseline pipelines (compute, fragment, pure RT)
- Analyze bandwidth, frame time, traversal efficiency

### Expected Results

**Performance Predictions**:
- **Cornell (10%, opaque)**: 3.2× faster than pure ray marching
- **Cave (50%, mixed materials)**: 2.3× faster (glass + fog)
- **Urban (90%, glass buildings)**: 2.5× faster (refractive volume marching)

**Research Value**:
- First known voxel renderer combining RTX + flexible material ray marching
- Demonstrates material complexity handling beyond opaque geometry
- Publication-worthy innovation

### Success Criteria
- [x] Surface skin extraction reduces data by 5× ✓ (design complete)
- [x] Greedy meshing reduces triangles by 5-10× ✓ (algorithm specified)
- [ ] RTX first hit faster than DDA (measure)
- [ ] Refractive materials render correctly (visual validation)
- [ ] Volumetric media scattering works (fog visible)
- [ ] Hybrid outperforms pure ray marching by 2-3× (benchmark)

---

## Phase N+2: GigaVoxels Sparse Streaming (SCALABILITY EXTENSION) 🌌

**Duration**: 4-6 weeks
**Priority**: OPTIONAL - Scalability showcase
**Dependencies**: Phases H (Octree), I (Profiling) complete

**Research Document**: `documentation/VoxelStructures/GigaVoxels-CachingStrategy.md` (~90 pages)

### Concept

**Innovation**: GPU-managed caching with ray-guided on-demand streaming enables multi-gigavoxel datasets.

**Key Technique**: Brick pool (3D texture atlas) acts as LRU cache, streams bricks from CPU/disk based on visibility → **128× memory reduction** (256 GB → 2 GB cache).

### Implementation Tasks

**N+2.1: Brick Pool Architecture** (8-10h)
```cpp
// Files to create:
VoxelStreaming/include/BrickPool.h
VoxelStreaming/src/BrickPool.cpp

class BrickPool {
    VkImage brickAtlas_;  // 3D texture (e.g., 2048³)
    std::unordered_map<uint32_t, BrickSlot> slots_;

    uint32_t AllocateBrickSlot();  // LRU eviction
    void UploadBrick(VkCommandBuffer cmd, const BrickData& brick, uint32_t slot);
};
```

**Deliverables**:
- 3D texture atlas creation (brick pool)
- LRU cache management (eviction policy)
- Async brick upload (staging buffer → image)

**N+2.2: Request Buffer System** (6-8h)
```glsl
// GPU shader: Request missing bricks
layout(set = 0, binding = 3) buffer RequestBuffer {
    uint counter;
    uint nodeIndices[4096];
};

void RequestBrickLoad(uint nodeIndex) {
    uint slot = atomicAdd(requestBuffer.counter, 1);
    if (slot < 4096) {
        requestBuffer.nodeIndices[slot] = nodeIndex;
    }
}
```

**Deliverables**:
- GPU request buffer (atomic append)
- CPU readback (map buffer, extract requests)
- Priority sorting (distance to camera, frame timestamp)

**N+2.3: Streaming Manager (CPU)** (12-16h)
```cpp
class BrickStreamingManager {
    void ProcessRequests(VkCommandBuffer cmd) {
        // 1. Read request buffer from GPU
        auto requests = ReadRequestBuffer();

        // 2. Sort by priority (camera distance, LOD)
        std::sort(requests.begin(), requests.end(), ComparePriority);

        // 3. Load top N bricks (budget: 100 MB/frame @ 60 FPS)
        for (uint32_t nodeIndex : requests) {
            BrickData brick = LoadBrick(nodeIndex);  // From CPU cache or disk
            uint32_t slot = brickPool_.AllocateBrickSlot();
            brickPool_.UploadBrick(cmd, brick, slot);
        }
    }
};
```

**Deliverables**:
- Streaming manager (request processing)
- CPU cache layer (paged memory, disk loading)
- Bandwidth budget management (100 MB/frame limit)

**N+2.4: Multi-Resolution Mipmapping** (8-12h)
```cpp
struct OctreeNode {
    uint32_t brickPointers[4];  // LOD 0, 1, 2, 3
};

// Shader: Graceful degradation
vec4 SampleWithFallback(OctreeNode node, vec3 pos) {
    if (node.brickPointers[0] != INVALID) return SampleBrick(node.brickPointers[0], pos);
    if (node.brickPointers[1] != INVALID) return SampleBrick(node.brickPointers[1], pos);
    // ... fallback to lower LODs
    return PLACEHOLDER_COLOR;
}
```

**Deliverables**:
- Mipmap generation (box filter, trilinear)
- Multi-LOD storage in octree nodes
- Automatic LOD selection in shader

**N+2.5: Integration & Scalability Testing** (8-12h)
- Create GigaVoxelsNode (RenderGraph integration)
- Test with massive grids (1024³, 2048³, 4096³)
- Measure cache hit rate, streaming bandwidth
- Compare with baseline (dense texture)
- Visual quality validation (no visible "pop-in")

### Expected Results

**Scalability Metrics**:
- **512³ voxel grid**: 8× memory reduction (512 MB → 64 MB cache)
- **1024³ voxel grid**: 16× reduction (4 GB → 256 MB cache)
- **2048³ voxel grid**: 32× reduction (32 GB → 1 GB cache)
- **4096³ voxel grid**: 128× reduction (256 GB → 2 GB cache)

**Performance**:
- **Cold start**: +20-30 ms (initial streaming)
- **Warm cache**: +1-2 ms overhead (request processing)
- **Steady state**: Cache hit rate > 95% (≈ baseline performance)

**Research Value**:
- Demonstrates scalability to multi-gigavoxel datasets
- Real-world applicability (streaming essential for production)
- Academic contribution (bandwidth optimization analysis)

### Success Criteria
- [x] Brick pool architecture designed ✓ (90 pages)
- [x] Ray-guided streaming algorithm specified ✓
- [ ] 4096³ grid renders in 2 GB VRAM (measure)
- [ ] Cache hit rate > 95% after warm-up (profile)
- [ ] Streaming overhead < 2 ms/frame (benchmark)
- [ ] Visual quality comparable to fully loaded (qualitative)

---

## Extended Research Test Matrix

**Original**: 180 configurations (4 pipelines × 5 resolutions × 3 densities × 3 algorithms)

**Extended** (if N+1/N+2 implemented): **270 configurations**

| Pipeline | Phases | Priority |
|----------|--------|----------|
| Compute shader ray marching | G | ✅ Core |
| Fragment shader ray marching | J | ✅ Core |
| Hardware RT (AABB voxels) | K | ✅ Core |
| Hybrid (compute + fragment) | L | ✅ Core |
| **Hybrid RTX + surface-skin** | **N+1** | 🚀 **Advanced** |
| **GigaVoxels streaming** | **N+2** | 🌌 **Scalability** |

**Additional Research Questions**:
1. Does surface-skin extraction overhead offset RTX speedup?
2. How does greedy meshing affect BVH traversal quality?
3. What's optimal cache size for GigaVoxels (512 MB? 1 GB? 2 GB)?
4. Do complex materials (glass/fog) benefit more from hybrid approach?
5. Can GigaVoxels maintain 60 FPS with aggressive streaming (200 MB/frame)?

---

## Revised Timeline (With Extensions)

**Core Research** (Phases G-N): 28-31 weeks → May 2026 paper submission

**Extended Research** (Phases N+1, N+2): +9-13 weeks → August 2026

**Total**: 37-44 weeks from Phase F completion

**Recommendation**:
- Complete core research first (G-N)
- Submit initial paper (4 pipelines)
- Implement N+1/N+2 for extended journal publication (6 pipelines)

---

## Next Immediate Steps

1. ✅ **Complete Phase F** (slot task system) - DO NOT START RESEARCH YET
2. ⏳ **Read Phase G detailed spec** - Review compute pipeline requirements
3. ⏳ **Implement G.1** - ComputePipelineNode + cacher
4. ⏳ **Implement G.2** - Storage resource types
5. ⏳ **Implement G.3** - Basic ray marching shader
6. ⏳ **Implement G.4** - Timestamp queries
7. ⏳ **Validate Phase G** - Render test cube, measure timing

---

## Open Questions

1. **Octree Depth**: 8 levels (256³) or 9 levels (512³)?
   - **Recommendation**: Start with 8, add 9 if VRAM allows

2. **BLAS Strategy**: One AABB per voxel or hierarchical?
   - **Recommendation**: Hierarchical (octree nodes as AABBs)

3. **Hybrid Pipeline Priority**: Essential or optional?
   - **Recommendation**: Optional (implement if time permits)

4. **Test Parallelization**: Single GPU or multi-GPU?
   - **Recommendation**: Single GPU (simpler, more reproducible)

---

## Conclusion

This roadmap provides a clear, incremental path from current VIXEN state to full research capability. By focusing exclusively on research requirements and deferring non-essential features, the timeline is aggressive but achievable.

**Key Insight**: Start simple (compute shader), validate methodology (profiling), then add complexity (hardware RT, optimizations). Automate only when pipelines are stable.

**Estimated Completion**: 7 months from Phase F completion (or 5-6 months with scope reduction).

Ready to begin Phase F? Confirm completion before starting Phase G.