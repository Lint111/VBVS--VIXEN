/**
 * @file test_attribute_registry.cpp
 * @brief Tests for AttributeRegistry class
 *
 * Verifies:
 * - Key attribute registration
 * - Attribute addition/removal
 * - Brick allocation across attributes
 * - Observer notifications
 */

#include <gtest/gtest.h>
#include <AttributeRegistry.h>
#include <glm/glm.hpp>

using namespace Vixen::VoxelData;

class AttributeRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry = std::make_unique<AttributeRegistry>();
    }

    std::unique_ptr<AttributeRegistry> registry;
};

TEST_F(AttributeRegistryTest, RegisterKeyAttribute) {
    AttributeIndex idx = registry->registerKey("density", AttributeType::Float, 0.0f);

    EXPECT_NE(idx, INVALID_ATTRIBUTE_INDEX);
    EXPECT_TRUE(registry->hasAttribute("density"));
    EXPECT_TRUE(registry->isKeyAttribute("density"));
    EXPECT_EQ(registry->getKeyAttributeName(), "density");
}

TEST_F(AttributeRegistryTest, AddNonKeyAttribute) {
    registry->registerKey("density", AttributeType::Float, 0.0f);
    AttributeIndex colorIdx = registry->addAttribute("color", AttributeType::Vec3, glm::vec3(1.0f));

    EXPECT_NE(colorIdx, INVALID_ATTRIBUTE_INDEX);
    EXPECT_TRUE(registry->hasAttribute("color"));
    EXPECT_FALSE(registry->isKeyAttribute("color"));
}

TEST_F(AttributeRegistryTest, AllocateBrick) {
    registry->registerKey("density", AttributeType::Float, 0.0f);
    registry->addAttribute("color", AttributeType::Vec3, glm::vec3(1.0f));

    uint32_t brickID = registry->allocateBrick();

    EXPECT_EQ(registry->getBrickCount(), 1);

    // Get brick view and verify it works
    BrickView brick = registry->getBrick(brickID);
    EXPECT_EQ(brick.getVoxelCount(), 512);  // Valid brick has 512 voxels
}

TEST_F(AttributeRegistryTest, FreeBrick) {
    registry->registerKey("density", AttributeType::Float, 0.0f);

    uint32_t brick1 = registry->allocateBrick();
    uint32_t brick2 = registry->allocateBrick();

    EXPECT_EQ(registry->getBrickCount(), 2);

    registry->freeBrick(brick1);

    EXPECT_EQ(registry->getBrickCount(), 1);
}

TEST_F(AttributeRegistryTest, MultipleAttributes) {
    registry->registerKey("density", AttributeType::Float, 1.0f);
    registry->addAttribute("color", AttributeType::Vec3, glm::vec3(1.0f, 0.0f, 0.0f));
    registry->addAttribute("material", AttributeType::Uint32, 0u);

    EXPECT_EQ(registry->getAttributeCount(), 3);

    auto names = registry->getAttributeNames();
    EXPECT_EQ(names.size(), 3);
}

// Observer test helper
class TestObserver : public IAttributeRegistryObserver {
public:
    void onKeyChanged(const std::string& oldKey, const std::string& newKey) override {
        keyChangedCount++;
        lastOldKey = oldKey;
        lastNewKey = newKey;
    }

    void onAttributeAdded(const std::string& name, AttributeType type) override {
        attributeAddedCount++;
        lastAddedName = name;
    }

    void onAttributeRemoved(const std::string& name) override {
        attributeRemovedCount++;
        lastRemovedName = name;
    }

    int keyChangedCount = 0;
    int attributeAddedCount = 0;
    int attributeRemovedCount = 0;
    std::string lastOldKey;
    std::string lastNewKey;
    std::string lastAddedName;
    std::string lastRemovedName;
};

TEST_F(AttributeRegistryTest, ObserverNotifications) {
    TestObserver observer;
    registry->addObserver(&observer);

    registry->registerKey("density", AttributeType::Float, 0.0f);
    // Key registration doesn't trigger onKeyChanged (only changeKey does)

    registry->addAttribute("color", AttributeType::Vec3, glm::vec3(1.0f));
    EXPECT_EQ(observer.attributeAddedCount, 1);
    EXPECT_EQ(observer.lastAddedName, "color");

    registry->removeAttribute("color");
    EXPECT_EQ(observer.attributeRemovedCount, 1);
    EXPECT_EQ(observer.lastRemovedName, "color");

    registry->removeObserver(&observer);
}
