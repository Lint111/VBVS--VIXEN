#include <gtest/gtest.h>
#include "VoxelInjector.h"
#include "GaiaVoxelWorld.h"
#include <glm/glm.hpp>
#include <unordered_set>

using namespace GaiaVoxel;

// ===========================================================================
// Mock SVO for Testing (No LaineKarrasOctree Dependency)
// ===========================================================================

class MockSVO {
public:
    struct InsertedVoxel {
        gaia::ecs::Entity entity;
        glm::vec3 position;
    };

    std::vector<InsertedVoxel> insertedVoxels;
    bool compacted = false;

    void insertVoxel(const glm::vec3& position, gaia::ecs::Entity entity) {
        insertedVoxels.push_back({entity, position});
    }

    void compactToESVOFormat() {
        compacted = true;
    }

    size_t getInsertCount() const {
        return insertedVoxels.size();
    }

    bool wasCompacted() const {
        return compacted;
    }
};

// ===========================================================================
// Constructor Tests
// ===========================================================================

TEST(VoxelInjectorTest, CreateInjector) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);

    // Should construct without errors
    SUCCEED();
}

// ===========================================================================
// Brick Grouping Tests
// ===========================================================================

TEST(VoxelInjectorTest, ComputeBrickCoord_Origin) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);

    // Create entity at origin
    auto entity = world.createVoxel(glm::vec3(0.0f, 0.0f, 0.0f));

    std::vector<gaia::ecs::Entity> entities = {entity};
    auto groups = injector.groupByBrick(entities, 8);

    EXPECT_EQ(groups.size(), 1);

    // Origin should map to brick (0, 0, 0)
    auto it = groups.begin();
    EXPECT_EQ(it->first.x, 0);
    EXPECT_EQ(it->first.y, 0);
    EXPECT_EQ(it->first.z, 0);
}

TEST(VoxelInjectorTest, GroupByBrick_SingleBrick) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);

    // Create 8 entities within same 8³ brick
    std::vector<gaia::ecs::Entity> entities;
    for (int i = 0; i < 8; ++i) {
        auto entity = world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f));
        entities.push_back(entity);
    }

    auto groups = injector.groupByBrick(entities, 8);

    // All entities should map to same brick
    EXPECT_EQ(groups.size(), 1);

    auto& brickEntities = groups.begin()->second;
    EXPECT_EQ(brickEntities.size(), 8);
}

TEST(VoxelInjectorTest, GroupByBrick_MultipleBricks) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);

    // Create entities across 3 different bricks
    std::vector<gaia::ecs::Entity> entities;

    // Brick 1: (0-7, 0-7, 0-7)
    entities.push_back(world.createVoxel(glm::vec3(0.0f, 0.0f, 0.0f)));
    entities.push_back(world.createVoxel(glm::vec3(5.0f, 5.0f, 5.0f)));

    // Brick 2: (8-15, 0-7, 0-7)
    entities.push_back(world.createVoxel(glm::vec3(10.0f, 0.0f, 0.0f)));
    entities.push_back(world.createVoxel(glm::vec3(12.0f, 3.0f, 2.0f)));

    // Brick 3: (0-7, 8-15, 0-7)
    entities.push_back(world.createVoxel(glm::vec3(0.0f, 10.0f, 0.0f)));

    auto groups = injector.groupByBrick(entities, 8);

    EXPECT_EQ(groups.size(), 3);

    // Verify brick entity counts
    std::vector<size_t> counts;
    for (const auto& [coord, brickEntities] : groups) {
        counts.push_back(brickEntities.size());
    }

    std::sort(counts.begin(), counts.end());
    EXPECT_EQ(counts[0], 1); // Brick 3
    EXPECT_EQ(counts[1], 2); // Brick 1
    EXPECT_EQ(counts[2], 2); // Brick 2
}

TEST(VoxelInjectorTest, GroupByBrick_DifferentResolutions) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);

    std::vector<gaia::ecs::Entity> entities;

    // Create entities spanning 32 units
    for (int i = 0; i < 32; ++i) {
        entities.push_back(world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f)));
    }

    // Group by brick size 8
    auto groups8 = injector.groupByBrick(entities, 8);
    EXPECT_EQ(groups8.size(), 4); // 32 / 8 = 4 bricks

    // Group by brick size 16
    auto groups16 = injector.groupByBrick(entities, 16);
    EXPECT_EQ(groups16.size(), 2); // 32 / 16 = 2 bricks
}

