// test_voxel_traversal.cpp
// Unit tests for voxel traversal utilities (Ray-AABB intersection, DDA algorithm)
//
// Tests validate:
// - Ray-AABB intersection (hit/miss, entry/exit distances)
// - DDA initialization and stepping
// - Voxel bounds checking
//
// Target: 80%+ code coverage for VoxelTraversal.h

#include <gtest/gtest.h>
#include "Data/VoxelTraversal.h"
#include <glm/gtc/epsilon.hpp>

using namespace VIXEN::RenderGraph;

// ============================================================================
// RAY-AABB INTERSECTION TESTS
// ============================================================================

class RayAABBTest : public ::testing::Test {
protected:
    const float EPSILON = 1e-5f;

    bool FloatEqual(float a, float b) const {
        return std::abs(a - b) < EPSILON;
    }
};

TEST_F(RayAABBTest, RayHitsAABB_FrontFace) {
    // Ray pointing at front face of unit cube at origin
    Ray ray(glm::vec3(-2.0f, 0.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f));
    AABB aabb(glm::vec3(0.0f), glm::vec3(1.0f));

    RayAABBHit hit = IntersectRayAABB(ray, aabb);

    EXPECT_TRUE(hit.hit) << "Ray should hit AABB from front";
    EXPECT_TRUE(FloatEqual(hit.tEnter, 2.0f)) << "Entry distance should be 2.0";
    EXPECT_TRUE(FloatEqual(hit.tExit, 3.0f)) << "Exit distance should be 3.0";
}

TEST_F(RayAABBTest, RayMissesAABB_Above) {
    // Ray passing above AABB
    Ray ray(glm::vec3(-1.0f, 2.0f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f));
    AABB aabb(glm::vec3(0.0f), glm::vec3(1.0f));

    RayAABBHit hit = IntersectRayAABB(ray, aabb);

    EXPECT_FALSE(hit.hit) << "Ray should miss AABB (passing above)";
}

TEST_F(RayAABBTest, RayMissesAABB_BehindOrigin) {
    // Ray pointing away from AABB (AABB is behind ray origin)
    Ray ray(glm::vec3(2.0f, 0.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f));
    AABB aabb(glm::vec3(0.0f), glm::vec3(1.0f));

    RayAABBHit hit = IntersectRayAABB(ray, aabb);

    EXPECT_FALSE(hit.hit) << "Ray should miss AABB (behind ray origin)";
}

TEST_F(RayAABBTest, RayOriginInsideAABB) {
    // Ray starts inside AABB
    Ray ray(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f));
    AABB aabb(glm::vec3(0.0f), glm::vec3(1.0f));

    RayAABBHit hit = IntersectRayAABB(ray, aabb);

    EXPECT_TRUE(hit.hit) << "Ray inside AABB should hit";
    EXPECT_TRUE(hit.tEnter <= 0.0f) << "Entry distance should be negative (already inside)";
    EXPECT_TRUE(hit.tExit > 0.0f) << "Exit distance should be positive";
}

TEST_F(RayAABBTest, RayHitsDiagonal) {
    // Ray passing through AABB diagonally
    Ray ray(glm::vec3(-1.0f, -1.0f, -1.0f), glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)));
    AABB aabb(glm::vec3(0.0f), glm::vec3(1.0f));

    RayAABBHit hit = IntersectRayAABB(ray, aabb);

    EXPECT_TRUE(hit.hit) << "Diagonal ray should hit AABB";
    EXPECT_GT(hit.tEnter, 0.0f) << "Entry distance should be positive";
    EXPECT_GT(hit.tExit, hit.tEnter) << "Exit should be after entry";
}

