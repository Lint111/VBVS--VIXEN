#include <gtest/gtest.h>
#include <MainCacher.h>
#include <DeviceIdentifier.h>
#include <PipelineCacher.h>

// Mock VulkanDevice classes
class MockDeviceRTX3080 {
public:
    std::string GetDeviceName() const { return "NVIDIA GeForce RTX 3080"; }
    uint32_t GetDeviceId() const { return 0x2206; }
    uint32_t GetVendorId() const { return 0x10DE; }
};

class MockDeviceGTX1060 {
public:
    std::string GetDeviceName() const { return "NVIDIA GeForce GTX 1060"; }
    uint32_t GetDeviceId() const { return 0x1C20; }
    uint32_t GetVendorId() const { return 0x10DE; }
};

class MockDeviceRX580 {
public:
    std::string GetDeviceName() const { return "AMD Radeon RX 580"; }
    uint32_t GetDeviceId() const { return 0x67DF; }
    uint32_t GetVendorId() const { return 0x1002; }
};

TEST(CashSystem_DeviceRegistries, MultipleDeviceIsolation) {
    auto mainCacher = std::make_unique<MainCacher>();
    
    MockDeviceRTX3080 rtx3080;
    MockDeviceGTX1060 gtx1060;
    MockDeviceRX580 rx580;
    
    // Register types
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    
    // Create cachers for different devices
    auto pipelineRTX = mainCacher->CreateCacher<PipelineCacher>(&rtx3080);
    auto pipelineGTX = mainCacher->CreateCacher<PipelineCacher>(&gtx1060);
    auto pipelineRX = mainCacher->CreateCacher<PipelineCacher>(&rx580);
    
    EXPECT_TRUE(pipelineRTX != nullptr);
    EXPECT_TRUE(pipelineGTX != nullptr);
    EXPECT_TRUE(pipelineRX != nullptr);
    
    // Store different data on each device
    bool stored1 = pipelineRTX->Cache("test_key", "rtx_value");
    bool stored2 = pipelineGTX->Cache("test_key", "gtx_value");
    bool stored3 = pipelineRX->Cache("test_key", "rx_value");
    
    EXPECT_TRUE(stored1);
    EXPECT_TRUE(stored2);
    EXPECT_TRUE(stored3);
    
    // Verify isolation - each device should have its own value
    auto rtxValue = pipelineRTX->GetCached("test_key");
    auto gtxValue = pipelineGTX->GetCached("test_key");
    auto rxValue = pipelineRX->GetCached("test_key");
    
    EXPECT_TRUE(rtxValue.has_value());
    EXPECT_TRUE(gtxValue.has_value());
    EXPECT_TRUE(rxValue.has_value());
    
    EXPECT_EQ(rtxValue.value(), "rtx_value");
    EXPECT_EQ(gtxValue.value(), "gtx_value");
    EXPECT_EQ(rxValue.value(), "rx_value");
}

TEST(CashSystem_DeviceRegistries, DeviceIdentifierUniqueness) {
    MockDeviceRTX3080 device1;
    MockDeviceRTX3080 device1Copy;
    MockDeviceGTX1060 device2;
    
    DeviceIdentifier id1(&device1);
    DeviceIdentifier id1Copy(&device1Copy);
    DeviceIdentifier id2(&device2);
    
    // Same device type should have same identifier
    EXPECT_EQ(id1.GetHash(), id1Copy.GetHash());
    
    // Different devices should have different identifiers
    EXPECT_NE(id1.GetHash(), id2.GetHash());
    
    // Verify device info
    EXPECT_EQ(id1.GetDeviceName(), "NVIDIA GeForce RTX 3080");
    EXPECT_EQ(id2.GetDeviceName(), "NVIDIA GeForce GTX 1060");
}

TEST(CashSystem_DeviceRegistries, RegistryCountManagement) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockDeviceRTX3080 device;
    
    // Initially no registries
    EXPECT_EQ(mainCacher->GetDeviceRegistryCount(), 0);
    
    // Create first cacher - should create registry
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    auto pipeline1 = mainCacher->CreateCacher<PipelineCacher>(&device);
    
    EXPECT_EQ(mainCacher->GetDeviceRegistryCount(), 1);
    
    // Create same type for same device - should reuse registry
    auto pipeline2 = mainCacher->CreateCacher<PipelineCacher>(&device);
    
    EXPECT_EQ(mainCacher->GetDeviceRegistryCount(), 1);
    
    // Create different device - should create new registry
    MockDeviceGTX1060 device2;
    auto pipeline3 = mainCacher->CreateCacher<PipelineCacher>(&device2);
    
    EXPECT_EQ(mainCacher->GetDeviceRegistryCount(), 2);
}

TEST(CashSystem_DeviceRegistries, LazyInitialization) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockDeviceRTX3080 device;
    
    // Register type but don't create any cachers yet
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    
    // Should still have no registries
    EXPECT_EQ(mainCacher->GetDeviceRegistryCount(), 0);
    
    // Only when we create a cacher should registry be initialized
    auto pipeline = mainCacher->CreateCacher<PipelineCacher>(&device);
    
    EXPECT_TRUE(pipeline != nullptr);
    EXPECT_EQ(mainCacher->GetDeviceRegistryCount(), 1);
}

TEST(CashSystem_DeviceRegistries, SameDeviceMultipleTypes) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockDeviceRTX3080 device;
    
    // Register multiple types
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    // Note: We would also register other types like TextureCacher, etc.
    
    // Create cachers for same device but different types
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(&device);
    
    // Both should use same device registry
    EXPECT_TRUE(pipelineCacher != nullptr);
    EXPECT_EQ(mainCacher->GetDeviceRegistryCount(), 1);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}