TEST(VoxelInjectorTest, GroupByBrick_NegativeCoordinates) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);

    std::vector<gaia::ecs::Entity> entities;

    // Entities at negative coordinates
    entities.push_back(world.createVoxel(glm::vec3(-5.0f, -5.0f, -5.0f)));
    entities.push_back(world.createVoxel(glm::vec3(-10.0f, -10.0f, -10.0f)));
    entities.push_back(world.createVoxel(glm::vec3(-15.0f, 0.0f, 0.0f)));

    auto groups = injector.groupByBrick(entities, 8);

    // Should handle negative coords correctly
    EXPECT_GE(groups.size(), 2);
}

TEST(VoxelInjectorTest, GroupByBrick_EmptyInput) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);

    std::vector<gaia::ecs::Entity> entities;
    auto groups = injector.groupByBrick(entities, 8);

    EXPECT_EQ(groups.size(), 0);
}

// ===========================================================================
// Entity Insertion Tests (Using Mock SVO)
// ===========================================================================

TEST(VoxelInjectorTest, InsertEntities_SingleEntity) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);
    MockSVO svo;

    auto entity = world.createVoxel(glm::vec3(10.0f, 5.0f, 3.0f));
    std::vector<gaia::ecs::Entity> entities = {entity};

    size_t inserted = injector.insertEntities(entities, svo, 8);

    EXPECT_EQ(inserted, 1);
    EXPECT_EQ(svo.getInsertCount(), 1);
    EXPECT_EQ(svo.insertedVoxels[0].entity, entity);
}

TEST(VoxelInjectorTest, InsertEntities_MultipleEntities) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);
    MockSVO svo;

    std::vector<gaia::ecs::Entity> entities;
    for (int i = 0; i < 50; ++i) {
        entities.push_back(world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f)));
    }

    size_t inserted = injector.insertEntities(entities, svo, 8);

    EXPECT_EQ(inserted, 50);
    EXPECT_EQ(svo.getInsertCount(), 50);
}

TEST(VoxelInjectorTest, InsertEntitiesBatched_SingleBrick) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);
    MockSVO svo;

    // All entities in same brick
    std::vector<gaia::ecs::Entity> entities;
    for (int i = 0; i < 8; ++i) {
        entities.push_back(world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f)));
    }

    size_t inserted = injector.insertEntitiesBatched(entities, svo, 8);

    EXPECT_EQ(inserted, 8);
    EXPECT_EQ(svo.getInsertCount(), 8);

    auto stats = injector.getLastInsertionStats();
    EXPECT_EQ(stats.totalInserted, 8);
    EXPECT_EQ(stats.brickCount, 1); // Only 1 brick touched
}

TEST(VoxelInjectorTest, InsertEntitiesBatched_MultipleBricks) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);
    MockSVO svo;

    // Entities across 4 bricks
    std::vector<gaia::ecs::Entity> entities;
    for (int i = 0; i < 32; ++i) {
        entities.push_back(world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f)));
    }

    size_t inserted = injector.insertEntitiesBatched(entities, svo, 8);

    EXPECT_EQ(inserted, 32);
    EXPECT_EQ(svo.getInsertCount(), 32);

    auto stats = injector.getLastInsertionStats();
    EXPECT_EQ(stats.totalInserted, 32);
    EXPECT_EQ(stats.brickCount, 4); // 4 bricks (32 / 8)
}

TEST(VoxelInjectorTest, InsertEntitiesBatched_VerifyBatchingOptimization) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);
    MockSVO svo;

    // Create 100 entities across 10 bricks (10 entities per brick)
    std::vector<gaia::ecs::Entity> entities;
    for (int brick = 0; brick < 10; ++brick) {
        for (int i = 0; i < 10; ++i) {
            float x = static_cast<float>(brick * 8 + i % 8);
            float y = static_cast<float>(i / 8);
            entities.push_back(world.createVoxel(glm::vec3(x, y, 0.0f)));
        }
    }

    size_t inserted = injector.insertEntitiesBatched(entities, svo, 8);

    EXPECT_EQ(inserted, 100);

    auto stats = injector.getLastInsertionStats();
    EXPECT_EQ(stats.totalInserted, 100);
    EXPECT_GE(stats.brickCount, 10); // At least 10 bricks
}

