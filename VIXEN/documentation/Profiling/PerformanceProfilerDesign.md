# Performance Profiler Design Document

**Created**: November 2, 2025
**Phase**: I.1 (Performance Profiling System - Design)
**Status**: Design Complete - Ready for Implementation

---

## Executive Summary

This document specifies the design of a **Performance Profiling System** for automated metrics collection during voxel ray tracing research. The profiler captures frame timing, GPU performance, memory bandwidth, and ray throughput for 180+ test configurations, enabling statistical analysis and comparative pipeline evaluation.

**Key Requirements**:
1. **Per-frame metrics**: Frame time, GPU time, bandwidth, VRAM usage
2. **Statistical analysis**: Min, max, mean, stddev, percentiles (1st, 50th, 99th)
3. **CSV export**: Structured format for external analysis (Python, R, Excel)
4. **Low overhead**: <1% performance impact on measurements
5. **Automated testing**: Integrate with Phase M benchmark runner

---

## Research Context

### Test Matrix Requirements

**180 Configurations**:
- 4 pipelines (compute, fragment, hardware RT, hybrid)
- 5 resolutions (32³, 64³, 128³, 256³, 512³)
- 3 densities (sparse, medium, dense)
- 3 algorithm variants (baseline, empty-skip, BlockWalk)

**Per Configuration**:
- Warmup: 60 frames (discard for measurement)
- Measurement: 300 frames (collect metrics)
- Output: CSV file with per-frame data + aggregate statistics

**Total Data Volume**:
- 180 configs × 300 frames = 54,000 frames
- ~1 KB per frame = ~54 MB total CSV data

---

## Section 1: Metric Collection Strategy

### 1.1 Primary Metrics

**Frame Timing (CPU)**:
```cpp
struct FrameTiming {
    uint64_t frameNumber;        // Monotonic frame counter
    double timestampMs;          // Time since test start (ms)
    float frameTimeMs;           // Delta time between frames (ms)
};
```

**GPU Timing** (via VkQueryPool):
```cpp
struct GPUTiming {
    float gpuTimeMs;             // GPU execution time (ms)
    float computeShaderTimeMs;   // Compute dispatch time (ms, if applicable)
    float fragmentShaderTimeMs;  // Fragment shader time (ms, if applicable)
    float rayTracingTimeMs;      // Ray tracing dispatch time (ms, if applicable)
};
```

**Memory Bandwidth** (via VK_KHR_performance_query):
```cpp
struct MemoryBandwidth {
    float bandwidthReadGB;       // Read bandwidth (GB/s)
    float bandwidthWriteGB;      // Write bandwidth (GB/s)
    float totalBandwidthGB;      // Read + Write (GB/s)
};
```

**Memory Usage** (via VK_EXT_memory_budget):
```cpp
struct MemoryUsage {
    uint64_t vramUsageMB;        // Current VRAM usage (MB)
    uint64_t vramBudgetMB;       // Available VRAM budget (MB)
    float vramUtilization;       // Usage / Budget (0.0-1.0)
};
```

**Ray Throughput** (derived):
```cpp
struct RayThroughput {
    uint64_t raysPerSecond;      // Total rays / frame time
    float mRaysPerSecond;        // Million rays per second
    float raysPerPixel;          // Average rays per pixel (for adaptive sampling)
};
```

**Traversal Metrics** (shader instrumentation):
```cpp
struct TraversalMetrics {
    float avgVoxelsPerRay;       // Average voxels tested per ray
    float avgNodesPerRay;        // Average octree nodes visited
    float emptySpaceSkipRatio;   // Percentage of empty space skipped
};
```

### 1.2 Collection Mechanisms

**Timestamp Queries** (VkQueryPool):
```cpp
// RenderGraph/include/Core/PerformanceProfiler.h

class TimestampQueryPool {
public:
    TimestampQueryPool(VkDevice device, uint32_t queryCount);

    void Reset(VkCommandBuffer cmd);
    void WriteTimestamp(VkCommandBuffer cmd, uint32_t queryIndex, VkPipelineStageFlagBits stage);
    std::vector<uint64_t> GetResults();  // CPU reads back results

    float GetElapsedMs(uint32_t startIdx, uint32_t endIdx) const;

private:
    VkDevice device_;
    VkQueryPool queryPool_;
    uint32_t queryCount_;
    float timestampPeriod_;  // From VkPhysicalDeviceLimits
};
```

