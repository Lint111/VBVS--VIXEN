#include <gtest/gtest.h>
#include <CashSystem/MainCacher.h>
#include <CashSystem/PipelineCacher.h>
#include <CashSystem/ShaderCompilationCacher.h>
#include <chrono>

// Mock VulkanDevice
class MockVulkanDevice {
public:
    std::string GetDeviceName() const { return "PerformanceTestDevice"; }
    uint32_t GetDeviceId() const { return 9999; }
};

TEST(CashSystem_Performance, CacheHitPerformance) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device;
    
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(&device);
    
    // Pre-populate cache
    for (int i = 0; i < 1000; ++i) {
        std::string key = "key_" + std::to_string(i);
        std::string value = "value_" + std::to_string(i);
        pipelineCacher->Cache(key, value);
    }
    
    // Measure cache hit performance
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 10000; ++i) {
        std::string key = "key_" + std::to_string(i % 1000); // All should hit
        auto result = pipelineCacher->GetCached(key);
        EXPECT_TRUE(result.has_value());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Should be very fast - less than 100ms for 10k operations
    EXPECT_LT(duration.count(), 100000); // 100ms in microseconds
    
    // Log performance (optional, for debugging)
    // std::cout << "Cache hit performance: " << duration.count() / 10000.0 << " microseconds per operation\n";
}

TEST(CashSystem_Performance, CacheMissPerformance) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device;
    
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(&device);
    
    // Measure cache miss performance
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 10000; ++i) {
        std::string key = "nonexistent_" + std::to_string(i);
        auto result = pipelineCacher->GetCached(key);
        EXPECT_FALSE(result.has_value());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Should also be fast
    EXPECT_LT(duration.count(), 100000); // 100ms in microseconds
}

TEST(CashSystem_Performance, MultiDevicePerformance) {
    auto mainCacher = std::make_unique<MainCacher>();
    
    MockVulkanDevice device1, device2, device3;
    
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    
    // Create cachers for multiple devices
    auto pipeline1 = mainCacher->CreateCacher<PipelineCacher>(&device1);
    auto pipeline2 = mainCacher->CreateCacher<PipelineCacher>(&device2);
    auto pipeline3 = mainCacher->CreateCacher<PipelineCacher>(&device3);
    
    // Measure performance with multiple device registries
    auto start = std::chrono::high_resolution_clock::now();
    
    // Each device has its own cache
    pipeline1->Cache("key1", "value1");
    pipeline2->Cache("key2", "value2");
    pipeline3->Cache("key3", "value3");
    
    // Retrieve from each device
    auto val1 = pipeline1->GetCached("key1");
    auto val2 = pipeline2->GetCached("key2");
    auto val3 = pipeline3->GetCached("key3");
    
    EXPECT_TRUE(val1.has_value());
    EXPECT_TRUE(val2.has_value());
    EXPECT_TRUE(val3.has_value());
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Should still be fast even with multiple devices
    EXPECT_LT(duration.count(), 10000); // 10ms
}

TEST(CashSystem_Performance, HybridCachingEfficiency) {
    auto mainCacher = std::make_unique<MainCacher>();
    
    MockVulkanDevice device1, device2, device3;
    
    // Set up hybrid caching
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->CreateGlobalCacher<ShaderCompilationCacher>();
    
    auto shaderCompiler = mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr);
    
    // Simulate expensive shader compilation
    auto start = std::chrono::high_resolution_clock::now();
    
    // Compile 100 shaders (expensive)
    for (int i = 0; i < 100; ++i) {
        std::string shaderKey = "shader_" + std::to_string(i) + ".spv";
        std::string compiledData = "compiled_spirv_data_" + std::to_string(i);
        shaderCompiler->Cache(shaderKey, compiledData);
    }
    
    auto compileEnd = std::chrono::high_resolution_clock::now();
    auto compileDuration = std::chrono::duration_cast<std::chrono::microseconds>(compileEnd - start);
    
    // Now simulate 3 devices all using the compiled shaders
    auto compiler1 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device1);
    auto compiler2 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device2);
    auto compiler3 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device3);
    
    // All devices should instantly access compiled shaders
    int cacheHits = 0;
    for (int i = 0; i < 100; ++i) {
        std::string shaderKey = "shader_" + std::to_string(i) + ".spv";
        
        auto data1 = compiler1->GetCached(shaderKey);
        auto data2 = compiler2->GetCached(shaderKey);
        auto data3 = compiler3->GetCached(shaderKey);
        
        if (data1.has_value() && data2.has_value() && data3.has_value()) {
            cacheHits++;
        }
    }
    
    auto accessEnd = std::chrono::high_resolution_clock::now();
    auto accessDuration = std::chrono::duration_cast<std::chrono::microseconds>(accessEnd - compileEnd);
    
    // All 100 shaders should be available to all devices
    EXPECT_EQ(cacheHits, 100);
    
    // Access should be much faster than compilation
    EXPECT_LT(accessDuration.count(), compileDuration.count() / 10);
    
    // This demonstrates the performance benefit: compile once, use everywhere
}

TEST(CashSystem_Performance, MemoryUsageEfficiency) {
    auto mainCacher = std::make_unique<MainCacher>();
    
    MockVulkanDevice device1, device2;
    
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->CreateGlobalCacher<ShaderCompilationCacher>();
    
    // Measure creation overhead
    auto start = std::chrono::high_resolution_clock::now();
    
    // Create many pipeline cachers (should be device-specific)
    std::vector<decltype(mainCacher->CreateCacher<PipelineCacher>(&device1))> pipelineCachers;
    for (int i = 0; i < 10; ++i) {
        pipelineCachers.push_back(mainCacher->CreateCacher<PipelineCacher>(&device1));
    }
    
    // Create many shader cachers (should be global/shared)
    std::vector<decltype(mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr))> shaderCachers;
    for (int i = 0; i < 10; ++i) {
        shaderCachers.push_back(mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Creation should be fast
    EXPECT_LT(duration.count(), 50000); // 50ms
    
    // Verify that shader cachers are actually shared (same instances)
    for (size_t i = 1; i < shaderCachers.size(); ++i) {
        EXPECT_EQ(shaderCachers[0].get(), shaderCachers[i].get());
    }
    
    // Verify that pipeline cachers are device-specific
    for (size_t i = 1; i < pipelineCachers.size(); ++i) {
        EXPECT_EQ(pipelineCachers[0].get(), pipelineCachers[i].get()); // Same device = same instance
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}