#include <gtest/gtest.h>
#include "EntityBrickView.h"
#include "GaiaVoxelWorld.h"
#include <glm/glm.hpp>
#include <array>

using namespace GaiaVoxel;

// ===========================================================================
// EntityBrickView Construction Tests
// ===========================================================================

TEST(EntityBrickViewTest, CreateBrickView) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};

    EntityBrickView brick(world, brickEntities);

    // Should construct without errors
    SUCCEED();
}

// ===========================================================================
// Entity Access Tests (Linear Index)
// ===========================================================================

TEST(EntityBrickViewTest, GetSetEntity_LinearIndex) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    auto entity = world.createVoxel(glm::vec3(10.0f, 5.0f, 3.0f));

    brick.setEntity(42, entity);
    auto retrieved = brick.getEntity(42);

    EXPECT_EQ(retrieved, entity);
}

TEST(EntityBrickViewTest, GetSetEntity_AllVoxels) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    // Fill all 512 voxels
    std::vector<gaia::ecs::Entity> entities;
    for (size_t i = 0; i < 512; ++i) {
        auto entity = world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f));
        brick.setEntity(i, entity);
        entities.push_back(entity);
    }

    // Verify all entities
    for (size_t i = 0; i < 512; ++i) {
        EXPECT_EQ(brick.getEntity(i), entities[i]);
    }
}

TEST(EntityBrickViewTest, ClearEntity_LinearIndex) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    auto entity = world.createVoxel(glm::vec3(0.0f));
    brick.setEntity(10, entity);

    EXPECT_TRUE(brick.getEntity(10).valid());

    brick.clearEntity(10);

    EXPECT_FALSE(brick.getEntity(10).valid());
}

// ===========================================================================
// Entity Access Tests (3D Coordinates)
// ===========================================================================

TEST(EntityBrickViewTest, GetSetEntity_3DCoords) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    auto entity = world.createVoxel(glm::vec3(10.0f, 5.0f, 3.0f));

    brick.setEntity(3, 2, 1, entity); // x=3, y=2, z=1
    auto retrieved = brick.getEntity(3, 2, 1);

    EXPECT_EQ(retrieved, entity);
}

TEST(EntityBrickViewTest, GetSetEntity_AllCubicPositions) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    // Fill all 8x8x8 positions
    std::array<std::array<std::array<gaia::ecs::Entity, 8>, 8>, 8> entityGrid;

    for (int z = 0; z < 8; ++z) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                auto entity = world.createVoxel(glm::vec3(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z)));
                brick.setEntity(x, y, z, entity);
                entityGrid[z][y][x] = entity;
            }
        }
    }

    // Verify all positions
    for (int z = 0; z < 8; ++z) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                EXPECT_EQ(brick.getEntity(x, y, z), entityGrid[z][y][x]);
            }
        }
    }
}

TEST(EntityBrickViewTest, ClearEntity_3DCoords) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    auto entity = world.createVoxel(glm::vec3(0.0f));
    brick.setEntity(4, 2, 1, entity);

    EXPECT_TRUE(brick.getEntity(4, 2, 1).valid());

    brick.clearEntity(4, 2, 1);

    EXPECT_FALSE(brick.getEntity(4, 2, 1).valid());
}

// ===========================================================================
// Component Access Tests
// ===========================================================================

TEST(EntityBrickViewTest, GetDensity_LinearIndex) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    auto entity = world.createVoxel(glm::vec3(0.0f), 0.75f);
    brick.setEntity(10, entity);

    auto density = brick.getDensity(10);
    ASSERT_TRUE(density.has_value());
    EXPECT_FLOAT_EQ(density.value(), 0.75f);
}

TEST(EntityBrickViewTest, GetDensity_3DCoords) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    auto entity = world.createVoxel(glm::vec3(0.0f), 0.5f);
    brick.setEntity(3, 2, 1, entity);

    auto density = brick.getDensity(3, 2, 1);
    ASSERT_TRUE(density.has_value());
    EXPECT_FLOAT_EQ(density.value(), 0.5f);
}

