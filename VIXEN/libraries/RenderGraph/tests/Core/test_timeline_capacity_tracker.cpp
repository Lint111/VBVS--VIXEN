// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

#include <gtest/gtest.h>
#include "Core/TimelineCapacityTracker.h"
#include <thread>
#include <chrono>

using namespace Vixen::RenderGraph;

// =============================================================================
// Test Fixture
// =============================================================================

class TimelineCapacityTrackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Default config for most tests (single GPU queue, single CPU thread)
        defaultConfig_.numGPUQueues = 1;
        defaultConfig_.numCPUThreads = 1;
        defaultConfig_.gpuTimeBudgetNs = 16'666'666;  // 60 FPS
        defaultConfig_.cpuTimeBudgetNs = 8'000'000;   // 8ms
        defaultConfig_.historyDepth = 60;
        defaultConfig_.adaptiveThreshold = 0.90f;
        defaultConfig_.enableAdaptiveScheduling = true;
        defaultConfig_.hysteresisDamping = 0.10f;
        defaultConfig_.hysteresisDeadband = 0.05f;
    }

    TimelineCapacityTracker::Config defaultConfig_;
};

// =============================================================================
// Phase 1.1: TimelineCapacityTracker Foundation
// =============================================================================

TEST_F(TimelineCapacityTrackerTest, ConstructorInitializesConfig) {
    TimelineCapacityTracker tracker(defaultConfig_);

    const auto& config = tracker.GetConfig();
    EXPECT_EQ(config.numGPUQueues, 1);
    EXPECT_EQ(config.numCPUThreads, 1);
    EXPECT_EQ(config.gpuTimeBudgetNs, 16'666'666);
    EXPECT_EQ(config.cpuTimeBudgetNs, 8'000'000);
    EXPECT_EQ(config.historyDepth, 60);
    EXPECT_FLOAT_EQ(config.adaptiveThreshold, 0.90f);
    EXPECT_TRUE(config.enableAdaptiveScheduling);
    EXPECT_FLOAT_EQ(config.hysteresisDamping, 0.10f);
    EXPECT_FLOAT_EQ(config.hysteresisDeadband, 0.05f);
}

TEST_F(TimelineCapacityTrackerTest, DefaultConstructorUsesDefaults) {
    TimelineCapacityTracker tracker;

    const auto& config = tracker.GetConfig();
    EXPECT_EQ(config.gpuTimeBudgetNs, 16'666'666);  // 60 FPS default
    EXPECT_EQ(config.cpuTimeBudgetNs, 8'000'000);   // 8ms default
    EXPECT_EQ(config.historyDepth, 60);             // 60 frames default
}

TEST_F(TimelineCapacityTrackerTest, MultiDeviceTopology) {
    defaultConfig_.numGPUQueues = 3;  // Graphics, compute, transfer
    defaultConfig_.numCPUThreads = 4; // 4 CPU cores
    TimelineCapacityTracker tracker(defaultConfig_);

    const auto& timeline = tracker.GetCurrentTimeline();
    EXPECT_EQ(timeline.gpuQueues.size(), 3);
    EXPECT_EQ(timeline.cpuThreads.size(), 4);

    // All devices should have correct budgets
    for (const auto& gpu : timeline.gpuQueues) {
        EXPECT_EQ(gpu.budgetNs, 16'666'666);
    }
    for (const auto& cpu : timeline.cpuThreads) {
        EXPECT_EQ(cpu.budgetNs, 8'000'000);
    }
}

