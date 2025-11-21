#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/random.hpp>
#include <vector>
#include <random>
#include "LaineKarrasOctree.h"
#include "VoxelInjection.h"
#include "BrickStorage.h"
#include "ISVOStructure.h"
#include "SVOTypes.h"

using namespace SVO;

class ComprehensiveRayCastingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a 10x10x10 world
        worldMin = glm::vec3(0, 0, 0);
        worldMax = glm::vec3(10, 10, 10);
        worldCenter = (worldMin + worldMax) * 0.5f;
    }

    // Helper function to create an octree with specific voxels
    std::unique_ptr<LaineKarrasOctree> createOctreeWithVoxels(
        const std::vector<glm::vec3>& voxelPositions,
        int maxDepth = 6)
    {
        // Create brick storage for additive insertion with bricks
        auto brickStorage = std::make_shared<BrickStorage<DefaultLeafData>>(3, 2048); // depth 3 = 8x8x8, capacity 2048

        auto octree = std::make_unique<LaineKarrasOctree>(brickStorage.get());

        // We CAN use bricks with additive insertion - we implemented this!
        VoxelInjector injector(brickStorage.get()); // Pass brick storage to injector
        InjectionConfig config;
        config.maxLevels = maxDepth;
        config.minVoxelSize = 0.01f;
        // Enable bricks for the bottom 3 levels
        config.brickDepthLevels = 3;

        // Insert all voxels
        for (const auto& pos : voxelPositions) {
            ::VoxelData::DynamicVoxelScalar voxel;
            voxel.set("position", pos);
            voxel.set("normal", glm::vec3(0, 1, 0)); // Default normal
            voxel.set("color", glm::vec3(1, 1, 1));
            voxel.set("density", 1.0f);

            // Use the insertVoxel signature from test_voxel_injection.cpp
            injector.insertVoxel(*octree, pos, voxel, config);
        }

        // Compact to ESVO format
        injector.compactToESVOFormat(*octree);

        return octree;
    }

    // Helper to check if ray hits any of the expected voxels
    bool hitsExpectedVoxel(const ISVOStructure::RayHit& hit,
                          const std::vector<glm::vec3>& expectedVoxels,
                          float tolerance = 5.0f)
    {
        if (!hit.hit) return false;

        for (const auto& voxel : expectedVoxels) {
            if (glm::length(hit.position - voxel) < tolerance) {
                return true;
            }
        }
        return false;
    }

    glm::vec3 worldMin, worldMax, worldCenter;
};

