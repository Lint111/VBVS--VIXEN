#include <gtest/gtest.h>
#include "Logger.h"
#include <memory>

// Test fixture
class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger = std::make_unique<Logger>("TestLogger", true);
    }

    void TearDown() override {
        logger.reset();
    }

    std::unique_ptr<Logger> logger;
};

// ============================================================================
// Basic Logging Tests
// ============================================================================

TEST_F(LoggerTest, LoggerCreationEnabled) {
    EXPECT_TRUE(logger->IsEnabled());
    EXPECT_EQ(logger->GetName(), "TestLogger");
}

TEST_F(LoggerTest, LoggerCreationDisabled) {
    Logger disabledLogger("Disabled", false);
    EXPECT_FALSE(disabledLogger.IsEnabled());
}

TEST_F(LoggerTest, EnableDisableLogging) {
    logger->SetEnabled(false);
    EXPECT_FALSE(logger->IsEnabled());

    logger->SetEnabled(true);
    EXPECT_TRUE(logger->IsEnabled());
}

TEST_F(LoggerTest, TerminalOutputToggle) {
    EXPECT_FALSE(logger->HasTerminalOutput());

    logger->SetTerminalOutput(true);
    EXPECT_TRUE(logger->HasTerminalOutput());

    logger->SetTerminalOutput(false);
    EXPECT_FALSE(logger->HasTerminalOutput());
}

// ============================================================================
// Logging Methods Tests
// ============================================================================

TEST_F(LoggerTest, DebugLogging) {
    logger->Debug("Debug message");
    std::string logs = logger->ExtractLogs();
    EXPECT_NE(logs.find("Debug message"), std::string::npos);
    EXPECT_NE(logs.find("DEBUG"), std::string::npos);
}

TEST_F(LoggerTest, InfoLogging) {
    logger->Info("Info message");
    std::string logs = logger->ExtractLogs();
    EXPECT_NE(logs.find("Info message"), std::string::npos);
    EXPECT_NE(logs.find("INFO"), std::string::npos);
}

TEST_F(LoggerTest, WarningLogging) {
    logger->Warning("Warning message");
    std::string logs = logger->ExtractLogs();
    EXPECT_NE(logs.find("Warning message"), std::string::npos);
    EXPECT_NE(logs.find("WARNING"), std::string::npos);
}

TEST_F(LoggerTest, ErrorLogging) {
    logger->Error("Error message");
    std::string logs = logger->ExtractLogs();
    EXPECT_NE(logs.find("Error message"), std::string::npos);
    EXPECT_NE(logs.find("ERROR"), std::string::npos);
}

TEST_F(LoggerTest, CriticalLogging) {
    logger->Critical("Critical message");
    std::string logs = logger->ExtractLogs();
    EXPECT_NE(logs.find("Critical message"), std::string::npos);
    EXPECT_NE(logs.find("CRITICAL"), std::string::npos);
}

TEST_F(LoggerTest, MultipleLogEntries) {
    logger->Debug("First");
    logger->Info("Second");
    logger->Warning("Third");

    std::string logs = logger->ExtractLogs();
    EXPECT_NE(logs.find("First"), std::string::npos);
    EXPECT_NE(logs.find("Second"), std::string::npos);
    EXPECT_NE(logs.find("Third"), std::string::npos);
}

TEST_F(LoggerTest, DisabledLoggerDoesNotLog) {
    logger->SetEnabled(false);
    logger->Info("Should not log");

    std::string logs = logger->ExtractLogs();
    EXPECT_EQ(logs.find("Should not log"), std::string::npos);
}

// ============================================================================
// Clear Tests
// ============================================================================

TEST_F(LoggerTest, ClearRemovesLogs) {
    logger->Info("Message 1");
    logger->Info("Message 2");

    std::string logsBefore = logger->ExtractLogs();
    EXPECT_NE(logsBefore.find("Message 1"), std::string::npos);

    logger->Clear();

    std::string logsAfter = logger->ExtractLogs();
    EXPECT_EQ(logsAfter.find("Message 1"), std::string::npos);
    EXPECT_EQ(logsAfter.find("Message 2"), std::string::npos);
}

// ============================================================================
// Hierarchical Logging Tests
// ============================================================================

