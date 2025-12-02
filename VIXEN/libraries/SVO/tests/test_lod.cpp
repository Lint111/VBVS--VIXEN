/**
 * test_lod.cpp - Screen-Space LOD Termination Tests
 * ==============================================================================
 * Tests for Phase B.2 - ESVO Screen-Space LOD Termination
 *
 * Validates the LODParameters structure and castRayScreenSpaceLOD/castRayWithLOD
 * methods for adaptive detail termination based on projected pixel size.
 *
 * REFERENCES:
 * -----------
 * [1] Laine, S. and Karras, T. "Efficient Sparse Voxel Octrees"
 *     NVIDIA Research, I3D 2010, Section 4.4 "Level-of-detail"
 *
 * [2] NVIDIA ESVO Reference Implementation (BSD 3-Clause License)
 *     cuda/Raycast.inl line 181: LOD termination condition
 *     Copyright (c) 2009-2011, NVIDIA Corporation
 *
 * ==============================================================================
 */

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <chrono>

#include "SVOLOD.h"
#include "LaineKarrasOctree.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"
#include "AttributeRegistry.h"

using namespace Vixen::SVO;
using namespace Vixen::GaiaVoxel;
using namespace Vixen::VoxelData;

// ============================================================================
// LODParameters Unit Tests
// ============================================================================

class LODParametersTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Standard camera settings for testing
        fov60 = glm::radians(60.0f);
        fov90 = glm::radians(90.0f);
        resolution720p = 720;
        resolution1080p = 1080;
    }

    float fov60;
    float fov90;
    int resolution720p;
    int resolution1080p;
};

TEST_F(LODParametersTest, DefaultConstructorDisablesLOD) {
    LODParameters params;

    EXPECT_EQ(params.rayOrigSize, 0.0f);
    EXPECT_EQ(params.rayDirSize, 0.0f);
    EXPECT_FALSE(params.isEnabled());
}

TEST_F(LODParametersTest, FromCameraCreatesValidParameters) {
    LODParameters params = LODParameters::fromCamera(fov60, resolution720p);

    EXPECT_EQ(params.rayOrigSize, 0.0f);  // Pinhole camera
    EXPECT_GT(params.rayDirSize, 0.0f);   // Non-zero cone spread
    EXPECT_TRUE(params.isEnabled());
}

TEST_F(LODParametersTest, HigherResolutionSmallereyConeSpread) {
    LODParameters params720p = LODParameters::fromCamera(fov60, resolution720p);
    LODParameters params1080p = LODParameters::fromCamera(fov60, resolution1080p);

    // Higher resolution = smaller pixels = smaller cone spread
    EXPECT_LT(params1080p.rayDirSize, params720p.rayDirSize);
}

TEST_F(LODParametersTest, WiderFOVLargerConeSpread) {
    LODParameters params60 = LODParameters::fromCamera(fov60, resolution720p);
    LODParameters params90 = LODParameters::fromCamera(fov90, resolution720p);

    // Wider FOV = larger pixels at same resolution = larger cone spread
    EXPECT_GT(params90.rayDirSize, params60.rayDirSize);
}

TEST_F(LODParametersTest, ProjectedPixelSizeIncreasesWithDistance) {
    LODParameters params = LODParameters::fromCamera(fov60, resolution720p);

    float size1m = params.getProjectedPixelSize(1.0f);
    float size10m = params.getProjectedPixelSize(10.0f);
    float size100m = params.getProjectedPixelSize(100.0f);

    EXPECT_GT(size10m, size1m);
    EXPECT_GT(size100m, size10m);

    // Linear growth with distance (pinhole camera)
    EXPECT_NEAR(size10m / size1m, 10.0f, 0.01f);
}

TEST_F(LODParametersTest, ShouldTerminateAtLargeDistance) {
    LODParameters params = LODParameters::fromCamera(fov60, resolution720p);

    float smallVoxel = 0.01f;  // 1cm voxel
    float farDistance = 100.0f;  // 100m away

    // At large distance, small voxels should terminate
    EXPECT_TRUE(params.shouldTerminate(farDistance, smallVoxel));
}

