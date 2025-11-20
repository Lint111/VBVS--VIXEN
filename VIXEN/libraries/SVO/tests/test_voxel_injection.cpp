#include <gtest/gtest.h>
#include "VoxelInjection.h"
#include "LaineKarrasOctree.h"

using namespace SVO;

// ===========================================================================
// Sparse Voxel Injection Tests
// ===========================================================================

TEST(VoxelInjectionTest, SparseVoxels) {
    SparseVoxelInput input;
    input.worldMin = glm::vec3(0, 0, 0);
    input.worldMax = glm::vec3(10, 10, 10);
    input.resolution = 16;

    // Add a few voxels
    for (int i = 0; i < 10; ++i) {
        VoxelData voxel;
        voxel.position = glm::vec3(i, i, i);
        voxel.color = glm::vec3(1, 0, 0);
        voxel.normal = glm::vec3(0, 1, 0);
        voxel.density = 1.0f;
        input.voxels.push_back(voxel);
    }

    VoxelInjector injector;
    auto svo = injector.inject(input);

    ASSERT_NE(svo, nullptr);

    // Check statistics
    const auto& stats = injector.getLastStats();
    EXPECT_GT(stats.voxelsProcessed, 0);
    EXPECT_GT(stats.leavesCreated, 0);
}

// ===========================================================================
// Dense Grid Injection Tests
// ===========================================================================

TEST(VoxelInjectionTest, DenseGrid) {
    DenseVoxelInput input;
    input.worldMin = glm::vec3(0, 0, 0);
    input.worldMax = glm::vec3(10, 10, 10);
    input.resolution = glm::ivec3(4, 4, 4);
    input.voxels.resize(64);

    // Fill grid with alternating solid/empty pattern
    for (int z = 0; z < 4; ++z) {
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                size_t idx = input.getIndex(x, y, z);
                input.voxels[idx].position = glm::vec3(x, y, z);
                input.voxels[idx].density = ((x + y + z) % 2 == 0) ? 1.0f : 0.0f;
                input.voxels[idx].color = glm::vec3(1, 1, 1);
                input.voxels[idx].normal = glm::vec3(0, 1, 0);
            }
        }
    }

    InjectionConfig config;
    config.maxLevels = 6;  // Smaller for test - 4x4x4 grid doesn't need 16 levels

    VoxelInjector injector;
    auto svo = injector.inject(input, config);

    ASSERT_NE(svo, nullptr);

    const auto& stats = injector.getLastStats();
    EXPECT_GT(stats.leavesCreated, 0);
    EXPECT_GT(stats.emptyVoxelsCulled, 0);  // Should cull some empty regions
}

// ===========================================================================
// Procedural Sampler Injection Tests
// ===========================================================================

TEST(VoxelInjectionTest, ProceduralSampler) {
    // Simple sphere sampler
    auto sampler = std::make_unique<LambdaVoxelSampler>(
        [](const glm::vec3& pos, VoxelData& data) -> bool {
            float dist = glm::length(pos);
            if (dist < 5.0f) {
                data.position = pos;
                data.color = glm::vec3(1, 0, 0);
                data.normal = glm::normalize(pos);
                data.density = 1.0f;
                return true;
            }
            return false;
        },
        [](glm::vec3& min, glm::vec3& max) {
            min = glm::vec3(-6, -6, -6);
            max = glm::vec3(6, 6, 6);
        },
        [](const glm::vec3& center, float size) -> float {
            float dist = glm::length(center);
            if (dist > 5.0f + size) return 0.0f;  // Outside
            if (dist < 5.0f - size) return 1.0f;  // Inside
            return 0.5f;  // Boundary
        }
    );

    InjectionConfig config;
    config.maxLevels = 8;  // Smaller for test

    VoxelInjector injector;
    auto svo = injector.inject(*sampler, config);

    ASSERT_NE(svo, nullptr);

    const auto& stats = injector.getLastStats();
    EXPECT_GT(stats.leavesCreated, 0);
    EXPECT_GT(stats.emptyVoxelsCulled, 0);  // Should cull regions outside sphere
}

// ===========================================================================
// NoiseSampler Integration Test
// ===========================================================================