TEST(VoxelInjectorTest, CompactOctree) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);
    MockSVO svo;

    // Insert some entities
    std::vector<gaia::ecs::Entity> entities;
    for (int i = 0; i < 10; ++i) {
        entities.push_back(world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f)));
    }

    injector.insertEntities(entities, svo, 8);

    EXPECT_FALSE(svo.wasCompacted());

    // Trigger compaction
    injector.compactOctree(svo);

    EXPECT_TRUE(svo.wasCompacted());
}

// ===========================================================================
// Statistics Tests
// ===========================================================================

TEST(VoxelInjectorTest, GetLastInsertionStats_InitialState) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);

    auto stats = injector.getLastInsertionStats();
    EXPECT_EQ(stats.totalInserted, 0);
    EXPECT_EQ(stats.failedInsertions, 0);
    EXPECT_EQ(stats.brickCount, 0);
}

TEST(VoxelInjectorTest, GetLastInsertionStats_AfterInsertion) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);
    MockSVO svo;

    std::vector<gaia::ecs::Entity> entities;
    for (int i = 0; i < 50; ++i) {
        entities.push_back(world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f)));
    }

    injector.insertEntitiesBatched(entities, svo, 8);

    auto stats = injector.getLastInsertionStats();
    EXPECT_EQ(stats.totalInserted, 50);
    EXPECT_GE(stats.brickCount, 1);
}

// ===========================================================================
// Edge Case Tests
// ===========================================================================

TEST(VoxelInjectorTest, InsertEntities_EmptyList) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);
    MockSVO svo;

    std::vector<gaia::ecs::Entity> entities;
    size_t inserted = injector.insertEntities(entities, svo, 8);

    EXPECT_EQ(inserted, 0);
    EXPECT_EQ(svo.getInsertCount(), 0);
}

TEST(VoxelInjectorTest, InsertEntities_InvalidEntity) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);
    MockSVO svo;

    // Create entity and then destroy it
    auto entity = world.createVoxel(glm::vec3(0.0f));
    world.destroyVoxel(entity);

    std::vector<gaia::ecs::Entity> entities = {entity};
    size_t inserted = injector.insertEntities(entities, svo, 8);

    // Should handle invalid entity gracefully
    EXPECT_EQ(inserted, 0);
    EXPECT_EQ(svo.getInsertCount(), 0);

    auto stats = injector.getLastInsertionStats();
    EXPECT_GE(stats.failedInsertions, 1);
}

TEST(VoxelInjectorTest, InsertEntities_MixValidAndInvalid) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);
    MockSVO svo;

    std::vector<gaia::ecs::Entity> entities;

    // Add valid entity
    entities.push_back(world.createVoxel(glm::vec3(0.0f)));

    // Add invalid entity
    auto invalidEntity = world.createVoxel(glm::vec3(1.0f));
    world.destroyVoxel(invalidEntity);
    entities.push_back(invalidEntity);

    // Add another valid entity
    entities.push_back(world.createVoxel(glm::vec3(2.0f)));

    size_t inserted = injector.insertEntities(entities, svo, 8);

    EXPECT_EQ(inserted, 2); // Only 2 valid entities
    EXPECT_EQ(svo.getInsertCount(), 2);

    auto stats = injector.getLastInsertionStats();
    EXPECT_EQ(stats.totalInserted, 2);
    EXPECT_GE(stats.failedInsertions, 1);
}

TEST(VoxelInjectorTest, BrickCoordHash_Uniqueness) {
    VoxelInjector::BrickCoordHash hasher;

    VoxelInjector::BrickCoord coord1{0, 0, 0};
    VoxelInjector::BrickCoord coord2{1, 0, 0};
    VoxelInjector::BrickCoord coord3{0, 1, 0};
    VoxelInjector::BrickCoord coord4{0, 0, 1};

    std::unordered_set<size_t> hashes;
    hashes.insert(hasher(coord1));
    hashes.insert(hasher(coord2));
    hashes.insert(hasher(coord3));
    hashes.insert(hasher(coord4));

    // All hashes should be unique
    EXPECT_EQ(hashes.size(), 4);
}

