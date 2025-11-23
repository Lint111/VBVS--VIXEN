#include <gtest/gtest.h>
#include "GaiaVoxelWorld.h"
#include "DynamicVoxelStruct.h"
#include <glm/glm.hpp>
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include "ComponentData.h"
#include <span>

using namespace GaiaVoxel;

// ===========================================================================
// Entity Creation Tests
// ===========================================================================

TEST(GaiaVoxelWorldTest, CreateSingleVoxel) {
    GaiaVoxelWorld world;

    glm::vec3 pos(10.0f, 5.0f, 3.0f);
    float density = 1.0f;
    glm::vec3 color(1.0f, 0.0f, 0.0f); // Red
    glm::vec3 normal(0.0f, 1.0f, 0.0f); // +Y

    auto entity = world.createVoxel(pos, density, color, normal);

    // Verify entity is valid
    ASSERT_TRUE( world.exists(entity));
    EXPECT_TRUE(world.exists(entity));
}

TEST(GaiaVoxelWorldTest, CreateVoxelDefaultParameters) {
    GaiaVoxelWorld world;

    glm::vec3 pos(5.0f, 5.0f, 5.0f);
    auto entity = world.createVoxel(pos);

    ASSERT_TRUE( world.exists(entity));

    // Check default values
    auto density = world.getComponentValue<Density>(entity);
    ASSERT_TRUE(density.has_value());
    EXPECT_FLOAT_EQ(density.value(), 1.0f);

    auto color = world.getComponentValue<Color>(entity);
    ASSERT_TRUE(color.has_value());
    EXPECT_EQ(color.value(), glm::vec3(1.0f)); // White

    auto normal = world.getComponentValue<Normal>(entity);
    ASSERT_TRUE(normal.has_value());
    EXPECT_EQ(normal.value(), glm::vec3(0.0f, 1.0f, 0.0f)); // +Y
}

TEST(GaiaVoxelWorldTest, CreateMultipleVoxels) {
    GaiaVoxelWorld world;

    std::vector<GaiaVoxelWorld::EntityID> entities;
    for (int i = 0; i < 100; ++i) {
        glm::vec3 pos(static_cast<float>(i), 0.0f, 0.0f);
        auto entity = world.createVoxel(pos, 1.0f, glm::vec3(1.0f, 0.0f, 0.0f));
        entities.push_back(entity);
    }

    EXPECT_EQ(entities.size(), 100);

    // Verify all entities are valid
    for (auto entity : entities) {
        EXPECT_TRUE( world.exists(entity));
        EXPECT_TRUE(world.exists(entity));
    }
}

TEST(GaiaVoxelWorldTest, DestroyVoxel) {
    GaiaVoxelWorld world;

    auto entity = world.createVoxel(glm::vec3(0.0f), 1.0f);
    ASSERT_TRUE(world.exists(entity));

    world.destroyVoxel(entity);
    EXPECT_FALSE(world.exists(entity));
}

TEST(GaiaVoxelWorldTest, ClearAllVoxels) {
    GaiaVoxelWorld world;

    std::vector<GaiaVoxelWorld::EntityID> entities;
    for (int i = 0; i < 50; ++i) {
        entities.push_back(world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f)));
    }

    world.clear();

    // All entities should be invalid after clear
    for (auto entity : entities) {
        EXPECT_FALSE(world.exists(entity));
    }
}

// ===========================================================================
// Component Access Tests
// ===========================================================================

TEST(GaiaVoxelWorldTest, GetPosition) {
    GaiaVoxelWorld world;

    glm::vec3 expectedPos(10.5f, 20.3f, -5.7f);
    auto entity = world.createVoxel(expectedPos);

    auto pos = world.getPosition(entity);
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos.value(), expectedPos);
}

TEST(GaiaVoxelWorldTest, GetDensity) {
    GaiaVoxelWorld world;

    float expectedDensity = 0.75f;
    auto entity = world.createVoxel(glm::vec3(0.0f), expectedDensity);

    auto density = world.getComponentValue<Density>(entity);
    ASSERT_TRUE(density.has_value());
    EXPECT_FLOAT_EQ(density.value(), expectedDensity);
}

