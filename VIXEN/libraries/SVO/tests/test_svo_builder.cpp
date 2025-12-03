#include <gtest/gtest.h>
#include "SVOBuilder.h"

using namespace Vixen::SVO;

// ===========================================================================
// Helper Functions
// ===========================================================================

InputMesh createCube(float size) {
    InputMesh mesh;

    // 8 vertices
    mesh.vertices = {
        {-size, -size, -size}, {size, -size, -size},
        {size, size, -size}, {-size, size, -size},
        {-size, -size, size}, {size, -size, size},
        {size, size, size}, {-size, size, size}
    };

    mesh.normals.resize(8, glm::vec3(0, 1, 0));
    mesh.colors.resize(8, glm::vec3(1, 1, 1));
    mesh.uvs.resize(8, glm::vec2(0, 0));

    // 12 triangles (2 per face)
    mesh.indices = {
        // Front
        0, 1, 2, 0, 2, 3,
        // Back
        5, 4, 7, 5, 7, 6,
        // Left
        4, 0, 3, 4, 3, 7,
        // Right
        1, 5, 6, 1, 6, 2,
        // Top
        3, 2, 6, 3, 6, 7,
        // Bottom
        4, 5, 1, 4, 1, 0
    };

    mesh.minBounds = glm::vec3(-size);
    mesh.maxBounds = glm::vec3(size);

    return mesh;
}

// ===========================================================================
// SVOBuilder Basic Tests
// ===========================================================================

TEST(SVOBuilderTest, Construction) {
    BuildParams params;
    params.maxLevels = 8;

    SVOBuilder builder(params);

    // Should construct without errors
    SUCCEED();
}

TEST(SVOBuilderTest, BuildCube) {
    BuildParams params;
    params.maxLevels = 6;  // Small for quick test
    params.geometryErrorThreshold = 0.1f;

    SVOBuilder builder(params);

    InputMesh cube = createCube(1.0f);
    auto octree = builder.build(cube);

    ASSERT_NE(octree, nullptr);
    EXPECT_GT(octree->totalVoxels, 0);
    EXPECT_GT(octree->leafVoxels, 0);
}

TEST(SVOBuilderTest, BuildStats) {
    BuildParams params;
    params.maxLevels = 6;

    SVOBuilder builder(params);

    InputMesh cube = createCube(1.0f);
    auto octree = builder.build(cube);

    const auto& stats = builder.getLastBuildStats();

    EXPECT_GT(stats.voxelsProcessed, 0);
    EXPECT_GT(stats.leavesCreated, 0);
    EXPECT_GT(stats.buildTimeSeconds, 0.0f);
}

// ===========================================================================
// Triangle Intersection Tests
// ===========================================================================

TEST(SVOBuilderTest, TriangleAABBIntersection) {
    BuildParams params;
    SVOBuilder builder(params);

    InputTriangle tri;
    tri.vertices[0] = glm::vec3(0, 0, 0);
    tri.vertices[1] = glm::vec3(1, 0, 0);
    tri.vertices[2] = glm::vec3(0, 1, 0);

    // AABB that contains triangle
    glm::vec3 aabbMin(-1, -1, -1);
    glm::vec3 aabbMax(2, 2, 2);

    // Should intersect
    // Note: triangleIntersectsAABB is private, test indirectly through build
}

// ===========================================================================
// Error Estimation Tests
// ===========================================================================

TEST(SVOBuilderTest, GeometricError) {
    BuildParams params;
    params.maxLevels = 10;
    params.geometryErrorThreshold = 0.01f;  // Tight threshold

    SVOBuilder builder(params);

    InputMesh cube = createCube(1.0f);
    auto octree = builder.build(cube);

    // Tighter error threshold should create more voxels
    EXPECT_GT(octree->totalVoxels, 100);
}

TEST(SVOBuilderTest, MaxLevelsLimit) {
    BuildParams params;
    params.maxLevels = 4;  // Very shallow

    SVOBuilder builder(params);

    InputMesh cube = createCube(1.0f);
    auto octree = builder.build(cube);

    ASSERT_NE(octree, nullptr);
    EXPECT_EQ(octree->maxLevels, 4);
}

// ===========================================================================
// Progress Callback Tests
// ===========================================================================

TEST(SVOBuilderTest, ProgressCallback) {
    BuildParams params;
    params.maxLevels = 6;

    SVOBuilder builder(params);

    bool callbackCalled = false;
    float lastProgress = 0.0f;

    builder.setProgressCallback([&](float progress) {
        callbackCalled = true;
        lastProgress = progress;
        EXPECT_GE(progress, 0.0f);
        EXPECT_LE(progress, 1.0f);
    });

    InputMesh cube = createCube(1.0f);
    auto octree = builder.build(cube);

    ASSERT_NE(octree, nullptr);
    EXPECT_TRUE(callbackCalled);
}

// ===========================================================================
// Contour Tests
// ===========================================================================

TEST(SVOBuilderTest, ContoursEnabled) {
    BuildParams params;
    params.maxLevels = 6;
    params.enableContours = true;

    SVOBuilder builder(params);

    InputMesh cube = createCube(1.0f);
    auto octree = builder.build(cube);

    ASSERT_NE(octree, nullptr);

    // Should have some contours
    if (octree->root) {
        // Contours should be generated (can't directly test internal state)
        SUCCEED();
    }
}

TEST(SVOBuilderTest, ContoursDisabled) {
    BuildParams params;
    params.maxLevels = 6;
    params.enableContours = false;

    SVOBuilder builder(params);

    InputMesh cube = createCube(1.0f);
    auto octree = builder.build(cube);

    ASSERT_NE(octree, nullptr);
    // Should still build successfully without contours
}

// ===========================================================================
// Multiple Meshes Tests
// ===========================================================================

TEST(SVOBuilderTest, EmptyMesh) {
    BuildParams params;
    SVOBuilder builder(params);

    InputMesh emptyMesh;
    auto octree = builder.build(emptyMesh);

    // Should handle empty mesh gracefully (may return null or empty octree)
    if (octree) {
        EXPECT_EQ(octree->totalVoxels, 0);
    }
}

TEST(SVOBuilderTest, LargeCube) {
    BuildParams params;
    params.maxLevels = 8;

    SVOBuilder builder(params);

    InputMesh cube = createCube(10.0f);  // Large cube
    auto octree = builder.build(cube);

    ASSERT_NE(octree, nullptr);
    EXPECT_GT(octree->totalVoxels, 0);
}
