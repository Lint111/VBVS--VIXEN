#pragma once

#include "Logger.h"
#include "GPUTimestampQuery.h"
#include <chrono>
#include <deque>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

/**
 * @brief GPU performance metrics logger with rolling statistics
 *
 * Tracks GPU dispatch timing, ray throughput (Mrays/sec), and pipeline statistics.
 * Maintains rolling averages for stable performance reporting.
 *
 * Extends the Logger hierarchy to integrate with existing node logging.
 *
 * Usage:
 * @code
 * auto gpuLogger = std::make_shared<GPUPerformanceLogger>("RayMarching", device);
 * nodeLogger->AddChild(gpuLogger);
 *
 * // During command buffer recording:
 * gpuLogger->RecordDispatchStart(cmdBuffer);
 * vkCmdDispatch(cmdBuffer, ...);
 * gpuLogger->RecordDispatchEnd(cmdBuffer, width, height);
 *
 * // After fence wait:
 * gpuLogger->CollectResults();  // Logs metrics automatically
 * @endcode
 */
class GPUPerformanceLogger : public Logger {
public:
    /**
     * @brief Construct GPU performance logger
     * @param name Logger name (will be suffixed with "_GPUPerf")
     * @param device Vulkan device for query pools
     * @param rollingWindowSize Number of frames for rolling average (default 60)
     */
    GPUPerformanceLogger(const std::string& name, VulkanDevice* device, size_t rollingWindowSize = 60);
    ~GPUPerformanceLogger() override = default;

    // ========================================================================
    // COMMAND BUFFER RECORDING
    // ========================================================================

    /**
     * @brief Reset queries at start of frame (call before recording dispatch)
     * @param cmdBuffer Command buffer to record reset into
     */
    void BeginFrame(VkCommandBuffer cmdBuffer);

    /**
     * @brief Record timestamp before compute dispatch
     * @param cmdBuffer Command buffer to record into
     */
    void RecordDispatchStart(VkCommandBuffer cmdBuffer);

    /**
     * @brief Record timestamp after compute dispatch
     * @param cmdBuffer Command buffer to record into
     * @param dispatchWidth Width of dispatch in pixels
     * @param dispatchHeight Height of dispatch in pixels
     */
    void RecordDispatchEnd(VkCommandBuffer cmdBuffer, uint32_t dispatchWidth, uint32_t dispatchHeight);

    // ========================================================================
    // RESULT COLLECTION (after fence wait)
    // ========================================================================

    /**
     * @brief Collect GPU results and update statistics
     *
     * Call after queue submission fence has been waited on.
     * Automatically logs performance metrics if enabled.
     */
    void CollectResults();

    // ========================================================================
    // PERFORMANCE METRICS
    // ========================================================================

    /**
     * @brief Get last frame's dispatch time in milliseconds
     */
    float GetLastDispatchMs() const { return lastDispatchMs_; }

    /**
     * @brief Get last frame's ray throughput in Mrays/sec
     */
    float GetLastMraysPerSec() const { return lastMraysPerSec_; }

    /**
     * @brief Get rolling average dispatch time in milliseconds
     */
    float GetAverageDispatchMs() const;

    /**
     * @brief Get rolling average ray throughput in Mrays/sec
     */
    float GetAverageMraysPerSec() const;

    /**
     * @brief Get minimum dispatch time in rolling window
     */
    float GetMinDispatchMs() const;

    /**
     * @brief Get maximum dispatch time in rolling window
     */
    float GetMaxDispatchMs() const;

    /**
     * @brief Get last frame's compute shader invocation count
     */
    uint64_t GetLastComputeInvocations() const { return lastComputeInvocations_; }

    /**
     * @brief Get formatted performance summary string
     */
    std::string GetPerformanceSummary() const;

    // ========================================================================
    // CONFIGURATION
    // ========================================================================

    /**
     * @brief Set how often to log performance (every N frames, 0 = never)
     */
    void SetLogFrequency(uint32_t frames) { logFrequency_ = frames; }

    /**
     * @brief Enable/disable terminal output of performance metrics
     */
    void SetPrintToTerminal(bool enable) { printToTerminal_ = enable; }

    /**
     * @brief Check if GPU timing is supported
     */
    bool IsTimingSupported() const { return query_ && query_->IsTimestampSupported(); }

private:
    std::unique_ptr<GPUTimestampQuery> query_;

    // Current frame data
    uint32_t currentWidth_ = 0;
    uint32_t currentHeight_ = 0;
    float lastDispatchMs_ = 0.0f;
    float lastMraysPerSec_ = 0.0f;
    uint64_t lastComputeInvocations_ = 0;

    // Rolling statistics
    std::deque<float> dispatchMsHistory_;
    std::deque<float> mraysHistory_;
    size_t rollingWindowSize_ = 60;

    // Logging control
    uint32_t logFrequency_ = 60;  // Log every 60 frames by default
    uint32_t frameCounter_ = 0;
    bool printToTerminal_ = true;
    bool hasRecordedTimestamps_ = false;  // Track if timestamps have been written to query pool

    void UpdateRollingStats();
};

} // namespace Vixen::RenderGraph
