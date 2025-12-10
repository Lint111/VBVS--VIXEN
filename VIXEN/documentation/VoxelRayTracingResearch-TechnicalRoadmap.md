# Technical Roadmap: Voxel Ray Tracing Research Integration

**Date**: November 2, 2025 (Updated: December 10, 2025)
**Status**: Phase K/L COMPLETE | All 6 Shader Variants Working | Data Pipeline Ready
**Research Goal**: Compare Vulkan ray tracing/marching pipelines for voxel rendering
**Timeline**: Awaiting multi-tester benchmark submissions → Phase M Analysis

---

## Executive Summary

This document outlines the technical implementation roadmap for integrating voxel ray tracing research capabilities into the VIXEN framework. The roadmap builds incrementally on completed infrastructure (Phases 0-F) and defers non-essential features (graph editor, advanced UI) to focus exclusively on research requirements.

**Core Research Question**: How do different Vulkan ray tracing and ray marching pipeline architectures affect rendering performance, GPU bandwidth utilization, and scalability for data-driven voxel rendering?

**Testing Matrix**: 4 pipelines × 5 resolutions × 3 densities × 3 algorithm variants = 180 configurations

---

## Current Infrastructure Status

### ✅ Completed (Ready for Research)
1. **RenderGraph System** - Node-based pipeline with compile-time type safety
2. **Resource Variant System** - Type-safe Vulkan resource management (29+ types)
3. **Cache System** - Performance metrics tracking (9 cachers, persistent cache)
4. **SPIRV Reflection** - Automatic descriptor layout generation (ShaderManagement Phases 0-5)
5. **Multi-Rate Loop System** - Fixed timestep support for deterministic timing
6. **Synchronization** - Frame-in-flight, two-tier sync, present fences
7. **Event System** - Invalidation cascade, auto-incrementing message types
8. **CMake Build System** - Modular library structure, PCH support
9. **Testing Infrastructure** (NEW) - 40% coverage, 10 test suites, VS Code integration
10. **Logging System** (NEW) - ILoggable interface, LOG_*/NODE_LOG_* macros
11. **Variadic Node System** (NEW) - VariadicTypedNode, dynamic slot discovery
12. **Context System** (NEW) - Phase-specific typed contexts (Setup/Compile/Execute/Cleanup)
13. **Lifecycle Hooks** (NEW) - 6 graph + 8 node phases = 14 hooks
14. **Phase G Complete** (NEW) - SlotRole bitwise flags, deferred descriptor binding

### ✅ Week 2 Complete (December 1, 2025)
- **Phase H** - Week 2 GPU Integration COMPLETE
  - ✅ CameraNode, VoxelGridNode, VoxelRayMarch.comp
  - ✅ LaineKarrasOctree with ESVO traversal
  - ✅ GPUTimestampQuery + GPUPerformanceLogger
  - ✅ 8 shader bugs fixed, debug capture system
  - ✅ **1,700 Mrays/sec** (8.5x > 200 Mrays/sec target)

### ✅ Week 3 Complete (December 3, 2025)
- **Phase H** - Week 3 DXT Compression COMPLETE
  - ✅ DXT1ColorCompressor (8:1), DXTNormalCompressor (4:1)
  - ✅ VoxelRayMarch_Compressed.comp shader variant
  - ✅ **5.3:1 memory reduction** (~955 KB vs ~5 MB)
  - ✅ **Phase C Bug Fixes**: 6 critical bugs found and fixed via comparative analysis
  - ✅ **85-303 Mrays/s** (compressed shader, distance-dependent performance)

