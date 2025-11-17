#include <gtest/gtest.h>
#include "VoxelInjection.h"

using namespace SVO;
using namespace SVO::Samplers;

// ===========================================================================
// NoiseSampler Tests
// ===========================================================================

TEST(NoiseSamplerTest, BasicSampling) {
    NoiseSampler::Params params;
    params.frequency = 0.1f;
    params.amplitude = 10.0f;
    params.threshold = 0.0f;

    NoiseSampler sampler(params);

    VoxelData data;

    // Sample at origin
    bool hasSolid = sampler.sample(glm::vec3(0, 0, 0), data);

    // Should return either solid or empty (deterministic noise)
    if (hasSolid) {
        EXPECT_GT(data.density, 0.0f);
        EXPECT_GE(data.color.r, 0.0f);
        EXPECT_LE(data.color.r, 1.0f);
    }
}

TEST(NoiseSamplerTest, Consistency) {
    NoiseSampler::Params params;
    NoiseSampler sampler(params);

    VoxelData data1, data2;
    glm::vec3 pos(5.0f, 10.0f, 15.0f);

    // Same position should give same result
    bool result1 = sampler.sample(pos, data1);
    bool result2 = sampler.sample(pos, data2);

    EXPECT_EQ(result1, result2);
    if (result1) {
        EXPECT_EQ(data1.density, data2.density);
    }
}

TEST(NoiseSamplerTest, DensityEstimate) {
    NoiseSampler::Params params;
    params.threshold = 0.0f;
    NoiseSampler sampler(params);

    // Density should be in [0,1]
    float density = sampler.estimateDensity(glm::vec3(0), 10.0f);
    EXPECT_GE(density, 0.0f);
    EXPECT_LE(density, 1.0f);
}

// ===========================================================================
// SDFSampler Tests
// ===========================================================================

TEST(SDFSamplerTest, SphereSDF) {
    float radius = 5.0f;

    auto sdfFunc = [radius](const glm::vec3& p) -> float {
        return SDF::sphere(p, radius);
    };

    SDFSampler sampler(sdfFunc, glm::vec3(-10), glm::vec3(10));

    VoxelData data;

    // Inside sphere
    EXPECT_TRUE(sampler.sample(glm::vec3(0, 0, 0), data));
    EXPECT_TRUE(sampler.sample(glm::vec3(2, 0, 0), data));

    // Outside sphere
    EXPECT_FALSE(sampler.sample(glm::vec3(10, 0, 0), data));
}

TEST(SDFSamplerTest, BoxSDF) {
    glm::vec3 size(5.0f, 5.0f, 5.0f);

    auto sdfFunc = [size](const glm::vec3& p) -> float {
        return SDF::box(p, size);
    };

    SDFSampler sampler(sdfFunc, glm::vec3(-10), glm::vec3(10));

    VoxelData data;

    // Inside box
    EXPECT_TRUE(sampler.sample(glm::vec3(0, 0, 0), data));
    EXPECT_TRUE(sampler.sample(glm::vec3(4, 4, 4), data));

    // Outside box
    EXPECT_FALSE(sampler.sample(glm::vec3(10, 0, 0), data));
}

TEST(SDFSamplerTest, NormalEstimation) {
    auto sdfFunc = [](const glm::vec3& p) -> float {
        return SDF::sphere(p, 5.0f);
    };

    SDFSampler sampler(sdfFunc, glm::vec3(-10), glm::vec3(10));

    VoxelData data;
    glm::vec3 pos(3, 0, 0);  // On X axis inside sphere

    ASSERT_TRUE(sampler.sample(pos, data));

    // Normal should point outward (roughly along +X)
    EXPECT_GT(data.normal.x, 0.5f);
}

// ===========================================================================
// HeightmapSampler Tests
// ===========================================================================

TEST(HeightmapSamplerTest, FlatTerrain) {
    HeightmapSampler::Params params;
    params.width = 10;
    params.height = 10;
    params.heights.resize(100, 0.5f);  // All at 50% height
    params.minHeight = 0.0f;
    params.maxHeight = 100.0f;

    HeightmapSampler sampler(params);

    VoxelData data;

    // Below terrain
    EXPECT_TRUE(sampler.sample(glm::vec3(5, 25, 5), data));  // y=25 < 50

    // Above terrain
    EXPECT_FALSE(sampler.sample(glm::vec3(5, 75, 5), data));  // y=75 > 50
}

TEST(HeightmapSamplerTest, BoundsCheck) {
    HeightmapSampler::Params params;
    params.width = 10;
    params.height = 10;
    params.heights.resize(100, 0.5f);
    params.minHeight = 0.0f;
    params.maxHeight = 100.0f;
    params.horizontalScale = 1.0f;

    HeightmapSampler sampler(params);

    glm::vec3 min, max;
    sampler.getBounds(min, max);

    EXPECT_EQ(min.y, 0.0f);
    EXPECT_EQ(max.y, 100.0f);
    EXPECT_EQ(max.x, 10.0f);  // width * horizontalScale
}

// ===========================================================================
// SDF Operations Tests
// ===========================================================================

TEST(SDFOperationsTest, Union) {
    float d1 = 5.0f;
    float d2 = 3.0f;

    float result = SDF::unionOp(d1, d2);
    EXPECT_EQ(result, 3.0f);  // Min
}

TEST(SDFOperationsTest, Subtraction) {
    float d1 = 2.0f;
    float d2 = 5.0f;

    float result = SDF::subtraction(d1, d2);
    EXPECT_EQ(result, 5.0f);  // max(-d1, d2) = max(-2, 5) = 5
}

TEST(SDFOperationsTest, Intersection) {
    float d1 = 5.0f;
    float d2 = 3.0f;

    float result = SDF::intersection(d1, d2);
    EXPECT_EQ(result, 5.0f);  // Max
}

TEST(SDFOperationsTest, SmoothUnion) {
    float d1 = 5.0f;
    float d2 = 3.0f;
    float k = 1.0f;

    float result = SDF::smoothUnion(d1, d2, k);

    // Should be less than min (3.0) due to smooth blend
    EXPECT_LT(result, 3.0f);
    EXPECT_GT(result, 2.0f);  // But not too much less
}
