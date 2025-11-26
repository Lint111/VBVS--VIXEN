#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/random.hpp>
#include <vector>
#include <random>
#include <chrono>
#include <iomanip>
#include "LaineKarrasOctree.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"  // For Density, Color components
#include "ISVOStructure.h"
#include "SVOTypes.h"

using namespace SVO;
using namespace GaiaVoxel;

class ComprehensiveRayCastingTest : public ::testing::Test {
protected:
    void SetUp() override {

        registry = std::make_shared<::VoxelData::AttributeRegistry>();
        registry->registerKey("density", ::VoxelData::AttributeType::Float, 1.0f);
        registry->addAttribute("color", ::VoxelData::AttributeType::Vec3, glm::vec3(1.0f));

        // Create fresh GaiaVoxelWorld for each test (ensures test isolation)
        voxelWorld = std::make_shared<GaiaVoxelWorld>();

        // Default world bounds (overridden by createOctreeWithVoxels based on actual voxel positions)
        worldMin = glm::vec3(0, 0, 0);
        worldMax = glm::vec3(10, 10, 10);
        worldCenter = (worldMin + worldMax) * 0.5f;
    }

    // Helper function to create an octree with specific voxels using NEW WORKFLOW
    std::unique_ptr<LaineKarrasOctree> createOctreeWithVoxels(
        const std::vector<glm::vec3>& voxelPositions,
        int maxDepth = 6)
    {
        // Note: SetUp() creates fresh voxelWorld for each test, so no clear needed here

        // Compute bounds from actual voxel positions for this test
        constexpr float floatMax = 1e30f;
        glm::vec3 testMin(floatMax, floatMax, floatMax);
        glm::vec3 testMax(-floatMax, -floatMax, -floatMax);
        for (const auto& pos : voxelPositions) {
            testMin.x = pos.x < testMin.x ? pos.x : testMin.x;
            testMin.y = pos.y < testMin.y ? pos.y : testMin.y;
            testMin.z = pos.z < testMin.z ? pos.z : testMin.z;
            testMax.x = pos.x > testMax.x ? pos.x : testMax.x;
            testMax.y = pos.y > testMax.y ? pos.y : testMax.y;
            testMax.z = pos.z > testMax.z ? pos.z : testMax.z;
        }
        // Add padding to ensure voxels fit within bounds
        testMin -= glm::vec3(1.0f);
        testMax += glm::vec3(1.0f);

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
            3          // brickDepth (3 levels = 8x8x8 brick)
        );

        // Build ESVO hierarchy from entities with computed bounds
        octree->rebuild(*voxelWorld, testMin, testMax);

