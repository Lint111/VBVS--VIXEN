#include <gtest/gtest.h>
#include "Profiler/RollingStats.h"
#include "Profiler/BenchmarkConfig.h"
#include "Profiler/FrameMetrics.h"

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
