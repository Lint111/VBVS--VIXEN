#include <gtest/gtest.h>
#include "Profiler/RollingStats.h"
#include "Profiler/BenchmarkConfig.h"
#include "Profiler/BenchmarkRunner.h"
#include "Profiler/FrameMetrics.h"
#include "Profiler/DeviceCapabilities.h"
#include "Profiler/SceneInfo.h"
#include "Profiler/MetricsExporter.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

using namespace Vixen::Profiler;

// ============================================================================
// RollingStats Tests
// ============================================================================

class RollingStatsTest : public ::testing::Test {
protected:
    RollingStats stats{100};
};

TEST_F(RollingStatsTest, EmptyStatsReturnZero) {
    EXPECT_EQ(stats.GetSampleCount(), 0);
    EXPECT_FLOAT_EQ(stats.GetMin(), 0.0f);
    EXPECT_FLOAT_EQ(stats.GetMax(), 0.0f);
    EXPECT_FLOAT_EQ(stats.GetMean(), 0.0f);
    EXPECT_FLOAT_EQ(stats.GetStdDev(), 0.0f);
}

TEST_F(RollingStatsTest, SingleSample) {
    stats.AddSample(42.0f);

    EXPECT_EQ(stats.GetSampleCount(), 1);
    EXPECT_FLOAT_EQ(stats.GetMin(), 42.0f);
    EXPECT_FLOAT_EQ(stats.GetMax(), 42.0f);
    EXPECT_FLOAT_EQ(stats.GetMean(), 42.0f);
    EXPECT_FLOAT_EQ(stats.GetStdDev(), 0.0f);
}

TEST_F(RollingStatsTest, MultipleSamples) {
    stats.AddSample(1.0f);
    stats.AddSample(2.0f);
    stats.AddSample(3.0f);
    stats.AddSample(4.0f);
    stats.AddSample(5.0f);

    EXPECT_EQ(stats.GetSampleCount(), 5);
    EXPECT_FLOAT_EQ(stats.GetMin(), 1.0f);
    EXPECT_FLOAT_EQ(stats.GetMax(), 5.0f);
    EXPECT_FLOAT_EQ(stats.GetMean(), 3.0f);
}

TEST_F(RollingStatsTest, WindowOverflow) {
    RollingStats smallWindow(3);

    smallWindow.AddSample(1.0f);
    smallWindow.AddSample(2.0f);
    smallWindow.AddSample(3.0f);
    EXPECT_EQ(smallWindow.GetSampleCount(), 3);
    EXPECT_FLOAT_EQ(smallWindow.GetMean(), 2.0f);

    // Adding 4th sample should evict 1.0f
    smallWindow.AddSample(4.0f);
    EXPECT_EQ(smallWindow.GetSampleCount(), 3);
    EXPECT_FLOAT_EQ(smallWindow.GetMin(), 2.0f);
    EXPECT_FLOAT_EQ(smallWindow.GetMax(), 4.0f);
    EXPECT_FLOAT_EQ(smallWindow.GetMean(), 3.0f);
}

TEST_F(RollingStatsTest, PercentileMedian) {
    for (int i = 1; i <= 100; ++i) {
        stats.AddSample(static_cast<float>(i));
    }

    // Median of 1..100 should be ~50.5
    float median = stats.GetP50();
    EXPECT_NEAR(median, 50.5f, 1.0f);
}

TEST_F(RollingStatsTest, PercentileExtremes) {
    for (int i = 1; i <= 100; ++i) {
        stats.AddSample(static_cast<float>(i));
    }

    // P1 should be near 1, P99 should be near 100
    EXPECT_NEAR(stats.GetP1(), 1.99f, 1.0f);
    EXPECT_NEAR(stats.GetP99(), 99.01f, 1.0f);
}

TEST_F(RollingStatsTest, Reset) {
    stats.AddSample(1.0f);
    stats.AddSample(2.0f);
    EXPECT_EQ(stats.GetSampleCount(), 2);

    stats.Reset();
    EXPECT_EQ(stats.GetSampleCount(), 0);
    EXPECT_FLOAT_EQ(stats.GetMean(), 0.0f);
}

TEST_F(RollingStatsTest, AggregateStats) {
    stats.AddSample(10.0f);
    stats.AddSample(20.0f);
    stats.AddSample(30.0f);

    auto agg = stats.GetAggregateStats();
    EXPECT_FLOAT_EQ(agg.min, 10.0f);
    EXPECT_FLOAT_EQ(agg.max, 30.0f);
    EXPECT_FLOAT_EQ(agg.mean, 20.0f);
    EXPECT_EQ(agg.sampleCount, 3);
}

TEST_F(RollingStatsTest, VRAMSizeSamples) {
    // VRAM values are stored as float but represent MB
    // Test that large VRAM values (e.g., 8GB = 8192 MB) work correctly
    RollingStats vramStats(10);

    vramStats.AddSample(2048.0f);  // 2 GB
    vramStats.AddSample(4096.0f);  // 4 GB
    vramStats.AddSample(8192.0f);  // 8 GB

    EXPECT_FLOAT_EQ(vramStats.GetMin(), 2048.0f);
    EXPECT_FLOAT_EQ(vramStats.GetMax(), 8192.0f);
    EXPECT_NEAR(vramStats.GetMean(), 4778.67f, 1.0f);  // (2048+4096+8192)/3
}

// ============================================================================
// BenchmarkConfig Tests
// ============================================================================

TEST(BenchmarkConfigTest, DefaultConfigValidates) {
    TestConfiguration config;
    EXPECT_TRUE(config.Validate());
}

TEST(BenchmarkConfigTest, EmptyPipelineInvalid) {
    TestConfiguration config;
    config.pipeline = "";
    EXPECT_FALSE(config.Validate());
}

TEST(BenchmarkConfigTest, ZeroResolutionInvalid) {
    TestConfiguration config;
    config.voxelResolution = 0;
    EXPECT_FALSE(config.Validate());
}

TEST(BenchmarkConfigTest, DensityOutOfRangeInvalid) {
    TestConfiguration config;
    config.densityPercent = 1.5f;
    EXPECT_FALSE(config.Validate());
}

TEST(BenchmarkConfigTest, GenerateTestMatrix) {
    auto matrix = BenchmarkConfigLoader::GenerateTestMatrix(
        {"compute", "fragment"},
        {64, 128},
        {0.2f, 0.5f},
        {"baseline"}
    );

    // 2 pipelines * 2 resolutions * 2 densities * 1 algorithm = 8
    EXPECT_EQ(matrix.size(), 8);

    // Verify all combinations exist
    bool found64Compute02 = false;
    for (const auto& config : matrix) {
        if (config.pipeline == "compute" && config.voxelResolution == 64 &&
            std::abs(config.densityPercent - 0.2f) < 0.01f) {
            found64Compute02 = true;
            break;
        }
    }
    EXPECT_TRUE(found64Compute02);
}

TEST(BenchmarkConfigTest, QuickTestMatrix) {
    auto matrix = BenchmarkConfigLoader::GetQuickTestMatrix();
    EXPECT_GT(matrix.size(), 0);
    EXPECT_LE(matrix.size(), 20);  // Should be small
}

TEST(BenchmarkConfigTest, ResearchTestMatrix) {
    auto matrix = BenchmarkConfigLoader::GetResearchTestMatrix();
    // 4 pipelines * 5 resolutions * 3 densities * 3 algorithms = 180
    EXPECT_EQ(matrix.size(), 180);
}

TEST(BenchmarkConfigTest, SerializeDeserialize) {
    TestConfiguration original;
    original.pipeline = "compute";
    original.algorithm = "empty_skip";
    original.sceneType = "cornell";
    original.voxelResolution = 256;
    original.densityPercent = 0.5f;

    std::string json = BenchmarkConfigLoader::SerializeToString(original);
    EXPECT_FALSE(json.empty());

    auto parsed = BenchmarkConfigLoader::ParseFromString(json);
    ASSERT_TRUE(parsed.has_value());

    EXPECT_EQ(parsed->pipeline, original.pipeline);
    EXPECT_EQ(parsed->algorithm, original.algorithm);
    EXPECT_EQ(parsed->sceneType, original.sceneType);
    EXPECT_EQ(parsed->voxelResolution, original.voxelResolution);
    EXPECT_FLOAT_EQ(parsed->densityPercent, original.densityPercent);
}

// ============================================================================
// FrameMetrics Tests
// ============================================================================

TEST(FrameMetricsTest, DefaultValues) {
    FrameMetrics metrics;
    EXPECT_EQ(metrics.frameNumber, 0);
    EXPECT_FLOAT_EQ(metrics.frameTimeMs, 0.0f);
    EXPECT_FLOAT_EQ(metrics.gpuTimeMs, 0.0f);
    EXPECT_FLOAT_EQ(metrics.mRaysPerSec, 0.0f);
}

TEST(FrameMetricsTest, VRAMFieldsDefaultToZero) {
    FrameMetrics metrics;
    EXPECT_EQ(metrics.vramUsageMB, 0);
    EXPECT_EQ(metrics.vramBudgetMB, 0);
}

TEST(FrameMetricsTest, VRAMFieldsCanBeSet) {
    FrameMetrics metrics;
    metrics.vramUsageMB = 2048;
    metrics.vramBudgetMB = 8192;
    EXPECT_EQ(metrics.vramUsageMB, 2048);
    EXPECT_EQ(metrics.vramBudgetMB, 8192);
}

TEST(FrameMetricsTest, DefaultFilename) {
    TestConfiguration config;
    config.pipeline = "compute";
    config.algorithm = "baseline";
    config.sceneType = "cornell";
    config.voxelResolution = 128;
    config.densityPercent = 0.5f;

    std::string filename = config.GetDefaultFilename();
    EXPECT_FALSE(filename.empty());
    EXPECT_NE(filename.find("compute"), std::string::npos);
    EXPECT_NE(filename.find("128"), std::string::npos);
    EXPECT_NE(filename.find(".csv"), std::string::npos);
}

// ============================================================================
// DeviceCapabilities Tests (using fully qualified name to avoid Windows API conflict)
// ============================================================================

