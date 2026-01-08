// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file PredictionErrorTracker.h
 * @brief Tracks prediction errors between estimated and actual task costs
 *
 * Sprint 6.3: Phase 3.1 - Prediction Error Tracking
 * Design Element: #38 Timeline Capacity Tracker
 *
 * Provides feedback loop for adaptive estimate correction by tracking:
 * - Per-task-type prediction errors (estimate vs actual)
 * - Rolling statistics (mean error, variance, bias)
 * - Correction factors for future estimates
 *
 * @see TimelineCapacityTracker for budget management
 * @see TaskQueue for task scheduling
 */

#include <cstdint>
#include <string>
#include <unordered_map>
#include <deque>
#include <cmath>
#include <algorithm>

namespace Vixen::RenderGraph {

/**
 * @brief Single prediction error record
 *
 * Captures one estimate-vs-actual measurement for analysis.
 */
struct PredictionError {
    uint64_t estimatedNs = 0;      ///< Original estimate in nanoseconds
    uint64_t actualNs = 0;         ///< Measured actual time in nanoseconds
    int64_t errorNs = 0;           ///< Signed error: actual - estimated (positive = underestimate)
    float errorRatio = 0.0f;       ///< Error as ratio: actual / estimated (1.0 = perfect)
    uint32_t frameNumber = 0;      ///< Frame when this error was recorded

    /**
     * @brief Compute error values from estimate and actual
     */
    void Compute() {
        errorNs = static_cast<int64_t>(actualNs) - static_cast<int64_t>(estimatedNs);
        if (estimatedNs > 0) {
            errorRatio = static_cast<float>(actualNs) / static_cast<float>(estimatedNs);
        } else {
            errorRatio = (actualNs > 0) ? 10.0f : 1.0f;  // Handle zero estimate
        }
    }
};

/**
 * @brief Rolling statistics for a task type's prediction accuracy
 *
 * Uses Welford's online algorithm for numerically stable variance computation.
 * Maintains bounded history for memory efficiency.
 */
struct TaskPredictionStats {
    // Identification
    std::string taskId;            ///< Task type identifier (e.g., "shadowMap", "postProcess")

    // Sample tracking
    uint32_t sampleCount = 0;      ///< Total samples recorded
    uint32_t windowSize = 60;      ///< Rolling window size (default: 60 frames)

    // Rolling statistics (computed over window)
    float meanErrorRatio = 1.0f;   ///< Mean error ratio (actual/estimated), 1.0 = perfect
    float varianceRatio = 0.0f;    ///< Variance in error ratio
    float stdDevRatio = 0.0f;      ///< Standard deviation of error ratio

    // Bias indicators
    float biasDirection = 0.0f;    ///< Signed bias: >0 = underestimate, <0 = overestimate
    float biasConfidence = 0.0f;   ///< Confidence in bias (0-1), higher = more consistent

    // Correction factor
    float correctionFactor = 1.0f; ///< Suggested multiplier for estimates (smoothed)

    // Absolute error stats (nanoseconds)
    int64_t meanErrorNs = 0;       ///< Mean signed error in nanoseconds
    uint64_t meanAbsErrorNs = 0;   ///< Mean absolute error in nanoseconds

    // Recent history (bounded)
    std::deque<PredictionError> history;

    /**
     * @brief Add new prediction error and update statistics
     */
    void AddSample(const PredictionError& error) {
        history.push_back(error);
        ++sampleCount;

        // Trim history to window size
        while (history.size() > windowSize) {
            history.pop_front();
        }

        // Recompute statistics from history
        RecomputeStats();
    }