TEST_F(RayAABBTest, RayParallelToFace) {
    // Ray parallel to AABB face (along X axis)
    Ray ray(glm::vec3(0.5f, 0.5f, -1.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    AABB aabb(glm::vec3(0.0f), glm::vec3(1.0f));

    RayAABBHit hit = IntersectRayAABB(ray, aabb);

    EXPECT_TRUE(hit.hit) << "Ray parallel to face should hit if aligned";
}

TEST_F(RayAABBTest, RayAxisAlignedZero) {
    // Ray with zero component in direction (parallel to plane)
    Ray ray(glm::vec3(0.5f, -1.0f, 0.5f), glm::vec3(0.0f, 1.0f, 0.0f));
    AABB aabb(glm::vec3(0.0f), glm::vec3(1.0f));

    RayAABBHit hit = IntersectRayAABB(ray, aabb);

    EXPECT_TRUE(hit.hit) << "Ray with zero X component should still hit";
    EXPECT_TRUE(FloatEqual(hit.tEnter, 1.0f)) << "Entry at Y=0";
    EXPECT_TRUE(FloatEqual(hit.tExit, 2.0f)) << "Exit at Y=1";
}

TEST_F(RayAABBTest, FastIntersectionTest_Hit) {
    Ray ray(glm::vec3(-1.0f, 0.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f));
    AABB aabb(glm::vec3(0.0f), glm::vec3(1.0f));

    bool hit = IntersectsRayAABB(ray, aabb);

    EXPECT_TRUE(hit) << "Fast intersection should detect hit";
}

TEST_F(RayAABBTest, FastIntersectionTest_Miss) {
    Ray ray(glm::vec3(-1.0f, 2.0f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f));
    AABB aabb(glm::vec3(0.0f), glm::vec3(1.0f));

    bool hit = IntersectsRayAABB(ray, aabb);

    EXPECT_FALSE(hit) << "Fast intersection should detect miss";
}

TEST_F(RayAABBTest, RayAtFunction) {
    Ray ray(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    glm::vec3 p = ray.At(5.0f);

    EXPECT_TRUE(glm::all(glm::epsilonEqual(p, glm::vec3(6.0f, 2.0f, 3.0f), EPSILON)));
}

TEST_F(RayAABBTest, AABBCenter) {
    AABB aabb(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(2.0f, 4.0f, 6.0f));

    glm::vec3 center = aabb.Center();

    EXPECT_TRUE(glm::all(glm::epsilonEqual(center, glm::vec3(1.0f, 2.0f, 3.0f), EPSILON)));
}

TEST_F(RayAABBTest, AABBExtents) {
    AABB aabb(glm::vec3(0.0f), glm::vec3(2.0f, 4.0f, 6.0f));

    glm::vec3 extents = aabb.Extents();

    EXPECT_TRUE(glm::all(glm::epsilonEqual(extents, glm::vec3(1.0f, 2.0f, 3.0f), EPSILON)));
}

TEST_F(RayAABBTest, AABBSize) {
    AABB aabb(glm::vec3(0.0f), glm::vec3(2.0f, 4.0f, 6.0f));

    glm::vec3 size = aabb.Size();

    EXPECT_TRUE(glm::all(glm::epsilonEqual(size, glm::vec3(2.0f, 4.0f, 6.0f), EPSILON)));
}

TEST_F(RayAABBTest, AABBContainsPoint_Inside) {
    AABB aabb(glm::vec3(0.0f), glm::vec3(1.0f));

    EXPECT_TRUE(aabb.Contains(glm::vec3(0.5f, 0.5f, 0.5f)));
}

TEST_F(RayAABBTest, AABBContainsPoint_Outside) {
    AABB aabb(glm::vec3(0.0f), glm::vec3(1.0f));

    EXPECT_FALSE(aabb.Contains(glm::vec3(2.0f, 0.5f, 0.5f)));
}

TEST_F(RayAABBTest, AABBContainsPoint_OnBoundary) {
    AABB aabb(glm::vec3(0.0f), glm::vec3(1.0f));

    EXPECT_TRUE(aabb.Contains(glm::vec3(0.0f, 0.0f, 0.0f))) << "Min corner should be inside";
    EXPECT_TRUE(aabb.Contains(glm::vec3(1.0f, 1.0f, 1.0f))) << "Max corner should be inside";
}

// ============================================================================
// DDA TRAVERSAL TESTS
// ============================================================================

class DDATest : public ::testing::Test {
protected:
    const float EPSILON = 1e-5f;
};

TEST_F(DDATest, InitializeDDA_AxisAligned_PositiveX) {
    // Ray moving in +X direction
    Ray ray(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f));
    DDAState state = InitializeDDA(ray, 64);

    EXPECT_EQ(state.voxelPos, glm::ivec3(0, 0, 0)) << "Starting voxel";
    EXPECT_EQ(state.step, glm::ivec3(1, 0, 0)) << "Step direction +X";
    EXPECT_GT(state.tDelta.x, 0.0f) << "tDelta.x should be positive";
}

TEST_F(DDATest, InitializeDDA_AxisAligned_NegativeY) {
    // Ray moving in -Y direction
    Ray ray(glm::vec3(5.5f, 10.5f, 5.5f), glm::vec3(0.0f, -1.0f, 0.0f));
    DDAState state = InitializeDDA(ray, 64);

    EXPECT_EQ(state.voxelPos, glm::ivec3(5, 10, 5)) << "Starting voxel";
    EXPECT_EQ(state.step, glm::ivec3(0, -1, 0)) << "Step direction -Y";
}

TEST_F(DDATest, InitializeDDA_Diagonal) {
    // Ray moving diagonally
    glm::vec3 dir = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
    Ray ray(glm::vec3(0.5f, 0.5f, 0.5f), dir);
    DDAState state = InitializeDDA(ray, 64);

    EXPECT_EQ(state.voxelPos, glm::ivec3(0, 0, 0));
    EXPECT_EQ(state.step, glm::ivec3(1, 1, 1)) << "Step in all positive directions";
}

TEST_F(DDATest, StepToNextVoxel_PositiveX) {
    // Set up DDA state moving in +X direction
    Ray ray(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f));
    DDAState state = InitializeDDA(ray, 64);

    glm::ivec3 initialPos = state.voxelPos;
    state.StepToNextVoxel();

    EXPECT_EQ(state.voxelPos, initialPos + glm::ivec3(1, 0, 0)) << "Should step in +X";
}

TEST_F(DDATest, StepToNextVoxel_Diagonal_MultipleSteps) {
    // Ray moving diagonally - step multiple times
    glm::vec3 dir = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
    Ray ray(glm::vec3(0.1f, 0.1f, 0.5f), dir);
    DDAState state = InitializeDDA(ray, 64);

    // Take 10 steps - should advance in X and Y roughly equally
    for (int i = 0; i < 10; ++i) {
        state.StepToNextVoxel();
    }

    EXPECT_GT(state.voxelPos.x, 0) << "Should advance in X";
    EXPECT_GT(state.voxelPos.y, 0) << "Should advance in Y";
    EXPECT_EQ(state.voxelPos.z, 0) << "Should not advance in Z (no Z component)";
}

TEST_F(DDATest, GetCurrentT) {
    Ray ray(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f));
    DDAState state = InitializeDDA(ray, 64);

    float t = state.GetCurrentT();

    // GetCurrentT() returns the entry point of the current voxel.
    // Since we start at (0.5, 0.5, 0.5) inside voxel (0,0,0), the entry point
    // is negative (we entered this voxel before t=0). This is correct.
    EXPECT_LT(t, 0.0f) << "Current t for starting voxel should be negative (already inside)";

    // Step to next voxel and verify t is now positive
    state.StepToNextVoxel();
    float tAfterStep = state.GetCurrentT();
    EXPECT_GT(tAfterStep, 0.0f) << "After stepping, current t should be positive";
}