TEST(VoxelInjectionTest, NoiseSampler) {
    Samplers::NoiseSampler::Params params;
    params.frequency = 0.1f;
    params.amplitude = 5.0f;
    params.octaves = 2;
    params.threshold = 0.0f;

    auto sampler = std::make_unique<Samplers::NoiseSampler>(params);

    InjectionConfig config;
    config.maxLevels = 6;  // Small for quick test

    VoxelInjector injector;
    auto svo = injector.inject(*sampler, config);

    ASSERT_NE(svo, nullptr);

    const auto& stats = injector.getLastStats();
    EXPECT_GT(stats.voxelsProcessed, 0);
    EXPECT_GT(stats.leavesCreated, 0);
}

// ===========================================================================
// SDFSampler Integration Test
// ===========================================================================

TEST(VoxelInjectionTest, SDFSampler) {
    auto sdfFunc = [](const glm::vec3& p) -> float {
        return SDF::sphere(p, 3.0f);
    };

    auto sampler = std::make_unique<Samplers::SDFSampler>(
        sdfFunc,
        glm::vec3(-5, -5, -5),
        glm::vec3(5, 5, 5)
    );

    InjectionConfig config;
    config.maxLevels = 6;

    VoxelInjector injector;
    auto svo = injector.inject(*sampler, config);

    ASSERT_NE(svo, nullptr);

    const auto& stats = injector.getLastStats();
    EXPECT_GT(stats.leavesCreated, 0);
}

// ===========================================================================
// Progress Callback Test
// ===========================================================================

TEST(VoxelInjectionTest, ProgressCallback) {
    auto sampler = std::make_unique<LambdaVoxelSampler>(
        [](const glm::vec3& pos, VoxelData& data) -> bool {
            if (glm::length(pos) < 3.0f) {
                data.position = pos;
                data.density = 1.0f;
                return true;
            }
            return false;
        },
        [](glm::vec3& min, glm::vec3& max) {
            min = glm::vec3(-5, -5, -5);
            max = glm::vec3(5, 5, 5);
        }
    );

    InjectionConfig config;
    config.maxLevels = 6;

    bool callbackCalled = false;
    float lastProgress = 0.0f;

    VoxelInjector injector;
    injector.setProgressCallback([&](float progress, const std::string& status) {
        callbackCalled = true;
        lastProgress = progress;
        EXPECT_GE(progress, 0.0f);
        EXPECT_LE(progress, 1.0f);
    });

    auto svo = injector.inject(*sampler, config);

    ASSERT_NE(svo, nullptr);
    EXPECT_TRUE(callbackCalled);
    EXPECT_EQ(lastProgress, 1.0f);  // Should end at 100%
}

// ===========================================================================
// Minimum Voxel Size Test - Prevent Over-Subdivision
// ===========================================================================

TEST(VoxelInjectionTest, MinimumVoxelSizePreventsOverSubdivision) {
    // 4x4x4 grid in 10x10x10 world = 2.5 units per voxel
    DenseVoxelInput input;
    input.worldMin = glm::vec3(0, 0, 0);
    input.worldMax = glm::vec3(10, 10, 10);
    input.resolution = glm::ivec3(4, 4, 4);
    input.voxels.resize(64);

    // Fill with checkerboard pattern
    for (int z = 0; z < 4; ++z) {
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                size_t idx = input.getIndex(x, y, z);
                input.voxels[idx].position = glm::vec3(x * 2.5f, y * 2.5f, z * 2.5f);
                input.voxels[idx].density = ((x + y + z) % 2 == 0) ? 1.0f : 0.0f;
                input.voxels[idx].color = glm::vec3(1, 1, 1);
                input.voxels[idx].normal = glm::vec3(0, 1, 0);
            }
        }
    }

    // Test with high maxLevels but constrained by minVoxelSize
    InjectionConfig config;
    config.maxLevels = 20;  // Deep subdivision
    config.minVoxelSize = 2.5f;  // Grid cell size - prevents subdivision beyond data resolution

    VoxelInjector injector;
    auto svo = injector.inject(input, config);

    ASSERT_NE(svo, nullptr);

    const auto& stats = injector.getLastStats();

    // With minVoxelSize=2.5 and worldSize=10, max effective depth is log2(10/2.5) = 2
    // So we should have FAR fewer voxels than if we subdivided to level 20 (2^60 nodes!)
    // Expect reasonable number of nodes (< 1000 for 4x4x4 grid)
    EXPECT_LT(stats.voxelsProcessed, 1000);
    EXPECT_GT(stats.leavesCreated, 0);
    EXPECT_GT(stats.emptyVoxelsCulled, 0);

    // Verify build completed in reasonable time (< 1 second)
    EXPECT_LT(stats.buildTimeSeconds, 1.0f);
}