TEST_F(LoggerTest, AddChildLogger) {
    auto childLogger = std::make_shared<Logger>("ChildLogger", true);
    logger->AddChild(childLogger);

    EXPECT_EQ(logger->GetChildren().size(), 1);
    EXPECT_EQ(logger->GetChildren()[0], childLogger);
}

TEST_F(LoggerTest, RemoveChildLogger) {
    auto childLogger = std::make_shared<Logger>("ChildLogger", true);
    logger->AddChild(childLogger);
    EXPECT_EQ(logger->GetChildren().size(), 1);

    logger->RemoveChild(childLogger.get());
    EXPECT_EQ(logger->GetChildren().size(), 0);
}

TEST_F(LoggerTest, ExtractLogsIncludesChildren) {
    logger->Info("Parent message");

    auto childLogger = std::make_shared<Logger>("Child", true);
    childLogger->Info("Child message");
    logger->AddChild(childLogger);

    std::string logs = logger->ExtractLogs();
    EXPECT_NE(logs.find("Parent message"), std::string::npos);
    EXPECT_NE(logs.find("Child message"), std::string::npos);
    EXPECT_NE(logs.find("Child"), std::string::npos);
}

TEST_F(LoggerTest, ClearAllClearsChildrenToo) {
    logger->Info("Parent message");

    auto childLogger = std::make_shared<Logger>("Child", true);
    childLogger->Info("Child message");
    logger->AddChild(childLogger);

    logger->ClearAll();

    std::string parentLogs = logger->ExtractLogs();
    std::string childLogs = childLogger->ExtractLogs();

    EXPECT_EQ(parentLogs.find("Parent message"), std::string::npos);
    EXPECT_EQ(childLogs.find("Child message"), std::string::npos);
}

TEST_F(LoggerTest, ClearChildrenRemovesReferences) {
    auto child1 = std::make_shared<Logger>("Child1", true);
    auto child2 = std::make_shared<Logger>("Child2", true);

    logger->AddChild(child1);
    logger->AddChild(child2);
    EXPECT_EQ(logger->GetChildren().size(), 2);

    logger->ClearChildren();
    EXPECT_EQ(logger->GetChildren().size(), 0);

    // Children themselves should still have their logs
    child1->Info("Test");
    std::string childLogs = child1->ExtractLogs();
    EXPECT_NE(childLogs.find("Test"), std::string::npos);
}

// ============================================================================
// Log Level Tests
// ============================================================================

TEST_F(LoggerTest, LogLevelGeneric) {
    logger->Log(LogLevel::LOG_INFO, "Generic log");
    std::string logs = logger->ExtractLogs();
    EXPECT_NE(logs.find("Generic log"), std::string::npos);
    EXPECT_NE(logs.find("INFO"), std::string::npos);
}

TEST_F(LoggerTest, AllLogLevelsWork) {
    logger->Log(LogLevel::LOG_DEBUG, "Debug");
    logger->Log(LogLevel::LOG_INFO, "Info");
    logger->Log(LogLevel::LOG_WARNING, "Warning");
    logger->Log(LogLevel::LOG_ERROR, "Error");
    logger->Log(LogLevel::LOG_CRITICAL, "Critical");

    std::string logs = logger->ExtractLogs();
    EXPECT_NE(logs.find("DEBUG"), std::string::npos);
    EXPECT_NE(logs.find("INFO"), std::string::npos);
    EXPECT_NE(logs.find("WARNING"), std::string::npos);
    EXPECT_NE(logs.find("ERROR"), std::string::npos);
    EXPECT_NE(logs.find("CRITICAL"), std::string::npos);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(LoggerTest, EmptyMessageLogging) {
    logger->Info("");
    std::string logs = logger->ExtractLogs();
    EXPECT_NE(logs.find("INFO"), std::string::npos);
}

TEST_F(LoggerTest, MultilineMessageLogging) {
    logger->Info("Line 1\nLine 2\nLine 3");
    std::string logs = logger->ExtractLogs();
    EXPECT_NE(logs.find("Line 1"), std::string::npos);
    EXPECT_NE(logs.find("Line 2"), std::string::npos);
    EXPECT_NE(logs.find("Line 3"), std::string::npos);
}

TEST_F(LoggerTest, LongMessageLogging) {
    std::string longMessage(1000, 'X');
    logger->Info(longMessage);
    std::string logs = logger->ExtractLogs();
    EXPECT_NE(logs.find(longMessage), std::string::npos);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
