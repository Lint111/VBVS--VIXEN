#include <gtest/gtest.h>
#include "AttributeRegistry.h"
#include "BrickView.h"
#include <glm/glm.hpp>

using namespace Vixen::VoxelData;

// ============================================================================
// Test Fixture for BrickView Tests
// ============================================================================

class BrickViewTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry = std::make_shared<AttributeRegistry>();
    }

    std::shared_ptr<AttributeRegistry> registry;
};

// ============================================================================
// Basic Allocation and Indexing Tests
// ============================================================================

TEST_F(BrickViewTest, ConstructionParameters) {
    // BrickView always uses 8³ = 512 voxels
    uint32_t brickId = registry->allocateBrick();
    BrickView brickView = registry->getBrick(brickId);


    EXPECT_EQ(brickView.getVoxelCount(), 512); // 8³
}

TEST_F(BrickViewTest, AllocateMultipleBricks) {
    uint32_t brickId0 = registry->allocateBrick();
    uint32_t brickId1 = registry->allocateBrick();
    uint32_t brickId2 = registry->allocateBrick();

    BrickView view0 = registry->getBrick(brickId0);
    BrickView view1 = registry->getBrick(brickId1);
    BrickView view2 = registry->getBrick(brickId2);


    // Bricks should have different IDs
    EXPECT_NE(brickId0, brickId1);
    EXPECT_NE(brickId1, brickId2);
    EXPECT_NE(brickId0, brickId2);
}

TEST_F(BrickViewTest, Index3DConversion_Linear) {
    // Test LINEAR ordering (X varies fastest)
    uint32_t brickId = registry->allocateBrick();
    BrickView view = registry->getBrick(brickId);

    

    // Corner voxels
    EXPECT_EQ(view.getLinearIndex(0, 0, 0), 0);
    EXPECT_EQ(view.getLinearIndex(7, 7, 7), 511); // 8³-1

    // Edge cases
    EXPECT_EQ(view.getLinearIndex(1, 0, 0), 1);
    EXPECT_EQ(view.getLinearIndex(0, 1, 0), 8);
    EXPECT_EQ(view.getLinearIndex(0, 0, 1), 64);

    // Center voxel
    EXPECT_EQ(view.getLinearIndex(4, 4, 4), 4 + 4*8 + 4*64);
}

TEST_F(BrickViewTest, Index3DOutOfBounds) {
    uint32_t brickId = registry->allocateBrick();
    BrickView view = registry->getBrick(brickId);


    // Out of bounds access should throw or return invalid index
    // (Note: Current BrickView implementation may not throw, adjust test accordingly)
    EXPECT_THROW(view.getLinearIndex(-1, 0, 0), std::out_of_range);
    EXPECT_THROW(view.getLinearIndex(0, -1, 0), std::out_of_range);
    EXPECT_THROW(view.getLinearIndex(0, 0, -1), std::out_of_range);
    EXPECT_THROW(view.getLinearIndex(8, 0, 0), std::out_of_range);
    EXPECT_THROW(view.getLinearIndex(0, 8, 0), std::out_of_range);
    EXPECT_THROW(view.getLinearIndex(0, 0, 8), std::out_of_range);
}

// ============================================================================
// Data Access Tests - Float + Uint32 (Density + Material)
// ============================================================================

TEST_F(BrickViewTest, FloatAttribute_SetAndGet) {
    // Register density attribute
    auto densityIdx = registry->registerKey("density", AttributeType::Float, 0.0f);

    uint32_t brickId = registry->allocateBrick();
    BrickView view = registry->getBrick(brickId);
    // Set density for corner voxel
    size_t idx = view.getLinearIndex(0, 0, 0);
    view.set<float>("density", idx, 0.75f);

    // Retrieve
    float density = view.get<float>("density", idx);
    EXPECT_FLOAT_EQ(density, 0.75f);
}

TEST_F(BrickViewTest, MultipleAttributes_SetAndGet) {
    // Register attributes
    auto densityIdx = registry->registerKey("density", AttributeType::Float, 0.0f);
    auto materialIdx = registry->addAttribute("material", AttributeType::Uint32, 0u);

    uint32_t brickId = registry->allocateBrick();
    BrickView view = registry->getBrick(brickId);
    // Set density and material for corner voxel
    size_t idx = view.getLinearIndex(0, 0, 0);
    view.set<float>("density", idx, 0.75f);
    view.set<uint32_t>("material", idx, 42u);

    // Retrieve
    float density = view.get<float>("density", idx); 
    EXPECT_FLOAT_EQ(density, 0.75f);
    uint32_t material = view.get<uint32_t>("material", idx); 
    EXPECT_EQ(material, 42u);
}

