/**
 * @file test_loop_manager.cpp
 * @brief Comprehensive tests for LoopManager class
 *
 * Coverage: LoopManager.h (Target: 85%+)
 *
 * Tests:
 * - Loop registration
 * - Variable timestep loops (fixedTimestep = 0.0)
 * - Fixed timestep loops (60Hz, 120Hz)
 * - LoopCatchupMode::FireAndForget
 * - LoopCatchupMode::SingleCorrectiveStep
 * - LoopCatchupMode::MultipleSteps
 * - Spiral of death protection (maxCatchupTime)
 * - Frame index tracking
 * - Step count tracking
 * - Multiple independent loops
 * - Edge cases and stress tests
 */

#include <gtest/gtest.h>
#include "Core/LoopManager.h"
#include <thread>
#include <chrono>

using namespace Vixen::RenderGraph;

class LoopManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager = std::make_unique<LoopManager>();
    }

    void TearDown() override {
        manager.reset();
    }

    std::unique_ptr<LoopManager> manager;

    // Helper: Convert Hz to seconds
    double HzToSeconds(double hz) {
        return 1.0 / hz;
    }

    // Helper: Check if value is within tolerance
    bool IsWithinTolerance(double actual, double expected, double tolerance = 0.01) {
        return std::abs(actual - expected) <= tolerance;
    }
};

// ============================================================================
// Construction & Initialization
// ============================================================================

TEST_F(LoopManagerTest, ConstructorInitializesManager) {
    EXPECT_NO_THROW({
        LoopManager m;
    });
}

// ============================================================================
// Loop Registration
// ============================================================================

TEST_F(LoopManagerTest, RegisterLoopReturnsUniqueID) {
    LoopConfig config1 = {HzToSeconds(60.0), "Physics"};
    LoopConfig config2 = {HzToSeconds(120.0), "FastLogic"};

    uint32_t id1 = manager->RegisterLoop(config1);
    uint32_t id2 = manager->RegisterLoop(config2);

    EXPECT_NE(id1, id2) << "Loop IDs should be unique";
}

TEST_F(LoopManagerTest, RegisterLoopIDsAreSequential) {
    LoopConfig config = {HzToSeconds(60.0), "Test"};

    uint32_t id1 = manager->RegisterLoop(config);
    uint32_t id2 = manager->RegisterLoop(config);
    uint32_t id3 = manager->RegisterLoop(config);

    EXPECT_EQ(id1 + 1, id2);
    EXPECT_EQ(id2 + 1, id3);
}

TEST_F(LoopManagerTest, RegisterMultipleLoops) {
    for (int i = 0; i < 10; ++i) {
        LoopConfig config = {HzToSeconds(60.0), "Loop" + std::to_string(i)};
        uint32_t id = manager->RegisterLoop(config);
        EXPECT_EQ(id, static_cast<uint32_t>(i));
    }
}

// ============================================================================
// Loop Reference Access
// ============================================================================

TEST_F(LoopManagerTest, GetLoopReferenceReturnsValidPointer) {
    LoopConfig config = {HzToSeconds(60.0), "Physics"};
    uint32_t id = manager->RegisterLoop(config);

    const LoopReference* ref = manager->GetLoopReference(id);

    ASSERT_NE(ref, nullptr) << "Loop reference should not be null";
    EXPECT_EQ(ref->loopID, id);
}

TEST_F(LoopManagerTest, GetLoopReferenceReturnsNullForInvalidID) {
    const LoopReference* ref = manager->GetLoopReference(999);
    EXPECT_EQ(ref, nullptr) << "Invalid ID should return null";
}

TEST_F(LoopManagerTest, LoopReferenceHasStableAddress) {
    LoopConfig config = {HzToSeconds(60.0), "Physics"};
    uint32_t id = manager->RegisterLoop(config);

    const LoopReference* ref1 = manager->GetLoopReference(id);
    const LoopReference* ref2 = manager->GetLoopReference(id);

    EXPECT_EQ(ref1, ref2) << "Loop reference pointer should be stable";
}

TEST_F(LoopManagerTest, LoopReferenceInitialState) {
    LoopConfig config = {HzToSeconds(60.0), "Physics", LoopCatchupMode::MultipleSteps};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(ref->loopID, id);
    EXPECT_FALSE(ref->shouldExecuteThisFrame) << "Should not execute before first update";
    EXPECT_EQ(ref->deltaTime, 0.0);
    EXPECT_EQ(ref->stepCount, 0);
    EXPECT_EQ(ref->lastExecutedFrame, 0);
    EXPECT_EQ(ref->lastExecutionTimeMs, 0.0);
    EXPECT_EQ(ref->catchupMode, LoopCatchupMode::MultipleSteps);
}

