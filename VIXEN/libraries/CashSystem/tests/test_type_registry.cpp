#include <gtest/gtest.h>
#include <TypeRegistry.h>
#include <MainCacher.h>
#include <PipelineCacher.h>
#include <ShaderCompilationCacher.h>

TEST(CashSystem_TypeRegistry, RegistrationAndLookup) {
    TypeRegistry registry;
    
    // Test registration returns valid ID
    auto pipelineId = registry.RegisterType<PipelineCacher>("PipelineCacher");
    EXPECT_TRUE(pipelineId.has_value());
    EXPECT_NE(pipelineId.value(), 0); // Assuming non-zero IDs
    
    // Test lookup by type
    auto lookupId = registry.GetTypeId<PipelineCacher>();
    EXPECT_EQ(lookupId, pipelineId);
    
    // Test lookup by name
    auto nameId = registry.GetTypeIdByName("PipelineCacher");
    EXPECT_EQ(nameId, pipelineId);
}

TEST(CashSystem_TypeRegistry, MultipleTypes) {
    TypeRegistry registry;
    
    auto pipelineId = registry.RegisterType<PipelineCacher>("PipelineCacher");
    auto shaderId = registry.RegisterType<ShaderCompilationCacher>("ShaderCompilationCacher");
    
    EXPECT_TRUE(pipelineId.has_value());
    EXPECT_TRUE(shaderId.has_value());
    EXPECT_NE(pipelineId.value(), shaderId.value());
    
    // Both lookups should work
    EXPECT_EQ(registry.GetTypeId<PipelineCacher>(), pipelineId);
    EXPECT_EQ(registry.GetTypeId<ShaderCompilationCacher>(), shaderId);
}

TEST(CashSystem_TypeRegistry, DuplicateRegistration) {
    TypeRegistry registry;
    
    // Register same type multiple times
    auto id1 = registry.RegisterType<PipelineCacher>("PipelineCacher");
    auto id2 = registry.RegisterType<PipelineCacher>("PipelineCacher");
    auto id3 = registry.RegisterType<PipelineCacher>("PipelineCacher");
    
    // All should return same ID
    EXPECT_EQ(id1, id2);
    EXPECT_EQ(id2, id3);
    EXPECT_EQ(id1, id3);
}

TEST(CashSystem_TypeRegistry, UnregisteredTypeLookup) {
    TypeRegistry registry;
    
    // Try to lookup unregistered type
    auto unknownId = registry.GetTypeId<MainCacher>();
    EXPECT_FALSE(unknownId.has_value());
    
    // Try to lookup by unknown name
    auto unknownNameId = registry.GetTypeIdByName("UnknownType");
    EXPECT_FALSE(unknownNameId.has_value());
}

TEST(CashSystem_TypeRegistry, MainCacherIntegration) {
    auto mainCacher = std::make_unique<MainCacher>();
    
    // Verify internal registry is working
    EXPECT_EQ(mainCacher->GetRegisteredTypes().size(), 0);
    
    // Register through MainCacher
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    EXPECT_EQ(mainCacher->GetRegisteredTypes().size(), 1);
    
    mainCacher->RegisterType<ShaderCompilationCacher>("ShaderCompilationCacher");
    EXPECT_EQ(mainCacher->GetRegisteredTypes().size(), 2);
    
    // Verify types are registered
    EXPECT_TRUE(mainCacher->IsTypeRegistered<PipelineCacher>());
    EXPECT_TRUE(mainCacher->IsTypeRegistered<ShaderCompilationCacher>());
    EXPECT_FALSE(mainCacher->IsTypeRegistered<MainCacher>());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}