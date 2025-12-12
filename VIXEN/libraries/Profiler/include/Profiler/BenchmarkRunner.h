#pragma once

#include "FrameMetrics.h"
#include "TestSuiteResults.h"
#include "DeviceCapabilities.h"
#include "MetricsExporter.h"
#include "RollingStats.h"
#include "BenchmarkGraphFactory.h"
#include "ProfilerGraphAdapter.h"
#include "BenchmarkConfig.h"
#include "FrameCapture.h"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <memory>

// Forward declarations to avoid circular includes
namespace Vixen::RenderGraph {
    class RenderGraph;
    class NodeTypeRegistry;
}

namespace Vixen::EventBus {
    class MessageBus;
}

namespace Vixen::Profiler {

/// Benchmark execution state
enum class BenchmarkState {
    Idle,           // Not running
    Warmup,         // Warmup phase (frames not recorded)
    Measuring,      // Measurement phase (frames recorded)
    Completed,      // Run completed
    Error           // Error occurred
};

/// Bandwidth estimation configuration
struct BandwidthEstimationConfig {
    float avgBytesPerRay = 96.0f;           // Conservative estimate for octree traversal
    float minBytesPerRay = 64.0f;           // Minimum (best case)
    float maxBytesPerRay = 128.0f;          // Maximum (worst case)
    bool useEstimation = true;               // Enable bandwidth estimation fallback
};

/// Orchestrates benchmark test execution and results collection
class BenchmarkRunner {
public:
    /// Callback invoked each frame during benchmark
    /// @param frameNum Current frame number (0-indexed relative to start of measurement)
    using FrameCallback = std::function<void(uint32_t frameNum)>;

    /// Callback for progress reporting
    /// @param currentTest Index of current test in matrix
    /// @param totalTests Total number of tests
    /// @param currentFrame Current frame in current test
    /// @param totalFrames Total frames in current test (warmup + measurement)
    using ProgressCallback = std::function<void(uint32_t currentTest, uint32_t totalTests,
                                                 uint32_t currentFrame, uint32_t totalFrames)>;

    BenchmarkRunner();
    ~BenchmarkRunner();

    //==========================================================================
    // High-Level API: Complete Benchmark Suite Execution
    //==========================================================================

    /**
     * @brief Run a complete benchmark suite with internal Vulkan lifecycle
     *
     * This is the primary entry point for benchmark execution. It handles:
     * - Vulkan instance and device creation
     * - RenderGraph setup (headless or windowed)
     * - Test matrix execution with profiler hooks
     * - Results collection and export
     * - Vulkan cleanup
     *
     * The caller (vixen_benchmark) only needs to create TestConfiguration
     * structs - all Vulkan operations are internal to this method.
     *
     * @param config Suite configuration including tests, output path, etc.
     * @return Suite results with all collected metrics
     *
     * Usage:
     * @code
     * BenchmarkSuiteConfig config;
     * config.outputDir = "./results";
     * config.tests = BenchmarkConfigLoader::GetQuickTestMatrix();
     * config.headless = true;
     *
     * BenchmarkRunner runner;
     * auto results = runner.RunSuite(config);
     * std::cout << "Passed: " << results.GetPassCount() << "\n";
     * @endcode
     */
    TestSuiteResults RunSuite(const BenchmarkSuiteConfig& config);

    /**
     * @brief List available GPUs
     *
     * Creates a temporary Vulkan instance to enumerate physical devices.
     * Prints GPU info to stdout. Does not affect runner state.
     */
    static void ListAvailableGPUs();

    //==========================================================================
    // Low-Level API: Manual Test Execution (for custom integrations)
    //==========================================================================

    // Configuration
    /// Load benchmark configuration from JSON file
    /// @param configPath Path to JSON configuration file
    /// @return true if configuration loaded successfully
    bool LoadConfig(const std::filesystem::path& configPath);

    /// Set output directory for results
    void SetOutputDirectory(const std::filesystem::path& path);

    /// Get output directory
    const std::filesystem::path& GetOutputDirectory() const { return outputDirectory_; }

    /// Set device capabilities (must be called before running)
    void SetDeviceCapabilities(const DeviceCapabilities& caps);

    /// Set bandwidth estimation configuration
    void SetBandwidthEstimationConfig(const BandwidthEstimationConfig& config);

    // Test matrix management
    /// Generate test matrix from loaded configuration
    std::vector<TestConfiguration> GenerateTestMatrix() const;

    /// Set test matrix directly (bypasses LoadConfig)
    void SetTestMatrix(const std::vector<TestConfiguration>& matrix);

    /// Get current test matrix
    const std::vector<TestConfiguration>& GetTestMatrix() const { return testMatrix_; }

    // Callbacks
    /// Set callback invoked each frame during measurement
    void SetFrameCallback(FrameCallback callback);

    /// Set progress callback for UI updates
    void SetProgressCallback(ProgressCallback callback);

    // Execution control (hooks for integration - actual rendering is external)
    /// Start benchmark suite execution
    /// @return true if started successfully
    bool StartSuite();

    /// Begin next test in matrix (call from render loop)
    /// @return true if more tests remain
    bool BeginNextTest();

    /// Called each frame during test execution
    /// @param metrics Frame metrics to record
    void RecordFrame(const FrameMetrics& metrics);

    /// Check if current test is complete
    bool IsCurrentTestComplete() const;

    /// Finalize current test and prepare for next
    void FinalizeCurrentTest();

    /// Abort current suite execution
    void AbortSuite();

    // State queries
    BenchmarkState GetState() const { return state_; }
    bool IsRunning() const { return state_ != BenchmarkState::Idle && state_ != BenchmarkState::Completed; }
    uint32_t GetCurrentTestIndex() const { return currentTestIndex_; }
    uint32_t GetCurrentFrameNumber() const { return currentFrame_; }
    const TestConfiguration& GetCurrentTestConfig() const;

