// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

#include "GPUPerformanceLogger.h"
#include "PredictionErrorTracker.h"  // Sprint 6.3: Phase 3.1
#include "MessageBus.h"              // Sprint 6.3: Event-driven architecture
#include <cstdint>
#include <deque>
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Device-specific timeline tracking
 *
 * Tracks budget, measured time, and utilization for a single device
 * (GPU, CPU, or Transfer).
 */
struct DeviceTimeline {
    // Configuration (static per frame)
    uint64_t budgetNs = 0;              ///< Target frame time (e.g., 16.67ms)

    // Measurement (runtime, updated during frame)
    uint64_t measuredNs = 0;            ///< Actual time consumed
    uint64_t remainingNs = 0;           ///< budgetNs - measuredNs

    // Utilization (computed)
    float utilization = 0.0f;           ///< measuredNs / budgetNs (0.0-1.0+)
    bool exceededBudget = false;        ///< True if utilization > 1.0

    // Tracking
    uint32_t frameNumber = 0;
    uint32_t taskCount = 0;             ///< Tasks executed this frame

    /**
     * @brief Reset measurements for new frame
     *
     * Preserves budgetNs, clears measuredNs and computed values.
     */
    void Reset() {
        measuredNs = 0;
        remainingNs = budgetNs;
        utilization = 0.0f;
        exceededBudget = false;
        taskCount = 0;
    }

    /**
     * @brief Update computed values after measurements
     */
    void ComputeUtilization() {
        if (budgetNs > 0) {
            utilization = static_cast<float>(measuredNs) / static_cast<float>(budgetNs);
            exceededBudget = utilization > 1.0f;
            remainingNs = measuredNs < budgetNs ? (budgetNs - measuredNs) : 0;
        } else {
            utilization = 0.0f;
            exceededBudget = false;
            remainingNs = 0;
        }
    }
};

/**
 * @brief System-wide timeline capacity tracking
 *
 * Tracks multiple GPU queues, CPU threads, and transfer channels
 * for comprehensive multi-device performance monitoring.
 *
 * Architecture:
 * - gpuQueues[]: One timeline per GPU queue (graphics, compute, transfer, etc.)
 * - cpuThreads[]: One timeline per CPU thread/core
 * - Each DeviceTimeline tracks independent budget/measurement
 */
struct SystemTimeline {
    std::vector<DeviceTimeline> gpuQueues;      ///< GPU queues (graphics, compute, transfer)
    std::vector<DeviceTimeline> cpuThreads;     ///< CPU threads/cores

    uint32_t frameNumber = 0;

    /**
     * @brief Bottleneck identification
     */
    enum class Bottleneck {
        None,       ///< All devices under budget
        GPU,        ///< Any GPU queue at or exceeding budget
        CPU,        ///< Any CPU thread at or exceeding budget
        Unknown     ///< Unable to determine (no devices tracked)
    };

    /**
     * @brief Identify primary bottleneck across all devices
     *
     * Returns device type with highest utilization exceeding 90%.
     * If none exceed 90%, returns None.
     *
     * @return Bottleneck type
     */
    [[nodiscard]] Bottleneck GetBottleneck() const {
        constexpr float BOTTLENECK_THRESHOLD = 0.90f;

        float maxUtil = 0.0f;
        Bottleneck result = Bottleneck::None;

        // Check all GPU queues
        for (const auto& gpu : gpuQueues) {
            if (gpu.utilization > BOTTLENECK_THRESHOLD && gpu.utilization > maxUtil) {
                maxUtil = gpu.utilization;
                result = Bottleneck::GPU;
            }
        }

        // Check all CPU threads
        for (const auto& cpu : cpuThreads) {
            if (cpu.utilization > BOTTLENECK_THRESHOLD && cpu.utilization > maxUtil) {
                maxUtil = cpu.utilization;
                result = Bottleneck::CPU;
            }
        }

        return result;
    }

    /**
     * @brief Get maximum GPU utilization across all queues
     *
     * @return Highest GPU utilization (0.0-1.0+)
     */
    [[nodiscard]] float GetMaxGPUUtilization() const {
        float maxUtil = 0.0f;
        for (const auto& gpu : gpuQueues) {
            maxUtil = std::max(maxUtil, gpu.utilization);
        }
        return maxUtil;
    }