// ============================================================================
// TEST 1: Axis-Aligned Rays from Outside Grid
// ============================================================================
TEST_F(ComprehensiveRayCastingTest, AxisAlignedRaysFromOutside) {
    // Create octree with voxels along each axis
    std::vector<glm::vec3> voxels = {
        glm::vec3(5, 2, 2),   // X-axis target
        glm::vec3(2, 5, 2),   // Y-axis target
        glm::vec3(2, 2, 5),   // Z-axis target
        glm::vec3(8, 8, 8),   // Corner target
    };
    auto octree = createOctreeWithVoxels(voxels);

    // Test X-aligned ray from negative X
    {
        glm::vec3 origin(-5, 2, 2);
        glm::vec3 direction(1, 0, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "X-axis ray should hit voxel";
        EXPECT_TRUE(hitsExpectedVoxel(hit, {voxels[0]}))
            << "Should hit X-axis voxel at (5,2,2)";
    }

    // Test X-aligned ray from positive X
    {
        glm::vec3 origin(15, 8, 8);
        glm::vec3 direction(-1, 0, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Negative X-axis ray should hit voxel";
        EXPECT_TRUE(hitsExpectedVoxel(hit, {voxels[3]}))
            << "Should hit corner voxel at (8,8,8)";
    }

    // Test Y-aligned ray from negative Y
    {
        glm::vec3 origin(2, -5, 2);
        glm::vec3 direction(0, 1, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Y-axis ray should hit voxel";
        EXPECT_TRUE(hitsExpectedVoxel(hit, {voxels[1]}))
            << "Should hit Y-axis voxel at (2,5,2)";
    }

    // Test Y-aligned ray from positive Y
    {
        glm::vec3 origin(8, 15, 8);
        glm::vec3 direction(0, -1, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Negative Y-axis ray should hit voxel";
        EXPECT_TRUE(hitsExpectedVoxel(hit, {voxels[3]}))
            << "Should hit corner voxel at (8,8,8)";
    }

    // Test Z-aligned ray from negative Z
    {
        glm::vec3 origin(2, 2, -5);
        glm::vec3 direction(0, 0, 1);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Z-axis ray should hit voxel";
        EXPECT_TRUE(hitsExpectedVoxel(hit, {voxels[2]}))
            << "Should hit Z-axis voxel at (2,2,5)";
    }

    // Test Z-aligned ray from positive Z
    {
        glm::vec3 origin(8, 8, 15);
        glm::vec3 direction(0, 0, -1);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Negative Z-axis ray should hit voxel";
        EXPECT_TRUE(hitsExpectedVoxel(hit, {voxels[3]}))
            << "Should hit corner voxel at (8,8,8)";
    }
}

// ============================================================================
// TEST 2: Diagonal Rays at Various Angles
// ============================================================================
TEST_F(ComprehensiveRayCastingTest, DiagonalRaysVariousAngles) {
    // Create octree with scattered voxels
    std::vector<glm::vec3> voxels = {
        glm::vec3(2, 2, 2),
        glm::vec3(5, 5, 5),
        glm::vec3(8, 8, 8),
        glm::vec3(3, 7, 4),
    };
    auto octree = createOctreeWithVoxels(voxels);

    // Test perfect diagonal (1,1,1)
    {
        glm::vec3 origin(-2, -2, -2);
        glm::vec3 direction = glm::normalize(glm::vec3(1, 1, 1));
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Diagonal ray should hit voxel";
        // Should hit one of the diagonal voxels
        EXPECT_TRUE(hitsExpectedVoxel(hit, {voxels[0], voxels[1], voxels[2]}))
            << "Should hit a diagonal voxel";
    }

    // Test shallow angle in XY plane
    {
        glm::vec3 origin(-2, 3, 5);
        glm::vec3 direction = glm::normalize(glm::vec3(2, 0.5f, 0));
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        // May or may not hit depending on voxel positions
        if (hit.hit) {
            EXPECT_TRUE(hitsExpectedVoxel(hit, voxels, 5.0f))
                << "If hit, should be near a voxel";
        }
    }

    // Test steep angle in YZ plane
    {
        glm::vec3 origin(5, -2, -2);
        glm::vec3 direction = glm::normalize(glm::vec3(0, 1, 2));
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        if (hit.hit) {
            EXPECT_TRUE(hitsExpectedVoxel(hit, voxels, 5.0f))
                << "If hit, should be near a voxel";
        }
    }

    // Test arbitrary angle
    {
        glm::vec3 origin(-1, -1, -1);
        glm::vec3 direction = glm::normalize(glm::vec3(2.5f, 3.7f, 2.1f));
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        if (hit.hit) {
            EXPECT_TRUE(hitsExpectedVoxel(hit, voxels, 5.0f))
                << "If hit, should be near a voxel";
        }
    }
}

// ============================================================================
// TEST 3: Rays from Inside Grid
// ============================================================================
TEST_F(ComprehensiveRayCastingTest, RaysFromInsideGrid) {
    // Create a hollow cube with voxels on the walls
    std::vector<glm::vec3> voxels;
    // Create walls at x=1, x=9, y=1, y=9, z=1, z=9
    for (int y = 1; y <= 9; y++) {
        for (int z = 1; z <= 9; z++) {
            voxels.push_back(glm::vec3(1, y, z)); // Left wall
            voxels.push_back(glm::vec3(9, y, z)); // Right wall
        }
    }
    for (int x = 2; x <= 8; x++) {
        for (int z = 1; z <= 9; z++) {
            voxels.push_back(glm::vec3(x, 1, z)); // Bottom wall
            voxels.push_back(glm::vec3(x, 9, z)); // Top wall
        }
    }
    for (int x = 2; x <= 8; x++) {
        for (int y = 2; y <= 8; y++) {
            voxels.push_back(glm::vec3(x, y, 1)); // Front wall
            voxels.push_back(glm::vec3(x, y, 9)); // Back wall
        }
    }

    auto octree = createOctreeWithVoxels(voxels);

    // Ray from center outward in +X
    {
        glm::vec3 origin(5, 5, 5);
        glm::vec3 direction(1, 0, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray from center should hit right wall";
        EXPECT_NEAR(hit.position.x, 9.0f, 2.0f) << "Should hit right wall at x=9";
    }

    // Ray from center outward in -Y
    {
        glm::vec3 origin(5, 5, 5);
        glm::vec3 direction(0, -1, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray from center should hit bottom wall";
        EXPECT_NEAR(hit.position.y, 1.0f, 2.0f) << "Should hit bottom wall at y=1";
    }

    // Diagonal ray from inside
    {
        glm::vec3 origin(5, 5, 5);
        glm::vec3 direction = glm::normalize(glm::vec3(1, 1, 1));
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Diagonal ray from center should hit corner";
        // Should hit near (9,9,9) corner area
        EXPECT_GT(hit.position.x, 7.0f) << "Should hit near corner";
        EXPECT_GT(hit.position.y, 7.0f) << "Should hit near corner";
        EXPECT_GT(hit.position.z, 7.0f) << "Should hit near corner";
    }

    // Ray between walls (should hit far wall)
    {
        glm::vec3 origin(2, 5, 5);
        glm::vec3 direction(1, 0, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray should traverse empty space and hit far wall";
        EXPECT_NEAR(hit.position.x, 9.0f, 2.0f) << "Should hit right wall";
    }
}

// ============================================================================
// TEST 4: Complete Miss Cases
// ============================================================================
TEST_F(ComprehensiveRayCastingTest, CompleteMissCases) {
    // Create sparse octree with few voxels
    std::vector<glm::vec3> voxels = {
        glm::vec3(5, 5, 5),
        glm::vec3(2, 2, 2),
        glm::vec3(8, 8, 8),
    };
    auto octree = createOctreeWithVoxels(voxels);

    // Ray that passes above the grid
    {
        glm::vec3 origin(-5, 15, 5);
        glm::vec3 direction(1, 0, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_FALSE(hit.hit) << "Ray above grid should miss";
    }

    // Ray that passes below the grid
    {
        glm::vec3 origin(5, -5, 5);
        glm::vec3 direction(0, 0, 1);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_FALSE(hit.hit) << "Ray below grid should miss";
    }

    // Ray pointing away from grid
    {
        glm::vec3 origin(15, 15, 15);
        glm::vec3 direction(1, 1, 1);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_FALSE(hit.hit) << "Ray pointing away should miss";
    }

    // Ray that enters grid but misses all voxels
    {
        glm::vec3 origin(-1, 3.7f, 3.7f);
        glm::vec3 direction(1, 0, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        // This may or may not hit depending on voxel size at depth
        // If it misses, that's valid behavior for sparse octree
        if (!hit.hit) {
            SUCCEED() << "Valid miss in sparse region";
        } else {
            EXPECT_TRUE(hitsExpectedVoxel(hit, voxels, 5.0f))
                << "If hit, should be near a voxel";
        }
    }

    // Ray with very limited range that stops before hitting
    {
        glm::vec3 origin(-5, 5, 5);
        glm::vec3 direction(1, 0, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 2.0f); // tMax=2, won't reach grid
        EXPECT_FALSE(hit.hit) << "Ray with limited range should miss";
    }
}

// ============================================================================
// TEST 5: Multiple Hit Traversal (Ray passing through multiple voxels)
// ============================================================================
TEST_F(ComprehensiveRayCastingTest, MultipleVoxelTraversal) {
    // Create a line of voxels
    std::vector<glm::vec3> voxels;
    for (int x = 1; x <= 9; x++) {
        voxels.push_back(glm::vec3(x, 5, 5));
    }
    auto octree = createOctreeWithVoxels(voxels);

    // Ray along the line of voxels
    {
        glm::vec3 origin(-2, 5, 5);
        glm::vec3 direction(1, 0, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray should hit first voxel in line";
        EXPECT_LT(hit.position.x, 3.0f) << "Should hit first voxel around x=1";
    }

    // Create grid of voxels in XY plane
    voxels.clear();
    for (int x = 2; x <= 8; x += 2) {
        for (int y = 2; y <= 8; y += 2) {
            voxels.push_back(glm::vec3(x, y, 5));
        }
    }
    octree = createOctreeWithVoxels(voxels);

    // Diagonal ray through grid
    {
        glm::vec3 origin(0, 0, 5);
        glm::vec3 direction = glm::normalize(glm::vec3(1, 1, 0));
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Diagonal ray should hit grid";
        // Should hit one of the voxels in the grid
        EXPECT_TRUE(hitsExpectedVoxel(hit, voxels, 3.0f))
            << "Should hit a voxel in the grid";
    }
}

// ============================================================================
// TEST 6: Dense Volume Testing
// ============================================================================
TEST_F(ComprehensiveRayCastingTest, DenseVolumeTraversal) {
    // Create a solid cube from (3,3,3) to (7,7,7)
    std::vector<glm::vec3> voxels;
    for (int x = 3; x <= 7; x++) {
        for (int y = 3; y <= 7; y++) {
            for (int z = 3; z <= 7; z++) {
                voxels.push_back(glm::vec3(x, y, z));
            }
        }
    }
    auto octree = createOctreeWithVoxels(voxels, 8); // Higher depth for dense volume

    // Ray hitting the front face
    {
        glm::vec3 origin(5, 5, 0);
        glm::vec3 direction(0, 0, 1);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray should hit dense volume";
        EXPECT_NEAR(hit.position.z, 3.0f, 2.0f) << "Should hit front face around z=3";
    }

    // Ray hitting corner
    {
        glm::vec3 origin(0, 0, 0);
        glm::vec3 direction = glm::normalize(glm::vec3(1, 1, 1));
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Diagonal ray should hit dense volume";
        // Should hit near (3,3,3) corner
        EXPECT_NEAR(hit.position.x, 3.0f, 2.0f);
        EXPECT_NEAR(hit.position.y, 3.0f, 2.0f);
        EXPECT_NEAR(hit.position.z, 3.0f, 2.0f);
    }

    // Ray grazing the edge
    {
        glm::vec3 origin(2.9f, 5, 0);
        glm::vec3 direction(0, 0, 1);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        // May or may not hit depending on voxel boundaries
        if (hit.hit) {
            EXPECT_TRUE(hitsExpectedVoxel(hit, voxels, 3.0f))
                << "If hit, should be near volume edge";
        }
    }
}

// ============================================================================
// TEST 7: Edge Cases and Boundary Conditions
// ============================================================================
TEST_F(ComprehensiveRayCastingTest, EdgeCasesAndBoundaries) {
    // Single voxel at origin
    std::vector<glm::vec3> voxels = {
        glm::vec3(0.1f, 0.1f, 0.1f), // Just inside grid
        glm::vec3(9.9f, 9.9f, 9.9f), // Just inside opposite corner
        glm::vec3(5, 5, 5),           // Center
    };
    auto octree = createOctreeWithVoxels(voxels);

    // Ray exactly along grid boundary
    {
        glm::vec3 origin(0, 5, 5);
        glm::vec3 direction(1, 0, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray along boundary should hit center voxel";
        EXPECT_TRUE(hitsExpectedVoxel(hit, {voxels[2]}))
            << "Should hit center voxel";
    }

    // Ray starting exactly on grid boundary
    {
        glm::vec3 origin(0, 0, 0);
        glm::vec3 direction = glm::normalize(glm::vec3(1, 1, 1));
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray from corner should hit";
        // Should hit origin voxel or center
        EXPECT_TRUE(hitsExpectedVoxel(hit, voxels, 5.0f))
            << "Should hit a voxel";
    }

    // Ray with zero-length direction (should handle gracefully)
    {
        glm::vec3 origin(5, 5, 5);
        glm::vec3 direction(0, 0, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_FALSE(hit.hit) << "Zero direction should return miss";
    }

    // Ray with very small direction components
    {
        glm::vec3 origin(-1, 5, 5);
        glm::vec3 direction = glm::normalize(glm::vec3(1, 0.0001f, 0.0001f));
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        // Should handle near-axis-aligned ray correctly
        if (hit.hit) {
            EXPECT_TRUE(hitsExpectedVoxel(hit, voxels, 5.0f))
                << "Should hit a voxel if successful";
        }
    }
}

// ============================================================================
// TEST 8: Random Stress Testing
// ============================================================================
TEST_F(ComprehensiveRayCastingTest, RandomStressTesting) {
    // Create random voxel cloud
    std::mt19937 rng(42); // Fixed seed for reproducibility
    std::uniform_real_distribution<float> dist(0.5f, 9.5f);

    std::vector<glm::vec3> voxels;
    for (int i = 0; i < 50; i++) {
        voxels.push_back(glm::vec3(dist(rng), dist(rng), dist(rng)));
    }
    auto octree = createOctreeWithVoxels(voxels, 7);

    // Cast many random rays
    std::uniform_real_distribution<float> posDist(-5.0f, 15.0f);
    std::uniform_real_distribution<float> dirDist(-1.0f, 1.0f);

    int hits = 0;
    int misses = 0;
    const int numRays = 100;

    for (int i = 0; i < numRays; i++) {
        glm::vec3 origin(posDist(rng), posDist(rng), posDist(rng));
        glm::vec3 direction(dirDist(rng), dirDist(rng), dirDist(rng));

        if (glm::length(direction) < 0.001f) continue; // Skip near-zero directions
        direction = glm::normalize(direction);

        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        if (hit.hit) {
            hits++;
            // Verify hit is reasonable
            EXPECT_GE(hit.tMin, 0.0f) << "Hit distance should be non-negative";
            EXPECT_LE(hit.tMin, 100.0f) << "Hit distance should be within range";
        } else {
            misses++;
        }
    }

    // With 50 voxels in a 10x10x10 space, we expect some hits
    EXPECT_GT(hits, 0) << "Random rays should hit some voxels";
    EXPECT_GT(misses, 0) << "Random rays should miss some times";

    std::cout << "Random stress test: " << hits << " hits, " << misses << " misses out of "
              << numRays << " rays\n";
}

// ============================================================================
// TEST 9: Performance Characteristics Test
// ============================================================================
TEST_F(ComprehensiveRayCastingTest, PerformanceCharacteristics) {
    // Test that deeper octrees still work correctly
    for (int depth = 4; depth <= 10; depth += 2) {
        std::vector<glm::vec3> voxels = {
            glm::vec3(5, 5, 5),
            glm::vec3(2.5f, 2.5f, 2.5f),
            glm::vec3(7.5f, 7.5f, 7.5f),
        };

        auto octree = createOctreeWithVoxels(voxels, depth);

        glm::vec3 origin(0, 0, 0);
        glm::vec3 direction = glm::normalize(glm::vec3(1, 1, 1));

        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray should hit at depth " << depth;

        // Deeper octrees should have more precise hits (smaller scale values)
        if (depth >= 8) {
            EXPECT_LE(hit.scale, 23 - depth + 2)
                << "Deeper octrees should have smaller scale values";
        }
    }
}

// ============================================================================
// TEST 10: Cornell Box-like Scene
// ============================================================================
TEST_F(ComprehensiveRayCastingTest, CornellBoxScene) {
    // Create a Cornell box with colored walls and objects
    std::vector<glm::vec3> walls;
    std::vector<glm::vec3> objects;

    // Back wall (z=9)
    for (int x = 0; x <= 10; x++) {
        for (int y = 0; y <= 10; y++) {
            walls.push_back(glm::vec3(x, y, 9.5f));
        }
    }

    // Left wall (x=0.5)
    for (int y = 0; y <= 10; y++) {
        for (int z = 0; z < 9; z++) {
            walls.push_back(glm::vec3(0.5f, y, z));
        }
    }

    // Right wall (x=9.5)
    for (int y = 0; y <= 10; y++) {
        for (int z = 0; z < 9; z++) {
            walls.push_back(glm::vec3(9.5f, y, z));
        }
    }

    // Floor (y=0.5)
    for (int x = 1; x < 9; x++) {
        for (int z = 0; z < 9; z++) {
            walls.push_back(glm::vec3(x, 0.5f, z));
        }
    }

    // Ceiling (y=9.5)
    for (int x = 1; x < 9; x++) {
        for (int z = 0; z < 9; z++) {
            walls.push_back(glm::vec3(x, 9.5f, z));
        }
    }

    // Add two boxes as objects
    // Box 1: Small box at (3, 0.5, 3)
    for (int x = 2; x <= 4; x++) {
        for (int y = 1; y <= 3; y++) {
            for (int z = 2; z <= 4; z++) {
                objects.push_back(glm::vec3(x, y, z));
            }
        }
    }

    // Box 2: Tall box at (6, 0.5, 6)
    for (int x = 5; x <= 7; x++) {
        for (int y = 1; y <= 6; y++) {
            for (int z = 5; z <= 7; z++) {
                objects.push_back(glm::vec3(x, y, z));
            }
        }
    }

    // Combine all voxels
    std::vector<glm::vec3> allVoxels;
    allVoxels.insert(allVoxels.end(), walls.begin(), walls.end());
    allVoxels.insert(allVoxels.end(), objects.begin(), objects.end());

    // Use depth 8 with bricks for a good balance
    // This gives octree depth 5 (8-3) with 8x8x8 bricks at leaves
    auto octree = createOctreeWithVoxels(allVoxels, 8);

    // Camera ray from front of box looking in
    {
        glm::vec3 origin(5, 5, -2);
        glm::vec3 direction(0, 0, 1);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray should hit scene";
        // Should hit either an object or back wall
        EXPECT_GT(hit.position.z, 0.0f) << "Should hit something in scene";
    }

    // Ray between objects
    {
        glm::vec3 origin(4.5f, 3, -2);
        glm::vec3 direction(0, 0, 1);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray between objects should hit back wall";
        EXPECT_NEAR(hit.position.z, 9.5f, 2.0f) << "Should hit back wall";
    }

    // Shadow ray from object to light (ceiling)
    {
        glm::vec3 origin(3, 3, 3); // Top of small box
        glm::vec3 direction(0, 1, 0); // Up toward ceiling
        auto hit = octree->castRay(origin, direction, 0.1f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Shadow ray should hit ceiling";
        EXPECT_NEAR(hit.position.y, 9.5f, 2.0f) << "Should hit ceiling";
    }

    // Indirect lighting ray (bounce off wall)
    {
        glm::vec3 origin(1, 5, 5); // Near left wall
        glm::vec3 direction = glm::normalize(glm::vec3(1, 0.2f, 0.3f));
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Bounce ray should hit something";
    }
}