    /**
     * @brief Recompute all statistics from history
     *
     * Uses Welford's algorithm for stable variance computation.
     */
    void RecomputeStats() {
        if (history.empty()) {
            ResetStats();
            return;
        }

        // Compute mean error ratio using Welford's algorithm
        double mean = 0.0;
        double m2 = 0.0;
        int64_t totalErrorNs = 0;
        uint64_t totalAbsErrorNs = 0;
        size_t n = 0;

        for (const auto& e : history) {
            ++n;
            double ratio = static_cast<double>(e.errorRatio);
            double delta = ratio - mean;
            mean += delta / static_cast<double>(n);
            double delta2 = ratio - mean;
            m2 += delta * delta2;

            totalErrorNs += e.errorNs;
            totalAbsErrorNs += static_cast<uint64_t>(std::abs(e.errorNs));
        }

        meanErrorRatio = static_cast<float>(mean);
        varianceRatio = (n > 1) ? static_cast<float>(m2 / static_cast<double>(n - 1)) : 0.0f;
        stdDevRatio = std::sqrt(varianceRatio);

        // Absolute error stats
        meanErrorNs = static_cast<int64_t>(totalErrorNs / static_cast<int64_t>(n));
        meanAbsErrorNs = totalAbsErrorNs / static_cast<uint64_t>(n);

        // Bias direction: >0 means underestimate (actual > estimate)
        biasDirection = meanErrorRatio - 1.0f;

        // Bias confidence: high if variance is low and we have enough samples
        // confidence = 1.0 when stdDev is 0 (perfect consistency)
        // confidence = signalToNoise / 3.0 otherwise, clamped to [0, 1]
        if (std::abs(biasDirection) > 0.001f) {
            if (stdDevRatio < 0.001f) {
                // Perfect consistency (zero variance) = maximum confidence
                biasConfidence = 1.0f;
            } else {
                float signalToNoise = std::abs(biasDirection) / stdDevRatio;
                biasConfidence = std::clamp(signalToNoise / 3.0f, 0.0f, 1.0f);
            }
        } else {
            // No significant bias = no confidence needed
            biasConfidence = 0.0f;
        }

        // Correction factor: smoothed inverse of mean error ratio
        // If we underestimate (ratio > 1), correction should increase estimates
        // Smoothed to prevent overcorrection: lerp toward target with 0.1 rate
        float targetCorrection = meanErrorRatio;
        correctionFactor = correctionFactor * 0.9f + targetCorrection * 0.1f;

        // Clamp correction to reasonable bounds [0.5, 2.0]
        correctionFactor = std::clamp(correctionFactor, 0.5f, 2.0f);
    }

    /**
     * @brief Reset statistics to defaults
     */
    void ResetStats() {
        meanErrorRatio = 1.0f;
        varianceRatio = 0.0f;
        stdDevRatio = 0.0f;
        biasDirection = 0.0f;
        biasConfidence = 0.0f;
        correctionFactor = 1.0f;
        meanErrorNs = 0;
        meanAbsErrorNs = 0;
    }

    /**
     * @brief Check if we have enough samples for reliable statistics
     */
    [[nodiscard]] bool HasReliableStats() const {
        return sampleCount >= 10 && history.size() >= 10;
    }

    /**
     * @brief Get most recent error
     */
    [[nodiscard]] const PredictionError* GetLastError() const {
        return history.empty() ? nullptr : &history.back();
    }
};

/**
 * @brief Aggregated global prediction statistics
 */
struct GlobalPredictionStats {
    uint32_t totalSamples = 0;         ///< Total samples across all task types
    uint32_t taskTypeCount = 0;        ///< Number of unique task types tracked

    float globalMeanErrorRatio = 1.0f; ///< Mean error ratio across all tasks
    float globalVariance = 0.0f;       ///< Variance in error ratio across all tasks

    float overestimatePercent = 0.0f;  ///< Percentage of tasks that overestimate
    float underestimatePercent = 0.0f; ///< Percentage of tasks that underestimate
    float accuratePercent = 0.0f;      ///< Percentage within ±10% of actual

    int64_t totalBiasNs = 0;           ///< Total bias in nanoseconds (sum of all errors)
};

/**
 * @brief Tracks prediction errors for adaptive estimate correction
 *
 * Sprint 6.3: Phase 3.1 - Prediction Error Tracking
 *
 * Maintains per-task-type statistics for learning estimate accuracy
 * and providing correction factors for future estimates.
 *
 * Usage:
 * @code
 * PredictionErrorTracker tracker;
 *
 * // After task execution
 * uint64_t estimated = 2'000'000;  // 2ms estimate
 * uint64_t actual = 2'500'000;     // 2.5ms actual
 * tracker.RecordPrediction("shadowMap", estimated, actual, frameNum);
 *
 * // Get correction factor for future estimates
 * float correction = tracker.GetCorrectionFactor("shadowMap");
 * uint64_t correctedEstimate = estimated * correction;  // ~2.5ms
 * @endcode
 */
class PredictionErrorTracker {
public:
    /**
     * @brief Configuration for prediction error tracking
     */
    struct Config {
        uint32_t windowSize = 60;           ///< Rolling window size per task type
        uint32_t maxTaskTypes = 64;         ///< Maximum unique task types to track
        float accuracyThreshold = 0.10f;    ///< ±10% is considered "accurate"
        float minCorrectionChange = 0.01f;  ///< Minimum correction change to report
        bool enableDetailedHistory = true;  ///< Store per-sample history (memory vs detail)
    };