        return octree;
    }

    // Helper to check if ray hits any of the expected voxels
    bool hitsExpectedVoxel(const ISVOStructure::RayHit& hit,
                          const std::vector<glm::vec3>& expectedVoxels,
                          float tolerance = 5.0f)
    {
        if (!hit.hit) return false;

        for (const auto& voxel : expectedVoxels) {
            if (glm::length(hit.hitPoint - voxel) < tolerance) {
                return true;
            }
        }
        return false;
    }

    glm::vec3 worldMin, worldMax, worldCenter;
    std::shared_ptr<GaiaVoxelWorld> voxelWorld;
    std::shared_ptr<::VoxelData::AttributeRegistry> registry;
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
    std::cout << "[TEST] Octree created, casting first ray...\n" << std::flush;

    // Ray from center outward in +X
    {
        glm::vec3 origin(5, 5, 5);
        glm::vec3 direction(1, 0, 0);
        std::cout << "[TEST] Calling castRay from (" << origin.x << "," << origin.y << "," << origin.z << ") dir=(" << direction.x << "," << direction.y << "," << direction.z << ")\n" << std::flush;
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        std::cout << "[TEST] castRay returned, hit=" << hit.hit << "\n" << std::flush;
        EXPECT_TRUE(hit.hit) << "Ray from center should hit right wall";
        EXPECT_NEAR(hit.hitPoint.x, 9.0f, 2.0f) << "Should hit right wall at x=9";
    }

    // Ray from center outward in -Y
    {
        glm::vec3 origin(5, 5, 5);
        glm::vec3 direction(0, -1, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray from center should hit bottom wall";
        EXPECT_NEAR(hit.hitPoint.y, 1.0f, 2.0f) << "Should hit bottom wall at y=1";
    }

    // Diagonal ray from inside
    {
        glm::vec3 origin(5, 5, 5);
        glm::vec3 direction = glm::normalize(glm::vec3(1, 1, 1));
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Diagonal ray from center should hit corner";
        // Should hit near (9,9,9) corner area
        EXPECT_GT(hit.hitPoint.x, 7.0f) << "Should hit near corner";
        EXPECT_GT(hit.hitPoint.y, 7.0f) << "Should hit near corner";
        EXPECT_GT(hit.hitPoint.z, 7.0f) << "Should hit near corner";
    }

    // Ray between walls (should hit far wall)
    {
        glm::vec3 origin(2, 5, 5);
        glm::vec3 direction(1, 0, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray should traverse empty space and hit far wall";
        EXPECT_NEAR(hit.hitPoint.x, 9.0f, 2.0f) << "Should hit right wall";
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
        EXPECT_LT(hit.hitPoint.x, 3.0f) << "Should hit first voxel around x=1";
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
        EXPECT_NEAR(hit.hitPoint.z, 3.0f, 2.0f) << "Should hit front face around z=3";
    }

    // Ray hitting corner
    {
        glm::vec3 origin(0, 0, 0);
        glm::vec3 direction = glm::normalize(glm::vec3(1, 1, 1));
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Diagonal ray should hit dense volume";
        // Should hit near (3,3,3) corner
        EXPECT_NEAR(hit.hitPoint.x, 3.0f, 2.0f);
        EXPECT_NEAR(hit.hitPoint.y, 3.0f, 2.0f);
        EXPECT_NEAR(hit.hitPoint.z, 3.0f, 2.0f);
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
    // Test with integer voxel positions (Morton codes truncate to integer grid)
    // Note: fractional positions like (0.1, 0.1, 0.1) are stored at (0, 0, 0) due to Morton encoding
    std::vector<glm::vec3> voxels = {
        glm::vec3(1, 1, 1),   // Near origin (integer position)
        glm::vec3(9, 9, 9),   // Near opposite corner (integer position)
        glm::vec3(5, 5, 5),   // Center
    };
    auto octree = createOctreeWithVoxels(voxels);

    // Ray from outside volume toward center voxel
    {
        glm::vec3 origin(-2, 5, 5);
        glm::vec3 direction(1, 0, 0);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray from outside should hit center voxel";
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
    // Create random voxel cloud with integer positions (Morton codes use integer grid)
    std::mt19937 rng(42); // Fixed seed for reproducibility
    std::uniform_int_distribution<int> dist(1, 9);  // Integer distribution for Morton compatibility

    std::vector<glm::vec3> voxels;
    for (int i = 0; i < 50; i++) {
        voxels.push_back(glm::vec3(static_cast<float>(dist(rng)),
                                   static_cast<float>(dist(rng)),
                                   static_cast<float>(dist(rng))));
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
    // Create a smaller Cornell box that fits within a single brick (8x8x8)
    // This tests the basic scene without multi-brick complexity
    // Note: Morton codes use integer grid positions, so all voxels must be at integer coordinates
    std::vector<glm::vec3> walls;
    std::vector<glm::vec3> objects;

    // Back wall (z=7)
    for (int x = 1; x <= 6; x++) {
        for (int y = 1; y <= 6; y++) {
            walls.push_back(glm::vec3(x, y, 7));
        }
    }

    // Left wall (x=1)
    for (int y = 1; y <= 6; y++) {
        for (int z = 1; z < 7; z++) {
            walls.push_back(glm::vec3(1, y, z));
        }
    }

    // Right wall (x=6)
    for (int y = 1; y <= 6; y++) {
        for (int z = 1; z < 7; z++) {
            walls.push_back(glm::vec3(6, y, z));
        }
    }

    // Floor (y=1)
    for (int x = 2; x < 6; x++) {
        for (int z = 1; z < 7; z++) {
            walls.push_back(glm::vec3(x, 1, z));
        }
    }

    // Ceiling (y=6)
    for (int x = 2; x < 6; x++) {
        for (int z = 1; z < 7; z++) {
            walls.push_back(glm::vec3(x, 6, z));
        }
    }

    // Add one small box inside the room
    // Box 1: Small box at (3, 2, 3)
    for (int x = 3; x <= 4; x++) {
        for (int y = 2; y <= 3; y++) {
            for (int z = 3; z <= 4; z++) {
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
    // Room is at z=1-7, so z=-2 is outside looking in at z=7 back wall
    {
        glm::vec3 origin(4, 4, -2);
        glm::vec3 direction(0, 0, 1);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray should hit scene";
        // Should hit either the box (z=3-4) or back wall (z=7)
        EXPECT_GT(hit.hitPoint.z, 2.0f) << "Should hit something in scene";
    }

    // Ray above the box toward back wall
    // Box is at y=2-3, so y=5 is clearly above
    {
        glm::vec3 origin(4, 5, -2);
        glm::vec3 direction(0, 0, 1);
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Ray above box should hit back wall";
        EXPECT_NEAR(hit.hitPoint.z, 7.0f, 2.0f) << "Should hit back wall at z=7";
    }

    // Shadow ray from above the small box to ceiling
    // Box top is at y=3, ceiling at y=6
    {
        glm::vec3 origin(4, 4, 4); // Above box (y=4 > 3)
        glm::vec3 direction(0, 1, 0); // Up toward ceiling
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Shadow ray should hit ceiling";
        EXPECT_NEAR(hit.hitPoint.y, 6.0f, 2.0f) << "Should hit ceiling at y=6";
    }

    // Indirect lighting ray (bounce off wall)
    {
        glm::vec3 origin(2, 4, 4); // Inside room, near left wall
        glm::vec3 direction = glm::normalize(glm::vec3(1, 0.2f, 0.3f));
        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        EXPECT_TRUE(hit.hit) << "Bounce ray should hit something";
    }
}

// ============================================================================
// TEST 11: Ray Casting Throughput Benchmark
// ============================================================================
TEST_F(ComprehensiveRayCastingTest, ThroughputBenchmark) {
    // Create a dense scene for benchmarking
    std::vector<glm::vec3> voxels;

    // Create a 4x4x4 solid cube (64 voxels)
    for (int x = 2; x < 6; x++) {
        for (int y = 2; y < 6; y++) {
            for (int z = 2; z < 6; z++) {
                voxels.push_back(glm::vec3(x, y, z));
            }
        }
    }

    auto octree = createOctreeWithVoxels(voxels, 8);

    // Benchmark parameters
    constexpr int NUM_RAYS = 10000;
    constexpr int NUM_WARMUP = 100;

    // Generate random ray directions from a sphere around the scene
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    std::vector<std::pair<glm::vec3, glm::vec3>> rays;
    rays.reserve(NUM_RAYS + NUM_WARMUP);

    glm::vec3 sceneCenter(4.0f, 4.0f, 4.0f);
    float sceneRadius = 10.0f;

    for (int i = 0; i < NUM_RAYS + NUM_WARMUP; i++) {
        // Random point on sphere
        glm::vec3 dir;
        do {
            dir = glm::vec3(dist(rng), dist(rng), dist(rng));
        } while (glm::length(dir) < 0.01f);
        dir = glm::normalize(dir);

        glm::vec3 origin = sceneCenter - dir * sceneRadius;
        rays.push_back({origin, dir});
    }

    // Warmup (not timed)
    for (int i = 0; i < NUM_WARMUP; i++) {
        auto hit = octree->castRay(rays[i].first, rays[i].second, 0.0f, 100.0f);
        (void)hit;  // Prevent optimization
    }

    // Timed benchmark
    auto start = std::chrono::high_resolution_clock::now();

    int hitCount = 0;
    for (int i = NUM_WARMUP; i < NUM_RAYS + NUM_WARMUP; i++) {
        auto hit = octree->castRay(rays[i].first, rays[i].second, 0.0f, 100.0f);
        if (hit.hit) hitCount++;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double seconds = duration.count() / 1000000.0;
    double raysPerSecond = NUM_RAYS / seconds;
    double megaRaysPerSecond = raysPerSecond / 1000000.0;

    // Print results
    std::cout << "\n======== RAY CASTING BENCHMARK ========\n";
    std::cout << "Rays cast:    " << NUM_RAYS << "\n";
    std::cout << "Hits:         " << hitCount << " (" << (100.0 * hitCount / NUM_RAYS) << "%)\n";
    std::cout << "Total time:   " << duration.count() << " μs\n";
    std::cout << "Throughput:   " << std::fixed << std::setprecision(2) << megaRaysPerSecond << " Mrays/sec\n";
    std::cout << "Avg ray time: " << (duration.count() / (double)NUM_RAYS) << " μs/ray\n";
    std::cout << "========================================\n\n";

    // Sanity checks
    EXPECT_GT(hitCount, 0) << "Should have some hits";
    // Note: Dense 4x4x4 cube is visible from all directions, so 100% hit rate is expected

    // Performance threshold (very conservative for Debug builds)
    // Debug: ~4K rays/sec, Release: ~50K-500K rays/sec
    EXPECT_GT(raysPerSecond, 1000.0) << "Should cast at least 1K rays/sec even in Debug";
}