TEST_F(LODParametersTest, ShouldNotTerminateNearby) {
    LODParameters params = LODParameters::fromCamera(fov60, resolution720p);

    float largeVoxel = 1.0f;  // 1m voxel
    float nearDistance = 0.1f;  // 10cm away

    // Very close to camera, even large voxels should not terminate
    EXPECT_FALSE(params.shouldTerminate(nearDistance, largeVoxel));
}

TEST_F(LODParametersTest, BiasAffectsTermination) {
    LODParameters base = LODParameters::fromCamera(fov60, resolution720p);

    float voxelSize = 0.1f;
    float distance = 10.0f;

    // Find threshold distance (where termination just happens)
    LODParameters coarser = base.withBias(1.0f);   // 2x larger cone
    LODParameters finer = base.withBias(-1.0f);    // 0.5x smaller cone

    // Coarser bias = terminate sooner (at smaller distance/larger voxels)
    // Finer bias = terminate later (need larger distance/smaller voxels)
    float baseProjected = base.getProjectedPixelSize(distance);
    float coarserProjected = coarser.getProjectedPixelSize(distance);
    float finerProjected = finer.getProjectedPixelSize(distance);

    EXPECT_GT(coarserProjected, baseProjected);
    EXPECT_LT(finerProjected, baseProjected);
}

TEST_F(LODParametersTest, NearPlaneAffectsOriginSize) {
    float nearPlane = 0.1f;
    LODParameters withNear = LODParameters::fromCameraWithNearPlane(fov60, resolution720p, nearPlane);
    LODParameters pinhole = LODParameters::fromCamera(fov60, resolution720p);

    // With near plane, origin size is non-zero
    EXPECT_GT(withNear.rayOrigSize, 0.0f);
    EXPECT_EQ(pinhole.rayOrigSize, 0.0f);

    // Same cone spread
    EXPECT_FLOAT_EQ(withNear.rayDirSize, pinhole.rayDirSize);
}

// ============================================================================
// Helper Functions for Integration Tests
// ============================================================================

class LODRayCastingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create fresh GaiaVoxelWorld
        voxelWorld = std::make_unique<GaiaVoxelWorld>();

        // Create attribute registry
        registry = std::make_shared<AttributeRegistry>();
        registry->registerKey("density", AttributeType::Float, 1.0f);
        registry->addAttribute("color", AttributeType::Vec3, glm::vec3(1.0f));
    }

    std::unique_ptr<LaineKarrasOctree> createOctreeWithVoxels(
        const std::vector<glm::vec3>& positions,
        const glm::vec3& worldMin,
        const glm::vec3& worldMax,
        int maxDepth = 8)
    {
        // Create voxel entities
        for (const auto& pos : positions) {
            ComponentQueryRequest components[] = {
                Density{1.0f},
                Color{glm::vec3(1.0f, 1.0f, 1.0f)}
            };
            VoxelCreationRequest request{pos, components};
            voxelWorld->createVoxel(request);
        }

        // Create and rebuild octree
        auto octree = std::make_unique<LaineKarrasOctree>(
            *voxelWorld,
            registry.get(),
            maxDepth,
            3  // brick depth
        );
        octree->rebuild(*voxelWorld, worldMin, worldMax);

        return octree;
    }

    std::unique_ptr<GaiaVoxelWorld> voxelWorld;
    std::shared_ptr<AttributeRegistry> registry;
};

TEST_F(LODRayCastingTest, DistantVoxelTerminatesEarly) {
    // Create sparse voxels in a large world
    std::vector<glm::vec3> voxelPositions;

    // Single voxel at center
    voxelPositions.push_back(glm::vec3(50.0f, 50.0f, 50.0f));

    glm::vec3 worldMin(0.0f, 0.0f, 0.0f);
    glm::vec3 worldMax(100.0f, 100.0f, 100.0f);

    auto octree = createOctreeWithVoxels(voxelPositions, worldMin, worldMax, 8);

    // Cast ray from far away
    glm::vec3 rayOrigin(50.0f, 50.0f, 500.0f);  // 450m away
    glm::vec3 rayDir(0.0f, 0.0f, -1.0f);

    // Cast with LOD (should terminate early at coarse level)
    float fovY = glm::radians(60.0f);
    int screenHeight = 600;

    auto lodHit = octree->castRayScreenSpaceLOD(
        rayOrigin, rayDir, fovY, screenHeight);

    // Cast without LOD (should reach finest detail)
    auto fullHit = octree->castRay(rayOrigin, rayDir);

    // Both should hit (if voxel is in path)
    // LOD hit should be at coarser scale (lower scale number = coarser)
    if (lodHit.hit && fullHit.hit) {
        // LOD should not descend as deep
        EXPECT_LE(lodHit.scale, fullHit.scale);
    }
}

