#include <gtest/gtest.h>
#include <CashSystem/MainCacher.h>
#include <CashSystem/DeviceIdentifier.h>
#include <CashSystem/PipelineCacher.h>
#include <CashSystem/ShaderCompilationCacher.h>

// Mock VulkanDevice classes for testing
class MockVulkanDevice1 {
public:
    std::string GetDeviceName() const { return "RTX 3080"; }
    uint32_t GetDeviceId() const { return 1234; }
    uint32_t GetVendorId() const { return 0x10DE; }
};

class MockVulkanDevice2 {
public:
    std::string GetDeviceName() const { return "GTX 1060"; }
    uint32_t GetDeviceId() const { return 5678; }
    uint32_t GetVendorId() const { return 0x10DE; }
};

TEST(CashSystem_MultiDevice, DeviceRegistryCreation) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice1 device1;
    MockVulkanDevice2 device2;
    
    // Register types
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->RegisterType<ShaderCompilationCacher>("ShaderCompilationCacher");
    
    // Create cachers for different devices
    auto pipelineCacher1 = mainCacher->CreateCacher<PipelineCacher>(&device1);
    auto pipelineCacher2 = mainCacher->CreateCacher<PipelineCacher>(&device2);
    auto shaderCacherGlobal = mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr);
    
    EXPECT_TRUE(pipelineCacher1 != nullptr);
    EXPECT_TRUE(pipelineCacher2 != nullptr);
    EXPECT_TRUE(shaderCacherGlobal != nullptr);
    
    // Pipeline cachers should be device-specific (different instances)
    EXPECT_NE(pipelineCacher1.get(), pipelineCacher2.get());
    
    // Shader compilation cacher should be global (same instance for any device)
    auto shaderCacher1 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device1);
    auto shaderCacher2 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device2);
    EXPECT_EQ(shaderCacher1.get(), shaderCacher2.get());
}

TEST(CashSystem_MultiDevice, DeviceIsolation) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice1 device1;
    
    // Register and create pipeline cacher
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(&device1);
    
    // Store data on device1's cache
    std::string testKey = "device1_pipeline";
    std::string testValue = "device1_value";
    bool stored = pipelineCacher->Cache(testKey, testValue);
    EXPECT_TRUE(stored);
    
    // Retrieve and verify
    auto retrieved = pipelineCacher->GetCached(testKey);
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved.value(), testValue);
}

TEST(CashSystem_MultiDevice, DeviceIdentifierSystem) {
    MockVulkanDevice1 device1;
    MockVulkanDevice2 device2;
    
    // Test DeviceIdentifier creation
    DeviceIdentifier id1(&device1);
    DeviceIdentifier id2(&device2);
    DeviceIdentifier id1_copy(&device1);
    
    // Different devices should have different identifiers
    EXPECT_NE(id1.GetHash(), id2.GetHash());
    
    // Same device should have same identifier
    EXPECT_EQ(id1.GetHash(), id1_copy.GetHash());
    
    // Verify identifier contents
    EXPECT_EQ(id1.GetDeviceName(), "RTX 3080");
    EXPECT_EQ(id2.GetDeviceName(), "GTX 1060");
}

TEST(CashSystem_MultiDevice, LazyRegistryInitialization) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice1 device1;
    
    // Initially no registries should exist
    EXPECT_EQ(mainCacher->GetDeviceRegistryCount(), 0);
    
    // Create a cacher - this should lazily initialize device registry
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(&device1);
    
    // Now one registry should exist
    EXPECT_EQ(mainCacher->GetDeviceRegistryCount(), 1);
    
    // Create another device
    MockVulkanDevice2 device2;
    auto pipelineCacher2 = mainCacher->CreateCacher<PipelineCacher>(&device2);
    
    // Now two registries should exist
    EXPECT_EQ(mainCacher->GetDeviceRegistryCount(), 2);
}

TEST(CashSystem_MultiDevice, HybridCachingPatterns) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice1 device1;
    
    // Register both device-dependent and device-independent cachers
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->RegisterType<ShaderCompilationCacher>("ShaderCompilationCacher");
    
    // Create instances
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(&device1);
    auto shaderCacher = mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr);
    
    // Test device-dependent caching
    std::string pipelineKey = "vertex_shader_pipeline";
    std::string pipelineValue = "VkPipeline_handle_device1";
    bool storedPipeline = pipelineCacher->Cache(pipelineKey, pipelineValue);
    EXPECT_TRUE(storedPipeline);
    
    // Test device-independent caching
    std::string shaderKey = "vertex_shader_spv";
    std::string shaderValue = "compiled_spirv_data";
    bool storedShader = shaderCacher->Cache(shaderKey, shaderValue);
    EXPECT_TRUE(storedShader);
    
    // Verify device-dependent cache is per-device
    MockVulkanDevice2 device2;
    auto pipelineCacher2 = mainCacher->CreateCacher<PipelineCacher>(&device2);
    auto pipelineMiss = pipelineCacher2->GetCached(pipelineKey);
    EXPECT_FALSE(pipelineMiss.has_value());
    
    // Verify device-independent cache is global
    auto shaderCacher2 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device2);
    auto shaderHit = shaderCacher2->GetCached(shaderKey);
    EXPECT_TRUE(shaderHit.has_value());
    EXPECT_EQ(shaderHit.value(), shaderValue);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}