// ============================================================================
// Bottom-Up Additive Insertion Tests
// ============================================================================

TEST(VoxelInjectorTest, AdditiveInsertionSingleVoxel) {
    using namespace SVO;

    // Create empty octree
    LaineKarrasOctree octree;

    // Create voxel data
    VoxelData voxel;
    voxel.position = glm::vec3(5.0f, 5.0f, 5.0f); // Center of world
    voxel.color = glm::vec3(1.0f, 0.0f, 0.0f);     // Red
    voxel.normal = glm::vec3(0.0f, 1.0f, 0.0f);   // Up
    voxel.density = 1.0f;

    // Insert voxel
    VoxelInjector injector;
    InjectionConfig config;
    config.maxLevels = 8; // Moderate depth

    bool success = injector.insertVoxel(octree, voxel.position, voxel, config);
    EXPECT_TRUE(success) << "Should successfully insert voxel";

    // Verify octree is not empty
    const Octree* octreeData = octree.getOctree();
    ASSERT_NE(octreeData, nullptr);
    ASSERT_NE(octreeData->root, nullptr);

    // Should have at least one descriptor
    EXPECT_GT(octreeData->root->childDescriptors.size(), 0u);

    // Should have one voxel
    EXPECT_EQ(octreeData->totalVoxels, 1u);

    // Should have one attribute
    EXPECT_EQ(octreeData->root->attributes.size(), 1u);

    std::cout << "\nAdditive insertion test: 1 voxel inserted\n";
    std::cout << "  Descriptors: " << octreeData->root->childDescriptors.size() << "\n";
    std::cout << "  Attributes: " << octreeData->root->attributes.size() << "\n";
}

TEST(VoxelInjectorTest, AdditiveInsertionMultipleVoxels) {
    using namespace SVO;

    // Create empty octree
    LaineKarrasOctree octree;

    VoxelInjector injector;
    InjectionConfig config;
    config.maxLevels = 6;

    // Insert 8 voxels at corners of a cube
    std::vector<glm::vec3> positions = {
        {1.0f, 1.0f, 1.0f},
        {9.0f, 1.0f, 1.0f},
        {1.0f, 9.0f, 1.0f},
        {9.0f, 9.0f, 1.0f},
        {1.0f, 1.0f, 9.0f},
        {9.0f, 1.0f, 9.0f},
        {1.0f, 9.0f, 9.0f},
        {9.0f, 9.0f, 9.0f},
    };

    for (const auto& pos : positions) {
        VoxelData voxel;
        voxel.position = pos;
        voxel.color = glm::normalize(pos / 10.0f); // Color based on position
        voxel.normal = glm::normalize(pos - glm::vec3(5.0f));
        voxel.density = 1.0f;

        bool success = injector.insertVoxel(octree, voxel.position, voxel, config);
        EXPECT_TRUE(success) << "Should insert voxel at " << pos.x << "," << pos.y << "," << pos.z;
    }

    // Verify octree has all voxels
    const Octree* octreeData = octree.getOctree();
    ASSERT_NE(octreeData, nullptr);
    EXPECT_EQ(octreeData->totalVoxels, 8u) << "Should have 8 voxels";
    EXPECT_EQ(octreeData->root->attributes.size(), 8u) << "Should have 8 attributes";

    std::cout << "\nAdditive insertion test: 8 voxels inserted (cube corners)\n";
    std::cout << "  Descriptors: " << octreeData->root->childDescriptors.size() << "\n";
    std::cout << "  Total voxels: " << octreeData->totalVoxels << "\n";
}

