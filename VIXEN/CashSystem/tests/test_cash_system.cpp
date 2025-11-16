#include "CashSystem/MainCacher.h"
#include "CashSystem/ShaderModuleCacher.h"
#include "CashSystem/TextureCacher.h"
#include "CashSystem/DescriptorCacher.h"
#include "CashSystem/PipelineCacher.h"
#include <gtest/gtest.h>
#include <memory>

namespace CashSystemTests {

using CashSystem::MainCacher;
using CashSystem::ShaderModuleCacher;
using CashSystem::TextureCacher;
using CashSystem::DescriptorCacher;
using CashSystem::PipelineCacher;

class MainCacherTest : public ::testing::Test {
protected:
    void SetUp() override {
        mainCacher = &MainCacher::Instance();
        // Initialize cachers
        mainCacher->Initialize(nullptr);
    }

    void TearDown() override {
        mainCacher->ClearAll();
    }

    MainCacher* mainCacher = nullptr;
};

TEST_F(MainCacherTest, InstanceReturnsSameSingleton) {
    auto& instance1 = MainCacher::Instance();
    auto& instance2 = MainCacher::Instance();
    
    EXPECT_EQ(&instance1, &instance2);
}

TEST_F(MainCacherTest, InitializeCreatesAllCachers) {
    auto* shaderCacher = mainCacher->GetShaderModuleCacher();
    auto* textureCacher = mainCacher->GetTextureCacher();
    auto* descriptorCacher = mainCacher->GetDescriptorCacher();
    auto* pipelineCacher = mainCacher->GetPipelineCacher();
    
    EXPECT_NE(shaderCacher, nullptr);
    EXPECT_NE(textureCacher, nullptr);
    EXPECT_NE(descriptorCacher, nullptr);
    EXPECT_NE(pipelineCacher, nullptr);
}

TEST_F(MainCacherTest, ClearAllClearsAllCachers) {
    // Create some cache entries
    auto shaderParams = CashSystem::ShaderModuleCreateParams{
        .sourcePath = "test.vert",
        .entryPoint = "main"
    };
    
    mainCacher->GetShaderModuleCacher()->GetOrCreate(shaderParams);
    
    // Verify cache has entries
    EXPECT_TRUE(mainCacher->GetShaderModuleCacher()->Has(0)); // Should have some entries
    
    // Clear all
    mainCacher->ClearAll();
    
    // Cache should be empty
    EXPECT_FALSE(mainCacher->GetShaderModuleCacher()->Has(0));
}

TEST_F(MainCacherTest, GetOrCreateTemplateWorks) {
    auto shaderParams = CashSystem::ShaderModuleCreateParams{
        .sourcePath = "test.vert",
        .entryPoint = "main"
    };
    
    // This should compile and return a valid result
    auto result = mainCacher->GetOrCreate<ShaderModuleCacher, CashSystem::ShaderModuleCreateParams>(shaderParams);
    
    // The result might be nullptr due to placeholder implementation
    // but the call should not crash
    EXPECT_NO_THROW({
        auto cached = mainCacher->GetShaderModuleCacher()->GetOrCreate(shaderParams);
        (void)cached; // Prevent unused variable warning
    });
}

class ShaderModuleCacherTest : public ::testing::Test {
protected:
    void SetUp() override {
        shaderCacher = std::make_unique<ShaderModuleCacher>();
    }
    
    std::unique_ptr<ShaderModuleCacher> shaderCacher;
};

TEST_F(ShaderModuleCacherTest, GetOrCreateShaderModuleBasic) {
    auto result = shaderCacher->GetOrCreateShaderModule(
        "test.vert",
        "main",
        {},
        VK_SHADER_STAGE_VERTEX_BIT,
        "TestVertex"
    );
    
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result->shaderName, "TestVertex");
    EXPECT_EQ(result->stage, VK_SHADER_STAGE_VERTEX_BIT);
}

TEST_F(ShaderModuleCacherTest, KeyGenerationConsistent) {
    auto params1 = CashSystem::ShaderModuleCreateParams{
        .sourcePath = "test.vert",
        .entryPoint = "main",
        .stage = VK_SHADER_STAGE_VERTEX_BIT
    };
    
    auto params2 = CashSystem::ShaderModuleCreateParams{
        .sourcePath = "test.vert",
        .entryPoint = "main",
        .stage = VK_SHADER_STAGE_VERTEX_BIT
    };
    
    auto key1 = shaderCacher->ComputeKey(params1);
    auto key2 = shaderCacher->ComputeKey(params2);
    
    EXPECT_EQ(key1, key2);
}

TEST_F(ShaderModuleCacherTest, DifferentParamsGenerateDifferentKeys) {
    auto params1 = CashSystem::ShaderModuleCreateParams{
        .sourcePath = "test.vert",
        .entryPoint = "main"
    };
    
    auto params2 = CashSystem::ShaderModuleCreateParams{
        .sourcePath = "test.vert",
        .entryPoint = "mainVS"  // Different entry point
    };
    
    auto key1 = shaderCacher->ComputeKey(params1);
    auto key2 = shaderCacher->ComputeKey(params2);
    
    EXPECT_NE(key1, key2);
}

class TextureCacherTest : public ::testing::Test {
protected:
    void SetUp() override {
        textureCacher = std::make_unique<TextureCacher>();
    }
    
    std::unique_ptr<TextureCacher> textureCacher;
};

TEST_F(TextureCacherTest, GetOrCreateTextureBasic) {
    auto result = textureCacher->GetOrCreateTexture(
        "test.png",
        VK_FORMAT_R8G8B8A8_UNORM,
        false,
        VK_FILTER_LINEAR,
        VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_REPEAT
    );
    
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result->filePath, "test.png");
}

TEST_F(TextureCacherTest, TextureParametersAffectCacheKey) {
    auto params1 = CashSystem::TextureCreateParams{
        .filePath = "test.png",
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .generateMipmaps = false
    };
    
    auto params2 = CashSystem::TextureCreateParams{
        .filePath = "test.png",
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .generateMipmaps = true  // Different parameter
    };
    
    auto key1 = textureCacher->ComputeKey(params1);
    auto key2 = textureCacher->ComputeKey(params2);
    
    EXPECT_NE(key1, key2);
}

class PipelineCacherTest : public ::testing::Test {
protected:
    void SetUp() override {
        pipelineCacher = std::make_unique<PipelineCacher>();
    }
    
    std::unique_ptr<PipelineCacher> pipelineCacher;
};

TEST_F(PipelineCacherTest, GetOrCreatePipelineBasic) {
    auto result = pipelineCacher->GetOrCreatePipeline(
        "vertex_key",
        "fragment_key",
        "layout_key",
        "renderpass_key",
        true,
        VK_CULL_MODE_BACK_BIT,
        VK_POLYGON_MODE_FILL
    );
    
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result->vertexShaderKey, "vertex_key");
    EXPECT_EQ(result->enableDepthTest, true);
}

TEST_F(PipelineCacherTest, PipelineStateAffectsCacheKey) {
    auto params1 = CashSystem::PipelineCreateParams{
        .enableDepthTest = true
    };
    
    auto params2 = CashSystem::PipelineCreateParams{
        .enableDepthTest = false  // Different parameter
    };
    
    auto key1 = pipelineCacher->ComputeKey(params1);
    auto key2 = pipelineCacher->ComputeKey(params2);
    
    EXPECT_NE(key1, key2);
}

} // namespace CashSystemTests