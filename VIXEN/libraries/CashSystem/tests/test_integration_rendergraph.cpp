#include <gtest/gtest.h>
#include <MainCacher.h>
#include <PipelineCacher.h>
#include <ShaderCompilationCacher.h>

// Mock VulkanDevice for RenderGraph integration testing
class MockVulkanDevice {
public:
    std::string GetDeviceName() const { return "RenderGraphTestDevice"; }
    uint32_t GetDeviceId() const { return 5678; }
};

// Mock structures that might be used in RenderGraph
struct MockPipelineConfig {
    std::string vertexShader;
    std::string fragmentShader;
    uint32_t renderPassId;
};

struct MockShaderModule {
    std::vector<uint32_t> spirvData;
    std::string shaderPath;
};

TEST(CashSystem_IntegrationRenderGraph, PipelineCacherIntegration) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device;
    
    // Register pipeline cacher (device-dependent)
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(&device);
    
    // Simulate pipeline creation in RenderGraph
    MockPipelineConfig config1{
        .vertexShader = "vertex.vert",
        .fragmentShader = "fragment.frag",
        .renderPassId = 1
    };
    
    MockPipelineConfig config2{
        .vertexShader = "vertex.vert",
        .fragmentShader = "lighting.frag",
        .renderPassId = 1
    };
    
    // Create cache keys (would be more complex in real usage)
    std::string key1 = config1.vertexShader + "_" + config1.fragmentShader + "_pass" + std::to_string(config1.renderPassId);
    std::string key2 = config2.vertexShader + "_" + config2.fragmentShader + "_pass" + std::to_string(config2.renderPassId);
    
    // Cache pipeline configurations
    bool stored1 = pipelineCacher->Cache(key1, "VkPipelineHandle1");
    bool stored2 = pipelineCacher->Cache(key2, "VkPipelineHandle2");
    
    EXPECT_TRUE(stored1);
    EXPECT_TRUE(stored2);
    
    // Simulate RenderGraph requesting cached pipelines
    auto pipeline1 = pipelineCacher->GetCached(key1);
    auto pipeline2 = pipelineCacher->GetCached(key2);
    
    EXPECT_TRUE(pipeline1.has_value());
    EXPECT_TRUE(pipeline2.has_value());
    EXPECT_EQ(pipeline1.value(), "VkPipelineHandle1");
    EXPECT_EQ(pipeline2.value(), "VkPipelineHandle2");
}

TEST(CashSystem_IntegrationRenderGraph, ShaderCompilationCacheSharing) {
    auto mainCacher = std::make_unique<MainCacher>();
    
    // Create global shader compilation cache
    mainCacher->CreateGlobalCacher<ShaderCompilationCacher>();
    
    MockVulkanDevice device1, device2, device3;
    
    // Multiple devices can share compiled shaders
    auto compiler1 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device1);
    auto compiler2 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device2);
    auto compiler3 = mainCacher->CreateCacher<ShaderCompilationCacher>(&device3);
    
    // Simulate shader compilation (expensive operation)
    std::vector<std::string> shaderFiles = {
        "shaders/vertex.vert",
        "shaders/fragment.frag",
        "shaders/compute.comp",
        "shaders/geometry.geom"
    };
    
    // Compile shaders once
    for (const auto& shaderFile : shaderFiles) {
        std::string compiledData = "compiled_spirv_for_" + shaderFile;
        compiler1->Cache(shaderFile, compiledData);
    }
    
    // All devices should have immediate access
    for (const auto& shaderFile : shaderFiles) {
        auto data1 = compiler1->GetCached(shaderFile);
        auto data2 = compiler2->GetCached(shaderFile);
        auto data3 = compiler3->GetCached(shaderFile);
        
        EXPECT_TRUE(data1.has_value());
        EXPECT_TRUE(data2.has_value());
        EXPECT_TRUE(data3.has_value());
        
        EXPECT_EQ(data1.value(), "compiled_spirv_for_" + shaderFile);
        EXPECT_EQ(data2.value(), "compiled_spirv_for_" + shaderFile);
        EXPECT_EQ(data3.value(), "compiled_spirv_for_" + shaderFile);
    }
    
    // Verify all compilers are the same instance (global sharing)
    EXPECT_EQ(compiler1.get(), compiler2.get());
    EXPECT_EQ(compiler2.get(), compiler3.get());
}

TEST(CashSystem_IntegrationRenderGraph, NodeInstanceDeviceContext) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device;
    
    // Register device-dependent types
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    
    // Simulate node instances receiving device context
    auto nodeInstance1 = mainCacher->CreateCacher<PipelineCacher>(&device);
    auto nodeInstance2 = mainCacher->CreateCacher<PipelineCacher>(&device);
    
    // Both instances should share the same device-specific cache
    EXPECT_EQ(nodeInstance1.get(), nodeInstance2.get()); // Same device = same cache
    
    // Store data through one instance
    nodeInstance1->Cache("shared_data", "value");
    
    // Should be accessible through the other
    auto retrieved = nodeInstance2->GetCached("shared_data");
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved.value(), "value");
}