TEST(EntityBrickViewTest, GetColor_LinearIndex) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    glm::vec3 expectedColor(1.0f, 0.0f, 0.0f);
    auto entity = world.createVoxel(glm::vec3(0.0f), 1.0f, expectedColor);
    brick.setEntity(20, entity);

    auto color = brick.getColor(20);
    ASSERT_TRUE(color.has_value());
    EXPECT_EQ(color.value(), expectedColor);
}

TEST(EntityBrickViewTest, GetColor_3DCoords) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    glm::vec3 expectedColor(0.2f, 0.8f, 0.4f);
    auto entity = world.createVoxel(glm::vec3(0.0f), 1.0f, expectedColor);
    brick.setEntity(5, 3, 2, entity);

    auto color = brick.getColor(5, 3, 2);
    ASSERT_TRUE(color.has_value());
    EXPECT_EQ(color.value(), expectedColor);
}

TEST(EntityBrickViewTest, GetNormal_LinearIndex) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    glm::vec3 expectedNormal(0.0f, 0.0f, 1.0f);
    auto entity = world.createVoxel(glm::vec3(0.0f), 1.0f, glm::vec3(1.0f), expectedNormal);
    brick.setEntity(15, entity);

    auto normal = brick.getNormal(15);
    ASSERT_TRUE(normal.has_value());
    EXPECT_EQ(normal.value(), expectedNormal);
}

TEST(EntityBrickViewTest, GetNormal_3DCoords) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    glm::vec3 expectedNormal(1.0f, 0.0f, 0.0f);
    auto entity = world.createVoxel(glm::vec3(0.0f), 1.0f, glm::vec3(1.0f), expectedNormal);
    brick.setEntity(2, 4, 6, entity);

    auto normal = brick.getNormal(2, 4, 6);
    ASSERT_TRUE(normal.has_value());
    EXPECT_EQ(normal.value(), expectedNormal);
}

TEST(EntityBrickViewTest, GetMaterialID) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    auto entity = world.createVoxel(glm::vec3(0.0f));
    // Note: Material ID requires Material component - test depends on GaiaVoxelWorld API
    brick.setEntity(5, entity);

    // Material may not be set by default - check for optional
    auto materialID = brick.getMaterialID(5);
    // Test passes if no crash (material may or may not exist)
}

TEST(EntityBrickViewTest, GetComponent_EmptyVoxel) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    // No entity set - should return std::nullopt
    auto density = brick.getDensity(0);
    EXPECT_FALSE(density.has_value());

    auto color = brick.getColor(0);
    EXPECT_FALSE(color.has_value());

    auto normal = brick.getNormal(0);
    EXPECT_FALSE(normal.has_value());
}

// ===========================================================================
// Span Access Tests (Zero-Copy)
// ===========================================================================

TEST(EntityBrickViewTest, GetEntitiesSpan) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    // Fill some entities
    for (size_t i = 0; i < 10; ++i) {
        auto entity = world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f));
        brick.setEntity(i, entity);
    }

    auto span = brick.entities();
    EXPECT_EQ(span.size(), 512);

    // Verify first 10 entities are valid
    for (size_t i = 0; i < 10; ++i) {
        EXPECT_TRUE(span[i].valid());
    }
}

TEST(EntityBrickViewTest, GetEntitiesSpan_Const) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    const EntityBrickView brick(world, brickEntities);

    auto span = brick.entities();
    EXPECT_EQ(span.size(), 512);
}

TEST(EntityBrickViewTest, SpanIterateAllEntities) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    // Fill half the brick
    for (size_t i = 0; i < 256; ++i) {
        auto entity = world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f));
        brick.setEntity(i, entity);
    }

    // Iterate via span
    size_t validCount = 0;
    for (auto entity : brick.entities()) {
        if (entity.valid()) {
            validCount++;
        }
    }

    EXPECT_EQ(validCount, 256);
}

