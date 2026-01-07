#pragma once

#include "Logger.h"
#include "Core/GPUQueryManager.h"
#include <chrono>
#include <deque>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <memory>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

/**
 * @brief GPU performance metrics logger with per-frame timing
 *
 * Tracks GPU dispatch timing and ray throughput (Mrays/sec) using GPUQueryManager
 * for coordinated query pool access. Properly handles multiple frames-in-flight.
 *
 * Usage:
 * @code
 * // Create shared query manager (typically owned by RenderGraph or Application)
 * auto queryMgr = std::make_shared<GPUQueryManager>(device, 3, 8);
 *
 * // Create logger with manager
 * auto gpuLogger = std::make_shared<GPUPerformanceLogger>("RayMarching", queryMgr);
 * nodeLogger->AddChild(gpuLogger);
 *
 * // Each frame in ExecuteImpl:
 * uint32_t frameIdx = currentFrameIndex;
 *
 * // 1. Read previous frame's results (after fence wait)
 * gpuLogger->CollectResults(frameIdx);
 *
 * // 2. Record new queries in command buffer
 * gpuLogger->BeginFrame(cmdBuffer, frameIdx);
 * gpuLogger->RecordDispatchStart(cmdBuffer, frameIdx);
 * vkCmdDispatch(cmdBuffer, ...);
 * gpuLogger->RecordDispatchEnd(cmdBuffer, frameIdx, width, height);
 * @endcode
 */
class GPUPerformanceLogger : public Logger {
public:
    /**
     * @brief Construct GPU performance logger with GPUQueryManager
     * @param name Logger name (suffixed with "_GPUPerf")
     * @param queryManager Shared query manager for coordinated GPU queries
     * @param rollingWindowSize Number of frames for rolling average (default 60)
     */
    GPUPerformanceLogger(const std::string& name, std::shared_ptr<GPUQueryManager> queryManager,
                         size_t rollingWindowSize = 60);
    ~GPUPerformanceLogger() override = default;

    // ========================================================================
    // COMMAND BUFFER RECORDING (per-frame)
    // ========================================================================

    /**
     * @brief Reset queries for this frame (call at start of recording)
     */
    void BeginFrame(VkCommandBuffer cmdBuffer, uint32_t frameIndex);

    /**
     * @brief Record timestamp before dispatch
     */
    void RecordDispatchStart(VkCommandBuffer cmdBuffer, uint32_t frameIndex);

    /**
     * @brief Record timestamp after dispatch
     */
    void RecordDispatchEnd(VkCommandBuffer cmdBuffer, uint32_t frameIndex,
                           uint32_t dispatchWidth, uint32_t dispatchHeight);

    // ========================================================================
    // RESULT COLLECTION (per-frame, after fence wait)
    // ========================================================================

    /**
     * @brief Collect GPU results for this frame (call after fence wait)
     */
    void CollectResults(uint32_t frameIndex);

    // ========================================================================
    // PERFORMANCE METRICS
    // ========================================================================

    float GetLastDispatchMs() const { return lastDispatchMs_; }
    float GetLastMraysPerSec() const { return lastMraysPerSec_; }
    float GetAverageDispatchMs() const;
    float GetAverageMraysPerSec() const;
    float GetMinDispatchMs() const;
    float GetMaxDispatchMs() const;
    std::string GetPerformanceSummary() const;

    // ========================================================================
    // MEMORY TRACKING
    // ========================================================================

    /**
     * @brief Register a buffer allocation for memory tracking
     * @param name Buffer name for logging
     * @param sizeBytes Size in bytes
     */
    void RegisterBufferAllocation(const std::string& name, VkDeviceSize sizeBytes);

    /**
     * @brief Unregister a buffer (on deallocation)
     */
    void UnregisterBufferAllocation(const std::string& name);

    /**
     * @brief Get total tracked memory in bytes
     */
    VkDeviceSize GetTotalTrackedMemory() const { return totalTrackedMemory_; }

    /**
     * @brief Get total tracked memory in MB
     */
    float GetTotalTrackedMemoryMB() const { return static_cast<float>(totalTrackedMemory_) / (1024.0f * 1024.0f); }

    /**
     * @brief Get memory breakdown summary
     */
    std::string GetMemorySummary() const;

    // ========================================================================
    // CONFIGURATION
    // ========================================================================

    void SetLogFrequency(uint32_t frames) { logFrequency_ = frames; }
    void SetPrintToTerminal(bool enable) { printToTerminal_ = enable; }
    bool IsTimingSupported() const { return queryManager_ && queryManager_->IsTimestampSupported(); }

    /**
     * @brief Get the query slot handle allocated by this logger
     */
    GPUQueryManager::QuerySlotHandle GetQuerySlot() const { return querySlot_; }

private:
    std::shared_ptr<GPUQueryManager> queryManager_;
    GPUQueryManager::QuerySlotHandle querySlot_ = GPUQueryManager::INVALID_SLOT;

    // Per-frame dispatch dimensions (stored when RecordDispatchEnd called)
    struct FrameDispatchInfo {
        uint32_t width = 0;
        uint32_t height = 0;
    };
    std::vector<FrameDispatchInfo> frameDispatchInfo_;

    // Current frame data
    float lastDispatchMs_ = 0.0f;
    float lastMraysPerSec_ = 0.0f;

    // Rolling statistics
    std::deque<float> dispatchMsHistory_;
    std::deque<float> mraysHistory_;
    size_t rollingWindowSize_ = 60;

    // Logging control
    uint32_t logFrequency_ = 60;
    uint32_t frameCounter_ = 0;
    bool printToTerminal_ = true;

    // Memory tracking
    std::unordered_map<std::string, VkDeviceSize> bufferAllocations_;
    VkDeviceSize totalTrackedMemory_ = 0;

    void UpdateRollingStats();
};

} // namespace Vixen::RenderGraph
