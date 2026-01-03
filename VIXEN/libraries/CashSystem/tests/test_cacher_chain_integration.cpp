/**
 * @file test_cacher_chain_integration.cpp
 * @brief Sprint 5 Phase 5.3: Integration tests for cacher chain
 *
 * Tests the data flow between cachers:
 * - VoxelAABBData → AccelerationStructure dependency
 * - Cache key generation and propagation
 * - Resource cleanup chain
 * - Hot-reload simulation (cache invalidation)
 * - Scene change handling
 *
 * These are CPU-only tests - they verify the caching logic
 * without requiring a real Vulkan device.
 */

#include <gtest/gtest.h>
#include "AccelerationStructureCacher.h"
#include "VoxelAABBCacher.h"
#include "TLASInstanceManager.h"
#include "CacheKeyHasher.h"
#include "Memory/IMemoryAllocator.h"

#include <memory>
#include <vector>
#include <unordered_set>

using namespace CashSystem;

// ============================================================================
// Mock Data Factories
// ============================================================================

/**
 * @brief Creates mock VoxelAABBData for testing
 *
 * Creates a VoxelAABBData with specified parameters but no real GPU resources.
 */
class MockVoxelAABBDataFactory {
public:
    static VoxelAABBData Create(uint32_t aabbCount, uint32_t resolution = 64) {
        VoxelAABBData data;
        data.aabbCount = aabbCount;
        data.gridResolution = resolution;
        data.voxelSize = 1.0f / resolution;

        // Simulate valid buffer handles (for cache key computation)
        data.aabbAllocation.buffer = reinterpret_cast<VkBuffer>(++nextHandle_);
        data.aabbAllocation.size = aabbCount * sizeof(VoxelAABB);
        data.materialIdAllocation.buffer = reinterpret_cast<VkBuffer>(++nextHandle_);
        data.brickMappingAllocation.buffer = reinterpret_cast<VkBuffer>(++nextHandle_);

        return data;
    }

    static void Reset() { nextHandle_ = 0; }

private:
    static inline uint64_t nextHandle_ = 0;
};

/**
 * @brief Creates mock AccelerationStructureData for testing
 */
class MockAccelStructFactory {
public:
    static CachedAccelerationStructure CreateFromAABBData(
        const VoxelAABBData& aabbData,
        ASBuildMode buildMode = ASBuildMode::Static
    ) {
        CachedAccelerationStructure cached;
        cached.sourceAABBCount = aabbData.aabbCount;
        cached.buildMode = buildMode;

        // Simulate valid handles
        cached.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(++nextHandle_);
        if (buildMode == ASBuildMode::Static) {
            cached.accelStruct.tlas = reinterpret_cast<VkAccelerationStructureKHR>(++nextHandle_);
        }

        // For dynamic mode, create instance manager
        if (buildMode == ASBuildMode::Dynamic) {
            cached.instanceManager = std::make_unique<TLASInstanceManager>();
        }

        return cached;
    }

    static void Reset() { nextHandle_ = 0; }

private:
    static inline uint64_t nextHandle_ = 0;
};

// ============================================================================
// Cache Key Tests
// ============================================================================

class CacheKeyTest : public ::testing::Test {
protected:
    void SetUp() override {
        MockVoxelAABBDataFactory::Reset();
        MockAccelStructFactory::Reset();
    }
};

TEST_F(CacheKeyTest, AABBCreateInfoHashDiffers) {
    // Create two different VoxelAABBData
    auto aabb1 = MockVoxelAABBDataFactory::Create(100);
    auto aabb2 = MockVoxelAABBDataFactory::Create(200);

    // Create AccelStructCreateInfo for each
    AccelStructCreateInfo ci1, ci2;
    ci1.aabbData = &aabb1;
    ci2.aabbData = &aabb2;

    // Hash should differ because aabbData pointers differ
    auto hash1 = ci1.ComputeHash();
    auto hash2 = ci2.ComputeHash();

    EXPECT_NE(hash1, hash2);
}

TEST_F(CacheKeyTest, SameAABBDataSameHash) {
    auto aabbData = MockVoxelAABBDataFactory::Create(100);

    AccelStructCreateInfo ci1, ci2;
    ci1.aabbData = &aabbData;
    ci2.aabbData = &aabbData;

    // Same AABB data should produce same hash
    EXPECT_EQ(ci1.ComputeHash(), ci2.ComputeHash());
}

