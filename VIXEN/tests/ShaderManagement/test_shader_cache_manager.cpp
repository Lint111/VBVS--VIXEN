/**
 * @file test_shader_cache_manager.cpp
 * @brief Unit tests for ShaderCacheManager class
 */

#include <gtest/gtest.h>
#include <ShaderManagement/ShaderCacheManager.h>
#include <filesystem>
#include <vector>
#include <thread>
#include <chrono>

using namespace ShaderManagement;

// ============================================================================
// Test Fixture
// ============================================================================

class ShaderCacheManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary test cache directory
        testCacheDir = std::filesystem::temp_directory_path() / "shader_cache_test";
        std::filesystem::remove_all(testCacheDir);
        std::filesystem::create_directories(testCacheDir);

        config.cacheDirectory = testCacheDir;
        config.enabled = true;
        config.validateCache = true;
        config.maxCacheSizeMB = 10;  // 10 MB limit for tests
    }

    void TearDown() override {
        // Clean up test cache directory
        std::filesystem::remove_all(testCacheDir);
    }

    // Helper function to create dummy SPIR-V data
    std::vector<uint32_t> CreateDummySpirv(size_t size = 100) {
        std::vector<uint32_t> spirv(size);
        for (size_t i = 0; i < size; ++i) {
            spirv[i] = static_cast<uint32_t>(i);
        }
        return spirv;
    }

    std::filesystem::path testCacheDir;
    ShaderCacheConfig config;
};

// ============================================================================
// Construction and Configuration Tests
// ============================================================================

TEST_F(ShaderCacheManagerTest, ConstructionWithDefaultConfig) {
    ShaderCacheManager cache;
    EXPECT_TRUE(cache.IsEnabled());
}

TEST_F(ShaderCacheManagerTest, ConstructionWithCustomConfig) {
    ShaderCacheManager cache(config);
    EXPECT_TRUE(cache.IsEnabled());
    EXPECT_EQ(cache.GetCacheDirectory(), testCacheDir);
}

TEST_F(ShaderCacheManagerTest, EnableDisableCache) {
    ShaderCacheManager cache(config);

    EXPECT_TRUE(cache.IsEnabled());

    cache.SetEnabled(false);
    EXPECT_FALSE(cache.IsEnabled());

    cache.SetEnabled(true);
    EXPECT_TRUE(cache.IsEnabled());
}

TEST_F(ShaderCacheManagerTest, SetMaxCacheSize) {
    ShaderCacheManager cache(config);

    cache.SetMaxCacheSize(100);
    // Size is updated internally - no direct getter, but affects eviction
    SUCCEED();
}

// ============================================================================
// Basic Cache Operations
// ============================================================================

TEST_F(ShaderCacheManagerTest, StoreAndLookup) {
    ShaderCacheManager cache(config);

    std::string key = "test_shader_key_001";
    auto spirv = CreateDummySpirv(50);

    // Store
    EXPECT_TRUE(cache.Store(key, spirv));

    // Lookup
    auto retrieved = cache.Lookup(key);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->size(), spirv.size());
    EXPECT_EQ(*retrieved, spirv);
}

TEST_F(ShaderCacheManagerTest, LookupNonExistent) {
    ShaderCacheManager cache(config);

    auto result = cache.Lookup("nonexistent_key");
    EXPECT_FALSE(result.has_value());
}

TEST_F(ShaderCacheManagerTest, ContainsCheck) {
    ShaderCacheManager cache(config);

    std::string key = "test_key";
    auto spirv = CreateDummySpirv();

    EXPECT_FALSE(cache.Contains(key));

    cache.Store(key, spirv);
    EXPECT_TRUE(cache.Contains(key));
}

TEST_F(ShaderCacheManagerTest, StoreOverwrite) {
    ShaderCacheManager cache(config);

    std::string key = "overwrite_test";
    auto spirv1 = CreateDummySpirv(10);
    auto spirv2 = CreateDummySpirv(20);

    cache.Store(key, spirv1);
    cache.Store(key, spirv2);  // Overwrite

    auto retrieved = cache.Lookup(key);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->size(), 20);
}