**Usage**:
```cpp
// Record timestamps around compute dispatch
timestampPool.WriteTimestamp(cmd, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
timestampPool.WriteTimestamp(cmd, 1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

// Later: Read results
auto timestamps = timestampPool.GetResults();
float gpuTimeMs = timestampPool.GetElapsedMs(0, 1);
```

**Performance Counters** (VK_KHR_performance_query):
```cpp
class PerformanceCounterQuery {
public:
    PerformanceCounterQuery(VkDevice device, VkPhysicalDevice physicalDevice);

    void BeginQuery(VkCommandBuffer cmd);
    void EndQuery(VkCommandBuffer cmd);

    MemoryBandwidth GetBandwidth() const;

private:
    VkDevice device_;
    VkQueryPool performanceQueryPool_;

    // Counter indices (device-specific)
    uint32_t readBandwidthCounterIdx_;
    uint32_t writeBandwidthCounterIdx_;
};
```

**Platform Support**:
- **NVIDIA**: `VK_KHR_performance_query` supported (RTX 2000+ series)
- **AMD**: Limited support (fallback to estimates)
- **Intel**: No support (fallback only)

**Fallback** (bandwidth estimation):
```cpp
// If VK_KHR_performance_query unavailable, estimate from known access patterns
float EstimateBandwidth(uint32_t voxelResolution, float gpuTimeMs, uint32_t rayCount) {
    // Assume each ray samples N voxels on average
    uint32_t avgSamplesPerRay = voxelResolution / 4;  // Heuristic
    uint64_t totalSamples = rayCount * avgSamplesPerRay;
    uint64_t totalBytes = totalSamples * 1;  // 1 byte per voxel (R8)

    float seconds = gpuTimeMs / 1000.0f;
    return (totalBytes / seconds) / 1e9f;  // GB/s
}
```

**Memory Budget** (VK_EXT_memory_budget):
```cpp
class MemoryBudgetTracker {
public:
    MemoryBudgetTracker(VkDevice device, VkPhysicalDevice physicalDevice);

    MemoryUsage GetMemoryUsage() const;

private:
    VkDevice device_;
    VkPhysicalDevice physicalDevice_;
};

MemoryUsage MemoryBudgetTracker::GetMemoryUsage() const {
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProps = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT
    };
    VkPhysicalDeviceMemoryProperties2 memProps = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        .pNext = &budgetProps
    };

    vkGetPhysicalDeviceMemoryProperties2(physicalDevice_, &memProps);

    // Sum DEVICE_LOCAL heaps
    uint64_t totalUsage = 0;
    uint64_t totalBudget = 0;
    for (uint32_t i = 0; i < memProps.memoryProperties.memoryHeapCount; ++i) {
        if (memProps.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            totalUsage += budgetProps.heapUsage[i];
            totalBudget += budgetProps.heapBudget[i];
        }
    }

    return {
        .vramUsageMB = totalUsage / (1024 * 1024),
        .vramBudgetMB = totalBudget / (1024 * 1024),
        .vramUtilization = static_cast<float>(totalUsage) / totalBudget
    };
}
```

### 1.3 Derived Metrics

**Ray Throughput**:
```cpp
RayThroughput CalculateRayThroughput(uint32_t screenWidth, uint32_t screenHeight, float frameTimeMs) {
    uint64_t totalRays = static_cast<uint64_t>(screenWidth) * screenHeight;
    float seconds = frameTimeMs / 1000.0f;
    uint64_t raysPerSecond = static_cast<uint64_t>(totalRays / seconds);

    return {
        .raysPerSecond = raysPerSecond,
        .mRaysPerSecond = raysPerSecond / 1e6f,
        .raysPerPixel = 1.0f  // Baseline (adaptive sampling would vary)
    };
}
```

**Bandwidth Efficiency**:
```cpp
float CalculateBandwidthEfficiency(float bandwidthGB, uint64_t raysPerSecond) {
    // Rays per GB of bandwidth (higher = more efficient)
    return raysPerSecond / (bandwidthGB * 1e9f);
}
```

---