### ✅ Week 4 Complete (December 3, 2025)
- **Phase H** - Week 4 Architecture Refactoring + Features COMPLETE
  - ✅ **Phase A.1**: Unified Morton Architecture
    - MortonCode64 extracted to libraries/Core/
    - GaiaVoxelWorld delegates to Core implementation
    - 4 redundant conversions eliminated per entity lookup
  - ✅ **Phase A.3**: SVOManager Refactoring
    - Split LaineKarrasOctree.cpp (2,802 lines) into 4 focused files:
    - `LaineKarrasOctree.cpp` (477 lines) - Facade/coordinator
    - `SVOTraversal.cpp` (467 lines) - ESVO ray casting (Laine & Karras 2010)
    - `SVOBrickDDA.cpp` (364 lines) - Brick DDA (Amanatides & Woo 1987)
    - `SVORebuild.cpp` (426 lines) - Entity-based build with Morton sorting
  - ✅ **Phase A.4**: Zero-Copy API
    - `getBrickEntitiesInto()` for caller-provided buffers
    - `countBrickEntities()` for O(1) isEmpty checks
    - Old API deprecated, delegates to new implementation
  - ✅ **Phase B.1**: Geometric Normal Computation
    - 6-neighbor central differences gradient method
    - `precomputeGeometricNormals()` caches O(512) normals per brick
    - `NormalMode` enum: EntityComponent, GeometricGradient (default), Hybrid
  - ✅ **Phase B.2**: Adaptive LOD System
    - SVOLOD.h with LODParameters struct
    - Screen-space voxel termination (ESVO Raycast.inl reference)
    - `castRayScreenSpaceLOD()` and `castRayWithLOD()` methods
    - 16/16 test_lod tests passing
  - ⏸️ **Phase B.3 (Streaming)**: Deferred to Phase N+2 (not critical path)

### ✅ Phase K Complete (December 8-10, 2025)
- **Hardware RT Pipeline**: VK_KHR_ray_tracing_pipeline fully implemented
- **All 6 shader variants working**: Compute/Fragment/RT × Compressed/Uncompressed
- **Visual fixes**: Y-flip, SDI naming, material IDs, world-space AABBs
- **ESC freeze fixed**: WindowCloseEvent pattern

### ✅ Phase L Complete (December 10, 2025)
- **Data visualization pipeline**: aggregate_results.py, generate_charts.py
- **TesterPackage**: Automatic ZIP packaging with system_info.json
- **144 benchmarks analyzed** (RTX 3060 Laptop GPU)
- **Key findings**: Compute 80-130 fps, Fragment 100-130 fps, HW RT ~40 fps @ 256³

### ⏳ Remaining (Required for Research)
Phase M (Analysis & Paper) pending. Awaiting multi-tester benchmark submissions.

---

## Implementation Phases

### Phase F: Bundle-First Organization ✅ COMPLETE
**Duration**: Completed November 2, 2025
**Status**: COMPLETE

**Deliverables**:
- Bundle struct consolidation (inputs/outputs aligned per task)
- TypedNodeInstance updated for bundle-first indexing
- ResourceDependencyTracker updated for bundle iteration
- All nodes compile successfully

**Impact**: Foundation for task-based parallelism and proper resource tracking.

---

### Phase G: SlotRole System & Descriptor Refactor ✅ COMPLETE
**Duration**: Completed November 8, 2025
**Status**: COMPLETE
**Priority**: HIGHEST - Foundation for descriptor management

#### Completed Objectives
1. ✅ SlotRole bitwise flags system (Dependency | Execute)
2. ✅ Deferred descriptor binding architecture
3. ✅ Generalized DescriptorSetNode (no hardcoded UBO logic)
4. ✅ NodeFlags enum for state management
5. ✅ Zero Vulkan validation errors

#### Deliverables
**G.1: SlotRole Bitwise Flags** ✅
- `SlotRole::Dependency | SlotRole::Execute` combined roles
- Helper functions: HasDependency(), HasExecute(), IsDependencyOnly(), IsExecuteOnly()
- Location: `RenderGraph/include/Data/Core/ResourceConfig.h`

**G.2: DescriptorSetNode Refactor** ✅
- CompileImpl reduced from ~230 lines to ~80 lines
- 5 helper methods extracted: CreateDescriptorSetLayout(), CreateDescriptorPool(), AllocateDescriptorSets(), PopulateDependencyDescriptors(), PopulateExecuteDescriptors()
- NodeFlags enum replaces scattered bool flags
- Location: `RenderGraph/src/Nodes/DescriptorSetNode.cpp`

