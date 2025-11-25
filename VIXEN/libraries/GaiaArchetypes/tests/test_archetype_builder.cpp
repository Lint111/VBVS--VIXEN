#include <gtest/gtest.h>
#include "ArchetypeBuilder.h"
#include <gaia.h>
#include <thread>
#include <atomic>
#include <vector>

using namespace GaiaArchetype;

// ===========================================================================
// Test Components
// ===========================================================================

struct TestPosition {
    float x = 0, y = 0, z = 0;
    bool operator==(const TestPosition& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct TestVelocity {
    float vx = 0, vy = 0, vz = 0;
};

struct TestHealth {
    int value = 100;
    int maxValue = 100;
};

struct TestMass {
    float mass = 1.0f;
};

struct TestTag {};  // Empty tag component

// ===========================================================================
// ArchetypeBuilder Basic Tests
// ===========================================================================

class ArchetypeBuilderTest : public ::testing::Test {
protected:
    gaia::ecs::World world;
};

TEST_F(ArchetypeBuilderTest, CreateSimpleArchetype) {
    auto archetype = ArchetypeBuilder("SimpleEntity")
        .withComponent<TestPosition>()
        .withComponent<TestHealth>()
        .build();

    EXPECT_EQ(archetype.name, "SimpleEntity");
    EXPECT_EQ(archetype.requiredComponents.size(), 2);
    EXPECT_EQ(archetype.requiredComponentTypes.size(), 2);
}

TEST_F(ArchetypeBuilderTest, CreateArchetypeWithInitialValues) {
    TestPosition initialPos{1.0f, 2.0f, 3.0f};

    auto archetype = ArchetypeBuilder("PositionedEntity")
        .withComponent<TestPosition>(initialPos)
        .build();

    EXPECT_EQ(archetype.name, "PositionedEntity");
    EXPECT_EQ(archetype.requiredComponents.size(), 1);
}

TEST_F(ArchetypeBuilderTest, CreateArchetypeWithRelationship) {
    auto partOfTag = world.add();
    RelationshipType partOf{partOfTag, "partof", false};

    bool hookCalled = false;
    auto archetype = ArchetypeBuilder("Container")
        .withComponent<TestPosition>()
        .acceptsRelationship(partOf)
            .onAdded([&hookCalled](auto&, auto, auto, auto&) {
                hookCalled = true;
            })
            .done()
        .build();

    EXPECT_EQ(archetype.name, "Container");
    EXPECT_TRUE(archetype.acceptsRelationship(partOf));
    EXPECT_NE(archetype.getHooks(partOf), nullptr);
}

TEST_F(ArchetypeBuilderTest, ArchetypeWithBundleHook) {
    auto partOfTag = world.add();
    RelationshipType partOf{partOfTag, "partof", false};

    size_t batchCount = 0;
    auto archetype = ArchetypeBuilder("BatchContainer")
        .withComponent<TestPosition>()
        .acceptsRelationship(partOf)
            .onBundleAdded([&batchCount](auto&, auto sources, auto, auto&) {
                batchCount = sources.size();
            })
            .bundleThreshold(8)
            .done()
        .build();

    auto hooks = archetype.getHooks(partOf);
    ASSERT_NE(hooks, nullptr);
    EXPECT_EQ(hooks->bundleThreshold, 8);
    EXPECT_TRUE(hooks->onBundleAdded != nullptr);
}

TEST_F(ArchetypeBuilderTest, CreateArchetypeWithOptionalComponent) {
    auto archetype = ArchetypeBuilder("OptionalEntity")
        .withComponent<TestPosition>()
        .withOptionalComponent<TestVelocity>()
        .build();

    EXPECT_EQ(archetype.requiredComponents.size(), 1);
    EXPECT_EQ(archetype.optionalComponents.size(), 1);
    EXPECT_EQ(archetype.requiredComponentTypes.size(), 1);
    EXPECT_EQ(archetype.optionalComponentTypes.size(), 1);
}

TEST_F(ArchetypeBuilderTest, CreateArchetypeWithAllHookTypes) {
    auto partOfTag = world.add();
    RelationshipType partOf{partOfTag, "partof", false};

    bool addCalled = false;
    bool removeCalled = false;
    bool bundleCalled = false;

    auto archetype = ArchetypeBuilder("FullHooks")
        .withComponent<TestPosition>()
        .acceptsRelationship(partOf)
            .onAdded([&addCalled](auto&, auto, auto, auto&) { addCalled = true; })
            .onRemoved([&removeCalled](auto&, auto, auto, auto&) { removeCalled = true; })
            .onBundleAdded([&bundleCalled](auto&, auto, auto, auto&) { bundleCalled = true; })
            .bundleThreshold(4)
            .done()
        .build();

    auto hooks = archetype.getHooks(partOf);
    ASSERT_NE(hooks, nullptr);
    EXPECT_TRUE(hooks->onAdded != nullptr);
    EXPECT_TRUE(hooks->onRemoved != nullptr);
    EXPECT_TRUE(hooks->onBundleAdded != nullptr);
    EXPECT_EQ(hooks->bundleThreshold, 4);
}

TEST_F(ArchetypeBuilderTest, CreateArchetypeWithSourceRelationship) {
    auto partOfTag = world.add();
    RelationshipType partOf{partOfTag, "partof", false};

    auto archetype = ArchetypeBuilder("SourceEntity")
        .withComponent<TestPosition>()
        .canRelate(partOf)
        .build();

    EXPECT_EQ(archetype.sourceRelationships.size(), 1);
    EXPECT_EQ(archetype.sourceRelationships[0], partOf);
}

TEST_F(ArchetypeBuilderTest, CreateArchetypeWithMultipleComponents) {
    auto archetype = ArchetypeBuilder("ComplexEntity")
        .withComponent<TestPosition>()
        .withComponent<TestVelocity>()
        .withComponent<TestHealth>()
        .withComponent<TestMass>()
        .build();

    EXPECT_EQ(archetype.requiredComponents.size(), 4);
    EXPECT_EQ(archetype.requiredComponentTypes.size(), 4);
}

TEST_F(ArchetypeBuilderTest, CreateArchetypeWithMultipleRelationships) {
    auto partOfTag = world.add();
    auto containsTag = world.add();
    RelationshipType partOf{partOfTag, "partof", false};
    RelationshipType contains{containsTag, "contains", false};

    auto archetype = ArchetypeBuilder("MultiRelation")
        .withComponent<TestPosition>()
        .acceptsRelationship(partOf)
            .onAdded([](auto&, auto, auto, auto&) {})
            .done()
        .acceptsRelationship(contains)
            .onAdded([](auto&, auto, auto, auto&) {})
            .done()
        .build();

    EXPECT_TRUE(archetype.acceptsRelationship(partOf));
    EXPECT_TRUE(archetype.acceptsRelationship(contains));
}

TEST_F(ArchetypeBuilderTest, ArchetypeDoesNotAcceptUnregisteredRelationship) {
    auto partOfTag = world.add();
    auto otherTag = world.add();
    RelationshipType partOf{partOfTag, "partof", false};
    RelationshipType other{otherTag, "other", false};

    auto archetype = ArchetypeBuilder("SingleRelation")
        .withComponent<TestPosition>()
        .acceptsRelationship(partOf)
            .onAdded([](auto&, auto, auto, auto&) {})
            .done()
        .build();

    EXPECT_TRUE(archetype.acceptsRelationship(partOf));
    EXPECT_FALSE(archetype.acceptsRelationship(other));
}

TEST_F(ArchetypeBuilderTest, EmptyArchetype) {
    auto archetype = ArchetypeBuilder("EmptyArchetype")
        .build();

    EXPECT_EQ(archetype.name, "EmptyArchetype");
    EXPECT_TRUE(archetype.requiredComponents.empty());
    EXPECT_TRUE(archetype.optionalComponents.empty());
    EXPECT_TRUE(archetype.acceptedRelationships.empty());
}

TEST_F(ArchetypeBuilderTest, ArchetypeWithTagComponent) {
    auto archetype = ArchetypeBuilder("TaggedEntity")
        .withComponent<TestTag>()
        .build();

    EXPECT_EQ(archetype.requiredComponents.size(), 1);
}

// ===========================================================================
// ArchetypeBuilder Fluent API Tests
// ===========================================================================

TEST_F(ArchetypeBuilderTest, FluentChaining) {
    auto tag = world.add();
    RelationshipType rel{tag, "test", false};

    // Test that all methods chain correctly
    auto archetype = ArchetypeBuilder("FluentTest")
        .withComponent<TestPosition>()
        .withComponent<TestVelocity>()
        .withOptionalComponent<TestHealth>()
        .acceptsRelationship(rel)
            .onAdded([](auto&, auto, auto, auto&) {})
            .onRemoved([](auto&, auto, auto, auto&) {})
            .onBundleAdded([](auto&, auto, auto, auto&) {})
            .bundleThreshold(32)
            .done()
        .canRelate(rel)
        .build();

    EXPECT_EQ(archetype.name, "FluentTest");
    EXPECT_EQ(archetype.requiredComponents.size(), 2);
    EXPECT_EQ(archetype.optionalComponents.size(), 1);
    EXPECT_TRUE(archetype.acceptsRelationship(rel));
    EXPECT_EQ(archetype.sourceRelationships.size(), 1);
}

TEST_F(ArchetypeBuilderTest, ImplicitDoneConversion) {
    auto tag = world.add();
    RelationshipType rel{tag, "test", false};

    // Test implicit conversion from RelationshipConfigBuilder to ArchetypeBuilder
    auto archetype = ArchetypeBuilder("ImplicitConversion")
        .withComponent<TestPosition>()
        .acceptsRelationship(rel)
            .onAdded([](auto&, auto, auto, auto&) {})
        .build();  // No explicit done() call

    EXPECT_TRUE(archetype.acceptsRelationship(rel));
}

// ===========================================================================
// ArchetypeRegistry Tests
// ===========================================================================

class ArchetypeRegistryTest : public ::testing::Test {
protected:
    ArchetypeRegistry registry;
};

TEST_F(ArchetypeRegistryTest, RegisterAndRetrieveArchetype) {
    auto archetype = ArchetypeBuilder("TestArchetype")
        .withComponent<TestPosition>()
        .build();

    registry.registerArchetype(std::move(archetype));

    EXPECT_TRUE(registry.hasArchetype("TestArchetype"));
    EXPECT_NE(registry.getArchetype("TestArchetype"), nullptr);
}

TEST_F(ArchetypeRegistryTest, ArchetypeNotFound) {
    EXPECT_FALSE(registry.hasArchetype("NonExistent"));
    EXPECT_EQ(registry.getArchetype("NonExistent"), nullptr);
}

TEST_F(ArchetypeRegistryTest, RegisterMultipleArchetypes) {
    registry.registerArchetype(ArchetypeBuilder("Type1").withComponent<TestPosition>().build());
    registry.registerArchetype(ArchetypeBuilder("Type2").withComponent<TestVelocity>().build());
    registry.registerArchetype(ArchetypeBuilder("Type3").withComponent<TestHealth>().build());

    EXPECT_TRUE(registry.hasArchetype("Type1"));
    EXPECT_TRUE(registry.hasArchetype("Type2"));
    EXPECT_TRUE(registry.hasArchetype("Type3"));
    EXPECT_EQ(registry.archetypes().size(), 3);
}

TEST_F(ArchetypeRegistryTest, OverwriteArchetype) {
    registry.registerArchetype(ArchetypeBuilder("Test").withComponent<TestPosition>().build());
    registry.registerArchetype(ArchetypeBuilder("Test").withComponent<TestVelocity>().build());

    EXPECT_TRUE(registry.hasArchetype("Test"));
    // Should have the newer definition (with Velocity type)
    auto* archetype = registry.getArchetype("Test");
    ASSERT_NE(archetype, nullptr);
    EXPECT_EQ(archetype->requiredComponentTypes.size(), 1);
}

TEST_F(ArchetypeRegistryTest, GetArchetypesReturnsAllRegistered) {
    registry.registerArchetype(ArchetypeBuilder("A").build());
    registry.registerArchetype(ArchetypeBuilder("B").build());

    const auto& all = registry.archetypes();
    EXPECT_EQ(all.size(), 2);
    EXPECT_TRUE(all.contains("A"));
    EXPECT_TRUE(all.contains("B"));
}

// ===========================================================================
// EntityFactory Tests
// ===========================================================================

class EntityFactoryTest : public ::testing::Test {
protected:
    gaia::ecs::World world;
    ArchetypeRegistry registry;

    void SetUp() override {
        auto archetype = ArchetypeBuilder("TestEntity")
            .withComponent<TestPosition>()
            .withComponent<TestHealth>()
            .build();

        registry.registerArchetype(std::move(archetype));
    }
};

TEST_F(EntityFactoryTest, CreateEntityFromArchetype) {
    EntityFactory factory(world, registry);

    auto entity = factory.create("TestEntity");

    EXPECT_TRUE(world.valid(entity));
    EXPECT_TRUE(world.has<TestPosition>(entity));
    EXPECT_TRUE(world.has<TestHealth>(entity));
}

TEST_F(EntityFactoryTest, CreateEntityBatch) {
    EntityFactory factory(world, registry);

    auto entities = factory.createBatch("TestEntity", 10);

    EXPECT_EQ(entities.size(), 10);
    for (auto entity : entities) {
        EXPECT_TRUE(world.valid(entity));
        EXPECT_TRUE(world.has<TestPosition>(entity));
        EXPECT_TRUE(world.has<TestHealth>(entity));
    }
}

TEST_F(EntityFactoryTest, CreateUnknownArchetypeReturnsInvalid) {
    EntityFactory factory(world, registry);

    auto entity = factory.create("UnknownArchetype");

    EXPECT_FALSE(world.valid(entity));
}

TEST_F(EntityFactoryTest, CreateEntityWithComponentOverride) {
    EntityFactory factory(world, registry);

    auto entity = factory.create("TestEntity",
        [](gaia::ecs::World& w, gaia::ecs::Entity e) {
            w.set<TestPosition>(e) = TestPosition{10.0f, 20.0f, 30.0f};
        }
    );

    EXPECT_TRUE(world.valid(entity));
    const auto& pos = world.get<TestPosition>(entity);
    EXPECT_EQ(pos.x, 10.0f);
    EXPECT_EQ(pos.y, 20.0f);
    EXPECT_EQ(pos.z, 30.0f);
}

TEST_F(EntityFactoryTest, CreateEntityBatchEmpty) {
    EntityFactory factory(world, registry);

    auto entities = factory.createBatch("TestEntity", 0);

    EXPECT_TRUE(entities.empty());
}

TEST_F(EntityFactoryTest, CreateEntityBatchUnknownArchetype) {
    EntityFactory factory(world, registry);

    auto entities = factory.createBatch("UnknownArchetype", 10);

    EXPECT_TRUE(entities.empty());
}

TEST_F(EntityFactoryTest, CreateLargeBatch) {
    EntityFactory factory(world, registry);

    auto entities = factory.createBatch("TestEntity", 1000);

    EXPECT_EQ(entities.size(), 1000);
    // Verify first and last entities
    EXPECT_TRUE(world.valid(entities.front()));
    EXPECT_TRUE(world.valid(entities.back()));
}

// ===========================================================================
// RelationshipType Tests
// ===========================================================================

class RelationshipTypeTest : public ::testing::Test {
protected:
    gaia::ecs::World world;
};

TEST_F(RelationshipTypeTest, CreatePartOfRelationship) {
    auto partOf = Relations::createPartOf(world);

    EXPECT_TRUE(world.valid(partOf.tag));
    EXPECT_EQ(partOf.name, "partof");
    EXPECT_FALSE(partOf.isExclusive);
}

TEST_F(RelationshipTypeTest, CreateContainsRelationship) {
    auto contains = Relations::createContains(world);

    EXPECT_TRUE(world.valid(contains.tag));
    EXPECT_EQ(contains.name, "contains");
}

TEST_F(RelationshipTypeTest, CreateChildOfRelationship) {
    auto childOf = Relations::createChildOf(world);

    EXPECT_TRUE(world.valid(childOf.tag));
    EXPECT_EQ(childOf.name, "childof");
}

TEST_F(RelationshipTypeTest, CreateCustomRelationship) {
    auto custom = Relations::createCustom(world, "attached_to", true);

    EXPECT_TRUE(world.valid(custom.tag));
    EXPECT_EQ(custom.name, "attached_to");
    EXPECT_TRUE(custom.isExclusive);
}

TEST_F(RelationshipTypeTest, CreateCustomRelationshipNonExclusive) {
    auto custom = Relations::createCustom(world, "linked_to", false);

    EXPECT_TRUE(world.valid(custom.tag));
    EXPECT_EQ(custom.name, "linked_to");
    EXPECT_FALSE(custom.isExclusive);
}

TEST_F(RelationshipTypeTest, RelationshipTypeEquality) {
    auto rel1 = Relations::createPartOf(world);
    auto rel2 = Relations::createPartOf(world);
    auto rel3 = Relations::createContains(world);

    // Each call creates a new tag
    EXPECT_NE(rel1.tag, rel2.tag);
    EXPECT_NE(rel1.tag, rel3.tag);

    // Same tag should compare equal
    RelationshipType rel1Copy = rel1;
    EXPECT_EQ(rel1, rel1Copy);
}

TEST_F(RelationshipTypeTest, RelationshipTypeHash) {
    auto rel1 = Relations::createPartOf(world);
    auto rel2 = Relations::createPartOf(world);

    RelationshipTypeHash hasher;
    auto hash1 = hasher(rel1);
    auto hash2 = hasher(rel2);

    // Different tags should have different hashes (usually)
    EXPECT_NE(hash1, hash2);

    // Same type should have same hash
    EXPECT_EQ(hasher(rel1), hasher(rel1));
}

// ===========================================================================
// Edge Case Tests
// ===========================================================================

class ArchetypeBuilderEdgeCaseTest : public ::testing::Test {
protected:
    gaia::ecs::World world;
};

TEST_F(ArchetypeBuilderEdgeCaseTest, ArchetypeWithEmptyName) {
    auto archetype = ArchetypeBuilder("")
        .withComponent<TestPosition>()
        .build();

    EXPECT_EQ(archetype.name, "");
}

TEST_F(ArchetypeBuilderEdgeCaseTest, ArchetypeWithLongName) {
    std::string longName(1000, 'A');
    auto archetype = ArchetypeBuilder(longName)
        .withComponent<TestPosition>()
        .build();

    EXPECT_EQ(archetype.name, longName);
}

TEST_F(ArchetypeBuilderEdgeCaseTest, ArchetypeWithSpecialCharactersInName) {
    auto archetype = ArchetypeBuilder("Test::Archetype<int>::Type_1")
        .withComponent<TestPosition>()
        .build();

    EXPECT_EQ(archetype.name, "Test::Archetype<int>::Type_1");
}

TEST_F(ArchetypeBuilderEdgeCaseTest, ArchetypeWithUnicodeInName) {
    auto archetype = ArchetypeBuilder("TestEntity_日本語")
        .withComponent<TestPosition>()
        .build();

    EXPECT_EQ(archetype.name, "TestEntity_日本語");
}

TEST_F(ArchetypeBuilderEdgeCaseTest, ZeroBundleThreshold) {
    auto tag = world.add();
    RelationshipType rel{tag, "test", false};

    auto archetype = ArchetypeBuilder("ZeroThreshold")
        .acceptsRelationship(rel)
            .bundleThreshold(0)
            .done()
        .build();

    auto hooks = archetype.getHooks(rel);
    ASSERT_NE(hooks, nullptr);
    EXPECT_EQ(hooks->bundleThreshold, 0);
}

TEST_F(ArchetypeBuilderEdgeCaseTest, LargeBundleThreshold) {
    auto tag = world.add();
    RelationshipType rel{tag, "test", false};

    auto archetype = ArchetypeBuilder("LargeThreshold")
        .acceptsRelationship(rel)
            .bundleThreshold(SIZE_MAX)
            .done()
        .build();

    auto hooks = archetype.getHooks(rel);
    ASSERT_NE(hooks, nullptr);
    EXPECT_EQ(hooks->bundleThreshold, SIZE_MAX);
}

TEST_F(ArchetypeBuilderEdgeCaseTest, GetHooksReturnsNullForUnregistered) {
    auto tag = world.add();
    RelationshipType rel{tag, "test", false};

    auto archetype = ArchetypeBuilder("NoHooks")
        .withComponent<TestPosition>()
        .build();

    EXPECT_EQ(archetype.getHooks(rel), nullptr);
}

// ===========================================================================
// Integration Tests
// ===========================================================================

class ArchetypeBuilderIntegrationTest : public ::testing::Test {
protected:
    gaia::ecs::World world;
    ArchetypeRegistry registry;
};

TEST_F(ArchetypeBuilderIntegrationTest, CompleteWorkflow) {
    // 1. Create relationship types
    auto partOf = Relations::createPartOf(world);

    // 2. Build archetypes
    int addHookCount = 0;
    auto volumeArchetype = ArchetypeBuilder("Volume")
        .withComponent<TestPosition>()
        .withComponent<TestHealth>()
        .acceptsRelationship(partOf)
            .onAdded([&addHookCount](auto&, auto, auto, auto&) {
                addHookCount++;
            })
            .done()
        .build();

    auto voxelArchetype = ArchetypeBuilder("Voxel")
        .withComponent<TestPosition>()
        .canRelate(partOf)
        .build();

    // 3. Register archetypes
    registry.registerArchetype(std::move(volumeArchetype));
    registry.registerArchetype(std::move(voxelArchetype));

    // 4. Create entities
    EntityFactory factory(world, registry);
    auto volume = factory.create("Volume");
    auto voxels = factory.createBatch("Voxel", 10);

    // 5. Verify entities
    EXPECT_TRUE(world.valid(volume));
    EXPECT_EQ(voxels.size(), 10);

    // 6. Get archetype and invoke hook manually
    const auto* volumeDef = registry.getArchetype("Volume");
    ASSERT_NE(volumeDef, nullptr);

    const auto* hooks = volumeDef->getHooks(partOf);
    ASSERT_NE(hooks, nullptr);

    // Simulate hook invocation
    if (hooks->onAdded) {
        for (auto voxel : voxels) {
            hooks->onAdded(world, voxel, volume, partOf);
        }
    }

    EXPECT_EQ(addHookCount, 10);
}

TEST_F(ArchetypeBuilderIntegrationTest, ComponentValuesAreInitialized) {
    TestPosition initialPos{100.0f, 200.0f, 300.0f};
    TestHealth initialHealth{50, 100};

    registry.registerArchetype(
        ArchetypeBuilder("InitializedEntity")
            .withComponent<TestPosition>(initialPos)
            .withComponent<TestHealth>(initialHealth)
            .build()
    );

    EntityFactory factory(world, registry);
    auto entity = factory.create("InitializedEntity");

    EXPECT_TRUE(world.valid(entity));

    const auto& pos = world.get<TestPosition>(entity);
    EXPECT_EQ(pos.x, 100.0f);
    EXPECT_EQ(pos.y, 200.0f);
    EXPECT_EQ(pos.z, 300.0f);

    const auto& health = world.get<TestHealth>(entity);
    EXPECT_EQ(health.value, 50);
    EXPECT_EQ(health.maxValue, 100);
}

// ===========================================================================
// Thread Safety Tests (Basic Validation)
// ===========================================================================

class ArchetypeRegistryThreadTest : public ::testing::Test {
protected:
    ArchetypeRegistry registry;
};

TEST_F(ArchetypeRegistryThreadTest, ConcurrentArchetypeRegistration) {
    // Note: ArchetypeRegistry is not thread-safe by design
    // This test validates single-threaded sequential registration followed by
    // concurrent reads, which is the expected usage pattern

    // Register archetypes sequentially
    for (int i = 0; i < 100; ++i) {
        std::string name = "Archetype" + std::to_string(i);
        registry.registerArchetype(ArchetypeBuilder(name).withComponent<TestPosition>().build());
    }

    // Concurrent reads
    std::vector<std::thread> threads;
    std::atomic<int> readCount{0};

    for (int t = 0; t < 10; ++t) {
        threads.emplace_back([this, &readCount]() {
            for (int i = 0; i < 100; ++i) {
                std::string name = "Archetype" + std::to_string(i);
                if (registry.hasArchetype(name)) {
                    readCount++;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(readCount.load(), 1000);  // All reads should succeed
}