TEST(GaiaVoxelWorldTest, GetColor) {
    GaiaVoxelWorld world;

    glm::vec3 expectedColor(0.2f, 0.8f, 0.4f);
    auto entity = world.createVoxel(glm::vec3(0.0f), 1.0f, expectedColor);

    auto color = world.getComponentValue<Color>(entity);
    ASSERT_TRUE(color.has_value());
    EXPECT_EQ(color.value(), expectedColor);
}

TEST(GaiaVoxelWorldTest, GetNormal) {
    GaiaVoxelWorld world;

    glm::vec3 expectedNormal(0.577f, 0.577f, 0.577f); // Normalized diagonal
    auto entity = world.createVoxel(glm::vec3(0.0f), 1.0f, glm::vec3(1.0f), expectedNormal);

    auto normal = world.getComponentValue<Normal>(entity);
    ASSERT_TRUE(normal.has_value());
    EXPECT_EQ(normal.value(), expectedNormal);
}

TEST(GaiaVoxelWorldTest, SetPosition) {
    GaiaVoxelWorld world;

    auto entity = world.createVoxel(glm::vec3(0.0f));

    glm::vec3 newPos(100.0f, 200.0f, 300.0f);
    world.setPosition(entity, newPos);

    auto pos = world.getPosition(entity);
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos.value(), newPos);
}

TEST(GaiaVoxelWorldTest, SetDensity) {
    GaiaVoxelWorld world;

    auto entity = world.createVoxel(glm::vec3(0.0f), 1.0f);

    float newDensity = 0.25f;
    world.setComponent<Density>(entity, newDensity);

    auto density = world.getComponentValue<Density>(entity);
    ASSERT_TRUE(density.has_value());
    EXPECT_FLOAT_EQ(density.value(), newDensity);
}

TEST(GaiaVoxelWorldTest, SetColor) {
    GaiaVoxelWorld world;

    auto entity = world.createVoxel(glm::vec3(0.0f));

    glm::vec3 newColor(0.1f, 0.2f, 0.3f);
    world.setComponent<Color>(entity, Color{newColor});

    auto color = world.getComponentValue<Color>(entity);
    ASSERT_TRUE(color.has_value());
    EXPECT_EQ(color.value(), newColor);
}

TEST(GaiaVoxelWorldTest, SetNormal) {
    GaiaVoxelWorld world;

    auto entity = world.createVoxel(glm::vec3(0.0f));

    glm::vec3 newNormal(1.0f, 0.0f, 0.0f); // +X
    world.setComponent<Normal>(entity, Normal{newNormal});

    auto normal = world.getComponentValue<Normal>(entity);
    ASSERT_TRUE(normal.has_value());
    EXPECT_EQ(normal.value(), newNormal);
}

TEST(GaiaVoxelWorldTest, GetNonExistentEntity) {
    GaiaVoxelWorld world;

	struct position_t {};


    auto entity = world.createVoxel(glm::vec3(0.0f));



    world.destroyVoxel(entity);

    // All getters should return std::nullopt for destroyed entity
    EXPECT_FALSE(world.getComponentValue<position_t>(entity).has_value());
    EXPECT_FALSE(world.getComponentValue<Density>(entity).has_value());
    EXPECT_FALSE(world.getComponentValue<Color>(entity).has_value());
    EXPECT_FALSE(world.getComponentValue<Normal>(entity).has_value());
}

// ===========================================================================
// Spatial Query Tests
// ===========================================================================

TEST(GaiaVoxelWorldTest, QueryRegion_EmptyResult) {
    GaiaVoxelWorld world;

    // Create voxel outside query region
    world.createVoxel(glm::vec3(100.0f, 100.0f, 100.0f));

    glm::vec3 queryMin(0.0f, 0.0f, 0.0f);
    glm::vec3 queryMax(10.0f, 10.0f, 10.0f);

    auto results = world.queryRegion(queryMin, queryMax);
    EXPECT_TRUE(results.empty());
}