TEST_F(TimelineCapacityTrackerTest, BeginFrameResetsCurrentFrame) {
    TimelineCapacityTracker tracker(defaultConfig_);

    // Record some measurements
    tracker.RecordGPUTime(1'000'000);  // 1ms
    tracker.RecordCPUTime(500'000);    // 0.5ms

    // Begin new frame should reset
    tracker.BeginFrame();

    const auto& timeline = tracker.GetCurrentTimeline();
    EXPECT_EQ(timeline.gpuQueues[0].measuredNs, 0);
    EXPECT_EQ(timeline.cpuThreads[0].measuredNs, 0);
    EXPECT_EQ(timeline.gpuQueues[0].taskCount, 0);
    EXPECT_EQ(timeline.cpuThreads[0].taskCount, 0);
}

TEST_F(TimelineCapacityTrackerTest, FrameNumberIncrementsOnBeginFrame) {
    TimelineCapacityTracker tracker(defaultConfig_);

    EXPECT_EQ(tracker.GetCurrentTimeline().frameNumber, 0);

    tracker.BeginFrame();
    EXPECT_EQ(tracker.GetCurrentTimeline().frameNumber, 1);

    tracker.BeginFrame();
    EXPECT_EQ(tracker.GetCurrentTimeline().frameNumber, 2);

    tracker.BeginFrame();
    EXPECT_EQ(tracker.GetCurrentTimeline().frameNumber, 3);
}

TEST_F(TimelineCapacityTrackerTest, BudgetsArePreservedAfterReset) {
    TimelineCapacityTracker tracker(defaultConfig_);

    tracker.BeginFrame();

    const auto& timeline = tracker.GetCurrentTimeline();
    EXPECT_EQ(timeline.gpuQueues[0].budgetNs, 16'666'666);
    EXPECT_EQ(timeline.cpuThreads[0].budgetNs, 8'000'000);
}

// =============================================================================
// Phase 1.2: Measurement Recording
// =============================================================================

TEST_F(TimelineCapacityTrackerTest, RecordGPUTimeAccumulates) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    tracker.RecordGPUTime(1'000'000);  // 1ms
    tracker.RecordGPUTime(2'000'000);  // 2ms
    tracker.RecordGPUTime(500'000);    // 0.5ms

    const auto& timeline = tracker.GetCurrentTimeline();
    EXPECT_EQ(timeline.gpuQueues[0].measuredNs, 3'500'000);  // 3.5ms total
    EXPECT_EQ(timeline.gpuQueues[0].taskCount, 3);
}

TEST_F(TimelineCapacityTrackerTest, RecordCPUTimeAccumulates) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    tracker.RecordCPUTime(500'000);   // 0.5ms
    tracker.RecordCPUTime(1'000'000); // 1ms
    tracker.RecordCPUTime(250'000);   // 0.25ms

    const auto& timeline = tracker.GetCurrentTimeline();
    EXPECT_EQ(timeline.cpuThreads[0].measuredNs, 1'750'000);  // 1.75ms total
    EXPECT_EQ(timeline.cpuThreads[0].taskCount, 3);
}

TEST_F(TimelineCapacityTrackerTest, RecordGPUTimeMultipleQueues) {
    defaultConfig_.numGPUQueues = 3;
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // Record to different queues
    tracker.RecordGPUTime(0, 2'000'000);  // Graphics: 2ms
    tracker.RecordGPUTime(1, 1'000'000);  // Compute: 1ms
    tracker.RecordGPUTime(2, 500'000);    // Transfer: 0.5ms

    const auto& timeline = tracker.GetCurrentTimeline();
    EXPECT_EQ(timeline.gpuQueues[0].measuredNs, 2'000'000);
    EXPECT_EQ(timeline.gpuQueues[1].measuredNs, 1'000'000);
    EXPECT_EQ(timeline.gpuQueues[2].measuredNs, 500'000);
}

TEST_F(TimelineCapacityTrackerTest, RecordCPUTimeMultipleThreads) {
    defaultConfig_.numCPUThreads = 4;
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // Record to different threads
    tracker.RecordCPUTime(0, 1'000'000);  // Thread 0: 1ms
    tracker.RecordCPUTime(1, 2'000'000);  // Thread 1: 2ms
    tracker.RecordCPUTime(2, 500'000);    // Thread 2: 0.5ms
    tracker.RecordCPUTime(3, 1'500'000);  // Thread 3: 1.5ms

    const auto& timeline = tracker.GetCurrentTimeline();
    EXPECT_EQ(timeline.cpuThreads[0].measuredNs, 1'000'000);
    EXPECT_EQ(timeline.cpuThreads[1].measuredNs, 2'000'000);
    EXPECT_EQ(timeline.cpuThreads[2].measuredNs, 500'000);
    EXPECT_EQ(timeline.cpuThreads[3].measuredNs, 1'500'000);
}

TEST_F(TimelineCapacityTrackerTest, InvalidQueueIndexIgnored) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // Record to invalid queue (only queue 0 exists)
    tracker.RecordGPUTime(0, 1'000'000);  // Valid
    tracker.RecordGPUTime(5, 2'000'000);  // Invalid - ignored

    const auto& timeline = tracker.GetCurrentTimeline();
    EXPECT_EQ(timeline.gpuQueues[0].measuredNs, 1'000'000);  // Only valid recording
}

TEST_F(TimelineCapacityTrackerTest, UtilizationComputedInRealTime) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // Record 50% of GPU budget
    tracker.RecordGPUTime(8'333'333);  // 8.33ms (50% of 16.67ms)

    const auto& timeline = tracker.GetCurrentTimeline();
    EXPECT_NEAR(timeline.gpuQueues[0].utilization, 0.50f, 0.01f);
    EXPECT_EQ(timeline.gpuQueues[0].remainingNs, 8'333'333);  // 50% remaining
    EXPECT_FALSE(timeline.gpuQueues[0].exceededBudget);
}

TEST_F(TimelineCapacityTrackerTest, UtilizationExceedsOne) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // Record 120% of GPU budget
    tracker.RecordGPUTime(20'000'000);  // 20ms (120% of 16.67ms)

    const auto& timeline = tracker.GetCurrentTimeline();
    EXPECT_NEAR(timeline.gpuQueues[0].utilization, 1.20f, 0.01f);
    EXPECT_EQ(timeline.gpuQueues[0].remainingNs, 0);  // No budget remaining
    EXPECT_TRUE(timeline.gpuQueues[0].exceededBudget);
}

TEST_F(TimelineCapacityTrackerTest, RemainingBudgetCalculatedCorrectly) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // Use 10ms of 16.67ms GPU budget
    tracker.RecordGPUTime(10'000'000);

    EXPECT_EQ(tracker.GetGPURemainingBudget(), 6'666'666);
}

TEST_F(TimelineCapacityTrackerTest, GetMinGPURemainingBudget) {
    defaultConfig_.numGPUQueues = 3;
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // Queue 0: 40% used (10ms remaining)
    tracker.RecordGPUTime(0, 6'666'666);
    // Queue 1: 60% used (6.67ms remaining) <- minimum
    tracker.RecordGPUTime(1, 10'000'000);
    // Queue 2: 30% used (11.67ms remaining)
    tracker.RecordGPUTime(2, 5'000'000);

    EXPECT_EQ(tracker.GetMinGPURemainingBudget(), 6'666'666);
}

TEST_F(TimelineCapacityTrackerTest, CanScheduleMoreWorkBelowThreshold) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 80% utilization (below 90% threshold)
    tracker.RecordGPUTime(13'333'333);

    EXPECT_TRUE(tracker.CanScheduleMoreWork());
    EXPECT_FALSE(tracker.IsOverBudget());
}

TEST_F(TimelineCapacityTrackerTest, CannotScheduleMoreWorkAboveThreshold) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 95% utilization (above 90% threshold)
    tracker.RecordGPUTime(15'833'333);

    EXPECT_FALSE(tracker.CanScheduleMoreWork());
    EXPECT_FALSE(tracker.IsOverBudget());
}

