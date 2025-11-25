#include <gtest/gtest.h>
#include "ArchetypeBuilder.h"
#include <gaia.h>

using namespace GaiaArchetype;

// Test component for archetype builder tests
struct TestPosition {
    float x = 0, y = 0, z = 0;
};

struct TestVelocity {
    float vx = 0, vy = 0, vz = 0;
};

struct TestHealth {
    int value = 100;
};

// ============================================================================
// ArchetypeBuilder Tests
// ============================================================================

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
    // Create a relationship type
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

// ============================================================================
// ArchetypeRegistry Tests
// ============================================================================

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

// ============================================================================
// EntityFactory Tests
// ============================================================================

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
    }
}

TEST_F(EntityFactoryTest, CreateUnknownArchetypeReturnsInvalid) {
    EntityFactory factory(world, registry);

    auto entity = factory.create("UnknownArchetype");

    EXPECT_FALSE(world.valid(entity));
}

// ============================================================================
// RelationshipType Tests
// ============================================================================

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

TEST_F(RelationshipTypeTest, CreateCustomRelationship) {
    auto custom = Relations::createCustom(world, "attached_to", true);

    EXPECT_TRUE(world.valid(custom.tag));
    EXPECT_EQ(custom.name, "attached_to");
    EXPECT_TRUE(custom.isExclusive);
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