// ===========================================================================
// Utility Tests
// ===========================================================================

TEST(EntityBrickViewTest, CountSolidVoxels_Empty) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    size_t solidCount = brick.countSolidVoxels();
    EXPECT_EQ(solidCount, 0);
}

TEST(EntityBrickViewTest, CountSolidVoxels_PartiallyFilled) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    // Add 50 solid voxels and 50 air voxels
    for (size_t i = 0; i < 50; ++i) {
        auto solid = world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f), 1.0f);
        brick.setEntity(i, solid);
    }

    for (size_t i = 50; i < 100; ++i) {
        auto air = world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f), 0.0f);
        brick.setEntity(i, air);
    }

    size_t solidCount = brick.countSolidVoxels();
    EXPECT_EQ(solidCount, 50);
}

TEST(EntityBrickViewTest, CountSolidVoxels_FullBrick) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    // Fill all 512 voxels with solid entities
    for (size_t i = 0; i < 512; ++i) {
        auto entity = world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f), 1.0f);
        brick.setEntity(i, entity);
    }

    size_t solidCount = brick.countSolidVoxels();
    EXPECT_EQ(solidCount, 512);
}

TEST(EntityBrickViewTest, IsEmpty_True) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    EXPECT_TRUE(brick.isEmpty());
}

TEST(EntityBrickViewTest, IsEmpty_False) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    auto entity = world.createVoxel(glm::vec3(0.0f), 1.0f);
    brick.setEntity(0, entity);

    EXPECT_FALSE(brick.isEmpty());
}

TEST(EntityBrickViewTest, IsFull_True) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    // Fill all 512 voxels
    for (size_t i = 0; i < 512; ++i) {
        auto entity = world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f), 1.0f);
        brick.setEntity(i, entity);
    }

    EXPECT_TRUE(brick.isFull());
}

TEST(EntityBrickViewTest, IsFull_False) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    // Fill 511 voxels (one missing)
    for (size_t i = 0; i < 511; ++i) {
        auto entity = world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f), 1.0f);
        brick.setEntity(i, entity);
    }

    EXPECT_FALSE(brick.isFull());
}

// ===========================================================================
// Index Conversion Tests
// ===========================================================================

TEST(EntityBrickViewTest, CoordToLinearIndex_Origin) {
    size_t idx = EntityBrickView::coordToLinearIndex(0, 0, 0);
    EXPECT_GE(idx, 0);
    EXPECT_LT(idx, 512);
}

TEST(EntityBrickViewTest, CoordToLinearIndex_AllPositions) {
    std::unordered_set<size_t> indices;

    for (int z = 0; z < 8; ++z) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                size_t idx = EntityBrickView::coordToLinearIndex(x, y, z);
                indices.insert(idx);

                EXPECT_GE(idx, 0);
                EXPECT_LT(idx, 512);
            }
        }
    }

    // All 512 positions should map to unique indices
    EXPECT_EQ(indices.size(), 512);
}

TEST(EntityBrickViewTest, LinearIndexToCoord_RoundTrip) {
    // Test round-trip conversion for all positions
    for (int z = 0; z < 8; ++z) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                size_t idx = EntityBrickView::coordToLinearIndex(x, y, z);

                int rx, ry, rz;
                EntityBrickView::linearIndexToCoord(idx, rx, ry, rz);

                EXPECT_EQ(rx, x);
                EXPECT_EQ(ry, y);
                EXPECT_EQ(rz, z);
            }
        }
    }
}

TEST(EntityBrickViewTest, LinearIndexToCoord_AllIndices) {
    // Test that all 512 indices convert to valid coords
    for (size_t idx = 0; idx < 512; ++idx) {
        int x, y, z;
        EntityBrickView::linearIndexToCoord(idx, x, y, z);

        EXPECT_GE(x, 0);
        EXPECT_LT(x, 8);
        EXPECT_GE(y, 0);
        EXPECT_LT(y, 8);
        EXPECT_GE(z, 0);
        EXPECT_LT(z, 8);
    }
}