TEST_F(TimelineCapacityTrackerTest, IsOverBudgetWhenExceeds100Percent) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 110% utilization
    tracker.RecordGPUTime(18'333'333);

    EXPECT_FALSE(tracker.CanScheduleMoreWork());
    EXPECT_TRUE(tracker.IsOverBudget());
}

// =============================================================================
// Phase 1.3: History & Statistics Tracking
// =============================================================================

TEST_F(TimelineCapacityTrackerTest, EndFrameStoresInHistory) {
    TimelineCapacityTracker tracker(defaultConfig_);

    // Frame 1
    tracker.BeginFrame();
    tracker.RecordGPUTime(8'000'000);  // 8ms
    tracker.EndFrame();

    // Frame 2
    tracker.BeginFrame();
    tracker.RecordGPUTime(10'000'000);  // 10ms
    tracker.EndFrame();

    const auto& history = tracker.GetHistory();
    ASSERT_EQ(history.size(), 2);

    EXPECT_EQ(history[0].gpuQueues[0].measuredNs, 8'000'000);
    EXPECT_EQ(history[1].gpuQueues[0].measuredNs, 10'000'000);
}

TEST_F(TimelineCapacityTrackerTest, HistoryTrimsToMaxDepth) {
    defaultConfig_.historyDepth = 5;  // Small history for test
    TimelineCapacityTracker tracker(defaultConfig_);

    // Simulate 10 frames
    for (int i = 0; i < 10; ++i) {
        tracker.BeginFrame();
        tracker.RecordGPUTime(1'000'000);
        tracker.EndFrame();
    }

    const auto& history = tracker.GetHistory();
    EXPECT_EQ(history.size(), 5);  // Trimmed to max depth
}

