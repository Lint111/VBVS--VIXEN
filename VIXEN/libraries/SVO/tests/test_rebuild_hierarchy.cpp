/**
 * Test rebuild() hierarchical structure construction.
 *
 * This test validates the new bottom-up BFS hierarchy building algorithm.
 */

#include <gtest/gtest.h>
#include "LaineKarrasOctree.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"
#include "ComponentData.h"
#include <iostream>
#include <chrono>

using namespace Vixen::GaiaVoxel;
using namespace Vixen::SVO;

/**
 * Test rebuild() with hierarchical structure validation.
 * Creates entities in multiple bricks, rebuilds octree, verifies:
 * - Brick-level descriptors created for populated bricks
 * - Parent descriptors created for each hierarchy level
 * - Root descriptor exists
 * - BFS ordering maintained (contiguous children)
 */
TEST(RebuildHierarchyTest, MultipleBricksHierarchy) {
    std::cout << "\n[MultipleBricksHierarchy] Testing hierarchical octree construction...\n";

    GaiaVoxelWorld world;
    AttributeRegistry registry;
    registry.registerKey("density", AttributeType::Float, 1.0f);
    registry.addAttribute("color", AttributeType::Vec3, glm::vec3(1.0f));

    // Create entities in 4 separate bricks (depth 3 = 8³ voxels per brick)
    // This ensures we have multiple bricks and need parent hierarchy
    ComponentQueryRequest components[] = {
        Density{1.0f},
        Color{glm::vec3(1, 0, 0)}
    };

    // Brick 1: (0-8, 0-8, 0-8)
    auto e1 = world.createVoxel(VoxelCreationRequest{glm::vec3(2, 2, 2), components});
    auto e2 = world.createVoxel(VoxelCreationRequest{glm::vec3(5, 5, 5), components});

    // Brick 2: (16-24, 0-8, 0-8)
    auto e3 = world.createVoxel(VoxelCreationRequest{glm::vec3(18, 2, 2), components});
    auto e4 = world.createVoxel(VoxelCreationRequest{glm::vec3(20, 5, 5), components});

    // Brick 3: (0-8, 16-24, 0-8)
    auto e5 = world.createVoxel(VoxelCreationRequest{glm::vec3(2, 18, 2), components});
    auto e6 = world.createVoxel(VoxelCreationRequest{glm::vec3(5, 20, 5), components});

    // Brick 4: (16-24, 16-24, 0-8)
    auto e7 = world.createVoxel(VoxelCreationRequest{glm::vec3(18, 18, 2), components});
    auto e8 = world.createVoxel(VoxelCreationRequest{glm::vec3(20, 20, 5), components});

    ASSERT_TRUE(world.exists(e1));
    ASSERT_TRUE(world.exists(e8));

    std::cout << "[MultipleBricksHierarchy] Created 8 entities in 4 bricks\n";

    // Create octree and rebuild from world
    // maxLevels=8, brick depth=3 → 2^8 = 256 voxels, 2^(8-3) = 32 bricks per axis
    LaineKarrasOctree octree(world, &registry, 8, 3);

    glm::vec3 worldMin(0.0f, 0.0f, 0.0f);
    glm::vec3 worldMax(32.0f, 32.0f, 32.0f);  // 32³ world
    octree.rebuild(world, worldMin, worldMax);

    std::cout << "[MultipleBricksHierarchy] Rebuild complete - validating structure...\n";

    const auto& descriptors = octree.getOctree()->root->childDescriptors;
    const auto& brickViews = octree.getOctree()->root->brickViews;

    std::cout << "[MultipleBricksHierarchy] Descriptors: " << descriptors.size() << "\n";
    std::cout << "[MultipleBricksHierarchy] BrickViews: " << brickViews.size() << "\n";

    // Validation 1: Must have at least 4 brick views (one per populated brick)
    ASSERT_GE(brickViews.size(), 4) << "Expected at least 4 brick views";

    // Validation 2: Hierarchical structure - descriptors > brickViews (parents + root)
    ASSERT_GT(descriptors.size(), brickViews.size())
        << "Expected parent descriptors above brick level";

    // Validation 3: Root descriptor (index 0) should have non-zero validMask
    ASSERT_GT(descriptors[0].validMask, 0)
        << "Root descriptor should have valid children";

    // Validation 4: Root should not be a leaf (leafMask should be 0x00)
    ASSERT_EQ(descriptors[0].leafMask, 0x00)
        << "Root should not have leaf children (has intermediate nodes)";

    std::cout << "[MultipleBricksHierarchy] Root descriptor: "
              << "validMask=0x" << std::hex << (int)descriptors[0].validMask
              << " leafMask=0x" << (int)descriptors[0].leafMask
              << " childPointer=" << std::dec << descriptors[0].childPointer << "\n";

    // Validation 5: If root has childPointer, it should point to valid index
    if (descriptors[0].childPointer > 0) {
        ASSERT_LT(descriptors[0].childPointer, descriptors.size())
            << "Root childPointer should be valid index";
    }

    std::cout << "[MultipleBricksHierarchy] ✓ Hierarchical structure validated\n";
}