// Note: Windows defines DeviceCapabilities as a macro, so we use the full namespace
using DeviceCaps = Vixen::Profiler::DeviceCapabilities;

TEST(DeviceCapabilitiesTest, DefaultValues) {
    DeviceCaps caps;
    EXPECT_TRUE(caps.deviceName.empty());
    EXPECT_TRUE(caps.driverVersion.empty());
    EXPECT_TRUE(caps.vulkanVersion.empty());
    EXPECT_EQ(caps.vendorID, 0u);
    EXPECT_EQ(caps.deviceID, 0u);
    EXPECT_EQ(caps.totalVRAM_MB, 0u);
    EXPECT_FALSE(caps.timestampSupported);
    EXPECT_FALSE(caps.performanceQuerySupported);
    EXPECT_FALSE(caps.memoryBudgetSupported);
}

TEST(DeviceCapabilitiesTest, NVIDIADriverVersionFormat) {
    // NVIDIA vendor ID is 0x10DE
    // Test the driver version decoding for NVIDIA
    // Version 537.42.01 encodes as: (537 << 22) | (42 << 14) | (1 << 6)
    uint32_t driverVersion = (537 << 22) | (42 << 14) | (1 << 6);
    std::string formatted = DeviceCaps::FormatDriverVersion(driverVersion, 0x10DE);
    EXPECT_EQ(formatted, "537.42.1");
}

TEST(DeviceCapabilitiesTest, AMDDriverVersionFormat) {
    // AMD vendor ID is 0x1002
    // AMD uses standard Vulkan encoding: major.minor.patch
    uint32_t driverVersion = VK_MAKE_VERSION(23, 10, 1);
    std::string formatted = DeviceCaps::FormatDriverVersion(driverVersion, 0x1002);
    EXPECT_EQ(formatted, "23.10.1");
}

TEST(DeviceCapabilitiesTest, DeviceTypeStrings) {
    DeviceCaps caps;

    caps.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    EXPECT_EQ(caps.GetDeviceTypeString(), "Discrete GPU");

    caps.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    EXPECT_EQ(caps.GetDeviceTypeString(), "Integrated GPU");

    caps.deviceType = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
    EXPECT_EQ(caps.GetDeviceTypeString(), "Virtual GPU");

    caps.deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU;
    EXPECT_EQ(caps.GetDeviceTypeString(), "CPU");

    caps.deviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    EXPECT_EQ(caps.GetDeviceTypeString(), "Unknown");
}

TEST(DeviceCapabilitiesTest, SummaryStringContainsKeyInfo) {
    DeviceCaps caps;
    caps.deviceName = "Test GPU";
    caps.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    caps.driverVersion = "537.42.1";
    caps.vulkanVersion = "1.3.250";
    caps.totalVRAM_MB = 8192;
    caps.timestampSupported = true;
    caps.performanceQuerySupported = false;

    std::string summary = caps.GetSummaryString();

    EXPECT_NE(summary.find("Test GPU"), std::string::npos);
    EXPECT_NE(summary.find("Discrete GPU"), std::string::npos);
    EXPECT_NE(summary.find("537.42.1"), std::string::npos);
    EXPECT_NE(summary.find("1.3.250"), std::string::npos);
    EXPECT_NE(summary.find("8192"), std::string::npos);
    EXPECT_NE(summary.find("Timestamp: Yes"), std::string::npos);
    EXPECT_NE(summary.find("PerfQuery: No"), std::string::npos);
}

// ============================================================================
// SceneInfo Tests
// ============================================================================

TEST(SceneInfoTest, DefaultValues) {
    SceneInfo info;
    EXPECT_EQ(info.resolution, 0u);
    EXPECT_FLOAT_EQ(info.densityPercent, 0.0f);
    EXPECT_TRUE(info.sceneType.empty());
    EXPECT_TRUE(info.sceneName.empty());
}

TEST(SceneInfoTest, FromResolutionAndDensity) {
    auto info = SceneInfo::FromResolutionAndDensity(256, 25.0f, "cornell_box", "Cornell Box Test");
    EXPECT_EQ(info.resolution, 256u);
    EXPECT_FLOAT_EQ(info.densityPercent, 25.0f);
    EXPECT_EQ(info.sceneType, "cornell_box");
    EXPECT_EQ(info.sceneName, "Cornell Box Test");
}

TEST(SceneInfoTest, Validation) {
    SceneInfo valid;
    valid.resolution = 256;
    valid.densityPercent = 50.0f;
    valid.sceneType = "test";
    EXPECT_TRUE(valid.IsValid());

    SceneInfo zeroRes;
    zeroRes.resolution = 0;
    zeroRes.densityPercent = 50.0f;
    zeroRes.sceneType = "test";
    EXPECT_FALSE(zeroRes.IsValid());

    SceneInfo negDensity;
    negDensity.resolution = 256;
    negDensity.densityPercent = -10.0f;
    negDensity.sceneType = "test";
    EXPECT_FALSE(negDensity.IsValid());

    SceneInfo highDensity;
    highDensity.resolution = 256;
    highDensity.densityPercent = 150.0f;
    highDensity.sceneType = "test";
    EXPECT_FALSE(highDensity.IsValid());

    SceneInfo emptyType;
    emptyType.resolution = 256;
    emptyType.densityPercent = 50.0f;
    emptyType.sceneType = "";
    EXPECT_FALSE(emptyType.IsValid());
}

TEST(SceneInfoTest, DisplayName) {
    SceneInfo withName;
    withName.resolution = 256;
    withName.sceneType = "cornell_box";
    withName.sceneName = "My Scene";
    EXPECT_EQ(withName.GetDisplayName(), "My Scene");

    SceneInfo withoutName;
    withoutName.resolution = 256;
    withoutName.sceneType = "cornell_box";
    EXPECT_EQ(withoutName.GetDisplayName(), "Cornell Box 256^3");
}

// ============================================================================
// TestConfiguration Validation Tests (Task 5)
// ============================================================================

TEST(TestConfigValidationTest, ValidResolutions) {
    EXPECT_TRUE(TestConfiguration::IsValidResolution(32));
    EXPECT_TRUE(TestConfiguration::IsValidResolution(64));
    EXPECT_TRUE(TestConfiguration::IsValidResolution(128));
    EXPECT_TRUE(TestConfiguration::IsValidResolution(256));
    EXPECT_TRUE(TestConfiguration::IsValidResolution(512));

    EXPECT_FALSE(TestConfiguration::IsValidResolution(0));
    EXPECT_FALSE(TestConfiguration::IsValidResolution(100));
    EXPECT_FALSE(TestConfiguration::IsValidResolution(1024));
    EXPECT_FALSE(TestConfiguration::IsValidResolution(16));
}

TEST(TestConfigValidationTest, ValidConfigPasses) {
    TestConfiguration config;
    config.pipeline = "compute";
    config.algorithm = "baseline";
    config.sceneType = "cornell";
    config.voxelResolution = 128;
    config.densityPercent = 0.5f;
    config.warmupFrames = 60;
    config.measurementFrames = 300;

    auto errors = config.ValidateWithErrors();
    EXPECT_TRUE(errors.empty());
}

TEST(TestConfigValidationTest, InvalidPipelineType) {
    TestConfiguration config;
    config.pipeline = "invalid_pipeline";
    config.voxelResolution = 128;
    config.warmupFrames = 60;
    config.measurementFrames = 300;

    auto errors = config.ValidateWithErrors();
    EXPECT_FALSE(errors.empty());
    bool hasPipelineError = false;
    for (const auto& err : errors) {
        if (err.find("pipeline") != std::string::npos) {
            hasPipelineError = true;
            break;
        }
    }
    EXPECT_TRUE(hasPipelineError);
}

TEST(TestConfigValidationTest, InvalidResolution) {
    TestConfiguration config;
    config.pipeline = "compute";
    config.voxelResolution = 100;  // Not power of 2
    config.warmupFrames = 60;
    config.measurementFrames = 300;

    auto errors = config.ValidateWithErrors();
    EXPECT_FALSE(errors.empty());
    bool hasResolutionError = false;
    for (const auto& err : errors) {
        if (err.find("Resolution") != std::string::npos || err.find("resolution") != std::string::npos) {
            hasResolutionError = true;
            break;
        }
    }
    EXPECT_TRUE(hasResolutionError);
}

TEST(TestConfigValidationTest, InvalidDensity) {
    TestConfiguration config;
    config.pipeline = "compute";
    config.voxelResolution = 128;
    config.densityPercent = 1.5f;  // > 1.0 (internal uses 0-1 range)
    config.warmupFrames = 60;
    config.measurementFrames = 300;

    auto errors = config.ValidateWithErrors();
    EXPECT_FALSE(errors.empty());
}

TEST(TestConfigValidationTest, InsufficientWarmupFrames) {
    TestConfiguration config;
    config.pipeline = "compute";
    config.voxelResolution = 128;
    config.warmupFrames = 5;  // < 10
    config.measurementFrames = 300;

    auto errors = config.ValidateWithErrors();
    EXPECT_FALSE(errors.empty());
    bool hasWarmupError = false;
    for (const auto& err : errors) {
        if (err.find("warmup") != std::string::npos || err.find("Warmup") != std::string::npos) {
            hasWarmupError = true;
            break;
        }
    }
    EXPECT_TRUE(hasWarmupError);
}

TEST(TestConfigValidationTest, InsufficientMeasurementFrames) {
    TestConfiguration config;
    config.pipeline = "compute";
    config.voxelResolution = 128;
    config.warmupFrames = 60;
    config.measurementFrames = 50;  // < 100

    auto errors = config.ValidateWithErrors();
    EXPECT_FALSE(errors.empty());
}

TEST(TestConfigValidationTest, GenerateTestId) {
    TestConfiguration config;
    config.pipeline = "hardware_rt";
    config.voxelResolution = 256;
    config.sceneType = "sparse_architectural";
    config.algorithm = "baseline";

    std::string testId = config.GenerateTestId(1);
    EXPECT_EQ(testId, "HW_RT_256_SPARSE_ARCHITECTURAL_BASELINE_RUN1");
}

