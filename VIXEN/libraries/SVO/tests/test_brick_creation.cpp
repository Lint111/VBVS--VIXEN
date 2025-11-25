#include <gtest/gtest.h>
#include "LaineKarrasOctree.h"
#include "SVOBuilder.h"
#include <AttributeRegistry.h>
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"
#include "ComponentData.h"
#include <iostream>

using namespace SVO;
using namespace GaiaVoxel;

/**
 * Helper to create voxel in GaiaVoxelWorld with standard components.
 */
GaiaVoxelWorld::EntityID createVoxel(
    GaiaVoxelWorld& world,
    const glm::vec3& position,
    float density,
    const glm::vec3& color,
    const glm::vec3& normal)
{
    ComponentQueryRequest comps[] = {
        Density{density},
        Color{color},
        Normal{normal}
    };
    VoxelCreationRequest req{position, comps};
    return world.createVoxel(req);
}

// Test that bricks are created when building octree from GaiaVoxelWorld
TEST(BrickCreationTest, BricksAreAllocatedFromGaiaWorld) {
    GaiaVoxelWorld world;

    // Create AttributeRegistry with density as key attribute
    auto registry = std::make_shared<::VoxelData::AttributeRegistry>();
    registry->registerKey("density", ::VoxelData::AttributeType::Float, 0.0f);
    registry->addAttribute("color", ::VoxelData::AttributeType::Vec3, glm::vec3(1.0f));
    registry->addAttribute("normal", ::VoxelData::AttributeType::Vec3, glm::vec3(0, 1, 0));

    // Create sphere of voxels using GaiaVoxelWorld
    glm::vec3 sphereCenter(50.0f, 50.0f, 50.0f);
    float sphereRadius = 30.0f;
    int voxelCount = 0;

    for (float x = 20.0f; x <= 80.0f; x += 5.0f) {
        for (float y = 20.0f; y <= 80.0f; y += 5.0f) {
            for (float z = 20.0f; z <= 80.0f; z += 5.0f) {
                glm::vec3 pos(x, y, z);
                float dist = glm::length(pos - sphereCenter);
                if (dist < sphereRadius) {
                    glm::vec3 normal = glm::normalize(pos - sphereCenter);
                    createVoxel(world, pos, 1.0f, glm::vec3(1, 0, 0), normal);
                    voxelCount++;
                }
            }
        }
    }

    std::cout << "Created " << voxelCount << " voxels in sphere\n";

    // Create octree from GaiaVoxelWorld
    LaineKarrasOctree octree(world, registry.get(), 8, 3);

    // Rebuild octree from entities
    glm::vec3 worldMin(0.0f);
    glm::vec3 worldMax(100.0f);
    octree.rebuild(world, worldMin, worldMax);

    std::cout << "Octree rebuilt from GaiaVoxelWorld entities\n";

    // Verify octree structure exists
    ASSERT_NE(octree.getOctree(), nullptr);
}

