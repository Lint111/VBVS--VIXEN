/**
 * @file test_lifetime_safety.cpp
 * @brief Sprint 5 Phase 5.1: Lifetime/Safety tests for shared_ptr fix
 *
 * Tests the pointer safety fix from Phase 1.1 where CachedAccelerationStructure
 * stores sourceAABBCount (value) instead of aabbDataRef (pointer).
 *
 * Verifies:
 * - AccelerationStructure remains valid after VoxelAABBData is destroyed
 * - No dangling pointer dependencies between AS and source data
 * - Proper decoupling of cached resources from creation parameters
 */

#include <gtest/gtest.h>
#include "AccelerationStructureCacher.h"
#include "VoxelAABBCacher.h"
#include "TLASInstanceManager.h"
#include "Memory/IMemoryAllocator.h"

#include <memory>
#include <optional>

using namespace CashSystem;

// ============================================================================
// Phase 5.1: Lifetime/Safety Tests for Pointer Safety Fix
// ============================================================================

class LifetimeSafetyTest : public ::testing::Test {
protected:
    // Helper to create a mock VoxelAABBData with specified count
    static VoxelAABBData CreateMockAABBData(uint32_t aabbCount) {
        VoxelAABBData data;
        data.aabbCount = aabbCount;
        data.gridResolution = 64;
        data.voxelSize = 1.0f;
        // Note: buffers are null (no real Vulkan device), but count is valid
        return data;
    }
};

// -----------------------------------------------------------------------------
// Test: CachedAccelerationStructure stores sourceAABBCount, not pointer
// -----------------------------------------------------------------------------

TEST_F(LifetimeSafetyTest, SourceAABBCountIsValueNotPointer) {
    // This test verifies the Phase 1.1 fix: CachedAccelerationStructure stores
    // sourceAABBCount as a uint32_t value, not as a pointer to external data.

    CachedAccelerationStructure cached;

    // Set sourceAABBCount directly (as would happen in Create())
    cached.sourceAABBCount = 42;
    cached.buildMode = ASBuildMode::Static;

    // Simulate valid handles for IsValid() check
    cached.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);
    cached.accelStruct.tlas = reinterpret_cast<VkAccelerationStructureKHR>(2);

    // The count is stored as a value - no external dependency
    EXPECT_EQ(cached.sourceAABBCount, 42);
    EXPECT_TRUE(cached.IsValid());
}

// -----------------------------------------------------------------------------
// Test: AS remains valid after source AABB data scope ends
// -----------------------------------------------------------------------------

TEST_F(LifetimeSafetyTest, ASValidAfterAABBDataScopeEnds) {
    // Create AS in inner scope where VoxelAABBData exists
    CachedAccelerationStructure cached;

    {
        // Inner scope - VoxelAABBData exists here
        VoxelAABBData aabbData = CreateMockAABBData(100);
        EXPECT_EQ(aabbData.aabbCount, 100);

        // Simulate what Create() does: copy the count, not store pointer
        cached.sourceAABBCount = aabbData.aabbCount;
        cached.buildMode = ASBuildMode::Static;
        cached.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);
        cached.accelStruct.tlas = reinterpret_cast<VkAccelerationStructureKHR>(2);

        // VoxelAABBData goes out of scope here
    }

    // After VoxelAABBData is destroyed, cached AS should still be valid
    // This would have crashed with the old pointer-based design
    EXPECT_EQ(cached.sourceAABBCount, 100);
    EXPECT_TRUE(cached.IsValid());
}

// -----------------------------------------------------------------------------
// Test: Multiple AS instances with independent counts
// -----------------------------------------------------------------------------

TEST_F(LifetimeSafetyTest, MultipleASIndependentCounts) {
    // Create multiple CachedAccelerationStructure instances from different data
    CachedAccelerationStructure cached1, cached2, cached3;

    {
        VoxelAABBData data1 = CreateMockAABBData(50);
        VoxelAABBData data2 = CreateMockAABBData(100);
        VoxelAABBData data3 = CreateMockAABBData(200);

        cached1.sourceAABBCount = data1.aabbCount;
        cached2.sourceAABBCount = data2.aabbCount;
        cached3.sourceAABBCount = data3.aabbCount;

        // Set valid handles
        cached1.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);
        cached1.accelStruct.tlas = reinterpret_cast<VkAccelerationStructureKHR>(2);
        cached2.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(3);
        cached2.accelStruct.tlas = reinterpret_cast<VkAccelerationStructureKHR>(4);
        cached3.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(5);
        cached3.accelStruct.tlas = reinterpret_cast<VkAccelerationStructureKHR>(6);

        cached1.buildMode = ASBuildMode::Static;
        cached2.buildMode = ASBuildMode::Static;
        cached3.buildMode = ASBuildMode::Static;
    }

    // After all source data destroyed, each AS should retain its own count
    EXPECT_EQ(cached1.sourceAABBCount, 50);
    EXPECT_EQ(cached2.sourceAABBCount, 100);
    EXPECT_EQ(cached3.sourceAABBCount, 200);

    EXPECT_TRUE(cached1.IsValid());
    EXPECT_TRUE(cached2.IsValid());
    EXPECT_TRUE(cached3.IsValid());
}