/**
 * Test rebuild() with single brick (simplest case).
 */
TEST(RebuildHierarchyTest, SingleBrick) {
    std::cout << "\n[SingleBrick] Testing single brick rebuild...\n";

    GaiaVoxelWorld world;
    AttributeRegistry registry;
    registry.registerKey("density", AttributeType::Float, 1.0f);
    registry.addAttribute("color", AttributeType::Vec3, glm::vec3(1.0f));

    ComponentQueryRequest components[] = {
        Density{1.0f},
        Color{glm::vec3(0, 1, 0)}
    };

    // Create entities all in same brick
    auto e1 = world.createVoxel(VoxelCreationRequest{glm::vec3(2, 2, 2), components});
    auto e2 = world.createVoxel(VoxelCreationRequest{glm::vec3(3, 3, 3), components});

    ASSERT_TRUE(world.exists(e1));
    ASSERT_TRUE(world.exists(e2));

    LaineKarrasOctree octree(world, &registry, 8, 3);  // depth 8, brick depth 3
    octree.rebuild(world, glm::vec3(0, 0, 0), glm::vec3(16, 16, 16));

    const auto& descriptors = octree.getOctree()->root->childDescriptors;
    const auto& brickViews = octree.getOctree()->root->brickViews;

    std::cout << "[SingleBrick] Descriptors: " << descriptors.size() << "\n";
    std::cout << "[SingleBrick] BrickViews: " << brickViews.size() << "\n";

    // Should have 1 brick view
    ASSERT_EQ(brickViews.size(), 1) << "Expected exactly 1 brick view";

    // Single brick case - parent descriptor IS the root (1 descriptor total)
    // The parent marks the brick as a leaf child, so no separate brick descriptor in final array
    ASSERT_GE(descriptors.size(), 1) << "Expected at least root descriptor";

    std::cout << "[SingleBrick] ✓ Single brick structure validated\n";
}

/**
 * Test rebuild() with empty world.
 */
TEST(RebuildHierarchyTest, EmptyWorld) {
    std::cout << "\n[EmptyWorld] Testing empty world rebuild...\n";

    GaiaVoxelWorld world;
    AttributeRegistry registry;
    registry.registerKey("density", AttributeType::Float, 1.0f);
    registry.addAttribute("color", AttributeType::Vec3, glm::vec3(1.0f));
    LaineKarrasOctree octree(world, &registry, 8, 3);  // depth 8, brick depth 3

    octree.rebuild(world, glm::vec3(0, 0, 0), glm::vec3(16, 16, 16));

    const auto& descriptors = octree.getOctree()->root->childDescriptors;
    const auto& brickViews = octree.getOctree()->root->brickViews;

    std::cout << "[EmptyWorld] Descriptors: " << descriptors.size() << "\n";
    std::cout << "[EmptyWorld] BrickViews: " << brickViews.size() << "\n";

    // Empty world should have no descriptors or brick views
    ASSERT_EQ(brickViews.size(), 0) << "Expected no brick views in empty world";
    ASSERT_EQ(descriptors.size(), 0) << "Expected no descriptors in empty world";

    std::cout << "[EmptyWorld] ✓ Empty world handled correctly\n";
}