## Section 2: Statistical Analysis Design

### 2.1 Rolling Statistics

**Purpose**: Real-time statistics over sliding window (e.g., last 60 frames).

```cpp
class RollingStats {
public:
    RollingStats(size_t windowSize = 60);

    void AddSample(float value);
    void Reset();

    float GetMin() const;
    float GetMax() const;
    float GetMean() const;
    float GetStdDev() const;
    float GetPercentile(float p) const;  // p ∈ [0, 1]

private:
    std::deque<float> samples_;
    size_t windowSize_;

    // Cached statistics (updated on AddSample)
    float min_;
    float max_;
    float sum_;
    float sumSquared_;
};

void RollingStats::AddSample(float value) {
    samples_.push_back(value);

    if (samples_.size() > windowSize_) {
        float removed = samples_.front();
        samples_.pop_front();

        // Update cached values
        sum_ -= removed;
        sumSquared_ -= removed * removed;
    }

    // Update cached values
    sum_ += value;
    sumSquared_ += value * value;
    min_ = std::min(min_, value);
    max_ = std::max(max_, value);
}

float RollingStats::GetMean() const {
    return samples_.empty() ? 0.0f : sum_ / samples_.size();
}

float RollingStats::GetStdDev() const {
    if (samples_.empty()) return 0.0f;

    float mean = GetMean();
    float variance = (sumSquared_ / samples_.size()) - (mean * mean);
    return std::sqrt(std::max(0.0f, variance));
}

float RollingStats::GetPercentile(float p) const {
    if (samples_.empty()) return 0.0f;

    std::vector<float> sorted(samples_.begin(), samples_.end());
    std::sort(sorted.begin(), sorted.end());

    size_t index = static_cast<size_t>(p * (sorted.size() - 1));
    return sorted[index];
}
```

### 2.2 Aggregate Statistics

**Purpose**: Full-test statistics (all 300 measurement frames).

```cpp
struct AggregateStats {
    float min;
    float max;
    float mean;
    float stddev;
    float percentile_1st;   // 1st percentile (outlier threshold)
    float percentile_50th;  // Median
    float percentile_99th;  // 99th percentile (outlier threshold)
    uint32_t sampleCount;
};

AggregateStats CalculateAggregateStats(const std::vector<float>& samples) {
    if (samples.empty()) {
        return {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0};
    }

    // Sort for percentiles
    std::vector<float> sorted = samples;
    std::sort(sorted.begin(), sorted.end());

    // Min, max
    float min = sorted.front();
    float max = sorted.back();

    // Mean
    float sum = std::accumulate(sorted.begin(), sorted.end(), 0.0f);
    float mean = sum / sorted.size();

    // Standard deviation
    float sumSquaredDiff = 0.0f;
    for (float val : sorted) {
        float diff = val - mean;
        sumSquaredDiff += diff * diff;
    }
    float stddev = std::sqrt(sumSquaredDiff / sorted.size());

    // Percentiles
    auto getPercentile = [&sorted](float p) {
        size_t idx = static_cast<size_t>(p * (sorted.size() - 1));
        return sorted[idx];
    };

    return {
        .min = min,
        .max = max,
        .mean = mean,
        .stddev = stddev,
        .percentile_1st = getPercentile(0.01f),
        .percentile_50th = getPercentile(0.50f),
        .percentile_99th = getPercentile(0.99f),
        .sampleCount = static_cast<uint32_t>(sorted.size())
    };
}
```

---

## Section 3: CSV Export Format

### 3.1 File Format Specification

**Header** (metadata comments):
```csv
# VIXEN Voxel Ray Tracing Benchmark Results
# Test Configuration
# Pipeline: compute
# Resolution: 128
# Density: 0.5
# Algorithm: empty_skip
# Scene: sphere
# Date: 2025-11-15T14:30:00Z
# GPU: NVIDIA GeForce RTX 3080
# Driver: 536.67
# Vulkan: 1.4.321
# Warmup Frames: 60
# Measurement Frames: 300
#
# Column Descriptions:
# frame: Frame number (0-indexed)
# timestamp_ms: Time since test start (milliseconds)
# frame_time_ms: CPU frame time (ms)
# gpu_time_ms: GPU execution time (ms, from timestamp queries)
# bandwidth_read_gb: Memory read bandwidth (GB/s, from performance counters or estimated)
# bandwidth_write_gb: Memory write bandwidth (GB/s)
# vram_mb: VRAM usage (MB)
# rays_per_sec: Ray throughput (rays/second)
# mrays_per_sec: Ray throughput (million rays/second)
# voxels_per_ray: Average voxels tested per ray (from shader instrumentation)
# fps: Frames per second (1000 / frame_time_ms)
#
# Aggregate Statistics (appended at end of file)
```