// ============================================================================
// Variable Timestep Loops (fixedTimestep = 0.0)
// ============================================================================

TEST_F(LoopManagerTest, VariableTimestepLoopAlwaysExecutes) {
    LoopConfig config = {0.0, "VariableLoop"};  // fixedTimestep = 0.0
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Update with various frame times
    manager->UpdateLoops(0.016);  // 60 FPS
    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_NEAR(ref->deltaTime, 0.016, 0.001);

    manager->UpdateLoops(0.033);  // 30 FPS
    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_NEAR(ref->deltaTime, 0.033, 0.001);

    manager->UpdateLoops(0.008);  // 120 FPS
    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_NEAR(ref->deltaTime, 0.008, 0.001);
}

TEST_F(LoopManagerTest, VariableTimestepLoopUsesClampedFrameTime) {
    LoopConfig config = {0.0, "VariableLoop"};
    config.maxCatchupTime = 0.1;  // 100ms max
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Update with large frame time (200ms)
    manager->UpdateLoops(0.200);

    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_NEAR(ref->deltaTime, 0.1, 0.001) << "Should clamp to maxCatchupTime";
}

// ============================================================================
// Fixed Timestep Loops - Basic Behavior
// ============================================================================

TEST_F(LoopManagerTest, FixedTimestepLoopDoesNotExecuteImmediately) {
    LoopConfig config = {HzToSeconds(60.0), "Physics"};  // 16.6ms timestep
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Small frame time (5ms) - should not execute
    manager->UpdateLoops(0.005);

    EXPECT_FALSE(ref->shouldExecuteThisFrame);
    EXPECT_EQ(ref->deltaTime, 0.0);
    EXPECT_EQ(ref->stepCount, 0);
}

TEST_F(LoopManagerTest, FixedTimestepLoopExecutesWhenAccumulatorFull) {
    LoopConfig config = {HzToSeconds(60.0), "Physics"};  // 16.6ms timestep
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Accumulate enough time (20ms > 16.6ms)
    manager->UpdateLoops(0.020);

    EXPECT_TRUE(ref->shouldExecuteThisFrame);
}

TEST_F(LoopManagerTest, FixedTimestep60HzSimulation) {
    LoopConfig config = {HzToSeconds(60.0), "Physics"};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Frame 1: 16.6ms (exactly 60 FPS)
    manager->UpdateLoops(HzToSeconds(60.0));
    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_NEAR(ref->deltaTime, HzToSeconds(60.0), 0.001);
    EXPECT_EQ(ref->stepCount, 1);

    // Frame 2: 16.6ms
    manager->UpdateLoops(HzToSeconds(60.0));
    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_EQ(ref->stepCount, 2);

    // Frame 3: 8ms (too short)
    manager->UpdateLoops(0.008);
    EXPECT_FALSE(ref->shouldExecuteThisFrame);
    EXPECT_EQ(ref->stepCount, 2) << "Step count should not increase";
}

TEST_F(LoopManagerTest, FixedTimestep120HzSimulation) {
    LoopConfig config = {HzToSeconds(120.0), "FastLogic"};  // 8.33ms
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Frame 1: 16.6ms (should execute - more than 8.33ms)
    manager->UpdateLoops(0.0166);
    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_NEAR(ref->deltaTime, HzToSeconds(120.0), 0.001);
}

// ============================================================================
// LoopCatchupMode::FireAndForget
// ============================================================================

TEST_F(LoopManagerTest, FireAndForgetUsesAccumulatedTime) {
    LoopConfig config = {HzToSeconds(60.0), "Physics", LoopCatchupMode::FireAndForget};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Accumulate 50ms (more than 16.6ms fixed timestep)
    manager->UpdateLoops(0.050);

    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_NEAR(ref->deltaTime, 0.050, 0.001) << "Should use full accumulated time";
    EXPECT_EQ(ref->stepCount, 1);
}

TEST_F(LoopManagerTest, FireAndForgetResetsAccumulator) {
    LoopConfig config = {HzToSeconds(60.0), "Physics", LoopCatchupMode::FireAndForget};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Frame 1: 50ms
    manager->UpdateLoops(0.050);
    EXPECT_TRUE(ref->shouldExecuteThisFrame);

    // Frame 2: 10ms (not enough to execute - accumulator was reset)
    manager->UpdateLoops(0.010);
    EXPECT_FALSE(ref->shouldExecuteThisFrame);
}