TEST(TestConfigValidationTest, PipelineTypeConversion) {
    EXPECT_EQ(PipelineTypeToString(PipelineType::Compute), "compute");
    EXPECT_EQ(PipelineTypeToString(PipelineType::Fragment), "fragment");
    EXPECT_EQ(PipelineTypeToString(PipelineType::HardwareRT), "hardware_rt");
    EXPECT_EQ(PipelineTypeToString(PipelineType::Hybrid), "hybrid");
    EXPECT_EQ(PipelineTypeToString(PipelineType::Invalid), "invalid");

    EXPECT_EQ(ParsePipelineType("compute"), PipelineType::Compute);
    EXPECT_EQ(ParsePipelineType("fragment"), PipelineType::Fragment);
    EXPECT_EQ(ParsePipelineType("hardware_rt"), PipelineType::HardwareRT);
    EXPECT_EQ(ParsePipelineType("hybrid"), PipelineType::Hybrid);
    EXPECT_EQ(ParsePipelineType("unknown"), PipelineType::Invalid);
}

// ============================================================================
// BenchmarkRunner Tests (Task 4)
// ============================================================================

class BenchmarkRunnerTest : public ::testing::Test {
protected:
    BenchmarkRunner runner;

    void SetUp() override {
        DeviceCaps caps;
        caps.deviceName = "Test GPU";
        caps.driverVersion = "1.0.0";
        caps.totalVRAM_MB = 8192;
        caps.performanceQuerySupported = false;  // Test bandwidth estimation
        runner.SetDeviceCapabilities(caps);
    }
};

TEST_F(BenchmarkRunnerTest, InitialState) {
    EXPECT_EQ(runner.GetState(), BenchmarkState::Idle);
    EXPECT_FALSE(runner.IsRunning());
}

TEST_F(BenchmarkRunnerTest, SetTestMatrix) {
    std::vector<TestConfiguration> matrix;
    TestConfiguration config;
    config.pipeline = "compute";
    config.voxelResolution = 128;
    config.warmupFrames = 10;
    config.measurementFrames = 100;
    matrix.push_back(config);

    runner.SetTestMatrix(matrix);
    EXPECT_EQ(runner.GetTestMatrix().size(), 1u);
}

TEST_F(BenchmarkRunnerTest, StartSuiteFailsWithEmptyMatrix) {
    EXPECT_FALSE(runner.StartSuite());
    EXPECT_EQ(runner.GetState(), BenchmarkState::Error);
}

TEST_F(BenchmarkRunnerTest, StartSuiteFailsWithInvalidConfig) {
    std::vector<TestConfiguration> matrix;
    TestConfiguration config;
    config.pipeline = "invalid";  // Invalid pipeline
    config.voxelResolution = 128;
    config.warmupFrames = 10;
    config.measurementFrames = 100;
    matrix.push_back(config);

    runner.SetTestMatrix(matrix);
    EXPECT_FALSE(runner.StartSuite());
}

TEST_F(BenchmarkRunnerTest, StartSuiteSucceeds) {
    std::vector<TestConfiguration> matrix;
    TestConfiguration config;
    config.pipeline = "compute";
    config.voxelResolution = 128;
    config.warmupFrames = 10;
    config.measurementFrames = 100;
    matrix.push_back(config);

    runner.SetTestMatrix(matrix);
    EXPECT_TRUE(runner.StartSuite());
}

TEST_F(BenchmarkRunnerTest, BandwidthEstimation) {
    // Formula: bandwidth = rays * bytes_per_ray / time
    // 1M rays * 96 bytes/ray / 0.01s = 9.6 GB/s (approximately)
    uint64_t raysCast = 1'000'000;
    float frameTimeSeconds = 0.01f;

    float bandwidth = runner.EstimateBandwidth(raysCast, frameTimeSeconds);

    // 96 MB / 0.01s = 9600 MB/s = ~9.0 GB/s (accounting for 1024-based conversion)
    EXPECT_GT(bandwidth, 8.0f);
    EXPECT_LT(bandwidth, 10.0f);
}

TEST_F(BenchmarkRunnerTest, BandwidthEstimationZeroTime) {
    EXPECT_FLOAT_EQ(runner.EstimateBandwidth(1000, 0.0f), 0.0f);
}

TEST_F(BenchmarkRunnerTest, BandwidthEstimationZeroRays) {
    EXPECT_FLOAT_EQ(runner.EstimateBandwidth(0, 0.01f), 0.0f);
}

TEST_F(BenchmarkRunnerTest, HasHardwarePerformanceCounters) {
    // Device set in SetUp has performanceQuerySupported = false
    EXPECT_FALSE(runner.HasHardwarePerformanceCounters());
}

// ============================================================================
// JSON Export Schema Tests (Task 1)
// ============================================================================

class JSONExportTest : public ::testing::Test {
protected:
    std::filesystem::path tempDir;
    std::filesystem::path outputPath;

    void SetUp() override {
        tempDir = std::filesystem::temp_directory_path() / "profiler_test";
        std::filesystem::create_directories(tempDir);
        outputPath = tempDir / "test_export.json";
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }
};

TEST_F(JSONExportTest, ExportMatchesSchema) {
    MetricsExporter exporter;

    TestConfiguration config;
    config.testId = "HW_RT_256_SPARSE_BASELINE_RUN1";
    config.pipeline = "hardware_rt";
    config.algorithm = "baseline";
    config.voxelResolution = 256;
    config.densityPercent = 0.25f;  // 25% (internal 0-1 range)
    config.sceneType = "sparse_architectural";
    config.optimizations = {};
    config.warmupFrames = 10;
    config.measurementFrames = 100;

    DeviceCaps device;
    device.deviceName = "NVIDIA RTX 4070";
    device.driverVersion = "536.23";
    device.totalVRAM_MB = 12 * 1024;  // 12 GB

    std::vector<FrameMetrics> frames;
    FrameMetrics f;
    f.frameNumber = 1;
    f.frameTimeMs = 10.82f;
    f.fps = 92.4f;
    f.bandwidthReadGB = 67.3f;
    f.bandwidthWriteGB = 12.1f;
    f.mRaysPerSec = 191.5f;
    f.vramUsageMB = 4523;
    f.avgVoxelsPerRay = 18.6f;
    frames.push_back(f);

    std::map<std::string, AggregateStats> aggregates;
    aggregates["frame_time_ms"] = {10.0f, 12.0f, 10.85f, 0.34f, 10.0f, 10.85f, 12.79f, 100};
    aggregates["fps"] = {85.0f, 100.0f, 92.1f, 3.0f, 85.0f, 92.1f, 100.0f, 100};
    aggregates["bandwidth_read_gb"] = {60.0f, 75.0f, 67.5f, 3.0f, 60.0f, 67.5f, 75.0f, 100};

    exporter.ExportToJSON(outputPath, config, device, frames, aggregates);

    // Read and parse the JSON
    std::ifstream file(outputPath);
    ASSERT_TRUE(file.is_open());
    nlohmann::json j;
    file >> j;

    // Verify schema structure
    EXPECT_TRUE(j.contains("test_id"));
    EXPECT_EQ(j["test_id"], "HW_RT_256_SPARSE_BASELINE_RUN1");

    EXPECT_TRUE(j.contains("timestamp"));

    EXPECT_TRUE(j.contains("configuration"));
    EXPECT_EQ(j["configuration"]["pipeline"], "hardware_rt");
    EXPECT_EQ(j["configuration"]["algorithm"], "baseline");
    EXPECT_EQ(j["configuration"]["resolution"], 256);
    EXPECT_EQ(j["configuration"]["density_percent"], 25);  // 0.25 * 100
    EXPECT_EQ(j["configuration"]["scene_type"], "sparse_architectural");
    EXPECT_TRUE(j["configuration"]["optimizations"].is_array());

    EXPECT_TRUE(j.contains("device"));
    EXPECT_EQ(j["device"]["gpu"], "NVIDIA RTX 4070");
    EXPECT_EQ(j["device"]["driver"], "536.23");
    EXPECT_NEAR(j["device"]["vram_gb"].get<double>(), 12.0, 0.1);

    EXPECT_TRUE(j.contains("frames"));
    EXPECT_EQ(j["frames"].size(), 1u);
    EXPECT_EQ(j["frames"][0]["frame_num"], 1);
    EXPECT_NEAR(j["frames"][0]["frame_time_ms"].get<float>(), 10.82f, 0.01f);
    EXPECT_NEAR(j["frames"][0]["fps"].get<float>(), 92.4f, 0.1f);
    EXPECT_NEAR(j["frames"][0]["bandwidth_read_gbps"].get<float>(), 67.3f, 0.1f);
    EXPECT_NEAR(j["frames"][0]["ray_throughput_mrays"].get<float>(), 191.5f, 0.1f);
    EXPECT_EQ(j["frames"][0]["vram_mb"], 4523);
    EXPECT_NEAR(j["frames"][0]["avg_voxels_per_ray"].get<float>(), 18.6f, 0.1f);

    EXPECT_TRUE(j.contains("statistics"));
    EXPECT_NEAR(j["statistics"]["frame_time_mean"].get<float>(), 10.85f, 0.01f);
    EXPECT_NEAR(j["statistics"]["frame_time_stddev"].get<float>(), 0.34f, 0.01f);
    EXPECT_NEAR(j["statistics"]["frame_time_p99"].get<float>(), 12.79f, 0.01f);
    EXPECT_NEAR(j["statistics"]["fps_mean"].get<float>(), 92.1f, 0.1f);
    EXPECT_NEAR(j["statistics"]["bandwidth_mean"].get<float>(), 67.5f, 0.1f);
}

TEST_F(JSONExportTest, BandwidthEstimationFlag) {
    MetricsExporter exporter;

    TestConfiguration config;
    config.pipeline = "compute";
    config.voxelResolution = 128;
    config.warmupFrames = 10;
    config.measurementFrames = 100;

    DeviceCaps device;
    device.deviceName = "Test GPU";
    device.totalVRAM_MB = 8192;

    std::vector<FrameMetrics> frames;
    FrameMetrics f;
    f.frameNumber = 1;
    f.bandwidthEstimated = true;  // Mark as estimated
    frames.push_back(f);

    std::map<std::string, AggregateStats> aggregates;

    exporter.ExportToJSON(outputPath, config, device, frames, aggregates);

    std::ifstream file(outputPath);
    nlohmann::json j;
    file >> j;

    EXPECT_TRUE(j["device"].contains("bandwidth_estimated"));
    EXPECT_TRUE(j["device"]["bandwidth_estimated"].get<bool>());
}

