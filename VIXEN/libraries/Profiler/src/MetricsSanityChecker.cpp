#include "Profiler/MetricsSanityChecker.h"
#include <cmath>
#include <map>

namespace Vixen::Profiler {

ValidationResult MetricsSanityChecker::Validate(
    const std::vector<FrameMetrics>& frames,
    const TestConfiguration& config) const {

    ValidationResult result;
    result.valid = true;

    if (frames.empty()) {
        AddCheck(result, "no_frames", "frames",
                 "No frame data collected", SanityCheckSeverity::Error);
        return result;
    }

    // Run all checks
    CheckPositiveValues(frames, result);
    CheckPerformance(frames, result);
    CheckGpuCpuTiming(frames, result);
    CheckRayCount(frames, config, result);
    CheckMetricsPresent(frames, result);
    CheckSufficientSamples(frames, config, result);

    // Update valid flag based on error count
    result.valid = (result.errorCount == 0);

    return result;
}

ValidationResult MetricsSanityChecker::ValidateAggregates(
    const std::map<std::string, AggregateStats>& aggregates) const {

    ValidationResult result;
    result.valid = true;

    CheckOutliers(aggregates, result);

    result.valid = (result.errorCount == 0);
    return result;
}

void MetricsSanityChecker::CheckPositiveValues(
    const std::vector<FrameMetrics>& frames,
    ValidationResult& result) const {

    uint32_t negativeFrameTime = 0;
    uint32_t negativeGpuTime = 0;
    uint32_t negativeFps = 0;
    uint32_t zeroFrameTime = 0;

    for (const auto& frame : frames) {
        if (frame.frameTimeMs < 0.0f) negativeFrameTime++;
        if (frame.gpuTimeMs < 0.0f) negativeGpuTime++;
        if (frame.fps < 0.0f) negativeFps++;
        if (frame.frameTimeMs == 0.0f) zeroFrameTime++;
    }

    float totalFrames = static_cast<float>(frames.size());

    if (negativeFrameTime > 0) {
        AddCheck(result, "negative_frame_time", "frameTimeMs",
                 "Frame time has negative values",
                 SanityCheckSeverity::Error,
                 negativeFrameTime, negativeFrameTime / totalFrames);
    }

    if (negativeGpuTime > 0) {
        AddCheck(result, "negative_gpu_time", "gpuTimeMs",
                 "GPU time has negative values",
                 SanityCheckSeverity::Error,
                 negativeGpuTime, negativeGpuTime / totalFrames);
    }

    if (negativeFps > 0) {
        AddCheck(result, "negative_fps", "fps",
                 "FPS has negative values",
                 SanityCheckSeverity::Error,
                 negativeFps, negativeFps / totalFrames);
    }

    if (zeroFrameTime > 0) {
        AddCheck(result, "zero_frame_time", "frameTimeMs",
                 "Frame time is exactly zero (likely measurement error)",
                 SanityCheckSeverity::Warning,
                 zeroFrameTime, zeroFrameTime / totalFrames);
    }
}

void MetricsSanityChecker::CheckPerformance(
    const std::vector<FrameMetrics>& frames,
    ValidationResult& result) const {

    uint32_t lowFpsFrames = 0;
    // Use 59.0 to allow for vsync tolerance (60 FPS vsync can report 59.9)
    const float fpsThreshold = 59.0f;

    for (const auto& frame : frames) {
        if (frame.fps > 0.0f && frame.fps < fpsThreshold) {
            lowFpsFrames++;
        }
    }

    float totalFrames = static_cast<float>(frames.size());

    // Only warn if majority of frames are below threshold
    if (lowFpsFrames > frames.size() / 2) {
        AddCheck(result, "low_fps", "fps",
                 "Performance below 60 FPS for majority of frames",
                 SanityCheckSeverity::Warning,
                 lowFpsFrames, lowFpsFrames / totalFrames);
    }
}

void MetricsSanityChecker::CheckGpuCpuTiming(
    const std::vector<FrameMetrics>& frames,
    ValidationResult& result) const {

    uint32_t gpuExceedsCpu = 0;

    for (const auto& frame : frames) {
        // GPU time should not exceed CPU frame time (GPU runs in parallel)
        // Allow small tolerance for measurement noise
        if (frame.gpuTimeMs > frame.frameTimeMs * 1.1f && frame.gpuTimeMs > 0.0f) {
            gpuExceedsCpu++;
        }
    }

    float totalFrames = static_cast<float>(frames.size());

    if (gpuExceedsCpu > frames.size() / 4) {
        AddCheck(result, "gpu_exceeds_cpu", "gpuTimeMs",
                 "GPU time exceeds CPU frame time (possible timing error)",
                 SanityCheckSeverity::Warning,
                 gpuExceedsCpu, gpuExceedsCpu / totalFrames);
    }
}

void MetricsSanityChecker::CheckRayCount(
    const std::vector<FrameMetrics>& frames,
    const TestConfiguration& config,
    ValidationResult& result) const {

    uint64_t expectedRays = static_cast<uint64_t>(config.screenWidth) * config.screenHeight;
    uint32_t mismatchCount = 0;

    for (const auto& frame : frames) {
        if (frame.totalRaysCast != expectedRays && frame.totalRaysCast != 0) {
            mismatchCount++;
        }
    }

    float totalFrames = static_cast<float>(frames.size());

    if (mismatchCount > 0) {
        AddCheck(result, "ray_count_mismatch", "totalRaysCast",
                 "Ray count doesn't match screen dimensions (" +
                 std::to_string(expectedRays) + " expected)",
                 SanityCheckSeverity::Error,
                 mismatchCount, mismatchCount / totalFrames);
    }
}

void MetricsSanityChecker::CheckMetricsPresent(
    const std::vector<FrameMetrics>& frames,
    ValidationResult& result) const {

    uint32_t zeroBandwidth = 0;
    uint32_t zeroMrays = 0;
    uint32_t zeroVoxelIter = 0;

    for (const auto& frame : frames) {
        if (frame.bandwidthReadGB == 0.0f) zeroBandwidth++;
        if (frame.mRaysPerSec == 0.0f) zeroMrays++;
        if (frame.avgVoxelsPerRay == 0.0f) zeroVoxelIter++;
    }

    float totalFrames = static_cast<float>(frames.size());

    // Warn if ALL frames are missing a metric (suggests instrumentation issue)
    if (zeroBandwidth == frames.size()) {
        AddCheck(result, "missing_bandwidth", "bandwidthReadGB",
                 "Bandwidth data not collected (all frames zero)",
                 SanityCheckSeverity::Warning,
                 zeroBandwidth, 1.0f);
    }

    if (zeroMrays == frames.size()) {
        AddCheck(result, "missing_mrays", "mRaysPerSec",
                 "Ray throughput not collected (all frames zero)",
                 SanityCheckSeverity::Warning,
                 zeroMrays, 1.0f);
    }

    if (zeroVoxelIter == frames.size()) {
        AddCheck(result, "missing_voxel_iterations", "avgVoxelsPerRay",
                 "Voxel iteration count not collected (all frames zero)",
                 SanityCheckSeverity::Warning,
                 zeroVoxelIter, 1.0f);
    }
}

void MetricsSanityChecker::CheckSufficientSamples(
    const std::vector<FrameMetrics>& frames,
    const TestConfiguration& config,
    ValidationResult& result) const {

    if (frames.size() < config.measurementFrames) {
        AddCheck(result, "insufficient_samples", "frames",
                 "Collected " + std::to_string(frames.size()) +
                 " frames, expected " + std::to_string(config.measurementFrames),
                 SanityCheckSeverity::Error,
                 static_cast<uint32_t>(config.measurementFrames - frames.size()),
                 1.0f - static_cast<float>(frames.size()) / config.measurementFrames);
    }
}

void MetricsSanityChecker::CheckOutliers(
    const std::map<std::string, AggregateStats>& aggregates,
    ValidationResult& result) const {

    const float outlierThreshold = 100.0f;  // p99/p1 ratio threshold

    for (const auto& [metricName, stats] : aggregates) {
        if (stats.p1 > 0.0f && stats.p99 > 0.0f) {
            float ratio = stats.p99 / stats.p1;
            if (ratio > outlierThreshold) {
                AddCheck(result, "extreme_outliers", metricName,
                         "Extreme variation detected (p99/p1 ratio: " +
                         std::to_string(static_cast<int>(ratio)) + ")",
                         SanityCheckSeverity::Warning);
            }
        }

        // Check for NaN/Inf in aggregates
        if (std::isnan(stats.mean) || std::isinf(stats.mean)) {
            AddCheck(result, "invalid_aggregate", metricName,
                     "Aggregate contains NaN or Inf values",
                     SanityCheckSeverity::Error);
        }
    }
}

void MetricsSanityChecker::AddCheck(
    ValidationResult& result,
    const std::string& name,
    const std::string& metric,
    const std::string& message,
    SanityCheckSeverity severity,
    uint32_t affectedFrames,
    float failureRate) const {

    SanityCheckResult check;
    check.checkName = name;
    check.metric = metric;
    check.message = message;
    check.severity = severity;
    check.affectedFrames = affectedFrames;
    check.failureRate = failureRate;

    result.checks.push_back(check);

    switch (severity) {
        case SanityCheckSeverity::Info:
            result.infoCount++;
            break;
        case SanityCheckSeverity::Warning:
            result.warningCount++;
            break;
        case SanityCheckSeverity::Error:
            result.errorCount++;
            result.valid = false;
            break;
    }
}

} // namespace Vixen::Profiler
