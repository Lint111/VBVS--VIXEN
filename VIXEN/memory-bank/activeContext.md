# Active Context

**Last Updated**: December 3, 2025
**Current Branch**: `claude/phase-h-voxel-infrastructure`
**Status**: Phase H COMPLETE - Ready for Phase I

---

Plan saved to: ~\.claude\plans\valiant-prancing-toucan.md · /plan to edit
     Phase I - Performance Profiling System Plan

     Branch: claude/phase-i-performance-profiling
     Duration: 2-3 weeks (34-48 hours)
     Goal: Automated metrics collection for 180-configuration research benchmark

     ---
     Architecture Overview

     Key Insight: Use existing GraphLifecycleHooks (PreExecute/PostExecute) for timing, external ProfilerSystem (like EventBus pattern) for data
     aggregation.

     ┌─────────────────────────────────────────────────────────────┐
     │                     ProfilerSystem                          │
     │  (External, like EventBus - hookable and extensible)        │
     ├─────────────────────────────────────────────────────────────┤
     │  DeviceCapabilities     │  Registered once per test suite   │
     │  TestConfiguration      │  Scene, resolution, algorithm     │
     │  FrameMetricsCollector  │  Hooked via GraphLifecycleHooks   │
     │  NodeMetricsExtractor   │  Custom loggers before cleanup    │
     │  ResultsExporter        │  CSV/JSON output                  │
     └─────────────────────────────────────────────────────────────┘
              │                          │
              ▼                          ▼
     ┌─────────────────┐      ┌─────────────────────┐
     │ GraphLifecycle  │      │ GPUPerformanceLogger│
     │ Hooks           │      │ (existing)          │
     │ - PreExecute    │      │ - dispatch timing   │
     │ - PostExecute   │      │ - memory tracking   │
     └─────────────────┘      └─────────────────────┘

     ---
     Implementation Order

     I.1: Core Infrastructure (10-14h)

     I.1a: RollingStats (2-3h)
     - Reusable statistics class with percentiles
     - File: RenderGraph/include/Core/RollingStats.h

     I.1b: ProfilerSystem (8-10h)
     - External system (singleton like EventBus)
     - Register/unregister hooks pattern
     - Owns: DeviceCapabilities, TestConfig, FrameCollector, Exporters
     - Files:
       - RenderGraph/include/Core/ProfilerSystem.h
       - RenderGraph/src/Core/ProfilerSystem.cpp

     I.2: Data Collection (8-12h)

     I.2a: DeviceCapabilities (2-3h)
     - Capture once per test suite: GPU name, driver, Vulkan version, limits
     - Query VkPhysicalDeviceProperties, VkPhysicalDeviceMemoryProperties
     - File: RenderGraph/include/Core/DeviceCapabilities.h

     I.2b: FrameMetricsCollector (4-6h)
     - Hook into GraphLifecycleHooks (PreExecute/PostExecute)
     - Collect: frame time, GPU time, MRays/s
     - Delegate to GPUPerformanceLogger for GPU timing
     - File: RenderGraph/include/Core/FrameMetricsCollector.h

     I.2c: NodeMetricsExtractor (2-3h)
     - Extract scene size, resolution from VoxelGridNode before cleanup
     - Extract screen resolution from SwapChainNode
     - Pattern: Register extractors, call before graph cleanup
     - File: RenderGraph/include/Core/NodeMetricsExtractor.h

     I.3: Export System (6-8h)

     I.3a: MetricsExporter (4-5h)
     - CSV export with metadata header
     - JSON export for programmatic access
     - File: RenderGraph/include/Core/MetricsExporter.h

     I.3b: TestSuiteResults (2-3h)
     - Aggregate multiple test runs
     - Device info + array of test results
     - File: RenderGraph/include/Core/TestSuiteResults.h

     I.4: Configuration (6-8h)

     I.4a: BenchmarkConfig (4-5h)
     - nlohmann/json for parsing
     - Test matrix generation
     - Files:
       - tests/Benchmarks/BenchmarkConfig.h
       - tests/Benchmarks/BenchmarkConfig.cpp

     I.4b: Add nlohmann/json dependency (2-3h)
     - Add to CMake via FetchContent or vcpkg
     - File: CMakeLists.txt modification

     ---
     Key Design Decisions

     1. External System Pattern: ProfilerSystem follows EventBus pattern (hookable, extensible)
     2. Lifecycle Hook Integration: Use existing PreExecute/PostExecute hooks for frame timing
     3. Two-Phase Data Collection:
       - Frame metrics via hooks (real-time)
       - Node metrics via extractors (before cleanup)
     4. Device Registration Once: Capture GPU info at test suite start, include in all results
     5. Dual Export: CSV for analysis tools, JSON for programmatic access
     6. nlohmann/json: Header-only, widely used, clean API

     ---
     CSV Output Format

     # VIXEN Benchmark - Pipeline: compute, Resolution: 128, Density: 0.5
     # GPU: NVIDIA RTX 3080, Driver: 536.67
     frame,timestamp_ms,frame_time_ms,gpu_time_ms,bandwidth_read_gb,bandwidth_write_gb,vram_mb,mrays_per_sec,fps
     0,0.00,16.67,14.23,23.45,8.12,2847,124.5,59.98
     ...
     # Aggregates: frame_time_ms,16.21,18.52,16.72,0.31,16.35,16.70,17.92

     ---
     JSON Config Schema

     {
       "pipeline": "compute",
       "algorithm": "baseline",
       "scene": { "type": "cornell", "resolution": 128, "density": 0.5 },
       "render": { "width": 800, "height": 600 },
       "profiling": { "warmupFrames": 60, "measurementFrames": 300 }
     }

     ---
     Library Structure

     New Library: libraries/Profiler/ (static library, follows EventBus/Logger pattern)

     libraries/Profiler/
     ├── CMakeLists.txt
     ├── include/
     │   └── Profiler/
     │       ├── ProfilerSystem.h
     │       ├── RollingStats.h
     │       ├── DeviceCapabilities.h
     │       ├── FrameMetrics.h
     │       ├── MetricsCollector.h
     │       ├── MetricsExporter.h
     │       ├── TestSuiteResults.h
     │       └── BenchmarkConfig.h
     ├── src/
     │   ├── ProfilerSystem.cpp
     │   ├── RollingStats.cpp
     │   ├── DeviceCapabilities.cpp
     │   ├── MetricsCollector.cpp
     │   ├── MetricsExporter.cpp
     │   └── BenchmarkConfig.cpp
     └── tests/
         └── test_profiler.cpp

     ---
     Files to Create

     | File                                                     | Purpose                     | Est. Lines |
     |----------------------------------------------------------|-----------------------------|------------|
     | libraries/Profiler/CMakeLists.txt                        | Library build config        | ~40        |
     | libraries/Profiler/include/Profiler/ProfilerSystem.h     | Main system interface       | ~150       |
     | libraries/Profiler/src/ProfilerSystem.cpp                | Implementation              | ~300       |
     | libraries/Profiler/include/Profiler/RollingStats.h       | Statistics with percentiles | ~80        |
     | libraries/Profiler/src/RollingStats.cpp                  | Implementation              | ~100       |
     | libraries/Profiler/include/Profiler/DeviceCapabilities.h | GPU info capture            | ~80        |
     | libraries/Profiler/src/DeviceCapabilities.cpp            | Implementation              | ~100       |
     | libraries/Profiler/include/Profiler/FrameMetrics.h       | Metric structs              | ~60        |
     | libraries/Profiler/include/Profiler/MetricsCollector.h   | Hook-based collection       | ~80        |
     | libraries/Profiler/src/MetricsCollector.cpp              | Implementation              | ~150       |
     | libraries/Profiler/include/Profiler/MetricsExporter.h    | CSV/JSON export             | ~80        |
     | libraries/Profiler/src/MetricsExporter.cpp               | Implementation              | ~200       |
     | libraries/Profiler/include/Profiler/TestSuiteResults.h   | Aggregate results           | ~60        |
     | libraries/Profiler/include/Profiler/BenchmarkConfig.h    | Config + JSON loader        | ~100       |
     | libraries/Profiler/src/BenchmarkConfig.cpp               | Implementation              | ~150       |
     | libraries/Profiler/tests/test_profiler.cpp               | Unit tests                  | ~200       |

     ---
     Files to Modify

     | File                     | Change                             |
     |--------------------------|------------------------------------|
     | libraries/CMakeLists.txt | Add add_subdirectory(Profiler)     |
     | CMakeLists.txt           | Add nlohmann/json via FetchContent |

     ---
     Testing Strategy

     1. Unit tests for RollingStats (percentile accuracy)
     2. Unit tests for BenchmarkConfig (JSON parsing, validation)
     3. Integration test: Profile 100 frames, export CSV, verify format
     4. Manual validation: Compare bandwidth estimate vs Nsight Graphics

     ---
     Risk Mitigation

     | Risk                                 | Mitigation                            |
     |--------------------------------------|---------------------------------------|
     | VK_KHR_performance_query unavailable | Estimation fallback (always works)    |
     | Driver variance in counters          | Document counter availability per GPU |
     | CSV format issues                    | Validate with pandas import test      |

     ---
     Success Criteria

     - Profile 300+ frames with <1% overhead
     - CSV readable by pandas/Excel
     - Bandwidth within ±20% of Nsight (when hardware counters available)
     - JSON config drives test execution

