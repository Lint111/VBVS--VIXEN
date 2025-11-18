#include <gtest/gtest.h>
#include "VoxelInjection.h"

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