TEST_F(DDATest, IsVoxelInBounds_Inside) {
    EXPECT_TRUE(IsVoxelInBounds(glm::ivec3(0, 0, 0), 64));
    EXPECT_TRUE(IsVoxelInBounds(glm::ivec3(32, 32, 32), 64));
    EXPECT_TRUE(IsVoxelInBounds(glm::ivec3(63, 63, 63), 64));
}

TEST_F(DDATest, IsVoxelInBounds_Outside) {
    EXPECT_FALSE(IsVoxelInBounds(glm::ivec3(-1, 0, 0), 64)) << "Negative X";
    EXPECT_FALSE(IsVoxelInBounds(glm::ivec3(0, -1, 0), 64)) << "Negative Y";
    EXPECT_FALSE(IsVoxelInBounds(glm::ivec3(0, 0, -1), 64)) << "Negative Z";
    EXPECT_FALSE(IsVoxelInBounds(glm::ivec3(64, 0, 0), 64)) << "X = gridSize";
    EXPECT_FALSE(IsVoxelInBounds(glm::ivec3(0, 64, 0), 64)) << "Y = gridSize";
    EXPECT_FALSE(IsVoxelInBounds(glm::ivec3(0, 0, 64), 64)) << "Z = gridSize";
}

TEST_F(DDATest, IsVoxelInBounds_EdgeCases) {
    EXPECT_TRUE(IsVoxelInBounds(glm::ivec3(0, 0, 0), 1)) << "Single voxel grid";
    EXPECT_FALSE(IsVoxelInBounds(glm::ivec3(1, 0, 0), 1)) << "Out of bounds for 1³ grid";
}