    /**
     * @brief Construct with configuration
     */
    explicit PredictionErrorTracker(const Config& config = Config{})
        : config_(config) {}

    ~PredictionErrorTracker() = default;

    // Non-copyable, movable
    PredictionErrorTracker(const PredictionErrorTracker&) = delete;
    PredictionErrorTracker& operator=(const PredictionErrorTracker&) = delete;
    PredictionErrorTracker(PredictionErrorTracker&&) noexcept = default;
    PredictionErrorTracker& operator=(PredictionErrorTracker&&) noexcept = default;

    // =========================================================================
    // Recording
    // =========================================================================

    /**
     * @brief Record a prediction result (estimate vs actual)
     *
     * @param taskId Task type identifier (e.g., "shadowMap", "postProcess")
     * @param estimatedNs Original estimate in nanoseconds
     * @param actualNs Measured actual time in nanoseconds
     * @param frameNumber Current frame number (for history)
     */
    void RecordPrediction(
        const std::string& taskId,
        uint64_t estimatedNs,
        uint64_t actualNs,
        uint32_t frameNumber = 0
    ) {
        // Create or find task stats
        auto& stats = GetOrCreateTaskStats(taskId);

        // Create error record
        PredictionError error;
        error.estimatedNs = estimatedNs;
        error.actualNs = actualNs;
        error.frameNumber = frameNumber;
        error.Compute();

        // Add to task stats
        stats.AddSample(error);

        // Update global totals
        ++totalSamples_;
        totalBiasNs_ += error.errorNs;

        // Categorize this sample
        if (error.errorRatio > 1.0f + config_.accuracyThreshold) {
            ++underestimateCount_;  // actual > estimate
        } else if (error.errorRatio < 1.0f - config_.accuracyThreshold) {
            ++overestimateCount_;   // actual < estimate
        } else {
            ++accurateCount_;       // within threshold
        }
    }

    /**
     * @brief Record prediction with numeric task ID (convenience)
     */
    void RecordPrediction(
        uint32_t taskIndex,
        uint64_t estimatedNs,
        uint64_t actualNs,
        uint32_t frameNumber = 0
    ) {
        RecordPrediction("task_" + std::to_string(taskIndex), estimatedNs, actualNs, frameNumber);
    }

    // =========================================================================
    // Per-Task Queries
    // =========================================================================

    /**
     * @brief Get statistics for a specific task type
     *
     * @param taskId Task type identifier
     * @return Pointer to stats (nullptr if task not tracked)
     */
    [[nodiscard]] const TaskPredictionStats* GetTaskStats(const std::string& taskId) const {
        auto it = taskStats_.find(taskId);
        return (it != taskStats_.end()) ? &it->second : nullptr;
    }

    /**
     * @brief Get correction factor for a task type's estimates
     *
     * Returns a multiplier to apply to future estimates:
     * - 1.0 = estimates are accurate
     * - >1.0 = estimates are too low (underestimate)
     * - <1.0 = estimates are too high (overestimate)
     *
     * @param taskId Task type identifier
     * @return Correction multiplier (1.0 if task not tracked)
     */
    [[nodiscard]] float GetCorrectionFactor(const std::string& taskId) const {
        auto* stats = GetTaskStats(taskId);
        if (stats && stats->HasReliableStats()) {
            return stats->correctionFactor;
        }
        return 1.0f;  // No data = no correction
    }

    /**
     * @brief Get bias direction for a task type
     *
     * @param taskId Task type identifier
     * @return Bias: >0 = underestimate, <0 = overestimate, 0 = unknown
     */
    [[nodiscard]] float GetBiasDirection(const std::string& taskId) const {
        auto* stats = GetTaskStats(taskId);
        return stats ? stats->biasDirection : 0.0f;
    }

    /**
     * @brief Get mean absolute error for a task type
     *
     * @param taskId Task type identifier
     * @return Mean absolute error in nanoseconds (0 if not tracked)
     */
    [[nodiscard]] uint64_t GetMeanAbsoluteError(const std::string& taskId) const {
        auto* stats = GetTaskStats(taskId);
        return stats ? stats->meanAbsErrorNs : 0;
    }