TEST_F(CacheKeyTest, BuildFlagsAffectHash) {
    auto aabbData = MockVoxelAABBDataFactory::Create(100);

    AccelStructCreateInfo ci1, ci2;
    ci1.aabbData = &aabbData;
    ci2.aabbData = &aabbData;

    ci1.preferFastTrace = true;
    ci2.preferFastTrace = false;

    // Different build flags should produce different hash
    EXPECT_NE(ci1.ComputeHash(), ci2.ComputeHash());
}

TEST_F(CacheKeyTest, BuildModeAffectsHash) {
    auto aabbData = MockVoxelAABBDataFactory::Create(100);

    AccelStructCreateInfo ci1, ci2;
    ci1.aabbData = &aabbData;
    ci2.aabbData = &aabbData;

    ci1.buildMode = ASBuildMode::Static;
    ci2.buildMode = ASBuildMode::Dynamic;

    // Different build mode should produce different hash
    EXPECT_NE(ci1.ComputeHash(), ci2.ComputeHash());
}

TEST_F(CacheKeyTest, NullAABBDataHashIsZeroBased) {
    AccelStructCreateInfo ci;
    ci.aabbData = nullptr;

    // Should produce a deterministic hash even with null data
    auto hash = ci.ComputeHash();
    EXPECT_NE(hash, 0);  // Hash includes other fields

    // Calling again should produce same hash
    EXPECT_EQ(ci.ComputeHash(), hash);
}

// ============================================================================
// Cacher Chain Data Flow Tests
// ============================================================================

class CacherChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        MockVoxelAABBDataFactory::Reset();
        MockAccelStructFactory::Reset();
    }
};

TEST_F(CacherChainTest, AABBToASDataFlow) {
    // Step 1: Create AABB data (from VoxelAABBCacher output)
    VoxelAABBData aabbData = MockVoxelAABBDataFactory::Create(1000);
    EXPECT_TRUE(aabbData.IsValid());
    EXPECT_EQ(aabbData.aabbCount, 1000);

    // Step 2: Create AccelStructCreateInfo referencing AABB data
    AccelStructCreateInfo asCreateInfo;
    asCreateInfo.aabbData = &aabbData;
    asCreateInfo.preferFastTrace = true;
    asCreateInfo.buildMode = ASBuildMode::Static;

    // Step 3: Create CachedAccelerationStructure (simulates AccelerationStructureCacher)
    CachedAccelerationStructure cached = MockAccelStructFactory::CreateFromAABBData(
        aabbData, asCreateInfo.buildMode);

    // Verify the chain is complete
    EXPECT_TRUE(cached.IsValid());
    EXPECT_EQ(cached.sourceAABBCount, 1000);  // Count captured from AABB data
    EXPECT_EQ(cached.buildMode, ASBuildMode::Static);
}

TEST_F(CacherChainTest, MultipleASFromSameAABB) {
    // One AABB data can be used for multiple AS with different build modes
    VoxelAABBData aabbData = MockVoxelAABBDataFactory::Create(500);

    // Create static AS
    AccelStructCreateInfo staticInfo;
    staticInfo.aabbData = &aabbData;
    staticInfo.buildMode = ASBuildMode::Static;
    CachedAccelerationStructure staticAS = MockAccelStructFactory::CreateFromAABBData(
        aabbData, staticInfo.buildMode);

    // Create dynamic AS from same data
    AccelStructCreateInfo dynamicInfo;
    dynamicInfo.aabbData = &aabbData;
    dynamicInfo.buildMode = ASBuildMode::Dynamic;
    CachedAccelerationStructure dynamicAS = MockAccelStructFactory::CreateFromAABBData(
        aabbData, dynamicInfo.buildMode);

    // Both should be valid
    EXPECT_TRUE(staticAS.IsValid());
    EXPECT_TRUE(dynamicAS.IsValid());

    // Both reference same source count
    EXPECT_EQ(staticAS.sourceAABBCount, 500);
    EXPECT_EQ(dynamicAS.sourceAABBCount, 500);

    // Static has TLAS, dynamic doesn't (managed separately)
    EXPECT_NE(staticAS.accelStruct.tlas, VK_NULL_HANDLE);
    EXPECT_EQ(dynamicAS.accelStruct.tlas, VK_NULL_HANDLE);

    // Dynamic has instance manager
    EXPECT_NE(dynamicAS.instanceManager, nullptr);
}