    /**
     * @brief Get maximum CPU utilization across all threads
     *
     * @return Highest CPU utilization (0.0-1.0+)
     */
    [[nodiscard]] float GetMaxCPUUtilization() const {
        float maxUtil = 0.0f;
        for (const auto& cpu : cpuThreads) {
            maxUtil = std::max(maxUtil, cpu.utilization);
        }
        return maxUtil;
    }

    /**
     * @brief Get aggregate GPU time (sum of all queues)
     *
     * @return Total GPU time in nanoseconds
     */
    [[nodiscard]] uint64_t GetTotalGPUTime() const {
        uint64_t total = 0;
        for (const auto& gpu : gpuQueues) {
            total += gpu.measuredNs;
        }
        return total;
    }

    /**
     * @brief Get aggregate CPU time (sum of all threads)
     *
     * @return Total CPU time in nanoseconds
     */
    [[nodiscard]] uint64_t GetTotalCPUTime() const {
        uint64_t total = 0;
        for (const auto& cpu : cpuThreads) {
            total += cpu.measuredNs;
        }
        return total;
    }

    /**
     * @brief Reset all device timelines
     */
    void Reset() {
        for (auto& gpu : gpuQueues) {
            gpu.Reset();
        }
        for (auto& cpu : cpuThreads) {
            cpu.Reset();
        }
    }

    /**
     * @brief Update all device utilizations
     */
    void ComputeUtilizations() {
        for (auto& gpu : gpuQueues) {
            gpu.ComputeUtilization();
        }
        for (auto& cpu : cpuThreads) {
            cpu.ComputeUtilization();
        }
    }
};

/**
 * @brief Runtime performance tracker with adaptive scheduling
 *
 * Sprint 6.3: Timeline Capacity Tracker
 * Design Element: #38 Timeline Capacity Tracker
 *
 * Bridges budget planning (estimates) with runtime execution (measurements).
 * Provides feedback loop for adaptive scheduling and predictive estimation.
 *
 * Key Features:
 * - Measures actual GPU/CPU time via GPUPerformanceLogger + profiler
 * - Tracks utilization (% of frame budget used)
 * - Learns from measurements to improve future estimates
 * - Suggests additional tasks when capacity available
 * - Identifies bottlenecks (GPU vs. CPU)
 *
 * ## GPU Query Frame Synchronization
 *
 * **IMPORTANT**: GPU timestamp queries have inherent latency.
 *
 * Query timing architecture:
 * ```
 * Frame N:   [GPU commands] → [Write timestamps to query pool]
 * Frame N+1: [Query results become available]
 * Frame N+2: [Read results via vkGetQueryPoolResults]
 * ```
 *
 * TimelineCapacityTracker handles this via two patterns:
 *
 * 1. **Immediate measurement** (RecordGPUTime/RecordCPUTime):
 *    - Node passes measured time directly after synchronization point
 *    - Assumes caller has waited for GPU completion (e.g., after vkQueueWaitIdle)
 *    - Use when precise per-frame timing is critical
 *
 * 2. **Deferred measurement** (via GPUPerformanceLogger):
 *    - Timestamps written to query pool during execution
 *    - Results read N frames later when available
 *    - Utilization reflects N-frame-delayed measurements
 *    - Better for trend-based adaptive scheduling (smooths variance)
 *
 * Caller responsibilities:
 * - Ensure vkGetQueryPoolResults reports VK_SUCCESS before reading
 * - Call RecordGPUTime with actual nanoseconds (not query indices)
 * - Accept that utilization reflects delayed measurements (ok for adaptation)
 *
 * @see TaskQueue for budget-aware task scheduling (estimates)
 * @see DeviceBudgetManager for memory budget tracking
 * @see GPUPerformanceLogger for GPU time measurement
 */
class TimelineCapacityTracker {
public:
    /**
     * @brief Configuration for capacity tracking
     */
    struct Config {
        // Device topology
        uint32_t numGPUQueues = 1;                  ///< Number of GPU queues to track (graphics, compute, transfer)
        uint32_t numCPUThreads = 1;                 ///< Number of CPU threads/cores to track

        // Per-device budgets (applied to each GPU queue / CPU thread)
        uint64_t gpuTimeBudgetNs = 16'666'666;      ///< 60 FPS target (16.67ms)
        uint64_t cpuTimeBudgetNs = 8'000'000;       ///< Half frame for CPU (8ms)

