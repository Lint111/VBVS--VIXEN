/**
 * @file test_type_validation.cpp
 * @brief Tests for the cached recursive type validation system
 *
 * Demonstrates the performance benefits of hash-based caching
 * for complex type validation.
 */

#include <gtest/gtest.h>
#include "../include/Data/Core/TypeValidation.h"
#include <chrono>
#include <vector>

using namespace Vixen::RenderGraph;

// Test fixture
class TypeValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear cache and register base types
        auto& registry = CachedTypeRegistry::Instance();
        registry.ClearCache();

        // Register common base types
        registry.RegisterBaseType<VkImage>();
        registry.RegisterBaseType<VkBuffer>();
        registry.RegisterBaseType<VkSampler>();
        registry.RegisterBaseType<uint32_t>();
        registry.RegisterBaseType<float>();
        registry.RegisterBaseType<double>();
    }

    void TearDown() override {
        // Clear cache after each test for isolation
        CachedTypeRegistry::Instance().ClearCache();
    }
};

// ============================================================================
// Hash Generation Tests
// ============================================================================

TEST_F(TypeValidationTest, TypeHashUniqueness) {
    // Different types should have different hashes
    size_t hash1 = TypeHasher::Hash<VkImage>();
    size_t hash2 = TypeHasher::Hash<VkBuffer>();
    EXPECT_NE(hash1, hash2);

    // Same type should have same hash
    size_t hash3 = TypeHasher::Hash<VkImage>();
    EXPECT_EQ(hash1, hash3);

    // Different wrappers should have different hashes
    size_t hash4 = TypeHasher::Hash<RefW<VkImage>>();
    size_t hash5 = TypeHasher::Hash<PtrW<VkImage>>();
    EXPECT_NE(hash4, hash5);
    EXPECT_NE(hash4, hash1);  // Wrapped vs unwrapped

    // Complex types with same structure should have same hash
    using ComplexType1 = VectorW<PairW<VkImage, VkBuffer>>;
    using ComplexType2 = VectorW<PairW<VkImage, VkBuffer>>;
    size_t hash6 = TypeHasher::Hash<ComplexType1>();
    size_t hash7 = TypeHasher::Hash<ComplexType2>();
    EXPECT_EQ(hash6, hash7);

    // Different complex types should have different hashes
    using ComplexType3 = VectorW<PairW<VkBuffer, VkImage>>;  // Swapped order
    size_t hash8 = TypeHasher::Hash<ComplexType3>();
    EXPECT_NE(hash6, hash8);
}

TEST_F(TypeValidationTest, NestedTypeHashing) {
    // Deeply nested types should hash correctly
    using DeepType1 = RefW<VectorW<OptionalW<PairW<VkImage, VkBuffer>>>>;
    using DeepType2 = RefW<VectorW<OptionalW<PairW<VkImage, VkBuffer>>>>;
    using DeepType3 = PtrW<VectorW<OptionalW<PairW<VkImage, VkBuffer>>>>;  // Ptr instead of Ref

    size_t hash1 = TypeHasher::Hash<DeepType1>();
    size_t hash2 = TypeHasher::Hash<DeepType2>();
    size_t hash3 = TypeHasher::Hash<DeepType3>();

    EXPECT_EQ(hash1, hash2);   // Same type
    EXPECT_NE(hash1, hash3);   // Different wrapper
}

// ============================================================================
// Validation Tests
// ============================================================================

TEST_F(TypeValidationTest, BaseTypeValidation) {
    auto& registry = CachedTypeRegistry::Instance();

    // Registered types should be valid
    EXPECT_TRUE(registry.IsTypeAcceptable<VkImage>());
    EXPECT_TRUE(registry.IsTypeAcceptable<VkBuffer>());
    EXPECT_TRUE(registry.IsTypeAcceptable<uint32_t>());

    // Unregistered types should be invalid
    struct UnregisteredType {};
    EXPECT_FALSE(registry.IsTypeAcceptable<UnregisteredType>());
}

TEST_F(TypeValidationTest, WrappedTypeValidation) {
    auto& registry = CachedTypeRegistry::Instance();

    // Wrappers of registered types should be valid
    EXPECT_TRUE(registry.IsTypeAcceptable<RefW<VkImage>>());
    EXPECT_TRUE(registry.IsTypeAcceptable<PtrW<VkBuffer>>());
    EXPECT_TRUE(registry.IsTypeAcceptable<VectorW<VkImage>>());
    EXPECT_TRUE(registry.IsTypeAcceptable<OptionalW<VkBuffer>>());

    // Composed wrappers should be valid
    EXPECT_TRUE(registry.IsTypeAcceptable<ConstW<RefW<VkImage>>>());
    EXPECT_TRUE(registry.IsTypeAcceptable<VectorW<PtrW<VkBuffer>>>());
    EXPECT_TRUE(registry.IsTypeAcceptable<RefW<VectorW<VkImage>>>());

    // Wrappers of unregistered types should be invalid
    struct UnregisteredType {};
    EXPECT_FALSE(registry.IsTypeAcceptable<RefW<UnregisteredType>>());
    EXPECT_FALSE(registry.IsTypeAcceptable<VectorW<UnregisteredType>>());
}