TEST_F(TimelineCapacityTrackerTest, GetAverageGPUUtilizationOverSingleFrame) {
    TimelineCapacityTracker tracker(defaultConfig_);

    tracker.BeginFrame();
    tracker.RecordGPUTime(8'333'333);  // 50% of 16.67ms
    tracker.EndFrame();

    float avgUtil = tracker.GetAverageGPUUtilization(1);
    EXPECT_NEAR(avgUtil, 0.50f, 0.01f);
}

TEST_F(TimelineCapacityTrackerTest, GetAverageGPUUtilizationOverMultipleFrames) {
    TimelineCapacityTracker tracker(defaultConfig_);

    // Frame 1: 50% utilization
    tracker.BeginFrame();
    tracker.RecordGPUTime(8'333'333);
    tracker.EndFrame();

    // Frame 2: 70% utilization
    tracker.BeginFrame();
    tracker.RecordGPUTime(11'666'666);
    tracker.EndFrame();

    // Frame 3: 90% utilization
    tracker.BeginFrame();
    tracker.RecordGPUTime(15'000'000);
    tracker.EndFrame();

    // Average: (0.50 + 0.70 + 0.90) / 3 = 0.70
    float avgUtil = tracker.GetAverageGPUUtilization(3);
    EXPECT_NEAR(avgUtil, 0.70f, 0.01f);
}

TEST_F(TimelineCapacityTrackerTest, GetAverageCPUUtilization) {
    TimelineCapacityTracker tracker(defaultConfig_);

    // Frame 1: 25% CPU utilization (2ms of 8ms)
    tracker.BeginFrame();
    tracker.RecordCPUTime(2'000'000);
    tracker.EndFrame();

    // Frame 2: 50% CPU utilization (4ms of 8ms)
    tracker.BeginFrame();
    tracker.RecordCPUTime(4'000'000);
    tracker.EndFrame();

    // Average: (0.25 + 0.50) / 2 = 0.375
    float avgUtil = tracker.GetAverageCPUUtilization(2);
    EXPECT_NEAR(avgUtil, 0.375f, 0.01f);
}