TEST(VoxelInjectorTest, BrickCoordEquality) {
    VoxelInjector::BrickCoord coord1{5, 10, 15};
    VoxelInjector::BrickCoord coord2{5, 10, 15};
    VoxelInjector::BrickCoord coord3{5, 10, 16};

    EXPECT_TRUE(coord1 == coord2);
    EXPECT_FALSE(coord1 == coord3);
}

// ===========================================================================
// Performance Tests (Conceptual - No Hard Requirements)
// ===========================================================================

TEST(VoxelInjectorTest, BatchingReducesTraversals) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);
    MockSVO svo;

    // Create 512 entities in a single 8³ brick
    std::vector<gaia::ecs::Entity> entities;
    for (int z = 0; z < 8; ++z) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                entities.push_back(world.createVoxel(glm::vec3(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z))));
            }
        }
    }

    // Batched insertion
    injector.insertEntitiesBatched(entities, svo, 8);

    auto stats = injector.getLastInsertionStats();
    EXPECT_EQ(stats.totalInserted, 512);
    EXPECT_EQ(stats.brickCount, 1); // Only 1 brick = only 1 octree traversal!

    // Without batching, would require 512 traversals
    // With batching: 1 traversal (512× reduction)
}

TEST(VoxelInjectorTest, LargeBatchInsertion) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);
    MockSVO svo;

    // Create 10,000 entities
    std::vector<gaia::ecs::Entity> entities;
    for (int i = 0; i < 10000; ++i) {
        float x = static_cast<float>(i % 100);
        float y = static_cast<float>((i / 100) % 100);
        float z = static_cast<float>(i / 10000);
        entities.push_back(world.createVoxel(glm::vec3(x, y, z)));
    }

    size_t inserted = injector.insertEntitiesBatched(entities, svo, 8);

    EXPECT_EQ(inserted, 10000);
    EXPECT_EQ(svo.getInsertCount(), 10000);

    auto stats = injector.getLastInsertionStats();
    EXPECT_EQ(stats.totalInserted, 10000);
    EXPECT_GT(stats.brickCount, 0);
}

// ===========================================================================
// Integration Tests (With GaiaVoxelWorld Attributes)
// ===========================================================================

TEST(VoxelInjectorTest, InsertedEntitiesRetainAttributes) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);
    MockSVO svo;

    // Create entities with specific attributes
    std::vector<gaia::ecs::Entity> entities;
    std::vector<glm::vec3> expectedColors;

    for (int i = 0; i < 10; ++i) {
        glm::vec3 color(static_cast<float>(i) / 10.0f, 0.5f, 0.5f);
        auto entity = world.createVoxel(
            glm::vec3(static_cast<float>(i), 0.0f, 0.0f),
            1.0f,
            color,
            glm::vec3(0.0f, 1.0f, 0.0f));

        entities.push_back(entity);
        expectedColors.push_back(color);
    }

    injector.insertEntities(entities, svo, 8);

    // Verify attributes are still accessible after insertion
    for (size_t i = 0; i < entities.size(); ++i) {
        auto color = world.getColor(entities[i]);
        ASSERT_TRUE(color.has_value());
        EXPECT_EQ(color.value(), expectedColors[i]);
    }
}

TEST(VoxelInjectorTest, VerifyBrickGroupingPreservesEntityData) {
    GaiaVoxelWorld world;
    VoxelInjector injector(world);

    // Create entities with unique density values
    std::vector<gaia::ecs::Entity> entities;
    std::vector<float> expectedDensities;

    for (int i = 0; i < 20; ++i) {
        float density = static_cast<float>(i) / 20.0f;
        auto entity = world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f), density);
        entities.push_back(entity);
        expectedDensities.push_back(density);
    }

    // Group by brick
    auto groups = injector.groupByBrick(entities, 8);

    // Verify all entities are accounted for
    size_t totalEntities = 0;
    for (const auto& [coord, brickEntities] : groups) {
        totalEntities += brickEntities.size();
    }
    EXPECT_EQ(totalEntities, 20);

    // Verify densities are preserved
    size_t entityIdx = 0;
    for (const auto& [coord, brickEntities] : groups) {
        for (auto entity : brickEntities) {
            auto density = world.getDensity(entity);
            ASSERT_TRUE(density.has_value());

            // Find matching expected density
            bool found = false;
            for (float expectedDensity : expectedDensities) {
                if (std::abs(density.value() - expectedDensity) < 0.001f) {
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found);
        }
    }
}