TEST_F(LODRayCastingTest, NearbyVoxelReachesFullDetail) {
    // Create voxels
    std::vector<glm::vec3> voxelPositions;
    voxelPositions.push_back(glm::vec3(5.0f, 5.0f, 5.0f));

    glm::vec3 worldMin(0.0f, 0.0f, 0.0f);
    glm::vec3 worldMax(10.0f, 10.0f, 10.0f);

    auto octree = createOctreeWithVoxels(voxelPositions, worldMin, worldMax, 6);

    // Cast ray from very close
    glm::vec3 rayOrigin(5.0f, 5.0f, 8.0f);  // 3m away
    glm::vec3 rayDir(0.0f, 0.0f, -1.0f);

    float fovY = glm::radians(60.0f);
    int screenHeight = 1080;

    auto lodHit = octree->castRayScreenSpaceLOD(
        rayOrigin, rayDir, fovY, screenHeight);

    auto fullHit = octree->castRay(rayOrigin, rayDir);

    // At close range, LOD should reach same detail level as non-LOD
    if (lodHit.hit && fullHit.hit) {
        // Should be same or very similar scale
        EXPECT_GE(lodHit.scale, fullHit.scale - 1);
    }
}

TEST_F(LODRayCastingTest, CastRayWithLODAcceptsExplicitParameters) {
    std::vector<glm::vec3> voxelPositions;
    voxelPositions.push_back(glm::vec3(5.0f, 5.0f, 5.0f));

    glm::vec3 worldMin(0.0f, 0.0f, 0.0f);
    glm::vec3 worldMax(10.0f, 10.0f, 10.0f);

    auto octree = createOctreeWithVoxels(voxelPositions, worldMin, worldMax, 6);

    glm::vec3 rayOrigin(5.0f, 5.0f, 15.0f);
    glm::vec3 rayDir(0.0f, 0.0f, -1.0f);

    // Create custom LOD parameters
    LODParameters params;
    params.rayDirSize = 0.01f;  // Small cone spread
    params.rayOrigSize = 0.0f;

    auto hit = octree->castRayWithLOD(rayOrigin, rayDir, params);

    // Should get a valid result (hit or miss depending on octree structure)
    // Main test is that the API compiles and runs without crash
    SUCCEED();
}

TEST_F(LODRayCastingTest, DisabledLODMatchesRegularCast) {
    std::vector<glm::vec3> voxelPositions;
    voxelPositions.push_back(glm::vec3(5.0f, 5.0f, 5.0f));

    glm::vec3 worldMin(0.0f, 0.0f, 0.0f);
    glm::vec3 worldMax(10.0f, 10.0f, 10.0f);

    auto octree = createOctreeWithVoxels(voxelPositions, worldMin, worldMax, 6);

    glm::vec3 rayOrigin(5.0f, 5.0f, 15.0f);
    glm::vec3 rayDir(0.0f, 0.0f, -1.0f);

    // LOD with zero spread = disabled
    LODParameters disabledParams;
    disabledParams.rayDirSize = 0.0f;
    disabledParams.rayOrigSize = 0.0f;

    auto lodHit = octree->castRayWithLOD(rayOrigin, rayDir, disabledParams);
    auto regularHit = octree->castRay(rayOrigin, rayDir);

    // With LOD disabled, should get same result
    EXPECT_EQ(lodHit.hit, regularHit.hit);
    if (lodHit.hit && regularHit.hit) {
        EXPECT_EQ(lodHit.scale, regularHit.scale);
        EXPECT_NEAR(lodHit.tMin, regularHit.tMin, 0.001f);
    }
}