TEST_F(BrickViewTest, MultipleBricks_DataIsolation) {
    auto densityIdx = registry->registerKey("density", AttributeType::Float, 0.0f);
    auto materialIdx = registry->addAttribute("material", AttributeType::Uint32, 0u);

    uint32_t brickId0 = registry->allocateBrick();
    uint32_t brickId1 = registry->allocateBrick();

    BrickView view0 = registry->getBrick(brickId0);
    BrickView view1 = registry->getBrick(brickId1);


    size_t centerIdx = view0.getLinearIndex(4, 4, 4);

    // Write to brick 0
    view0.set<float>("density", centerIdx, 1.0f);
    view0.set<uint32_t>("material", centerIdx, 100u);
    // Write to brick 1
    view1.set<float>("density", centerIdx, 0.5f);
    view1.set<uint32_t>("material", centerIdx, 200u);

    // Verify isolation
    float d0 = view0.get<float>("density", centerIdx); 
    EXPECT_FLOAT_EQ(d0, 1.0f);
    uint32_t m0 = view0.get<uint32_t>("material", centerIdx); 
    EXPECT_EQ(m0, 100u);
    float d1 = view1.get<float>("density", centerIdx); 
    EXPECT_FLOAT_EQ(d1, 0.5f);
    uint32_t m1 = view1.get<uint32_t>("material", centerIdx); 
    EXPECT_EQ(m1, 200u);
}

TEST_F(BrickViewTest, FillBrick_GradientPattern) {
    auto densityIdx = registry->registerKey("density", AttributeType::Float, 0.0f);
    auto materialIdx = registry->addAttribute("material", AttributeType::Uint32, 0u);

    uint32_t brickId = registry->allocateBrick();
    BrickView view = registry->getBrick(brickId);


    // Fill brick with gradient pattern (8³ = 512 voxels)
    for (int z = 0; z < 8; ++z) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                size_t idx = view.getLinearIndex(x, y, z);
                float density = (x + y + z) / 21.0f; // [0, 1] (max = 7+7+7=21)
                uint32_t material = x + y * 8 + z * 64;

                view.set<float>("density", idx, density);
                view.set<uint32_t>("material", idx, material);
            }
        }
    }

    // Verify corners and center
    float d000 = view.get<float>("density", view.getLinearIndex(0, 0, 0)); 
    EXPECT_FLOAT_EQ(d000, 0.0f);
    float d777 = view.get<float>("density", view.getLinearIndex(7, 7, 7)); 
    EXPECT_FLOAT_EQ(d777, 1.0f);
    uint32_t mat123 = view.get<uint32_t>("material", view.getLinearIndex(1, 2, 3)); 
    EXPECT_EQ(mat123, 1 + 2*8 + 3*64);
}

// ============================================================================
// Vec3 Attribute Tests (RGB Color)
// ============================================================================

TEST_F(BrickViewTest, Vec3Attribute_Color) {
    auto colorIdx = registry->registerKey("color", AttributeType::Vec3, glm::vec3(0.0f));

    uint32_t brickId = registry->allocateBrick();
    BrickView view = registry->getBrick(brickId);
    size_t idx = view.getLinearIndex(4, 4, 4);
    glm::vec3 color(1.0f, 0.5f, 0.25f);
    view.set<glm::vec3>("color", idx, color);

    glm::vec3 retrieved = view.get<glm::vec3>("color", idx);
    EXPECT_FLOAT_EQ(retrieved.r, 1.0f);
    EXPECT_FLOAT_EQ(retrieved.g, 0.5f);
    EXPECT_FLOAT_EQ(retrieved.b, 0.25f);
}

// ============================================================================
// 3D Coordinate API Tests (setAt3D / getAt3D)
// ============================================================================

