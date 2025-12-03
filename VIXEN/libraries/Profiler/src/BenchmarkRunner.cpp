#include "Profiler/BenchmarkRunner.h"
#include "Profiler/BenchmarkConfig.h"
#include <fstream>
#include <stdexcept>

namespace Vixen::Profiler {

BenchmarkRunner::BenchmarkRunner() = default;
BenchmarkRunner::~BenchmarkRunner() = default;

bool BenchmarkRunner::LoadConfig(const std::filesystem::path& configPath) {
    configPath_ = configPath;
    testMatrix_ = BenchmarkConfigLoader::LoadBatchFromFile(configPath);
    return !testMatrix_.empty();
}

void BenchmarkRunner::SetOutputDirectory(const std::filesystem::path& path) {
    outputDirectory_ = path;
}

void BenchmarkRunner::SetDeviceCapabilities(const DeviceCapabilities& caps) {
    deviceCapabilities_ = caps;
}

void BenchmarkRunner::SetBandwidthEstimationConfig(const BandwidthEstimationConfig& config) {
    bandwidthConfig_ = config;
}

std::vector<TestConfiguration> BenchmarkRunner::GenerateTestMatrix() const {
    return testMatrix_;
}

void BenchmarkRunner::SetTestMatrix(const std::vector<TestConfiguration>& matrix) {
    testMatrix_ = matrix;
}

void BenchmarkRunner::SetFrameCallback(FrameCallback callback) {
    frameCallback_ = std::move(callback);
}

void BenchmarkRunner::SetProgressCallback(ProgressCallback callback) {
    progressCallback_ = std::move(callback);
}

bool BenchmarkRunner::StartSuite() {
    if (testMatrix_.empty()) {
        state_ = BenchmarkState::Error;
        return false;
    }

    // Validate all configurations
    for (const auto& config : testMatrix_) {
        auto errors = config.ValidateWithErrors();
        if (!errors.empty()) {
            state_ = BenchmarkState::Error;
            return false;
        }
    }

    // Ensure output directory exists
    MetricsExporter::EnsureDirectoryExists(outputDirectory_);

    // Initialize suite
    suiteResults_.Clear();
    suiteResults_.SetDeviceCapabilities(deviceCapabilities_);
    suiteResults_.SetStartTime(std::chrono::system_clock::now());
    suiteStartTime_ = std::chrono::system_clock::now();

    currentTestIndex_ = 0;
    state_ = BenchmarkState::Idle;

    return true;
}

bool BenchmarkRunner::BeginNextTest() {
    if (currentTestIndex_ >= testMatrix_.size()) {
        state_ = BenchmarkState::Completed;
        suiteResults_.SetEndTime(std::chrono::system_clock::now());
        return false;
    }

    // Setup current test
    currentConfig_ = testMatrix_[currentTestIndex_];
    currentFrames_.clear();
    currentFrame_ = 0;
    testStartTime_ = std::chrono::system_clock::now();

    // Initialize stats trackers
    InitializeStatsTrackers();

    // Start in warmup phase
    state_ = BenchmarkState::Warmup;

    return true;
}

void BenchmarkRunner::RecordFrame(const FrameMetrics& metrics) {
    if (state_ == BenchmarkState::Idle || state_ == BenchmarkState::Completed ||
        state_ == BenchmarkState::Error) {
        return;
    }

    // Handle warmup phase
    if (state_ == BenchmarkState::Warmup) {
        currentFrame_++;
        if (currentFrame_ >= currentConfig_.warmupFrames) {
            // Transition to measurement
            state_ = BenchmarkState::Measuring;
            currentFrame_ = 0;
        }
        ReportProgress();
        return;
    }

    // Measurement phase
    if (state_ == BenchmarkState::Measuring) {
        // Apply bandwidth estimation if needed
        FrameMetrics adjustedMetrics = metrics;
        if (!HasHardwarePerformanceCounters() && bandwidthConfig_.useEstimation) {
            float estimatedBW = EstimateBandwidth(metrics.totalRaysCast, metrics.frameTimeMs / 1000.0f);
            adjustedMetrics.bandwidthReadGB = estimatedBW;
            adjustedMetrics.bandwidthEstimated = true;
        }

        currentFrames_.push_back(adjustedMetrics);
        UpdateStats(adjustedMetrics);

        // Invoke frame callback
        if (frameCallback_) {
            frameCallback_(currentFrame_);
        }

        currentFrame_++;
        ReportProgress();
    }
}

bool BenchmarkRunner::IsCurrentTestComplete() const {
    return state_ == BenchmarkState::Measuring &&
           currentFrame_ >= currentConfig_.measurementFrames;
}

void BenchmarkRunner::FinalizeCurrentTest() {
    if (currentFrames_.empty()) {
        currentTestIndex_++;
        state_ = BenchmarkState::Idle;
        return;
    }

    // Compute aggregates
    auto aggregates = ComputeAggregates();

    // Create test results
    TestRunResults results;
    results.config = currentConfig_;
    results.frames = std::move(currentFrames_);
    results.aggregates = std::move(aggregates);
    results.startTime = testStartTime_;
    results.endTime = std::chrono::system_clock::now();

    // Add to suite
    suiteResults_.AddTestRun(results);

    // Export individual test results
    std::string filename = currentConfig_.testId.empty()
        ? currentConfig_.GenerateTestId(currentTestIndex_ + 1)
        : currentConfig_.testId;
    ExportTestResults(results, filename + ".json");

    // Prepare for next test
    currentFrames_.clear();
    currentTestIndex_++;
    state_ = BenchmarkState::Idle;
}

void BenchmarkRunner::AbortSuite() {
    state_ = BenchmarkState::Idle;
    currentFrames_.clear();
}

const TestConfiguration& BenchmarkRunner::GetCurrentTestConfig() const {
    static TestConfiguration empty;
    if (currentTestIndex_ < testMatrix_.size()) {
        return currentConfig_;
    }
    return empty;
}

void BenchmarkRunner::ExportAllResults() {
    MetricsExporter exporter;

    // Export each test result
    for (size_t i = 0; i < suiteResults_.GetAllResults().size(); ++i) {
        const auto& result = suiteResults_.GetAllResults()[i];
        std::string filename = result.config.testId.empty()
            ? result.config.GenerateTestId(static_cast<uint32_t>(i + 1))
            : result.config.testId;

        auto filepath = outputDirectory_ / (filename + ".json");
        exporter.ExportToJSON(filepath, result.config, deviceCapabilities_,
                              result.frames, result.aggregates);
    }

    // Export suite summary
    auto summaryPath = outputDirectory_ / "suite_summary.json";
    suiteResults_.ExportSummary(summaryPath.string());
}

void BenchmarkRunner::ExportTestResults(const TestRunResults& results, const std::string& filename) {
    MetricsExporter exporter;
    auto filepath = outputDirectory_ / filename;
    exporter.ExportToJSON(filepath, results.config, deviceCapabilities_,
                          results.frames, results.aggregates);
}

float BenchmarkRunner::EstimateBandwidth(uint64_t raysCast, float frameTimeSeconds) const {
    if (frameTimeSeconds <= 0.0f || raysCast == 0) {
        return 0.0f;
    }

    // Formula: bandwidth_estimate = rays_cast * avg_bytes_per_ray / frame_time_s
    // Result in bytes/second, convert to GB/s
    double bytesTransferred = static_cast<double>(raysCast) * bandwidthConfig_.avgBytesPerRay;
    double bytesPerSecond = bytesTransferred / static_cast<double>(frameTimeSeconds);
    double gbPerSecond = bytesPerSecond / (1024.0 * 1024.0 * 1024.0);

    return static_cast<float>(gbPerSecond);
}

bool BenchmarkRunner::HasHardwarePerformanceCounters() const {
    return deviceCapabilities_.performanceQuerySupported;
}

void BenchmarkRunner::InitializeStatsTrackers() {
    currentStats_.clear();

    // Initialize rolling stats for each metric
    const uint32_t windowSize = currentConfig_.measurementFrames;
    currentStats_["frame_time_ms"] = RollingStats(windowSize);
    currentStats_["fps"] = RollingStats(windowSize);
    currentStats_["bandwidth_read_gb"] = RollingStats(windowSize);
    currentStats_["bandwidth_write_gb"] = RollingStats(windowSize);
    currentStats_["vram_mb"] = RollingStats(windowSize);
    currentStats_["mrays_per_sec"] = RollingStats(windowSize);
}

void BenchmarkRunner::UpdateStats(const FrameMetrics& metrics) {
    currentStats_["frame_time_ms"].AddSample(metrics.frameTimeMs);
    currentStats_["fps"].AddSample(metrics.fps);
    currentStats_["bandwidth_read_gb"].AddSample(metrics.bandwidthReadGB);
    currentStats_["bandwidth_write_gb"].AddSample(metrics.bandwidthWriteGB);
    currentStats_["vram_mb"].AddSample(static_cast<float>(metrics.vramUsageMB));
    currentStats_["mrays_per_sec"].AddSample(metrics.mRaysPerSec);
}

std::map<std::string, AggregateStats> BenchmarkRunner::ComputeAggregates() const {
    std::map<std::string, AggregateStats> aggregates;

    for (const auto& [name, stats] : currentStats_) {
        aggregates[name] = const_cast<RollingStats&>(stats).GetAggregateStats();
    }

    return aggregates;
}

void BenchmarkRunner::ReportProgress() {
    if (progressCallback_) {
        uint32_t totalFrames = currentConfig_.warmupFrames + currentConfig_.measurementFrames;
        uint32_t absoluteFrame = (state_ == BenchmarkState::Warmup)
            ? currentFrame_
            : currentConfig_.warmupFrames + currentFrame_;

        progressCallback_(currentTestIndex_,
                          static_cast<uint32_t>(testMatrix_.size()),
                          absoluteFrame,
                          totalFrames);
    }
}

} // namespace Vixen::Profiler
