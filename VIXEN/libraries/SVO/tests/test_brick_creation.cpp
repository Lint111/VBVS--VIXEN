#include <gtest/gtest.h>
#include "VoxelInjection.h"
#include "LaineKarrasOctree.h"
#include "BrickStorage.h"
#include "SVOBuilder.h"
#include <iostream>

using namespace SVO;

// Test that bricks are actually created when brickDepthLevels > 0
TEST(BrickCreationTest, BricksAreAllocatedAtCorrectDepth) {
    // Create a sphere sampler that generates solid voxels
    auto sampler = std::make_unique<LambdaVoxelSampler>(
        [](const glm::vec3& pos, VoxelData& data) -> bool {
            float dist = glm::length(pos - glm::vec3(50.0f));
            if (dist < 30.0f) {
                data.position = pos;
                data.color = glm::vec3(1, 0, 0);
                data.normal = glm::normalize(pos - glm::vec3(50.0f));
                data.density = 1.0f;
                return true;
            }
            return false;
        },
        [](glm::vec3& min, glm::vec3& max) {
            min = glm::vec3(0);
            max = glm::vec3(100);
        },
        [](const glm::vec3& center, float size) -> float {
            float dist = glm::length(center - glm::vec3(50.0f));
            if (dist > 30.0f + size) return 0.0f;  // Outside
            if (dist < 30.0f - size) return 1.0f;  // Inside
            return 0.5f;  // Partially inside
        }
    );

    // Configure with brick depth levels
    InjectionConfig config;
    config.maxLevels = 8;
    config.brickDepthLevels = 3;  // Bricks at depth 5 (8-3)
    config.minVoxelSize = 0.1f;

    // Create brick storage (depth 3 = 8x8x8 voxels per brick, capacity 1024 bricks)
    auto brickStorage = std::make_shared<BrickStorage<DefaultLeafData>>(3, 1024);

    // Inject with brick storage
    VoxelInjector injector(brickStorage.get());
    auto svo = injector.inject(*sampler, config);

    ASSERT_NE(svo, nullptr);

    // Verify bricks were created
    const auto& stats = injector.getLastStats();
    std::cout << "Injection stats:\n";
    std::cout << "  Voxels processed: " << stats.voxelsProcessed << "\n";
    std::cout << "  Leaves created: " << stats.leavesCreated << "\n";
    std::cout << "  Empty culled: " << stats.emptyVoxelsCulled << "\n";

    // Check brick references were populated
    auto octree = dynamic_cast<LaineKarrasOctree*>(svo.get());
    ASSERT_NE(octree, nullptr);
    ASSERT_NE(octree->getOctree(), nullptr);
    ASSERT_NE(octree->getOctree()->root, nullptr);

    size_t brickCount = octree->getOctree()->root->brickReferences.size();
    std::cout << "Brick references created: " << brickCount << "\n";

    // With brickDepthLevels=3, we should have brick references
    // The count should match the number of leaf descriptors
    EXPECT_GT(brickCount, 0) << "No bricks were created despite brickDepthLevels=3";

    // Verify brick storage has allocated bricks
    size_t allocatedBricks = brickStorage->getBrickCount();
    std::cout << "Bricks allocated in storage: " << allocatedBricks << "\n";
    EXPECT_GT(allocatedBricks, 0) << "BrickStorage has no allocated bricks";

    // Count non-empty brick references (bricks with actual data)
    size_t solidBricks = 0;
    for (const auto& brickRef : octree->getOctree()->root->brickReferences) {
        if (brickRef.brickID != 0xFFFFFFFF) {  // Valid brick ID
            solidBricks++;
        }
    }
    std::cout << "Solid bricks (non-empty): " << solidBricks << "\n";
    EXPECT_GT(solidBricks, 0) << "All brick references are empty";
}

