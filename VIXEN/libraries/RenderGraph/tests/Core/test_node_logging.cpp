#include <gtest/gtest.h>
#include "Core/NodeLogging.h"
#include <memory>

// V-M26 gap: NODE_LOG_ERROR/CRITICAL previously short-circuited on nodeLogger->IsEnabled()
// before ever calling Logger::Error()/Critical(), so Logger::Log()'s terminal-visibility bypass
// for Error/Critical (audit V-M26) never ran for node loggers — which ship enabled=false.

class NodeLoggingTest : public ::testing::Test {
protected:
    void TearDown() override {
        Vixen::Log::Logger::SetGlobalMinLevel(Vixen::Log::LogLevel::LOG_DEBUG);
    }
};

TEST_F(NodeLoggingTest, ErrorReachesTerminalThroughDisabledNodeLogger) {
    std::shared_ptr<Logger> nodeLogger = std::make_shared<Logger>("Node"); // enabled=false
    testing::internal::CaptureStdout();
    NODE_LOG_ERROR("device creation failed");
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("device creation failed"), std::string::npos);
}

TEST_F(NodeLoggingTest, CriticalReachesTerminalThroughDisabledNodeLogger) {
    std::shared_ptr<Logger> nodeLogger = std::make_shared<Logger>("Node");
    testing::internal::CaptureStdout();
    NODE_LOG_CRITICAL("no Vulkan-capable GPUs found");
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("no Vulkan-capable GPUs found"), std::string::npos);
}

TEST_F(NodeLoggingTest, WarningStaysGatedThroughDisabledNodeLogger) {
    std::shared_ptr<Logger> nodeLogger = std::make_shared<Logger>("Node");
    testing::internal::CaptureStdout();
    NODE_LOG_WARNING("should stay silent");
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output.find("should stay silent"), std::string::npos);
}

TEST_F(NodeLoggingTest, ErrorObjVariantReachesTerminalThroughDisabledNodeLogger) {
    struct Owner {
        std::shared_ptr<Logger> nodeLogger = std::make_shared<Logger>("Node");
    } owner;
    testing::internal::CaptureStdout();
    NODE_LOG_ERROR_OBJ((&owner), "obj-variant error");
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("obj-variant error"), std::string::npos);
}

TEST_F(NodeLoggingTest, GlobalMinLevelAboveErrorStillSilencesNodeLogger) {
    std::shared_ptr<Logger> nodeLogger = std::make_shared<Logger>("Node");
    Vixen::Log::Logger::SetGlobalMinLevel(Vixen::Log::LogLevel::LOG_CRITICAL);
    testing::internal::CaptureStdout();
    NODE_LOG_ERROR("deliberately silenced");
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output.find("deliberately silenced"), std::string::npos);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