## Current Focus: Phase I - Performance Profiling System

**Phase H (Voxel Infrastructure) COMPLETE:**
- Week 1-2: CPU + GPU infrastructure, 1,700 Mrays/sec achieved
- Week 3: DXT compression (5.3:1 ratio), Phase C bug fixes
- Week 4: Morton unification, SVOManager refactor, Geometric normals, LOD (16/16 tests)

**Next: Phase I - Performance Profiling System (2-3 weeks)**
- Automated per-frame metric collection
- GPU bandwidth monitoring via VK_KHR_performance_query
- CSV export for research data analysis
- Benchmark configuration system for 180-configuration test matrix

---

## Phase I Implementation Tasks

| Task | Description | Est. Time |
|------|-------------|-----------|
| I.1 | PerformanceProfiler core (rolling stats, percentiles) | 8-12h |
| I.2 | GPU performance counters (VK_KHR_performance_query) | 8-12h |
| I.3 | CSV export system | 4-6h |
| I.4 | Benchmark configuration system (JSON-driven) | 6-8h |

### I.1: PerformanceProfiler Core
```cpp
// Files to create:
RenderGraph/include/Core/PerformanceProfiler.h
RenderGraph/src/Core/PerformanceProfiler.cpp
```
- Per-frame metric aggregation (frame time, GPU time, draw calls)
- Rolling statistics (configurable window size)
- Percentile calculation (1st, 50th, 99th)
- Memory-efficient circular buffer

