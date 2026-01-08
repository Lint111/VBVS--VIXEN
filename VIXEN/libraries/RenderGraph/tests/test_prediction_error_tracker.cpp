/**
 * @file test_prediction_error_tracker.cpp
 * @brief Unit tests for PredictionErrorTracker
 *
 * Sprint 6.3: Phase 3.1 - Prediction Error Tracking
 *
 * Tests:
 * - Basic prediction recording
 * - Error ratio computation
 * - Rolling statistics (mean, variance)
 * - Correction factor generation
 * - Bias detection
 * - Global statistics aggregation
 * - Window size management
 * - Edge cases (zero estimates, overflow)
 */

#include <gtest/gtest.h>
#include "Core/PredictionErrorTracker.h"
#include "Core/TimelineCapacityTracker.h"

using namespace Vixen::RenderGraph;

// ============================================================================
// TEST FIXTURE
// ============================================================================

class PredictionErrorTrackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        tracker = std::make_unique<PredictionErrorTracker>();
    }

    std::unique_ptr<PredictionErrorTracker> tracker;
};

// ============================================================================
// BASIC RECORDING TESTS
// ============================================================================

TEST_F(PredictionErrorTrackerTest, RecordSinglePrediction) {
    tracker->RecordPrediction("task1", 1'000'000, 1'200'000, 0);

    EXPECT_EQ(tracker->GetTotalSamples(), 1);
    EXPECT_EQ(tracker->GetTaskTypeCount(), 1);

    const auto* stats = tracker->GetTaskStats("task1");
    ASSERT_NE(stats, nullptr);
    EXPECT_EQ(stats->sampleCount, 1);
    EXPECT_EQ(stats->taskId, "task1");
}

TEST_F(PredictionErrorTrackerTest, RecordMultiplePredictionsSameTask) {
    tracker->RecordPrediction("task1", 1'000'000, 1'200'000, 0);
    tracker->RecordPrediction("task1", 1'000'000, 1'100'000, 1);
    tracker->RecordPrediction("task1", 1'000'000, 1'300'000, 2);

    EXPECT_EQ(tracker->GetTotalSamples(), 3);
    EXPECT_EQ(tracker->GetTaskTypeCount(), 1);

    const auto* stats = tracker->GetTaskStats("task1");
    ASSERT_NE(stats, nullptr);
    EXPECT_EQ(stats->sampleCount, 3);
}

