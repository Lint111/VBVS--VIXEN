#include <gtest/gtest.h>
#include <CashSystem.h>
#include <TypeRegistry.h>
#include <MainCacher.h>
#include <PipelineCacher.h>
#include <ShaderCompilationCacher.h>
#include "logger/Logger.h"

// Mock VulkanDevice for testing
class MockVulkanDevice {
public:
    std::string GetDeviceName() const { return "MockDevice"; }
    uint32_t GetDeviceId() const { return 1234; }
};

TEST(CashSystem_Basic, InitializeMainCacher) {
    // Test basic MainCacher initialization
    auto mainCacher = std::make_unique<MainCacher>();
    EXPECT_TRUE(mainCacher != nullptr);
    EXPECT_EQ(mainCacher->GetRegisteredTypes().size(), 0);
}

TEST(CashSystem_Basic, RegisterAndRetrieveTypes) {
    auto mainCacher = std::make_unique<MainCacher>();
    
    // Register some test types
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->RegisterType<ShaderCompilationCacher>("ShaderCompilationCacher");
    
    EXPECT_EQ(mainCacher->GetRegisteredTypes().size(), 2);
    EXPECT_TRUE(mainCacher->IsTypeRegistered<PipelineCacher>());
    EXPECT_TRUE(mainCacher->IsTypeRegistered<ShaderCompilationCacher>());
    EXPECT_FALSE(mainCacher->IsTypeRegistered<MainCacher>());
}

TEST(CashSystem_Basic, CreateCachers) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice mockDevice;
    
    // Register and create PipelineCacher
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(&mockDevice);
    EXPECT_TRUE(pipelineCacher != nullptr);
    
    // Register and create ShaderCompilationCacher (device-independent)
    mainCacher->RegisterType<ShaderCompilationCacher>("ShaderCompilationCacher");
    auto shaderCacher = mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr);
    EXPECT_TRUE(shaderCacher != nullptr);
}

TEST(CashSystem_Basic, LoggerIntegration) {
    // Test that MainCacher properly integrates with logging
    auto logger = std::make_unique<Logger>("CashSystemTest", true);
    auto mainCacher = std::make_unique<MainCacher>(logger.get());
    
    // Test that logging works (this should not crash)
    mainCacher->SetDebugMode(true);
    mainCacher->LogInfo("Test log message");
    
    EXPECT_TRUE(logger->IsEnabled());
    EXPECT_TRUE(mainCacher->IsDebugModeEnabled());
}

TEST(CashSystem_Basic, CacheOperations) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice mockDevice;
    
    // Register and create PipelineCacher
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(&mockDevice);
    
    // Test basic cache operations
    std::string testKey = "test_pipeline";
    std::string testValue = "test_value";
    
    // Store a value
    bool stored = pipelineCacher->Cache(testKey, testValue);
    EXPECT_TRUE(stored);
    
    // Retrieve the value
    auto retrieved = pipelineCacher->GetCached(testKey);
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved.value(), testValue);
    
    // Test cache miss
    auto miss = pipelineCacher->GetCached("nonexistent");
    EXPECT_FALSE(miss.has_value());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}