TEST_F(LoopManagerTest, FireAndForgetWithLagSpike) {
    LoopConfig config = {HzToSeconds(60.0), "Physics", LoopCatchupMode::FireAndForget};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Lag spike: 100ms frame
    manager->UpdateLoops(0.100);

    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_NEAR(ref->deltaTime, 0.100, 0.001) << "Should use full 100ms";
    EXPECT_EQ(ref->stepCount, 1) << "Should execute only once";
}

// ============================================================================
// LoopCatchupMode::SingleCorrectiveStep
// ============================================================================

TEST_F(LoopManagerTest, SingleCorrectiveStepUsesFixedDelta) {
    LoopConfig config = {HzToSeconds(60.0), "Physics", LoopCatchupMode::SingleCorrectiveStep};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Accumulate 50ms (more than 16.6ms)
    manager->UpdateLoops(0.050);

    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_NEAR(ref->deltaTime, HzToSeconds(60.0), 0.001) << "Should use fixed timestep";
    EXPECT_EQ(ref->stepCount, 1);
}

TEST_F(LoopManagerTest, SingleCorrectiveStepTracksDebt) {
    LoopConfig config = {HzToSeconds(60.0), "Physics", LoopCatchupMode::SingleCorrectiveStep};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Frame 1: 50ms (accumulator = 50ms)
    manager->UpdateLoops(0.050);
    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_EQ(ref->stepCount, 1);

    // Frame 2: 5ms (accumulator = 50 - 16.6 + 5 = 38.4ms)
    manager->UpdateLoops(0.005);
    EXPECT_TRUE(ref->shouldExecuteThisFrame) << "Still have debt";
    EXPECT_EQ(ref->stepCount, 2);

    // Frame 3: 5ms (accumulator = 38.4 - 16.6 + 5 = 26.8ms)
    manager->UpdateLoops(0.005);
    EXPECT_TRUE(ref->shouldExecuteThisFrame) << "Still have debt";
    EXPECT_EQ(ref->stepCount, 3);
}

TEST_F(LoopManagerTest, SingleCorrectiveStepExecutesOncePerUpdate) {
    LoopConfig config = {HzToSeconds(60.0), "Physics", LoopCatchupMode::SingleCorrectiveStep};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Large frame time (100ms = ~6 steps worth)
    manager->UpdateLoops(0.100);

    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_EQ(ref->stepCount, 1) << "Should execute only once per UpdateLoops() call";
}

// ============================================================================
// LoopCatchupMode::MultipleSteps (Default)
// ============================================================================

TEST_F(LoopManagerTest, MultipleStepsUsesFixedDelta) {
    LoopConfig config = {HzToSeconds(60.0), "Physics", LoopCatchupMode::MultipleSteps};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Frame time: 20ms (> 16.6ms)
    manager->UpdateLoops(0.020);

    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_NEAR(ref->deltaTime, HzToSeconds(60.0), 0.001);
    EXPECT_EQ(ref->stepCount, 1);
}

TEST_F(LoopManagerTest, MultipleStepsDecreasesAccumulator) {
    LoopConfig config = {HzToSeconds(60.0), "Physics", LoopCatchupMode::MultipleSteps};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Frame 1: 50ms (accumulator = 50ms)
    manager->UpdateLoops(0.050);
    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_EQ(ref->stepCount, 1);

    // Frame 2: 0ms (accumulator = 50 - 16.6 = 33.4ms, still > 16.6)
    manager->UpdateLoops(0.0);
    EXPECT_TRUE(ref->shouldExecuteThisFrame) << "Should execute again due to remaining debt";
    EXPECT_EQ(ref->stepCount, 2);

    // Frame 3: 0ms (accumulator = 33.4 - 16.6 = 16.8ms, still > 16.6)
    manager->UpdateLoops(0.0);
    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_EQ(ref->stepCount, 3);

    // Frame 4: 0ms (accumulator = 16.8 - 16.6 = 0.2ms, < 16.6)
    manager->UpdateLoops(0.0);
    EXPECT_FALSE(ref->shouldExecuteThisFrame) << "Accumulator depleted";
    EXPECT_EQ(ref->stepCount, 3);
}