TEST_F(PredictionErrorTrackerTest, RecordPredictionsDifferentTasks) {
    tracker->RecordPrediction("shadowMap", 2'000'000, 2'500'000, 0);
    tracker->RecordPrediction("postProcess", 1'000'000, 800'000, 0);
    tracker->RecordPrediction("lighting", 3'000'000, 3'000'000, 0);

    EXPECT_EQ(tracker->GetTotalSamples(), 3);
    EXPECT_EQ(tracker->GetTaskTypeCount(), 3);

    EXPECT_NE(tracker->GetTaskStats("shadowMap"), nullptr);
    EXPECT_NE(tracker->GetTaskStats("postProcess"), nullptr);
    EXPECT_NE(tracker->GetTaskStats("lighting"), nullptr);
}

// ============================================================================
// ERROR COMPUTATION TESTS
// ============================================================================

TEST_F(PredictionErrorTrackerTest, ErrorRatioPerfectEstimate) {
    // Actual = Estimate => ratio = 1.0
    tracker->RecordPrediction("perfect", 1'000'000, 1'000'000, 0);

    const auto* stats = tracker->GetTaskStats("perfect");
    ASSERT_NE(stats, nullptr);

    const auto* lastError = stats->GetLastError();
    ASSERT_NE(lastError, nullptr);

    EXPECT_EQ(lastError->errorNs, 0);
    EXPECT_FLOAT_EQ(lastError->errorRatio, 1.0f);
}

TEST_F(PredictionErrorTrackerTest, ErrorRatioUnderestimate) {
    // Actual > Estimate => underestimate, ratio > 1.0
    tracker->RecordPrediction("underest", 1'000'000, 1'500'000, 0);  // 50% under

    const auto* stats = tracker->GetTaskStats("underest");
    ASSERT_NE(stats, nullptr);

    const auto* lastError = stats->GetLastError();
    ASSERT_NE(lastError, nullptr);

    EXPECT_EQ(lastError->errorNs, 500'000);  // actual - estimated
    EXPECT_FLOAT_EQ(lastError->errorRatio, 1.5f);
}

TEST_F(PredictionErrorTrackerTest, ErrorRatioOverestimate) {
    // Actual < Estimate => overestimate, ratio < 1.0
    tracker->RecordPrediction("overest", 2'000'000, 1'000'000, 0);  // 50% over

    const auto* stats = tracker->GetTaskStats("overest");
    ASSERT_NE(stats, nullptr);

    const auto* lastError = stats->GetLastError();
    ASSERT_NE(lastError, nullptr);

    EXPECT_EQ(lastError->errorNs, -1'000'000);  // actual - estimated
    EXPECT_FLOAT_EQ(lastError->errorRatio, 0.5f);
}

// ============================================================================
// ROLLING STATISTICS TESTS
// ============================================================================

TEST_F(PredictionErrorTrackerTest, MeanErrorRatioComputation) {
    // Record consistent underestimates (actual 20% higher)
    for (int i = 0; i < 20; ++i) {
        tracker->RecordPrediction("consistent", 1'000'000, 1'200'000, i);
    }

    const auto* stats = tracker->GetTaskStats("consistent");
    ASSERT_NE(stats, nullptr);

    // Mean ratio should be ~1.2
    EXPECT_NEAR(stats->meanErrorRatio, 1.2f, 0.01f);
    // Variance should be low (consistent errors)
    EXPECT_LT(stats->stdDevRatio, 0.05f);
}

TEST_F(PredictionErrorTrackerTest, VarianceComputation) {
    // Record varying errors to test variance computation
    tracker->RecordPrediction("varied", 1'000'000, 1'000'000, 0);  // ratio 1.0
    tracker->RecordPrediction("varied", 1'000'000, 1'500'000, 1);  // ratio 1.5
    tracker->RecordPrediction("varied", 1'000'000, 500'000, 2);    // ratio 0.5
    tracker->RecordPrediction("varied", 1'000'000, 2'000'000, 3);  // ratio 2.0

    const auto* stats = tracker->GetTaskStats("varied");
    ASSERT_NE(stats, nullptr);

    // Mean should be (1.0 + 1.5 + 0.5 + 2.0) / 4 = 1.25
    EXPECT_NEAR(stats->meanErrorRatio, 1.25f, 0.01f);
    // Variance should be significant due to varied data
    EXPECT_GT(stats->varianceRatio, 0.1f);
}

// ============================================================================
// CORRECTION FACTOR TESTS
// ============================================================================

TEST_F(PredictionErrorTrackerTest, CorrectionFactorNoData) {
    // No data => correction factor should be 1.0
    float correction = tracker->GetCorrectionFactor("unknown");
    EXPECT_FLOAT_EQ(correction, 1.0f);
}

TEST_F(PredictionErrorTrackerTest, CorrectionFactorUnderestimate) {
    // Consistent 25% underestimate
    for (int i = 0; i < 15; ++i) {
        tracker->RecordPrediction("underest", 1'000'000, 1'250'000, i);
    }

    // Correction should move toward 1.25
    float correction = tracker->GetCorrectionFactor("underest");
    EXPECT_GT(correction, 1.0f);  // Should increase estimates
    EXPECT_LT(correction, 1.5f);  // But smoothed
}

TEST_F(PredictionErrorTrackerTest, CorrectionFactorOverestimate) {
    // Consistent 20% overestimate
    for (int i = 0; i < 15; ++i) {
        tracker->RecordPrediction("overest", 1'000'000, 800'000, i);
    }

    // Correction should move toward 0.8
    float correction = tracker->GetCorrectionFactor("overest");
    EXPECT_LT(correction, 1.0f);  // Should decrease estimates
    EXPECT_GT(correction, 0.5f);  // But smoothed
}

TEST_F(PredictionErrorTrackerTest, CorrectionFactorClampedBounds) {
    PredictionErrorTracker::Config config;
    config.windowSize = 10;
    PredictionErrorTracker boundedTracker(config);

    // Extreme underestimate (10x)
    for (int i = 0; i < 20; ++i) {
        boundedTracker.RecordPrediction("extreme", 1'000'000, 10'000'000, i);
    }

    // Correction should be clamped to max 2.0
    float correction = boundedTracker.GetCorrectionFactor("extreme");
    EXPECT_LE(correction, 2.0f);
    EXPECT_GE(correction, 0.5f);
}

// ============================================================================
// BIAS DETECTION TESTS
// ============================================================================

TEST_F(PredictionErrorTrackerTest, BiasDirectionUnderestimate) {
    // Consistent underestimate
    for (int i = 0; i < 20; ++i) {
        tracker->RecordPrediction("bias", 1'000'000, 1'300'000, i);
    }

    float bias = tracker->GetBiasDirection("bias");
    EXPECT_GT(bias, 0.0f);  // Positive bias = underestimate
}

TEST_F(PredictionErrorTrackerTest, BiasDirectionOverestimate) {
    // Consistent overestimate
    for (int i = 0; i < 20; ++i) {
        tracker->RecordPrediction("bias", 1'000'000, 700'000, i);
    }

    float bias = tracker->GetBiasDirection("bias");
    EXPECT_LT(bias, 0.0f);  // Negative bias = overestimate
}

TEST_F(PredictionErrorTrackerTest, BiasConfidenceConsistent) {
    // Highly consistent errors => high confidence
    for (int i = 0; i < 20; ++i) {
        tracker->RecordPrediction("consistent", 1'000'000, 1'200'000, i);
    }

    const auto* stats = tracker->GetTaskStats("consistent");
    ASSERT_NE(stats, nullptr);
    EXPECT_GT(stats->biasConfidence, 0.5f);
}

// ============================================================================
// GLOBAL STATISTICS TESTS
// ============================================================================

TEST_F(PredictionErrorTrackerTest, GlobalStatsEmpty) {
    auto global = tracker->GetGlobalStats();
    EXPECT_EQ(global.totalSamples, 0);
    EXPECT_EQ(global.taskTypeCount, 0);
}

TEST_F(PredictionErrorTrackerTest, GlobalStatsAggregation) {
    // Mix of accurate, under, and over estimates
    tracker->RecordPrediction("accurate", 1'000'000, 1'000'000, 0);   // Accurate
    tracker->RecordPrediction("under", 1'000'000, 1'500'000, 0);      // Underestimate
    tracker->RecordPrediction("over", 1'000'000, 500'000, 0);         // Overestimate

    auto global = tracker->GetGlobalStats();
    EXPECT_EQ(global.totalSamples, 3);
    EXPECT_EQ(global.taskTypeCount, 3);

    // One accurate (within 10%), one under, one over
    EXPECT_GT(global.accuratePercent, 0.0f);
    EXPECT_GT(global.underestimatePercent, 0.0f);
    EXPECT_GT(global.overestimatePercent, 0.0f);
}

// ============================================================================
// WINDOW SIZE TESTS
// ============================================================================

TEST_F(PredictionErrorTrackerTest, WindowSizeTrimming) {
    PredictionErrorTracker::Config config;
    config.windowSize = 5;
    PredictionErrorTracker smallWindow(config);

    // Record more samples than window size
    for (int i = 0; i < 10; ++i) {
        smallWindow.RecordPrediction("task", 1'000'000, 1'100'000 + i * 10'000, i);
    }

    const auto* stats = smallWindow.GetTaskStats("task");
    ASSERT_NE(stats, nullptr);

    // History should be trimmed to window size
    EXPECT_EQ(stats->history.size(), 5);
    // But total sample count should still be 10
    EXPECT_EQ(stats->sampleCount, 10);
}

// ============================================================================
// EDGE CASES
// ============================================================================

TEST_F(PredictionErrorTrackerTest, ZeroEstimate) {
    // Zero estimate should be handled gracefully
    tracker->RecordPrediction("zero", 0, 1'000'000, 0);

    const auto* stats = tracker->GetTaskStats("zero");
    ASSERT_NE(stats, nullptr);

    const auto* lastError = stats->GetLastError();
    ASSERT_NE(lastError, nullptr);

    // Error ratio should be large but not infinite
    EXPECT_EQ(lastError->errorRatio, 10.0f);  // Capped at 10x
}

TEST_F(PredictionErrorTrackerTest, ZeroActual) {
    // Zero actual should work
    tracker->RecordPrediction("zeroactutal", 1'000'000, 0, 0);

    const auto* stats = tracker->GetTaskStats("zeroactutal");
    ASSERT_NE(stats, nullptr);

    const auto* lastError = stats->GetLastError();
    ASSERT_NE(lastError, nullptr);

    EXPECT_EQ(lastError->errorRatio, 0.0f);
    EXPECT_EQ(lastError->errorNs, -1'000'000);
}

TEST_F(PredictionErrorTrackerTest, ClearStatistics) {
    tracker->RecordPrediction("task1", 1'000'000, 1'200'000, 0);
    tracker->RecordPrediction("task2", 1'000'000, 800'000, 0);

    EXPECT_EQ(tracker->GetTotalSamples(), 2);

    tracker->Clear();

    EXPECT_EQ(tracker->GetTotalSamples(), 0);
    EXPECT_EQ(tracker->GetTaskTypeCount(), 0);
    EXPECT_EQ(tracker->GetTaskStats("task1"), nullptr);
}

TEST_F(PredictionErrorTrackerTest, ClearSingleTask) {
    tracker->RecordPrediction("task1", 1'000'000, 1'200'000, 0);
    tracker->RecordPrediction("task2", 1'000'000, 800'000, 0);

    tracker->ClearTask("task1");

    EXPECT_EQ(tracker->GetTaskStats("task1"), nullptr);
    EXPECT_NE(tracker->GetTaskStats("task2"), nullptr);
}

TEST_F(PredictionErrorTrackerTest, ReliableStatsThreshold) {
    // Less than 10 samples = not reliable
    for (int i = 0; i < 5; ++i) {
        tracker->RecordPrediction("fewsamples", 1'000'000, 1'200'000, i);
    }
    EXPECT_FALSE(tracker->HasReliableStats("fewsamples"));

    // 10+ samples = reliable
    for (int i = 5; i < 15; ++i) {
        tracker->RecordPrediction("fewsamples", 1'000'000, 1'200'000, i);
    }
    EXPECT_TRUE(tracker->HasReliableStats("fewsamples"));
}

// ============================================================================
// TIMELINE CAPACITY TRACKER INTEGRATION TESTS
// ============================================================================

class TimelineCapacityTrackerPredictionTest : public ::testing::Test {
protected:
    TimelineCapacityTracker tracker;
};

TEST_F(TimelineCapacityTrackerPredictionTest, RecordPredictionViaTracker) {
    tracker.RecordPrediction("shadowMap", 2'000'000, 2'500'000);

    const auto* stats = tracker.GetPredictionStats("shadowMap");
    ASSERT_NE(stats, nullptr);
    EXPECT_EQ(stats->sampleCount, 1);
}

TEST_F(TimelineCapacityTrackerPredictionTest, GetCorrectedEstimate) {
    // Build up some data
    for (int i = 0; i < 15; ++i) {
        tracker.RecordPrediction("render", 1'000'000, 1'200'000);  // 20% under
    }

    // Get corrected estimate
    uint64_t original = 1'000'000;
    uint64_t corrected = tracker.GetCorrectedEstimate("render", original);

    // Corrected should be higher than original
    EXPECT_GT(corrected, original);
}

TEST_F(TimelineCapacityTrackerPredictionTest, GetGlobalPredictionStats) {
    tracker.RecordPrediction("task1", 1'000'000, 1'100'000);
    tracker.RecordPrediction("task2", 1'000'000, 900'000);

    auto global = tracker.GetGlobalPredictionStats();
    EXPECT_EQ(global.totalSamples, 2);
    EXPECT_EQ(global.taskTypeCount, 2);
}

TEST_F(TimelineCapacityTrackerPredictionTest, DirectTrackerAccess) {
    auto& predTracker = tracker.GetPredictionTracker();
    predTracker.RecordPrediction("direct", 1'000'000, 1'500'000, 0);

    EXPECT_EQ(tracker.GetPredictionTracker().GetTotalSamples(), 1);
}