// ============================================================================
// Resource Cleanup Chain Tests
// ============================================================================

class CleanupChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        MockVoxelAABBDataFactory::Reset();
        MockAccelStructFactory::Reset();
    }
};

TEST_F(CleanupChainTest, AABBCleanupDoesntInvalidateAS) {
    // This tests the Phase 1 fix: AS stores sourceAABBCount, not pointer
    CachedAccelerationStructure cached;

    {
        // Create AABB data in inner scope
        VoxelAABBData aabbData = MockVoxelAABBDataFactory::Create(250);

        // Create AS from it
        cached = MockAccelStructFactory::CreateFromAABBData(aabbData, ASBuildMode::Static);

        EXPECT_TRUE(cached.IsValid());
        EXPECT_EQ(cached.sourceAABBCount, 250);

        // AABB data goes out of scope here
    }

    // AS should still be valid after AABB data is destroyed
    EXPECT_TRUE(cached.IsValid());
    EXPECT_EQ(cached.sourceAABBCount, 250);
}

TEST_F(CleanupChainTest, SceneChangeInvalidatesChain) {
    // Simulate scene change: old AABB data is destroyed, new scene is loaded

    // Scene 1
    auto aabbData1 = std::make_unique<VoxelAABBData>(MockVoxelAABBDataFactory::Create(100));
    AccelStructCreateInfo ci1;
    ci1.aabbData = aabbData1.get();
    auto hash1 = ci1.ComputeHash();

    // Create AS for scene 1
    CachedAccelerationStructure as1 = MockAccelStructFactory::CreateFromAABBData(*aabbData1);
    EXPECT_TRUE(as1.IsValid());

    // Scene change - destroy old data
    aabbData1.reset();

    // AS1 should still be valid (independent lifetime)
    EXPECT_TRUE(as1.IsValid());

    // Scene 2 - new AABB data
    auto aabbData2 = std::make_unique<VoxelAABBData>(MockVoxelAABBDataFactory::Create(200));
    AccelStructCreateInfo ci2;
    ci2.aabbData = aabbData2.get();
    auto hash2 = ci2.ComputeHash();

    // Different scene = different cache key
    EXPECT_NE(hash1, hash2);

    // Create AS for scene 2
    CachedAccelerationStructure as2 = MockAccelStructFactory::CreateFromAABBData(*aabbData2);
    EXPECT_TRUE(as2.IsValid());

    // Both AS instances exist independently
    EXPECT_EQ(as1.sourceAABBCount, 100);
    EXPECT_EQ(as2.sourceAABBCount, 200);
}

// ============================================================================
// Hot-Reload Simulation Tests
// ============================================================================

class HotReloadTest : public ::testing::Test {
protected:
    void SetUp() override {
        MockVoxelAABBDataFactory::Reset();
        MockAccelStructFactory::Reset();
    }
};

TEST_F(HotReloadTest, ShaderChangePreservesAS) {
    // Shader changes should NOT invalidate acceleration structures
    // AS is geometry-dependent, not shader-dependent

    VoxelAABBData aabbData = MockVoxelAABBDataFactory::Create(100);
    CachedAccelerationStructure cached = MockAccelStructFactory::CreateFromAABBData(
        aabbData, ASBuildMode::Static);

    EXPECT_TRUE(cached.IsValid());

    // Simulate shader hot-reload (nothing happens to AS)
    // In real code, ShaderModuleCacher would invalidate and recreate shaders
    // but AccelerationStructureCacher would be unaffected

    // AS remains valid
    EXPECT_TRUE(cached.IsValid());
    EXPECT_EQ(cached.sourceAABBCount, 100);
}