    // Results
    /// Get complete suite results
    const TestSuiteResults& GetSuiteResults() const { return suiteResults_; }

    /// Export all results to output directory
    void ExportAllResults();

    /// Export single test results
    void ExportTestResults(const TestRunResults& results, const std::string& filename);

    // Bandwidth estimation (Task 3)
    /// Estimate bandwidth when hardware counters unavailable
    /// @param raysCast Number of rays cast this frame
    /// @param frameTimeSeconds Frame time in seconds
    /// @return Estimated bandwidth in GB/s
    float EstimateBandwidth(uint64_t raysCast, float frameTimeSeconds) const;

    /// Check if hardware performance queries are available
    bool HasHardwarePerformanceCounters() const;

    //==========================================================================
    // Graph Management (Integration with BenchmarkGraphFactory)
    //==========================================================================

    /// Graph factory callback type
    /// @param graph RenderGraph to build into
    /// @param config Current test configuration
    /// @param width Window/render width
    /// @param height Window/render height
    /// @return BenchmarkGraph containing node handles
    using GraphFactoryFunc = std::function<BenchmarkGraph(
        Vixen::RenderGraph::RenderGraph* graph,
        const TestConfiguration& config,
        uint32_t width,
        uint32_t height
    )>;

    /**
     * @brief Set custom graph factory function
     *
     * By default, uses BenchmarkGraphFactory::BuildComputeRayMarchGraph.
     * Override for custom graph construction.
     *
     * @param factory Function to create graphs for tests
     */
    void SetGraphFactory(GraphFactoryFunc factory);

    /**
     * @brief Set render dimensions for graph creation
     *
     * @param width Window/render width in pixels
     * @param height Window/render height in pixels
     */
    void SetRenderDimensions(uint32_t width, uint32_t height);

    /**
     * @brief Create a benchmark graph for the current test configuration
     *
     * Creates graph using the factory function and wires profiler hooks.
     * Call after BeginNextTest() to create graph for current test.
     *
     * @param graph RenderGraph to build into (externally owned)
     * @return BenchmarkGraph with node handles, or empty graph if creation failed
     */
    BenchmarkGraph CreateGraphForCurrentTest(Vixen::RenderGraph::RenderGraph* graph);

    /**
     * @brief Get the profiler adapter for manual hook wiring
     *
     * Use this to access the adapter for frame callbacks in render loop:
     * @code
     * runner.GetAdapter().SetFrameContext(cmdBuffer, frameIndex);
     * runner.GetAdapter().OnFrameBegin();
     * // ... dispatch ...
     * runner.GetAdapter().OnDispatchEnd(dispatchW, dispatchH);
     * runner.GetAdapter().OnFrameEnd();
     * @endcode
     *
     * @return Reference to ProfilerGraphAdapter
     */
    ProfilerGraphAdapter& GetAdapter() { return adapter_; }

    /**
     * @brief Get const reference to adapter
     */
    const ProfilerGraphAdapter& GetAdapter() const { return adapter_; }

    /**
     * @brief Get current benchmark graph structure (if created)
     *
     * @return Current BenchmarkGraph or empty if no graph created
     */
    const BenchmarkGraph& GetCurrentGraph() const { return currentGraph_; }

    /**
     * @brief Check if a graph has been created for current test
     */
    bool HasCurrentGraph() const { return currentGraph_.IsValid(); }

    /**
     * @brief Clear current graph (call before destroying RenderGraph)
     */
    void ClearCurrentGraph();

private:
    // High-level suite execution helpers
    TestSuiteResults RunSuiteHeadless(const BenchmarkSuiteConfig& config);
    TestSuiteResults RunSuiteWithWindow(const BenchmarkSuiteConfig& config);

    // Configuration
    std::filesystem::path configPath_;
    std::filesystem::path outputDirectory_ = "./benchmark_results";
    std::vector<TestConfiguration> testMatrix_;
    DeviceCapabilities deviceCapabilities_;
    BandwidthEstimationConfig bandwidthConfig_;

    // Callbacks
    FrameCallback frameCallback_;
    ProgressCallback progressCallback_;

    // Execution state
    BenchmarkState state_ = BenchmarkState::Idle;
    uint32_t currentTestIndex_ = 0;
    uint32_t currentFrame_ = 0;
    std::chrono::system_clock::time_point suiteStartTime_;
    std::chrono::system_clock::time_point testStartTime_;

    // Current test data
    TestConfiguration currentConfig_;
    std::vector<FrameMetrics> currentFrames_;
    std::map<std::string, RollingStats> currentStats_;

    // Results
    TestSuiteResults suiteResults_;

    // Graph management
    GraphFactoryFunc graphFactory_;
    ProfilerGraphAdapter adapter_;
    BenchmarkGraph currentGraph_;
    uint32_t renderWidth_ = 800;
    uint32_t renderHeight_ = 600;

    // Helpers
    void InitializeStatsTrackers();
    void UpdateStats(const FrameMetrics& metrics);
    std::map<std::string, AggregateStats> ComputeAggregates() const;
    void ReportProgress();

    /// Collect VRAM usage via VK_EXT_memory_budget
    /// @param physicalDevice Vulkan physical device
    /// @param metrics FrameMetrics to populate with VRAM data
    void CollectVRAMUsage(VkPhysicalDevice physicalDevice, FrameMetrics& metrics) const;

    // Frame capture for debugging
    std::unique_ptr<FrameCapture> frameCapture_;
    bool midFrameCaptured_ = false;  // Track if mid-frame capture done for current test
};

} // namespace Vixen::Profiler
