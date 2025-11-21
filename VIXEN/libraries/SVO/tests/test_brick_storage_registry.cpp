#include <gtest/gtest.h>
#include "BrickStorage.h"
#include "AttributeRegistry.h"
#include <glm/glm.hpp>

using namespace SVO;
using namespace VoxelData;

// Test that BrickStorage can be constructed with AttributeRegistry
TEST(BrickStorageRegistry, ConstructionWithRegistry) {
    // Create registry and register attributes
    auto registry = std::make_shared<AttributeRegistry>();
    registry->registerKey("density", AttributeType::Float, 0.0f);
    registry->addAttribute("material", AttributeType::Uint32, 0u);

    // Create BrickStorage backed by registry
    DefaultBrickStorage storage(registry.get(), 3, BrickIndexOrder::Morton);

    EXPECT_EQ(storage.getDepth(), 3);
    EXPECT_EQ(storage.getSideLength(), 8);
    EXPECT_EQ(storage.getVoxelsPerBrick(), 512);
    EXPECT_EQ(storage.getBrickCount(), 0);
}

// Test brick allocation through registry
TEST(BrickStorageRegistry, BrickAllocation) {
    auto registry = std::make_shared<AttributeRegistry>();
    registry->registerKey("density", AttributeType::Float, 0.0f);
    registry->addAttribute("material", AttributeType::Uint32, 0u);

    DefaultBrickStorage storage(registry.get(), 3);

    uint32_t brick0 = storage.allocateBrick();
    uint32_t brick1 = storage.allocateBrick();

    EXPECT_EQ(brick0, 0);
    EXPECT_EQ(brick1, 1);
    EXPECT_EQ(storage.getBrickCount(), 2);
    EXPECT_EQ(registry->getBrickCount(), 2);
}

// Test get/set through BrickView delegation
TEST(BrickStorageRegistry, GetSetDelegation) {
    auto registry = std::make_shared<AttributeRegistry>();
    registry->registerKey("density", AttributeType::Float, 0.0f);
    registry->addAttribute("material", AttributeType::Uint32, 0u);

    DefaultBrickStorage storage(registry.get(), 3);
    uint32_t brickID = storage.allocateBrick();

    // Set density (array 0)
    storage.set<0>(brickID, 42, 0.8f);

    // Set material (array 1)
    storage.set<1>(brickID, 42, 123u);

    // Get values back
    float density = storage.get<0>(brickID, 42);
    uint32_t material = storage.get<1>(brickID, 42);

    EXPECT_FLOAT_EQ(density, 0.8f);
    EXPECT_EQ(material, 123u);
}

// Test 3D coordinate indexing still works
TEST(BrickStorageRegistry, Index3DConversion) {
    auto registry = std::make_shared<AttributeRegistry>();
    registry->registerKey("density", AttributeType::Float, 0.0f);
    registry->addAttribute("material", AttributeType::Uint32, 0u);

    DefaultBrickStorage storage(registry.get(), 3, BrickIndexOrder::LinearXYZ);

    // Corner voxels
    EXPECT_EQ(storage.getIndex(0, 0, 0), 0);
    EXPECT_EQ(storage.getIndex(7, 7, 7), 511);

    // Edge cases
    EXPECT_EQ(storage.getIndex(1, 0, 0), 1);
    EXPECT_EQ(storage.getIndex(0, 1, 0), 8);
    EXPECT_EQ(storage.getIndex(0, 0, 1), 64);
}

// Test attribute name mapping
TEST(BrickStorageRegistry, AttributeNameMapping) {
    auto registry = std::make_shared<AttributeRegistry>();
    registry->registerKey("density", AttributeType::Float, 0.0f);
    registry->addAttribute("material", AttributeType::Uint32, 0u);

    DefaultBrickStorage storage(registry.get(), 3);
    uint32_t brickID = storage.allocateBrick();

    // Set via template index (compile-time)
    storage.set<0>(brickID, 10, 1.0f);  // density
    storage.set<1>(brickID, 10, 99u);   // material

    // Verify via BrickView (runtime attribute name)
    BrickView brick = registry->getBrick(brickID);
    float density = brick.get<float>("density", 10);
    uint32_t material = brick.get<uint32_t>("material", 10);

    EXPECT_FLOAT_EQ(density, 1.0f);
    EXPECT_EQ(material, 99u);
}