TEST(GaiaVoxelWorldTest, QueryRegion_SingleVoxel) {
    GaiaVoxelWorld world;

    glm::vec3 voxelPos(5.0f, 5.0f, 5.0f);
    auto entity = world.createVoxel(voxelPos);

    glm::vec3 queryMin(0.0f, 0.0f, 0.0f);
    glm::vec3 queryMax(10.0f, 10.0f, 10.0f);

    auto results = world.queryRegion(queryMin, queryMax);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0], entity);
}

TEST(GaiaVoxelWorldTest, QueryRegion_MultipleVoxels) {
    GaiaVoxelWorld world;

    // Create voxels in region
    std::vector<GaiaVoxelWorld::EntityID> expectedEntities;
    for (int i = 0; i < 10; ++i) {
        glm::vec3 pos(static_cast<float>(i), 0.0f, 0.0f);
        expectedEntities.push_back(world.createVoxel(pos));
    }

    // Create voxels outside region
    world.createVoxel(glm::vec3(100.0f, 0.0f, 0.0f));
    world.createVoxel(glm::vec3(-100.0f, 0.0f, 0.0f));

    glm::vec3 queryMin(-1.0f, -1.0f, -1.0f);
    glm::vec3 queryMax(11.0f, 1.0f, 1.0f);

    auto results = world.queryRegion(queryMin, queryMax);
    EXPECT_EQ(results.size(), expectedEntities.size());
}

TEST(GaiaVoxelWorldTest, QuerySolidVoxels) {
    GaiaVoxelWorld world;

    // Create mix of solid and air voxels
    auto solid1 = world.createVoxel(glm::vec3(0.0f), 1.0f);  // Solid
    auto air1 = world.createVoxel(glm::vec3(1.0f), 0.0f);    // Air
    auto solid2 = world.createVoxel(glm::vec3(2.0f), 0.5f);  // Solid
    auto air2 = world.createVoxel(glm::vec3(3.0f), 0.0f);    // Air

    auto solidVoxels = world.querySolidVoxels();

    EXPECT_EQ(solidVoxels.size(), 2);
    EXPECT_NE(std::find(solidVoxels.begin(), solidVoxels.end(), solid1), solidVoxels.end());
    EXPECT_NE(std::find(solidVoxels.begin(), solidVoxels.end(), solid2), solidVoxels.end());
}

TEST(GaiaVoxelWorldTest, CountVoxelsInRegion) {
    GaiaVoxelWorld world;

    for (int i = 0; i < 25; ++i) {
        world.createVoxel(glm::vec3(static_cast<float>(i % 5), static_cast<float>(i / 5), 0.0f));
    }

    glm::vec3 queryMin(0.0f, 0.0f, -1.0f);
    glm::vec3 queryMax(5.0f, 5.0f, 1.0f);

    size_t count = world.countVoxelsInRegion(queryMin, queryMax);
    EXPECT_EQ(count, 25);
}

// ===========================================================================
// Batch Operation Tests
// ===========================================================================

// NOTE: DynamicVoxelScalar batch API removed - use VoxelCreationRequest instead
TEST(GaiaVoxelWorldTest, DISABLED_CreateVoxelsBatch_DynamicVoxelScalar) {
    // TODO: Remove or convert to new API
    SUCCEED();
}