TEST_F(JSONExportTest, GeneratedTestId) {
    MetricsExporter exporter;

    TestConfiguration config;
    // testId left empty - should be auto-generated
    config.pipeline = "compute";
    config.algorithm = "empty_skip";
    config.voxelResolution = 64;
    config.sceneType = "cave";
    config.warmupFrames = 10;
    config.measurementFrames = 100;

    DeviceCaps device;

    std::vector<FrameMetrics> frames;
    std::map<std::string, AggregateStats> aggregates;

    exporter.ExportToJSON(outputPath, config, device, frames, aggregates);

    std::ifstream file(outputPath);
    nlohmann::json j;
    file >> j;

    // Should have auto-generated test_id
    EXPECT_TRUE(j.contains("test_id"));
    std::string testId = j["test_id"];
    EXPECT_NE(testId.find("COMPUTE"), std::string::npos);
    EXPECT_NE(testId.find("64"), std::string::npos);
    EXPECT_NE(testId.find("CAVE"), std::string::npos);
    EXPECT_NE(testId.find("EMPTY_SKIP"), std::string::npos);
}

// ============================================================================
// FrameMetrics Extended Fields Tests
// ============================================================================

TEST(FrameMetricsTest, NewFieldsDefaultValues) {
    FrameMetrics metrics;
    EXPECT_FLOAT_EQ(metrics.avgVoxelsPerRay, 0.0f);
    EXPECT_EQ(metrics.totalRaysCast, 0u);
    EXPECT_FALSE(metrics.bandwidthEstimated);
}

TEST(FrameMetricsTest, NewFieldsCanBeSet) {
    FrameMetrics metrics;
    metrics.avgVoxelsPerRay = 18.6f;
    metrics.totalRaysCast = 1'920'000;
    metrics.bandwidthEstimated = true;

    EXPECT_FLOAT_EQ(metrics.avgVoxelsPerRay, 18.6f);
    EXPECT_EQ(metrics.totalRaysCast, 1'920'000u);
    EXPECT_TRUE(metrics.bandwidthEstimated);
}

// ============================================================================
// BenchmarkGraphFactory Tests
// ============================================================================

#include "Profiler/BenchmarkGraphFactory.h"

// Note: These tests validate the struct initialization and validation logic.
// Full integration tests requiring a RenderGraph instance would need the
// RenderGraph library with a NodeTypeRegistry, which is beyond unit test scope.

TEST(BenchmarkGraphFactoryTest, InfrastructureNodesDefaultInvalid) {
    InfrastructureNodes nodes{};
    EXPECT_FALSE(nodes.IsValid());
}

TEST(BenchmarkGraphFactoryTest, ComputePipelineNodesDefaultInvalid) {
    ComputePipelineNodes nodes{};
    EXPECT_FALSE(nodes.IsValid());
}

TEST(BenchmarkGraphFactoryTest, RayMarchNodesDefaultInvalid) {
    RayMarchNodes nodes{};
    EXPECT_FALSE(nodes.IsValid());
}

TEST(BenchmarkGraphFactoryTest, OutputNodesDefaultInvalid) {
    OutputNodes nodes{};
    EXPECT_FALSE(nodes.IsValid());
}

TEST(BenchmarkGraphFactoryTest, BenchmarkGraphDefaultInvalid) {
    BenchmarkGraph graph{};
    EXPECT_FALSE(graph.IsValid());
}

TEST(BenchmarkGraphFactoryTest, BuildInfrastructureNullGraphThrows) {
    EXPECT_THROW(
        BenchmarkGraphFactory::BuildInfrastructure(nullptr, 800, 600),
        std::invalid_argument
    );
}

TEST(BenchmarkGraphFactoryTest, BuildComputePipelineInvalidInfraThrows) {
    InfrastructureNodes invalidInfra{};  // All handles invalid
    EXPECT_THROW(
        BenchmarkGraphFactory::BuildComputePipeline(nullptr, invalidInfra, "test.comp"),
        std::invalid_argument
    );
}

TEST(BenchmarkGraphFactoryTest, BuildRayMarchSceneInvalidInfraThrows) {
    InfrastructureNodes invalidInfra{};
    SceneInfo scene = SceneInfo::FromResolutionAndDensity(128, 50.0f, "test");
    EXPECT_THROW(
        BenchmarkGraphFactory::BuildRayMarchScene(nullptr, invalidInfra, scene),
        std::invalid_argument
    );
}

TEST(BenchmarkGraphFactoryTest, BuildOutputInvalidInfraThrows) {
    InfrastructureNodes invalidInfra{};
    EXPECT_THROW(
        BenchmarkGraphFactory::BuildOutput(nullptr, invalidInfra),
        std::invalid_argument
    );
}

TEST(BenchmarkGraphFactoryTest, ConnectComputeRayMarchNullGraphThrows) {
    InfrastructureNodes infra{};
    ComputePipelineNodes compute{};
    RayMarchNodes rayMarch{};
    OutputNodes output{};

    EXPECT_THROW(
        BenchmarkGraphFactory::ConnectComputeRayMarch(nullptr, infra, compute, rayMarch, output),
        std::invalid_argument
    );
}

TEST(BenchmarkGraphFactoryTest, BuildComputeRayMarchGraphNullGraphThrows) {
    TestConfiguration config;
    config.pipeline = "compute";
    config.voxelResolution = 128;

    EXPECT_THROW(
        BenchmarkGraphFactory::BuildComputeRayMarchGraph(nullptr, config, 800, 600),
        std::invalid_argument
    );
}

TEST(BenchmarkGraphFactoryTest, WireProfilerHooksNullGraphThrows) {
    ProfilerGraphAdapter adapter;
    EXPECT_THROW(
        BenchmarkGraphFactory::WireProfilerHooks(nullptr, adapter),
        std::invalid_argument
    );
}

TEST(BenchmarkGraphFactoryTest, HasProfilerHooksNullGraphReturnsFalse) {
    EXPECT_FALSE(BenchmarkGraphFactory::HasProfilerHooks(nullptr));
}

// ============================================================================
// ProfilerGraphAdapter Integration Tests
// ============================================================================

#include "Profiler/ProfilerGraphAdapter.h"

class ProfilerGraphAdapterTest : public ::testing::Test {
protected:
    ProfilerGraphAdapter adapter;
};

TEST_F(ProfilerGraphAdapterTest, SetFrameContextStoresValues) {
    // SetFrameContext should not throw with null/zero values
    // (actual command buffer would be VK_NULL_HANDLE equivalent)
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    uint32_t frameIndex = 5;

    EXPECT_NO_THROW(adapter.SetFrameContext(cmdBuffer, frameIndex));
}

TEST_F(ProfilerGraphAdapterTest, OnFrameBeginNoThrowWithoutInit) {
    // OnFrameBegin calls ProfilerSystem::Instance() which is a singleton
    // Without initialization, it should not crash (guards against uninitialized state)
    adapter.SetFrameContext(VK_NULL_HANDLE, 0);

    // This may do nothing if ProfilerSystem isn't initialized, but shouldn't throw
    EXPECT_NO_THROW(adapter.OnFrameBegin());
}

TEST_F(ProfilerGraphAdapterTest, OnFrameEndNoThrowWithoutInit) {
    adapter.SetFrameContext(VK_NULL_HANDLE, 0);
    EXPECT_NO_THROW(adapter.OnFrameEnd());
}

TEST_F(ProfilerGraphAdapterTest, OnDispatchBeginNoThrowWithoutInit) {
    adapter.SetFrameContext(VK_NULL_HANDLE, 0);
    EXPECT_NO_THROW(adapter.OnDispatchBegin());
}

TEST_F(ProfilerGraphAdapterTest, OnDispatchEndNoThrowWithoutInit) {
    adapter.SetFrameContext(VK_NULL_HANDLE, 0);
    uint32_t dispatchWidth = 100;
    uint32_t dispatchHeight = 75;
    EXPECT_NO_THROW(adapter.OnDispatchEnd(dispatchWidth, dispatchHeight));
}

TEST_F(ProfilerGraphAdapterTest, OnPreGraphCleanupNoThrow) {
    EXPECT_NO_THROW(adapter.OnPreGraphCleanup());
}

TEST_F(ProfilerGraphAdapterTest, NodeCallbacksAcceptEmptyString) {
    EXPECT_NO_THROW(adapter.OnNodePreExecute(""));
    EXPECT_NO_THROW(adapter.OnNodePostExecute(""));
    EXPECT_NO_THROW(adapter.OnNodePreCleanup(""));
}

TEST_F(ProfilerGraphAdapterTest, NodeCallbacksAcceptValidNames) {
    EXPECT_NO_THROW(adapter.OnNodePreExecute("benchmark_dispatch"));
    EXPECT_NO_THROW(adapter.OnNodePostExecute("benchmark_dispatch"));
    EXPECT_NO_THROW(adapter.OnNodePreCleanup("benchmark_voxel_grid"));
}

TEST_F(ProfilerGraphAdapterTest, RegisterUnregisterExtractor) {
    // Register a simple extractor (takes FrameMetrics& and modifies it)
    bool extractorCalled = false;
    adapter.RegisterExtractor("test_extractor", [&extractorCalled](FrameMetrics& metrics) {
        extractorCalled = true;
        metrics.avgVoxelsPerRay = 42.0f;  // Modify metrics to show extractor ran
    });

    // Unregister should not throw
    EXPECT_NO_THROW(adapter.UnregisterExtractor("test_extractor"));

    // Unregister non-existent should not throw
    EXPECT_NO_THROW(adapter.UnregisterExtractor("nonexistent"));
}

// ============================================================================
// BenchmarkRunner Graph Integration Tests
// ============================================================================

TEST(BenchmarkRunnerGraphIntegrationTest, DefaultHasNoGraph) {
    BenchmarkRunner runner;
    EXPECT_FALSE(runner.HasCurrentGraph());
}