### I.2: GPU Performance Counters
```cpp
// Files to create:
RenderGraph/include/Core/GPUCounters.h
RenderGraph/src/Core/GPUCounters.cpp
```
- Memory bandwidth (read/write GB/s) via VK_KHR_performance_query
- VRAM usage (VK_EXT_memory_budget)
- Ray throughput (rays/sec = pixel_count / frame_time)

### I.3: CSV Export System
```cpp
// Files to create:
RenderGraph/include/Core/MetricsExporter.h
RenderGraph/src/Core/MetricsExporter.cpp
```
Output format:
```csv
frame,timestamp_ms,frame_time_ms,gpu_time_ms,bandwidth_read_gb,bandwidth_write_gb,vram_mb,rays_per_sec
```

### I.4: Benchmark Configuration
```cpp
// Files to create:
tests/Benchmarks/BenchmarkConfig.h
tests/Benchmarks/BenchmarkConfig.cpp
```

---

## Todo List (Active Tasks)

### Phase I: Performance Profiling System (NEXT)
- [ ] I.1: PerformanceProfiler core
- [ ] I.2: GPU performance counter integration
- [ ] I.3: CSV export system
- [ ] I.4: Benchmark configuration system

### Deferred to Phase N+2
- [ ] Streaming foundation (SVOStreaming.h, LRU eviction)
- [ ] GPU shader LOD early termination
- [ ] Multi-threaded CPU ray casting
- [ ] SIMD ray packets (AVX2)

---

## Technical Reference

### Phase H Final Performance

| Variant | Throughput | Memory |
|---------|------------|--------|
| Uncompressed | 1,700 Mrays/sec | ~5 MB |
| Compressed | 85-303 Mrays/sec | ~955 KB (5.3:1) |

### Key Files Created (Phase H Week 4)

| File | Purpose |
|------|---------|
| `libraries/Core/include/MortonEncoding.h` | Unified MortonCode64 |
| `libraries/SVO/src/SVOTraversal.cpp` | ESVO ray casting |
| `libraries/SVO/src/SVOBrickDDA.cpp` | Brick DDA traversal |
| `libraries/SVO/src/SVORebuild.cpp` | Entity-based build |
| `libraries/SVO/include/SVOLOD.h` | Adaptive LOD |

### ESVO Coordinate Spaces
- **LOCAL SPACE**: Octree storage (ray-independent, integer grid)
- **MIRRORED SPACE**: ESVO traversal (ray-direction-dependent)
- **WORLD SPACE**: 3D world coordinates (mat4 transform)

### octant_mask Convention
- Starts at 7, XOR each bit for positive ray direction
- bit=0 -> axis IS mirrored, bit=1 -> NOT mirrored
- Convert: `localIdx = mirroredIdx ^ (~octant_mask & 7)`

---

## Reference Sources

### ESVO Implementation Paths
- Paper: `C:\Users\liory\Downloads\source-archive\efficient-sparse-voxel-octrees\`
- Key files: `trunk/src/octree/Octree.cpp` (castRay), `trunk/src/octree/build/Builder.cpp`
- DXT: `trunk/src/octree/Util.cpp` (encode/decode)
- LOD: `trunk/src/octree/cuda/Raycast.inl` (screen-space termination)

### Papers
- Laine & Karras (2010): "Efficient Sparse Voxel Octrees"
- Amanatides & Woo (1987): "A Fast Voxel Traversal Algorithm" (DDA)

---

## Known Limitations

These edge cases are documented and accepted:

1. **Single-brick octrees**: Fallback code path for `bricksPerAxis=1`
2. **Axis-parallel rays**: Require `computeCorrectedTcMax()` filtering
3. **Brick boundaries**: Handled by descriptor-based lookup (not position-based)
4. **DXT lossy compression**: Colors may shift slightly (acceptable for voxels)

---

**End of Active Context**
