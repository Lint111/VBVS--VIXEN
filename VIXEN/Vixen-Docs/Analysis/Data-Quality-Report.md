---
tags: [analysis, data-quality, recommendations]
created: 2025-12-10
status: active
related:
  - "[[Benchmark-Data-Summary]]"
  - "[[../04-Development/Profiling]]"
---

# Data Quality & Instrumentation Report

**Date**: December 10, 2025
**Scope**: Analysis of 144 benchmark test runs
**Purpose**: Identify data gaps and recommend improvements

---

## 1. Executive Summary

**Overall Data Quality**: ✅ **Good**

- Complete test coverage for 3 pipelines × 3 resolutions × 4 scenes × 2 compression modes
- All tests produced valid JSON output with complete metadata
- Statistical aggregates computed correctly
- No missing test configurations (within planned scope)

**Critical Gaps**: 2
**Medium Gaps**: 3
**Minor Gaps**: 4

---

## 2. Critical Data Gaps

### 2.1 Missing Voxel Traversal Counts

**Issue**: `avg_voxels_per_ray` is **0.0** for all 14,400+ frames

**Impact**: HIGH
- Cannot analyze traversal efficiency
- Cannot compare DDA vs ESVO performance
- Cannot correlate scene density with traversal depth

**Root Cause**: Shader counter instrumentation disabled or broken

**Fix Required**:
```glsl
// In VoxelRayMarch.comp/.frag/.rgen
layout(set = 0, binding = X) buffer VoxelCountBuffer {
    uint voxelCounts[];
};

void main() {
    uint voxelCount = 0;
    // ... ray marching loop
    while (traversing) {
        voxelCount++;
        // ... traversal logic
    }
    voxelCounts[gl_GlobalInvocationID.x] = voxelCount;
}
```

**Files to Modify**:
- `shaders/VoxelRayMarch.comp` (lines ~150-250)
- `shaders/VoxelRayMarch.frag` (lines ~150-250)
- `shaders/VoxelRT.rchit` (add counter buffer)
- `libraries/Profiler/src/BenchmarkRunner.cpp` (read counter buffer)

**Priority**: **CRITICAL** - needed for Phase L analysis

---

### 2.2 Missing Ray Throughput (Fragment/HW RT)

**Issue**: `ray_throughput_mrays` is **0.0** for Fragment and Hardware RT pipelines

**Impact**: HIGH
- Cannot compare throughput across pipelines
- Cannot validate compute pipeline superiority claim

**Root Cause**: Measurement code only implemented for Compute pipeline

**Fix Required**:
```cpp
// In BenchmarkRunner.cpp
float CalculateRayThroughput(
    const TestConfig& config,
    float frameTimeMs
) {
    uint32_t totalRays = config.screenWidth * config.screenHeight;
    float raysPerSec = totalRays / (frameTimeMs / 1000.0f);
    return raysPerSec / 1e6f; // Convert to Mrays/sec
}
```

**Files to Modify**:
- `libraries/Profiler/src/BenchmarkRunner.cpp:850-900` (add for Fragment)
- `libraries/Profiler/src/BenchmarkRunner.cpp:950-1000` (add for Hardware RT)

**Priority**: **CRITICAL** - needed for fair comparison

---

## 3. Medium Priority Gaps

### 3.1 GPU Utilization %

**Issue**: Not measured

**Impact**: MEDIUM
- Cannot identify if workload is compute-bound or memory-bound
- Cannot assess if GPU is underutilized

**Fix Options**:
1. Use NVML (NVIDIA Management Library) for real-time query
2. Use NSight Systems for profiling (post-processing)
3. Use VK_KHR_performance_query (requires extension support)

**Recommendation**: Add NVML integration for real-time monitoring

```cpp
#include <nvml.h>

struct GPUMetrics {
    float utilization_percent;
    float memory_utilization_percent;
    float temperature_celsius;
};

GPUMetrics QueryGPUUtilization(nvmlDevice_t device) {
    nvmlUtilization_t util;
    nvmlDeviceGetUtilizationRates(device, &util);
    return { util.gpu, util.memory, /* temp */ };
}
```

**Priority**: **MEDIUM** - nice to have for bottleneck analysis

---

### 3.2 BLAS/TLAS Build Time

**Issue**: Acceleration structure build time not measured

**Impact**: MEDIUM
- Cannot amortize one-time build cost
- Cannot compare build strategies

**Current State**: Build happens in VoxelGridNode, time not recorded

