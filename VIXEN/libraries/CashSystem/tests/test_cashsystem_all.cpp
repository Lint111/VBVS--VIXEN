#include <gtest/gtest.h>
#include <MainCacher.h>
#include <MainCashLogger.h>
#include <PipelineCacher.h>
#include <ShaderCompilationCacher.h>
#include "logger/Logger.h"

// Mock VulkanDevice for comprehensive testing
class ComprehensiveTestDevice {
public:
    std::string GetDeviceName() const { return "ComprehensiveTestDevice"; }
    uint32_t GetDeviceId() const { return 7777; }
    uint32_t GetVendorId() const { return 0x1234; }
};

// Mock structures for realistic testing
struct TestPipelineConfig {
    std::string vertexShader;
    std::string fragmentShader;
    bool depthTestEnabled;
    uint32_t renderPass;
};

struct TestShaderData {
    std::string sourcePath;
    std::vector<uint32_t> spirvBinary;
    std::string entryPoint;
};

TEST(CashSystem_All, ComprehensiveSystemTest) {
    // Create comprehensive test environment
    auto parentLogger = std::make_unique<Logger>("TestApplication", true);
    auto mainLogger = std::make_unique<MainCashLogger>("CashSystem", parentLogger.get());
    mainLogger->SetDebugMode(true);
    
    auto mainCacher = std::make_unique<MainCacher>(parentLogger.get());
    
    ComprehensiveTestDevice device1, device2, device3;
    
    // Test 1: Type registration and factory creation
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->CreateGlobalCacher<ShaderCompilationCacher>();
    
    EXPECT_EQ(mainCacher->GetRegisteredTypes().size(), 1);
    
    auto pipeline1 = mainCacher->CreateCacher<PipelineCacher>(&device1);
    auto pipeline2 = mainCacher->CreateCacher<PipelineCacher>(&device2);
    auto shaderCompiler = mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr);
    
    EXPECT_TRUE(pipeline1 != nullptr);
    EXPECT_TRUE(pipeline2 != nullptr);
    EXPECT_TRUE(shaderCompiler != nullptr);
    
    // Test 2: Device-specific caching
    TestPipelineConfig config1{
        .vertexShader = "main.vert",
        .fragmentShader = "main.frag", 
        .depthTestEnabled = true,
        .renderPass = 1
    };
    
    std::string pipelineKey = config1.vertexShader + "_" + config1.fragmentShader + "_depth_" + 
                             std::to_string(config1.depthTestEnabled) + "_pass_" + std::to_string(config1.renderPass);
    
    bool stored = pipeline1->Cache(pipelineKey, "VkPipeline_Handle_Device1");
    EXPECT_TRUE(stored);
    
    auto retrieved = pipeline1->GetCached(pipelineKey);
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved.value(), "VkPipeline_Handle_Device1");
    
    // Test 3: Device isolation
    auto device2Value = pipeline2->GetCached(pipelineKey);
    EXPECT_FALSE(device2Value.has_value()); // Different device, different cache
    
    // Test 4: Global shader compilation cache
    TestShaderData shaderData1{
        .sourcePath = "shaders/basic.vert",
        .spirvBinary = {0x07230203, 0x00010000, 0x00080001, 0x00000019}, // Mock SPIR-V
        .entryPoint = "main"
    };
    
    bool shaderStored = shaderCompiler->Cache(shaderData1.sourcePath, "compiled_spirv_data_xyz");
    EXPECT_TRUE(shaderStored);
    
    // Test 5: Cross-device shader sharing
    auto shaderCompiler2 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device2);
    auto sharedShader = shaderCompiler2->GetCached(shaderData1.sourcePath);
    
    EXPECT_TRUE(sharedShader.has_value());
    EXPECT_EQ(sharedShader.value(), "compiled_spirv_data_xyz");
    
    // Test 6: Performance with multiple operations
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 100; ++i) {
        std::string testKey = "perf_test_" + std::to_string(i);
        std::string testValue = "perf_value_" + std::to_string(i);
        
        pipeline1->Cache(testKey, testValue);
        auto retrieved = pipeline1->GetCached(testKey);
        EXPECT_TRUE(retrieved.has_value());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Should complete quickly
    EXPECT_LT(duration.count(), 50000); // 50ms
    
    // Test 7: Logger functionality
    mainLogger->LogToSubLogger("PipelineCacher", LogLevel::LOG_INFO, "Pipeline operation completed");
    mainLogger->LogToSubLogger("ShaderCompilationCacher", LogLevel::LOG_DEBUG, "Shader cache hit");
    mainLogger->Log(LogLevel::LOG_WARNING, "System warning");
    
    std::string allLogs = mainLogger->ExtractAllLogs();
    EXPECT_FALSE(allLogs.empty());
    
    // Test 8: Registry management
    EXPECT_EQ(mainCacher->GetDeviceRegistryCount(), 2); // device1 and device2
    
    // Test 9: Memory efficiency verification
    auto shaderCompiler3 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device3);
    EXPECT_EQ(shaderCompiler.get(), shaderCompiler2.get()); // Same global instance
    EXPECT_EQ(shaderCompiler2.get(), shaderCompiler3.get()); // All share same instance
    
    // Test 10: Error handling
    mainLogger->SetDebugMode(false);
    EXPECT_FALSE(mainLogger->IsDebugMode());
    
    mainLogger->SetDebugMode(true);
    EXPECT_TRUE(mainLogger->IsDebugMode());
    
    // Final verification: system should be in consistent state
    EXPECT_EQ(mainCacher->GetRegisteredTypes().size(), 1);
    EXPECT_EQ(mainCacher->GetDeviceRegistryCount(), 2);
    EXPECT_EQ(mainLogger->GetSubLoggerCount(), 0); // No sub-loggers created explicitly
}