// ============================================================================
// Cache Management Tests
// ============================================================================

TEST_F(ShaderCacheManagerTest, Remove) {
    ShaderCacheManager cache(config);

    std::string key = "remove_test";
    auto spirv = CreateDummySpirv();

    cache.Store(key, spirv);
    EXPECT_TRUE(cache.Contains(key));

    EXPECT_TRUE(cache.Remove(key));
    EXPECT_FALSE(cache.Contains(key));

    // Removing again should return false
    EXPECT_FALSE(cache.Remove(key));
}

TEST_F(ShaderCacheManagerTest, Clear) {
    ShaderCacheManager cache(config);

    // Store multiple entries
    for (int i = 0; i < 5; ++i) {
        std::string key = "key_" + std::to_string(i);
        cache.Store(key, CreateDummySpirv());
    }

    auto stats = cache.GetStatistics();
    EXPECT_GT(stats.cachedShaderCount, 0);

    cache.Clear();

    stats = cache.GetStatistics();
    EXPECT_EQ(stats.cachedShaderCount, 0);

    // Verify entries are actually gone
    EXPECT_FALSE(cache.Contains("key_0"));
    EXPECT_FALSE(cache.Contains("key_4"));
}

TEST_F(ShaderCacheManagerTest, ValidateCache) {
    ShaderCacheManager cache(config);

    // Store some valid entries
    cache.Store("valid1", CreateDummySpirv());
    cache.Store("valid2", CreateDummySpirv());

    // Validation should find no corrupted entries
    uint32_t removed = cache.ValidateCache();
    EXPECT_EQ(removed, 0);
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(ShaderCacheManagerTest, StatisticsHitMiss) {
    ShaderCacheManager cache(config);

    std::string key = "stats_test";
    auto spirv = CreateDummySpirv();

    // Initial stats
    auto stats = cache.GetStatistics();
    size_t initialHits = stats.totalCacheHits;
    size_t initialMisses = stats.totalCacheMisses;

    // Miss
    cache.Lookup("nonexistent");
    stats = cache.GetStatistics();
    EXPECT_EQ(stats.totalCacheMisses, initialMisses + 1);

    // Store and hit
    cache.Store(key, spirv);
    cache.Lookup(key);
    stats = cache.GetStatistics();
    EXPECT_EQ(stats.totalCacheHits, initialHits + 1);
}

TEST_F(ShaderCacheManagerTest, StatisticsBytesTracking) {
    ShaderCacheManager cache(config);

    auto stats1 = cache.GetStatistics();
    size_t initialBytes = stats1.totalBytesWritten;

    auto spirv = CreateDummySpirv(100);
    cache.Store("bytes_test", spirv);

    auto stats2 = cache.GetStatistics();
    EXPECT_GT(stats2.totalBytesWritten, initialBytes);
}

TEST_F(ShaderCacheManagerTest, ResetStatistics) {
    ShaderCacheManager cache(config);

    // Generate some statistics
    cache.Store("key1", CreateDummySpirv());
    cache.Lookup("key1");
    cache.Lookup("nonexistent");

    auto stats1 = cache.GetStatistics();
    EXPECT_GT(stats1.totalCacheHits, 0);
    EXPECT_GT(stats1.totalCacheMisses, 0);

    cache.ResetStatistics();

    auto stats2 = cache.GetStatistics();
    EXPECT_EQ(stats2.totalCacheHits, 0);
    EXPECT_EQ(stats2.totalCacheMisses, 0);
}

TEST_F(ShaderCacheManagerTest, HitRateCalculation) {
    ShaderCacheManager cache(config);

    std::string key = "hitrate_test";
    cache.Store(key, CreateDummySpirv());

    // 3 hits, 1 miss = 75% hit rate
    cache.Lookup(key);
    cache.Lookup(key);
    cache.Lookup(key);
    cache.Lookup("nonexistent");

    auto stats = cache.GetStatistics();
    float hitRate = stats.GetHitRate();
    EXPECT_FLOAT_EQ(hitRate, 0.75f);
}

// ============================================================================
// Disabled Cache Tests
// ============================================================================

TEST_F(ShaderCacheManagerTest, DisabledCacheNoOps) {
    ShaderCacheManager cache(config);
    cache.SetEnabled(false);

    std::string key = "disabled_test";
    auto spirv = CreateDummySpirv();

    // Store should be no-op
    cache.Store(key, spirv);

    // Lookup should return nullopt
    auto result = cache.Lookup(key);
    EXPECT_FALSE(result.has_value());

    // Contains should return false
    EXPECT_FALSE(cache.Contains(key));
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(ShaderCacheManagerTest, ConcurrentStoreAndLookup) {
    ShaderCacheManager cache(config);

    constexpr int numThreads = 4;
    constexpr int entriesPerThread = 10;

    std::vector<std::thread> threads;

    // Spawn threads that store entries
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&cache, t]() {
            for (int i = 0; i < entriesPerThread; ++i) {
                std::string key = "thread_" + std::to_string(t) + "_key_" + std::to_string(i);
                auto spirv = std::vector<uint32_t>(100, t * 1000 + i);
                cache.Store(key, spirv);
            }
        });
    }

    // Wait for all stores to complete
    for (auto& thread : threads) {
        thread.join();
    }

    threads.clear();

    // Spawn threads that lookup entries
    std::atomic<int> successCount{0};
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&cache, &successCount, t]() {
            for (int i = 0; i < entriesPerThread; ++i) {
                std::string key = "thread_" + std::to_string(t) + "_key_" + std::to_string(i);
                auto result = cache.Lookup(key);
                if (result.has_value()) {
                    successCount++;
                }
            }
        });
    }

    // Wait for all lookups to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // All entries should have been found
    EXPECT_EQ(successCount.load(), numThreads * entriesPerThread);
}