TEST_F(TimelineCapacityTrackerTest, AverageUtilizationLimitsToAvailableFrames) {
    TimelineCapacityTracker tracker(defaultConfig_);

    // Only 2 frames available
    tracker.BeginFrame();
    tracker.RecordGPUTime(8'000'000);
    tracker.EndFrame();

    tracker.BeginFrame();
    tracker.RecordGPUTime(10'000'000);
    tracker.EndFrame();

    // Request 10 frames, but only 2 available
    float avgUtil = tracker.GetAverageGPUUtilization(10);
    // Should average over 2 frames, not fail
    EXPECT_GT(avgUtil, 0.0f);
}

TEST_F(TimelineCapacityTrackerTest, GetMaxGPUUtilization) {
    defaultConfig_.numGPUQueues = 3;
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // Different utilizations per queue
    tracker.RecordGPUTime(0, 8'333'333);   // 50%
    tracker.RecordGPUTime(1, 13'333'333);  // 80% <- maximum
    tracker.RecordGPUTime(2, 6'666'666);   // 40%

    const auto& timeline = tracker.GetCurrentTimeline();
    EXPECT_NEAR(timeline.GetMaxGPUUtilization(), 0.80f, 0.01f);
}

TEST_F(TimelineCapacityTrackerTest, BottleneckDetectionNone) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 50% GPU, 40% CPU (both under 90%)
    tracker.RecordGPUTime(8'333'333);
    tracker.RecordCPUTime(3'200'000);
    tracker.EndFrame();

    const auto& timeline = tracker.GetHistory().back();
    EXPECT_EQ(timeline.GetBottleneck(), SystemTimeline::Bottleneck::None);
}

TEST_F(TimelineCapacityTrackerTest, BottleneckDetectionGPU) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 95% GPU, 50% CPU
    tracker.RecordGPUTime(15'833'333);
    tracker.RecordCPUTime(4'000'000);
    tracker.EndFrame();

    const auto& timeline = tracker.GetHistory().back();
    EXPECT_EQ(timeline.GetBottleneck(), SystemTimeline::Bottleneck::GPU);
}

TEST_F(TimelineCapacityTrackerTest, BottleneckDetectionCPU) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 50% GPU, 95% CPU
    tracker.RecordGPUTime(8'333'333);
    tracker.RecordCPUTime(7'600'000);
    tracker.EndFrame();

    const auto& timeline = tracker.GetHistory().back();
    EXPECT_EQ(timeline.GetBottleneck(), SystemTimeline::Bottleneck::CPU);
}

TEST_F(TimelineCapacityTrackerTest, BottleneckDetectionHighestWins) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 92% GPU, 95% CPU (CPU higher)
    tracker.RecordGPUTime(15'333'333);
    tracker.RecordCPUTime(7'600'000);
    tracker.EndFrame();

    const auto& timeline = tracker.GetHistory().back();
    EXPECT_EQ(timeline.GetBottleneck(), SystemTimeline::Bottleneck::CPU);
}

// =============================================================================
// Phase 1.4: Damped Hysteresis System
// =============================================================================

TEST_F(TimelineCapacityTrackerTest, HysteresisDeadbandNoChange) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 92% utilization (target 92%, delta=0%, within 5% deadband)
    tracker.RecordGPUTime(15'333'333);

    float scale = tracker.ComputeTaskCountScale();
    EXPECT_FLOAT_EQ(scale, 1.0f);  // No change
}

TEST_F(TimelineCapacityTrackerTest, HysteresisIncreasesBelowTarget) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 70% utilization (target 92%, delta=-22%)
    tracker.RecordGPUTime(11'666'666);

    float scale = tracker.ComputeTaskCountScale();
    EXPECT_GT(scale, 1.0f);  // Should increase
    EXPECT_LE(scale, 1.10f); // Max +10% damping
}

TEST_F(TimelineCapacityTrackerTest, HysteresisDecreasesAboveTarget) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 110% utilization (target 92%, delta=+18%)
    tracker.RecordGPUTime(18'333'333);

    float scale = tracker.ComputeTaskCountScale();
    EXPECT_LT(scale, 1.0f);  // Should decrease
    EXPECT_GE(scale, 0.90f); // Max -10% damping
}

