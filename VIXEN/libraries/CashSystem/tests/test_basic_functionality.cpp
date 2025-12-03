#include <gtest/gtest.h>
#include <MainCacher.h>
#include <PipelineCacher.h>
#include <ShaderCompilationCacher.h>
#include "logger/Logger.h"

TEST(CashSystem_BasicFunctionality, CacheStoreAndRetrieve) {
    auto mainCacher = std::make_unique<MainCacher>();
    
    // Register a type
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    
    // Create cacher
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(nullptr);
    ASSERT_TRUE(pipelineCacher != nullptr);
    
    // Test store and retrieve
    std::string key = "test_pipeline_key";
    std::string value = "test_pipeline_value";
    
    bool stored = pipelineCacher->Cache(key, value);
    EXPECT_TRUE(stored);
    
    auto retrieved = pipelineCacher->GetCached(key);
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved.value(), value);
}

TEST(CashSystem_BasicFunctionality, CacheMiss) {
    auto mainCacher = std::make_unique<MainCacher>();
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(nullptr);
    
    // Test cache miss
    auto miss = pipelineCacher->GetCached("nonexistent_key");
    EXPECT_FALSE(miss.has_value());
}

TEST(CashSystem_BasicFunctionality, MultipleCachers) {
    auto mainCacher = std::make_unique<MainCacher>();
    
    // Register multiple types
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    mainCacher->RegisterType<ShaderCompilationCacher>("ShaderCompilationCacher");
    
    // Create multiple cachers
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(nullptr);
    auto shaderCacher = mainCacher->CreateCacher<ShaderCompilationCacher>(nullptr);
    
    EXPECT_TRUE(pipelineCacher != nullptr);
    EXPECT_TRUE(shaderCacher != nullptr);
    
    // Store different data in each
    bool stored1 = pipelineCacher->Cache("pipe1", "value1");
    bool stored2 = shaderCacher->Cache("shader1", "value2");
    
    EXPECT_TRUE(stored1);
    EXPECT_TRUE(stored2);
    
    // Verify cross-contamination doesn't happen
    auto miss1 = pipelineCacher->GetCached("shader1");
    auto miss2 = shaderCacher->GetCached("pipe1");
    
    EXPECT_FALSE(miss1.has_value());
    EXPECT_FALSE(miss2.has_value());
}

TEST(CashSystem_BasicFunctionality, OverwriteCache) {
    auto mainCacher = std::make_unique<MainCacher>();
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(nullptr);
    
    std::string key = "same_key";
    
    // Store first value
    bool stored1 = pipelineCacher->Cache(key, "first_value");
    EXPECT_TRUE(stored1);
    
    // Overwrite with second value
    bool stored2 = pipelineCacher->Cache(key, "second_value");
    EXPECT_TRUE(stored2);
    
    // Should get second value
    auto retrieved = pipelineCacher->GetCached(key);
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved.value(), "second_value");
}

TEST(CashSystem_BasicFunctionality, EmptyCache) {
    auto mainCacher = std::make_unique<MainCacher>();
    mainCacher->RegisterType<PipelineCacher>("PipelineCacher");
    
    auto pipelineCacher = mainCacher->CreateCacher<PipelineCacher>(nullptr);
    
    // Test empty cache operations
    auto empty1 = pipelineCacher->GetCached("any_key");
    EXPECT_FALSE(empty1.has_value());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}