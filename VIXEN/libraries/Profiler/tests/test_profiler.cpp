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