        // History tracking
        uint32_t historyDepth = 60;                 ///< Frames to track (default 60)
        uint32_t maxHistoryDepth = 300;             ///< Max history cap (300 frames = 90KB)

        // Adaptive scheduling parameters
        float adaptiveThreshold = 0.90f;            ///< Add work if < 90% utilized
        bool enableAdaptiveScheduling = true;       ///< Auto-adjust task count

        // Damped hysteresis parameters (Phase 1.4)
        float hysteresisDamping = 0.10f;            ///< Max ±10% change per frame
        float hysteresisDeadband = 0.05f;           ///< ±5% deadband prevents micro-adjustments
    };

    /**
     * @brief Construct with configuration
     *
     * @param config Capacity tracking configuration
     */
    explicit TimelineCapacityTracker(const Config& config = Config{});

    /**
     * @brief Destructor
     */
    ~TimelineCapacityTracker() = default;

    // Non-copyable, movable
    TimelineCapacityTracker(const TimelineCapacityTracker&) = delete;
    TimelineCapacityTracker& operator=(const TimelineCapacityTracker&) = delete;
    TimelineCapacityTracker(TimelineCapacityTracker&&) noexcept = default;
    TimelineCapacityTracker& operator=(TimelineCapacityTracker&&) noexcept = default;

    // =========================================================================
    // Frame Lifecycle
    // =========================================================================

    /**
     * @brief Begin new frame, reset measurements
     *
     * Call at the start of RenderGraph execution (PreExecute hook).
     * Resets current frame measurements while preserving budgets.
     */
    void BeginFrame();

    /**
     * @brief End frame, compute utilization and store in history
     *
     * Call at the end of RenderGraph execution (PostExecute hook).
     * Computes utilization, updates history, advances frame counter.
     */
    void EndFrame();

    // =========================================================================
    // Measurement Recording
    // =========================================================================

    /**
     * @brief Record actual GPU time for a specific queue
     *
     * Called after task execution with measured time from GPUPerformanceLogger.
     * Accumulates into currentFrame_.gpuQueues[queueIndex].measuredNs.
     *
     * @param queueIndex GPU queue index (0 = graphics, 1 = compute, etc.)
     * @param nanoseconds Measured GPU time
     */
    void RecordGPUTime(uint32_t queueIndex, uint64_t nanoseconds);

    /**
     * @brief Record actual GPU time (single-device convenience)
     *
     * Equivalent to RecordGPUTime(0, nanoseconds).
     * For backward compatibility and simple single-GPU usage.
     *
     * @param nanoseconds Measured GPU time
     */
    void RecordGPUTime(uint64_t nanoseconds) {
        RecordGPUTime(0, nanoseconds);
    }

    /**
     * @brief Record actual CPU time for a specific thread
     *
     * Called after task execution with measured time from Profiler.
     * Accumulates into currentFrame_.cpuThreads[threadIndex].measuredNs.
     *
     * @param threadIndex CPU thread index
     * @param nanoseconds Measured CPU time
     */
    void RecordCPUTime(uint32_t threadIndex, uint64_t nanoseconds);

    /**
     * @brief Record actual CPU time (single-thread convenience)
     *
     * Equivalent to RecordCPUTime(0, nanoseconds).
     * For backward compatibility and simple single-thread usage.
     *
     * @param nanoseconds Measured CPU time
     */
    void RecordCPUTime(uint64_t nanoseconds) {
        RecordCPUTime(0, nanoseconds);
    }

    // =========================================================================
    // Capacity Queries
    // =========================================================================

    /**
     * @brief Get current frame timeline state
     *
     * @return Reference to current SystemTimeline
     */
    [[nodiscard]] const SystemTimeline& GetCurrentTimeline() const {
        return currentFrame_;
    }

    /**
     * @brief Get remaining GPU budget for specific queue
     *
     * @param queueIndex GPU queue index
     * @return Nanoseconds remaining (0 if over budget or invalid index)
     */
    [[nodiscard]] uint64_t GetGPURemainingBudget(uint32_t queueIndex) const {
        if (queueIndex >= currentFrame_.gpuQueues.size()) {
            return 0;
        }
        return currentFrame_.gpuQueues[queueIndex].remainingNs;
    }

