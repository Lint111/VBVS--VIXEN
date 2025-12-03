/**
 * @file test_attribute_storage.cpp
 * @brief Tests for AttributeStorage class
 *
 * Verifies:
 * - Slot allocation and deallocation
 * - Memory layout correctness
 * - Type safety for different attribute types
 */

#include <gtest/gtest.h>
#include <AttributeStorage.h>
#include <glm/glm.hpp>

using namespace Vixen::VoxelData;

class AttributeStorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        floatStorage = std::make_unique<AttributeStorage>("density", AttributeType::Float, 0.0f);
        vec3Storage = std::make_unique<AttributeStorage>("color", AttributeType::Vec3, glm::vec3(1.0f));
    }

    std::unique_ptr<AttributeStorage> floatStorage;
    std::unique_ptr<AttributeStorage> vec3Storage;
};

TEST_F(AttributeStorageTest, FloatStorageProperties) {
    EXPECT_EQ(floatStorage->getName(), "density");
    EXPECT_EQ(floatStorage->getType(), AttributeType::Float);
    EXPECT_EQ(floatStorage->getElementSize(), sizeof(float));
}

TEST_F(AttributeStorageTest, Vec3StorageProperties) {
    EXPECT_EQ(vec3Storage->getName(), "color");
    EXPECT_EQ(vec3Storage->getType(), AttributeType::Vec3);
    EXPECT_EQ(vec3Storage->getElementSize(), sizeof(float));  // Component size
}

TEST_F(AttributeStorageTest, AllocateSlot) {
    size_t slot0 = floatStorage->allocateSlot();
    size_t slot1 = floatStorage->allocateSlot();

    EXPECT_EQ(slot0, 0);
    EXPECT_EQ(slot1, 1);
    EXPECT_EQ(floatStorage->getAllocatedSlots(), 2);
}

TEST_F(AttributeStorageTest, FreeAndReuseSlot) {
    size_t slot0 = floatStorage->allocateSlot();
    size_t slot1 = floatStorage->allocateSlot();

    floatStorage->freeSlot(slot0);
    EXPECT_EQ(floatStorage->getAllocatedSlots(), 1);

    // Next allocation should reuse freed slot
    size_t slot2 = floatStorage->allocateSlot();
    EXPECT_EQ(slot2, slot0);  // Should get slot0 back
}

TEST_F(AttributeStorageTest, SlotDataAccess) {
    size_t slot = floatStorage->allocateSlot();
    auto view = floatStorage->getSlotView<float>(slot);

    // Should have 512 elements per slot
    EXPECT_EQ(view.size(), AttributeStorage::VOXELS_PER_BRICK);

    // Write and read back
    view[0] = 1.5f;
    view[511] = 2.5f;

    EXPECT_FLOAT_EQ(view[0], 1.5f);
    EXPECT_FLOAT_EQ(view[511], 2.5f);
}

TEST_F(AttributeStorageTest, ReserveCapacity) {
    floatStorage->reserve(100);

    // Should be able to allocate 100 slots without reallocation
    for (int i = 0; i < 100; ++i) {
        floatStorage->allocateSlot();
    }

    EXPECT_EQ(floatStorage->getAllocatedSlots(), 100);
}
