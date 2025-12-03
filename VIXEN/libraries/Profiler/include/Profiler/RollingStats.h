#pragma once

#include "FrameMetrics.h"
#include <deque>
#include <vector>
#include <cstddef>

namespace Vixen::Profiler {

/// Rolling statistics calculator with percentile support
/// Uses a fixed-size sliding window for memory efficiency
class RollingStats {
public:
    /// Create rolling stats with specified window size
    /// @param windowSize Maximum number of samples to keep (default: 300 frames = 5 seconds at 60fps)
    explicit RollingStats(size_t windowSize = 300);

    /// Add a new sample value
    void AddSample(float value);

    /// Clear all samples and reset statistics
    void Reset();

    /// Get minimum value in window
    float GetMin() const;

    /// Get maximum value in window
    float GetMax() const;

    /// Get arithmetic mean of samples
    float GetMean() const;

    /// Get standard deviation of samples
    float GetStdDev() const;

    /// Get number of samples currently in window
    size_t GetSampleCount() const { return samples_.size(); }

    /// Get configured window size
    size_t GetWindowSize() const { return windowSize_; }

    /// Get percentile value (p in range [0.0, 1.0])
    /// Uses linear interpolation between samples
    /// @param p Percentile as fraction (0.01 = 1st percentile, 0.5 = median, 0.99 = 99th)
    float GetPercentile(float p) const;

    /// Convenience methods for common percentiles
    float GetP1() const { return GetPercentile(0.01f); }
    float GetP50() const { return GetPercentile(0.50f); }  // Median
    float GetP99() const { return GetPercentile(0.99f); }

    /// Get complete aggregate statistics
    AggregateStats GetAggregateStats() const;

    /// Check if enough samples collected for meaningful statistics
    bool HasMinimumSamples(size_t minCount = 10) const { return samples_.size() >= minCount; }

private:
    std::deque<float> samples_;
    size_t windowSize_;

    // Running totals for O(1) mean calculation
    double sum_ = 0.0;
    double sumSquared_ = 0.0;

    // Cached sorted samples for percentile calculation (invalidated on AddSample)
    mutable std::vector<float> sortedCache_;
    mutable bool sortedCacheValid_ = false;

    void InvalidateCache() { sortedCacheValid_ = false; }
    void EnsureSortedCache() const;
};

} // namespace Vixen::Profiler