TEST_F(BrickViewTest, ThreeDCoordinateAPI) {
    auto densityIdx = registry->registerKey("density", AttributeType::Float, 0.0f);

    uint32_t brickId = registry->allocateBrick();
    BrickView view = registry->getBrick(brickId);


    // Set using 3D coordinates
    view.setAt3D<float>("density", 3, 5, 7, 0.42f);

    // Get using 3D coordinates
    float density = view.getAt3D<float>("density", 3, 5, 7);
    EXPECT_FLOAT_EQ(density, 0.42f);

    // Verify same as linear index
    size_t idx = view.getLinearIndex(3, 5, 7);
    float dens = view.get<float>("density", idx); 
    EXPECT_FLOAT_EQ(dens, 0.42f);
}

// ============================================================================
// Pointer Access Tests (Zero-Cost Path)
// ============================================================================

TEST_F(BrickViewTest, PointerAccess_DirectWrite) {
    auto densityIdx = registry->registerKey("density", AttributeType::Float, 0.0f);

    uint32_t brickId = registry->allocateBrick();
    BrickView view = registry->getBrick(brickId);
    // Get direct pointer to density data
    float* densityPtr = view.getAttributePointer<float>(densityIdx);
    ASSERT_NE(densityPtr, nullptr);

    // Write directly via pointer (zero-cost)
    densityPtr[0] = 0.1f;
    densityPtr[256] = 0.5f; // Middle of brick
    densityPtr[511] = 0.9f; // Last voxel

    // Verify via get<T>
    float d0 = view.get<float>("density", 0);
    float d256 = view.get<float>("density", 256);
    float d511 = view.get<float>("density", 511);
    EXPECT_FLOAT_EQ(d0, 0.1f);
    EXPECT_FLOAT_EQ(d256, 0.5f);
    EXPECT_FLOAT_EQ(d511, 0.9f);
}

TEST_F(BrickViewTest, PointerAccess_Vec3) {
    auto colorIdx = registry->registerKey("color", AttributeType::Vec3, glm::vec3(0.0f));

    uint32_t brickId = registry->allocateBrick();
    BrickView view = registry->getBrick(brickId);
    // Get direct pointer to color data
    glm::vec3* colorPtr = view.getAttributePointer<glm::vec3>(colorIdx);
    ASSERT_NE(colorPtr, nullptr);

    // Write directly
    colorPtr[0] = glm::vec3(1.0f, 0.0f, 0.0f); // Red
    colorPtr[100] = glm::vec3(0.0f, 1.0f, 0.0f); // Green

    // Verify
    glm::vec3 red = view.get<glm::vec3>("color", 0);
    EXPECT_FLOAT_EQ(red.r, 1.0f);
    EXPECT_FLOAT_EQ(red.g, 0.0f);
    EXPECT_FLOAT_EQ(red.b, 0.0f);
}

// ============================================================================
// Index-Based Access Tests (AttributeIndex for O(1) lookup)
// ============================================================================

TEST_F(BrickViewTest, IndexBasedAccess_Performance) {
    auto densityIdx = registry->registerKey("density", AttributeType::Float, 0.0f);
    auto colorIdx = registry->addAttribute("color", AttributeType::Vec3, glm::vec3(0.0f));

    uint32_t brickId = registry->allocateBrick();
    BrickView view = registry->getBrick(brickId);

    // Use AttributeIndex directly (fastest path)
    EXPECT_EQ(densityIdx, 0); // Key attribute is always index 0

    // Get pointers using AttributeIndex (zero-cost O(1) lookup)
    float* densityPtr = view.getAttributePointer<float>(densityIdx);
    glm::vec3* colorPtr = view.getAttributePointer<glm::vec3>(colorIdx);

    ASSERT_NE(densityPtr, nullptr);
    ASSERT_NE(colorPtr, nullptr);

    // Fill brick using pointers (simulates tight loop in ray traversal)
    for (size_t i = 0; i < 512; ++i) {
        densityPtr[i] = static_cast<float>(i) / 512.0f;
        colorPtr[i] = glm::vec3(static_cast<float>(i % 256) / 255.0f);
    }

    // Verify
    EXPECT_FLOAT_EQ(densityPtr[0], 0.0f);
    EXPECT_FLOAT_EQ(densityPtr[511], 511.0f / 512.0f);
    EXPECT_FLOAT_EQ(colorPtr[100].r, 100.0f / 255.0f);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