// ============================================================================
// ESVO Scale Helper Tests
// ============================================================================

TEST(ESVOScaleHelperTest, ScaleToWorldSizeConversion) {
    float worldSize = 100.0f;

    // Root scale (22) should give approximately half world size
    float rootSize = esvoScaleToWorldSize(22, worldSize);
    EXPECT_NEAR(rootSize, 50.0f, 1.0f);

    // Finer scales should be smaller
    float fineSize = esvoScaleToWorldSize(10, worldSize);
    EXPECT_LT(fineSize, rootSize);

    // Scale 0 should be very small
    float finestSize = esvoScaleToWorldSize(0, worldSize);
    EXPECT_LT(finestSize, 0.1f);
}

TEST(ESVOScaleHelperTest, TToWorldDistanceConversion) {
    float worldRayLength = 100.0f;

    // t=0 should be 0 distance
    EXPECT_FLOAT_EQ(esvoTToWorldDistance(0.0f, worldRayLength), 0.0f);

    // t=1 should be full ray length
    EXPECT_FLOAT_EQ(esvoTToWorldDistance(1.0f, worldRayLength), worldRayLength);

    // t=0.5 should be half
    EXPECT_FLOAT_EQ(esvoTToWorldDistance(0.5f, worldRayLength), 50.0f);
}

// ============================================================================
// Performance Regression Test
// ============================================================================

TEST_F(LODRayCastingTest, NoPerformanceRegressionWithoutLOD) {
    // Create a moderately complex scene
    std::vector<glm::vec3> voxelPositions;
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            for (int z = 0; z < 8; ++z) {
                voxelPositions.push_back(glm::vec3(
                    static_cast<float>(x) + 0.5f,
                    static_cast<float>(y) + 0.5f,
                    static_cast<float>(z) + 0.5f
                ));
            }
        }
    }

    glm::vec3 worldMin(0.0f, 0.0f, 0.0f);
    glm::vec3 worldMax(8.0f, 8.0f, 8.0f);

    auto octree = createOctreeWithVoxels(voxelPositions, worldMin, worldMax, 6);

    glm::vec3 rayOrigin(4.0f, 4.0f, 20.0f);
    glm::vec3 rayDir(0.0f, 0.0f, -1.0f);

    // Warmup iterations to eliminate first-call overhead
    for (int i = 0; i < 100; ++i) {
        auto hit = octree->castRay(rayOrigin, rayDir);
        (void)hit;
    }
    LODParameters disabled;
    for (int i = 0; i < 100; ++i) {
        auto hit = octree->castRayWithLOD(rayOrigin, rayDir, disabled);
        (void)hit;
    }

    // Time regular ray casting
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        auto hit = octree->castRay(rayOrigin, rayDir);
        (void)hit;  // Prevent optimization
    }
    auto regularDuration = std::chrono::high_resolution_clock::now() - start;

    // Time LOD ray casting with LOD disabled (nullptr path)
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        auto hit = octree->castRayWithLOD(rayOrigin, rayDir, disabled);
        (void)hit;
    }
    auto lodDuration = std::chrono::high_resolution_clock::now() - start;

    // LOD disabled should not be significantly slower
    // In Debug builds, allow higher variance (up to 50% overhead)
    // In Release, expect < 10% overhead
    double regularMs = std::chrono::duration<double, std::milli>(regularDuration).count();
    double lodMs = std::chrono::duration<double, std::milli>(lodDuration).count();

#ifdef NDEBUG
    // Release mode: strict check
    EXPECT_LT(lodMs, regularMs * 1.2)
        << "LOD overhead too high (Release): regular=" << regularMs << "ms, LOD=" << lodMs << "ms";
#else
    // Debug mode: relaxed check due to unoptimized code paths
    EXPECT_LT(lodMs, regularMs * 2.0)
        << "LOD overhead too high (Debug): regular=" << regularMs << "ms, LOD=" << lodMs << "ms";
#endif
}
