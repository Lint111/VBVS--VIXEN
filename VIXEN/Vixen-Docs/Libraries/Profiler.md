---
title: Profiler Library
aliases: [Profiler, Performance, GPU Timing, Benchmarks]
tags: [library, profiling, performance, gpu]
created: 2025-12-06
related:
  - "[[Overview]]"
  - "[[VulkanResources]]"
  - "[[RenderGraph]]"
---

# Profiler Library

GPU performance measurement, benchmark runner, and metrics export for VIXEN.

> **Note:** For detailed profiling workflow, see [[../04-Development/Profiling|Profiling Guide]].

---

## 1. GPU Timestamp Queries

### 1.1 GPUTimestampQuery Class

```cpp
class GPUTimestampQuery {
    VkQueryPool timestampPool_;
    VkQueryPool pipelineStatsPool_;
    float timestampPeriod_;  // Nanoseconds per tick
    uint32_t maxTimestamps_ = 4;

public:
    void Create(VkDevice device, VkPhysicalDevice physicalDevice);
    void ResetQueries(VkCommandBuffer cmd);
    void WriteTimestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage, uint32_t query);
    std::optional<double> GetElapsedMs(VkDevice device, uint32_t startQuery, uint32_t endQuery);
    double CalculateMraysPerSec(double elapsedMs, uint32_t width, uint32_t height);
};
```

### 1.2 Usage Pattern

```cpp
void ComputeDispatchNode::ExecuteImpl(Context& ctx) {
    // Reset queries at frame start
    gpuQuery_.ResetQueries(cmd);

    // Record start timestamp
    gpuQuery_.WriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);

    // Dispatch compute shader
    vkCmdDispatch(cmd, dispatchX, dispatchY, dispatchZ);

    // Record end timestamp
    gpuQuery_.WriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1);
}
```

---

## 2. Metrics

### 2.1 Primary Metrics

| Metric | Unit | Calculation |
|--------|------|-------------|
| Frame Time | ms | CPU timer |
| GPU Time | ms | VkQueryPool timestamps |
| Ray Throughput | Mrays/sec | (width * height) / (gpu_time_s * 1e6) |
| VRAM Usage | MB | VK_EXT_memory_budget |

### 2.2 Statistics

| Statistic | Description |
|-----------|-------------|
| Min | Minimum over window |
| Max | Maximum over window |
| Mean | Average over window |
| StdDev | Standard deviation |
| P50, P95, P99 | Percentiles |

---

## 3. Benchmark Runner

### 3.1 Configuration

```json
{
  "tests": [
    {
      "name": "compute_cornell_128",
      "pipeline": "compute",
      "shader": "VoxelRayMarch.comp",
      "scene": "cornell",
      "resolution": 128,
      "frames": 300
    }
  ],
  "output": {
    "format": ["json", "csv"],
    "directory": "results/"
  }
}
```

### 3.2 BenchmarkRunner Class

```cpp
class BenchmarkRunner {
public:
    void LoadConfig(const std::string& configPath);
    void Run();
    void ExportResults(const std::string& outputPath);

private:
    std::vector<BenchmarkConfig> tests_;
    std::vector<BenchmarkResult> results_;
};
```

### 3.3 Execution

```bash
./binaries/vixen_benchmark.exe \
  --config benchmark_config.json \
  --render \
  --output results/
```

---

## 4. Export Formats

### 4.1 JSON Export

```json
{
  "test_run": {
    "id": "compute_128_cornell_esvo",
    "timestamp": "2025-12-06T10:30:00Z",
    "device": "NVIDIA GeForce RTX 3080"
  },
  "metrics": {
    "frame_count": 300,
    "warmup_frames": 10,
    "frame_times_ms": [0.32, 0.31, 0.33],
    "gpu_times_ms": [0.27, 0.28, 0.27],
    "mrays_per_sec": [1720, 1680, 1750]
  },
  "statistics": {
    "frame_time": {
      "min": 0.28, "max": 0.45, "mean": 0.32
    }
  }
}
```

### 4.2 CSV Export

```csv
frame,frame_time_ms,gpu_time_ms,mrays_per_sec
0,0.32,0.27,1720.5
1,0.31,0.28,1680.2
2,0.33,0.27,1750.8
```

---

## 5. Current Results

### 5.1 Compute Shader (Uncompressed)

| Resolution | GPU Time | Throughput |
|------------|----------|------------|
| 720x720 | 0.27-0.34 ms | 1,400-1,750 Mrays/sec |
| 1080x1080 | 0.45-0.55 ms | ~2,100 Mrays/sec |

### 5.2 Compute Shader (Compressed)

| Resolution | GPU Time | Throughput | Memory |
|------------|----------|------------|--------|
| 720x720 | Variable | 85-303 Mrays/sec | ~955 KB |

---

## 6. Code References

| Component | Location |
|-----------|----------|
| GPUTimestampQuery | `libraries/VulkanResources/include/GPUTimestampQuery.h` |
| GPUPerformanceLogger | `libraries/RenderGraph/include/Core/GPUPerformanceLogger.h` |
| BenchmarkRunner | `libraries/Profiler/src/BenchmarkRunner.cpp` |
| BenchmarkConfig | `libraries/Profiler/src/BenchmarkConfig.cpp` |
| MetricsExporter | `libraries/Profiler/src/MetricsExporter.cpp` |

---

## 7. Related Pages

- [[Overview]] - Library index
- [[../04-Development/Profiling|Profiling Guide]] - Detailed workflow
- [[VulkanResources]] - GPU timing queries
- [[RenderGraph]] - Performance logging integration