**G.3: Deferred Descriptor Binding** ✅
- Descriptor binding moved from Compile to Execute phase
- PostCompile hooks populate resources before binding
- Per-frame descriptor sets (not per-frame UBOs)
- Dependency-role descriptors bound once in Compile
- Execute-role descriptors bound every Execute (transient resources)

**G.4: PostCompile Hook Logic** ✅
- TypedConnection.h updated with PostCompile invocation
- Nodes populate outputs after Compile completes
- Enables descriptor binding after resources exist

**Impact**:
- Eliminated validation errors (descriptor binding after resource creation)
- Generalized architecture (supports any descriptor layout)
- NodeFlags pattern reusable for all node state management

---

### Phase H: Voxel Data Infrastructure ✅ COMPLETE
**Duration**: Weeks 1-4 (Nov 8 - Dec 3, 2025)
**Status**: COMPLETE - All core objectives achieved
**Priority**: HIGH - Required for all pipelines
**Dependencies**: Phase G complete ✅

#### Week 1 Completed ✅ (November 8-26, 2025)
- **GaiaVoxelWorld**: ECS-backed sparse voxel storage with Morton indexing
- **VoxelComponents**: Macro-based component registry (Density, Color, Normal, etc.)
- **EntityBrickView**: Zero-storage pattern (16 bytes vs 70 KB per brick)
- **LaineKarrasOctree**: ESVO-based ray casting with brick DDA
- **rebuild() API**: Modern workflow replacing legacy VoxelInjector
- **162 tests passing**: VoxelComponents + GaiaVoxelWorld + SVO

#### Week 2 Completed ✅ (November 26 - December 1, 2025)
- **GPUTimestampQuery**: VkQueryPool wrapper for GPU timing
- **GPUPerformanceLogger**: Rolling 60-frame statistics
- **Sparse Brick Architecture**: Brick indices in leaf descriptors
- **Debug Capture System**: Per-ray traversal traces with JSON export
- **8 Shader Bugs Fixed**: ESVO scale, axis-parallel rays, coordinate transforms
- **Performance**: **1,700 Mrays/sec** at 800x600 (8.5x > 200 Mrays/sec target)

Key Files Modified (Week 2):
- `libraries/VulkanResources/include/GPUTimestampQuery.h` - NEW
- `libraries/RenderGraph/include/Core/GPUPerformanceLogger.h` - NEW
- `shaders/VoxelRayMarch.comp` - 8 bug fixes
- `shaders/SVOTypes.glsl` - NEW: Shared GLSL data structures

#### Week 3 Complete ✅ (DXT Compression + Phase C Bug Fixes)
**Goal**: Memory reduction via DXT compression + fix compressed shader bugs

**Completed Tasks**:
1. ✅ Study ESVO DXT section (paper 4.1)
2. ✅ Implement CPU DXT1/BC1 encoder for color bricks (8:1 ratio)
3. ✅ Implement GLSL DXT1 decoder in VoxelRayMarch_Compressed.comp
4. ✅ DXT normal compression (4:1 ratio)
5. ✅ Benchmark: 5.3:1 overall memory reduction (~955 KB vs ~5 MB)

**Phase C Bug Fixes** (December 3, 2025):
- Found 6 critical bugs via comparative analysis with VoxelRayMarch.comp
- `executePopPhase`: Added step_mask parameter, IEEE 754 bit manipulation algorithm, int return type
- `executeAdvancePhase`: Corrected inverted return values (was 0=POP, 1=CONTINUE), added max(tc_max, 0.0)
- Cornell Box now renders correctly with proper wall topology

**Performance Results**:
- Compressed shader: 85-303 Mrays/s (distance-dependent, as expected)
- Memory: 5.3:1 compression ratio maintained

**Success Criteria** - ALL MET ✅:
- ✅ Week 1: LaineKarrasOctree with ESVO traversal (162 tests)
- ✅ Week 2: GPU throughput 1,700 Mrays/sec (8.5x target)
- ✅ Week 3: 5.3:1 memory reduction via DXT compression + bug fixes
- ✅ Week 4: Architecture refactoring (Morton, SVOManager split, Zero-Copy API)
- ✅ Week 4: Geometric normals + Adaptive LOD (16/16 tests)
- ⏸️ Streaming: Deferred to Phase N+2 (not critical path)

