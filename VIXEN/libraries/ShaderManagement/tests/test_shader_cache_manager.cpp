#include <gtest/gtest.h>
#include "ShaderCacheManager.h"
#include "ShaderCompiler.h"
#include <filesystem>
#include <fstream>

using namespace ShaderManagement;

// Test fixture for shader cache manager tests
class ShaderCacheManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        testCacheDir = std::filesystem::temp_directory_path() / "shader_cache_test";

        // Clean up any existing test cache
        if (std::filesystem::exists(testCacheDir)) {
            std::filesystem::remove_all(testCacheDir);
        }

        std::filesystem::create_directories(testCacheDir);

        ShaderCacheConfig config;
        config.cacheDirectory = testCacheDir;

        cacheManager = std::make_unique<ShaderCacheManager>(config);
    }

    void TearDown() override {
        cacheManager.reset();

        if (std::filesystem::exists(testCacheDir)) {
            std::filesystem::remove_all(testCacheDir);
        }
    }

    std::filesystem::path testCacheDir;
    std::unique_ptr<ShaderCacheManager> cacheManager;

    // Helper: Compile a simple shader
    std::vector<uint32_t> CompileTestShader() {
        std::string source = R"(
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    fragColor = inColor;
}
        )";

        ShaderCompiler compiler;
        auto result = compiler.Compile(ShaderStage::Vertex, source);

        if (!result.success) {
            return {};
        }

        return result.spirv;
    }
};

// ===== Cache Storage Tests =====

TEST_F(ShaderCacheManagerTest, StoreAndRetrieveShader) {
    auto spirv = CompileTestShader();
    ASSERT_FALSE(spirv.empty());

    std::string cacheKey = "test_shader_v1";

    // Store shader
    bool stored = cacheManager->Store(cacheKey, spirv);
    EXPECT_TRUE(stored);

    // Retrieve shader
    auto retrieved = cacheManager->Lookup(cacheKey);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved.value(), spirv);
}

TEST_F(ShaderCacheManagerTest, RetrieveNonExistentShader) {
    auto retrieved = cacheManager->Lookup("does_not_exist");
    EXPECT_FALSE(retrieved.has_value());
}

TEST_F(ShaderCacheManagerTest, OverwriteExistingCache) {
    auto spirv1 = CompileTestShader();
    ASSERT_FALSE(spirv1.empty());

    std::string cacheKey = "test_shader";

    // Store first version
    cacheManager->Store(cacheKey, spirv1);

    // Create different SPIR-V (modify first word after header)
    auto spirv2 = spirv1;
    if (spirv2.size() > 10) {
        spirv2[10] = spirv2[10] + 1; // Modify to make it different
    }

    // Overwrite with second version
    bool stored = cacheManager->Store(cacheKey, spirv2);
    EXPECT_TRUE(stored);

    // Should retrieve the second version
    auto retrieved = cacheManager->Lookup(cacheKey);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved.value(), spirv2);
}

// ===== Cache Key Tests =====

TEST_F(ShaderCacheManagerTest, DifferentKeysStoreSeparately) {
    auto spirv = CompileTestShader();
    ASSERT_FALSE(spirv.empty());

    cacheManager->Store("shader_a", spirv);
    cacheManager->Store("shader_b", spirv);

    auto retrievedA = cacheManager->Lookup("shader_a");
    auto retrievedB = cacheManager->Lookup("shader_b");

    EXPECT_TRUE(retrievedA.has_value());
    EXPECT_TRUE(retrievedB.has_value());
}

TEST_F(ShaderCacheManagerTest, EmptyKeyShouldFail) {
    auto spirv = CompileTestShader();
    ASSERT_FALSE(spirv.empty());

    bool stored = cacheManager->Store("", spirv);
    EXPECT_FALSE(stored);
}

// ===== Cache Existence Tests =====

TEST_F(ShaderCacheManagerTest, HasCacheEntryReturnsTrue) {
    auto spirv = CompileTestShader();
    ASSERT_FALSE(spirv.empty());

    std::string cacheKey = "test_shader";
    cacheManager->Store(cacheKey, spirv);

    EXPECT_TRUE(cacheManager->Contains(cacheKey));
}

TEST_F(ShaderCacheManagerTest, HasCacheEntryReturnsFalse) {
    EXPECT_FALSE(cacheManager->Contains("does_not_exist"));
}

// ===== Cache Invalidation Tests =====

