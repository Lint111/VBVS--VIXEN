#include <gtest/gtest.h>
#include "RelationshipObserver.h"
#include <gaia.h>

using namespace GaiaArchetype;

// ============================================================================
// RelationshipObserver Tests
// ============================================================================

class RelationshipObserverTest : public ::testing::Test {
protected:
    gaia::ecs::World world;
    std::unique_ptr<RelationshipObserver> observer;
    std::unique_ptr<RelationshipTypeRegistry> types;
    gaia::ecs::Entity partOfTag;

    void SetUp() override {
        observer = std::make_unique<RelationshipObserver>(world);
        types = std::make_unique<RelationshipTypeRegistry>(world);
        partOfTag = types->partOf();
    }
};

TEST_F(RelationshipObserverTest, AddRelationshipTriggersCallback) {
    auto source = world.add();
    auto target = world.add();

    bool callbackTriggered = false;
    gaia::ecs::Entity callbackSource;
    gaia::ecs::Entity callbackTarget;

    observer->onRelationshipAdded(partOfTag, [&](const auto& ctx) {
        callbackTriggered = true;
        callbackSource = ctx.source;
        callbackTarget = ctx.target;
    });

    observer->addRelationship(source, target, partOfTag);

    EXPECT_TRUE(callbackTriggered);
    EXPECT_EQ(callbackSource, source);
    EXPECT_EQ(callbackTarget, target);
}

TEST_F(RelationshipObserverTest, RemoveRelationshipTriggersCallback) {
    auto source = world.add();
    auto target = world.add();

    bool removedCallbackTriggered = false;

    observer->onRelationshipRemoved(partOfTag, [&](const auto&) {
        removedCallbackTriggered = true;
    });

    // First add the relationship
    observer->addRelationship(source, target, partOfTag);
    EXPECT_FALSE(removedCallbackTriggered);

    // Now remove it
    observer->removeRelationship(source, target, partOfTag);
    EXPECT_TRUE(removedCallbackTriggered);
}

TEST_F(RelationshipObserverTest, HasRelationship) {
    auto source = world.add();
    auto target = world.add();

    EXPECT_FALSE(observer->hasRelationship(source, target, partOfTag));

    observer->addRelationship(source, target, partOfTag);
    EXPECT_TRUE(observer->hasRelationship(source, target, partOfTag));

    observer->removeRelationship(source, target, partOfTag);
    EXPECT_FALSE(observer->hasRelationship(source, target, partOfTag));
}

TEST_F(RelationshipObserverTest, BatchAddTriggersCallback) {
    std::vector<gaia::ecs::Entity> sources;
    for (int i = 0; i < 20; ++i) {
        sources.push_back(world.add());
    }
    auto target = world.add();

    size_t batchSize = 0;
    observer->onBatchAdded(partOfTag, [&](const auto& ctx) {
        batchSize = ctx.sources.size();
    });

    observer->addRelationshipBatch(sources, target, partOfTag);

    EXPECT_EQ(batchSize, 20);
}

TEST_F(RelationshipObserverTest, BatchBelowThresholdUsesIndividualCallbacks) {
    std::vector<gaia::ecs::Entity> sources;
    for (int i = 0; i < 5; ++i) {
        sources.push_back(world.add());
    }
    auto target = world.add();

    int individualCallCount = 0;
    bool batchCallTriggered = false;

    observer->onRelationshipAdded(partOfTag, [&](const auto&) {
        individualCallCount++;
    });

    observer->onBatchAdded(partOfTag, [&](const auto&) {
        batchCallTriggered = true;
    });

    // Default threshold is 16, so 5 entities should use individual callbacks
    observer->addRelationshipBatch(sources, target, partOfTag);

    EXPECT_EQ(individualCallCount, 5);
    EXPECT_FALSE(batchCallTriggered);
}

TEST_F(RelationshipObserverTest, GetSourcesForTarget) {
    auto target = world.add();
    std::vector<gaia::ecs::Entity> sources;

    for (int i = 0; i < 5; ++i) {
        auto source = world.add();
        sources.push_back(source);
        observer->addRelationship(source, target, partOfTag);
    }

    auto retrieved = observer->getSourcesFor(target, partOfTag);

    EXPECT_EQ(retrieved.size(), 5);
}