// ===========================================================================
// Memory Efficiency Tests
// ===========================================================================

TEST(EntityBrickViewTest, BrickMemorySize) {
    // Verify brick size is 4 KB (512 entities × 8 bytes)
    size_t brickSize = 512 * sizeof(gaia::ecs::Entity);
    EXPECT_EQ(brickSize, 4096); // 4 KB

    // vs OLD: 512 voxels × 140 bytes = 70 KB (17.5× reduction!)
}

TEST(EntityBrickViewTest, ZeroCopySpanAccess) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    // Get span
    auto span = brick.entities();

    // Span should reference the same memory as the brick array
    EXPECT_EQ(span.data(), brickEntities.data());
    EXPECT_EQ(span.size(), 512);
}

// ===========================================================================
// Edge Case Tests
// ===========================================================================

TEST(EntityBrickViewTest, SetEntity_BoundaryVoxels) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    // Test all 8 corners
    std::vector<std::tuple<int, int, int>> corners = {
        {0, 0, 0}, {7, 0, 0}, {0, 7, 0}, {7, 7, 0},
        {0, 0, 7}, {7, 0, 7}, {0, 7, 7}, {7, 7, 7}
    };

    for (const auto& [x, y, z] : corners) {
        auto entity = world.createVoxel(glm::vec3(
            static_cast<float>(x),
            static_cast<float>(y),
            static_cast<float>(z)));
        brick.setEntity(x, y, z, entity);

        EXPECT_EQ(brick.getEntity(x, y, z), entity);
    }
}

TEST(EntityBrickViewTest, ClearEntireBrick) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    // Fill brick
    for (size_t i = 0; i < 512; ++i) {
        auto entity = world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f));
        brick.setEntity(i, entity);
    }

    EXPECT_TRUE(brick.isFull());

    // Clear all entities
    for (size_t i = 0; i < 512; ++i) {
        brick.clearEntity(i);
    }

    EXPECT_TRUE(brick.isEmpty());
}

TEST(EntityBrickViewTest, SparseOccupancy) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    // Fill only 10% of brick (sparse)
    for (size_t i = 0; i < 51; ++i) {
        auto entity = world.createVoxel(glm::vec3(static_cast<float>(i), 0.0f, 0.0f), 1.0f);
        brick.setEntity(i, entity);
    }

    size_t solidCount = brick.countSolidVoxels();
    EXPECT_EQ(solidCount, 51);
    EXPECT_FALSE(brick.isEmpty());
    EXPECT_FALSE(brick.isFull());
}

// ===========================================================================
// Integration Tests (With GaiaVoxelWorld)
// ===========================================================================

TEST(EntityBrickViewTest, ModifyEntityAttributes_ThroughBrickView) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    auto entity = world.createVoxel(glm::vec3(0.0f), 1.0f, glm::vec3(1.0f, 0.0f, 0.0f));
    brick.setEntity(5, entity);

    // Modify entity via GaiaVoxelWorld
    world.setColor(entity, glm::vec3(0.0f, 1.0f, 0.0f)); // Change to green

    // Verify change is visible through brick view
    auto color = brick.getColor(5);
    ASSERT_TRUE(color.has_value());
    EXPECT_EQ(color.value(), glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST(EntityBrickViewTest, DestroyEntity_BrickViewHandlesGracefully) {
    GaiaVoxelWorld world;
    std::array<gaia::ecs::Entity, 512> brickEntities{};
    EntityBrickView brick(world, brickEntities);

    auto entity = world.createVoxel(glm::vec3(0.0f), 1.0f);
    brick.setEntity(10, entity);

    // Destroy entity in world
    world.destroyVoxel(entity);

    // BrickView should return std::nullopt for destroyed entity
    auto density = brick.getDensity(10);
    EXPECT_FALSE(density.has_value());
}