TEST_F(DDATest, DDA_FullTraversal_AxisAligned) {
    // Ray traversing along X axis through 64 voxel grid
    Ray ray(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f));
    DDAState state = InitializeDDA(ray, 64);

    int steps = 0;
    int maxSteps = 100;

    // Step through voxels until we exit the grid
    while (steps < maxSteps) {
        state.StepToNextVoxel();
        steps++;

        // Check if we exited bounds
        if (!IsVoxelInBounds(state.voxelPos, 64)) {
            break;
        }
    }

    EXPECT_GT(steps, 0) << "Should take at least one step";
    EXPECT_LT(steps, maxSteps) << "Should exit bounds before max steps";
    EXPECT_FALSE(IsVoxelInBounds(state.voxelPos, 64)) << "Should have exited grid bounds";
}

TEST_F(DDATest, DDA_FullTraversal_Diagonal) {
    // Ray traversing diagonally through grid
    glm::vec3 dir = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
    Ray ray(glm::vec3(0.5f, 0.5f, 0.5f), dir);
    DDAState state = InitializeDDA(ray, 64);

    int steps = 0;
    int maxSteps = 200;

    while (IsVoxelInBounds(state.voxelPos, 64) && steps < maxSteps) {
        state.StepToNextVoxel();
        steps++;
    }

    EXPECT_GT(steps, 0) << "Should take steps";
    EXPECT_LT(steps, maxSteps) << "Should exit before max steps";
    EXPECT_FALSE(IsVoxelInBounds(state.voxelPos, 64)) << "Should exit grid bounds";
}

// ============================================================================
// INTEGRATION TESTS
// ============================================================================

class TraversalIntegrationTest : public ::testing::Test {};

TEST_F(TraversalIntegrationTest, RayAABB_ThenDDA) {
    // Integration: Use Ray-AABB to find grid entry, then initialize DDA
    Ray ray(glm::vec3(-10.0f, 32.0f, 32.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    AABB gridAABB(glm::vec3(0.0f), glm::vec3(64.0f));

    RayAABBHit hit = IntersectRayAABB(ray, gridAABB);
    ASSERT_TRUE(hit.hit) << "Ray should hit grid AABB";

    // Advance ray to entry point
    glm::vec3 entryPoint = ray.At(hit.tEnter + 1e-5f);
    Ray entryRay(entryPoint, ray.direction);

    // Initialize DDA at entry point
    DDAState state = InitializeDDA(entryRay, 64);

    EXPECT_TRUE(IsVoxelInBounds(state.voxelPos, 64)) << "DDA should start inside grid";
}