**Data Rows**:
```csv
frame,timestamp_ms,frame_time_ms,gpu_time_ms,bandwidth_read_gb,bandwidth_write_gb,vram_mb,rays_per_sec,mrays_per_sec,voxels_per_ray,fps
0,0.0,16.7,14.2,23.4,8.1,2847,124000000,124.0,23.4,59.88
1,16.7,16.8,14.3,23.5,8.2,2847,123800000,123.8,23.5,59.52
2,33.5,16.6,14.1,23.3,8.0,2847,124200000,124.2,23.3,60.24
...
299,4998.3,16.7,14.2,23.4,8.1,2847,124000000,124.0,23.4,59.88
```

**Aggregate Statistics** (footer):
```csv
#
# Aggregate Statistics (300 frames)
# metric,min,max,mean,stddev,p1,p50,p99
frame_time_ms,16.2,18.5,16.7,0.3,16.3,16.7,17.9
gpu_time_ms,13.8,15.2,14.2,0.2,14.0,14.2,14.8
bandwidth_read_gb,22.8,24.5,23.4,0.4,23.0,23.4,24.2
bandwidth_write_gb,7.8,8.6,8.1,0.2,7.9,8.1,8.4
vram_mb,2847,2847,2847,0,2847,2847,2847
rays_per_sec,120000000,128000000,124000000,1500000,121000000,124000000,126000000
mrays_per_sec,120.0,128.0,124.0,1.5,121.0,124.0,126.0
voxels_per_ray,22.1,24.8,23.4,0.6,22.5,23.4,24.5
fps,54.1,61.7,59.9,1.1,55.9,59.9,61.4
```

### 3.2 Export Implementation

```cpp
class CSVExporter {
public:
    CSVExporter(const std::string& filepath);

    void WriteHeader(const BenchmarkConfig& config);
    void WriteDataRow(const FrameMetrics& metrics);
    void WriteAggregateStats(const std::map<std::string, AggregateStats>& stats);

    void Flush();

private:
    std::ofstream file_;
    bool headerWritten_;
};

void CSVExporter::WriteHeader(const BenchmarkConfig& config) {
    file_ << "# VIXEN Voxel Ray Tracing Benchmark Results\n";
    file_ << "# Test Configuration\n";
    file_ << "# Pipeline: " << config.pipeline << "\n";
    file_ << "# Resolution: " << config.voxelResolution << "\n";
    file_ << "# Density: " << config.densityPercent << "\n";
    file_ << "# Algorithm: " << config.algorithm << "\n";
    file_ << "# Scene: " << config.sceneType << "\n";
    file_ << "# Date: " << GetISO8601Timestamp() << "\n";
    file_ << "# GPU: " << config.gpuName << "\n";
    file_ << "# Driver: " << config.driverVersion << "\n";
    file_ << "# Vulkan: " << config.vulkanVersion << "\n";
    file_ << "# Warmup Frames: " << config.warmupFrames << "\n";
    file_ << "# Measurement Frames: " << config.measurementFrames << "\n";
    file_ << "#\n";

    // Column headers
    file_ << "frame,timestamp_ms,frame_time_ms,gpu_time_ms,"
          << "bandwidth_read_gb,bandwidth_write_gb,vram_mb,"
          << "rays_per_sec,mrays_per_sec,voxels_per_ray,fps\n";

    headerWritten_ = true;
}

void CSVExporter::WriteDataRow(const FrameMetrics& metrics) {
    file_ << metrics.frameNumber << ","
          << metrics.timestampMs << ","
          << metrics.frameTimeMs << ","
          << metrics.gpuTimeMs << ","
          << metrics.bandwidthReadGB << ","
          << metrics.bandwidthWriteGB << ","
          << metrics.vramUsageMB << ","
          << metrics.raysPerSecond << ","
          << metrics.mRaysPerSecond << ","
          << metrics.voxelsPerRay << ","
          << metrics.fps << "\n";
}

void CSVExporter::WriteAggregateStats(const std::map<std::string, AggregateStats>& stats) {
    file_ << "#\n# Aggregate Statistics\n";
    file_ << "# metric,min,max,mean,stddev,p1,p50,p99\n";

    for (const auto& [metricName, stat] : stats) {
        file_ << metricName << ","
              << stat.min << ","
              << stat.max << ","
              << stat.mean << ","
              << stat.stddev << ","
              << stat.percentile_1st << ","
              << stat.percentile_50th << ","
              << stat.percentile_99th << "\n";
    }
}
```

