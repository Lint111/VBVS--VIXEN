#include "Profiler/ProfilerSystem.h"
#include <nlohmann/json.hpp>
#include <Logger.h>
#include <iostream>
#include <ctime>

namespace Vixen::Profiler {

ProfilerSystem& ProfilerSystem::Instance() {
    static ProfilerSystem instance;
    return instance;
}

void ProfilerSystem::Initialize(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t framesInFlight) {
    if (initialized_) {
        return;
    }

    collector_ = std::make_unique<MetricsCollector>();
    collector_->Initialize(device, physicalDevice, framesInFlight);

    CaptureDeviceCapabilities(physicalDevice);

    initialized_ = true;
}

void ProfilerSystem::Shutdown() {
    if (!initialized_) return;

    if (testRunActive_) {
        EndTestRun(false);
    }

    collector_->Shutdown();
    collector_.reset();

    initialized_ = false;
}

void ProfilerSystem::CaptureDeviceCapabilities(VkPhysicalDevice physicalDevice) {
    deviceCapabilities_ = DeviceCapabilities::Capture(physicalDevice);
}

void ProfilerSystem::StartTestRun(const TestConfiguration& config) {
    if (!initialized_) return;

    if (testRunActive_) {
        EndTestRun(false);
    }

    currentConfig_ = config;
    currentResults_ = TestRunResults{};
    currentResults_.config = config;
    currentResults_.startTime = std::chrono::system_clock::now();

    collector_->Reset();
    collector_->SetWarmupFrames(config.warmupFrames);

    testRunActive_ = true;
    testRunStartTime_ = std::chrono::system_clock::now();
}

void ProfilerSystem::EndTestRun(bool autoExport) {
    if (!testRunActive_) return;

    currentResults_.endTime = std::chrono::system_clock::now();
    FinalizeCurrentResults();

    testRunActive_ = false;

    // Add to test suite
    testSuiteResults_.AddTestRun(currentResults_);

    if (autoExport && autoExportEnabled_) {
        ExportCurrentResults();
    }
}

void ProfilerSystem::OnFrameBegin(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (!testRunActive_ || !collector_) return;
    collector_->OnFrameBegin(cmdBuffer, frameIndex);
}

void ProfilerSystem::OnDispatchBegin(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (!testRunActive_ || !collector_) return;
    collector_->OnDispatchBegin(cmdBuffer, frameIndex);
}

void ProfilerSystem::OnDispatchEnd(VkCommandBuffer cmdBuffer, uint32_t frameIndex,
                                   uint32_t dispatchWidth, uint32_t dispatchHeight) {
    if (!testRunActive_ || !collector_) return;
    collector_->OnDispatchEnd(cmdBuffer, frameIndex, dispatchWidth, dispatchHeight);
}

void ProfilerSystem::OnFrameEnd(uint32_t frameIndex) {
    if (!testRunActive_ || !collector_) return;

    collector_->OnFrameEnd(frameIndex);

    // Store frame metrics (skip warmup)
    if (!collector_->IsWarmingUp()) {
        currentResults_.frames.push_back(collector_->GetLastFrameMetrics());

        // Check if we've collected enough frames
        if (currentResults_.frames.size() >= currentConfig_.measurementFrames) {
            EndTestRun(autoExportEnabled_);
        }
    }
}

void ProfilerSystem::OnPreCleanup() {
    if (!collector_) return;
    collector_->OnPreCleanup();
}

void ProfilerSystem::RegisterExtractor(const std::string& name, NodeMetricsExtractor extractor) {
    if (collector_) {
        collector_->RegisterExtractor(name, std::move(extractor));
    }
}

void ProfilerSystem::UnregisterExtractor(const std::string& name) {
    if (collector_) {
        collector_->UnregisterExtractor(name);
    }
}

const FrameMetrics& ProfilerSystem::GetLastFrameMetrics() const {
    static FrameMetrics empty;
    if (!collector_) return empty;
    return collector_->GetLastFrameMetrics();
}

const RollingStats* ProfilerSystem::GetRollingStats(const std::string& metricName) const {
    if (!collector_) return nullptr;
    return collector_->GetRollingStats(metricName);
}

void ProfilerSystem::SetOutputDirectory(const std::filesystem::path& directory) {
    outputDirectory_ = directory;
}

void ProfilerSystem::SetExportFormats(bool csv, bool json) {
    exportCSV_ = csv;
    exportJSON_ = json;
}

void ProfilerSystem::StartTestSuite(const std::string& suiteName) {
    testSuiteResults_.Clear();
    testSuiteResults_.SetSuiteName(suiteName);
    testSuiteResults_.SetDeviceCapabilities(deviceCapabilities_);
    testSuiteResults_.SetStartTime(std::chrono::system_clock::now());
}

void ProfilerSystem::EndTestSuite() {
    testSuiteResults_.SetEndTime(std::chrono::system_clock::now());

    // Export summary
    auto summaryPath = outputDirectory_ / "suite_summary.json";
    testSuiteResults_.ExportSummary(summaryPath.string());
}

size_t ProfilerSystem::RunTestBatch(
    const std::vector<TestConfiguration>& configs,
    std::function<bool(const TestConfiguration&)> testExecutor) {

    size_t successCount = 0;

    for (size_t i = 0; i < configs.size(); ++i) {
        const auto& config = configs[i];

        // Log progress
        std::cout << "[Profiler] Running test " << (i + 1) << "/" << configs.size()
                  << ": " << config.pipeline << "/" << config.shader << "/"
                  << config.voxelResolution << std::endl;

        StartTestRun(config);

        bool success = testExecutor(config);

        if (success && testRunActive_) {
            // Test completed normally
            successCount++;
        } else if (testRunActive_) {
            // Test failed, end run without export
            EndTestRun(false);
        }
    }

    return successCount;
}

void ProfilerSystem::FinalizeCurrentResults() {
    // Calculate aggregate statistics
    const auto& stats = collector_->GetAllRollingStats();
    for (const auto& [name, rollingStats] : stats) {
        currentResults_.aggregates[name] = rollingStats.GetAggregateStats();
    }
}

void ProfilerSystem::ExportCurrentResults() {
    MetricsExporter::EnsureDirectoryExists(outputDirectory_);

    if (exportCSV_) {
        auto csvPath = outputDirectory_ /
                       MetricsExporter::GetDefaultFilename(currentConfig_, ExportFormat::CSV);
        exporter_.ExportToCSV(csvPath, currentConfig_, deviceCapabilities_,
                              currentResults_.frames, currentResults_.aggregates);
    }

    if (exportJSON_) {
        auto jsonPath = outputDirectory_ /
                        MetricsExporter::GetDefaultFilename(currentConfig_, ExportFormat::JSON);
        exporter_.ExportToJSON(jsonPath, currentConfig_, deviceCapabilities_,
                               currentResults_.frames, currentResults_.aggregates);
    }
}

// TestSuiteResults implementation
double TestRunResults::GetDurationSeconds() const {
    auto duration = endTime - startTime;
    return std::chrono::duration<double>(duration).count();
}

void TestSuiteResults::SetDeviceCapabilities(const DeviceCapabilities& capabilities) {
    deviceCapabilities_ = capabilities;
}

void TestSuiteResults::AddTestRun(const TestRunResults& results) {
    results_.push_back(results);
}

double TestSuiteResults::GetTotalDurationSeconds() const {
    double total = 0.0;
    for (const auto& result : results_) {
        total += result.GetDurationSeconds();
    }
    return total;
}

void TestSuiteResults::ExportSummary(const std::string& filepath) const {
    nlohmann::json j;

    // Suite metadata
    j["suite_name"] = suiteName_;
    j["total_tests"] = results_.size();
    j["passed_tests"] = GetPassCount();
    j["total_duration_seconds"] = GetTotalDurationSeconds();

    // Timestamp info
    auto startTimeT = std::chrono::system_clock::to_time_t(suiteStartTime_);
    auto endTimeT = std::chrono::system_clock::to_time_t(suiteEndTime_);
    char startBuf[64], endBuf[64];
    std::strftime(startBuf, sizeof(startBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&startTimeT));
    std::strftime(endBuf, sizeof(endBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&endTimeT));
    j["start_time"] = startBuf;
    j["end_time"] = endBuf;

    // Device info (comprehensive)
    j["device"]["name"] = deviceCapabilities_.deviceName;
    j["device"]["vendor_id"] = deviceCapabilities_.vendorID;
    j["device"]["device_id"] = deviceCapabilities_.deviceID;
    j["device"]["driver"] = deviceCapabilities_.driverVersion;
    j["device"]["vulkan"] = deviceCapabilities_.vulkanVersion;
    j["device"]["vram_mb"] = deviceCapabilities_.totalVRAM_MB;
    j["device"]["timestamp_period_ns"] = deviceCapabilities_.timestampPeriod;
    j["device"]["timestamp_supported"] = deviceCapabilities_.timestampSupported;

    // Helper lambda to convert AggregateStats to JSON
    auto statsToJson = [](const AggregateStats& stats) -> nlohmann::json {
        nlohmann::json s;
        s["min"] = stats.min;
        s["max"] = stats.max;
        s["mean"] = stats.mean;
        s["stddev"] = stats.stddev;
        s["p1"] = stats.p1;
        s["p50"] = stats.p50;
        s["p99"] = stats.p99;
        s["samples"] = stats.sampleCount;
        return s;
    };

    // Full test results with all accumulated metrics
    nlohmann::json tests = nlohmann::json::array();
    for (const auto& result : results_) {
        nlohmann::json t;

        // Test configuration
        t["config"]["test_id"] = result.config.testId;
        t["config"]["pipeline"] = result.config.pipeline;
        t["config"]["shader"] = result.config.shader;
        t["config"]["scene"] = result.config.sceneType;
        t["config"]["resolution"] = result.config.voxelResolution;
        t["config"]["screen_width"] = result.config.screenWidth;
        t["config"]["screen_height"] = result.config.screenHeight;
        t["config"]["warmup_frames"] = result.config.warmupFrames;
        t["config"]["measurement_frames"] = result.config.measurementFrames;

        // Test metadata
        t["frames_collected"] = result.frames.size();
        t["duration_seconds"] = result.GetDurationSeconds();
        t["valid"] = result.IsValid();

        // All aggregate statistics
        nlohmann::json aggregates;
        for (const auto& [metricName, stats] : result.aggregates) {
            aggregates[metricName] = statsToJson(stats);
        }
        t["aggregates"] = aggregates;

        // Summary metrics (for quick access)
        if (result.aggregates.count("frame_time_ms")) {
            t["summary"]["frame_time_mean_ms"] = result.aggregates.at("frame_time_ms").mean;
            t["summary"]["frame_time_p99_ms"] = result.aggregates.at("frame_time_ms").p99;
        }
        if (result.aggregates.count("fps")) {
            t["summary"]["fps_mean"] = result.aggregates.at("fps").mean;
        }
        if (result.aggregates.count("mrays_per_sec")) {
            t["summary"]["mrays_mean"] = result.aggregates.at("mrays_per_sec").mean;
        }
        if (result.aggregates.count("bandwidth_write_gb")) {
            t["summary"]["bandwidth_write_gbps"] = result.aggregates.at("bandwidth_write_gb").mean;
        }
        if (result.aggregates.count("vram_mb")) {
            t["summary"]["vram_usage_mb"] = result.aggregates.at("vram_mb").mean;
        }

        // Calculate total rays from last frame (screen dimensions)
        if (!result.frames.empty()) {
            const auto& lastFrame = result.frames.back();
            t["summary"]["total_rays_per_frame"] = lastFrame.totalRaysCast;
        }

        tests.push_back(t);
    }
    j["tests"] = tests;

    std::ofstream file(filepath);
    if (file.is_open()) {
        file << j.dump(2);
    }
}

void TestSuiteResults::Clear() {
    results_.clear();
    suiteName_ = "Benchmark Suite";
}

} // namespace Vixen::Profiler