TEST_F(LoopManagerTest, MultipleStepsDefaultBehavior) {
    LoopConfig config = {HzToSeconds(60.0), "Physics"};  // Default = MultipleSteps
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    EXPECT_EQ(ref->catchupMode, LoopCatchupMode::MultipleSteps);
}

// ============================================================================
// Spiral of Death Protection (maxCatchupTime)
// ============================================================================

TEST_F(LoopManagerTest, MaxCatchupTimeClamps) {
    LoopConfig config = {HzToSeconds(60.0), "Physics"};
    config.maxCatchupTime = 0.1;  // 100ms max
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Massive lag spike: 500ms
    manager->UpdateLoops(0.500);

    // Should still execute but with clamped time
    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    // Delta should be fixed timestep (16.6ms), not 500ms or even 100ms
    EXPECT_NEAR(ref->deltaTime, HzToSeconds(60.0), 0.001);
}

TEST_F(LoopManagerTest, DefaultMaxCatchupTime) {
    LoopConfig config = {HzToSeconds(60.0), "Physics"};
    // Default maxCatchupTime = 0.25 (250ms)

    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // 300ms frame (> 250ms default max)
    manager->UpdateLoops(0.300);

    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    // With clamping to 250ms and fixed timestep, should execute
}

TEST_F(LoopManagerTest, NegativeFrameTimeClampedToMinimum) {
    LoopConfig config = {HzToSeconds(60.0), "Physics"};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Negative frame time (should not happen, but test defensive programming)
    manager->UpdateLoops(-0.016);

    // Should clamp to minimum (1ms) and not execute (1ms < 16.6ms)
    EXPECT_FALSE(ref->shouldExecuteThisFrame);
}

TEST_F(LoopManagerTest, ZeroFrameTimeClampedToMinimum) {
    LoopConfig config = {HzToSeconds(60.0), "Physics"};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Zero frame time
    manager->UpdateLoops(0.0);

    // Should clamp to minimum (1ms) and not execute (1ms < 16.6ms)
    EXPECT_FALSE(ref->shouldExecuteThisFrame);
}

// ============================================================================
// Frame Index Tracking
// ============================================================================

TEST_F(LoopManagerTest, SetCurrentFrameUpdatesFrameIndex) {
    manager->SetCurrentFrame(42);

    LoopConfig config = {HzToSeconds(60.0), "Physics"};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    manager->UpdateLoops(0.020);

    EXPECT_EQ(ref->lastExecutedFrame, 42);
}

TEST_F(LoopManagerTest, LastExecutedFrameTracksExecution) {
    LoopConfig config = {HzToSeconds(60.0), "Physics"};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    manager->SetCurrentFrame(10);
    manager->UpdateLoops(0.020);
    EXPECT_EQ(ref->lastExecutedFrame, 10);

    manager->SetCurrentFrame(11);
    manager->UpdateLoops(0.005);  // Not enough to execute
    EXPECT_EQ(ref->lastExecutedFrame, 10) << "Should not update if not executed";

    manager->SetCurrentFrame(12);
    manager->UpdateLoops(0.020);
    EXPECT_EQ(ref->lastExecutedFrame, 12);
}

// ============================================================================
// Step Count Tracking
// ============================================================================

TEST_F(LoopManagerTest, StepCountIncrementsOnExecution) {
    LoopConfig config = {HzToSeconds(60.0), "Physics"};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    EXPECT_EQ(ref->stepCount, 0);

    manager->UpdateLoops(0.020);
    EXPECT_EQ(ref->stepCount, 1);

    manager->UpdateLoops(0.020);
    EXPECT_EQ(ref->stepCount, 2);

    manager->UpdateLoops(0.020);
    EXPECT_EQ(ref->stepCount, 3);
}

TEST_F(LoopManagerTest, StepCountDoesNotIncrementIfNotExecuted) {
    LoopConfig config = {HzToSeconds(60.0), "Physics"};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    manager->UpdateLoops(0.005);  // Not enough to execute
    EXPECT_EQ(ref->stepCount, 0);

    manager->UpdateLoops(0.005);
    EXPECT_EQ(ref->stepCount, 0);
}

// ============================================================================
// Multiple Independent Loops
// ============================================================================