TEST_F(ShaderCacheManagerTest, InvalidateRemovesCache) {
    auto spirv = CompileTestShader();
    ASSERT_FALSE(spirv.empty());

    std::string cacheKey = "test_shader";
    cacheManager->Store(cacheKey, spirv);

    EXPECT_TRUE(cacheManager->Contains(cacheKey));

    // Invalidate
    cacheManager->Remove(cacheKey);

    EXPECT_FALSE(cacheManager->Contains(cacheKey));
    EXPECT_FALSE(cacheManager->Lookup(cacheKey).has_value());
}

TEST_F(ShaderCacheManagerTest, InvalidateNonExistentKeyDoesNotCrash) {
    // Should not crash or throw
    cacheManager->Remove("does_not_exist");
}

TEST_F(ShaderCacheManagerTest, ClearRemovesAllCaches) {
    auto spirv = CompileTestShader();
    ASSERT_FALSE(spirv.empty());

    cacheManager->Store("shader_a", spirv);
    cacheManager->Store("shader_b", spirv);
    cacheManager->Store("shader_c", spirv);

    EXPECT_TRUE(cacheManager->Contains("shader_a"));
    EXPECT_TRUE(cacheManager->Contains("shader_b"));
    EXPECT_TRUE(cacheManager->Contains("shader_c"));

    // Clear all
    cacheManager->Clear();

    EXPECT_FALSE(cacheManager->Contains("shader_a"));
    EXPECT_FALSE(cacheManager->Contains("shader_b"));
    EXPECT_FALSE(cacheManager->Contains("shader_c"));
}

// ===== Disk Persistence Tests =====

TEST_F(ShaderCacheManagerTest, CachePersistsAcrossInstances) {
    auto spirv = CompileTestShader();
    ASSERT_FALSE(spirv.empty());

    std::string cacheKey = "persistent_shader";

    ShaderCacheConfig config;
    config.cacheDirectory = testCacheDir;

    {
        // First instance - store shader
        ShaderCacheManager cache1(config);
        cache1.Store(cacheKey, spirv);
    }

    {
        // Second instance - should retrieve from disk
        ShaderCacheManager cache2(config);
        auto retrieved = cache2.Lookup(cacheKey);
        ASSERT_TRUE(retrieved.has_value());
        EXPECT_EQ(retrieved.value(), spirv);
    }
}

// ===== Error Handling Tests =====

TEST_F(ShaderCacheManagerTest, StoreEmptySpirvShouldFail) {
    std::vector<uint32_t> emptySpirv;
    bool stored = cacheManager->Store("test", emptySpirv);
    EXPECT_FALSE(stored);
}

TEST_F(ShaderCacheManagerTest, InvalidCacheDirectoryHandling) {
    // Test with a path that can't be created (root of system drive on Windows)
    std::filesystem::path invalidPath = "Z:/this/path/should/not/exist/shader_cache";

    ShaderCacheConfig config;
    config.cacheDirectory = invalidPath;

    // Constructor should not crash even with invalid path
    ShaderCacheManager invalidCache(config);

    auto spirv = CompileTestShader();
    ASSERT_FALSE(spirv.empty());

    // Store might fail, but shouldn't crash
    invalidCache.Store("test", spirv);
}

// ===== Performance Tests =====

TEST_F(ShaderCacheManagerTest, StoreMultipleShadersQuickly) {
    auto spirv = CompileTestShader();
    ASSERT_FALSE(spirv.empty());

    const int numShaders = 100;

    for (int i = 0; i < numShaders; ++i) {
        std::string key = "shader_" + std::to_string(i);
        bool stored = cacheManager->Store(key, spirv);
        EXPECT_TRUE(stored);
    }

    // Verify all were stored
    for (int i = 0; i < numShaders; ++i) {
        std::string key = "shader_" + std::to_string(i);
        EXPECT_TRUE(cacheManager->Contains(key));
    }
}

// ===== File System Tests =====

TEST_F(ShaderCacheManagerTest, CacheFilesCreatedOnDisk) {
    auto spirv = CompileTestShader();
    ASSERT_FALSE(spirv.empty());

    cacheManager->Store("test_shader", spirv);

    // Check that cache directory contains at least one file
    bool hasFiles = false;
    for (const auto& entry : std::filesystem::directory_iterator(testCacheDir)) {
        if (entry.is_regular_file()) {
            hasFiles = true;
            break;
        }
    }

    EXPECT_TRUE(hasFiles);
}