TEST_F(TimelineCapacityTrackerTest, HysteresisClampsTo10Percent) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 20% utilization (target 92%, delta=-72% - extreme low)
    tracker.RecordGPUTime(3'333'333);

    float scale = tracker.ComputeTaskCountScale();
    EXPECT_LE(scale, 1.10f);  // Clamped to max +10%
}

TEST_F(TimelineCapacityTrackerTest, HysteresisDisabledWhenAdaptiveOff) {
    defaultConfig_.enableAdaptiveScheduling = false;
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 70% utilization
    tracker.RecordGPUTime(11'666'666);

    float scale = tracker.ComputeTaskCountScale();
    EXPECT_FLOAT_EQ(scale, 1.0f);  // No scaling when disabled
}

TEST_F(TimelineCapacityTrackerTest, SuggestAdditionalTasksWhenBelowThreshold) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 50% utilization (8.33ms remaining in 16.67ms budget)
    tracker.RecordGPUTime(8'333'333);

    // Each task costs 2ms
    uint32_t additionalTasks = tracker.SuggestAdditionalTasks(2'000'000);
    EXPECT_EQ(additionalTasks, 4);  // 8.33ms / 2ms = 4 tasks
}

TEST_F(TimelineCapacityTrackerTest, SuggestAdditionalTasksZeroWhenOverThreshold) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 95% utilization (above 90% threshold)
    tracker.RecordGPUTime(15'833'333);

    uint32_t additionalTasks = tracker.SuggestAdditionalTasks(2'000'000);
    EXPECT_EQ(additionalTasks, 0);  // No additional tasks
}

TEST_F(TimelineCapacityTrackerTest, SuggestAdditionalTasksZeroWhenOverBudget) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 110% utilization
    tracker.RecordGPUTime(18'333'333);

    uint32_t additionalTasks = tracker.SuggestAdditionalTasks(2'000'000);
    EXPECT_EQ(additionalTasks, 0);  // No additional tasks
}

TEST_F(TimelineCapacityTrackerTest, SuggestAdditionalTasksDisabledWhenAdaptiveOff) {
    defaultConfig_.enableAdaptiveScheduling = false;
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    // 50% utilization
    tracker.RecordGPUTime(8'333'333);

    uint32_t additionalTasks = tracker.SuggestAdditionalTasks(2'000'000);
    EXPECT_EQ(additionalTasks, 0);  // Disabled
}

// =============================================================================
// Configuration Management
// =============================================================================

TEST_F(TimelineCapacityTrackerTest, SetGPUBudgetUpdatesAllQueues) {
    defaultConfig_.numGPUQueues = 3;
    TimelineCapacityTracker tracker(defaultConfig_);

    tracker.SetGPUBudget(33'333'333);  // 30 FPS

    EXPECT_EQ(tracker.GetConfig().gpuTimeBudgetNs, 33'333'333);
    for (const auto& gpu : tracker.GetCurrentTimeline().gpuQueues) {
        EXPECT_EQ(gpu.budgetNs, 33'333'333);
    }
}

TEST_F(TimelineCapacityTrackerTest, SetGPUBudgetForSpecificQueue) {
    defaultConfig_.numGPUQueues = 3;
    TimelineCapacityTracker tracker(defaultConfig_);

    tracker.SetGPUBudget(1, 20'000'000);  // Set queue 1 to different budget

    EXPECT_EQ(tracker.GetCurrentTimeline().gpuQueues[0].budgetNs, 16'666'666);  // Unchanged
    EXPECT_EQ(tracker.GetCurrentTimeline().gpuQueues[1].budgetNs, 20'000'000);  // Changed
    EXPECT_EQ(tracker.GetCurrentTimeline().gpuQueues[2].budgetNs, 16'666'666);  // Unchanged
}

