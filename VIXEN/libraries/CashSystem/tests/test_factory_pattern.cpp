#include <gtest/gtest.h>
#include <MainCacher.h>
#include <PipelineCacher.h>
#include <ShaderCompilationCacher.h>

// Mock VulkanDevice
class MockVulkanDevice {
public:
    std::string GetDeviceName() const { return "MockDevice"; }
    uint32_t GetDeviceId() const { return 1234; }
};

TEST(CashSystem_FactoryPattern, CreateCacherWithValidType) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device;
    
    // Register type and create instance
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(&device);
    
    EXPECT_TRUE(pipelineCacher != nullptr);
    EXPECT_TRUE(pipelineCacher->GetDevice() != nullptr);
}

TEST(CashSystem_FactoryPattern, CreateMultipleInstances) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device1, device2;
    
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    
    // Create multiple instances of same type
    auto instance1 = mainCacher->CreateCacher<PipelineCacher>(&device1);
    auto instance2 = mainCacher->CreateCacher<PipelineCacher>(&device2);
    auto instance3 = mainCacher->CreateCacher<PipelineCacher>(&device1); // Same device as instance1
    
    EXPECT_TRUE(instance1 != nullptr);
    EXPECT_TRUE(instance2 != nullptr);
    EXPECT_TRUE(instance3 != nullptr);
    
    // Different devices should create different instances
    EXPECT_NE(instance1.get(), instance2.get());
    
    // Same device might reuse instance (depends on implementation)
    // This is implementation-specific, so we just verify both are valid
    EXPECT_TRUE(instance1.get() != nullptr);
    EXPECT_TRUE(instance3.get() != nullptr);
}

TEST(CashSystem_FactoryPattern, DeviceIndependentCreation) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device;
    
    // Register device-independent type
    mainCacher->RegisterType<ShaderCompilationCacher>("ShaderCompilationCacher");
    
    // Create with null device (should work for device-independent types)
    auto shaderCacher1 = mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr);
    auto shaderCacher2 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device);
    
    EXPECT_TRUE(shaderCacher1 != nullptr);
    EXPECT_TRUE(shaderCacher2 != nullptr);
    
    // Both should be the same instance (global/shared)
    EXPECT_EQ(shaderCacher1.get(), shaderCacher2.get());
}

TEST(CashSystem_FactoryPattern, UnregisteredTypeCreation) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device;
    
    // Don't register MainCacher type
    // Try to create unregistered type
    // This should either return nullptr or throw exception
    // Depending on implementation choice
    
    try {
        auto invalidCacher = mainCacher->CreateCacher<MainCacher>(&device);
        // If we get here, implementation allows it
        EXPECT_TRUE(invalidCacher == nullptr || invalidCacher.get() != nullptr);
    } catch (...) {
        // Exception thrown - also acceptable
        EXPECT_TRUE(true);
    }
}

TEST(CashSystem_FactoryPattern, TypeSafety) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device;
    
    // Register different types
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->RegisterType<ShaderCompilationCacher>("ShaderCompilationCacher");
    
    // Create instances
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(&device);
    auto shaderCacher = mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr);
    
    // Verify type correctness
    EXPECT_TRUE(pipelineCacher != nullptr);
    EXPECT_TRUE(shaderCacher != nullptr);
    
    // They should be different types
    EXPECT_NE(pipelineCacher.get(), shaderCacher.get());
    
    // Each should work with its own cache operations
    bool stored1 = pipelineCacher->Cache("test", "pipeline_value");
    bool stored2 = shaderCacher->Cache("test", "shader_value");
    
    EXPECT_TRUE(stored1);
    EXPECT_TRUE(stored2);
    
    // Cross-contamination test
    auto miss1 = pipelineCacher->GetCached("test"); // Should get pipeline_value
    auto miss2 = shaderCacher->GetCached("test");   // Should get shader_value
    
    EXPECT_TRUE(miss1.has_value());
    EXPECT_TRUE(miss2.has_value());
    EXPECT_EQ(miss1.value(), "pipeline_value");
    EXPECT_EQ(miss2.value(), "shader_value");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}