// -----------------------------------------------------------------------------
// Test: VoxelAABBData cleanup doesn't invalidate AS
// -----------------------------------------------------------------------------

TEST_F(LifetimeSafetyTest, AABBDataCleanupDoesntInvalidateAS) {
    CachedAccelerationStructure cached;

    // Use unique_ptr to simulate explicit cleanup
    auto aabbData = std::make_unique<VoxelAABBData>(CreateMockAABBData(75));

    // Create AS from AABB data
    cached.sourceAABBCount = aabbData->aabbCount;
    cached.buildMode = ASBuildMode::Static;
    cached.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);
    cached.accelStruct.tlas = reinterpret_cast<VkAccelerationStructureKHR>(2);

    // Verify AS is valid before cleanup
    EXPECT_TRUE(cached.IsValid());
    EXPECT_EQ(cached.sourceAABBCount, 75);

    // Explicitly destroy the AABB data (simulating cacher cleanup)
    aabbData.reset();

    // AS should still be valid - no pointer dependency
    EXPECT_TRUE(cached.IsValid());
    EXPECT_EQ(cached.sourceAABBCount, 75);
}

// -----------------------------------------------------------------------------
// Test: AccelStructCreateInfo pointer is temporary (during Create only)
// -----------------------------------------------------------------------------

TEST_F(LifetimeSafetyTest, CreateInfoPointerIsTemporary) {
    // AccelStructCreateInfo.aabbData is a raw pointer that must be valid
    // during Create() but is NOT stored afterward

    AccelStructCreateInfo ci;
    VoxelAABBData tempData = CreateMockAABBData(123);

    // Set pointer (as would happen during GetOrCreate call)
    ci.aabbData = &tempData;
    EXPECT_EQ(ci.aabbData->aabbCount, 123);

    // After Create() would copy the count...
    CachedAccelerationStructure cached;
    cached.sourceAABBCount = ci.aabbData->aabbCount;
    cached.buildMode = ci.buildMode;
    cached.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);
    cached.accelStruct.tlas = reinterpret_cast<VkAccelerationStructureKHR>(2);

    // Clear the create info pointer (simulating end of GetOrCreate)
    ci.aabbData = nullptr;

    // Cached structure should still be valid
    EXPECT_TRUE(cached.IsValid());
    EXPECT_EQ(cached.sourceAABBCount, 123);
}

// -----------------------------------------------------------------------------
// Test: Zero AABB count results in invalid AS (regardless of handles)
// -----------------------------------------------------------------------------

TEST_F(LifetimeSafetyTest, ZeroAABBCountAlwaysInvalid) {
    CachedAccelerationStructure cached;
    cached.sourceAABBCount = 0;  // Zero count
    cached.buildMode = ASBuildMode::Static;

    // Even with valid handles, zero count = invalid
    cached.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);
    cached.accelStruct.tlas = reinterpret_cast<VkAccelerationStructureKHR>(2);

    EXPECT_FALSE(cached.IsValid());
}

// -----------------------------------------------------------------------------
// Test: Dynamic mode AS only needs BLAS, not TLAS
// -----------------------------------------------------------------------------

TEST_F(LifetimeSafetyTest, DynamicModeOnlyNeedsBLAS) {
    CachedAccelerationStructure cached;
    cached.sourceAABBCount = 50;
    cached.buildMode = ASBuildMode::Dynamic;

    // Only BLAS is set (TLAS managed separately in Dynamic mode)
    cached.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);
    cached.accelStruct.tlas = VK_NULL_HANDLE;

    EXPECT_TRUE(cached.IsValid());
}

// -----------------------------------------------------------------------------
// Test: shared_ptr wrapper maintains independent lifetime
// -----------------------------------------------------------------------------

