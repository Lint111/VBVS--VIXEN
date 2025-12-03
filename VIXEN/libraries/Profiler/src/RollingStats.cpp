#include "Profiler/RollingStats.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace Vixen::Profiler {

RollingStats::RollingStats(size_t windowSize)
    : windowSize_(windowSize) {
    sortedCache_.reserve(windowSize);
}

void RollingStats::AddSample(float value) {
    // Remove oldest sample if at capacity
    if (samples_.size() >= windowSize_) {
        float removed = samples_.front();
        sum_ -= removed;
        sumSquared_ -= static_cast<double>(removed) * removed;
        samples_.pop_front();
    }

    // Add new sample
    samples_.push_back(value);
    sum_ += value;
    sumSquared_ += static_cast<double>(value) * value;

    InvalidateCache();
}

void RollingStats::Reset() {
    samples_.clear();
    sum_ = 0.0;
    sumSquared_ = 0.0;
    sortedCache_.clear();
    sortedCacheValid_ = false;
}

float RollingStats::GetMin() const {
    if (samples_.empty()) return 0.0f;
    return *std::min_element(samples_.begin(), samples_.end());
}

float RollingStats::GetMax() const {
    if (samples_.empty()) return 0.0f;
    return *std::max_element(samples_.begin(), samples_.end());
}

float RollingStats::GetMean() const {
    if (samples_.empty()) return 0.0f;
    return static_cast<float>(sum_ / samples_.size());
}

float RollingStats::GetStdDev() const {
    if (samples_.size() < 2) return 0.0f;

    double n = static_cast<double>(samples_.size());
    double mean = sum_ / n;
    double variance = (sumSquared_ / n) - (mean * mean);

    // Handle numerical precision issues
    if (variance < 0.0) variance = 0.0;

    return static_cast<float>(std::sqrt(variance));
}

float RollingStats::GetPercentile(float p) const {
    if (samples_.empty()) return 0.0f;
    if (samples_.size() == 1) return samples_.front();

    EnsureSortedCache();

    // Clamp p to [0, 1]
    p = std::clamp(p, 0.0f, 1.0f);

    // Calculate index with linear interpolation
    float index = p * (sortedCache_.size() - 1);
    size_t lowerIdx = static_cast<size_t>(std::floor(index));
    size_t upperIdx = static_cast<size_t>(std::ceil(index));

    if (lowerIdx == upperIdx) {
        return sortedCache_[lowerIdx];
    }

    // Linear interpolation between adjacent values
    float fraction = index - lowerIdx;
    return sortedCache_[lowerIdx] * (1.0f - fraction) + sortedCache_[upperIdx] * fraction;
}

AggregateStats RollingStats::GetAggregateStats() const {
    AggregateStats stats;
    stats.min = GetMin();
    stats.max = GetMax();
    stats.mean = GetMean();
    stats.stddev = GetStdDev();
    stats.p1 = GetP1();
    stats.p50 = GetP50();
    stats.p99 = GetP99();
    stats.sampleCount = static_cast<uint32_t>(samples_.size());
    return stats;
}

void RollingStats::EnsureSortedCache() const {
    if (sortedCacheValid_) return;

    sortedCache_.assign(samples_.begin(), samples_.end());
    std::sort(sortedCache_.begin(), sortedCache_.end());
    sortedCacheValid_ = true;
}

} // namespace Vixen::Profiler