TEST_F(RelationshipObserverTest, UnregisterCallback) {
    auto source = world.add();
    auto target = world.add();

    int callCount = 0;
    auto handle = observer->onRelationshipAdded(partOfTag, [&](const auto&) {
        callCount++;
    });

    observer->addRelationship(source, target, partOfTag);
    EXPECT_EQ(callCount, 1);

    // Unregister and verify no more calls
    observer->unregisterCallback(handle);

    auto source2 = world.add();
    observer->addRelationship(source2, target, partOfTag);
    EXPECT_EQ(callCount, 1);  // Should still be 1
}

TEST_F(RelationshipObserverTest, MultipleCallbacksForSameRelationship) {
    auto source = world.add();
    auto target = world.add();

    int callback1Count = 0;
    int callback2Count = 0;

    observer->onRelationshipAdded(partOfTag, [&](const auto&) {
        callback1Count++;
    });

    observer->onRelationshipAdded(partOfTag, [&](const auto&) {
        callback2Count++;
    });

    observer->addRelationship(source, target, partOfTag);

    EXPECT_EQ(callback1Count, 1);
    EXPECT_EQ(callback2Count, 1);
}

TEST_F(RelationshipObserverTest, DeferredModeQueuesOperations) {
    auto source = world.add();
    auto target = world.add();

    int callCount = 0;
    observer->onRelationshipAdded(partOfTag, [&](const auto&) {
        callCount++;
    });

    observer->setDeferredMode(true);

    observer->addRelationship(source, target, partOfTag);
    EXPECT_EQ(callCount, 0);  // Not called yet

    observer->flush();
    EXPECT_EQ(callCount, 1);  // Now called
}

// ============================================================================
// RelationshipTypeRegistry Tests
// ============================================================================

class RelationshipTypeRegistryTest : public ::testing::Test {
protected:
    gaia::ecs::World world;
    std::unique_ptr<RelationshipTypeRegistry> registry;

    void SetUp() override {
        registry = std::make_unique<RelationshipTypeRegistry>(world);
    }
};

TEST_F(RelationshipTypeRegistryTest, GetOrCreateReturnsConsistentTag) {
    auto tag1 = registry->getOrCreate("myrelation");
    auto tag2 = registry->getOrCreate("myrelation");

    EXPECT_EQ(tag1, tag2);
}

TEST_F(RelationshipTypeRegistryTest, DifferentNamesReturnDifferentTags) {
    auto tag1 = registry->getOrCreate("relation1");
    auto tag2 = registry->getOrCreate("relation2");

    EXPECT_NE(tag1, tag2);
}

TEST_F(RelationshipTypeRegistryTest, ExistsReturnsCorrectly) {
    EXPECT_FALSE(registry->exists("myrelation"));

    registry->getOrCreate("myrelation");

    EXPECT_TRUE(registry->exists("myrelation"));
}

TEST_F(RelationshipTypeRegistryTest, GetNameReturnsCorrectName) {
    auto tag = registry->getOrCreate("myrelation");

    auto name = registry->getName(tag);

    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "myrelation");
}

TEST_F(RelationshipTypeRegistryTest, PartOfReturnsSameTag) {
    auto tag1 = registry->partOf();
    auto tag2 = registry->partOf();

    EXPECT_EQ(tag1, tag2);
    EXPECT_TRUE(registry->exists("partof"));
}

TEST_F(RelationshipTypeRegistryTest, ContainsReturnsSameTag) {
    auto tag1 = registry->contains();
    auto tag2 = registry->contains();

    EXPECT_EQ(tag1, tag2);
    EXPECT_TRUE(registry->exists("contains"));
}

TEST_F(RelationshipTypeRegistryTest, ChildOfReturnsGaiaBuiltIn) {
    auto tag = registry->childOf();

    EXPECT_EQ(tag, gaia::ecs::ChildOf);
}