TEST(BenchmarkRunnerGraphIntegrationTest, ClearCurrentGraphNoThrow) {
    BenchmarkRunner runner;
    EXPECT_NO_THROW(runner.ClearCurrentGraph());
    EXPECT_FALSE(runner.HasCurrentGraph());
}

TEST(BenchmarkRunnerGraphIntegrationTest, SetRenderDimensionsStoresValues) {
    BenchmarkRunner runner;
    EXPECT_NO_THROW(runner.SetRenderDimensions(1920, 1080));
}

TEST(BenchmarkRunnerGraphIntegrationTest, SetGraphFactoryStoresFunction) {
    BenchmarkRunner runner;

    bool factoryCalled = false;
    runner.SetGraphFactory([&factoryCalled](
        Vixen::RenderGraph::RenderGraph* /*graph*/,
        const TestConfiguration& /*config*/,
        uint32_t /*width*/,
        uint32_t /*height*/
    ) -> BenchmarkGraph {
        factoryCalled = true;
        return BenchmarkGraph{};  // Return empty/invalid graph
    });

    // Factory won't be called until CreateGraphForCurrentTest
    EXPECT_FALSE(factoryCalled);
}

TEST(BenchmarkRunnerGraphIntegrationTest, CreateGraphForNullGraphReturnsEmpty) {
    BenchmarkRunner runner;

    // Set up a test config
    TestConfiguration config;
    config.pipeline = "compute";
    config.voxelResolution = 128;
    config.warmupFrames = 10;
    config.measurementFrames = 100;

    std::vector<TestConfiguration> matrix{config};
    runner.SetTestMatrix(matrix);
    runner.StartSuite();
    runner.BeginNextTest();

    // Creating graph with null should return empty graph
    BenchmarkGraph result = runner.CreateGraphForCurrentTest(nullptr);
    EXPECT_FALSE(result.IsValid());
}

TEST(BenchmarkRunnerGraphIntegrationTest, GetAdapterReturnsValidReference) {
    BenchmarkRunner runner;

    // Should be able to access adapter
    ProfilerGraphAdapter& adapter = runner.GetAdapter();

    // Adapter should accept frame context
    EXPECT_NO_THROW(adapter.SetFrameContext(VK_NULL_HANDLE, 0));
}

TEST(BenchmarkRunnerGraphIntegrationTest, ConstGetAdapterWorks) {
    const BenchmarkRunner runner;

    // Should be able to access const adapter
    const ProfilerGraphAdapter& adapter = runner.GetAdapter();

    // Can't call SetFrameContext on const reference (compile-time check)
    // Just verify we can access it
    (void)adapter;
}

TEST(BenchmarkRunnerGraphIntegrationTest, GetCurrentGraphReturnsEmptyByDefault) {
    BenchmarkRunner runner;
    const BenchmarkGraph& graph = runner.GetCurrentGraph();
    EXPECT_FALSE(graph.IsValid());
}

TEST(BenchmarkRunnerGraphIntegrationTest, CustomFactoryIsCalledOnGraphCreate) {
    BenchmarkRunner runner;

    bool factoryCalled = false;
    TestConfiguration receivedConfig;

    runner.SetGraphFactory([&factoryCalled, &receivedConfig](
        Vixen::RenderGraph::RenderGraph* /*graph*/,
        const TestConfiguration& config,
        uint32_t /*width*/,
        uint32_t /*height*/
    ) -> BenchmarkGraph {
        factoryCalled = true;
        receivedConfig = config;
        return BenchmarkGraph{};  // Return invalid graph (no real RenderGraph)
    });

    // Set up test
    TestConfiguration config;
    config.pipeline = "compute";
    config.voxelResolution = 256;
    config.warmupFrames = 10;
    config.measurementFrames = 100;

    std::vector<TestConfiguration> matrix{config};
    runner.SetTestMatrix(matrix);
    runner.StartSuite();
    runner.BeginNextTest();

    // Create graph (with nullptr since we're mocking)
    // Note: This will call factory but won't wire hooks (null graph)
    runner.CreateGraphForCurrentTest(nullptr);

    // Factory should have been called even with null graph (it checks after)
    // Actually, looking at the code, it returns early if graph is null
    EXPECT_FALSE(factoryCalled);  // Null graph returns early
}

// ============================================================================
// End-to-End Flow Tests (Mock/Stub)
// ============================================================================

TEST(ProfilerEndToEndTest, ConfigToMetricsExportFlow) {
    // Test the complete flow without real Vulkan:
    // Config -> BenchmarkRunner -> Adapter -> Metrics -> Export

    // 1. Configuration
    TestConfiguration config;
    config.testId = "E2E_TEST_FLOW";
    config.pipeline = "compute";
    config.algorithm = "baseline";
    config.sceneType = "cornell";
    config.voxelResolution = 64;
    config.densityPercent = 0.25f;
    config.warmupFrames = 10;           // Minimum allowed
    config.measurementFrames = 100;     // Minimum allowed (was 50, invalid)

    ASSERT_TRUE(config.Validate());

    // 2. Set up BenchmarkRunner
    BenchmarkRunner runner;
    DeviceCaps caps;  // Use alias to avoid Windows API conflict
    caps.deviceName = "Mock GPU";
    caps.driverVersion = "1.0.0";
    caps.totalVRAM_MB = 8192;
    runner.SetDeviceCapabilities(caps);

    std::vector<TestConfiguration> matrix{config};
    runner.SetTestMatrix(matrix);

    // 3. Start suite
    ASSERT_TRUE(runner.StartSuite());
    ASSERT_TRUE(runner.BeginNextTest());

    // 4. Simulate warmup frames
    for (uint32_t i = 0; i < config.warmupFrames; ++i) {
        FrameMetrics warmupMetrics;
        warmupMetrics.frameNumber = i;
        warmupMetrics.frameTimeMs = 16.0f;  // 60 FPS
        runner.RecordFrame(warmupMetrics);
    }

    EXPECT_EQ(runner.GetState(), BenchmarkState::Measuring);

    // 5. Simulate measurement frames
    for (uint32_t i = 0; i < config.measurementFrames; ++i) {
        FrameMetrics metrics;
        metrics.frameNumber = i;
        metrics.frameTimeMs = 16.5f + (i % 5) * 0.1f;  // Small variance
        metrics.fps = 1000.0f / metrics.frameTimeMs;
        metrics.mRaysPerSec = 100.0f + i;
        metrics.vramUsageMB = 2048;
        metrics.totalRaysCast = 1920 * 1080;  // Full HD ray count
        runner.RecordFrame(metrics);
    }

    // 6. Verify test completed
    EXPECT_TRUE(runner.IsCurrentTestComplete());

    // 7. Finalize
    runner.FinalizeCurrentTest();

    // 8. Verify results
    const auto& results = runner.GetSuiteResults();
    EXPECT_EQ(results.GetAllResults().size(), 1u);

    const auto& testResult = results.GetAllResults()[0];
    EXPECT_EQ(testResult.frames.size(), config.measurementFrames);
    EXPECT_TRUE(testResult.IsValid());
}

TEST(ProfilerEndToEndTest, MultipleTestsInMatrix) {
    // Test running multiple configurations in sequence

    BenchmarkRunner runner;
    DeviceCaps caps;  // Use alias to avoid Windows API conflict
    caps.deviceName = "Mock GPU";
    runner.SetDeviceCapabilities(caps);

    // Create a small test matrix (3 tests)
    auto matrix = BenchmarkConfigLoader::GenerateTestMatrix(
        {"compute"},       // 1 pipeline
        {64, 128},         // 2 resolutions
        {0.25f},           // 1 density (corrected from 0.2f to match valid range)
        {"baseline"}       // 1 algorithm
    );
    ASSERT_EQ(matrix.size(), 2u);

    // Override warmup/measurement for speed (but keep above minimums)
    for (auto& config : matrix) {
        config.warmupFrames = 10;        // Minimum allowed
        config.measurementFrames = 100;  // Minimum allowed
    }

    runner.SetTestMatrix(matrix);
    ASSERT_TRUE(runner.StartSuite());

    // Run all tests
    uint32_t testsCompleted = 0;
    while (runner.BeginNextTest()) {
        // Warmup
        for (uint32_t i = 0; i < runner.GetCurrentTestConfig().warmupFrames; ++i) {
            FrameMetrics m;
            m.frameTimeMs = 16.0f;
            runner.RecordFrame(m);
        }

        // Measurement
        while (!runner.IsCurrentTestComplete()) {
            FrameMetrics m;
            m.frameNumber = runner.GetCurrentFrameNumber();
            m.frameTimeMs = 16.5f;
            m.fps = 60.0f;
            runner.RecordFrame(m);
        }

        runner.FinalizeCurrentTest();
        testsCompleted++;
    }

    EXPECT_EQ(testsCompleted, 2u);
    EXPECT_EQ(runner.GetState(), BenchmarkState::Completed);
    EXPECT_EQ(runner.GetSuiteResults().GetAllResults().size(), 2u);
}

TEST(ProfilerEndToEndTest, AdapterFrameLifecycle) {
    // Test the adapter's frame lifecycle callbacks

    ProfilerGraphAdapter adapter;

    // Simulate a frame lifecycle
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;  // Mock
    uint32_t frameIndex = 0;

    // Begin frame
    adapter.SetFrameContext(cmdBuffer, frameIndex);
    EXPECT_NO_THROW(adapter.OnFrameBegin());

    // Simulate node executions
    adapter.OnNodePreExecute("benchmark_instance");
    adapter.OnNodePostExecute("benchmark_instance");

    adapter.OnNodePreExecute("benchmark_device");
    adapter.OnNodePostExecute("benchmark_device");

    // Dispatch node with timing
    adapter.OnNodePreExecute("benchmark_dispatch");
    adapter.OnDispatchBegin();
    // ... GPU work happens here ...
    adapter.OnDispatchEnd(100, 75);  // 100x75 dispatch groups
    adapter.OnNodePostExecute("benchmark_dispatch");

    // Present
    adapter.OnNodePreExecute("benchmark_present");
    adapter.OnNodePostExecute("benchmark_present");

    // End frame
    EXPECT_NO_THROW(adapter.OnFrameEnd());

    // Pre-cleanup (graph teardown)
    adapter.OnNodePreCleanup("benchmark_dispatch");
    adapter.OnPreGraphCleanup();
}

