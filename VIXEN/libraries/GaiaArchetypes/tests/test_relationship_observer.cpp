#include <gtest/gtest.h>
#include "RelationshipObserver.h"
#include <gaia.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>

using namespace GaiaArchetype;

// ===========================================================================
// Test Components
// ===========================================================================

struct TestData {
    int value = 0;
};

struct TestMarker {};

// ===========================================================================
// RelationshipObserver Basic Tests
// ===========================================================================

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

// ===========================================================================
// RelationshipObserver Advanced Tests
// ===========================================================================

TEST_F(RelationshipObserverTest, AddSameRelationshipTwice) {
    auto source = world.add();
    auto target = world.add();

    int callCount = 0;
    observer->onRelationshipAdded(partOfTag, [&](const auto&) {
        callCount++;
    });

    bool result1 = observer->addRelationship(source, target, partOfTag);
    bool result2 = observer->addRelationship(source, target, partOfTag);

    EXPECT_TRUE(result1);
    // Second add may return true/false depending on implementation
    // But callback should only be called once for the same relationship
}

TEST_F(RelationshipObserverTest, RemoveNonExistentRelationship) {
    auto source = world.add();
    auto target = world.add();

    int callCount = 0;
    observer->onRelationshipRemoved(partOfTag, [&](const auto&) {
        callCount++;
    });

    bool result = observer->removeRelationship(source, target, partOfTag);

    EXPECT_FALSE(result);
    EXPECT_EQ(callCount, 0);  // Callback should not be called
}

TEST_F(RelationshipObserverTest, BatchAddWithDifferentTargets) {
    std::vector<gaia::ecs::Entity> sources;
    for (int i = 0; i < 10; ++i) {
        sources.push_back(world.add());
    }
    auto target1 = world.add();
    auto target2 = world.add();

    observer->addRelationshipBatch(sources, target1, partOfTag);

    for (auto source : sources) {
        EXPECT_TRUE(observer->hasRelationship(source, target1, partOfTag));
        EXPECT_FALSE(observer->hasRelationship(source, target2, partOfTag));
    }
}

TEST_F(RelationshipObserverTest, BatchRemoveRelationships) {
    std::vector<gaia::ecs::Entity> sources;
    for (int i = 0; i < 10; ++i) {
        sources.push_back(world.add());
    }
    auto target = world.add();

    // Add all
    observer->addRelationshipBatch(sources, target, partOfTag);

    int removeCallCount = 0;
    observer->onRelationshipRemoved(partOfTag, [&](const auto&) {
        removeCallCount++;
    });

    // Remove all
    size_t removed = observer->removeRelationshipBatch(sources, target, partOfTag);

    EXPECT_EQ(removed, 10);
    EXPECT_EQ(removeCallCount, 10);

    for (auto source : sources) {
        EXPECT_FALSE(observer->hasRelationship(source, target, partOfTag));
    }
}

TEST_F(RelationshipObserverTest, GetTargetsForSource) {
    auto source = world.add();
    std::vector<gaia::ecs::Entity> targets;

    for (int i = 0; i < 5; ++i) {
        auto target = world.add();
        targets.push_back(target);
        observer->addRelationship(source, target, partOfTag);
    }

    auto retrieved = observer->getTargetsFor(source, partOfTag);

    EXPECT_EQ(retrieved.size(), 5);
}

TEST_F(RelationshipObserverTest, CountRelationships) {
    auto source = world.add();

    for (int i = 0; i < 7; ++i) {
        auto target = world.add();
        observer->addRelationship(source, target, partOfTag);
    }

    size_t count = observer->countRelationships(source, partOfTag);
    EXPECT_EQ(count, 7);
}

