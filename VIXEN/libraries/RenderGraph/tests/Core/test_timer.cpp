/**
 * @file test_timer.cpp
 * @brief Comprehensive tests for Timer class
 *
 * Coverage: Timer.h (Target: 90%+)
 *
 * Tests:
 * - Construction and initialization
 * - Delta time measurement
 * - Elapsed time measurement
 * - Reset functionality
 * - Precision validation (microsecond+)
 * - Independence of GetElapsedTime() from delta measurement
 */

#include <gtest/gtest.h>
#include "Core/Timer.h"
#include <thread>
#include <chrono>

using namespace Vixen::RenderGraph;

class TimerTest : public ::testing::Test {
protected:
    void SetUp() override {
        timer = std::make_unique<Timer>();
    }

    void TearDown() override {
        timer.reset();
    }

    std::unique_ptr<Timer> timer;

    // Helper: Sleep for specified milliseconds
    void SleepMs(int milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    // Helper: Check if value is within tolerance (40% + 20ms absolute for Windows scheduler variance)
    bool IsWithinTolerance(double actual, double expected, double relativeTolerance = 0.40, double absoluteTolerance = 0.020) {
        double absDiff = std::abs(actual - expected);
        double relDiff = absDiff / expected;
        return absDiff <= absoluteTolerance || relDiff <= relativeTolerance;
    }
};

// ============================================================================
// Construction & Initialization
// ============================================================================

TEST_F(TimerTest, ConstructorInitializesTimer) {
    // Timer should be constructed without errors
    EXPECT_NO_THROW({
        Timer t;
    });
}

TEST_F(TimerTest, FirstDeltaTimeIsSmall) {
    // First GetDeltaTime() call should be very small (< 1ms typically)
    double dt = timer->GetDeltaTime();
    EXPECT_GE(dt, 0.0);  // Non-negative
    EXPECT_LT(dt, 0.1);  // Less than 100ms (very generous upper bound)
}

TEST_F(TimerTest, FirstElapsedTimeIsSmall) {
    // GetElapsedTime() immediately after construction should be very small
    double elapsed = timer->GetElapsedTime();
    EXPECT_GE(elapsed, 0.0);  // Non-negative
    EXPECT_LT(elapsed, 0.1);  // Less than 100ms
}

// ============================================================================
// Delta Time Measurement
// ============================================================================

TEST_F(TimerTest, GetDeltaTimeMeasuresTimeBetweenCalls) {
    // Reset to start fresh
    timer->Reset();

    // First delta should be very small
    double dt1 = timer->GetDeltaTime();
    EXPECT_GE(dt1, 0.0);
    EXPECT_LT(dt1, 0.01);  // < 10ms

    // Sleep 50ms
    SleepMs(50);

    // Second delta should be ~50ms
    double dt2 = timer->GetDeltaTime();
    EXPECT_TRUE(IsWithinTolerance(dt2, 0.050))
        << "Expected ~50ms, got " << (dt2 * 1000.0) << "ms";

    // Sleep 100ms
    SleepMs(100);

    // Third delta should be ~100ms
    double dt3 = timer->GetDeltaTime();
    EXPECT_TRUE(IsWithinTolerance(dt3, 0.100))
        << "Expected ~100ms, got " << (dt3 * 1000.0) << "ms";
}

TEST_F(TimerTest, DeltaTimeIsAlwaysPositive) {
    for (int i = 0; i < 10; ++i) {
        SleepMs(1);  // Small sleep to ensure time passes
        double dt = timer->GetDeltaTime();
        EXPECT_GT(dt, 0.0) << "Iteration " << i << ": Delta time should be positive";
    }
}

TEST_F(TimerTest, ConsecutiveDeltaTimesAreIndependent) {
    // Each GetDeltaTime() call measures from the last call, not from construction
    timer->Reset();
    timer->GetDeltaTime();  // Reset delta measurement

    SleepMs(30);
    double dt1 = timer->GetDeltaTime();

    SleepMs(30);
    double dt2 = timer->GetDeltaTime();

    // Both should be ~30ms, not 30ms and 60ms
    EXPECT_TRUE(IsWithinTolerance(dt1, 0.030))
        << "First delta: expected ~30ms, got " << (dt1 * 1000.0) << "ms";
    EXPECT_TRUE(IsWithinTolerance(dt2, 0.030))
        << "Second delta: expected ~30ms, got " << (dt2 * 1000.0) << "ms";
}

TEST_F(TimerTest, DeltaTimePrecision) {
    // Timer should have at least microsecond precision
    timer->Reset();
    timer->GetDeltaTime();

    // Sleep for 1ms
    SleepMs(1);

    double dt = timer->GetDeltaTime();

    // Should measure at least 0.5ms (accounting for sleep imprecision)
    EXPECT_GE(dt, 0.0005);

    // Should be less than 20ms (Windows scheduler variance)
    EXPECT_LT(dt, 0.020);
}

// ============================================================================
// Elapsed Time Measurement
// ============================================================================

TEST_F(TimerTest, GetElapsedTimeIsMonotonicallyIncreasing) {
    timer->Reset();

    double elapsed1 = timer->GetElapsedTime();
    SleepMs(10);
    double elapsed2 = timer->GetElapsedTime();
    SleepMs(10);
    double elapsed3 = timer->GetElapsedTime();

    EXPECT_GT(elapsed2, elapsed1) << "Elapsed time should increase";
    EXPECT_GT(elapsed3, elapsed2) << "Elapsed time should keep increasing";
}

TEST_F(TimerTest, GetElapsedTimeMeasuresFromConstruction) {
    timer->Reset();

    SleepMs(50);
    double elapsed = timer->GetElapsedTime();

    // Should be ~50ms
    EXPECT_TRUE(IsWithinTolerance(elapsed, 0.050))
        << "Expected ~50ms, got " << (elapsed * 1000.0) << "ms";
}

TEST_F(TimerTest, GetElapsedTimeAccumulatesAcrossMultipleCalls) {
    timer->Reset();

    SleepMs(30);
    double elapsed1 = timer->GetElapsedTime();

    SleepMs(30);
    double elapsed2 = timer->GetElapsedTime();

    // elapsed1 should be ~30ms
    EXPECT_TRUE(IsWithinTolerance(elapsed1, 0.030))
        << "First elapsed: expected ~30ms, got " << (elapsed1 * 1000.0) << "ms";

    // elapsed2 should be ~60ms (total time)
    EXPECT_TRUE(IsWithinTolerance(elapsed2, 0.060))
        << "Second elapsed: expected ~60ms, got " << (elapsed2 * 1000.0) << "ms";
}

TEST_F(TimerTest, GetElapsedTimeDoesNotAffectDeltaMeasurement) {
    timer->Reset();
    timer->GetDeltaTime();  // Reset delta measurement

    SleepMs(20);

    // Call GetElapsedTime() multiple times
    double elapsed1 = timer->GetElapsedTime();
    double elapsed2 = timer->GetElapsedTime();
    double elapsed3 = timer->GetElapsedTime();

    // GetElapsedTime() shouldn't reset lastFrameTime
    double dt = timer->GetDeltaTime();

    // Delta should still be ~20ms (time since last GetDeltaTime())
    EXPECT_TRUE(IsWithinTolerance(dt, 0.020))
        << "Delta should not be affected by GetElapsedTime() calls. "
        << "Expected ~20ms, got " << (dt * 1000.0) << "ms";
}

// ============================================================================
// Reset Functionality
// ============================================================================

TEST_F(TimerTest, ResetResetsStartTime) {
    timer->Reset();
    SleepMs(50);

    double elapsed1 = timer->GetElapsedTime();
    EXPECT_TRUE(IsWithinTolerance(elapsed1, 0.050));

    // Reset timer
    timer->Reset();

    // Elapsed time should now be very small
    double elapsed2 = timer->GetElapsedTime();
    EXPECT_LT(elapsed2, 0.010) << "Elapsed time should be reset to near-zero";
}

TEST_F(TimerTest, ResetResetsDeltaTime) {
    timer->Reset();
    timer->GetDeltaTime();  // Clear initial delta

    SleepMs(50);
    timer->GetDeltaTime();  // ~50ms

    // Reset timer
    timer->Reset();

    // Next GetDeltaTime() should be very small (not ~50ms)
    double dt = timer->GetDeltaTime();
    EXPECT_LT(dt, 0.010) << "Delta time should be reset to near-zero";
}

TEST_F(TimerTest, ResetDoesNotThrow) {
    EXPECT_NO_THROW({
        timer->Reset();
        timer->Reset();
        timer->Reset();
    });
}

TEST_F(TimerTest, MultipleResetsWork) {
    for (int i = 0; i < 5; ++i) {
        timer->Reset();
        SleepMs(20);

        double elapsed = timer->GetElapsedTime();
        EXPECT_TRUE(IsWithinTolerance(elapsed, 0.020))
            << "Reset " << i << ": expected ~20ms, got " << (elapsed * 1000.0) << "ms";
    }
}

// ============================================================================
// Edge Cases & Stress Tests
// ============================================================================

TEST_F(TimerTest, RapidGetDeltaTimeCalls) {
    // Calling GetDeltaTime() very rapidly should not crash
    timer->Reset();
    timer->GetDeltaTime();

    for (int i = 0; i < 1000; ++i) {
        double dt = timer->GetDeltaTime();
        EXPECT_GE(dt, 0.0);
    }
}

TEST_F(TimerTest, RapidGetElapsedTimeCalls) {
    // Calling GetElapsedTime() very rapidly should not crash
    timer->Reset();

    double lastElapsed = 0.0;
    for (int i = 0; i < 1000; ++i) {
        double elapsed = timer->GetElapsedTime();
        EXPECT_GE(elapsed, lastElapsed) << "Elapsed time should never decrease";
        lastElapsed = elapsed;
    }
}

TEST_F(TimerTest, LongRunningTimer) {
    // Timer should work correctly after running for a longer period
    timer->Reset();

    SleepMs(200);

    double elapsed = timer->GetElapsedTime();
    EXPECT_TRUE(IsWithinTolerance(elapsed, 0.200))
        << "Expected ~200ms, got " << (elapsed * 1000.0) << "ms";
}

TEST_F(TimerTest, MultipleTimersAreIndependent) {
    // Multiple Timer instances should not interfere with each other
    Timer timer1;
    Timer timer2;

    timer1.Reset();
    SleepMs(20);
    timer2.Reset();
    SleepMs(20);

    double elapsed1 = timer1.GetElapsedTime();
    double elapsed2 = timer2.GetElapsedTime();

    // timer1 should be ~40ms, timer2 should be ~20ms
    EXPECT_TRUE(IsWithinTolerance(elapsed1, 0.040))
        << "Timer1: expected ~40ms, got " << (elapsed1 * 1000.0) << "ms";
    EXPECT_TRUE(IsWithinTolerance(elapsed2, 0.020))
        << "Timer2: expected ~20ms, got " << (elapsed2 * 1000.0) << "ms";
}

// ============================================================================
// Usage Pattern Tests (Real-world scenarios)
// ============================================================================

TEST_F(TimerTest, GameLoopSimulation) {
    // Simulate a game loop with variable frame times
    timer->Reset();
    timer->GetDeltaTime();  // Clear initial delta

    // Frame 1: 16ms (60 FPS)
    SleepMs(16);
    double dt1 = timer->GetDeltaTime();
    EXPECT_TRUE(IsWithinTolerance(dt1, 0.016));

    // Frame 2: 33ms (30 FPS)
    SleepMs(33);
    double dt2 = timer->GetDeltaTime();
    EXPECT_TRUE(IsWithinTolerance(dt2, 0.033));

    // Frame 3: 16ms (60 FPS again)
    SleepMs(16);
    double dt3 = timer->GetDeltaTime();
    EXPECT_TRUE(IsWithinTolerance(dt3, 0.016));

    // Total elapsed should be ~65ms
    double totalElapsed = timer->GetElapsedTime();
    EXPECT_TRUE(IsWithinTolerance(totalElapsed, 0.065));
}

TEST_F(TimerTest, ProfilingUsagePattern) {
    // Simulate profiling a section of code
    timer->Reset();

    // Do some "work"
    SleepMs(25);

    // Measure elapsed time
    double profileTime = timer->GetElapsedTime();

    EXPECT_TRUE(IsWithinTolerance(profileTime, 0.025))
        << "Profiling: expected ~25ms, got " << (profileTime * 1000.0) << "ms";
}

// ============================================================================
// Performance Characteristics
// ============================================================================

TEST_F(TimerTest, GetDeltaTimeIsLowOverhead) {
    // GetDeltaTime() should execute very quickly (< 1us typically)
    timer->Reset();

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 10000; ++i) {
        timer->GetDeltaTime();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;

    // 10,000 calls should take less than 10ms total (1us per call)
    EXPECT_LT(duration.count(), 0.010)
        << "10,000 GetDeltaTime() calls took " << (duration.count() * 1000.0) << "ms";
}

TEST_F(TimerTest, GetElapsedTimeIsLowOverhead) {
    // GetElapsedTime() should execute very quickly
    timer->Reset();

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 10000; ++i) {
        timer->GetElapsedTime();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;

    // 10,000 calls should take less than 10ms total
    EXPECT_LT(duration.count(), 0.010)
        << "10,000 GetElapsedTime() calls took " << (duration.count() * 1000.0) << "ms";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