---

## Section 4: Profiler API Design

### 4.1 Public Interface

```cpp
// RenderGraph/include/Core/PerformanceProfiler.h

class PerformanceProfiler {
public:
    // Lifecycle
    PerformanceProfiler(VkDevice device, VkPhysicalDevice physicalDevice);
    ~PerformanceProfiler();

    void Initialize();
    void Shutdown();

    // Per-frame collection
    void BeginFrame();
    void EndFrame();

    void RecordTimestamp(VkCommandBuffer cmd, const std::string& label);

    // Query results
    const FrameMetrics& GetLastFrameMetrics() const;
    const RollingStats& GetRollingStats(const std::string& metricName) const;

    // Export
    void ExportToCSV(const std::string& filepath, const BenchmarkConfig& config);
    std::map<std::string, AggregateStats> GetAggregateStats() const;

    // Configuration
    void SetRollingWindowSize(uint32_t frames);
    void EnableMetric(const std::string& metricName, bool enable);

    void SetWarmupFrames(uint32_t frames) { warmupFrames_ = frames; }
    void SetMeasurementFrames(uint32_t frames) { measurementFrames_ = frames; }

private:
    VkDevice device_;
    VkPhysicalDevice physicalDevice_;

    // Query pools
    std::unique_ptr<TimestampQueryPool> timestampPool_;
    std::unique_ptr<PerformanceCounterQuery> performanceQuery_;
    std::unique_ptr<MemoryBudgetTracker> memoryTracker_;

    // Per-frame data
    std::vector<FrameMetrics> frameHistory_;
    FrameMetrics currentFrame_;

    // Rolling statistics
    std::map<std::string, RollingStats> rollingStats_;

    // Configuration
    uint32_t warmupFrames_ = 60;
    uint32_t measurementFrames_ = 300;
    uint32_t currentFrameNumber_ = 0;
    bool inWarmup_ = true;

    // Timestamps
    std::chrono::high_resolution_clock::time_point testStartTime_;
    std::chrono::high_resolution_clock::time_point lastFrameTime_;
};
```

### 4.2 Usage Example

```cpp
// Initialize profiler
PerformanceProfiler profiler(device, physicalDevice);
profiler.Initialize();
profiler.SetWarmupFrames(60);
profiler.SetMeasurementFrames(300);

// Per-frame loop
for (uint32_t frame = 0; frame < 360; ++frame) {
    profiler.BeginFrame();

    // Record command buffer
    VkCommandBuffer cmd = BeginCommandBuffer();

    profiler.RecordTimestamp(cmd, "compute_start");
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
    profiler.RecordTimestamp(cmd, "compute_end");

    EndCommandBuffer(cmd);

    // Submit and present
    SubmitCommandBuffer(cmd);
    Present();

    profiler.EndFrame();

    // Optional: Print rolling stats
    if (frame % 60 == 0) {
        auto stats = profiler.GetRollingStats("frame_time_ms");
        std::cout << "Frame time (mean): " << stats.GetMean() << " ms\n";
    }
}

// Export results
BenchmarkConfig config = {
    .pipeline = "compute",
    .voxelResolution = 128,
    .densityPercent = 0.5f,
    .algorithm = "baseline",
    .sceneType = "sphere"
};

profiler.ExportToCSV("results/compute_128_sparse_baseline.csv", config);

// Get aggregate statistics
auto aggregateStats = profiler.GetAggregateStats();
std::cout << "Mean GPU time: " << aggregateStats["gpu_time_ms"].mean << " ms\n";
std::cout << "99th percentile: " << aggregateStats["gpu_time_ms"].percentile_99th << " ms\n";
```