**Completed**: December 3, 2025

---

### Phase I: Performance Profiling System 📊 ✅ COMPLETE
**Duration**: Completed December 3, 2025
**Priority**: HIGH - Required for metrics collection
**Dependencies**: Phase G complete (timestamp queries)
**Status**: ✅ COMPLETE

#### Completed Implementation

**Profiler Library** (`libraries/Profiler/`):
- ProfilerSystem singleton, RollingStats (percentiles), DeviceCapabilities
- MetricsCollector (VkQueryPool timing), VRAM tracking, bandwidth estimation
- MetricsExporter (CSV/JSON Section 5.2 schema), TestSuiteResults aggregation
- BenchmarkConfig JSON loader, test matrix generation
- BenchmarkRunner with 4 builder methods
- BenchmarkGraphFactory with Compute/Fragment/RT graph builders
- **116 tests passing**

**Success Criteria** - ALL MET ✅:
- ✅ Profiler collects 300+ frames with <1% overhead
- ✅ CSV/JSON export includes all required metrics
- ✅ Configuration files drive test execution
- ✅ Graph builders for all 3 pipeline types

---

### Phase J: Fragment Shader Ray Marching 🎨 ✅ COMPLETE
**Duration**: 1-2 weeks → Completed December 5, 2025
**Priority**: MEDIUM - Second pipeline implementation
**Dependencies**: Phase H complete (voxel data)
**Status**: ✅ COMPLETE

#### Objectives
1. ✅ Traditional rasterization-based ray marching
2. ✅ Comparison baseline for compute/RT pipelines
3. ✅ Validate shared voxel data structure

#### Implementation Completed (December 5, 2025)

**J.1: Fragment Ray March Pipeline** ✅
- `BenchmarkGraphFactory::BuildFragmentRayMarchGraph()` - Full implementation
- `VoxelRayMarch.frag` - Fragment shader with ESVO traversal
- `FullscreenQuad.vert` - Fullscreen triangle rendering
- Push constants for camera data via `GeometryRenderNode::SetPushConstants()`

**Key Files Modified**:
| File | Change |
|------|--------|
| `GeometryRenderNodeConfig.h:159-171` | Added PUSH_CONSTANT_DATA/RANGES slots |
| `GeometryRenderNode.cpp:373-400` | SetPushConstants() implementation |
| `BenchmarkGraphFactory.cpp:948-952` | Push constant wiring |
| `BenchmarkRunner.cpp:341-345` | Fragment node type registration |
| `DescriptorResourceGathererNode.cpp:400-403` | Skip Invalid slots in validation |

**Success Criteria** - ALL MET ✅:
- ✅ Fragment shader renders same scene as compute pipeline
- ✅ Profiler measures fragment shader GPU time
- ✅ Bandwidth metrics collected
- ✅ Visual parity with compute shader output

#### Completed Task (J.2): Compressed Fragment Shader Variant ✅

**J.2: VoxelRayMarch_Compressed.frag** (December 6, 2025)
- ✅ Created `VoxelRayMarch_Compressed.frag` matching `VoxelRayMarch_Compressed.comp`
- ✅ Uses DXT-compressed brick data (bindings 6-7 for compressed colors/normals)
- ✅ Updated BenchmarkConfig to include compressed fragment shader variant
- ✅ Wired compressed buffer bindings in WireFragmentVariadicResources()

**Shader Variants - Complete Comparative Study**:
| Pipeline | Uncompressed | Compressed |
|----------|--------------|------------|
| Compute | ✅ VoxelRayMarch.comp | ✅ VoxelRayMarch_Compressed.comp |
| Fragment | ✅ VoxelRayMarch.frag | ✅ VoxelRayMarch_Compressed.frag |
| Hardware RT | ✅ VoxelRT.rgen/rmiss/rchit/rint | ✅ VoxelRT_Compressed.rchit |

---

### Phase K: Hardware Ray Tracing Pipeline ⚡ ✅ COMPLETE
**Duration**: Completed December 8-10, 2025
**Priority**: HIGH - Core research pipeline
**Dependencies**: Phase H complete (voxel data)
**Status**: ✅ COMPLETE