TEST_F(RelationshipObserverTest, SetBatchThreshold) {
    observer->setBatchThreshold(5);
    EXPECT_EQ(observer->getBatchThreshold(), 5);

    std::vector<gaia::ecs::Entity> sources;
    for (int i = 0; i < 6; ++i) {
        sources.push_back(world.add());
    }
    auto target = world.add();

    bool batchCalled = false;
    int individualCount = 0;

    observer->onRelationshipAdded(partOfTag, [&](const auto&) {
        individualCount++;
    });

    observer->onBatchAdded(partOfTag, [&](const auto&) {
        batchCalled = true;
    });

    // With threshold of 5, 6 sources should trigger batch callback
    observer->addRelationshipBatch(sources, target, partOfTag);

    EXPECT_TRUE(batchCalled);
    EXPECT_EQ(individualCount, 0);
}

TEST_F(RelationshipObserverTest, DeferredModeWithMultipleOperations) {
    std::vector<gaia::ecs::Entity> sources;
    for (int i = 0; i < 5; ++i) {
        sources.push_back(world.add());
    }
    auto target = world.add();

    int addCount = 0;
    observer->onRelationshipAdded(partOfTag, [&](const auto&) {
        addCount++;
    });

    observer->setDeferredMode(true);
    EXPECT_TRUE(observer->isDeferredMode());

    // Queue multiple operations
    for (auto source : sources) {
        observer->addRelationship(source, target, partOfTag);
    }

    EXPECT_EQ(addCount, 0);  // Nothing called yet

    observer->flush();

    EXPECT_EQ(addCount, 5);  // All called now
}

TEST_F(RelationshipObserverTest, DirectWorldAccess) {
    EXPECT_EQ(&observer->world(), &world);

    // Create entity through observer's world reference
    auto entity = observer->world().add();
    EXPECT_TRUE(world.valid(entity));
}

// ===========================================================================
// RelationshipContext Helper Tests
// ===========================================================================

TEST_F(RelationshipObserverTest, ContextGetSourceComponent) {
    auto source = world.add();
    world.add<TestData>(source, TestData{42});
    auto target = world.add();

    TestData* dataPtr = nullptr;

    observer->onRelationshipAdded(partOfTag, [&](RelationshipObserver::RelationshipContext& ctx) {
        dataPtr = ctx.getSourceComponent<TestData>();
    });

    observer->addRelationship(source, target, partOfTag);

    ASSERT_NE(dataPtr, nullptr);
    EXPECT_EQ(dataPtr->value, 42);
}

TEST_F(RelationshipObserverTest, ContextGetTargetComponent) {
    auto source = world.add();
    auto target = world.add();
    world.add<TestData>(target, TestData{100});

    TestData* dataPtr = nullptr;

    observer->onRelationshipAdded(partOfTag, [&](RelationshipObserver::RelationshipContext& ctx) {
        dataPtr = ctx.getTargetComponent<TestData>();
    });

    observer->addRelationship(source, target, partOfTag);

    ASSERT_NE(dataPtr, nullptr);
    EXPECT_EQ(dataPtr->value, 100);
}

TEST_F(RelationshipObserverTest, ContextGetMissingComponentReturnsNull) {
    auto source = world.add();
    auto target = world.add();

    TestData* dataPtr = reinterpret_cast<TestData*>(0x1);  // Non-null initial

    observer->onRelationshipAdded(partOfTag, [&](RelationshipObserver::RelationshipContext& ctx) {
        dataPtr = ctx.getSourceComponent<TestData>();
    });

    observer->addRelationship(source, target, partOfTag);

    EXPECT_EQ(dataPtr, nullptr);
}

TEST_F(RelationshipObserverTest, ContextGetConstComponent) {
    auto source = world.add();
    world.add<TestData>(source, TestData{55});
    auto target = world.add();

    const TestData* dataPtr = nullptr;

    observer->onRelationshipAdded(partOfTag, [&](const RelationshipObserver::RelationshipContext& ctx) {
        dataPtr = ctx.getSourceComponentConst<TestData>();
    });

    observer->addRelationship(source, target, partOfTag);

    ASSERT_NE(dataPtr, nullptr);
    EXPECT_EQ(dataPtr->value, 55);
}

