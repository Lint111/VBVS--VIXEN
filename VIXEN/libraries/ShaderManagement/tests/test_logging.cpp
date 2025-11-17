#include <gtest/gtest.h>
#include "ShaderManagement/ShaderLogger.h"
#include <vector>
#include <string>

using namespace ShaderManagement;

/**
 * @brief Test logging and telemetry system
 */
class LoggingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset telemetry before each test
        ShaderLogger::GetTelemetry().Reset();

        // Reset to default logger
        ShaderLogger::GetInstance().SetCallback(nullptr);
        ShaderLogger::GetInstance().SetMinimumLevel(LogLevel::Debug);

        capturedMessages.clear();
    }

    void TearDown() override {
        // Clean up
        ShaderLogger::GetInstance().SetCallback(nullptr);
    }

    // Helper to capture log messages
    std::vector<LogMessage> capturedMessages;

    void CaptureCallback(const LogMessage& msg) {
        capturedMessages.push_back(msg);
    }
};

TEST_F(LoggingTest, BasicLogging) {
    // Set custom callback
    ShaderLogger::GetInstance().SetCallback([this](const LogMessage& msg) {
        CaptureCallback(msg);
    });

    // Log messages at different levels
    ShaderLogger::LogDebug("Debug message", "Test");
    ShaderLogger::LogInfo("Info message", "Test");
    ShaderLogger::LogWarning("Warning message", "Test");
    ShaderLogger::LogError("Error message", "Test");

    // All messages should be captured
    EXPECT_EQ(capturedMessages.size(), 4);
    EXPECT_EQ(capturedMessages[0].level, LogLevel::Debug);
    EXPECT_EQ(capturedMessages[1].level, LogLevel::Info);
    EXPECT_EQ(capturedMessages[2].level, LogLevel::Warning);
    EXPECT_EQ(capturedMessages[3].level, LogLevel::Error);

    // Check categories
    for (const auto& msg : capturedMessages) {
        EXPECT_EQ(msg.category, "Test");
    }
}

TEST_F(LoggingTest, LogLevelFiltering) {
    ShaderLogger::GetInstance().SetCallback([this](const LogMessage& msg) {
        CaptureCallback(msg);
    });

    // Set minimum level to Warning
    ShaderLogger::GetInstance().SetMinimumLevel(LogLevel::Warning);

    // Log messages at all levels
    ShaderLogger::LogDebug("Debug message");
    ShaderLogger::LogInfo("Info message");
    ShaderLogger::LogWarning("Warning message");
    ShaderLogger::LogError("Error message");

    // Only Warning and Error should be captured
    EXPECT_EQ(capturedMessages.size(), 2);
    EXPECT_EQ(capturedMessages[0].level, LogLevel::Warning);
    EXPECT_EQ(capturedMessages[1].level, LogLevel::Error);
}

TEST_F(LoggingTest, TelemetryCounters) {
    auto& telemetry = ShaderLogger::GetTelemetry();

    // Initially zero
    EXPECT_EQ(telemetry.totalCompilations.load(), 0);
    EXPECT_EQ(telemetry.successfulCompilations.load(), 0);
    EXPECT_EQ(telemetry.failedCompilations.load(), 0);

    // Simulate some compilations
    telemetry.totalCompilations.fetch_add(10);
    telemetry.successfulCompilations.fetch_add(8);
    telemetry.failedCompilations.fetch_add(2);

    EXPECT_EQ(telemetry.totalCompilations.load(), 10);
    EXPECT_EQ(telemetry.successfulCompilations.load(), 8);
    EXPECT_EQ(telemetry.failedCompilations.load(), 2);

    // Success rate should be 0.8 (80%)
    EXPECT_DOUBLE_EQ(telemetry.GetSuccessRate(), 0.8);
}

TEST_F(LoggingTest, TelemetryCacheMetrics) {
    auto& telemetry = ShaderLogger::GetTelemetry();

    telemetry.cacheHits.fetch_add(7);
    telemetry.cacheMisses.fetch_add(3);

    EXPECT_EQ(telemetry.cacheHits.load(), 7);
    EXPECT_EQ(telemetry.cacheMisses.load(), 3);

    // Cache hit rate should be 0.7 (70%)
    EXPECT_DOUBLE_EQ(telemetry.GetCacheHitRate(), 0.7);
}

TEST_F(LoggingTest, TelemetryTimer) {
    auto& telemetry = ShaderLogger::GetTelemetry();

    auto initialTime = telemetry.totalCompileTimeUs.load();

    // Simulate timed operation
    {
        ScopedTelemetryTimer timer(telemetry.totalCompileTimeUs);
        // Simulate work
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto finalTime = telemetry.totalCompileTimeUs.load();

    // Time should have increased by at least 10ms (10000 microseconds)
    EXPECT_GT(finalTime, initialTime + 9000);  // Allow for timing variance
}

TEST_F(LoggingTest, TelemetryReset) {
    auto& telemetry = ShaderLogger::GetTelemetry();

    // Set some values
    telemetry.totalCompilations.store(100);
    telemetry.successfulCompilations.store(95);
    telemetry.failedCompilations.store(5);
    telemetry.cacheHits.store(50);
    telemetry.cacheMisses.store(50);

    // Reset
    telemetry.Reset();

    // All should be zero
    EXPECT_EQ(telemetry.totalCompilations.load(), 0);
    EXPECT_EQ(telemetry.successfulCompilations.load(), 0);
    EXPECT_EQ(telemetry.failedCompilations.load(), 0);
    EXPECT_EQ(telemetry.cacheHits.load(), 0);
    EXPECT_EQ(telemetry.cacheMisses.load(), 0);
    EXPECT_EQ(telemetry.totalCompileTimeUs.load(), 0);
}

TEST_F(LoggingTest, LogLevelToString) {
    EXPECT_STREQ(LogLevelToString(LogLevel::Debug), "DEBUG");
    EXPECT_STREQ(LogLevelToString(LogLevel::Info), "INFO");
    EXPECT_STREQ(LogLevelToString(LogLevel::Warning), "WARNING");
    EXPECT_STREQ(LogLevelToString(LogLevel::Error), "ERROR");
    EXPECT_STREQ(LogLevelToString(LogLevel::None), "NONE");
}

TEST_F(LoggingTest, ThreadSafety) {
    // Test that logging from multiple threads doesn't crash
    ShaderLogger::GetInstance().SetCallback([](const LogMessage&) {
        // Do nothing, just ensure thread safety
    });

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([]() {
            for (int j = 0; j < 100; ++j) {
                ShaderLogger::LogInfo("Thread message " + std::to_string(j));
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // If we get here without crashing, thread safety is working
    SUCCEED();
}
