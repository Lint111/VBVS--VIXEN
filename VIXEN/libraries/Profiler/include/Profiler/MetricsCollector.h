#pragma once

#include "FrameMetrics.h"
#include "RollingStats.h"
#include <vulkan/vulkan.h>
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <memory>

namespace Vixen::Profiler {

/// Callback type for extracting metrics from nodes before cleanup
using NodeMetricsExtractor = std::function<void(FrameMetrics&)>;

/// Collects per-frame metrics via hooks and registered extractors
/// Integrates with GraphLifecycleHooks for timing measurements
class MetricsCollector {
public:
    MetricsCollector();
    ~MetricsCollector();

    /// Initialize the collector with Vulkan device for GPU timing
    void Initialize(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t framesInFlight = 3);

    /// Shutdown and release resources
    void Shutdown();

    /// Register a node metrics extractor (called before graph cleanup)
    /// @param name Unique identifier for the extractor
    /// @param extractor Function that populates FrameMetrics from node state
    void RegisterExtractor(const std::string& name, NodeMetricsExtractor extractor);

    /// Unregister a previously registered extractor
    void UnregisterExtractor(const std::string& name);

    // ========================================================================
    // Frame lifecycle hooks (call from GraphLifecycleHooks)
    // ========================================================================

    /// Called at start of frame (PreExecute hook)
    void OnFrameBegin(VkCommandBuffer cmdBuffer, uint32_t frameIndex);

    /// Called before compute dispatch
    void OnDispatchBegin(VkCommandBuffer cmdBuffer, uint32_t frameIndex);

    /// Called after compute dispatch with dimensions
    void OnDispatchEnd(VkCommandBuffer cmdBuffer, uint32_t frameIndex,
                       uint32_t dispatchWidth, uint32_t dispatchHeight);

    /// Called at end of frame (PostExecute hook)
    void OnFrameEnd(uint32_t frameIndex);

    /// Called before graph cleanup to extract node metrics
    void OnPreCleanup();

    // ========================================================================
    // Results access
    // ========================================================================

    /// Get metrics for the most recent completed frame
    const FrameMetrics& GetLastFrameMetrics() const { return lastFrameMetrics_; }

    /// Get rolling statistics for a specific metric
    /// Valid names: "frame_time", "gpu_time", "mrays", "fps"
    const RollingStats* GetRollingStats(const std::string& metricName) const;

    /// Get all rolling stats as map
    const std::map<std::string, RollingStats>& GetAllRollingStats() const { return rollingStats_; }

    /// Get total frames collected (including warmup)
    uint64_t GetTotalFramesCollected() const { return totalFramesCollected_; }

    /// Reset all collected data
    void Reset();

    /// Set warmup frames (frames to skip before collecting statistics)
    void SetWarmupFrames(uint32_t frames) { warmupFrames_ = frames; }

    /// Check if still in warmup period
    bool IsWarmingUp() const { return totalFramesCollected_ < warmupFrames_; }

private:
    struct PerFrameData;
    std::unique_ptr<PerFrameData[]> frameData_;
    uint32_t framesInFlight_ = 0;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkQueryPool queryPool_ = VK_NULL_HANDLE;
    float timestampPeriod_ = 1.0f;

    std::map<std::string, NodeMetricsExtractor> extractors_;
    std::map<std::string, RollingStats> rollingStats_;

    FrameMetrics lastFrameMetrics_;
    uint64_t totalFramesCollected_ = 0;
    uint32_t warmupFrames_ = 60;

    std::chrono::high_resolution_clock::time_point profilingStartTime_;
    std::chrono::high_resolution_clock::time_point frameStartTime_;

    void CollectGPUResults(uint32_t frameIndex);
    void UpdateRollingStats(const FrameMetrics& metrics);
};

} // namespace Vixen::Profiler