TEST(CashSystem_IntegrationRenderGraph, GraphCompilationWorkflow) {
    auto mainCacher = std::make_unique<MainCacher>();
    MockVulkanDevice device;
    
    // Set up caching for graph compilation
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->CreateGlobalCacher<ShaderCompilationCacher>();
    
    // Simulate graph compilation phases
    struct GraphCompilationResult {
        std::string pipelineCache;
        std::unordered_map<std::string, std::string> shaderCache;
        bool success;
    };
    
    GraphCompilationResult result;
    
    // Phase 1: Shader compilation (device-independent)
    auto shaderCompiler = mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr);
    
    std::vector<std::string> shaders = {
        "main_vertex.vert",
        "main_fragment.frag",
        "shadow_vertex.vert",
        "shadow_fragment.frag"
    };
    
    for (const auto& shader : shaders) {
        std::string compiledData = "SPIRV_" + shader;
        shaderCompiler->Cache(shader, compiledData);
        result.shaderCache[shader] = compiledData;
    }
    
    // Phase 2: Pipeline creation (device-dependent)
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(&device);
    
    std::vector<std::string> pipelines = {
        "main_pipeline",
        "shadow_pipeline",
        "ui_pipeline"
    };
    
    for (const auto& pipeline : pipelines) {
        std::string pipelineHandle = "VkPipeline_" + pipeline;
        pipelineCacher->Cache(pipeline, pipelineHandle);
        result.pipelineCache = pipelineHandle; // Last one (for demo)
    }
    
    result.success = true;
    
    // Verify compilation results
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.shaderCache.size(), 4);
    EXPECT_FALSE(result.pipelineCache.empty());
    
    // Simulate graph execution with cache hits
    auto mainPipeline = pipelineCacher->GetCached("main_pipeline");
    auto mainVertShader = shaderCompiler->GetCached("main_vertex.vert");
    auto mainFragShader = shaderCompiler->GetCached("main_fragment.frag");
    
    EXPECT_TRUE(mainPipeline.has_value());
    EXPECT_TRUE(mainVertShader.has_value());
    EXPECT_TRUE(mainFragShader.has_value());
    
    // This demonstrates how CashSystem enables efficient graph compilation and execution
}

TEST(CashSystem_IntegrationRenderGraph, MultiDeviceGraphScenario) {
    auto mainCacher = std::make_unique<MainCacher>();
    
    MockVulkanDevice discreteGPU, integratedGPU;
    
    // Set up different caching strategies for different devices
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->CreateGlobalCacher<ShaderCompilationCacher>();
    
    // Simulate discrete GPU workflow
    auto discretePipeline = mainCacher->CreateCacher<PipelineCacher>(&discreteGPU);
    auto discreteShaderCompiler = mainCacher->CreateCacher<ShaderCompilationCacher>(&discreteGPU);
    
    // Simulate integrated GPU workflow
    auto integratedPipeline = mainCacher->CreateCacher<PipelineCacher>(&integratedGPU);
    auto integratedShaderCompiler = mainCacher->CreateCacher<ShaderCompilationCacher>(&integratedGPU);
    
    // Compile shaders once (shared across devices)
    std::string sharedShader = "shared.vert";
    discreteShaderCompiler->Cache(sharedShader, "shared_spirv_data");
    
    // Both devices should have access to compiled shader
    auto discreteAccess = integratedShaderCompiler->GetCached(sharedShader);
    auto integratedAccess = discreteShaderCompiler->GetCached(sharedShader);
    
    EXPECT_TRUE(discreteAccess.has_value());
    EXPECT_TRUE(integratedAccess.has_value());
    EXPECT_EQ(discreteAccess.value(), "shared_spirv_data");
    EXPECT_EQ(integratedAccess.value(), "shared_spirv_data");
    
    // But pipelines are device-specific
    discretePipeline->Cache("test_pipeline", "discrete_pipeline_handle");
    integratedPipeline->Cache("test_pipeline", "integrated_pipeline_handle");
    
    auto discretePipelineData = discretePipeline->GetCached("test_pipeline");
    auto integratedPipelineData = integratedPipeline->GetCached("test_pipeline");
    
    EXPECT_TRUE(discretePipelineData.has_value());
    EXPECT_TRUE(integratedPipelineData.has_value());
    EXPECT_NE(discretePipelineData.value(), integratedPipelineData.value());
    
    // This shows the hybrid caching benefits in multi-device scenarios
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}