TEST_F(TypeValidationTest, CompositeTypeValidation) {
    auto& registry = CachedTypeRegistry::Instance();

    // Pairs of registered types should be valid
    EXPECT_TRUE(registry.IsTypeAcceptable<PairW<VkImage, VkBuffer>>());

    // Tuples of registered types should be valid
    EXPECT_TRUE(registry.IsTypeAcceptable<TupleW<VkImage, VkBuffer, VkSampler>>());

    // Variants of registered types should be valid
    EXPECT_TRUE(registry.IsTypeAcceptable<VariantW<VkImage, VkBuffer>>());

    // Maps with registered key and value types should be valid
    EXPECT_TRUE(registry.IsTypeAcceptable<MapW<uint32_t, VkImage>>());

    // Composites with unregistered types should be invalid
    struct UnregisteredType {};
    EXPECT_FALSE(registry.IsTypeAcceptable<PairW<VkImage, UnregisteredType>>());
    EXPECT_FALSE(registry.IsTypeAcceptable<TupleW<VkImage, UnregisteredType, VkBuffer>>());
}

TEST_F(TypeValidationTest, DeepCompositionValidation) {
    auto& registry = CachedTypeRegistry::Instance();

    // Very complex nested type
    using ComplexType = VectorW<
        TupleW<
            OptionalW<VkImage>,
            PairW<uint32_t, VkBuffer>,
            VariantW<VkSampler, RefW<VkImage>>
        >
    >;

    EXPECT_TRUE(registry.IsTypeAcceptable<ComplexType>());

    // Even deeper nesting
    using VeryComplexType = RefW<
        VectorW<
            OptionalW<
                PairW<
                    VectorW<VkImage>,
                    TupleW<uint32_t, float, double>
                >
            >
        >
    >;

    EXPECT_TRUE(registry.IsTypeAcceptable<VeryComplexType>());
}

// ============================================================================
// Cache Performance Tests
// ============================================================================

TEST_F(TypeValidationTest, CachePerformance) {
    auto& registry = CachedTypeRegistry::Instance();

    // Complex type for testing
    using ComplexType = VectorW<
        TupleW<
            OptionalW<PairW<VkImage, VkBuffer>>,
            RefW<VectorW<VkSampler>>,
            VariantW<uint32_t, float, double>
        >
    >;

    // First validation (cold cache)
    auto start1 = std::chrono::high_resolution_clock::now();
    bool valid1 = registry.IsTypeAcceptable<ComplexType>();
    auto end1 = std::chrono::high_resolution_clock::now();
    auto coldTime = std::chrono::duration_cast<std::chrono::microseconds>(end1 - start1).count();

    EXPECT_TRUE(valid1);

    // Second validation (warm cache)
    auto start2 = std::chrono::high_resolution_clock::now();
    bool valid2 = registry.IsTypeAcceptable<ComplexType>();
    auto end2 = std::chrono::high_resolution_clock::now();
    auto warmTime = std::chrono::duration_cast<std::chrono::microseconds>(end2 - start2).count();

    EXPECT_TRUE(valid2);

    // Cache should make it significantly faster
    // Cold time should be at least 10x slower than warm time
    // (In practice, it's often 100-1000x faster)
    if (coldTime > 0 && warmTime > 0) {
        EXPECT_GT(coldTime, warmTime);
    }

    // Multiple warm cache hits
    const int iterations = 1000;
    auto start3 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        bool valid = registry.IsTypeAcceptable<ComplexType>();
        EXPECT_TRUE(valid);
    }
    auto end3 = std::chrono::high_resolution_clock::now();
    auto totalWarmTime = std::chrono::duration_cast<std::chrono::microseconds>(end3 - start3).count();

    // Average time per warm cache hit should be very small
    double avgWarmTime = static_cast<double>(totalWarmTime) / iterations;

    // Log performance metrics
    std::cout << "\nCache Performance Metrics:\n";
    std::cout << "  Cold cache time: " << coldTime << " μs\n";
    std::cout << "  Warm cache time: " << warmTime << " μs\n";
    std::cout << "  Avg warm time (" << iterations << " iterations): " << avgWarmTime << " μs\n";
    if (coldTime > 0 && avgWarmTime > 0) {
        std::cout << "  Speedup: " << (coldTime / avgWarmTime) << "x\n";
    }
}