    /**
     * @brief Check if task has reliable statistics
     *
     * @param taskId Task type identifier
     * @return true if enough samples for reliable correction
     */
    [[nodiscard]] bool HasReliableStats(const std::string& taskId) const {
        auto* stats = GetTaskStats(taskId);
        return stats && stats->HasReliableStats();
    }

    // =========================================================================
    // Global Queries
    // =========================================================================

    /**
     * @brief Get aggregated global statistics
     */
    [[nodiscard]] GlobalPredictionStats GetGlobalStats() const {
        GlobalPredictionStats global;
        global.totalSamples = totalSamples_;
        global.taskTypeCount = static_cast<uint32_t>(taskStats_.size());
        global.totalBiasNs = totalBiasNs_;

        if (totalSamples_ > 0) {
            float total = static_cast<float>(totalSamples_);
            global.overestimatePercent = static_cast<float>(overestimateCount_) / total * 100.0f;
            global.underestimatePercent = static_cast<float>(underestimateCount_) / total * 100.0f;
            global.accuratePercent = static_cast<float>(accurateCount_) / total * 100.0f;
        }

        // Compute global mean from task stats
        if (!taskStats_.empty()) {
            float sum = 0.0f;
            for (const auto& [id, stats] : taskStats_) {
                sum += stats.meanErrorRatio;
            }
            global.globalMeanErrorRatio = sum / static_cast<float>(taskStats_.size());
        }

        return global;
    }

    /**
     * @brief Get number of tracked task types
     */
    [[nodiscard]] uint32_t GetTaskTypeCount() const {
        return static_cast<uint32_t>(taskStats_.size());
    }

    /**
     * @brief Get total samples recorded
     */
    [[nodiscard]] uint32_t GetTotalSamples() const {
        return totalSamples_;
    }

    /**
     * @brief Get all task IDs being tracked
     */
    [[nodiscard]] std::vector<std::string> GetTrackedTaskIds() const {
        std::vector<std::string> ids;
        ids.reserve(taskStats_.size());
        for (const auto& [id, _] : taskStats_) {
            ids.push_back(id);
        }
        return ids;
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const Config& GetConfig() const {
        return config_;
    }

    /**
     * @brief Set window size for rolling statistics
     */
    void SetWindowSize(uint32_t windowSize) {
        config_.windowSize = windowSize;
        // Update existing task stats
        for (auto& [id, stats] : taskStats_) {
            stats.windowSize = windowSize;
        }
    }

    /**
     * @brief Clear all statistics
     */
    void Clear() {
        taskStats_.clear();
        totalSamples_ = 0;
        totalBiasNs_ = 0;
        overestimateCount_ = 0;
        underestimateCount_ = 0;
        accurateCount_ = 0;
    }

    /**
     * @brief Clear statistics for a specific task type
     */
    void ClearTask(const std::string& taskId) {
        taskStats_.erase(taskId);
    }

private:
    /**
     * @brief Get or create task statistics entry
     */
    TaskPredictionStats& GetOrCreateTaskStats(const std::string& taskId) {
        auto it = taskStats_.find(taskId);
        if (it != taskStats_.end()) {
            return it->second;
        }

        // Check max task types limit
        if (taskStats_.size() >= config_.maxTaskTypes) {
            // Evict least-used task (lowest sample count)
            auto minIt = std::min_element(taskStats_.begin(), taskStats_.end(),
                [](const auto& a, const auto& b) {
                    return a.second.sampleCount < b.second.sampleCount;
                });
            if (minIt != taskStats_.end()) {
                taskStats_.erase(minIt);
            }
        }

        // Create new entry
        TaskPredictionStats& stats = taskStats_[taskId];
        stats.taskId = taskId;
        stats.windowSize = config_.windowSize;
        return stats;
    }

    Config config_;
    std::unordered_map<std::string, TaskPredictionStats> taskStats_;

    // Global counters
    uint32_t totalSamples_ = 0;
    int64_t totalBiasNs_ = 0;
    uint32_t overestimateCount_ = 0;
    uint32_t underestimateCount_ = 0;
    uint32_t accurateCount_ = 0;
};

} // namespace Vixen::RenderGraph