TEST(CashSystem_All, RealisticRenderingWorkflow) {
    // Simulate a realistic rendering application workflow
    auto logger = std::make_unique<Logger>("RenderingApp", true);
    auto mainLogger = std::make_unique<MainCashLogger>("CashSystem", logger.get());
    mainLogger->SetDebugMode(true);
    
    auto mainCacher = std::make_unique<MainCacher>(logger.get());
    
    ComprehensiveTestDevice primaryDevice, secondaryDevice;
    
    // Setup caching infrastructure
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->CreateGlobalCacher<ShaderCompilationCacher>();
    
    // Simulate shader compilation phase
    auto shaderCompiler = mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr);
    
    std::vector<std::string> applicationShaders = {
        "shaders/vertex_main.vert",
        "shaders/fragment_lighting.frag", 
        "shaders/fragment_shadow.frag",
        "shaders/compute_particles.comp"
    };
    
    // Compile all application shaders once
    for (const auto& shader : applicationShaders) {
        std::string compiledData = "SPIRV_COMPILED_" + shader;
        shaderCompiler->Cache(shader, compiledData);
    }
    
    // Simulate pipeline creation for different rendering passes
    auto primaryPipeline = mainCacher->CreateCacher<PipelineCacher>(&primaryDevice);
    auto secondaryPipeline = mainCacher->CreateCacher<PipelineCacher>(&secondaryDevice);
    
    std::vector<std::string> pipelineConfigs = {
        "main_lighting_pass",
        "shadow_map_pass", 
        "post_process_pass",
        "ui_render_pass"
    };
    
    // Create pipelines on primary device
    for (const auto& config : pipelineConfigs) {
        std::string pipelineHandle = "VkPipeline_" + config + "_primary";
        primaryPipeline->Cache(config, pipelineHandle);
    }
    
    // Create pipelines on secondary device
    for (const auto& config : pipelineConfigs) {
        std::string pipelineHandle = "VkPipeline_" + config + "_secondary";
        secondaryPipeline->Cache(config, pipelineHandle);
    }
    
    // Simulate frame rendering with cache hits
    for (int frame = 0; frame < 10; ++frame) {
        // Use shared compiled shaders
        for (const auto& shader : applicationShaders) {
            auto compiledShader = shaderCompiler->GetCached(shader);
            EXPECT_TRUE(compiledShader.has_value());
        }
        
        // Use device-specific pipelines
        for (const auto& config : pipelineConfigs) {
            auto primaryPipe = primaryPipeline->GetCached(config);
            auto secondaryPipe = secondaryPipeline->GetCached(config);
            
            EXPECT_TRUE(primaryPipe.has_value());
            EXPECT_TRUE(secondaryPipe.has_value());
            
            // Verify device isolation
            EXPECT_NE(primaryPipe.value(), secondaryPipe.value());
        }
    }
    
    // Verify final system state
    EXPECT_EQ(mainCacher->GetDeviceRegistryCount(), 2);
    
    // All shader compilers should be the same instance (global sharing)
    auto shaderCompiler2 = mainCacher->CreateCacher<ShaderCompilationCacher>(&primaryDevice);
    auto shaderCompiler3 = mainCacher->CreateCacher<ShaderCompilationCacher>(&secondaryDevice);
    EXPECT_EQ(shaderCompiler.get(), shaderCompiler2.get());
    EXPECT_EQ(shaderCompiler2.get(), shaderCompiler3.get());
    
    // Performance verification: shader access should be very fast after compilation
    auto perfStart = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 1000; ++i) {
        for (const auto& shader : applicationShaders) {
            shaderCompiler->GetCached(shader);
        }
    }
    
    auto perfEnd = std::chrono::high_resolution_clock::now();
    auto perfDuration = std::chrono::duration_cast<std::chrono::microseconds>(perfEnd - perfStart);
    
    // Should be extremely fast for cache hits
    EXPECT_LT(perfDuration.count(), 10000); // 10ms for 4000 cache lookups
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}