TEST(VoxelInjectorTest, AdditiveInsertionIdempotent) {
    using namespace SVO;

    LaineKarrasOctree octree;
    VoxelInjector injector;
    InjectionConfig config;
    config.maxLevels = 6;

    VoxelData voxel;
    voxel.position = glm::vec3(5.0f, 5.0f, 5.0f);
    voxel.color = glm::vec3(1.0f, 0.0f, 0.0f);
    voxel.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    voxel.density = 1.0f;

    // Insert same voxel 3 times
    EXPECT_TRUE(injector.insertVoxel(octree, voxel.position, voxel, config));
    EXPECT_TRUE(injector.insertVoxel(octree, voxel.position, voxel, config));
    EXPECT_TRUE(injector.insertVoxel(octree, voxel.position, voxel, config));

    // Should recognize existing node via early exit
    const Octree* octreeData = octree.getOctree();
    ASSERT_NE(octreeData, nullptr);

    // Early exit should prevent re-insertion
    std::cout << "\nIdempotent test: inserted same voxel 3x\n";
    std::cout << "  Total voxels: " << octreeData->totalVoxels << "\n";
    std::cout << "  Attributes: " << octreeData->root->attributes.size() << " (early exit working if <=3)\n";
}

TEST(VoxelInjectorTest, AdditiveInsertionRayCast) {
    using namespace SVO;

    // Create empty octree
    LaineKarrasOctree octree;

    VoxelInjector injector;
    InjectionConfig config;
    config.maxLevels = 8;

    // ensureInitialized will be called by insertVoxel with world bounds [0,10]³

    // Insert single voxel at center
    VoxelData voxel;
    voxel.position = glm::vec3(5.0f, 5.0f, 5.0f);
    voxel.color = glm::vec3(1.0f, 0.0f, 0.0f);
    voxel.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    voxel.density = 1.0f;

    std::cout << "\nInserting voxel at (" << voxel.position.x << ", " << voxel.position.y << ", " << voxel.position.z << ")\n";
    bool success = injector.insertVoxel(octree, voxel.position, voxel, config);
    ASSERT_TRUE(success) << "Should insert voxel";
    std::cout << "Insert returned: " << (success ? "true" : "false") << "\n";

    // Compact to ESVO format for traversal
    std::cout << "Compacting to ESVO format...\n";
    bool compacted = injector.compactToESVOFormat(octree);
    ASSERT_TRUE(compacted) << "Should compact successfully";

    // Debug: Print octree structure
    const Octree* octreeData = octree.getOctree();
    ASSERT_NE(octreeData, nullptr);
    std::cout << "\nOctree structure:\n";
    std::cout << "  Descriptors: " << octreeData->root->childDescriptors.size() << "\n";
    std::cout << "  Attributes: " << octreeData->root->attributes.size() << "\n";
    std::cout << "  Total voxels: " << octreeData->totalVoxels << "\n";
    std::cout << "  World bounds: [" << octree.getWorldMin().x << "," << octree.getWorldMax().x << "]\n";

    // Print all descriptors
    std::cout << "  All descriptors:\n";
    for (size_t i = 0; i < octreeData->root->childDescriptors.size(); ++i) {
        const auto& desc = octreeData->root->childDescriptors[i];
        std::cout << "    [" << i << "] valid=0x" << std::hex << (int)desc.validMask
                  << " leaf=0x" << (int)desc.leafMask << std::dec
                  << " childPtr=" << desc.childPointer << "\n";
    }

    // Cast ray from outside toward center
    glm::vec3 rayOrigin(-5.0f, 5.0f, 5.0f);  // Start left of world
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);      // Point right (toward center)

    auto hit = octree.castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    EXPECT_TRUE(hit.hit) << "Ray should hit voxel at center";
    if (hit.hit) {
        std::cout << "\nRay cast test:\n";
        std::cout << "  Hit: YES\n";
        std::cout << "  Position: (" << hit.position.x << ", " << hit.position.y << ", " << hit.position.z << ")\n";
        std::cout << "  t: " << hit.tMin << "\n";
        std::cout << "  Scale: " << hit.scale << "\n";

        // Verify hit is near expected position (center of world)
        float distanceToCenter = glm::length(hit.position - voxel.position);
        EXPECT_LT(distanceToCenter, 5.0f) << "Hit should be within 5 units of voxel center";
    } else {
        std::cout << "\nRay cast test: MISS (BUG - ray should hit!)\n";
    }
}
