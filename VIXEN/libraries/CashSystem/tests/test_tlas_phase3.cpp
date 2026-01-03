#include <gtest/gtest.h>
#include "TLASInstanceManager.h"
#include "AccelerationStructureCacher.h"  // For ASBuildMode, AccelStructCreateInfo

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace CashSystem;

// ============================================================================
// TLASInstanceManager Tests - Pure CPU logic, fully testable
// ============================================================================

class TLASInstanceManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager = std::make_unique<TLASInstanceManager>();
    }

    void TearDown() override {
        manager.reset();
    }

    std::unique_ptr<TLASInstanceManager> manager;
};

TEST_F(TLASInstanceManagerTest, InitialState) {
    EXPECT_EQ(manager->GetActiveCount(), 0);
    EXPECT_TRUE(manager->IsEmpty());
    EXPECT_EQ(manager->GetDirtyLevel(), TLASInstanceManager::DirtyLevel::Clean);
}

TEST_F(TLASInstanceManagerTest, AddInstance) {
    TLASInstanceManager::Instance inst;
    inst.blasKey = 12345;
    inst.blasAddress = 0xDEADBEEF;
    inst.customIndex = 42;
    inst.mask = 0xFF;

    auto id = manager->AddInstance(inst);

    EXPECT_NE(id, TLASInstanceManager::InvalidId);
    EXPECT_EQ(manager->GetActiveCount(), 1);
    EXPECT_FALSE(manager->IsEmpty());
    EXPECT_EQ(manager->GetDirtyLevel(), TLASInstanceManager::DirtyLevel::StructuralChange);
}

TEST_F(TLASInstanceManagerTest, AddMultipleInstances) {
    TLASInstanceManager::Instance inst1, inst2, inst3;
    inst1.blasKey = 1;
    inst2.blasKey = 2;
    inst3.blasKey = 3;

    auto id1 = manager->AddInstance(inst1);
    auto id2 = manager->AddInstance(inst2);
    auto id3 = manager->AddInstance(inst3);

    EXPECT_EQ(manager->GetActiveCount(), 3);
    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, id3);
}

TEST_F(TLASInstanceManagerTest, RemoveInstance) {
    TLASInstanceManager::Instance inst;
    inst.blasKey = 12345;

    auto id = manager->AddInstance(inst);
    EXPECT_EQ(manager->GetActiveCount(), 1);

    manager->ClearDirty();  // Reset dirty state
    manager->RemoveInstance(id);

    EXPECT_EQ(manager->GetActiveCount(), 0);
    EXPECT_TRUE(manager->IsEmpty());
    EXPECT_EQ(manager->GetDirtyLevel(), TLASInstanceManager::DirtyLevel::StructuralChange);
}

TEST_F(TLASInstanceManagerTest, UpdateTransform) {
    TLASInstanceManager::Instance inst;
    inst.blasKey = 12345;

    auto id = manager->AddInstance(inst);
    manager->ClearDirty();  // Reset after add

    // Update transform
    glm::mat3x4 newTransform;
    newTransform[0] = glm::vec4(2.0f, 0, 0, 0);  // Scale X by 2
    newTransform[1] = glm::vec4(0, 2.0f, 0, 0);  // Scale Y by 2
    newTransform[2] = glm::vec4(0, 0, 2.0f, 0);  // Scale Z by 2

    manager->UpdateTransform(id, newTransform);

    // Transform-only update should set TransformsOnly dirty level
    EXPECT_EQ(manager->GetDirtyLevel(), TLASInstanceManager::DirtyLevel::TransformsOnly);
}

TEST_F(TLASInstanceManagerTest, DirtyLevelPrecedence) {
    TLASInstanceManager::Instance inst1, inst2;
    inst1.blasKey = 1;
    inst2.blasKey = 2;

    auto id1 = manager->AddInstance(inst1);
    manager->ClearDirty();

    // Transform update sets TransformsOnly
    glm::mat3x4 transform{};
    manager->UpdateTransform(id1, transform);
    EXPECT_EQ(manager->GetDirtyLevel(), TLASInstanceManager::DirtyLevel::TransformsOnly);

    // Structural change (add) should elevate to StructuralChange
    manager->AddInstance(inst2);
    EXPECT_EQ(manager->GetDirtyLevel(), TLASInstanceManager::DirtyLevel::StructuralChange);
}

TEST_F(TLASInstanceManagerTest, ClearDirty) {
    TLASInstanceManager::Instance inst;
    inst.blasKey = 12345;

    manager->AddInstance(inst);
    EXPECT_EQ(manager->GetDirtyLevel(), TLASInstanceManager::DirtyLevel::StructuralChange);

    manager->ClearDirty();
    EXPECT_EQ(manager->GetDirtyLevel(), TLASInstanceManager::DirtyLevel::Clean);
}

TEST_F(TLASInstanceManagerTest, Clear) {
    TLASInstanceManager::Instance inst1, inst2;
    inst1.blasKey = 1;
    inst2.blasKey = 2;

    manager->AddInstance(inst1);
    manager->AddInstance(inst2);
    EXPECT_EQ(manager->GetActiveCount(), 2);

    manager->Clear();

    EXPECT_EQ(manager->GetActiveCount(), 0);
    EXPECT_TRUE(manager->IsEmpty());
}