TEST_F(TimelineCapacityTrackerTest, SetCPUBudgetUpdatesAllThreads) {
    defaultConfig_.numCPUThreads = 4;
    TimelineCapacityTracker tracker(defaultConfig_);

    tracker.SetCPUBudget(16'000'000);  // 16ms

    EXPECT_EQ(tracker.GetConfig().cpuTimeBudgetNs, 16'000'000);
    for (const auto& cpu : tracker.GetCurrentTimeline().cpuThreads) {
        EXPECT_EQ(cpu.budgetNs, 16'000'000);
    }
}

TEST_F(TimelineCapacityTrackerTest, SetAdaptiveSchedulingUpdatesConfig) {
    TimelineCapacityTracker tracker(defaultConfig_);

    tracker.SetAdaptiveScheduling(false);
    EXPECT_FALSE(tracker.GetConfig().enableAdaptiveScheduling);

    tracker.SetAdaptiveScheduling(true);
    EXPECT_TRUE(tracker.GetConfig().enableAdaptiveScheduling);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(TimelineCapacityTrackerTest, ZeroBudgetHandledGracefully) {
    defaultConfig_.gpuTimeBudgetNs = 0;
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    tracker.RecordGPUTime(1'000'000);

    const auto& timeline = tracker.GetCurrentTimeline();
    EXPECT_EQ(timeline.gpuQueues[0].utilization, 0.0f);  // Division by zero avoided
    EXPECT_FALSE(timeline.gpuQueues[0].exceededBudget);
}

TEST_F(TimelineCapacityTrackerTest, ZeroEstimateInSuggestTasks) {
    TimelineCapacityTracker tracker(defaultConfig_);
    tracker.BeginFrame();

    tracker.RecordGPUTime(8'000'000);

    uint32_t additionalTasks = tracker.SuggestAdditionalTasks(0);
    EXPECT_EQ(additionalTasks, 0);  // Division by zero avoided
}

TEST_F(TimelineCapacityTrackerTest, EmptyHistoryReturnsZeroAverage) {
    TimelineCapacityTracker tracker(defaultConfig_);

    float avgUtil = tracker.GetAverageGPUUtilization(10);
    EXPECT_EQ(avgUtil, 0.0f);
}

TEST_F(TimelineCapacityTrackerTest, HistoryDepthClampedToMax) {
    defaultConfig_.historyDepth = 500;  // Exceeds max 300
    TimelineCapacityTracker tracker(defaultConfig_);

    EXPECT_LE(tracker.GetConfig().historyDepth, 300);
}

// =============================================================================
// Integration Scenario: Typical Usage
// =============================================================================

TEST_F(TimelineCapacityTrackerTest, TypicalUsageScenario) {
    TimelineCapacityTracker tracker(defaultConfig_);

    // Frame 1: Light load
    tracker.BeginFrame();
    tracker.RecordGPUTime(8'000'000);   // 48% GPU
    tracker.RecordCPUTime(2'000'000);   // 25% CPU
    tracker.EndFrame();

    EXPECT_TRUE(tracker.CanScheduleMoreWork());
    EXPECT_GT(tracker.SuggestAdditionalTasks(2'000'000), 0);

    // Frame 2: Heavy load
    tracker.BeginFrame();
    tracker.RecordGPUTime(17'000'000);  // 102% GPU
    tracker.RecordCPUTime(7'000'000);   // 87.5% CPU
    tracker.EndFrame();

    EXPECT_FALSE(tracker.CanScheduleMoreWork());
    EXPECT_TRUE(tracker.IsOverBudget());
    EXPECT_EQ(tracker.SuggestAdditionalTasks(2'000'000), 0);

    // Frame 3: Moderate load
    tracker.BeginFrame();
    tracker.RecordGPUTime(15'000'000);  // 90% GPU
    tracker.RecordCPUTime(4'000'000);   // 50% CPU
    tracker.EndFrame();

    // Average over 3 frames
    float avgGPU = tracker.GetAverageGPUUtilization(3);
    EXPECT_GT(avgGPU, 0.70f);  // (48% + 102% + 90%) / 3 = 80%
}
