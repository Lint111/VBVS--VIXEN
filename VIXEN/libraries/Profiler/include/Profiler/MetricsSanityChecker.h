#pragma once

#include "FrameMetrics.h"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace Vixen::Profiler {

/// Severity level for sanity check results
enum class SanityCheckSeverity {
    Info,       // Informational note
    Warning,    // Potential issue, data may still be usable
    Error       // Critical issue, data likely invalid
};

/// Result of a single sanity check
struct SanityCheckResult {
    std::string checkName;          // e.g., "positive_frame_time"
    std::string metric;             // e.g., "frameTimeMs"
    std::string message;            // Human-readable description
    SanityCheckSeverity severity;
    uint32_t affectedFrames = 0;    // Number of frames failing this check
    float failureRate = 0.0f;       // Percentage of frames failing (0.0-1.0)
};

/// Aggregate validation result for a test run
struct ValidationResult {
    bool valid = true;              // True if no errors (warnings allowed)
    std::vector<SanityCheckResult> checks;
    uint32_t errorCount = 0;
    uint32_t warningCount = 0;
    uint32_t infoCount = 0;

    /// Check if all validations passed (no errors)
    bool IsValid() const { return errorCount == 0; }

    /// Check if data is clean (no warnings or errors)
    bool IsClean() const { return errorCount == 0 && warningCount == 0; }
};

/// Validates collected benchmark metrics for sanity/correctness
class MetricsSanityChecker {
public:
    MetricsSanityChecker() = default;

    /// Validate a collection of frame metrics against sanity checks
    /// @param frames The collected frame metrics
    /// @param config The test configuration used
    /// @return Validation result with all check outcomes
    ValidationResult Validate(
        const std::vector<FrameMetrics>& frames,
        const TestConfiguration& config) const;

    /// Validate aggregate statistics
    /// @param aggregates Map of metric name to aggregate stats
    /// @return Validation result for aggregates
    ValidationResult ValidateAggregates(
        const std::map<std::string, AggregateStats>& aggregates) const;

private:
    // Individual check methods - each returns issues found
    void CheckPositiveValues(
        const std::vector<FrameMetrics>& frames,
        ValidationResult& result) const;

    void CheckPerformance(
        const std::vector<FrameMetrics>& frames,
        ValidationResult& result) const;

    void CheckGpuCpuTiming(
        const std::vector<FrameMetrics>& frames,
        ValidationResult& result) const;

    void CheckRayCount(
        const std::vector<FrameMetrics>& frames,
        const TestConfiguration& config,
        ValidationResult& result) const;

    void CheckMetricsPresent(
        const std::vector<FrameMetrics>& frames,
        ValidationResult& result) const;

    void CheckSufficientSamples(
        const std::vector<FrameMetrics>& frames,
        const TestConfiguration& config,
        ValidationResult& result) const;

    void CheckOutliers(
        const std::map<std::string, AggregateStats>& aggregates,
        ValidationResult& result) const;

    // Helper to add a check result
    void AddCheck(
        ValidationResult& result,
        const std::string& name,
        const std::string& metric,
        const std::string& message,
        SanityCheckSeverity severity,
        uint32_t affectedFrames = 0,
        float failureRate = 0.0f) const;
};

/// Convert severity to string for JSON output
inline std::string SeverityToString(SanityCheckSeverity severity) {
    switch (severity) {
        case SanityCheckSeverity::Info: return "info";
        case SanityCheckSeverity::Warning: return "warning";
        case SanityCheckSeverity::Error: return "error";
        default: return "unknown";
    }
}

} // namespace Vixen::Profiler