    /**
     * @brief Get remaining GPU budget (single-device convenience)
     *
     * Equivalent to GetGPURemainingBudget(0).
     *
     * @return Nanoseconds remaining (0 if over budget)
     */
    [[nodiscard]] uint64_t GetGPURemainingBudget() const {
        return GetGPURemainingBudget(0);
    }

    /**
     * @brief Get minimum remaining GPU budget across all queues
     *
     * Returns the smallest remaining budget (most constrained queue).
     *
     * @return Nanoseconds remaining (0 if any queue over budget)
     */
    [[nodiscard]] uint64_t GetMinGPURemainingBudget() const {
        uint64_t minRemaining = UINT64_MAX;
        for (const auto& gpu : currentFrame_.gpuQueues) {
            minRemaining = std::min(minRemaining, gpu.remainingNs);
        }
        return (minRemaining == UINT64_MAX) ? 0 : minRemaining;
    }

    /**
     * @brief Get remaining CPU budget for specific thread
     *
     * @param threadIndex CPU thread index
     * @return Nanoseconds remaining (0 if over budget or invalid index)
     */
    [[nodiscard]] uint64_t GetCPURemainingBudget(uint32_t threadIndex) const {
        if (threadIndex >= currentFrame_.cpuThreads.size()) {
            return 0;
        }
        return currentFrame_.cpuThreads[threadIndex].remainingNs;
    }

    /**
     * @brief Get remaining CPU budget (single-thread convenience)
     *
     * Equivalent to GetCPURemainingBudget(0).
     *
     * @return Nanoseconds remaining (0 if over budget)
     */
    [[nodiscard]] uint64_t GetCPURemainingBudget() const {
        return GetCPURemainingBudget(0);
    }

    /**
     * @brief Get minimum remaining CPU budget across all threads
     *
     * Returns the smallest remaining budget (most constrained thread).
     *
     * @return Nanoseconds remaining (0 if any thread over budget)
     */
    [[nodiscard]] uint64_t GetMinCPURemainingBudget() const {
        uint64_t minRemaining = UINT64_MAX;
        for (const auto& cpu : currentFrame_.cpuThreads) {
            minRemaining = std::min(minRemaining, cpu.remainingNs);
        }
        return (minRemaining == UINT64_MAX) ? 0 : minRemaining;
    }

    /**
     * @brief Check if system can schedule more work
     *
     * Returns true if maximum GPU utilization < adaptiveThreshold (default 90%).
     * Checks the most constrained GPU queue.
     *
     * @return true if utilization below adaptive threshold
     */
    [[nodiscard]] bool CanScheduleMoreWork() const {
        return currentFrame_.GetMaxGPUUtilization() < config_.adaptiveThreshold;
    }

    /**
     * @brief Check if system is over budget
     *
     * Returns true if any GPU queue or CPU thread utilization > 100%.
     *
     * @return true if any device exceeded budget
     */
    [[nodiscard]] bool IsOverBudget() const {
        for (const auto& gpu : currentFrame_.gpuQueues) {
            if (gpu.exceededBudget) return true;
        }
        for (const auto& cpu : currentFrame_.cpuThreads) {
            if (cpu.exceededBudget) return true;
        }
        return false;
    }

    // =========================================================================
    // Adaptive Scheduling (Phase 1.4: Damped Hysteresis)
    // =========================================================================

    /**
     * @brief Suggest number of additional tasks to schedule
     *
     * Uses remaining budget and task cost estimate to compute how many
     * additional tasks can fit in the current frame.
     *
     * @param estimatedCostPerTaskNs Average cost per task
     * @return Number of additional tasks to schedule (0 if none)
     */
    [[nodiscard]] uint32_t SuggestAdditionalTasks(
        uint64_t estimatedCostPerTaskNs
    ) const;

    /**
     * @brief Compute scale factor for next frame's task count
     *
     * Uses damped hysteresis to prevent oscillation:
     * - Deadband (±5%): No change if within band
     * - Proportional: Scale based on utilization delta
     * - Clamped: Max ±10% change per frame
     *
     * Example:
     * - 70% util → return 1.10 (increase by 10%)
     * - 92% util → return 1.00 (within deadband)
     * - 110% util → return 0.90 (decrease by 10%)
     *
     * @return Scale factor for task count (0.90 - 1.10 range)
     */
    [[nodiscard]] float ComputeTaskCountScale() const;