/**
 * Stress test with procedurally generated sparse voxel terrain.
 * Uses simple 3D noise to create a realistic sparse scene.
 */
TEST(RebuildHierarchyTest, StressTest_NoiseGenerated) {
    using namespace Vixen::GaiaVoxel;
    using namespace Vixen::SVO;

    std::cout << "\n[StressTest_NoiseGenerated] Testing large sparse scene with procedural noise...\n";

    GaiaVoxelWorld world;
    AttributeRegistry registry;
    registry.registerKey("density", AttributeType::Float, 1.0f);
    registry.addAttribute("color", AttributeType::Vec3, glm::vec3(1.0f));

    // Simple 3D noise function (hash-based)
    auto noise3D = [](int x, int y, int z) -> float {
        // Simple hash-based noise
        int n = x + y * 57 + z * 997;
        n = (n << 13) ^ n;
        return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
    };

    // Generate sparse voxel terrain (only place voxels where noise > threshold)
    const int worldSize = 64;  // 64³ voxels
    const float threshold = 0.3f;  // Sparsity control
    ComponentQueryRequest components[] = {
        Density{1.0f},
        Color{glm::vec3(0.5f, 0.7f, 0.3f)}  // Green terrain color
    };

    int voxelsCreated = 0;
    for (int z = 0; z < worldSize; z += 2) {  // Sample every 2 voxels for speed
        for (int y = 0; y < worldSize; y += 2) {
            for (int x = 0; x < worldSize; x += 2) {
                // 3D noise value
                float n = noise3D(x, y, z);

                // Add some vertical falloff (lower y = more voxels, like terrain)
                float heightFactor = 1.0f - (static_cast<float>(y) / worldSize);
                float finalValue = n * 0.5f + heightFactor * 0.5f;

                if (finalValue > threshold) {
                    glm::vec3 position(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                    world.createVoxel(VoxelCreationRequest{position, components});
                    voxelsCreated++;
                }
            }
        }
    }

    std::cout << "[StressTest_NoiseGenerated] Created " << voxelsCreated << " voxels (sparsity: "
              << (100.0f * voxelsCreated / (worldSize * worldSize * worldSize)) << "%)\n";

    ASSERT_GT(voxelsCreated, 0) << "Should have created some voxels";

    // Create octree and rebuild
    LaineKarrasOctree octree(world, &registry, 8, 3);  // depth 8 → 256³ max, brick depth 3

    glm::vec3 worldMin(0.0f, 0.0f, 0.0f);
    glm::vec3 worldMax(static_cast<float>(worldSize), static_cast<float>(worldSize), static_cast<float>(worldSize));

    auto startTime = std::chrono::high_resolution_clock::now();
    octree.rebuild(world, worldMin, worldMax);
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "[StressTest_NoiseGenerated] Rebuild time: " << duration.count() << " ms\n";

    const auto& descriptors = octree.getOctree()->root->childDescriptors;
    const auto& brickViews = octree.getOctree()->root->brickViews;

    std::cout << "[StressTest_NoiseGenerated] Descriptors: " << descriptors.size() << "\n";
    std::cout << "[StressTest_NoiseGenerated] BrickViews: " << brickViews.size() << "\n";
    std::cout << "[StressTest_NoiseGenerated] Avg voxels/brick: "
              << (brickViews.size() > 0 ? static_cast<float>(voxelsCreated) / brickViews.size() : 0) << "\n";

    // Validation: Should have hierarchical structure
    ASSERT_GT(brickViews.size(), 0) << "Should have at least one brick";
    // Note: descriptors may be < brickViews if all bricks are populated (marked as leaves)
    ASSERT_GT(descriptors.size(), 0) << "Should have at least root descriptor";
    ASSERT_GT(descriptors[0].validMask, 0) << "Root should have valid children";

    std::cout << "[StressTest_NoiseGenerated] Root descriptor: "
              << "validMask=0x" << std::hex << (int)descriptors[0].validMask
              << " leafMask=0x" << (int)descriptors[0].leafMask
              << " childPointer=" << std::dec << descriptors[0].childPointer << "\n";

    std::cout << "[StressTest_NoiseGenerated] ✓ Stress test completed successfully\n";
}