// Test that ray casting finds and traverses bricks
TEST(BrickCreationTest, RayCastingEntersBrickTraversal) {
    // Create a simple box sampler
    auto sampler = std::make_unique<LambdaVoxelSampler>(
        [](const glm::vec3& pos, VoxelData& data) -> bool {
            if (pos.x >= 40 && pos.x <= 60 &&
                pos.y >= 40 && pos.y <= 60 &&
                pos.z >= 40 && pos.z <= 60) {
                data.position = pos;
                data.color = glm::vec3(0, 1, 0);
                data.normal = glm::vec3(0, 1, 0);
                data.density = 1.0f;
                return true;
            }
            return false;
        },
        [](glm::vec3& min, glm::vec3& max) {
            min = glm::vec3(0);
            max = glm::vec3(100);
        },
        [](const glm::vec3& center, float size) -> float {
            glm::vec3 boxMin(40), boxMax(60);
            glm::vec3 cubeMin = center - glm::vec3(size);
            glm::vec3 cubeMax = center + glm::vec3(size);

            if (cubeMax.x < boxMin.x || cubeMin.x > boxMax.x ||
                cubeMax.y < boxMin.y || cubeMin.y > boxMax.y ||
                cubeMax.z < boxMin.z || cubeMin.z > boxMax.z) {
                return 0.0f;  // No overlap
            }
            if (cubeMin.x >= boxMin.x && cubeMax.x <= boxMax.x &&
                cubeMin.y >= boxMin.y && cubeMax.y <= boxMax.y &&
                cubeMin.z >= boxMin.z && cubeMax.z <= boxMax.z) {
                return 1.0f;  // Fully inside
            }
            return 0.5f;  // Partial overlap
        }
    );

    // Configure with brick depth
    InjectionConfig config;
    config.maxLevels = 7;
    config.brickDepthLevels = 3;  // Bricks at depth 4

    // Create brick storage (depth 3 = 8x8x8 voxels per brick, capacity 512 bricks)
    auto brickStorage = std::make_shared<BrickStorage<DefaultLeafData>>(3, 512);

    // Build octree with bricks
    VoxelInjector injector(brickStorage.get());
    auto svo = injector.inject(*sampler, config);

    ASSERT_NE(svo, nullptr);
    auto octreeStruct = dynamic_cast<LaineKarrasOctree*>(svo.get());
    ASSERT_NE(octreeStruct, nullptr);
    auto octree = octreeStruct->getOctree();
    ASSERT_NE(octree, nullptr);

    // Verify we have bricks
    ASSERT_GT(octree->root->brickReferences.size(), 0) << "No bricks to test traversal";

    // Cast a ray through the box using existing octree structure
    glm::vec3 origin(50, 50, 0);
    glm::vec3 direction(0, 0, 1);

    auto result = octreeStruct->castRay(origin, direction);

    EXPECT_TRUE(result.hit) << "Ray should hit the box";
    if (result.hit) {
        std::cout << "Ray hit at t=" << result.tMin << " pos=("
                  << result.position.x << "," << result.position.y << ","
                  << result.position.z << ")\n";

        // The hit should be within the box bounds
        // Note: Due to brick boundaries, the hit might not be exactly at z=40
        // Bricks align to octree nodes, not the exact box boundaries
        EXPECT_GE(result.position.z, 39.0f);  // Should be near or past the box front
        EXPECT_LE(result.position.z, 60.0f);  // Should be before the box back

        // Check if brick traversal was used
        // This would be indicated by the hit being inside a brick leaf
        // We can't directly test if traverseBrick() was called without adding instrumentation
        // but we can verify the hit is consistent with brick resolution
    }
}

// Test brick density queries
TEST(BrickCreationTest, BrickDensityQueries) {
    // Create a gradient sampler
    auto sampler = std::make_unique<LambdaVoxelSampler>(
        [](const glm::vec3& pos, VoxelData& data) -> bool {
            // Gradient along X axis
            data.position = pos;
            data.density = pos.x / 100.0f;  // 0 to 1 gradient
            data.color = glm::vec3(data.density, 0, 0);
            data.normal = glm::vec3(1, 0, 0);
            return data.density > 0.1f;  // Only solid above 10%
        },
        [](glm::vec3& min, glm::vec3& max) {
            min = glm::vec3(0);
            max = glm::vec3(100, 100, 100);
        },
        [](const glm::vec3& center, float size) -> float {
            float minX = center.x - size;
            float maxX = center.x + size;
            if (maxX < 10.0f) return 0.0f;  // Empty region
            if (minX > 10.0f) return 1.0f;  // Solid region
            return 0.5f;  // Mixed
        }
    );

    // Configure with bricks
    InjectionConfig config;
    config.maxLevels = 6;
    config.brickDepthLevels = 3;

    // Create brick storage (depth 3 = 8x8x8 voxels per brick, capacity 256 bricks)
    auto brickStorage = std::make_shared<BrickStorage<DefaultLeafData>>(3, 256);

    // Build octree
    VoxelInjector injector(brickStorage.get());
    auto svo = injector.inject(*sampler, config);

    ASSERT_NE(svo, nullptr);
    auto octreeStruct = dynamic_cast<LaineKarrasOctree*>(svo.get());
    ASSERT_NE(octreeStruct, nullptr);
    auto octree = octreeStruct->getOctree();
    ASSERT_NE(octree, nullptr);

    // Verify bricks exist
    size_t validBricks = 0;
    for (const auto& brickRef : octree->root->brickReferences) {
        if (brickRef.brickID != 0xFFFFFFFF) {
            validBricks++;

            // Query brick density at a few positions
            uint32_t brickRes = 1u << brickRef.brickDepth;  // 2^depth = 8 for depth=3

            // Check corner voxels using the brick data arrays directly
            // BrickStorage uses template parameter 0 for density array
            float density0 = brickStorage->template get<0>(brickRef.brickID, 0);  // (0,0,0) in Morton order
            float density511 = brickStorage->template get<0>(brickRef.brickID, 511);  // (7,7,7) in Morton order

            // At least one voxel should have non-zero density
            EXPECT_TRUE(density0 > 0 || density511 > 0)
                << "Brick " << brickRef.brickID << " has no solid voxels";
        }
    }

    std::cout << "Valid bricks with data: " << validBricks << "\n";
    EXPECT_GT(validBricks, 0) << "No valid bricks found";
}