TEST_F(HotReloadTest, GeometryChangeRequiresNewAS) {
    // When geometry changes (new AABB data), we need a new AS

    // Original geometry
    auto aabb1 = MockVoxelAABBDataFactory::Create(100);
    AccelStructCreateInfo ci1;
    ci1.aabbData = &aabb1;
    auto hash1 = ci1.ComputeHash();

    CachedAccelerationStructure as1 = MockAccelStructFactory::CreateFromAABBData(aabb1);

    // Modified geometry (e.g., user edits voxels)
    auto aabb2 = MockVoxelAABBDataFactory::Create(150);  // Different count
    AccelStructCreateInfo ci2;
    ci2.aabbData = &aabb2;
    auto hash2 = ci2.ComputeHash();

    // Different geometry = different hash = cache miss
    EXPECT_NE(hash1, hash2);

    // New AS for new geometry
    CachedAccelerationStructure as2 = MockAccelStructFactory::CreateFromAABBData(aabb2);

    // Both valid, different content
    EXPECT_TRUE(as1.IsValid());
    EXPECT_TRUE(as2.IsValid());
    EXPECT_NE(as1.sourceAABBCount, as2.sourceAABBCount);
}

// ============================================================================
// Dynamic TLAS Instance Management Tests
// ============================================================================

class DynamicTLASChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        MockVoxelAABBDataFactory::Reset();
        MockAccelStructFactory::Reset();
    }
};

TEST_F(DynamicTLASChainTest, DynamicModeInstanceLifecycle) {
    // Create dynamic mode AS
    VoxelAABBData aabbData = MockVoxelAABBDataFactory::Create(100);
    CachedAccelerationStructure cached = MockAccelStructFactory::CreateFromAABBData(
        aabbData, ASBuildMode::Dynamic);

    EXPECT_TRUE(cached.IsValid());
    ASSERT_NE(cached.instanceManager, nullptr);

    // Add instances
    TLASInstanceManager::Instance inst1;
    inst1.blasKey = 1;
    inst1.blasAddress = 0x1000;
    auto id1 = cached.instanceManager->AddInstance(inst1);

    TLASInstanceManager::Instance inst2;
    inst2.blasKey = 2;
    inst2.blasAddress = 0x2000;
    auto id2 = cached.instanceManager->AddInstance(inst2);

    EXPECT_EQ(cached.instanceManager->GetActiveCount(), 2);

    // Remove an instance
    cached.instanceManager->RemoveInstance(id1);
    EXPECT_EQ(cached.instanceManager->GetActiveCount(), 1);

    // Clear all
    cached.instanceManager->Clear();
    EXPECT_EQ(cached.instanceManager->GetActiveCount(), 0);

    // AS itself is still valid (BLAS remains)
    EXPECT_TRUE(cached.IsValid());
}

TEST_F(DynamicTLASChainTest, MultipleInstancesFromDifferentBLAS) {
    // Multiple BLAS (from different AABB data) can be instanced in one TLAS

    // BLAS 1 from scene A
    VoxelAABBData aabbA = MockVoxelAABBDataFactory::Create(100);
    CachedAccelerationStructure blasA = MockAccelStructFactory::CreateFromAABBData(
        aabbA, ASBuildMode::Dynamic);

    // BLAS 2 from scene B
    VoxelAABBData aabbB = MockVoxelAABBDataFactory::Create(200);
    CachedAccelerationStructure blasB = MockAccelStructFactory::CreateFromAABBData(
        aabbB, ASBuildMode::Dynamic);

    // Both have instance managers
    ASSERT_NE(blasA.instanceManager, nullptr);
    ASSERT_NE(blasB.instanceManager, nullptr);

    // Each can manage its own instances
    TLASInstanceManager::Instance instA, instB;
    instA.blasKey = 1;
    instB.blasKey = 2;

    blasA.instanceManager->AddInstance(instA);
    blasB.instanceManager->AddInstance(instB);

    EXPECT_EQ(blasA.instanceManager->GetActiveCount(), 1);
    EXPECT_EQ(blasB.instanceManager->GetActiveCount(), 1);
}

// ============================================================================
// Cache Key Hasher Tests
// ============================================================================

class CacheKeyHasherTest : public ::testing::Test {};

TEST_F(CacheKeyHasherTest, EmptyHasherProducesConsistentHash) {
    CacheKeyHasher h1, h2;
    EXPECT_EQ(h1.Finalize(), h2.Finalize());
}