// ============================================================================
// Cache Key Generation Tests
// ============================================================================

TEST(GenerateCacheKeyTest, DifferentSourcesProduceDifferentKeys) {
    std::vector<std::pair<std::string, std::string>> noDefines;

    std::string key1 = GenerateCacheKey("source1", "", 0, noDefines, "main");
    std::string key2 = GenerateCacheKey("source2", "", 0, noDefines, "main");

    EXPECT_NE(key1, key2);
}

TEST(GenerateCacheKeyTest, DifferentStagesProduceDifferentKeys) {
    std::vector<std::pair<std::string, std::string>> noDefines;
    std::string source = "same source";

    std::string key1 = GenerateCacheKey(source, "", 0, noDefines, "main");
    std::string key2 = GenerateCacheKey(source, "", 1, noDefines, "main");

    EXPECT_NE(key1, key2);
}

TEST(GenerateCacheKeyTest, DifferentDefinesProduceDifferentKeys) {
    std::string source = "same source";
    std::vector<std::pair<std::string, std::string>> defines1 = {{"A", "1"}};
    std::vector<std::pair<std::string, std::string>> defines2 = {{"A", "2"}};

    std::string key1 = GenerateCacheKey(source, "", 0, defines1, "main");
    std::string key2 = GenerateCacheKey(source, "", 0, defines2, "main");

    EXPECT_NE(key1, key2);
}

TEST(GenerateCacheKeyTest, DifferentEntryPointsProduceDifferentKeys) {
    std::vector<std::pair<std::string, std::string>> noDefines;
    std::string source = "same source";

    std::string key1 = GenerateCacheKey(source, "", 0, noDefines, "main");
    std::string key2 = GenerateCacheKey(source, "", 0, noDefines, "custom_main");

    EXPECT_NE(key1, key2);
}

TEST(GenerateCacheKeyTest, SameInputsProduceSameKey) {
    std::vector<std::pair<std::string, std::string>> defines = {{"A", "1"}, {"B", "2"}};
    std::string source = "consistent source";

    std::string key1 = GenerateCacheKey(source, "", 0, defines, "main");
    std::string key2 = GenerateCacheKey(source, "", 0, defines, "main");

    EXPECT_EQ(key1, key2);
}