### 4.3 Integration with RenderGraph

```cpp
// RenderGraph/include/Core/RenderGraph.h

class RenderGraph {
public:
    // ... existing methods

    void SetPerformanceProfiler(std::shared_ptr<PerformanceProfiler> profiler) {
        profiler_ = profiler;
    }

    void RenderFrame() {
        if (profiler_) {
            profiler_->BeginFrame();
        }

        // ... existing render logic

        if (profiler_) {
            profiler_->EndFrame();
        }
    }

private:
    std::shared_ptr<PerformanceProfiler> profiler_;
};
```

---

## Section 5: Performance Overhead Analysis

### 5.1 Overhead Sources

**Timestamp Queries**:
- CPU overhead: ~0.01ms per query (negligible)
- GPU overhead: ~0.1µs per query (negligible)
- **Total**: <0.1ms per frame

**Performance Counters**:
- CPU overhead: ~0.05ms per query
- GPU overhead: ~0.2µs per query
- **Total**: <0.2ms per frame

**Memory Budget**:
- CPU overhead: ~0.02ms per query (VkPhysicalDeviceMemoryBudget)
- **Total**: <0.05ms per frame

**CSV Writing** (disk I/O):
- Per-frame: ~0.1ms (buffered writes)
- Flush on shutdown: ~50ms for 300 frames

**Total Overhead**: <0.5ms per frame (~0.5% at 60 FPS)

### 5.2 Optimization Strategies

**Double-Buffered Query Pools**:
```cpp
// Avoid GPU stalls by using 2 query pools (ping-pong)
class TimestampQueryPool {
private:
    VkQueryPool queryPools_[2];
    uint32_t currentPoolIdx_ = 0;

public:
    void BeginFrame() {
        currentPoolIdx_ = (currentPoolIdx_ + 1) % 2;
        // Reset pool for next frame
    }

    std::vector<uint64_t> GetResults() {
        // Read from previous frame's pool (already completed)
        uint32_t readPoolIdx = (currentPoolIdx_ + 1) % 2;
        return ReadQueryPool(queryPools_[readPoolIdx]);
    }
};
```

**Async CSV Writing**:
```cpp
class CSVExporter {
private:
    std::thread writerThread_;
    std::queue<FrameMetrics> writeQueue_;
    std::mutex queueMutex_;

public:
    void WriteDataRowAsync(const FrameMetrics& metrics) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        writeQueue_.push(metrics);
    }

    void FlushAsync() {
        // Writer thread flushes queue in background
    }
};
```

---

## Summary

**Design Complete**: ✅ All 4 tasks specified

### Key Design Decisions

1. **Metric Collection**: Vulkan queries (timestamps, performance counters) + memory budget API
2. **Statistics**: Rolling stats (60-frame window) + aggregate stats (300 frames)
3. **Export Format**: CSV with metadata header + per-frame data + aggregate footer
4. **API**: Simple Begin/End frame pattern, minimal overhead (<0.5ms)

### Implementation Plan (Phase I)

**I.1**: Core profiler class (8-12h)
- `PerformanceProfiler` with begin/end frame
- Rolling statistics tracking

**I.2**: GPU performance counters (8-12h)
- VK_KHR_performance_query integration
- Bandwidth measurement
- Fallback estimation for unsupported GPUs

**I.3**: CSV export system (4-6h)
- Header generation
- Per-frame row writing
- Aggregate statistics footer

**I.4**: RenderGraph integration (2-4h)
- Hook profiler into RenderFrame()
- Node-level timestamp recording

**Total**: 22-34 hours for Phase I implementation

### Next Steps

1. ✅ Complete design (this document)
2. ⏳ Implement `PerformanceProfiler` class (Phase I.1)
3. ⏳ Implement GPU counter queries (Phase I.2)
4. ⏳ Implement CSV export (Phase I.3)
5. ⏳ Integrate with RenderGraph (Phase I.4)
6. ⏳ Validate with pilot tests (Phase I.5)

**Status**: Design complete ✅ - Ready for Phase I implementation after Phase F + G + H