TEST_F(CacheKeyHasherTest, DifferentInputsDifferentHashes) {
    CacheKeyHasher h1, h2;
    h1.Add(42);
    h2.Add(43);
    EXPECT_NE(h1.Finalize(), h2.Finalize());
}

TEST_F(CacheKeyHasherTest, OrderMatters) {
    CacheKeyHasher h1, h2;
    h1.Add(1);
    h1.Add(2);

    h2.Add(2);
    h2.Add(1);

    EXPECT_NE(h1.Finalize(), h2.Finalize());
}

TEST_F(CacheKeyHasherTest, SameInputsSameHash) {
    CacheKeyHasher h1, h2;
    h1.Add(100);
    h1.Add(true);
    h1.Add(ASBuildMode::Dynamic);

    h2.Add(100);
    h2.Add(true);
    h2.Add(ASBuildMode::Dynamic);

    EXPECT_EQ(h1.Finalize(), h2.Finalize());
}

// ============================================================================
// VoxelAABBData Validity Tests
// ============================================================================

class VoxelAABBDataTest : public ::testing::Test {
protected:
    void SetUp() override {
        MockVoxelAABBDataFactory::Reset();
    }
};

TEST_F(VoxelAABBDataTest, ValidDataCheck) {
    VoxelAABBData data = MockVoxelAABBDataFactory::Create(100);
    EXPECT_TRUE(data.IsValid());
    EXPECT_NE(data.GetAABBBuffer(), VK_NULL_HANDLE);
    EXPECT_EQ(data.aabbCount, 100);
}

TEST_F(VoxelAABBDataTest, InvalidWithNoBuffer) {
    VoxelAABBData data;
    data.aabbCount = 100;  // Count set but no buffer
    EXPECT_FALSE(data.IsValid());
}

TEST_F(VoxelAABBDataTest, InvalidWithZeroCount) {
    VoxelAABBData data;
    data.aabbAllocation.buffer = reinterpret_cast<VkBuffer>(1);  // Buffer set
    data.aabbCount = 0;  // But zero count
    EXPECT_FALSE(data.IsValid());
}

TEST_F(VoxelAABBDataTest, BufferSizeCalculation) {
    VoxelAABBData data = MockVoxelAABBDataFactory::Create(100);
    VkDeviceSize expectedSize = 100 * sizeof(VoxelAABB);
    EXPECT_EQ(data.GetAABBBufferSize(), expectedSize);
}

// ============================================================================
// AccelStructCreateInfo Equality Tests
// ============================================================================

class AccelStructCreateInfoTest : public ::testing::Test {
protected:
    void SetUp() override {
        MockVoxelAABBDataFactory::Reset();
    }
};

TEST_F(AccelStructCreateInfoTest, EqualityOperator) {
    VoxelAABBData aabbData = MockVoxelAABBDataFactory::Create(100);

    AccelStructCreateInfo ci1, ci2;
    ci1.aabbData = &aabbData;
    ci1.preferFastTrace = true;
    ci1.allowUpdate = false;
    ci1.buildMode = ASBuildMode::Static;

    ci2.aabbData = &aabbData;
    ci2.preferFastTrace = true;
    ci2.allowUpdate = false;
    ci2.buildMode = ASBuildMode::Static;

    EXPECT_EQ(ci1, ci2);
}

TEST_F(AccelStructCreateInfoTest, InequalityOnDifferentAABBData) {
    VoxelAABBData aabb1 = MockVoxelAABBDataFactory::Create(100);
    VoxelAABBData aabb2 = MockVoxelAABBDataFactory::Create(200);

    AccelStructCreateInfo ci1, ci2;
    ci1.aabbData = &aabb1;
    ci2.aabbData = &aabb2;

    EXPECT_NE(ci1, ci2);
}

TEST_F(AccelStructCreateInfoTest, InequalityOnDifferentFlags) {
    VoxelAABBData aabbData = MockVoxelAABBDataFactory::Create(100);

    AccelStructCreateInfo ci1, ci2;
    ci1.aabbData = &aabbData;
    ci2.aabbData = &aabbData;

    ci1.preferFastTrace = true;
    ci2.preferFastTrace = false;

    EXPECT_NE(ci1, ci2);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
