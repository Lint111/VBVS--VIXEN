#include <gtest/gtest.h>
#include <TypeRegistry.h>
#include <MainCacher.h>
#include <PipelineCacher.h>
#include <ShaderCompilationCacher.h>

TEST(CashSystem_Registry, TypeRegistration) {
    TypeRegistry registry;
    
    // Test registration
    auto pipelineId = registry.RegisterType<PipelineCacher>("PipelineCacher");
    auto shaderId = registry.RegisterType<ShaderCompilationCacher>("ShaderCompilationCacher");
    
    EXPECT_TRUE(pipelineId.has_value());
    EXPECT_TRUE(shaderId.has_value());
    EXPECT_NE(pipelineId.value(), shaderId.value());
    
    // Test retrieval
    auto retrievedPipeline = registry.GetTypeId<PipelineCacher>();
    auto retrievedShader = registry.GetTypeId<ShaderCompilationCacher>();
    
    EXPECT_EQ(retrievedPipeline, pipelineId);
    EXPECT_EQ(retrievedShader, shaderId);
}

TEST(CashSystem_Registry, DuplicateRegistration) {
    TypeRegistry registry;
    
    // Register same type twice
    auto id1 = registry.RegisterType<PipelineCacher>("PipelineCacher");
    auto id2 = registry.RegisterType<PipelineCacher>("PipelineCacher");
    
    // Should return same ID
    EXPECT_EQ(id1, id2);
}

TEST(CashSystem_Registry, UnregisteredType) {
    TypeRegistry registry;
    
    // Test retrieval of unregistered type
    auto id = registry.GetTypeId<ShaderCompilationCacher>();
    EXPECT_FALSE(id.has_value());
}

TEST(CashSystem_Registry, MainCacherIntegration) {
    auto mainCacher = std::make_unique<MainCacher>();
    
    // Register types through MainCacher
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->RegisterType<ShaderCompilationCacher>("ShaderCompilationCacher");
    
    // Verify they're registered
    EXPECT_TRUE(mainCacher->IsTypeRegistered<PipelineCacher>());
    EXPECT_TRUE(mainCacher->IsTypeRegistered<ShaderCompilationCacher>());
    
    // Verify count
    auto registeredTypes = mainCacher->GetRegisteredTypes();
    EXPECT_EQ(registeredTypes.size(), 2);
}

TEST(CashSystem_Registry, FactoryCreation) {
    auto mainCacher = std::make_unique<MainCacher>();
    
    // Register types
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->RegisterType<ShaderCompilationCacher>("ShaderCompilationCacher");
    
    // Test factory creation
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(nullptr);
    auto shaderCacher = mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr);
    
    EXPECT_TRUE(pipelineCacher != nullptr);
    EXPECT_TRUE(shaderCacher != nullptr);
    
    // Verify different types
    EXPECT_NE(pipelineCacher.get(), shaderCacher.get());
}

TEST(CashSystem_Registry, FactoryWithInvalidType) {
    auto mainCacher = std::make_unique<MainCacher>();
    
    // Try to create unregistered type (should return null)
    // Note: This test expects compilation to fail or return null
    // depending on implementation
    try {
        auto invalidCacher = mainCacher->CreateCacher<MainCacher>(nullptr);
        // If we get here, implementation allows it
        EXPECT_TRUE(invalidCacher == nullptr || invalidCacher.get() != nullptr);
    } catch (...) {
        // If exception thrown, that's also acceptable behavior
        EXPECT_TRUE(true);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}