    // =========================================================================
    // Historical Statistics
    // =========================================================================

    /**
     * @brief Get average GPU utilization over recent frames
     *
     * @param frameCount Number of frames to average (default 60)
     * @return Average utilization (0.0-1.0+)
     */
    [[nodiscard]] float GetAverageGPUUtilization(uint32_t frameCount = 60) const;

    /**
     * @brief Get average CPU utilization over recent frames
     *
     * @param frameCount Number of frames to average (default 60)
     * @return Average utilization (0.0-1.0+)
     */
    [[nodiscard]] float GetAverageCPUUtilization(uint32_t frameCount = 60) const;

    /**
     * @brief Get frame history for visualization/analysis
     *
     * @return Reference to history deque (most recent frame at back)
     */
    [[nodiscard]] const std::deque<SystemTimeline>& GetHistory() const {
        return history_;
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Update GPU budget target (applies to all queues)
     *
     * @param nanoseconds New budget (e.g., 16'666'666 for 60 FPS)
     */
    void SetGPUBudget(uint64_t nanoseconds) {
        config_.gpuTimeBudgetNs = nanoseconds;
        for (auto& gpu : currentFrame_.gpuQueues) {
            gpu.budgetNs = nanoseconds;
        }
    }

    /**
     * @brief Update CPU budget target (applies to all threads)
     *
     * @param nanoseconds New budget (e.g., 8'000'000 for 8ms)
     */
    void SetCPUBudget(uint64_t nanoseconds) {
        config_.cpuTimeBudgetNs = nanoseconds;
        for (auto& cpu : currentFrame_.cpuThreads) {
            cpu.budgetNs = nanoseconds;
        }
    }

    /**
     * @brief Update GPU budget for specific queue
     *
     * @param queueIndex GPU queue index
     * @param nanoseconds New budget
     */
    void SetGPUBudget(uint32_t queueIndex, uint64_t nanoseconds) {
        if (queueIndex < currentFrame_.gpuQueues.size()) {
            currentFrame_.gpuQueues[queueIndex].budgetNs = nanoseconds;
        }
    }

    /**
     * @brief Update CPU budget for specific thread
     *
     * @param threadIndex CPU thread index
     * @param nanoseconds New budget
     */
    void SetCPUBudget(uint32_t threadIndex, uint64_t nanoseconds) {
        if (threadIndex < currentFrame_.cpuThreads.size()) {
            currentFrame_.cpuThreads[threadIndex].budgetNs = nanoseconds;
        }
    }

    /**
     * @brief Enable/disable adaptive scheduling
     *
     * @param enabled true to enable adaptive task count scaling
     */
    void SetAdaptiveScheduling(bool enabled) {
        config_.enableAdaptiveScheduling = enabled;
    }

    /**
     * @brief Get current configuration
     *
     * @return Reference to current config
     */
    [[nodiscard]] const Config& GetConfig() const {
        return config_;
    }

    // =========================================================================
    // GPUPerformanceLogger Access (Composition)
    // =========================================================================

    /**
     * @brief Get GPU performance logger for direct timing access
     *
     * Provides access to composed GPUPerformanceLogger for nodes
     * that need detailed GPU timing statistics.
     *
     * @return Pointer to GPUPerformanceLogger (nullptr if not initialized)
     */
    [[nodiscard]] GPUPerformanceLogger* GetGPUPerformanceLogger() const {
        return gpuPerfLogger_.get();
    }

    /**
     * @brief Set GPU performance logger (composition)
     *
     * Called during initialization to inject GPUPerformanceLogger.
     * TimelineCapacityTracker delegates GPU timing to this logger.
     *
     * @param logger Shared pointer to GPUPerformanceLogger
     */
    void SetGPUPerformanceLogger(std::shared_ptr<GPUPerformanceLogger> logger) {
        gpuPerfLogger_ = std::move(logger);
    }

    // =========================================================================
    // Prediction Error Tracking (Phase 3.1)
    // =========================================================================

    /**
     * @brief Record a prediction result for error tracking
     *
     * Call after measuring actual execution time to track prediction accuracy.
     * Enables adaptive estimate correction in Phase 3.2.
     *
     * @param taskId Task type identifier (e.g., "shadowMap")
     * @param estimatedNs Original estimate in nanoseconds
     * @param actualNs Measured actual time in nanoseconds
     */
    void RecordPrediction(
        const std::string& taskId,
        uint64_t estimatedNs,
        uint64_t actualNs
    ) {
        predictionTracker_.RecordPrediction(taskId, estimatedNs, actualNs, frameCounter_);
    }

    /**
     * @brief Get correction factor for a task type's estimates
     *
     * Returns a multiplier to improve future estimates based on past accuracy.
     *
     * @param taskId Task type identifier
     * @return Correction multiplier (1.0 if no data)
     */
    [[nodiscard]] float GetCorrectionFactor(const std::string& taskId) const {
        return predictionTracker_.GetCorrectionFactor(taskId);
    }

    /**
     * @brief Apply correction factor to an estimate
     *
     * Convenience method that applies learned correction to an estimate.
     *
     * @param taskId Task type identifier
     * @param estimatedNs Original estimate in nanoseconds
     * @return Corrected estimate in nanoseconds
     */
    [[nodiscard]] uint64_t GetCorrectedEstimate(
        const std::string& taskId,
        uint64_t estimatedNs
    ) const {
        float correction = predictionTracker_.GetCorrectionFactor(taskId);
        return static_cast<uint64_t>(static_cast<float>(estimatedNs) * correction);
    }

    /**
     * @brief Get prediction error statistics for a task type
     *
     * @param taskId Task type identifier
     * @return Pointer to stats (nullptr if not tracked)
     */
    [[nodiscard]] const TaskPredictionStats* GetPredictionStats(
        const std::string& taskId
    ) const {
        return predictionTracker_.GetTaskStats(taskId);
    }

    /**
     * @brief Get global prediction error statistics
     */
    [[nodiscard]] GlobalPredictionStats GetGlobalPredictionStats() const {
        return predictionTracker_.GetGlobalStats();
    }

    /**
     * @brief Get direct access to prediction error tracker
     *
     * For advanced queries and configuration.
     */
    [[nodiscard]] PredictionErrorTracker& GetPredictionTracker() {
        return predictionTracker_;
    }

    [[nodiscard]] const PredictionErrorTracker& GetPredictionTracker() const {
        return predictionTracker_;
    }

    // =========================================================================
    // Event-Driven Architecture (Sprint 6.3)
    // =========================================================================

    /**
     * @brief Subscribe to frame events via MessageBus
     *
     * Enables self-managed frame lifecycle. When subscribed:
     * - FrameStartEvent → calls BeginFrame()
     * - FrameEndEvent → calls EndFrame() and publishes budget events
     *
     * @param messageBus MessageBus to subscribe to (non-owning)
     */
    void SubscribeToFrameEvents(EventBus::MessageBus* messageBus);

    /**
     * @brief Unsubscribe from frame events
     *
     * Note: Also happens automatically via RAII when object is destroyed.
     */
    void UnsubscribeFromFrameEvents() { subscriptions_.UnsubscribeAll(); }

    /**
     * @brief Check if subscribed to frame events
     */
    [[nodiscard]] bool IsSubscribed() const { return subscriptions_.HasSubscriptions(); }

    /**
     * @brief Get MessageBus (for publishing budget events)
     */
    [[nodiscard]] EventBus::MessageBus* GetMessageBus() const { return subscriptions_.GetBus(); }

private:
    Config config_;
    SystemTimeline currentFrame_;
    std::deque<SystemTimeline> history_;
    uint32_t frameCounter_ = 0;

    // Composition: Delegate GPU timing to GPUPerformanceLogger
    std::shared_ptr<GPUPerformanceLogger> gpuPerfLogger_;

    // Sprint 6.3: Phase 3.1 - Prediction error tracking
    PredictionErrorTracker predictionTracker_;

    // Sprint 6.3: Event-driven architecture (RAII subscriptions)
    EventBus::ScopedSubscriptions subscriptions_;

    // Internal helpers
    [[nodiscard]] float ComputeAverage(
        const std::deque<SystemTimeline>& data,
        uint32_t count,
        bool useGPU
    ) const;

    // Publish budget events based on current utilization
    void PublishBudgetEvents();
};

} // namespace Vixen::RenderGraph
