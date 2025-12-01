# GPU Performance System

**Location**: `libraries/VulkanResources/` and `libraries/RenderGraph/`
**Status**: Complete (Week 2)

## Overview

The GPU performance system provides accurate timing and throughput measurement for compute shader dispatches. It handles multiple frames-in-flight correctly by maintaining per-frame query pools.

## Components

### 1. GPUTimestampQuery

**Header**: `libraries/VulkanResources/include/GPUTimestampQuery.h`
**Namespace**: `Vixen::Vulkan::Resources`

Low-level GPU timestamp query manager with per-frame query pools.

```cpp
class GPUTimestampQuery {
public:
    GPUTimestampQuery(VulkanDevice* device, uint32_t framesInFlight, uint32_t maxTimestamps = 4);

    // Query support
    bool IsTimestampSupported() const;
    float GetTimestampPeriod() const;  // Nanoseconds per tick

    // Command buffer recording (per-frame)
    void ResetQueries(VkCommandBuffer cmdBuffer, uint32_t frameIndex);
    void WriteTimestamp(VkCommandBuffer cmdBuffer, uint32_t frameIndex,
                        VkPipelineStageFlagBits pipelineStage, uint32_t queryIndex);

    // Result retrieval (after fence wait)
    bool ReadResults(uint32_t frameIndex);
    float GetElapsedMs(uint32_t frameIndex, uint32_t startQuery, uint32_t endQuery) const;
    uint64_t GetElapsedNs(uint32_t frameIndex, uint32_t startQuery, uint32_t endQuery) const;
    float CalculateMraysPerSec(uint32_t frameIndex, uint32_t startQuery, uint32_t endQuery,
                               uint32_t width, uint32_t height) const;
};
```

**Key Design**:
- Separate `VkQueryPool` per frame-in-flight (avoids read/write conflicts)
- Results from frame N-1 read while recording frame N
- Handles GPU timestamp period for cross-vendor timing

### 2. GPUPerformanceLogger

**Header**: `libraries/RenderGraph/include/Core/GPUPerformanceLogger.h`
**Namespace**: `Vixen::RenderGraph`

High-level performance logger that integrates with the node logging system.

```cpp
class GPUPerformanceLogger : public Logger {
public:
    GPUPerformanceLogger(const std::string& name, VulkanDevice* device,
                         uint32_t framesInFlight = 3, size_t rollingWindowSize = 60);

    // Command buffer recording (per-frame)
    void BeginFrame(VkCommandBuffer cmdBuffer, uint32_t frameIndex);
    void RecordDispatchStart(VkCommandBuffer cmdBuffer, uint32_t frameIndex);
    void RecordDispatchEnd(VkCommandBuffer cmdBuffer, uint32_t frameIndex,
                           uint32_t dispatchWidth, uint32_t dispatchHeight);

    // Result collection (after fence wait)
    void CollectResults(uint32_t frameIndex);

    // Performance metrics
    float GetLastDispatchMs() const;
    float GetLastMraysPerSec() const;
    float GetAverageDispatchMs() const;
    float GetAverageMraysPerSec() const;
    std::string GetPerformanceSummary() const;

    // Configuration
    void SetLogFrequency(uint32_t frames);   // Log every N frames
    void SetPrintToTerminal(bool enable);
};
```

**Features**:
- Rolling average over configurable window (default 60 frames)
- Min/max tracking for performance variability analysis
- Automatic Mrays/sec calculation from dispatch dimensions
- Integrates with node Logger hierarchy

## Usage Pattern

### In VoxelGridNode (or similar compute node)

```cpp
// Construction (in node constructor or CompileImpl)
gpuLogger_ = std::make_shared<GPUPerformanceLogger>("VoxelRayMarch", device, framesInFlight);
nodeLogger_->AddChild(gpuLogger_);

// Each frame in ExecuteImpl:
void VoxelGridNode::ExecuteImpl(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    // 1. Collect previous frame's results (after fence wait)
    gpuLogger_->CollectResults(frameIndex);

    // 2. Begin new frame's timing
    gpuLogger_->BeginFrame(cmdBuffer, frameIndex);

    // 3. Record dispatch timing
    gpuLogger_->RecordDispatchStart(cmdBuffer, frameIndex);

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(...);
    vkCmdDispatch(cmdBuffer, groupCountX, groupCountY, 1);

    gpuLogger_->RecordDispatchEnd(cmdBuffer, frameIndex,
                                   groupCountX * 8, groupCountY * 8);  // Total pixels
}
```

### Frame Synchronization

The per-frame query pool design ensures:
1. Frame N-1 results are read after its fence signals
2. Frame N queries are recorded into a separate pool
3. No pipeline stalls waiting for query results

```
Frame 0: Record queries to pool[0]
Frame 1: Read pool[0] results, record to pool[1]
Frame 2: Read pool[1] results, record to pool[2]
Frame 3: Read pool[2] results, record to pool[0] (wraps)
```

## Pipeline Stages

For compute shaders, use these pipeline stages:

```cpp
// Before dispatch
WriteTimestamp(cmdBuffer, frameIdx, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);

// After dispatch
WriteTimestamp(cmdBuffer, frameIdx, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 1);
```

Alternative (broader timing):
```cpp
// TOP_OF_PIPE - after command buffer start
// BOTTOM_OF_PIPE - after all work complete
```

## Output Format

GPUPerformanceLogger outputs metrics like:
```
[VoxelRayMarch_GPUPerf] Dispatch: 2.34ms (1,723 Mrays/sec) | Avg: 2.28ms (1,758 Mrays/sec)
```

## Limitations

1. **Timestamp period varies by GPU**: AMD/NVIDIA/Intel have different tick rates
2. **Query pool count**: Limited per device (check `VkPhysicalDeviceLimits::timestampComputeAndGraphics`)
3. **Precision**: Some mobile GPUs don't support timestamp queries at all

## References

- [Vulkan Timestamp Queries](https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#queries-timestamps)
- `VkPhysicalDeviceLimits::timestampPeriod` - nanoseconds per tick
- `VkPhysicalDeviceLimits::timestampComputeAndGraphics` - compute support