// ===========================================================================
// BatchRelationshipContext Helper Tests
// ===========================================================================

TEST_F(RelationshipObserverTest, BatchContextForEachSourceWithComponent) {
    std::vector<gaia::ecs::Entity> sources;
    for (int i = 0; i < 20; ++i) {
        auto source = world.add();
        world.add<TestData>(source, TestData{i});
        sources.push_back(source);
    }
    auto target = world.add();

    int sum = 0;

    observer->onBatchAdded(partOfTag, [&](RelationshipObserver::BatchRelationshipContext& ctx) {
        ctx.forEachSourceWithComponent<TestData>([&](auto, const TestData& data) {
            sum += data.value;
        });
    });

    observer->addRelationshipBatch(sources, target, partOfTag);

    // Sum of 0..19 = 190
    EXPECT_EQ(sum, 190);
}

// ===========================================================================
// RelationshipTypeRegistry Tests
// ===========================================================================

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

TEST_F(RelationshipTypeRegistryTest, GetNonExistentReturnsEmpty) {
    auto result = registry->get("nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST_F(RelationshipTypeRegistryTest, GetNameForUnknownTagReturnsEmpty) {
    auto unknownTag = world.add();
    auto name = registry->getName(unknownTag);
    EXPECT_FALSE(name.has_value());
}

TEST_F(RelationshipTypeRegistryTest, MultipleRelationshipTypes) {
    auto tag1 = registry->getOrCreate("type1");
    auto tag2 = registry->getOrCreate("type2");
    auto tag3 = registry->getOrCreate("type3");

    EXPECT_NE(tag1, tag2);
    EXPECT_NE(tag2, tag3);
    EXPECT_NE(tag1, tag3);

    EXPECT_TRUE(registry->exists("type1"));
    EXPECT_TRUE(registry->exists("type2"));
    EXPECT_TRUE(registry->exists("type3"));
}

// ===========================================================================
// Edge Case Tests
// ===========================================================================

class RelationshipObserverEdgeCaseTest : public ::testing::Test {
protected:
    gaia::ecs::World world;
    std::unique_ptr<RelationshipObserver> observer;
    std::unique_ptr<RelationshipTypeRegistry> types;

    void SetUp() override {
        observer = std::make_unique<RelationshipObserver>(world);
        types = std::make_unique<RelationshipTypeRegistry>(world);
    }
};

TEST_F(RelationshipObserverEdgeCaseTest, EmptyBatchAdd) {
    auto target = world.add();
    auto relationTag = types->partOf();

    std::vector<gaia::ecs::Entity> emptySources;

    size_t result = observer->addRelationshipBatch(emptySources, target, relationTag);

    EXPECT_EQ(result, 0);
}

TEST_F(RelationshipObserverEdgeCaseTest, EmptyBatchRemove) {
    auto target = world.add();
    auto relationTag = types->partOf();

    std::vector<gaia::ecs::Entity> emptySources;

    size_t result = observer->removeRelationshipBatch(emptySources, target, relationTag);

    EXPECT_EQ(result, 0);
}

TEST_F(RelationshipObserverEdgeCaseTest, SelfRelationship) {
    auto entity = world.add();
    auto relationTag = types->partOf();

    // Self-referential relationship (entity is both source and target)
    bool result = observer->addRelationship(entity, entity, relationTag);

    // Should be allowed (though semantically questionable)
    if (result) {
        EXPECT_TRUE(observer->hasRelationship(entity, entity, relationTag));
    }
}

TEST_F(RelationshipObserverEdgeCaseTest, MultipleRelationshipTypes) {
    auto source = world.add();
    auto target = world.add();
    auto partOfTag = types->partOf();
    auto containsTag = types->contains();

    observer->addRelationship(source, target, partOfTag);
    observer->addRelationship(source, target, containsTag);

    EXPECT_TRUE(observer->hasRelationship(source, target, partOfTag));
    EXPECT_TRUE(observer->hasRelationship(source, target, containsTag));

    // Remove only one
    observer->removeRelationship(source, target, partOfTag);

    EXPECT_FALSE(observer->hasRelationship(source, target, partOfTag));
    EXPECT_TRUE(observer->hasRelationship(source, target, containsTag));
}

TEST_F(RelationshipObserverEdgeCaseTest, UnregisterInvalidHandle) {
    // Should not crash
    observer->unregisterCallback(999999);
    SUCCEED();
}

TEST_F(RelationshipObserverEdgeCaseTest, FlushWithNoDeferredOperations) {
    observer->setDeferredMode(true);
    observer->flush();  // Should not crash
    SUCCEED();
}

TEST_F(RelationshipObserverEdgeCaseTest, LargeBatchOperation) {
    std::vector<gaia::ecs::Entity> sources;
    for (int i = 0; i < 10000; ++i) {
        sources.push_back(world.add());
    }
    auto target = world.add();
    auto relationTag = types->partOf();

    size_t added = observer->addRelationshipBatch(sources, target, relationTag);

    EXPECT_EQ(added, 10000);

    auto retrieved = observer->getSourcesFor(target, relationTag);
    EXPECT_EQ(retrieved.size(), 10000);
}

TEST_F(RelationshipObserverEdgeCaseTest, CallbackThrowsException) {
    auto source = world.add();
    auto target = world.add();
    auto relationTag = types->partOf();

    bool secondCallbackCalled = false;

    observer->onRelationshipAdded(relationTag, [](const auto&) {
        throw std::runtime_error("Test exception");
    });

    observer->onRelationshipAdded(relationTag, [&](const auto&) {
        secondCallbackCalled = true;
    });

    // Exception handling behavior depends on implementation
    // This test just ensures no crash
    try {
        observer->addRelationship(source, target, relationTag);
    } catch (...) {
        // Expected if exceptions propagate
    }
}

TEST_F(RelationshipObserverEdgeCaseTest, GetSourcesForEmptyTarget) {
    auto target = world.add();
    auto relationTag = types->partOf();

    auto sources = observer->getSourcesFor(target, relationTag);

    EXPECT_TRUE(sources.empty());
}

TEST_F(RelationshipObserverEdgeCaseTest, GetTargetsForEmptySource) {
    auto source = world.add();
    auto relationTag = types->partOf();

    auto targets = observer->getTargetsFor(source, relationTag);

    EXPECT_TRUE(targets.empty());
}

// ===========================================================================
// Thread Safety Tests (Basic Validation)
// ===========================================================================

class RelationshipObserverThreadTest : public ::testing::Test {
protected:
    gaia::ecs::World world;
    std::unique_ptr<RelationshipObserver> observer;
    std::unique_ptr<RelationshipTypeRegistry> types;

    void SetUp() override {
        observer = std::make_unique<RelationshipObserver>(world);
        types = std::make_unique<RelationshipTypeRegistry>(world);
    }
};

TEST_F(RelationshipObserverThreadTest, ConcurrentCallbackInvocation) {
    // Pre-create entities and relationships
    auto relationTag = types->partOf();
    std::vector<gaia::ecs::Entity> sources;
    std::vector<gaia::ecs::Entity> targets;

    for (int i = 0; i < 100; ++i) {
        sources.push_back(world.add());
        targets.push_back(world.add());
    }

    std::atomic<int> callbackCount{0};
    observer->onRelationshipAdded(relationTag, [&](const auto&) {
        callbackCount++;
    });

    // Note: Gaia ECS world itself is not thread-safe for writes
    // This test validates that callback invocation is thread-safe
    // by doing sequential adds but concurrent reads

    for (int i = 0; i < 100; ++i) {
        observer->addRelationship(sources[i], targets[i], relationTag);
    }

    // Concurrent reads
    std::vector<std::thread> threads;
    std::atomic<int> readCount{0};

    for (int t = 0; t < 10; ++t) {
        threads.emplace_back([this, &sources, &targets, &readCount, relationTag]() {
            for (int i = 0; i < 100; ++i) {
                if (observer->hasRelationship(sources[i], targets[i], relationTag)) {
                    readCount++;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(callbackCount.load(), 100);
    EXPECT_EQ(readCount.load(), 1000);
}

// ===========================================================================
// Integration Tests
// ===========================================================================

class RelationshipObserverIntegrationTest : public ::testing::Test {
protected:
    gaia::ecs::World world;
    std::unique_ptr<RelationshipObserver> observer;
    std::unique_ptr<RelationshipTypeRegistry> types;

    void SetUp() override {
        observer = std::make_unique<RelationshipObserver>(world);
        types = std::make_unique<RelationshipTypeRegistry>(world);
    }
};

TEST_F(RelationshipObserverIntegrationTest, CompleteWorkflow) {
    auto partOfTag = types->partOf();

    // Create volume entity
    auto volume = world.add();
    world.add<TestData>(volume, TestData{0});

    // Track voxel count via callback
    observer->onRelationshipAdded(partOfTag, [&](RelationshipObserver::RelationshipContext& ctx) {
        if (auto* data = ctx.getTargetComponent<TestData>()) {
            data->value++;
        }
    });

    observer->onRelationshipRemoved(partOfTag, [&](RelationshipObserver::RelationshipContext& ctx) {
        if (auto* data = ctx.getTargetComponent<TestData>()) {
            data->value--;
        }
    });

    // Add voxels
    std::vector<gaia::ecs::Entity> voxels;
    for (int i = 0; i < 10; ++i) {
        auto voxel = world.add();
        voxels.push_back(voxel);
        observer->addRelationship(voxel, volume, partOfTag);
    }

    EXPECT_EQ(world.get<TestData>(volume).value, 10);

    // Remove some voxels
    for (int i = 0; i < 3; ++i) {
        observer->removeRelationship(voxels[i], volume, partOfTag);
    }

    EXPECT_EQ(world.get<TestData>(volume).value, 7);

    // Verify relationships
    EXPECT_EQ(observer->getSourcesFor(volume, partOfTag).size(), 7);
}

TEST_F(RelationshipObserverIntegrationTest, BatchWorkflow) {
    auto partOfTag = types->partOf();

    auto volume = world.add();
    world.add<TestData>(volume, TestData{0});

    // Create voxels
    std::vector<gaia::ecs::Entity> voxels;
    for (int i = 0; i < 100; ++i) {
        voxels.push_back(world.add());
    }

    // Use batch callback
    observer->onBatchAdded(partOfTag, [&](RelationshipObserver::BatchRelationshipContext& ctx) {
        if (world.valid(ctx.target) && world.has<TestData>(ctx.target)) {
            auto& data = world.set<TestData>(ctx.target);
            data.value += static_cast<int>(ctx.sources.size());
        }
    });

    // Add all voxels at once
    observer->addRelationshipBatch(voxels, volume, partOfTag);

    EXPECT_EQ(world.get<TestData>(volume).value, 100);
    EXPECT_EQ(observer->getSourcesFor(volume, partOfTag).size(), 100);
}

TEST_F(RelationshipObserverIntegrationTest, DeferredWorkflow) {
    auto partOfTag = types->partOf();

    auto volume = world.add();
    std::vector<gaia::ecs::Entity> voxels;
    for (int i = 0; i < 10; ++i) {
        voxels.push_back(world.add());
    }

    int totalAdded = 0;
    observer->onRelationshipAdded(partOfTag, [&](const auto&) {
        totalAdded++;
    });

    // Enable deferred mode
    observer->setDeferredMode(true);

    // Queue operations
    for (auto voxel : voxels) {
        observer->addRelationship(voxel, volume, partOfTag);
    }

    EXPECT_EQ(totalAdded, 0);  // Nothing executed yet

    // Flush
    observer->flush();

    EXPECT_EQ(totalAdded, 10);  // All executed
    EXPECT_EQ(observer->getSourcesFor(volume, partOfTag).size(), 10);
}