TEST_F(LoopManagerTest, MultipleLoopsAreIndependent) {
    LoopConfig config60 = {HzToSeconds(60.0), "Physics"};
    LoopConfig config120 = {HzToSeconds(120.0), "FastLogic"};

    uint32_t id60 = manager->RegisterLoop(config60);
    uint32_t id120 = manager->RegisterLoop(config120);

    const LoopReference* ref60 = manager->GetLoopReference(id60);
    const LoopReference* ref120 = manager->GetLoopReference(id120);

    // Frame: 10ms
    // 60Hz: 10ms < 16.6ms (should NOT execute)
    // 120Hz: 10ms > 8.3ms (should execute)
    manager->UpdateLoops(0.010);

    EXPECT_FALSE(ref60->shouldExecuteThisFrame);
    EXPECT_TRUE(ref120->shouldExecuteThisFrame);
}

TEST_F(LoopManagerTest, ThreeLoopsDifferentRates) {
    LoopConfig configSlow = {HzToSeconds(30.0), "Slow"};   // 33.3ms
    LoopConfig configMed = {HzToSeconds(60.0), "Medium"};  // 16.6ms
    LoopConfig configFast = {HzToSeconds(120.0), "Fast"};  // 8.3ms

    uint32_t idSlow = manager->RegisterLoop(configSlow);
    uint32_t idMed = manager->RegisterLoop(configMed);
    uint32_t idFast = manager->RegisterLoop(configFast);

    const LoopReference* refSlow = manager->GetLoopReference(idSlow);
    const LoopReference* refMed = manager->GetLoopReference(idMed);
    const LoopReference* refFast = manager->GetLoopReference(idFast);

    // Frame: 16.6ms
    // Slow: 16.6ms < 33.3ms (NO)
    // Med: 16.6ms = 16.6ms (YES)
    // Fast: 16.6ms > 8.3ms (YES)
    manager->UpdateLoops(HzToSeconds(60.0));

    EXPECT_FALSE(refSlow->shouldExecuteThisFrame);
    EXPECT_TRUE(refMed->shouldExecuteThisFrame);
    EXPECT_TRUE(refFast->shouldExecuteThisFrame);
}

TEST_F(LoopManagerTest, MultipleLoopsDifferentCatchupModes) {
    LoopConfig config1 = {HzToSeconds(60.0), "Loop1", LoopCatchupMode::FireAndForget};
    LoopConfig config2 = {HzToSeconds(60.0), "Loop2", LoopCatchupMode::SingleCorrectiveStep};
    LoopConfig config3 = {HzToSeconds(60.0), "Loop3", LoopCatchupMode::MultipleSteps};

    uint32_t id1 = manager->RegisterLoop(config1);
    uint32_t id2 = manager->RegisterLoop(config2);
    uint32_t id3 = manager->RegisterLoop(config3);

    const LoopReference* ref1 = manager->GetLoopReference(id1);
    const LoopReference* ref2 = manager->GetLoopReference(id2);
    const LoopReference* ref3 = manager->GetLoopReference(id3);

    // All should execute with 50ms frame
    manager->UpdateLoops(0.050);

    EXPECT_TRUE(ref1->shouldExecuteThisFrame);
    EXPECT_TRUE(ref2->shouldExecuteThisFrame);
    EXPECT_TRUE(ref3->shouldExecuteThisFrame);

    // But deltas differ:
    // FireAndForget: Uses full accumulated time
    EXPECT_NEAR(ref1->deltaTime, 0.050, 0.001);

    // SingleCorrectiveStep & MultipleSteps: Use fixed timestep
    EXPECT_NEAR(ref2->deltaTime, HzToSeconds(60.0), 0.001);
    EXPECT_NEAR(ref3->deltaTime, HzToSeconds(60.0), 0.001);
}

// ============================================================================
// Edge Cases & Stress Tests
// ============================================================================

TEST_F(LoopManagerTest, RapidUpdateCalls) {
    LoopConfig config = {HzToSeconds(60.0), "Physics"};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // 1000 rapid updates with small time
    for (int i = 0; i < 1000; ++i) {
        manager->UpdateLoops(0.001);  // 1ms per frame
    }

    // Should have executed ~60 times (1000ms / 16.6ms ≈ 60)
    EXPECT_GE(ref->stepCount, 55);
    EXPECT_LE(ref->stepCount, 65);
}

TEST_F(LoopManagerTest, ManyLoopsSimultaneously) {
    std::vector<uint32_t> ids;
    for (int i = 0; i < 100; ++i) {
        LoopConfig config = {HzToSeconds(60.0), "Loop" + std::to_string(i)};
        ids.push_back(manager->RegisterLoop(config));
    }

    manager->UpdateLoops(0.020);

    // All loops should execute
    for (uint32_t id : ids) {
        const LoopReference* ref = manager->GetLoopReference(id);
        EXPECT_TRUE(ref->shouldExecuteThisFrame);
    }
}