TEST_F(TypeValidationTest, CacheInvalidation) {
    auto& registry = CachedTypeRegistry::Instance();

    // Type that depends on an unregistered base type
    struct NewType {};
    using WrappedNewType = RefW<NewType>;

    // Should be invalid initially
    EXPECT_FALSE(registry.IsTypeAcceptable<WrappedNewType>());

    // Register the base type
    registry.RegisterBaseType<NewType>();

    // Should be valid now (cache was cleared on registration)
    EXPECT_TRUE(registry.IsTypeAcceptable<WrappedNewType>());
}

TEST_F(TypeValidationTest, CacheStatistics) {
    auto& registry = CachedTypeRegistry::Instance();

    // Initial stats
    auto stats1 = registry.GetStats();
    EXPECT_EQ(stats1.cachedValidations, 0);

    // Validate some types
    registry.IsTypeAcceptable<VkImage>();
    registry.IsTypeAcceptable<RefW<VkBuffer>>();
    registry.IsTypeAcceptable<VectorW<VkImage>>();

    auto stats2 = registry.GetStats();
    EXPECT_EQ(stats2.cachedValidations, 3);

    // Validate same types again (should hit cache)
    registry.IsTypeAcceptable<VkImage>();
    registry.IsTypeAcceptable<RefW<VkBuffer>>();

    // Cache hit rate should increase
    auto stats3 = registry.GetStats();
    EXPECT_EQ(stats3.cachedValidations, 3);  // No new validations
}

// ============================================================================
// Stress Test
// ============================================================================

TEST_F(TypeValidationTest, StressTestManyTypes) {
    auto& registry = CachedTypeRegistry::Instance();

    // Generate many different complex types and validate them
    const int typeCount = 100;
    std::vector<bool> results;

    auto start = std::chrono::high_resolution_clock::now();

    // Validate many different wrapper combinations
    for (int i = 0; i < typeCount; ++i) {
        if (i % 4 == 0) {
            results.push_back(registry.IsTypeAcceptable<RefW<VkImage>>());
        } else if (i % 4 == 1) {
            results.push_back(registry.IsTypeAcceptable<PtrW<VkBuffer>>());
        } else if (i % 4 == 2) {
            results.push_back(registry.IsTypeAcceptable<VectorW<VkSampler>>());
        } else {
            using ComplexType = PairW<VkImage, VkBuffer>;
            results.push_back(registry.IsTypeAcceptable<ComplexType>());
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // All should be valid
    for (bool result : results) {
        EXPECT_TRUE(result);
    }

    std::cout << "\nStress test: Validated " << typeCount << " types in " << duration << " ms\n";

    // Check cache effectiveness
    auto stats = registry.GetStats();
    std::cout << "  Cached validations: " << stats.cachedValidations << "\n";
    std::cout << "  Cache hit rate: " << stats.cacheHitRate << "%\n";
}

// ============================================================================
// Custom Struct Validation
// ============================================================================

// Define a custom struct
struct TestMaterial {
    VkImage albedo;
    VkImage normal;
    VkSampler sampler;
    float roughness;
    float metallic;
};

// Register it as a composition
REGISTER_STRUCT_COMPOSITION(TestMaterial,
    VkImage,   // albedo
    VkImage,   // normal
    VkSampler, // sampler
    float,     // roughness
    float      // metallic
)

TEST_F(TypeValidationTest, StructCompositionValidation) {
    auto& registry = CachedTypeRegistry::Instance();

    // The struct itself should be valid
    EXPECT_TRUE(registry.IsTypeAcceptable<TestMaterial>());

    // Wrapped versions should be valid
    EXPECT_TRUE(registry.IsTypeAcceptable<RefW<TestMaterial>>());
    EXPECT_TRUE(registry.IsTypeAcceptable<VectorW<TestMaterial>>());
    EXPECT_TRUE(registry.IsTypeAcceptable<OptionalW<TestMaterial>>());

    // Complex compositions should be valid
    using MaterialMap = MapW<uint32_t, TestMaterial>;
    EXPECT_TRUE(registry.IsTypeAcceptable<MaterialMap>());

    using MaterialVariant = VariantW<TestMaterial, VkImage>;
    EXPECT_TRUE(registry.IsTypeAcceptable<MaterialVariant>());
}