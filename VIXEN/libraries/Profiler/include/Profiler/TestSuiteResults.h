#pragma once

#include "FrameMetrics.h"
#include "DeviceCapabilities.h"
#include <vector>
#include <string>
#include <map>
#include <chrono>

namespace Vixen::Profiler {

/// Results from a single benchmark test run
struct TestRunResults {
    TestConfiguration config;
    std::vector<FrameMetrics> frames;
    std::map<std::string, AggregateStats> aggregates;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;

    /// Get duration of this test run
    double GetDurationSeconds() const;

    /// Check if test completed successfully (has measurement frames)
    bool IsValid() const { return frames.size() >= config.measurementFrames; }
};

/// Aggregated results from a complete test suite (multiple configurations)
class TestSuiteResults {
public:
    TestSuiteResults() = default;

    /// Set device capabilities (captured once at suite start)
    void SetDeviceCapabilities(const DeviceCapabilities& capabilities);

    /// Get device capabilities
    const DeviceCapabilities& GetDeviceCapabilities() const { return deviceCapabilities_; }

    /// Add results from a completed test run
    void AddTestRun(const TestRunResults& results);

    /// Get all test run results
    const std::vector<TestRunResults>& GetAllResults() const { return results_; }

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

private:
    DeviceCapabilities deviceCapabilities_;
    std::vector<TestRunResults> results_;
    std::string suiteName_ = "Benchmark Suite";
    std::chrono::system_clock::time_point suiteStartTime_;
    std::chrono::system_clock::time_point suiteEndTime_;
};

} // namespace Vixen::Profiler