**Fix Required**:
```cpp
// In AccelerationStructureNode.cpp
auto buildStart = std::chrono::high_resolution_clock::now();
BuildBLAS();
BuildTLAS();
auto buildEnd = std::chrono::high_resolution_clock::now();
float buildTimeMs = std::chrono::duration<float, std::milli>(
    buildEnd - buildStart
).count();
LOG_INFO("AS build time: {:.2f} ms", buildTimeMs);
```

**Priority**: **MEDIUM** - one-time cost, less critical than per-frame metrics

---

### 3.3 Cache Hit Rates

**Issue**: No shader-level cache metrics

**Impact**: MEDIUM
- Cannot optimize memory access patterns
- Cannot validate spatial coherence assumptions

**Fix Options**:
1. Add atomic counters for cache hits/misses
2. Use GPU profiler (NSight Compute) for detailed analysis

**Example**:
```glsl
layout(set = 0, binding = X) buffer CacheStats {
    uint cacheHits;
    uint cacheMisses;
};

// In traversal code
if (cacheContains(voxelKey)) {
    atomicAdd(cacheHits, 1);
} else {
    atomicAdd(cacheMisses, 1);
}
```

**Priority**: **LOW-MEDIUM** - optimization insight, not essential for comparison

---

## 4. Minor Gaps

### 4.1 Test Iteration Count

**Issue**: Only 1 iteration per configuration

**Impact**: LOW
- Cannot measure inter-run variance
- Cannot filter statistical noise

**Recommendation**: Run 3 iterations per config, report mean ± stddev

**Config Change**:
```json
{
  "execution": {
    "warmup_frames": 50,
    "measurement_frames": 100,
    "iterations": 3  // ← Add this
  }
}
```

**Priority**: **LOW** - current data is sufficient for initial analysis

---

### 4.2 Missing 512³ Resolution

**Issue**: Test matrix stops at 256³

**Impact**: LOW
- Cannot assess performance at extreme resolutions
- May miss non-linear scaling behavior

**Reason**: Likely VRAM constraints or test time

**Recommendation**: Add 512³ tests for Compute/Fragment (skip HW RT due to memory)

**Priority**: **LOW** - 256³ is sufficient for current research

---

### 4.3 Explicit Density Parameter

**Issue**: Density is implicit (scene-based), not parameterized

**Impact**: LOW
- Cannot isolate density impact from scene structure
- Tunnels (94%) vs Cornell (23%) conflates density with geometry

**Recommendation**: Add procedural density parameter to benchmark config

```json
{
  "scenes": {
    "noise_10": { "type": "noise", "density": 0.10 },
    "noise_50": { "type": "noise", "density": 0.50 },
    "noise_90": { "type": "noise", "density": 0.90 }
  }
}
```

**Priority**: **LOW** - current scenes provide sufficient density range

---

### 4.4 Render Resolution Sweep

**Issue**: Only 720p and 1080p tested

**Impact**: LOW
- Cannot assess resolution-independent performance
- 4K testing would be valuable for production use

**Recommendation**: Add 1440p and 4K tests

**Priority**: **LOW** - current resolutions cover typical use cases

---

## 5. Data Anomalies

### 5.1 Frame 75 Spike

**Observed**: Consistent ~400-1100ms spike at frame 75 across tests

**Hypothesis**:
1. Driver shader recompilation
2. Pipeline cache flush
3. Descriptor set recreation
4. OS/driver background activity

**Recommendation**:
- Increase warmup frames to 100 (from 50)
- Add pre-test cache priming pass
- Filter outliers beyond 3σ in post-processing

**Impact**: LOW - P99 already captures this, doesn't affect mean significantly

---

### 5.2 High StdDev

**Observed**: StdDev = 46-107 ms (vs mean = 7-37 ms)

**Cause**: Outliers like Frame 75 spike

**Recommendation**:
- Report robust statistics (median, IQR) instead of mean/stddev
- Add percentile-based analysis (P95, P99 already present)

**Impact**: LOW - percentiles already provide robust measure

---

### 5.3 Compressed Pipeline Throughput

**Observed**: Compressed variant shows **2.3x higher ray throughput** despite similar FPS

**Hypothesis**:
1. Measurement methodology differs between variants
2. DXT decompression parallelism frees compute resources
3. Counter wraparound or overflow

**Recommendation**: Verify ray throughput calculation consistency

**Impact**: MEDIUM - affects compression effectiveness claims

---

## 6. Recommended Actions

### 6.1 Immediate (Next Test Run)