TEST_F(LoopManagerTest, VeryHighFrequencyLoop) {
    LoopConfig config = {HzToSeconds(1000.0), "VeryFast"};  // 1ms timestep
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    manager->UpdateLoops(0.016);  // 16ms frame

    EXPECT_TRUE(ref->shouldExecuteThisFrame);
    EXPECT_NEAR(ref->deltaTime, HzToSeconds(1000.0), 0.0001);
}

TEST_F(LoopManagerTest, VeryLowFrequencyLoop) {
    LoopConfig config = {HzToSeconds(1.0), "VerySlow"};  // 1 second timestep
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // 500ms frame (should not execute)
    manager->UpdateLoops(0.500);
    EXPECT_FALSE(ref->shouldExecuteThisFrame);

    // Another 600ms (total 1100ms > 1s for tolerance)
    manager->UpdateLoops(0.600);
    EXPECT_TRUE(ref->shouldExecuteThisFrame);
}

// ============================================================================
// Real-World Usage Patterns
// ============================================================================

TEST_F(LoopManagerTest, TypicalGameLoopSimulation) {
    // Physics at 60Hz, Render at variable rate
    LoopConfig physicsConfig = {HzToSeconds(60.0), "Physics"};
    LoopConfig renderConfig = {0.0, "Render"};  // Variable

    uint32_t physicsID = manager->RegisterLoop(physicsConfig);
    uint32_t renderID = manager->RegisterLoop(renderConfig);

    const LoopReference* physics = manager->GetLoopReference(physicsID);
    const LoopReference* render = manager->GetLoopReference(renderID);

    // Frame 1: 60 FPS (16.6ms)
    manager->SetCurrentFrame(0);
    manager->UpdateLoops(HzToSeconds(60.0));
    EXPECT_TRUE(physics->shouldExecuteThisFrame);
    EXPECT_TRUE(render->shouldExecuteThisFrame);
    EXPECT_EQ(physics->stepCount, 1);

    // Frame 2: 30 FPS (33.3ms)
    manager->SetCurrentFrame(1);
    manager->UpdateLoops(HzToSeconds(30.0));
    EXPECT_TRUE(physics->shouldExecuteThisFrame);
    EXPECT_TRUE(render->shouldExecuteThisFrame);
    EXPECT_EQ(physics->stepCount, 2);

    // Frame 3: 120 FPS (8.3ms) - accumulated time may cause execution
    manager->SetCurrentFrame(2);
    manager->UpdateLoops(HzToSeconds(120.0));
    // Physics may or may not execute depending on accumulated error
    EXPECT_TRUE(render->shouldExecuteThisFrame) << "Render always executes";
    EXPECT_GE(physics->stepCount, 2) << "Step count should be at least 2";
    EXPECT_LE(physics->stepCount, 3) << "Step count should be at most 3";
}

TEST_F(LoopManagerTest, LagSpikeRecovery) {
    LoopConfig config = {HzToSeconds(60.0), "Physics", LoopCatchupMode::MultipleSteps};
    uint32_t id = manager->RegisterLoop(config);
    const LoopReference* ref = manager->GetLoopReference(id);

    // Normal frames
    manager->UpdateLoops(HzToSeconds(60.0));
    EXPECT_EQ(ref->stepCount, 1);

    manager->UpdateLoops(HzToSeconds(60.0));
    EXPECT_EQ(ref->stepCount, 2);

    // Lag spike: 100ms
    manager->UpdateLoops(0.100);
    EXPECT_EQ(ref->stepCount, 3);

    // Recovery: Small frames deplete accumulator
    manager->UpdateLoops(0.005);
    EXPECT_EQ(ref->stepCount, 4);

    manager->UpdateLoops(0.005);
    EXPECT_EQ(ref->stepCount, 5);

    manager->UpdateLoops(0.005);
    EXPECT_EQ(ref->stepCount, 6);

    manager->UpdateLoops(0.005);
    EXPECT_EQ(ref->stepCount, 7);

    // Eventually accumulator depletes (may take one more step due to accumulated error)
    manager->UpdateLoops(0.005);
    EXPECT_GE(ref->stepCount, 7) << "Should stabilize at 7 or 8";
    EXPECT_LE(ref->stepCount, 8) << "Should stabilize at 7 or 8";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
