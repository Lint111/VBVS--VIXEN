#include <gtest/gtest.h>
#include <MainCacher.h>
#include <PipelineCacher.h>
#include <ShaderCompilationCacher.h>

// Mock VulkanDevice
class MockVulkanDevice {
public:
    std::string GetDeviceName() const { return "TestDevice"; }
    uint32_t GetDeviceId() const { return 1234; }
};

TEST(CashSystem_HybridCaching, DeviceDependentVsIndependent) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device1, device2;
    
    // Register both types
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher"); // Device-dependent
    mainCacher->RegisterType<ShaderCompilationCacher>("ShaderCompilationCacher"); // Device-independent
    
    // Create instances
    auto pipeline1 = mainCacher->CreateCacher<PipelineCacher>(&device1);
    auto pipeline2 = mainCacher->CreateCacher<PipelineCacher>(&device2);
    auto shader1 = mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr);
    auto shader2 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device1);
    auto shader3 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device2);
    
    // Verify all created successfully
    EXPECT_TRUE(pipeline1 != nullptr);
    EXPECT_TRUE(pipeline2 != nullptr);
    EXPECT_TRUE(shader1 != nullptr);
    EXPECT_TRUE(shader2 != nullptr);
    EXPECT_TRUE(shader3 != nullptr);
    
    // Pipeline cachers should be device-specific (different instances)
    EXPECT_NE(pipeline1.get(), pipeline2.get());
    
    // Shader cachers should be device-independent (same instance)
    EXPECT_EQ(shader1.get(), shader2.get());
    EXPECT_EQ(shader2.get(), shader3.get());
    EXPECT_EQ(shader1.get(), shader3.get());
}

TEST(CashSystem_HybridCaching, SharedCompilationCache) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device1, device2;
    
    // Register shader compilation cacher (device-independent)
    mainCacher->CreateGlobalCacher<ShaderCompilationCacher>();
    
    // Simulate shader compilation on device1
    auto shaderCompiler = mainCacher->CreateCacher<ShaderCompilationCacher>(&device1);
    
    std::string shaderKey = "vertex_shader.vert";
    std::string compiledSPV = "compiled_spirv_binary_data_xyz123";
    
    // Store compiled shader
    bool stored = shaderCompiler->Cache(shaderKey, compiledSPV);
    EXPECT_TRUE(stored);
    
    // Verify compilation cache hit from device2
    auto shaderCompiler2 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device2);
    auto retrieved = shaderCompiler2->GetCached(shaderKey);
    
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved.value(), compiledSPV);
    
    // This demonstrates the key benefit: "Compile once, use everywhere"
    // The shader was compiled once and can be reused by any device
}

TEST(CashSystem_HybridCaching, DeviceSpecificPipelines) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device1, device2;
    
    // Register pipeline cacher (device-dependent)
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    
    // Create pipeline on device1
    auto pipeline1 = mainCacher->CreateCacher<PipelineCacher>(&device1);
    std::string pipelineKey = "graphics_pipeline_main";
    std::string pipelineHandle1 = "VkPipeline_handle_device1_abc";
    
    bool stored1 = pipeline1->Cache(pipelineKey, pipelineHandle1);
    EXPECT_TRUE(stored1);
    
    // Create pipeline on device2 - should be separate cache
    auto pipeline2 = mainCacher->CreateCacher<PipelineCacher>(&device2);
    std::string pipelineHandle2 = "VkPipeline_handle_device2_def";
    
    bool stored2 = pipeline2->Cache(pipelineKey, pipelineHandle2);
    EXPECT_TRUE(stored2);
    
    // Verify device isolation
    auto retrievedFromDevice1 = pipeline1->GetCached(pipelineKey);
    auto retrievedFromDevice2 = pipeline2->GetCached(pipelineKey);
    
    EXPECT_TRUE(retrievedFromDevice1.has_value());
    EXPECT_TRUE(retrievedFromDevice2.has_value());
    
    EXPECT_EQ(retrievedFromDevice1.value(), pipelineHandle1);
    EXPECT_EQ(retrievedFromDevice2.value(), pipelineHandle2);
    
    // Cross-device verification
    EXPECT_NE(retrievedFromDevice1.value(), retrievedFromDevice2.value());
}

TEST(CashSystem_HybridCaching, PerformanceOptimization) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device1, device2, device3;
    
    // Simulate a large shader compilation cache scenario
    mainCacher->CreateGlobalCacher<ShaderCompilationCacher>();
    auto globalCompiler = mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr);
    
    // Simulate compiling many shaders (expensive operation)
    for (int i = 0; i < 100; ++i) {
        std::string shaderKey = "shader_" + std::to_string(i) + ".spv";
        std::string compiledData = "compiled_data_" + std::to_string(i);
        globalCompiler->Cache(shaderKey, compiledData);
    }
    
    // Now multiple devices can access the same compiled shaders
    auto compiler1 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device1);
    auto compiler2 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device2);
    auto compiler3 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device3);
    
    // All devices should have access to the same 100 compiled shaders
    for (int i = 0; i < 100; ++i) {
        std::string shaderKey = "shader_" + std::to_string(i) + ".spv";
        
        auto data1 = compiler1->GetCached(shaderKey);
        auto data2 = compiler2->GetCached(shaderKey);
        auto data3 = compiler3->GetCached(shaderKey);
        
        EXPECT_TRUE(data1.has_value());
        EXPECT_TRUE(data2.has_value());
        EXPECT_TRUE(data3.has_value());
        
        EXPECT_EQ(data1.value(), "compiled_data_" + std::to_string(i));
        EXPECT_EQ(data2.value(), "compiled_data_" + std::to_string(i));
        EXPECT_EQ(data3.value(), "compiled_data_" + std::to_string(i));
    }
}

TEST(CashSystem_HybridCaching, MemoryEfficiency) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device1, device2;
    
    // Register both types
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->CreateGlobalCacher<ShaderCompilationCacher>();
    
    // Device-specific caches
    auto pipeline1 = mainCacher->CreateCacher<PipelineCacher>(&device1);
    auto pipeline2 = mainCacher->CreateCacher<PipelineCacher>(&device2);
    
    // Global cache (shared)
    auto shaderCompiler1 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device1);
    auto shaderCompiler2 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device2);
    
    // Verify memory efficiency:
    // 1. Pipeline cachers should be different instances
    EXPECT_NE(pipeline1.get(), pipeline2.get());
    
    // 2. Shader compilation cachers should be same instance
    EXPECT_EQ(shaderCompiler1.get(), shaderCompiler2.get());
    
    // 3. Each device has its own pipeline cache but shares compilation cache
    std::string testKey = "memory_efficiency_test";
    
    pipeline1->Cache(testKey, "device1_pipeline_data");
    pipeline2->Cache(testKey, "device2_pipeline_data");
    shaderCompiler1->Cache(testKey, "shared_compilation_data");
    
    // Verify separation
    EXPECT_EQ(pipeline1->GetCached(testKey).value(), "device1_pipeline_data");
    EXPECT_EQ(pipeline2->GetCached(testKey).value(), "device2_pipeline_data");
    
    // Verify sharing
    EXPECT_EQ(shaderCompiler1->GetCached(testKey).value(), "shared_compilation_data");
    EXPECT_EQ(shaderCompiler2->GetCached(testKey).value(), "shared_compilation_data");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}