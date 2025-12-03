#include <gtest/gtest.h>
#include "RM.h"
#include <string>

using namespace ResourceManagement;

// Test fixture
class ResourceManagementTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// ============================================================================
// Basic Value Access Tests
// ============================================================================

TEST_F(ResourceManagementTest, DefaultConstructorUninitialized) {
    RM<int> resource;
    EXPECT_FALSE(resource.Ready());
    EXPECT_FALSE(resource);
}

TEST_F(ResourceManagementTest, ValueConstructorReady) {
    RM<int> resource(42);
    EXPECT_TRUE(resource.Ready());
    EXPECT_TRUE(resource);
    EXPECT_EQ(resource.Value(), 42);
}

TEST_F(ResourceManagementTest, SetValueMarksReady) {
    RM<int> resource;
    EXPECT_FALSE(resource.Ready());

    resource.Set(100);
    EXPECT_TRUE(resource.Ready());
    EXPECT_EQ(resource.Value(), 100);
}

TEST_F(ResourceManagementTest, ValueOrReturnsDefault) {
    RM<int> resource;
    EXPECT_EQ(resource.ValueOr(99), 99);

    resource.Set(42);
    EXPECT_EQ(resource.ValueOr(99), 42);
}

TEST_F(ResourceManagementTest, ValueThrowsWhenNotReady) {
    RM<int> resource;
    EXPECT_THROW(resource.Value(), std::runtime_error);
}

TEST_F(ResourceManagementTest, PointerOperators) {
    struct TestStruct {
        int x = 10;
        int y = 20;
    };

    RM<TestStruct> resource(TestStruct{});
    EXPECT_EQ(resource->x, 10);
    EXPECT_EQ(resource->y, 20);

    resource->x = 30;
    EXPECT_EQ(resource->x, 30);
}

TEST_F(ResourceManagementTest, DereferenceOperator) {
    RM<int> resource(42);
    EXPECT_EQ(*resource, 42);

    *resource = 100;
    EXPECT_EQ(*resource, 100);
}

// ============================================================================
// State Management Tests
// ============================================================================

TEST_F(ResourceManagementTest, InitialStateUninitialized) {
    RM<int> resource;
    EXPECT_EQ(resource.GetState(), ResourceState::Uninitialized);
}

TEST_F(ResourceManagementTest, SetStateChangesState) {
    RM<int> resource(42);
    EXPECT_TRUE(resource.Has(ResourceState::Ready));

    resource.SetState(ResourceState::Outdated);
    EXPECT_EQ(resource.GetState(), ResourceState::Outdated);
    EXPECT_FALSE(resource.Has(ResourceState::Ready));
}

TEST_F(ResourceManagementTest, AddStatePreservesExisting) {
    RM<int> resource(42);
    resource.AddState(ResourceState::Locked);

    EXPECT_TRUE(resource.Has(ResourceState::Ready));
    EXPECT_TRUE(resource.Has(ResourceState::Locked));
}

TEST_F(ResourceManagementTest, RemoveStateKeepsOthers) {
    RM<int> resource(42);
    resource.AddState(ResourceState::Locked);

    EXPECT_TRUE(resource.Has(ResourceState::Ready));
    EXPECT_TRUE(resource.Has(ResourceState::Locked));

    resource.RemoveState(ResourceState::Locked);
    EXPECT_TRUE(resource.Has(ResourceState::Ready));
    EXPECT_FALSE(resource.Has(ResourceState::Locked));
}

TEST_F(ResourceManagementTest, MarkOutdatedRemovesReady) {
    RM<int> resource(42);
    EXPECT_TRUE(resource.Ready());

    resource.MarkOutdated();
    EXPECT_FALSE(resource.Ready());
    EXPECT_TRUE(resource.Has(ResourceState::Outdated));
}

TEST_F(ResourceManagementTest, MarkReadyRemovesOutdated) {
    RM<int> resource(42);
    resource.MarkOutdated();
    EXPECT_FALSE(resource.Ready());

    resource.MarkReady();
    EXPECT_TRUE(resource.Ready());
    EXPECT_FALSE(resource.Has(ResourceState::Outdated));
}

TEST_F(ResourceManagementTest, LockUnlockWorks) {
    RM<int> resource(42);
    EXPECT_FALSE(resource.IsLocked());

    resource.Lock();
    EXPECT_TRUE(resource.IsLocked());

    resource.Unlock();
    EXPECT_FALSE(resource.IsLocked());
}