TEST(GaiaVoxelWorldTest, CreateVoxelsBatch_CreationEntry) {
    GaiaVoxelWorld world;

    std::vector<VoxelCreationRequest> batch;
    for (int i = 0; i < 100; ++i) {
        VoxelCreationRequest request;

		request.position = glm::vec3(static_cast<float>(i), 0.0f, 0.0f);
        ComponentQueryRequest attrs[] = {
            Density{0.8f},
            Color{glm::vec3(1, 0, 0)},
            Normal{glm::vec3(0, 1, 0)},
            Material{42}
        };
        request.components = attrs;
        batch.push_back(request);
    }

    auto entities = world.createVoxelsBatch(batch);

    EXPECT_EQ(entities.size(), 100);

    // Verify attributes from first entity
    auto density = world.getComponentValue<Density>(entities[0]);
    ASSERT_TRUE(density.has_value());
    EXPECT_FLOAT_EQ(density.value(), 0.8f);

    auto color = world.getComponentValue<Color>(entities[0]);
    ASSERT_TRUE(color.has_value());
    EXPECT_EQ(color.value(), glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST(GaiaVoxelWorldTest, DestroyVoxelsBatch) {
    GaiaVoxelWorld world;

    std::vector<GaiaVoxelWorld::EntityID> entities;
    for (int i = 0; i < 20; ++i) {
        entities.push_back(world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f)));
    }

    world.destroyVoxelsBatch(entities);

    for (auto entity : entities) {
        EXPECT_FALSE(world.exists(entity));
    }
}

// ===========================================================================
// Statistics Tests
// ===========================================================================

TEST(GaiaVoxelWorldTest, GetStats) {
    GaiaVoxelWorld world;

    // Create mix of voxels
    for (int i = 0; i < 100; ++i) {
        float density = (i % 2 == 0) ? 1.0f : 0.0f; // 50 solid, 50 air
        world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f), density);
    }

    auto stats = world.getStats();

    EXPECT_EQ(stats.totalEntities, 100);
    EXPECT_EQ(stats.solidVoxels, 50);
    EXPECT_GT(stats.memoryUsageBytes, 0); // Should have allocated memory
}

// ===========================================================================
// Brick Storage Tests
// ===========================================================================

// NOTE: createVoxelInBrick() API REMOVED (Session 6 - Nov 23, 2025)
// Reason: Brick storage moved to BrickView pattern (not entity-based)
// See: memory-bank/activeContext.md lines 74-83
//
// New architecture uses BrickView for dense regions:
//   BrickView brick(mortonKeyOffset, brickDepth);
//   auto voxel = brick.getVoxel(localX, localY, localZ);
//
// This test is disabled until BrickView integration is complete.

TEST(GaiaVoxelWorldTest, DISABLED_CreateVoxelInBrick) {
    // TODO: Rewrite test using BrickView pattern when implemented
    SUCCEED();
}

// ===========================================================================
// Thread Safety Tests (Basic Validation)
// ===========================================================================

TEST(GaiaVoxelWorldTest, ConcurrentReads) {
    GaiaVoxelWorld world;

    auto entity = world.createVoxel(glm::vec3(10.0f, 5.0f, 3.0f), 1.0f);

    // Gaia ECS should handle concurrent reads
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&world, entity, &successCount]() {
            for (int j = 0; j < 100; ++j) {
                auto pos = world.getPosition(entity);
                if (pos.has_value()) {
                    successCount++;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(successCount.load(), 1000); // All reads should succeed
}

// ===========================================================================
// Edge Case Tests
// ===========================================================================

TEST(GaiaVoxelWorldTest, LargeCoordinates) {
    GaiaVoxelWorld world;

    glm::vec3 largePos(1000000.0f, -500000.0f, 750000.0f);
    auto entity = world.createVoxel(largePos);

    auto pos = world.getPosition(entity);
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos.value(), largePos);
}

TEST(GaiaVoxelWorldTest, ZeroDensity) {
    GaiaVoxelWorld world;

    auto entity = world.createVoxel(glm::vec3(0.0f), 0.0f); // Air voxel

    auto density = world.getComponentValue<Density>(entity);
    ASSERT_TRUE(density.has_value());
    EXPECT_FLOAT_EQ(density.value(), 0.0f);

    // Should NOT appear in solid voxels query
    auto solidVoxels = world.querySolidVoxels();
    EXPECT_EQ(std::find(solidVoxels.begin(), solidVoxels.end(), entity), solidVoxels.end());
}

TEST(GaiaVoxelWorldTest, NegativeCoordinates) {
    GaiaVoxelWorld world;

    glm::vec3 negPos(-10.0f, -20.0f, -30.0f);
    auto entity = world.createVoxel(negPos);

    auto pos = world.getPosition(entity);
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos.value(), negPos);
}