// Test that ray casting works with octree built from GaiaVoxelWorld
TEST(BrickCreationTest, RayCastingWithGaiaWorldOctree) {
    GaiaVoxelWorld world;

    // Create AttributeRegistry
    auto registry = std::make_shared<::VoxelData::AttributeRegistry>();
    registry->registerKey("density", ::VoxelData::AttributeType::Float, 0.0f);
    registry->addAttribute("color", ::VoxelData::AttributeType::Vec3, glm::vec3(1.0f));
    registry->addAttribute("normal", ::VoxelData::AttributeType::Vec3, glm::vec3(0, 1, 0));

    // Create a box of voxels (40-60 in each dimension)
    int voxelCount = 0;
    for (float x = 40.0f; x <= 60.0f; x += 2.0f) {
        for (float y = 40.0f; y <= 60.0f; y += 2.0f) {
            for (float z = 40.0f; z <= 60.0f; z += 2.0f) {
                createVoxel(world, glm::vec3(x, y, z), 1.0f, glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
                voxelCount++;
            }
        }
    }

    std::cout << "Created " << voxelCount << " voxels in box\n";

    // Build octree from GaiaVoxelWorld
    LaineKarrasOctree octree(world, registry.get(), 7, 3);
    octree.rebuild(world, glm::vec3(0.0f), glm::vec3(100.0f));

    // Cast a ray through the box
    glm::vec3 origin(50, 50, 0);
    glm::vec3 direction(0, 0, 1);

    auto result = octree.castRay(origin, direction);

    if (result.hit) {
        std::cout << "Ray hit at t=" << result.tMin << " pos=("
                  << result.position.x << "," << result.position.y << ","
                  << result.position.z << ")\n";

        // The hit should be near the box front
        EXPECT_GE(result.position.z, 39.0f);
        EXPECT_LE(result.position.z, 61.0f);
    }
}

// Test querying voxel data through GaiaVoxelWorld after hit
TEST(BrickCreationTest, EntityDataQueryAfterRayHit) {
    GaiaVoxelWorld world;

    // Create AttributeRegistry
    auto registry = std::make_shared<::VoxelData::AttributeRegistry>();
    registry->registerKey("density", ::VoxelData::AttributeType::Float, 0.0f);

    // Create voxels with gradient density
    for (float x = 10.0f; x <= 90.0f; x += 10.0f) {
        float density = x / 100.0f;
        createVoxel(world, glm::vec3(x, 50.0f, 50.0f), density, glm::vec3(density, 0, 0), glm::vec3(1, 0, 0));
    }

    // Build octree
    LaineKarrasOctree octree(world, nullptr, 6, 3);
    octree.rebuild(world, glm::vec3(0.0f), glm::vec3(100.0f));

    // Cast ray along X axis
    glm::vec3 origin(0, 50, 50);
    glm::vec3 direction(1, 0, 0);

    auto result = octree.castRay(origin, direction);

    if (result.hit && world.exists(result.entity)) {
        // Query component from the hit entity
        auto density = world.getComponentValue<Density>(result.entity);
        if (density.has_value()) {
            std::cout << "Hit entity has density: " << density.value() << "\n";
            EXPECT_GT(density.value(), 0.0f);
        }

        auto color = world.getComponentValue<Color>(result.entity);
        if (color.has_value()) {
            std::cout << "Hit entity has color: (" << color.value().r << ", "
                      << color.value().g << ", " << color.value().b << ")\n";
        }
    }
}

// Test multiple voxels at different positions
TEST(BrickCreationTest, MultipleVoxelPositions) {
    GaiaVoxelWorld world;

    // Create voxels at specific positions
    std::vector<std::pair<glm::vec3, gaia::ecs::Entity>> voxels;

    glm::vec3 positions[] = {
        glm::vec3(10, 10, 10),
        glm::vec3(50, 50, 50),
        glm::vec3(90, 90, 90),
        glm::vec3(25, 75, 50),
        glm::vec3(75, 25, 50)
    };

    for (const auto& pos : positions) {
        auto entity = createVoxel(world, pos, 1.0f, glm::vec3(1, 1, 1), glm::vec3(0, 1, 0));
        voxels.push_back({pos, entity});
    }

    // Verify all entities exist
    for (const auto& [pos, entity] : voxels) {
        ASSERT_TRUE(world.exists(entity)) << "Entity at (" << pos.x << ", " << pos.y << ", " << pos.z << ") should exist";

        auto retrievedPos = world.getPosition(entity);
        ASSERT_TRUE(retrievedPos.has_value());
        EXPECT_EQ(retrievedPos.value(), pos);
    }

    // Build octree
    LaineKarrasOctree octree(world, nullptr, 8, 3);
    octree.rebuild(world, glm::vec3(0.0f), glm::vec3(100.0f));

    std::cout << "Created octree with " << voxels.size() << " voxels at distinct positions\n";
}

// Test dense voxel grid
TEST(BrickCreationTest, DenseVoxelGrid) {
    GaiaVoxelWorld world;

    // Create a dense grid of voxels (8x8x8 = 512 voxels)
    int voxelCount = 0;
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            for (int z = 0; z < 8; z++) {
                glm::vec3 pos(x * 2.0f + 40.0f, y * 2.0f + 40.0f, z * 2.0f + 40.0f);
                createVoxel(world, pos, 1.0f, glm::vec3(x/7.0f, y/7.0f, z/7.0f), glm::vec3(0, 1, 0));
                voxelCount++;
            }
        }
    }

    EXPECT_EQ(voxelCount, 512);

    // Build octree
    LaineKarrasOctree octree(world, nullptr, 8, 3);
    octree.rebuild(world, glm::vec3(0.0f), glm::vec3(100.0f));

    // Cast rays to verify data
    auto result = octree.castRay(glm::vec3(48, 48, 0), glm::vec3(0, 0, 1));
    if (result.hit) {
        std::cout << "Dense grid hit at z=" << result.position.z << "\n";
        EXPECT_GE(result.position.z, 39.0f);
    }
}
