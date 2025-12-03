# Active Context

**Last Updated**: December 3, 2025
**Current Branch**: `claude/phase-h-voxel-infrastructure`
**Status**: Phase H COMPLETE - Ready for Phase I

---

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