TEST_F(LifetimeSafetyTest, SharedPtrIndependentLifetime) {
    std::shared_ptr<CachedAccelerationStructure> cachedPtr;

    {
        // Inner scope - create and configure AS
        auto aabbData = std::make_unique<VoxelAABBData>(CreateMockAABBData(88));

        cachedPtr = std::make_shared<CachedAccelerationStructure>();
        cachedPtr->sourceAABBCount = aabbData->aabbCount;
        cachedPtr->buildMode = ASBuildMode::Static;
        cachedPtr->accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);
        cachedPtr->accelStruct.tlas = reinterpret_cast<VkAccelerationStructureKHR>(2);

        // aabbData destroyed here
    }

    // shared_ptr to AS should still be valid
    ASSERT_NE(cachedPtr, nullptr);
    EXPECT_TRUE(cachedPtr->IsValid());
    EXPECT_EQ(cachedPtr->sourceAABBCount, 88);
}

// -----------------------------------------------------------------------------
// Test: weak_ptr correctly invalidates when AS destroyed
// -----------------------------------------------------------------------------

TEST_F(LifetimeSafetyTest, WeakPtrInvalidatesCorrectly) {
    std::weak_ptr<CachedAccelerationStructure> weakPtr;

    {
        auto cached = std::make_shared<CachedAccelerationStructure>();
        cached->sourceAABBCount = 99;
        cached->buildMode = ASBuildMode::Static;
        cached->accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);
        cached->accelStruct.tlas = reinterpret_cast<VkAccelerationStructureKHR>(2);

        weakPtr = cached;

        // While shared_ptr exists, weak_ptr should be valid
        EXPECT_FALSE(weakPtr.expired());
        auto locked = weakPtr.lock();
        ASSERT_NE(locked, nullptr);
        EXPECT_EQ(locked->sourceAABBCount, 99);

        // shared_ptr destroyed here
    }

    // After shared_ptr destroyed, weak_ptr should be expired
    EXPECT_TRUE(weakPtr.expired());
    EXPECT_EQ(weakPtr.lock(), nullptr);
}

// -----------------------------------------------------------------------------
// Test: TLASInstanceManager has independent lifetime from AABB data
// -----------------------------------------------------------------------------

TEST_F(LifetimeSafetyTest, TLASInstanceManagerIndependentLifetime) {
    CachedAccelerationStructure cached;
    cached.sourceAABBCount = 100;
    cached.buildMode = ASBuildMode::Dynamic;
    cached.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);

    // Create instance manager (for Dynamic mode)
    cached.instanceManager = std::make_unique<TLASInstanceManager>();

    // Add some instances
    TLASInstanceManager::Instance inst;
    inst.blasKey = 12345;
    inst.blasAddress = 0xDEADBEEF;
    auto id = cached.instanceManager->AddInstance(inst);
    EXPECT_NE(id, TLASInstanceManager::InvalidId);

    // The instance manager is owned by CachedAccelerationStructure
    // and has no dependency on the original VoxelAABBData
    EXPECT_EQ(cached.instanceManager->GetActiveCount(), 1);
    EXPECT_TRUE(cached.IsValid());
}

// -----------------------------------------------------------------------------
// Test: AccelerationStructureData IsValid checks
// -----------------------------------------------------------------------------

TEST_F(LifetimeSafetyTest, AccelerationStructureDataValidityChecks) {
    AccelerationStructureData asData;

    // Initially invalid (no handles)
    EXPECT_FALSE(asData.IsValid());

    // Only BLAS = still invalid (need both for AccelerationStructureData)
    asData.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);
    EXPECT_FALSE(asData.IsValid());

    // Both BLAS and TLAS = valid
    asData.tlas = reinterpret_cast<VkAccelerationStructureKHR>(2);
    EXPECT_TRUE(asData.IsValid());
}

// -----------------------------------------------------------------------------
// Test: VoxelAABBData IsValid independent of AS
// -----------------------------------------------------------------------------

TEST_F(LifetimeSafetyTest, VoxelAABBDataValidityIndependent) {
    VoxelAABBData aabbData;
    aabbData.aabbCount = 100;

    // No buffer = invalid
    EXPECT_FALSE(aabbData.IsValid());

    // With buffer (simulated) = valid
    aabbData.aabbAllocation.buffer = reinterpret_cast<VkBuffer>(1);
    EXPECT_TRUE(aabbData.IsValid());

    // Create AS from this data
    CachedAccelerationStructure cached;
    cached.sourceAABBCount = aabbData.aabbCount;
    cached.buildMode = ASBuildMode::Static;
    cached.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);
    cached.accelStruct.tlas = reinterpret_cast<VkAccelerationStructureKHR>(2);

    // "Destroy" AABB data
    aabbData.aabbAllocation.buffer = VK_NULL_HANDLE;
    aabbData.aabbCount = 0;
    EXPECT_FALSE(aabbData.IsValid());

    // AS should still be valid (independent lifetime)
    EXPECT_TRUE(cached.IsValid());
    EXPECT_EQ(cached.sourceAABBCount, 100);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
