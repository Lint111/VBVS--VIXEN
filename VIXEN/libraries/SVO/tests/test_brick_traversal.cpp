#include <gtest/gtest.h>
#include "LaineKarrasOctree.h"
#include "AttributeRegistry.h"
#include "DynamicVoxelStruct.h"
#include <glm/glm.hpp>
#include "GaiaVoxelWorld.h"

using namespace SVO;
using namespace GaiaVoxel;

/**
 * Test suite for brick DDA traversal within LaineKarrasOctree.
 * Validates brick-to-leaf transitions, brick misses, and multi-brick traversal.
 */
class BrickTraversalTest : public ::testing::Test {
protected:
    void SetUp() override {

        voxelWorld = std::make_shared<GaiaVoxelWorld>();


        // Create registry and register attributes
        registry = std::make_shared<::VoxelData::AttributeRegistry>();
        registry->registerKey("density", ::VoxelData::AttributeType::Float, 1.0f);
        registry->addAttribute("color", ::VoxelData::AttributeType::Vec3, glm::vec3(1.0f));
        registry->addAttribute("normal", ::VoxelData::AttributeType::Vec3, glm::vec3(0, 1, 0));

        // Create a 10x10x10 world
        worldMin = glm::vec3(0, 0, 0);
        worldMax = glm::vec3(10, 10, 10);
        worldCenter = (worldMin + worldMax) * 0.5f;

    }

    // Helper to create octree with voxels and bricks
    std::unique_ptr<LaineKarrasOctree> createOctreeWithBricks(
        const std::vector<glm::vec3>& voxelPositions,
        int maxDepth = 8,
        int brickDepthLevels = 3)
    {
        // Create voxel entities in the world
        for (const auto& pos : voxelPositions) {
            ComponentQueryRequest components[] = {
                Density{1.0f},
                Color{glm::vec3(1.0f, 1.0f, 1.0f)}
            };
            VoxelCreationRequest request{pos, components};

            voxelWorld->createVoxel(request);
        }

        // Create octree with GaiaVoxelWorld and rebuild hierarchy
        auto octree = std::make_unique<LaineKarrasOctree>(
            *voxelWorld,
            registry.get(),
            maxDepth,  // maxLevels
            brickDepthLevels  // brickDepth (3 levels = 8x8x8 brick)
        );

        // Build ESVO hierarchy from entities
        octree->rebuild(*voxelWorld, worldMin, worldMax);
        return octree;
    }


    glm::vec3 worldMin, worldMax, worldCenter;    
    std::shared_ptr<::VoxelData::AttributeRegistry> registry;
    std::shared_ptr<GaiaVoxelWorld> voxelWorld;
};

// ============================================================================
// TEST 1: Brick Hit → Leaf Transition
// ============================================================================
TEST_F(BrickTraversalTest, BrickHitToLeafTransition) {
    // Create voxels that will be stored in a brick
    // With depth 8 and brickDepthLevels 3, bricks cover bottom 3 levels (8x8x8)
    std::vector<glm::vec3> voxels = {
        glm::vec3(5.0f, 5.0f, 5.0f),
        glm::vec3(5.1f, 5.1f, 5.1f),
        glm::vec3(5.2f, 5.2f, 5.2f),
    };

    auto octree = createOctreeWithBricks(voxels, 8, 3);

    // Cast ray toward voxels (should enter brick and hit leaf)
    glm::vec3 rayOrigin(-2.0f, 5.0f, 5.0f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);

    auto hit = octree->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    EXPECT_TRUE(hit.hit) << "Ray should traverse brick and hit leaf voxel";
    EXPECT_NEAR(hit.hitPoint.x, 5.0f, 2.0f) << "Hit should be near voxel cluster";
    EXPECT_NEAR(hit.hitPoint.y, 5.0f, 2.0f);
    EXPECT_NEAR(hit.hitPoint.z, 5.0f, 2.0f);

    std::cout << "Brick → Leaf transition: Hit at ("
              << hit.hitPoint.x << ", " << hit.hitPoint.y << ", " << hit.hitPoint.z << ")\n";
}

// ============================================================================
// TEST 2: Brick Miss → Grid Continuation
// ============================================================================
TEST_F(BrickTraversalTest, BrickMissReturnToGrid) {
    // Create sparse voxels: one in brick region, one outside brick region
    std::vector<glm::vec3> voxels = {
        glm::vec3(2.0f, 2.0f, 2.0f),  // In brick region
        glm::vec3(8.0f, 8.0f, 8.0f),  // Outside brick region
    };

    auto octree = createOctreeWithBricks(voxels, 8, 3);

    // Cast ray that enters brick region, misses brick voxels, continues to second voxel
    glm::vec3 rayOrigin(-2.0f, 8.0f, 8.0f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);

    auto hit = octree->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    EXPECT_TRUE(hit.hit) << "Ray should miss brick, continue grid, and hit second voxel";
    EXPECT_NEAR(hit.hitPoint.x, 8.0f, 2.0f) << "Should hit voxel outside brick region";
    EXPECT_NEAR(hit.hitPoint.y, 8.0f, 2.0f);
    EXPECT_NEAR(hit.hitPoint.z, 8.0f, 2.0f);

    std::cout << "Brick miss → Grid continuation: Hit at ("
              << hit.hitPoint.x << ", " << hit.hitPoint.y << ", " << hit.hitPoint.z << ")\n";
}

