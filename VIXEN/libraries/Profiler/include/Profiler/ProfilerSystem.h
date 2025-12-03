#pragma once

#include "FrameMetrics.h"
#include "RollingStats.h"
#include "DeviceCapabilities.h"
#include "MetricsCollector.h"
#include "MetricsExporter.h"
#include "TestSuiteResults.h"
#include "BenchmarkConfig.h"

#include <vulkan/vulkan.h>
#include <memory>
#include <functional>
#include <filesystem>

namespace Vixen::Profiler {

/// Main profiler system - external hookable system like EventBus
/// Coordinates metrics collection, aggregation, and export
///
/// Usage:
/// 1. Initialize with Vulkan device at startup
/// 2. Register node extractors for scene-specific metrics
/// 3. Call OnFrameBegin/OnFrameEnd from GraphLifecycleHooks
/// 4. Start test run with StartTestRun(), stop with EndTestRun()
/// 5. Export results via GetCurrentResults() or auto-export
class ProfilerSystem {
public:
    /// Get singleton instance
    static ProfilerSystem& Instance();

    // ========================================================================
    // Initialization
    // ========================================================================

    /// Initialize the profiler system with Vulkan device
    /// @param device Logical device for GPU queries
    /// @param physicalDevice Physical device for capabilities
    /// @param framesInFlight Number of frames in flight for query pools
    void Initialize(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t framesInFlight = 3);

    /// Shutdown and release all resources
    void Shutdown();

    /// Check if system is initialized
    bool IsInitialized() const { return initialized_; }

    // ========================================================================
    // Device Capabilities
    // ========================================================================

    /// Capture and store device capabilities (call once per test suite)
    void CaptureDeviceCapabilities(VkPhysicalDevice physicalDevice);

    /// Get captured device capabilities
    const DeviceCapabilities& GetDeviceCapabilities() const { return deviceCapabilities_; }

    // ========================================================================
    // Test Run Management
    // ========================================================================

    /// Start a new test run with given configuration
    void StartTestRun(const TestConfiguration& config);

    /// End current test run and collect results
    /// @param autoExport If true, automatically export results to configured directory
    void EndTestRun(bool autoExport = true);

    /// Check if a test run is currently active
    bool IsTestRunActive() const { return testRunActive_; }

    /// Get current test configuration
    const TestConfiguration& GetCurrentConfig() const { return currentConfig_; }

    /// Get results from current/last test run
    const TestRunResults& GetCurrentResults() const { return currentResults_; }

    // ========================================================================
    // Frame Hooks (connect to GraphLifecycleHooks)
    // ========================================================================

    /// Called at start of frame (connect to PreExecute)
    void OnFrameBegin(VkCommandBuffer cmdBuffer, uint32_t frameIndex);

    /// Called before compute dispatch
    void OnDispatchBegin(VkCommandBuffer cmdBuffer, uint32_t frameIndex);

    /// Called after compute dispatch
    void OnDispatchEnd(VkCommandBuffer cmdBuffer, uint32_t frameIndex,
                       uint32_t dispatchWidth, uint32_t dispatchHeight);

    /// Called at end of frame (connect to PostExecute)
    void OnFrameEnd(uint32_t frameIndex);

    /// Called before graph cleanup (extract node metrics)
    void OnPreCleanup();

    // ========================================================================
    // Node Extractors
    // ========================================================================

    /// Register extractor for node-specific metrics
    void RegisterExtractor(const std::string& name, NodeMetricsExtractor extractor);

    /// Unregister extractor
    void UnregisterExtractor(const std::string& name);

    // ========================================================================
    // Real-time Metrics
    // ========================================================================

    /// Get most recent frame metrics
    const FrameMetrics& GetLastFrameMetrics() const;

    /// Get rolling statistics for a metric
    const RollingStats* GetRollingStats(const std::string& metricName) const;

    // ========================================================================
    // Export Configuration
    // ========================================================================

    /// Set output directory for auto-export
    void SetOutputDirectory(const std::filesystem::path& directory);

    /// Enable/disable auto-export on EndTestRun
    void SetAutoExport(bool enabled) { autoExportEnabled_ = enabled; }

    /// Set export formats
    void SetExportFormats(bool csv, bool json);

    // ========================================================================
    // Test Suite Management
    // ========================================================================

    /// Start a new test suite (clears previous results)
    void StartTestSuite(const std::string& suiteName = "Benchmark Suite");

    /// End test suite and export summary
    void EndTestSuite();

    /// Get accumulated test suite results
    const TestSuiteResults& GetTestSuiteResults() const { return testSuiteResults_; }

    /// Run a batch of tests from configuration list
    /// @param configs List of test configurations to run
    /// @param testExecutor Function that runs a single test (returns true on success)
    /// @return Number of successful tests
    size_t RunTestBatch(
        const std::vector<TestConfiguration>& configs,
        std::function<bool(const TestConfiguration&)> testExecutor);

private:
    ProfilerSystem() = default;
    ~ProfilerSystem() = default;
    ProfilerSystem(const ProfilerSystem&) = delete;
    ProfilerSystem& operator=(const ProfilerSystem&) = delete;

    bool initialized_ = false;
    bool testRunActive_ = false;
    bool autoExportEnabled_ = true;
    bool exportCSV_ = true;
    bool exportJSON_ = false;

    std::unique_ptr<MetricsCollector> collector_;
    MetricsExporter exporter_;
    DeviceCapabilities deviceCapabilities_;

    TestConfiguration currentConfig_;
    TestRunResults currentResults_;
    TestSuiteResults testSuiteResults_;

    std::filesystem::path outputDirectory_ = "benchmarks/results";
    std::chrono::system_clock::time_point testRunStartTime_;

    void FinalizeCurrentResults();
    void ExportCurrentResults();
};

} // namespace Vixen::Profiler