TEST_F(TLASInstanceManagerTest, GenerateVulkanInstances) {
    TLASInstanceManager::Instance inst1, inst2;
    inst1.blasKey = 1;
    inst1.blasAddress = 0x1000;
    inst1.customIndex = 10;
    inst1.mask = 0xF0;

    inst2.blasKey = 2;
    inst2.blasAddress = 0x2000;
    inst2.customIndex = 20;
    inst2.mask = 0x0F;

    manager->AddInstance(inst1);
    manager->AddInstance(inst2);

    std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
    manager->GenerateVulkanInstances(vkInstances);

    EXPECT_EQ(vkInstances.size(), 2);

    // Check first instance
    EXPECT_EQ(vkInstances[0].accelerationStructureReference, 0x1000);
    EXPECT_EQ(vkInstances[0].instanceCustomIndex, 10);
    EXPECT_EQ(vkInstances[0].mask, 0xF0);

    // Check second instance
    EXPECT_EQ(vkInstances[1].accelerationStructureReference, 0x2000);
    EXPECT_EQ(vkInstances[1].instanceCustomIndex, 20);
    EXPECT_EQ(vkInstances[1].mask, 0x0F);
}

TEST_F(TLASInstanceManagerTest, IdReuse) {
    TLASInstanceManager::Instance inst;
    inst.blasKey = 1;

    auto id1 = manager->AddInstance(inst);
    manager->RemoveInstance(id1);

    // Add another instance - should reuse the freed ID
    auto id2 = manager->AddInstance(inst);

    // ID should be reused (implementation detail, but good to verify)
    EXPECT_EQ(id1, id2);
}

TEST_F(TLASInstanceManagerTest, InvalidIdOperations) {
    // Operations on invalid ID should not crash
    manager->UpdateTransform(TLASInstanceManager::InvalidId, glm::mat3x4{});
    manager->RemoveInstance(TLASInstanceManager::InvalidId);

    // Also test with out-of-range ID
    manager->UpdateTransform(999999, glm::mat3x4{});
    manager->RemoveInstance(999999);

    EXPECT_TRUE(manager->IsEmpty());
}

// ============================================================================
// ASBuildMode and AccelStructCreateInfo Tests
// ============================================================================

class ASBuildModeTest : public ::testing::Test {};

TEST_F(ASBuildModeTest, DefaultBuildMode) {
    AccelStructCreateInfo ci;
    EXPECT_EQ(ci.buildMode, ASBuildMode::Static);
}

TEST_F(ASBuildModeTest, HashIncludesBuildMode) {
    AccelStructCreateInfo ci1, ci2;
    ci1.buildMode = ASBuildMode::Static;
    ci2.buildMode = ASBuildMode::Dynamic;

    // Same other params, different build mode should produce different hash
    auto hash1 = ci1.ComputeHash();
    auto hash2 = ci2.ComputeHash();

    EXPECT_NE(hash1, hash2);
}

TEST_F(ASBuildModeTest, DynamicModeParams) {
    AccelStructCreateInfo ci;
    ci.buildMode = ASBuildMode::Dynamic;
    ci.maxInstances = 2048;
    ci.imageCount = 3;

    EXPECT_EQ(ci.maxInstances, 2048);
    EXPECT_EQ(ci.imageCount, 3);
}

TEST_F(ASBuildModeTest, EqualityOperator) {
    AccelStructCreateInfo ci1, ci2;
    ci1.buildMode = ASBuildMode::Dynamic;
    ci2.buildMode = ASBuildMode::Dynamic;

    EXPECT_EQ(ci1, ci2);

    ci2.buildMode = ASBuildMode::Static;
    EXPECT_NE(ci1, ci2);
}

// ============================================================================
// CachedAccelerationStructure Tests
// ============================================================================

class CachedASTest : public ::testing::Test {};

TEST_F(CachedASTest, StaticModeValidity) {
    CachedAccelerationStructure cached;
    cached.buildMode = ASBuildMode::Static;
    cached.sourceAABBCount = 100;

    // Without valid AS handles, should be invalid
    EXPECT_FALSE(cached.IsValid());

    // With valid handles (simulated)
    cached.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);
    cached.accelStruct.tlas = reinterpret_cast<VkAccelerationStructureKHR>(2);

    EXPECT_TRUE(cached.IsValid());
}

TEST_F(CachedASTest, DynamicModeValidity) {
    CachedAccelerationStructure cached;
    cached.buildMode = ASBuildMode::Dynamic;
    cached.sourceAABBCount = 100;

    // Dynamic mode only needs BLAS, TLAS is managed separately
    EXPECT_FALSE(cached.IsValid());

    cached.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);
    // No TLAS needed for Dynamic mode validity
    EXPECT_TRUE(cached.IsValid());
}

TEST_F(CachedASTest, ZeroAABBsInvalid) {
    CachedAccelerationStructure cached;
    cached.buildMode = ASBuildMode::Static;
    cached.sourceAABBCount = 0;  // No AABBs
    cached.accelStruct.blas = reinterpret_cast<VkAccelerationStructureKHR>(1);
    cached.accelStruct.tlas = reinterpret_cast<VkAccelerationStructureKHR>(2);

    // Zero AABB count means invalid even with handles
    EXPECT_FALSE(cached.IsValid());
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