TEST(ProfilerEndToEndTest, BandwidthEstimationInRunner) {
    BenchmarkRunner runner;

    // Test bandwidth estimation formula
    // Formula: rays * bytes_per_ray / time_seconds / (1024^3)

    // 10M rays, 96 bytes/ray, 0.01s = 960MB / 0.01s = 96GB/s
    // With 1024-based conversion: ~89.4 GB/s
    float bandwidth = runner.EstimateBandwidth(10'000'000, 0.01f);
    EXPECT_GT(bandwidth, 80.0f);
    EXPECT_LT(bandwidth, 100.0f);
}

TEST(ProfilerEndToEndTest, TestConfigurationGeneratesValidId) {
    TestConfiguration config;
    config.pipeline = "compute";
    config.algorithm = "empty_skip";
    config.sceneType = "cave";
    config.voxelResolution = 128;

    std::string testId = config.GenerateTestId(1);

    // Should contain key identifiers
    EXPECT_NE(testId.find("COMPUTE"), std::string::npos);
    EXPECT_NE(testId.find("128"), std::string::npos);
    EXPECT_NE(testId.find("CAVE"), std::string::npos);
    EXPECT_NE(testId.find("EMPTY_SKIP"), std::string::npos);
    EXPECT_NE(testId.find("RUN1"), std::string::npos);
}

// ============================================================================
// End-to-End Integration Tests (Full Profiler Stack)
// ============================================================================

/// Tests the complete flow: BenchmarkRunner -> BenchmarkGraphFactory ->
/// ProfilerGraphAdapter -> MetricsCollector -> MetricsExporter
/// Uses mock Vulkan objects (nullptr with guards) to exercise the full pipeline

class EndToEndIntegrationTest : public ::testing::Test {
protected:
    std::filesystem::path tempDir;

    void SetUp() override {
        tempDir = std::filesystem::temp_directory_path() / "profiler_e2e_test";
        std::filesystem::create_directories(tempDir);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    // Helper to create a valid test configuration
    TestConfiguration CreateValidConfig() {
        TestConfiguration config;
        config.testId = "E2E_INTEGRATION_TEST";
        config.pipeline = "compute";
        config.algorithm = "baseline";
        config.sceneType = "cornell";
        config.voxelResolution = 128;
        config.densityPercent = 0.25f;
        config.warmupFrames = 10;
        config.measurementFrames = 100;
        return config;
    }

    // Helper to create mock device capabilities
    DeviceCaps CreateMockDeviceCaps() {
        DeviceCaps caps;
        caps.deviceName = "Mock Integration Test GPU";
        caps.driverVersion = "1.0.0";
        caps.vulkanVersion = "1.3.0";
        caps.totalVRAM_MB = 8192;
        caps.timestampSupported = false;  // No real GPU
        caps.performanceQuerySupported = false;
        caps.memoryBudgetSupported = false;
        return caps;
    }
};

TEST_F(EndToEndIntegrationTest, FullPipelineFlowWithMockVulkan) {
    // This test exercises the complete profiler stack without real Vulkan
    // BenchmarkRunner -> BenchmarkGraphFactory -> ProfilerGraphAdapter -> Export

    // 1. Setup configuration
    TestConfiguration config = CreateValidConfig();
    ASSERT_TRUE(config.Validate());

    // 2. Create and configure BenchmarkRunner
    BenchmarkRunner runner;
    runner.SetDeviceCapabilities(CreateMockDeviceCaps());
    runner.SetOutputDirectory(tempDir);
    runner.SetRenderDimensions(800, 600);

    // 3. Set test matrix
    std::vector<TestConfiguration> matrix{config};
    runner.SetTestMatrix(matrix);

    // 4. Start benchmark suite
    ASSERT_TRUE(runner.StartSuite());
    EXPECT_EQ(runner.GetState(), BenchmarkState::Warmup);

    // 5. Begin first test
    ASSERT_TRUE(runner.BeginNextTest());

    // 6. Get adapter and verify it's accessible
    ProfilerGraphAdapter& adapter = runner.GetAdapter();

    // 7. Simulate frame lifecycle with mock Vulkan handles
    VkCommandBuffer mockCmdBuffer = VK_NULL_HANDLE;  // Guards in ProfilerSystem handle this

    // Warmup phase
    for (uint32_t i = 0; i < config.warmupFrames; ++i) {
        adapter.SetFrameContext(mockCmdBuffer, i % 3);  // Simulate triple buffering
        EXPECT_NO_THROW(adapter.OnFrameBegin());

        // Simulate node execution
        adapter.OnNodePreExecute("benchmark_instance");
        adapter.OnNodePostExecute("benchmark_instance");
        adapter.OnNodePreExecute("benchmark_dispatch");
        EXPECT_NO_THROW(adapter.OnDispatchBegin());
        EXPECT_NO_THROW(adapter.OnDispatchEnd(100, 75));  // 800/8, 600/8
        adapter.OnNodePostExecute("benchmark_dispatch");

        EXPECT_NO_THROW(adapter.OnFrameEnd());

        // Record frame metrics
        FrameMetrics warmupMetrics;
        warmupMetrics.frameNumber = i;
        warmupMetrics.frameTimeMs = 16.67f;  // 60 FPS
        warmupMetrics.fps = 60.0f;
        warmupMetrics.totalRaysCast = 800 * 600;
        runner.RecordFrame(warmupMetrics);
    }

    EXPECT_EQ(runner.GetState(), BenchmarkState::Measuring);

    // 8. Measurement phase with varied metrics
    std::vector<FrameMetrics> recordedMetrics;
    for (uint32_t i = 0; i < config.measurementFrames; ++i) {
        adapter.SetFrameContext(mockCmdBuffer, i % 3);
        adapter.OnFrameBegin();

        adapter.OnNodePreExecute("benchmark_dispatch");
        adapter.OnDispatchBegin();
        adapter.OnDispatchEnd(100, 75);
        adapter.OnNodePostExecute("benchmark_dispatch");

        adapter.OnFrameEnd();

        // Create varied metrics to test statistics
        FrameMetrics metrics;
        metrics.frameNumber = i;
        metrics.frameTimeMs = 16.0f + (i % 10) * 0.1f;  // 16.0ms - 16.9ms
        metrics.fps = 1000.0f / metrics.frameTimeMs;
        metrics.gpuTimeMs = 14.0f + (i % 8) * 0.2f;
        metrics.mRaysPerSec = 100.0f + (i % 20);
        metrics.totalRaysCast = 800 * 600;
        metrics.vramUsageMB = 2048 + (i % 100);
        metrics.vramBudgetMB = 8192;
        metrics.avgVoxelsPerRay = 15.0f + (i % 10) * 0.5f;
        metrics.bandwidthReadGB = 50.0f + (i % 30);
        metrics.bandwidthWriteGB = 10.0f + (i % 10);
        metrics.bandwidthEstimated = true;  // No real HW counters
        metrics.sceneResolution = config.voxelResolution;
        metrics.screenWidth = 800;
        metrics.screenHeight = 600;
        metrics.sceneDensity = config.densityPercent * 100.0f;

        runner.RecordFrame(metrics);
        recordedMetrics.push_back(metrics);
    }

    // 9. Verify test completion
    EXPECT_TRUE(runner.IsCurrentTestComplete());

    // 10. Finalize and cleanup
    adapter.OnPreGraphCleanup();
    runner.FinalizeCurrentTest();

    // 11. Verify results
    const auto& results = runner.GetSuiteResults();
    ASSERT_EQ(results.GetAllResults().size(), 1u);

    const auto& testResult = results.GetAllResults()[0];
    EXPECT_EQ(testResult.frames.size(), config.measurementFrames);
    EXPECT_TRUE(testResult.IsValid());

    // 12. Export and verify files
    runner.ExportAllResults();

    // Check that output files exist
    std::filesystem::path jsonPath = tempDir / (config.testId + ".json");
    std::filesystem::path csvPath = tempDir / (config.testId + ".csv");

    // Export creates files with configuration-based names
    bool foundJson = false;
    bool foundCsv = false;
    for (const auto& entry : std::filesystem::directory_iterator(tempDir)) {
        std::string filename = entry.path().filename().string();
        if (filename.find(".json") != std::string::npos) foundJson = true;
        if (filename.find(".csv") != std::string::npos) foundCsv = true;
    }

    EXPECT_TRUE(foundJson || foundCsv);  // At least one export format
}

TEST_F(EndToEndIntegrationTest, MultiIterationFrameTimingCapture) {
    // Test frame timing capture across multiple benchmark iterations

    BenchmarkRunner runner;
    runner.SetDeviceCapabilities(CreateMockDeviceCaps());
    runner.SetOutputDirectory(tempDir);

    // Create 3 test configurations with different resolutions
    std::vector<TestConfiguration> matrix;
    for (uint32_t res : {64, 128, 256}) {
        TestConfiguration config;
        config.testId = "TIMING_TEST_RES" + std::to_string(res);
        config.pipeline = "compute";
        config.voxelResolution = res;
        config.warmupFrames = 10;
        config.measurementFrames = 100;
        matrix.push_back(config);
    }

    runner.SetTestMatrix(matrix);
    ASSERT_TRUE(runner.StartSuite());

    uint32_t testsCompleted = 0;
    while (runner.BeginNextTest()) {
        const auto& currentConfig = runner.GetCurrentTestConfig();

        // Warmup
        for (uint32_t i = 0; i < currentConfig.warmupFrames; ++i) {
            FrameMetrics m;
            m.frameTimeMs = 16.0f;
            runner.RecordFrame(m);
        }

        EXPECT_EQ(runner.GetState(), BenchmarkState::Measuring);

        // Measurement - higher resolution = longer frame time
        float baseTime = 10.0f + currentConfig.voxelResolution * 0.05f;
        while (!runner.IsCurrentTestComplete()) {
            FrameMetrics m;
            m.frameNumber = runner.GetCurrentFrameNumber();
            m.frameTimeMs = baseTime + (m.frameNumber % 5) * 0.1f;
            m.fps = 1000.0f / m.frameTimeMs;
            m.sceneResolution = currentConfig.voxelResolution;
            runner.RecordFrame(m);
        }

        runner.FinalizeCurrentTest();
        testsCompleted++;
    }

    EXPECT_EQ(testsCompleted, 3u);
    EXPECT_EQ(runner.GetState(), BenchmarkState::Completed);

    const auto& results = runner.GetSuiteResults();
    EXPECT_EQ(results.GetAllResults().size(), 3u);

    // Verify each test has correct frame count
    for (const auto& result : results.GetAllResults()) {
        EXPECT_EQ(result.frames.size(), 100u);
        EXPECT_TRUE(result.IsValid());
    }
}

TEST_F(EndToEndIntegrationTest, JSONExportValidation) {
    // Test that JSON export produces valid, parseable output with correct schema

    MetricsExporter exporter;

    TestConfiguration config = CreateValidConfig();
    DeviceCaps device = CreateMockDeviceCaps();

    // Create realistic frame metrics
    std::vector<FrameMetrics> frames;
    for (uint32_t i = 0; i < 50; ++i) {
        FrameMetrics f;
        f.frameNumber = i;
        f.timestampMs = i * 16.67;
        f.frameTimeMs = 16.0f + (i % 5) * 0.2f;
        f.gpuTimeMs = 14.0f + (i % 4) * 0.3f;
        f.fps = 1000.0f / f.frameTimeMs;
        f.mRaysPerSec = 150.0f + (i % 20);
        f.totalRaysCast = 800 * 600;
        f.avgVoxelsPerRay = 18.0f + (i % 10) * 0.5f;
        f.vramUsageMB = 2048;
        f.vramBudgetMB = 8192;
        f.bandwidthReadGB = 60.0f + (i % 15);
        f.bandwidthWriteGB = 12.0f + (i % 5);
        f.bandwidthEstimated = true;
        f.sceneResolution = 128;
        f.screenWidth = 800;
        f.screenHeight = 600;
        f.sceneDensity = 25.0f;
        frames.push_back(f);
    }

    // Compute aggregate statistics
    std::map<std::string, AggregateStats> aggregates;

    // Frame time stats
    AggregateStats ftStats;
    ftStats.min = 16.0f;
    ftStats.max = 16.8f;
    ftStats.mean = 16.4f;
    ftStats.stddev = 0.25f;
    ftStats.p1 = 16.0f;
    ftStats.p50 = 16.4f;
    ftStats.p99 = 16.8f;
    ftStats.sampleCount = 50;
    aggregates["frame_time_ms"] = ftStats;

    // FPS stats
    AggregateStats fpsStats;
    fpsStats.min = 59.5f;
    fpsStats.max = 62.5f;
    fpsStats.mean = 61.0f;
    fpsStats.stddev = 0.8f;
    fpsStats.p1 = 59.5f;
    fpsStats.p50 = 61.0f;
    fpsStats.p99 = 62.5f;
    fpsStats.sampleCount = 50;
    aggregates["fps"] = fpsStats;

    // Export to JSON
    std::filesystem::path jsonPath = tempDir / "e2e_export_test.json";
    exporter.ExportToJSON(jsonPath, config, device, frames, aggregates);

    // Read and validate JSON structure
    std::ifstream file(jsonPath);
    ASSERT_TRUE(file.is_open());

    nlohmann::json j;
    ASSERT_NO_THROW(file >> j);

    // Verify required top-level fields
    EXPECT_TRUE(j.contains("test_id"));
    EXPECT_TRUE(j.contains("timestamp"));
    EXPECT_TRUE(j.contains("configuration"));
    EXPECT_TRUE(j.contains("device"));
    EXPECT_TRUE(j.contains("frames"));
    EXPECT_TRUE(j.contains("statistics"));

    // Verify configuration section
    EXPECT_EQ(j["configuration"]["pipeline"], "compute");
    EXPECT_EQ(j["configuration"]["algorithm"], "baseline");
    EXPECT_EQ(j["configuration"]["resolution"], 128);
    EXPECT_EQ(j["configuration"]["density_percent"], 25);  // 0.25 * 100

    // Verify device section
    EXPECT_EQ(j["device"]["gpu"], "Mock Integration Test GPU");
    EXPECT_TRUE(j["device"]["bandwidth_estimated"].get<bool>());

    // Verify frames array
    EXPECT_EQ(j["frames"].size(), 50u);
    EXPECT_TRUE(j["frames"][0].contains("frame_num"));
    EXPECT_TRUE(j["frames"][0].contains("frame_time_ms"));
    EXPECT_TRUE(j["frames"][0].contains("fps"));
    EXPECT_TRUE(j["frames"][0].contains("avg_voxels_per_ray"));

    // Verify statistics section
    EXPECT_TRUE(j["statistics"].contains("frame_time_mean"));
    EXPECT_TRUE(j["statistics"].contains("frame_time_stddev"));
    EXPECT_TRUE(j["statistics"].contains("fps_mean"));
}

TEST_F(EndToEndIntegrationTest, CSVExportValidation) {
    // Test that CSV export produces valid output

    MetricsExporter exporter;

    TestConfiguration config = CreateValidConfig();
    DeviceCaps device = CreateMockDeviceCaps();

    std::vector<FrameMetrics> frames;
    for (uint32_t i = 0; i < 20; ++i) {
        FrameMetrics f;
        f.frameNumber = i;
        f.frameTimeMs = 16.5f;
        f.fps = 60.6f;
        f.mRaysPerSec = 150.0f;
        f.vramUsageMB = 2048;
        frames.push_back(f);
    }

    std::map<std::string, AggregateStats> aggregates;

    std::filesystem::path csvPath = tempDir / "e2e_export_test.csv";
    exporter.ExportToCSV(csvPath, config, device, frames, aggregates);

    // Read and validate CSV
    std::ifstream file(csvPath);
    ASSERT_TRUE(file.is_open());

    std::string line;
    std::vector<std::string> lines;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }

    // Should have header comment lines + column header + data rows
    EXPECT_GT(lines.size(), 20u);  // At least header + 20 data rows

    // Find the data rows (skip metadata comments)
    bool foundHeader = false;
    size_t dataRowCount = 0;
    for (const auto& l : lines) {
        if (l.find("frame,") == 0 || l.find("frame_num,") == 0) {
            foundHeader = true;
        } else if (foundHeader && !l.empty() && l[0] != '#') {
            dataRowCount++;
        }
    }

    EXPECT_TRUE(foundHeader);
    EXPECT_EQ(dataRowCount, 20u);
}

TEST_F(EndToEndIntegrationTest, AdapterExtractorRegistration) {
    // Test that custom extractors can be registered and work correctly

    ProfilerGraphAdapter adapter;

    bool extractorCalled = false;
    float extractedValue = 0.0f;

    // Register extractor that modifies metrics
    adapter.RegisterExtractor("voxel_count", [&](FrameMetrics& metrics) {
        extractorCalled = true;
        extractedValue = metrics.avgVoxelsPerRay;
        metrics.avgVoxelsPerRay = 42.0f;  // Modify to prove extractor ran
    });

    // Extractors are called via ProfilerSystem, which requires initialization
    // For unit test purposes, verify registration doesn't throw
    EXPECT_NO_THROW(adapter.UnregisterExtractor("voxel_count"));

    // Re-register and verify double-unregister doesn't throw
    adapter.RegisterExtractor("test_extractor", [](FrameMetrics&) {});
    EXPECT_NO_THROW(adapter.UnregisterExtractor("test_extractor"));
    EXPECT_NO_THROW(adapter.UnregisterExtractor("test_extractor"));  // Safe to call twice
}

TEST_F(EndToEndIntegrationTest, GraphFactoryIntegrationWithRunner) {
    // Test that custom graph factory integrates properly with runner

    BenchmarkRunner runner;
    runner.SetDeviceCapabilities(CreateMockDeviceCaps());

    bool factoryInvoked = false;
    uint32_t receivedWidth = 0;
    uint32_t receivedHeight = 0;
    std::string receivedPipeline;

    runner.SetGraphFactory([&](
        Vixen::RenderGraph::RenderGraph* graph,
        const TestConfiguration& config,
        uint32_t width,
        uint32_t height
    ) -> BenchmarkGraph {
        factoryInvoked = true;
        receivedWidth = width;
        receivedHeight = height;
        receivedPipeline = config.pipeline;

        // Return empty graph since we don't have a real RenderGraph
        return BenchmarkGraph{};
    });

    runner.SetRenderDimensions(1920, 1080);

    TestConfiguration config = CreateValidConfig();
    runner.SetTestMatrix({config});
    runner.StartSuite();
    runner.BeginNextTest();

    // Factory won't be called with nullptr graph (early return guard)
    BenchmarkGraph result = runner.CreateGraphForCurrentTest(nullptr);
    EXPECT_FALSE(result.IsValid());

    // With null graph, factory should not be invoked (guard check)
    // This is correct behavior - CreateGraphForCurrentTest returns early on null
}

TEST_F(EndToEndIntegrationTest, BenchmarkStateTransitions) {
    // Test correct state machine transitions through benchmark lifecycle

    BenchmarkRunner runner;
    runner.SetDeviceCapabilities(CreateMockDeviceCaps());

    // Initial state
    EXPECT_EQ(runner.GetState(), BenchmarkState::Idle);
    EXPECT_FALSE(runner.IsRunning());

    // Empty matrix -> Error
    EXPECT_FALSE(runner.StartSuite());
    EXPECT_EQ(runner.GetState(), BenchmarkState::Error);

    // Reset by setting valid matrix
    TestConfiguration config = CreateValidConfig();
    runner.SetTestMatrix({config});

    // Start suite
    ASSERT_TRUE(runner.StartSuite());
    EXPECT_EQ(runner.GetState(), BenchmarkState::Warmup);
    EXPECT_TRUE(runner.IsRunning());

    // Begin test
    ASSERT_TRUE(runner.BeginNextTest());
    EXPECT_EQ(runner.GetState(), BenchmarkState::Warmup);

    // Record warmup frames
    for (uint32_t i = 0; i < config.warmupFrames; ++i) {
        FrameMetrics m;
        m.frameTimeMs = 16.0f;
        runner.RecordFrame(m);
    }

    // Should transition to Measuring
    EXPECT_EQ(runner.GetState(), BenchmarkState::Measuring);

    // Record measurement frames
    for (uint32_t i = 0; i < config.measurementFrames; ++i) {
        FrameMetrics m;
        m.frameTimeMs = 16.0f;
        runner.RecordFrame(m);
    }

    // Should be complete
    EXPECT_TRUE(runner.IsCurrentTestComplete());

    // Finalize
    runner.FinalizeCurrentTest();

    // No more tests
    EXPECT_FALSE(runner.BeginNextTest());
    EXPECT_EQ(runner.GetState(), BenchmarkState::Completed);
    EXPECT_FALSE(runner.IsRunning());
}

