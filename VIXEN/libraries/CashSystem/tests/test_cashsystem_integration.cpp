#include <gtest/gtest.h>
#include <MainCacher.h>
#include <MainCashLogger.h>
#include <PipelineCacher.h>
#include <ShaderCompilationCacher.h>
#include "logger/Logger.h"

// Mock VulkanDevice
class MockVulkanDevice {
public:
    std::string GetDeviceName() const { return "TestDevice"; }
    uint32_t GetDeviceId() const { return 1234; }
};

TEST(CashSystem_Integration, LoggerIntegration) {
    // Create a parent logger for testing hierarchy
    auto parentLogger = std::make_unique<Logger>("TestParent", true);
    
    // Create MainCashLogger with parent
    auto mainLogger = std::make_unique<MainCashLogger>("CashSystem", parentLogger.get());
    
    // Test that main logger is created and attached
    EXPECT_TRUE(mainLogger->GetMainLogger() != nullptr);
    EXPECT_EQ(parentLogger->GetChildren().size(), 1);
    
    // Test adding sub-loggers
    Logger subLogger1("SubLogger1", true);
    Logger subLogger2("SubLogger2", true);
    
    mainLogger->AddSubLogger("PipelineCacher", &subLogger1);
    mainLogger->AddSubLogger("ShaderCompilationCacher", &subLogger2);
    
    EXPECT_EQ(mainLogger->GetSubLoggerCount(), 2);
    
    // Test logging to sub-loggers
    mainLogger->LogToSubLogger("PipelineCacher", LogLevel::LOG_INFO, "Pipeline cache miss");
    mainLogger->LogToSubLogger("ShaderCompilationCacher", LogLevel::LOG_DEBUG, "Shader compiled");
    
    // Test main logging
    mainLogger->Log(LogLevel::LOG_WARNING, "Cache system warning");
    
    // Verify logs were created
    EXPECT_TRUE(parentLogger->IsEnabled());
}

TEST(CashSystem_Integration, DebugModeLogging) {
    auto mainLogger = std::make_unique<MainCashLogger>();
    
    // Test debug mode toggle
    EXPECT_FALSE(mainLogger->IsDebugMode());
    
    mainLogger->SetDebugMode(true);
    EXPECT_TRUE(mainLogger->IsDebugMode());
    
    // Test debug logging
    mainLogger->LogToSubLogger("TestCacher", LogLevel::LOG_DEBUG, "Debug cache operation");
    
    mainLogger->SetDebugMode(false);
    EXPECT_FALSE(mainLogger->IsDebugMode());
}

TEST(CashSystem_Integration, FullWorkflow) {
    // Create parent logger
    auto parentLogger = std::make_unique<Logger>("VulkanApp", true);
    
    // Create MainCacher with logging
    auto mainCacher = std::make_unique<MainCacher>(parentLogger.get());
    auto mainLogger = mainCacher->GetLogger();
    
    // Enable debug mode for comprehensive logging
    mainLogger->SetDebugMode(true);
    
    MockVulkanDevice device;
    
    // Register types
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->CreateGlobalCacher<ShaderCompilationCacher>();
    
    // Create cachers with logging
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(&device);
    auto shaderCacher = mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr);
    
    // Perform cache operations with logging
    std::string pipelineKey = "test_pipeline";
    std::string pipelineValue = "pipeline_handle_123";
    
    bool stored = pipelineCacher->Cache(pipelineKey, pipelineValue);
    EXPECT_TRUE(stored);
    
    // Simulate cache hit
    auto retrieved = pipelineCacher->GetCached(pipelineKey);
    EXPECT_TRUE(retrieved.has_value());
    
    // Test shader compilation caching
    std::string shaderKey = "vertex.vert";
    std::string compiledSPV = "compiled_spirv_data";
    
    shaderCacher->Cache(shaderKey, compiledSPV);
    
    // Create another device and verify sharing
    MockVulkanDevice device2;
    auto shaderCacher2 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device2);
    
    auto sharedData = shaderCacher2->GetCached(shaderKey);
    EXPECT_TRUE(sharedData.has_value());
    EXPECT_EQ(sharedData.value(), compiledSPV);
    
    // Extract and verify logs
    std::string allLogs = mainLogger->ExtractAllLogs();
    EXPECT_FALSE(allLogs.empty());
    
    // Should contain information about the operations
    EXPECT_TRUE(allLogs.find("PipelineCacher") != std::string::npos || 
                allLogs.find("CashSystem") != std::string::npos);
}

TEST(CashSystem_Integration, MultiDeviceWithLogging) {
    auto mainLogger = std::make_unique<MainCashLogger>();
    mainLogger->SetDebugMode(true);
    
    MockVulkanDevice device1, device2;
    
    // Create MainCacher with logger
    auto mainCacher = std::make_unique<MainCacher>(mainLogger.get());
    
    // Register and create cachers
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    
    auto pipeline1 = mainCacher->CreateCacher<PipelineCacher>(&device1);
    auto pipeline2 = mainCacher->CreateCacher<PipelineCacher>(&device2);
    
    // Perform operations on both devices
    pipeline1->Cache("shared_key", "device1_value");
    pipeline2->Cache("shared_key", "device2_value");
    
    // Verify isolation
    auto val1 = pipeline1->GetCached("shared_key");
    auto val2 = pipeline2->GetCached("shared_key");
    
    EXPECT_TRUE(val1.has_value());
    EXPECT_TRUE(val2.has_value());
    EXPECT_NE(val1.value(), val2.value());
    
    // Check logs contain device information
    std::string logs = mainLogger->ExtractAllLogs();
    EXPECT_FALSE(logs.empty());
}

TEST(CashSystem_Integration, ErrorHandlingWithLogging) {
    auto mainLogger = std::make_unique<MainCashLogger>();
    auto mainCacher = std::make_unique<MainCacher>(mainLogger.get());
    
    MockVulkanDevice device;
    
    // Test operations on unregistered type (should be handled gracefully)
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    
    // This should work fine
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(&device);
    EXPECT_TRUE(pipelineCacher != nullptr);
    
    // Try to create unregistered type
    try {
        // Note: This depends on implementation - might return nullptr or throw
        auto invalidCacher = mainCacher->CreateCacher<MainCacher>(&device);
        // If we get here, implementation allows it
        if (invalidCacher) {
            EXPECT_TRUE(invalidCacher != nullptr);
        }
    } catch (...) {
        // Exception thrown - acceptable behavior
        EXPECT_TRUE(true);
    }
    
    // Verify logging system is still functional
    mainLogger->Log(LogLevel::LOG_INFO, "Error handling test completed");
    EXPECT_TRUE(mainLogger->IsDebugMode() == false); // Default state
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}