// ============================================================================
// TEST 3: Ray Through Multiple Bricks
// ============================================================================
TEST_F(BrickTraversalTest, RayThroughMultipleBricks) {
    // Create a line of voxels across multiple potential brick regions
    std::vector<glm::vec3> voxels;
    for (float x = 1.0f; x <= 9.0f; x += 1.0f) {
        voxels.push_back(glm::vec3(x, 5.0f, 5.0f));
    }

    auto octree = createOctreeWithBricks(voxels, 8, 3);

    // Cast ray along X axis through all voxels
    glm::vec3 rayOrigin(-2.0f, 5.0f, 5.0f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);

    auto hit = octree->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    EXPECT_TRUE(hit.hit) << "Ray should hit first voxel in line";
    EXPECT_LT(hit.hitPoint.x, 3.0f) << "Should hit first voxel around x=1";
    EXPECT_NEAR(hit.hitPoint.y, 5.0f, 1.0f);
    EXPECT_NEAR(hit.hitPoint.z, 5.0f, 1.0f);

    std::cout << "Multiple brick traversal: First hit at ("
              << hit.hitPoint.x << ", " << hit.hitPoint.y << ", " << hit.hitPoint.z << ")\n";
}

// ============================================================================
// TEST 4: Brick Boundary Grazing
// ============================================================================
TEST_F(BrickTraversalTest, BrickBoundaryGrazing) {
    // Create voxel at brick boundary
    // With 8x8x8 bricks, boundaries occur at multiples of brick size
    std::vector<glm::vec3> voxels = {
        glm::vec3(2.5f, 2.5f, 2.5f),  // Inside brick
    };

    auto octree = createOctreeWithBricks(voxels, 8, 3);

    // Cast ray that grazes brick boundary (near-parallel to boundary plane)
    glm::vec3 rayOrigin(2.49f, 2.0f, 0.0f);
    glm::vec3 rayDir = glm::normalize(glm::vec3(0.01f, 0.5f, 1.0f));

    auto hit = octree->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    // May or may not hit depending on precision - this tests that it doesn't crash
    if (hit.hit) {
        std::cout << "Grazing ray hit at ("
                  << hit.hitPoint.x << ", " << hit.hitPoint.y << ", " << hit.hitPoint.z << ")\n";
        EXPECT_NEAR(hit.hitPoint.x, 2.5f, 2.0f);
    } else {
        std::cout << "Grazing ray missed (acceptable for near-boundary case)\n";
    }

    SUCCEED() << "Brick boundary grazing handled without crash";
}

// ============================================================================
// TEST 5: Brick Edge Cases - Axis-Parallel Rays
// ============================================================================
TEST_F(BrickTraversalTest, BrickEdgeCases_AxisParallelRays) {
    // Create 3D grid of voxels in brick region
    std::vector<glm::vec3> voxels;
    for (float x = 2.0f; x <= 4.0f; x += 0.5f) {
        for (float y = 2.0f; y <= 4.0f; y += 0.5f) {
            for (float z = 2.0f; z <= 4.0f; z += 0.5f) {
                voxels.push_back(glm::vec3(x, y, z));
            }
        }
    }

    auto octree = createOctreeWithBricks(voxels, 8, 3);

    // Test axis-parallel rays through brick
    // +X ray
    {
        glm::vec3 rayOrigin(0.0f, 3.0f, 3.0f);
        glm::vec3 rayDir(1.0f, 0.0f, 0.0f);
        auto hit = octree->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

        EXPECT_TRUE(hit.hit) << "+X ray should hit brick voxels";
        EXPECT_NEAR(hit.hitPoint.y, 3.0f, 1.0f);
        EXPECT_NEAR(hit.hitPoint.z, 3.0f, 1.0f);
    }

    // +Y ray
    {
        glm::vec3 rayOrigin(3.0f, 0.0f, 3.0f);
        glm::vec3 rayDir(0.0f, 1.0f, 0.0f);
        auto hit = octree->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

        EXPECT_TRUE(hit.hit) << "+Y ray should hit brick voxels";
        EXPECT_NEAR(hit.hitPoint.x, 3.0f, 1.0f);
        EXPECT_NEAR(hit.hitPoint.z, 3.0f, 1.0f);
    }

    // +Z ray
    {
        glm::vec3 rayOrigin(3.0f, 3.0f, 0.0f);
        glm::vec3 rayDir(0.0f, 0.0f, 1.0f);
        auto hit = octree->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

        EXPECT_TRUE(hit.hit) << "+Z ray should hit brick voxels";
        EXPECT_NEAR(hit.hitPoint.x, 3.0f, 1.0f);
        EXPECT_NEAR(hit.hitPoint.y, 3.0f, 1.0f);
    }

    std::cout << "Axis-parallel brick traversal validated (X, Y, Z)\n";
}