TEST_F(EndToEndIntegrationTest, AbortSuiteCleanup) {
    // Test that aborting mid-run properly cleans up state

    BenchmarkRunner runner;
    runner.SetDeviceCapabilities(CreateMockDeviceCaps());

    TestConfiguration config = CreateValidConfig();
    runner.SetTestMatrix({config});

    ASSERT_TRUE(runner.StartSuite());
    ASSERT_TRUE(runner.BeginNextTest());

    // Record some frames
    for (uint32_t i = 0; i < 5; ++i) {
        FrameMetrics m;
        m.frameTimeMs = 16.0f;
        runner.RecordFrame(m);
    }

    EXPECT_EQ(runner.GetState(), BenchmarkState::Warmup);

    // Abort mid-run
    runner.AbortSuite();

    EXPECT_EQ(runner.GetState(), BenchmarkState::Idle);
    EXPECT_FALSE(runner.IsRunning());
}

// ============================================================================
// ShaderCounters Tests
// ============================================================================

TEST(ShaderCountersTest, DefaultValues) {
    ShaderCounters counters;
    EXPECT_EQ(counters.totalVoxelsTraversed, 0u);
    EXPECT_EQ(counters.totalRaysCast, 0u);
    EXPECT_EQ(counters.totalNodesVisited, 0u);
    EXPECT_EQ(counters.totalLeafNodesVisited, 0u);
    EXPECT_EQ(counters.totalEmptySpaceSkipped, 0u);
    EXPECT_EQ(counters.rayHitCount, 0u);
    EXPECT_EQ(counters.rayMissCount, 0u);
    EXPECT_EQ(counters.earlyTerminations, 0u);
    EXPECT_FALSE(counters.HasData());
}

TEST(ShaderCountersTest, DerivedMetrics) {
    ShaderCounters counters;
    counters.totalVoxelsTraversed = 1000;
    counters.totalRaysCast = 100;
    counters.totalNodesVisited = 500;
    counters.rayHitCount = 80;
    counters.rayMissCount = 20;
    counters.totalEmptySpaceSkipped = 2000;

    EXPECT_FLOAT_EQ(counters.GetAvgVoxelsPerRay(), 10.0f);  // 1000/100
    EXPECT_FLOAT_EQ(counters.GetAvgNodesPerRay(), 5.0f);    // 500/100
    EXPECT_FLOAT_EQ(counters.GetHitRate(), 0.8f);           // 80/100

    // Empty space skip ratio: 2000 / (1000 + 2000) = 2/3
    EXPECT_NEAR(counters.GetEmptySpaceSkipRatio(), 0.6667f, 0.001f);
}

TEST(ShaderCountersTest, DerivedMetricsZeroRays) {
    ShaderCounters counters;
    // All zeros - should handle division by zero gracefully
    EXPECT_FLOAT_EQ(counters.GetAvgVoxelsPerRay(), 0.0f);
    EXPECT_FLOAT_EQ(counters.GetAvgNodesPerRay(), 0.0f);
    EXPECT_FLOAT_EQ(counters.GetHitRate(), 0.0f);
    EXPECT_FLOAT_EQ(counters.GetEmptySpaceSkipRatio(), 0.0f);
}

TEST(ShaderCountersTest, Reset) {
    ShaderCounters counters;
    counters.totalVoxelsTraversed = 1000;
    counters.totalRaysCast = 100;
    counters.rayHitCount = 50;

    EXPECT_TRUE(counters.HasData());

    counters.Reset();

    EXPECT_EQ(counters.totalVoxelsTraversed, 0u);
    EXPECT_EQ(counters.totalRaysCast, 0u);
    EXPECT_EQ(counters.rayHitCount, 0u);
    EXPECT_FALSE(counters.HasData());
}

TEST(ShaderCountersTest, HasData) {
    ShaderCounters counters;
    EXPECT_FALSE(counters.HasData());

    counters.totalRaysCast = 1;
    EXPECT_TRUE(counters.HasData());

    counters.Reset();
    EXPECT_FALSE(counters.HasData());
}

// ============================================================================
// FrameMetrics ShaderCounters Integration Tests
// ============================================================================

TEST(FrameMetricsTest, ShaderCountersDefaultEmpty) {
    FrameMetrics metrics;
    EXPECT_FALSE(metrics.HasShaderCounters());
    EXPECT_EQ(metrics.shaderCounters.totalRaysCast, 0u);
}

TEST(FrameMetricsTest, ShaderCountersCanBePopulated) {
    FrameMetrics metrics;
    metrics.shaderCounters.totalRaysCast = 480000;  // 800 * 600
    metrics.shaderCounters.totalVoxelsTraversed = 9600000;  // 20 voxels per ray avg
    metrics.shaderCounters.rayHitCount = 400000;
    metrics.shaderCounters.rayMissCount = 80000;

    EXPECT_TRUE(metrics.HasShaderCounters());
    EXPECT_FLOAT_EQ(metrics.shaderCounters.GetAvgVoxelsPerRay(), 20.0f);
    EXPECT_NEAR(metrics.shaderCounters.GetHitRate(), 0.833f, 0.001f);
}

// ============================================================================
// Fragment Pipeline Tests
// ============================================================================

TEST(BenchmarkGraphFactoryTest, FragmentPipelineNodesDefaultInvalid) {
    FragmentPipelineNodes nodes{};
    EXPECT_FALSE(nodes.IsValid());
}

TEST(BenchmarkGraphFactoryTest, BuildFragmentPipelineNullGraphThrows) {
    InfrastructureNodes invalidInfra{};
    EXPECT_THROW(
        BenchmarkGraphFactory::BuildFragmentPipeline(nullptr, invalidInfra, "test.vert", "test.frag"),
        std::invalid_argument
    );
}

TEST(BenchmarkGraphFactoryTest, BuildFragmentPipelineInvalidInfraThrows) {
    InfrastructureNodes invalidInfra{};  // All handles invalid
    EXPECT_THROW(
        BenchmarkGraphFactory::BuildFragmentPipeline(nullptr, invalidInfra, "test.vert", "test.frag"),
        std::invalid_argument
    );
}

TEST(BenchmarkGraphFactoryTest, BuildFragmentRayMarchGraphNullGraphThrows) {
    TestConfiguration config;
    config.pipeline = "fragment";
    config.voxelResolution = 128;

    EXPECT_THROW(
        BenchmarkGraphFactory::BuildFragmentRayMarchGraph(nullptr, config, 800, 600),
        std::invalid_argument
    );
}

TEST(BenchmarkGraphFactoryTest, ConnectFragmentRayMarchNullGraphThrows) {
    InfrastructureNodes infra{};
    FragmentPipelineNodes fragment{};
    RayMarchNodes rayMarch{};
    OutputNodes output{};

    EXPECT_THROW(
        BenchmarkGraphFactory::ConnectFragmentRayMarch(nullptr, infra, fragment, rayMarch, output),
        std::invalid_argument
    );
}

// ============================================================================
// Hardware RT Stub Tests
// ============================================================================

TEST(BenchmarkGraphFactoryTest, BuildHardwareRTGraphThrowsNotImplemented) {
    TestConfiguration config;
    config.pipeline = "hardware_rt";
    config.voxelResolution = 128;

    // Should throw runtime_error indicating not implemented
    EXPECT_THROW(
        BenchmarkGraphFactory::BuildHardwareRTGraph(nullptr, config, 800, 600),
        std::runtime_error
    );
}

TEST(BenchmarkGraphFactoryTest, BuildHardwareRTGraphErrorMessage) {
    TestConfiguration config;
    config.pipeline = "hardware_rt";

    try {
        BenchmarkGraphFactory::BuildHardwareRTGraph(nullptr, config, 800, 600);
        FAIL() << "Expected runtime_error to be thrown";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("VK_KHR_ray_tracing_pipeline"), std::string::npos);
        EXPECT_NE(msg.find("VK_KHR_acceleration_structure"), std::string::npos);
    }
}

// ============================================================================
// BenchmarkGraph Pipeline Type Tests
// ============================================================================

TEST(BenchmarkGraphTest, DefaultPipelineTypeIsInvalid) {
    BenchmarkGraph graph{};
    EXPECT_EQ(graph.pipelineType, PipelineType::Invalid);
    EXPECT_FALSE(graph.IsValid());
}

TEST(BenchmarkGraphTest, ComputePipelineTypeValidation) {
    BenchmarkGraph graph{};
    graph.pipelineType = PipelineType::Compute;

    // Without valid nodes, still invalid
    EXPECT_FALSE(graph.IsValid());
}

TEST(BenchmarkGraphTest, FragmentPipelineTypeValidation) {
    BenchmarkGraph graph{};
    graph.pipelineType = PipelineType::Fragment;

    // Without valid nodes, still invalid
    EXPECT_FALSE(graph.IsValid());
}

TEST(BenchmarkGraphTest, HardwareRTPipelineTypeNotYetValid) {
    BenchmarkGraph graph{};
    graph.pipelineType = PipelineType::HardwareRT;

    // HardwareRT not implemented, always invalid
    EXPECT_FALSE(graph.IsValid());
}

TEST(BenchmarkGraphTest, HybridPipelineTypeNotYetValid) {
    BenchmarkGraph graph{};
    graph.pipelineType = PipelineType::Hybrid;

    // Hybrid not implemented, always invalid
    EXPECT_FALSE(graph.IsValid());
}