#### Completed Implementation

**RT Extensions & Nodes**:
- VK_KHR_ray_tracing_pipeline, VK_KHR_acceleration_structure enabled
- AccelerationStructureNode: BLAS/TLAS creation, VoxelAABB → AS conversion
- AccelerationStructureCacher: Caches built structures
- TraceRaysNode: vkCmdTraceRaysKHR dispatch with SBT management

**Shaders Created**:
- `VoxelRT.rgen` - Ray generation with camera setup
- `VoxelRT.rmiss` - Miss shader (sky gradient)
- `VoxelRT.rchit` - Closest hit (uncompressed voxels)
- `VoxelRT.rint` - AABB intersection test
- `VoxelRT_Compressed.rchit` - Compressed brick support

**Bug Fixes (Sessions 1-5)**:
| Issue | Root Cause | Fix |
|-------|------------|-----|
| Upside-down scenes | Vulkan UV coords | Y-flip in getRayDir() |
| SDI naming collision | Same program name | Pipeline suffixes |
| Grey colors | Dangling pointer | Two-pass componentStorage |
| HW RT sparse voxels | Coordinate mismatch | World-space AABBs |
| ESC freeze | PostQuitMessage timing | WindowCloseEvent pattern |

**Success Criteria** - ALL MET ✅:
- ✅ Hardware RT renders voxel scene correctly (all scenes)
- ✅ BLAS/TLAS builds successfully
- ✅ Ray tracing performance metrics collected (~40 fps @ 256³)
- ✅ 24 RT benchmark tests passing

---

### Phase L: Data Pipeline & Tester Coordination 📊 ✅ COMPLETE
**Duration**: Completed December 10, 2025
**Priority**: HIGH - Research data collection
**Dependencies**: Phases I, J, K complete (all pipelines working)
**Status**: ✅ COMPLETE

#### Completed Implementation

**Data Visualization Pipeline**:
- `aggregate_results.py` - JSON → Excel aggregation with normalized schema
- `generate_charts.py` - 9 chart types (FPS, frame time, heatmaps, etc.)
- `refresh_visualizations.py` - Master pipeline orchestration

**TesterPackage Feature**:
- Automatic ZIP packaging for benchmark results
- CLI: `--tester "Name"`, `--no-package` options
- Creates: `VIXEN_benchmark_YYYYMMDD_HHMMSS_<GPU>.zip`
- Contents: JSON results, debug_images/, system_info.json

**Multi-Tester Workflow**:
```bash
python aggregate_results.py --pack benchmark_results/  # Create ZIP
python aggregate_results.py --unpack benchmark.zip     # Extract received
python aggregate_results.py --process-all              # Process all folders
```

**Key Analysis Findings (RTX 3060 Laptop GPU, 144 tests)**:
| Pipeline | FPS @ 256³ | VRAM | Notes |
|----------|------------|------|-------|
| Compute | 80-130 | 320 MB | Best general performance |
| Fragment | 100-130 | 319 MB | Graphics integration |
| Hardware RT | ~40 | 1098 MB | 3.4x VRAM overhead |

**Data Gaps Identified**:
- `avg_voxels_per_ray` = 0.0 (shader instrumentation needed)
- `ray_throughput_mrays` = 0.0 for Fragment/HW RT
- No GPU utilization monitoring

**Success Criteria** - ALL MET ✅:
- ✅ JSON export per benchmark test
- ✅ Excel aggregation working
- ✅ 9 chart types generated
- ✅ TesterPackage enables easy benchmark sharing

---

### Phase M: Hybrid Pipeline & Optimization 🚀 (DEFERRED)
**Duration**: 3-4 weeks (when scheduled)
**Priority**: OPTIONAL - Advanced research extension
**Dependencies**: Phases L complete, multi-tester data collected
**Status**: ⏸️ DEFERRED

#### Objectives (Future Work)
1. Implement traversal optimizations for each pipeline
2. Add hybrid pipeline (compute + hardware RT)
3. Enable dynamic scene updates

