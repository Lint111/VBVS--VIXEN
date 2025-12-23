#pragma once

#include "FrameMetrics.h"
#include "DeviceCapabilities.h"
#include "MetricsSanityChecker.h"
#include <vector>
#include <string>
#include <map>
#include <chrono>

namespace Vixen::Profiler {

/// Cross-run statistics for a single metric
struct CrossRunStats {
    double mean = 0.0;      ///< Mean across all runs
    double stddev = 0.0;    ///< Standard deviation across runs
    double min = 0.0;       ///< Minimum value across runs
    double max = 0.0;       ///< Maximum value across runs
    uint32_t runCount = 0;  ///< Number of runs aggregated

    /// Check if stats have valid data
    bool HasData() const { return runCount > 0; }
};

/// Results from a single benchmark test run
struct TestRunResults {
    TestConfiguration config;
    std::vector<FrameMetrics> frames;
    std::map<std::string, AggregateStats> aggregates;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    ValidationResult validation;  // Sanity check results

    // Acceleration structure build timing (hardware_rt pipeline only)
    float blasBuildTimeMs = 0.0f;
    float tlasBuildTimeMs = 0.0f;

    /// Get duration of this test run
    double GetDurationSeconds() const;

    /// Check if test completed successfully (has measurement frames and passes validation)
    bool IsValid() const {
        return frames.size() >= config.measurementFrames && validation.IsValid();
    }

    /// Check if test completed with warnings but no errors
    bool HasWarnings() const { return validation.warningCount > 0; }
};

/// Results from multiple runs of the same configuration
struct MultiRunResults {
    TestConfiguration config;
    std::vector<TestRunResults> runs;  ///< Individual run results

    /// Cross-run statistics for key metrics
    CrossRunStats frameTimeMean;       ///< Statistics of per-run mean frame times
    CrossRunStats fpsMean;             ///< Statistics of per-run mean FPS
    CrossRunStats bandwidthMean;       ///< Statistics of per-run mean bandwidth
    CrossRunStats avgVoxelsPerRay;     ///< Statistics of per-run avg voxels/ray

    /// Get number of runs
    uint32_t GetRunCount() const { return static_cast<uint32_t>(runs.size()); }

    /// Check if all runs completed successfully
    bool AllRunsValid() const {
        for (const auto& run : runs) {
            if (!run.IsValid()) return false;
        }
        return !runs.empty();
    }

    /// Compute cross-run statistics from the individual runs
    void ComputeStatistics();
};

/// Aggregated results from a complete test suite (multiple configurations)
class TestSuiteResults {
public:
    TestSuiteResults() = default;

    /// Set device capabilities (captured once at suite start)
    void SetDeviceCapabilities(const DeviceCapabilities& capabilities);

    /// Get device capabilities
    const DeviceCapabilities& GetDeviceCapabilities() const { return deviceCapabilities_; }

    /// Add results from a completed test run (single-run mode)
    void AddTestRun(const TestRunResults& results);

    /// Add results from a multi-run configuration
    void AddMultiRun(const MultiRunResults& results);

    /// Merge results from another test suite (for multi-GPU benchmarking)
    void Merge(const TestSuiteResults& other) {
        // Merge all test runs
        for (const auto& result : other.GetAllResults()) {
            results_.push_back(result);
        }
        // Merge multi-run results if present
        for (const auto& multiRun : other.GetMultiRunResults()) {
            multiRunResults_.push_back(multiRun);
        }
    }

    /// Get all test run results (single-run mode, or flattened multi-run)
    const std::vector<TestRunResults>& GetAllResults() const { return results_; }

    /// Get multi-run results (when runsPerConfig > 1)
    const std::vector<MultiRunResults>& GetMultiRunResults() const { return multiRunResults_; }

    /// Check if this suite has multi-run data
    bool HasMultiRunData() const { return !multiRunResults_.empty(); }

    /// Get number of completed tests
    size_t GetTestCount() const { return results_.size(); }

    /// Get total number of tests (same as GetTestCount, for consistency)
    uint32_t GetTotalCount() const { return static_cast<uint32_t>(results_.size()); }

    /// Get number of passed tests (tests with valid results)
    uint32_t GetPassCount() const {
        uint32_t passed = 0;
        for (const auto& result : results_) {
            if (result.IsValid()) {
                ++passed;
            }
        }
        return passed;
    }

    /// Get total duration of all tests
    double GetTotalDurationSeconds() const;

    /// Set suite-level metadata
    void SetSuiteName(const std::string& name) { suiteName_ = name; }
    void SetStartTime(std::chrono::system_clock::time_point time) { suiteStartTime_ = time; }
    void SetEndTime(std::chrono::system_clock::time_point time) { suiteEndTime_ = time; }

    /// Get suite metadata
    const std::string& GetSuiteName() const { return suiteName_; }

    /// Export all results to a summary JSON file
    void ExportSummary(const std::string& filepath) const;

    /// Clear all results
    void Clear();

    /// Check if this is multi-GPU mode (results exported per-GPU, not centralized)
    bool IsMultiGPUMode() const { return isMultiGPUMode_; }

    // Public field for multi-GPU mode flag (set by BenchmarkRunner)
    bool isMultiGPUMode = false;

private:
    DeviceCapabilities deviceCapabilities_;
    std::vector<TestRunResults> results_;           ///< Single-run results (or flattened multi-run)
    std::vector<MultiRunResults> multiRunResults_;  ///< Multi-run results with cross-run statistics
    std::string suiteName_ = "Benchmark Suite";
    std::chrono::system_clock::time_point suiteStartTime_;
    std::chrono::system_clock::time_point suiteEndTime_;
    bool isMultiGPUMode_ = false;  ///< Flag indicating multi-GPU mode (results per-GPU, not centralized)
};

} // namespace Vixen::Profiler