// ============================================================================
// TEST 6: Dense Brick Volume
// ============================================================================
TEST_F(BrickTraversalTest, DenseBrickVolume) {
    // Fill entire 8x8x8 brick with voxels (512 voxels)
    std::vector<glm::vec3> voxels;
    float brickOrigin = 2.0f;
    float voxelSize = 0.125f; // 1/8 for 8x8x8 grid in unit cube

    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            for (int z = 0; z < 8; z++) {
                voxels.push_back(glm::vec3(
                    brickOrigin + x * voxelSize,
                    brickOrigin + y * voxelSize,
                    brickOrigin + z * voxelSize
                ));
            }
        }
    }

    auto octree = createOctreeWithBricks(voxels, 8, 3);

    // Ray should hit front face of dense brick
    glm::vec3 rayOrigin(0.0f, 2.5f, 2.5f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);

    auto hit = octree->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    EXPECT_TRUE(hit.hit) << "Ray should hit dense brick volume";
    EXPECT_NEAR(hit.hitPoint.x, brickOrigin, 0.5f) << "Should hit near brick front face";
    EXPECT_NEAR(hit.hitPoint.y, 2.5f, 0.5f);
    EXPECT_NEAR(hit.hitPoint.z, 2.5f, 0.5f);

    std::cout << "Dense brick volume: Hit at ("
              << hit.hitPoint.x << ", " << hit.hitPoint.y << ", " << hit.hitPoint.z << ")\n";
}

// ============================================================================
// TEST 7: Brick DDA Step Consistency
// ============================================================================
TEST_F(BrickTraversalTest, BrickDDAStepConsistency) {
    // Create checkerboard pattern in brick (alternating solid/empty)
    std::vector<glm::vec3> voxels;
    float brickOrigin = 3.0f;
    float voxelSize = 0.125f;

    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            for (int z = 0; z < 8; z++) {
                if ((x + y + z) % 2 == 0) { // Checkerboard
                    voxels.push_back(glm::vec3(
                        brickOrigin + x * voxelSize,
                        brickOrigin + y * voxelSize,
                        brickOrigin + z * voxelSize
                    ));
                }
            }
        }
    }

    auto octree = createOctreeWithBricks(voxels, 8, 3);

    // Ray through checkerboard - DDA should step consistently
    glm::vec3 rayOrigin(2.0f, 3.5f, 3.5f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);

    auto hit = octree->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    EXPECT_TRUE(hit.hit) << "Ray should hit checkerboard pattern";
    // Should hit first solid voxel in checkerboard
    EXPECT_GT(hit.hitPoint.x, brickOrigin) << "Should hit inside brick region";
    EXPECT_LT(hit.hitPoint.x, brickOrigin + 1.0f) << "Should hit within brick bounds";

    std::cout << "Brick DDA step consistency: Hit at ("
              << hit.hitPoint.x << ", " << hit.hitPoint.y << ", " << hit.hitPoint.z << ")\n";
}

// ============================================================================
// TEST 8: Brick-to-Brick Transition
// ============================================================================
TEST_F(BrickTraversalTest, BrickToBrickTransition) {
    // Create voxels in two spatially separate brick regions
    std::vector<glm::vec3> voxels = {
        glm::vec3(2.0f, 5.0f, 5.0f),  // First brick
        glm::vec3(7.0f, 5.0f, 5.0f),  // Second brick (different octree region)
    };

    auto octree = createOctreeWithBricks(voxels, 8, 3);

    // Cast ray through both brick regions
    glm::vec3 rayOrigin(-2.0f, 5.0f, 5.0f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);

    auto hit = octree->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    EXPECT_TRUE(hit.hit) << "Ray should hit first brick voxel";
    EXPECT_NEAR(hit.hitPoint.x, 2.0f, 2.0f) << "Should hit first voxel";

    std::cout << "Brick-to-brick transition: First hit at ("
              << hit.hitPoint.x << ", " << hit.hitPoint.y << ", " << hit.hitPoint.z << ")\n";

    // Note: Testing second hit requires multi-hit API (not yet implemented)
    // For now, verify first hit works correctly
}