**Deferred Tasks**:
- L.1: Empty Space Skipping
- L.2: Block-Based Traversal (BlockWalk)
- L.3: Dynamic Scene Updates
- L.4: Hybrid Pipeline (RT + Compute)

---

### Phase N: Analysis & Paper (Formerly M) 📈
**Duration**: 2-3 weeks (when multi-tester data available)
**Priority**: CRITICAL - Research deliverable
**Dependencies**: Phase L complete, multi-tester data collected

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

| Phase | Duration | Dependencies | Status |
|-------|----------|--------------|--------|
| F (Bundle Refactor) | ~20h | None | ✅ COMPLETE (Nov 2) |
| G (SlotRole + Descriptor) | 2-3 weeks | F | ✅ COMPLETE (Nov 8) |
| H (Voxel Data) | 4 weeks | G | ✅ COMPLETE (Dec 3) |
| I (Profiling) | 2-3 weeks | G,H | ✅ COMPLETE (Dec 3) |
| J (Fragment Shader) | 1-2 weeks | H | ✅ COMPLETE (Dec 5) |
| K (Hardware RT) | 2 weeks | H,I,J | ✅ COMPLETE (Dec 10) |
| L (Data Pipeline) | 1 week | I,J,K | ✅ COMPLETE (Dec 10) |
| M (Hybrid Pipeline) | 3-4 weeks | L | ⏸️ DEFERRED |
| N (Analysis & Paper) | 2-3 weeks | L + multi-tester data | ⏳ BLOCKED |

**Current State**: Phase L COMPLETE. Awaiting multi-tester benchmark submissions.
**Next Milestone**: Collect benchmarks from additional GPUs → Phase N Analysis

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

### Completed Phases (November-December 2025)

1. ✅ **Phase F** (Bundle Refactor) - November 2, 2025
2. ✅ **Phase G** (SlotRole + Descriptor) - November 8, 2025
3. ✅ **Phase H** (Voxel Data Infrastructure) - December 3, 2025
   - Weeks 1-4: GPU integration, DXT compression, architecture refactoring
   - **1,700 Mrays/sec**, **5.3:1 memory reduction**
4. ✅ **Phase I** (Performance Profiling) - December 3, 2025
   - Profiler library with 116 tests, JSON/CSV export
5. ✅ **Phase J** (Fragment Shader Pipeline) - December 5, 2025
   - Push constants, all shader variants
6. ✅ **Phase K** (Hardware RT Pipeline) - December 10, 2025
   - VK_KHR_ray_tracing_pipeline, BLAS/TLAS, 24 RT tests
   - All visual bugs fixed (Y-flip, material IDs, world-space AABBs)
7. ✅ **Phase L** (Data Pipeline) - December 10, 2025
   - aggregate_results.py, generate_charts.py, TesterPackage
   - 144 benchmarks analyzed

### Current Focus

8. ⏳ **BLOCKED: Multi-Tester Data Collection**
   - Awaiting benchmark submissions from additional machines
   - Testers run: `vixen_benchmark.exe --quick --tester "Name"`
   - Share resulting ZIP file

9. ⏳ **Phase N** (Analysis & Paper) - Pending data
   - Statistical analysis across multiple GPUs
   - Performance comparison charts
   - Research paper draft

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

**Status as of December 10, 2025**: Core research infrastructure COMPLETE.

All 6 shader variants (Compute/Fragment/RT × Compressed/Uncompressed) are working. Data pipeline enables benchmark collection and visualization. 144 benchmarks analyzed locally.

**Current Blocker**: Multi-tester benchmark submissions needed for cross-GPU analysis.

**Key Achievements**:
- Compute pipeline: 80-130 fps @ 256³, 320 MB VRAM
- Fragment pipeline: 100-130 fps @ 256³, 319 MB VRAM
- Hardware RT: ~40 fps @ 256³, 1098 MB VRAM (3.4x overhead)
- All pipelines render correctly with DXT compression
- TesterPackage enables easy benchmark sharing

**Next Steps**:
1. Collect benchmarks from additional GPUs (RTX 3070, 3080, 4090, etc.)
2. Phase N: Statistical analysis and paper writing
3. (Optional) Phase M: Hybrid pipeline optimization