// ============================================================================
// Generation Tracking Tests
// ============================================================================

TEST_F(ResourceManagementTest, InitialGenerationZero) {
    RM<int> resource;
    EXPECT_EQ(resource.GetGeneration(), 0);
}

TEST_F(ResourceManagementTest, SetIncrementsGeneration) {
    RM<int> resource;
    EXPECT_EQ(resource.GetGeneration(), 0);

    resource.Set(10);
    EXPECT_EQ(resource.GetGeneration(), 1);

    resource.Set(20);
    EXPECT_EQ(resource.GetGeneration(), 2);
}

TEST_F(ResourceManagementTest, ManualGenerationIncrement) {
    RM<int> resource(42);
    uint64_t gen = resource.GetGeneration();

    resource.IncrementGeneration();
    EXPECT_EQ(resource.GetGeneration(), gen + 1);
}

// ============================================================================
// Metadata Tests
// ============================================================================

TEST_F(ResourceManagementTest, SetAndGetMetadata) {
    RM<int> resource(42);
    resource.SetMetadata("name", std::string("test_resource"));
    resource.SetMetadata("count", 100);

    EXPECT_EQ(resource.GetMetadata<std::string>("name"), "test_resource");
    EXPECT_EQ(resource.GetMetadata<int>("count"), 100);
}

TEST_F(ResourceManagementTest, HasMetadataWorks) {
    RM<int> resource(42);
    EXPECT_FALSE(resource.HasMetadata("key"));

    resource.SetMetadata("key", 123);
    EXPECT_TRUE(resource.HasMetadata("key"));
}

TEST_F(ResourceManagementTest, GetMetadataThrowsOnMissing) {
    RM<int> resource(42);
    EXPECT_THROW(resource.GetMetadata<int>("missing"), std::runtime_error);
}

TEST_F(ResourceManagementTest, GetMetadataOrReturnsDefault) {
    RM<int> resource(42);
    EXPECT_EQ(resource.GetMetadataOr("missing", 999), 999);

    resource.SetMetadata("key", 123);
    EXPECT_EQ(resource.GetMetadataOr("key", 999), 123);
}

TEST_F(ResourceManagementTest, RemoveMetadataWorks) {
    RM<int> resource(42);
    resource.SetMetadata("key", 123);
    EXPECT_TRUE(resource.HasMetadata("key"));

    resource.RemoveMetadata("key");
    EXPECT_FALSE(resource.HasMetadata("key"));
}

TEST_F(ResourceManagementTest, ClearMetadataRemovesAll) {
    RM<int> resource(42);
    resource.SetMetadata("key1", 1);
    resource.SetMetadata("key2", 2);
    resource.SetMetadata("key3", 3);

    resource.ClearMetadata();
    EXPECT_FALSE(resource.HasMetadata("key1"));
    EXPECT_FALSE(resource.HasMetadata("key2"));
    EXPECT_FALSE(resource.HasMetadata("key3"));
}

// ============================================================================
// Reset Tests
// ============================================================================

TEST_F(ResourceManagementTest, ResetClearsEverything) {
    RM<int> resource(42);
    resource.SetMetadata("test", 123);
    resource.Lock();

    EXPECT_TRUE(resource.Ready());
    EXPECT_TRUE(resource.HasMetadata("test"));
    EXPECT_TRUE(resource.IsLocked());

    resource.Reset();

    EXPECT_FALSE(resource.Ready());
    EXPECT_EQ(resource.GetState(), ResourceState::Uninitialized);
    EXPECT_FALSE(resource.HasMetadata("test"));
}

// ============================================================================
// Complex Type Tests
// ============================================================================

TEST_F(ResourceManagementTest, WorksWithComplexTypes) {
    struct ComplexType {
        std::string name;
        std::vector<int> values;

        ComplexType(std::string n, std::vector<int> v)
            : name(std::move(n)), values(std::move(v)) {}
    };

    RM<ComplexType> resource;
    resource.Set(ComplexType("test", {1, 2, 3}));

    EXPECT_TRUE(resource.Ready());
    EXPECT_EQ(resource->name, "test");
    EXPECT_EQ(resource->values.size(), 3);
    EXPECT_EQ(resource->values[0], 1);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