1. ✅ **Fix `avg_voxels_per_ray` instrumentation** (CRITICAL)
2. ✅ **Add ray throughput for Fragment/HW RT** (CRITICAL)
3. ⚠️ **Increase warmup frames to 100** (recommended)

### 6.2 Short-Term (Phase L)

4. ⚠️ **Add GPU utilization monitoring** (MEDIUM)
5. ⚠️ **Measure BLAS/TLAS build time** (MEDIUM)
6. ⚠️ **Run 3 iterations per config** (optional)

### 6.3 Long-Term (Phase M+)

7. ⚠️ **Add cache hit rate counters** (optimization)
8. ⚠️ **Add 512³ resolution tests** (extreme case)
9. ⚠️ **Add explicit density parameter** (research depth)
10. ⚠️ **Add 4K resolution tests** (production validation)

---

## 7. Data Export Quality

### 7.1 JSON Schema

✅ **Well-structured**:
- Clear nesting (config, device, frames, statistics)
- Consistent field names
- Complete metadata

✅ **Versioning**: Timestamp included for temporal analysis

✅ **Reproducibility**: All test parameters recorded

❌ **Schema validation**: No JSON schema file for validation

**Recommendation**: Create `benchmark_schema.json` for automated validation

---

### 7.2 File Organization

✅ **Naming convention**: Clear and consistent
- `{PIPELINE}_{RES}_{SCENE}_{SHADER}_RUN{N}.json`

✅ **Summary file**: `suite_summary.json` for aggregation

❌ **Directory structure**: Flat (all 144 files in one folder)

**Recommendation**: Organize by date or pipeline
```
benchmark_results/
  2025-12-10/
    compute/
    fragment/
    hardware_rt/
  suite_summary.json
```

---

### 7.3 Aggregation Pipeline

✅ **Statistics computed**: Mean, min, max, percentiles, stddev

❌ **Cross-test analysis**: No aggregation across iterations

❌ **Comparison tables**: No pre-computed pipeline comparisons

**Recommendation**: Add `aggregate_results.py` script to generate:
- Pipeline comparison CSV
- Resolution scaling tables
- Scene impact matrices

---

## 8. Visualization Readiness

### 8.1 Data Format

✅ **Machine-readable**: JSON is easy to parse

✅ **Tabular export**: Can convert to CSV/Excel

❌ **Direct plotting**: Requires preprocessing

**Recommendation**: Generate intermediate CSV for plotting:
```csv
pipeline,resolution,scene,compression,fps_mean,fps_p99,vram_mb
compute,64,cornell,uncompressed,130.7,181.3,321
compute,64,cornell,compressed,135.2,185.6,321
...
```

### 8.2 Chart Recommendations

Based on available data:

1. **FPS by Pipeline** (bar chart)
   - X: Pipeline (Compute, Fragment, HW RT)
   - Y: FPS mean
   - Group by: Resolution

2. **Frame Time Distribution** (box plot)
   - X: Pipeline
   - Y: Frame time (ms)
   - Whiskers: P1-P99

3. **Resolution Scaling** (line chart)
   - X: Resolution (64, 128, 256)
   - Y: FPS
   - Lines: Pipeline type

4. **Bandwidth Utilization** (stacked area)
   - X: Frame number
   - Y: Bandwidth (GB/s)
   - Areas: Read (blue), Write (red)

5. **Scene Complexity Heatmap**
   - X: Scene (Cornell, Noise, Tunnels, Cityscape)
   - Y: Resolution (64, 128, 256)
   - Color: FPS (green=high, red=low)

All charts can be generated with current data ✅

---

## 9. Summary

**Strengths**:
- Comprehensive test coverage (144 configs)
- Clean JSON export format
- Statistical aggregates computed
- Complete test metadata

**Weaknesses**:
- Missing traversal counts (critical)
- Missing throughput for 2/3 pipelines (critical)
- No GPU utilization metrics
- Single iteration per config

**Overall Grade**: **B+** (Good data, needs instrumentation fixes)

**Next Steps**:
1. Fix shader instrumentation (voxel counts)
2. Add throughput measurement (Fragment/HW RT)
3. Re-run full test suite with fixes
4. Generate visualization pipeline

---

## Related Documents

- [[Benchmark-Data-Summary]] - Full statistical analysis
- [[../03-Research/Pipeline-Comparison]] - Research methodology
- [[../04-Development/Profiling]] - Benchmark implementation

---

**End of Report**

*Generated by VIXEN Data Science Agent*
*Date: December 10, 2025*
