/**
 * @file test_scene_generators.cpp
 * @brief Tests for procedural scene generators (Phase H.2.5)
 *
 * Validates density targets (±5%) for Cornell Box, Cave, and Urban scenes.
 * Ensures reproducibility and spatial coherence.
 */

#include <gtest/gtest.h>
#include "Data/SceneGenerator.h"

using namespace VIXEN::RenderGraph;

class SceneGeneratorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Test with multiple resolutions to verify scaling
    }

    // Helper: Check density within tolerance
    bool IsDensityValid(float actualPercent, float targetPercent, float tolerancePercent = 5.0f) {
        float lower = targetPercent - tolerancePercent;
        float upper = targetPercent + tolerancePercent;
        return actualPercent >= lower && actualPercent <= upper;
    }
};

// ============================================================================
// Cornell Box Tests (Target: 10% density ±5%)
// ============================================================================

TEST_F(SceneGeneratorTest, CornellBox_64_DensityTarget) {
    VoxelGrid grid(64);
    CornellBoxGenerator::Generate(grid);

    float density = grid.GetDensityPercent();
    uint32_t solidCount = grid.CountSolidVoxels();

    EXPECT_TRUE(IsDensityValid(density, 10.0f, 5.0f))
        << "Cornell Box 64³: density=" << density << "%, expected 10±5%";
    EXPECT_GT(solidCount, 0u) << "Cornell Box should have solid voxels";
}

TEST_F(SceneGeneratorTest, CornellBox_128_DensityTarget) {
    VoxelGrid grid(128);
    CornellBoxGenerator::Generate(grid);

    float density = grid.GetDensityPercent();

    EXPECT_TRUE(IsDensityValid(density, 10.0f, 5.0f))
        << "Cornell Box 128³: density=" << density << "%, expected 10±5%";
}

TEST_F(SceneGeneratorTest, CornellBox_256_DensityTarget) {
    VoxelGrid grid(256);
    CornellBoxGenerator::Generate(grid);

    float density = grid.GetDensityPercent();

    EXPECT_TRUE(IsDensityValid(density, 10.0f, 5.0f))
        << "Cornell Box 256³: density=" << density << "%, expected 10±5%";
}

TEST_F(SceneGeneratorTest, CornellBox_Reproducibility) {
    // Generate twice, should be identical
    VoxelGrid grid1(64);
    VoxelGrid grid2(64);

    CornellBoxGenerator::Generate(grid1);
    CornellBoxGenerator::Generate(grid2);

    EXPECT_EQ(grid1.GetDensityPercent(), grid2.GetDensityPercent())
        << "Cornell Box generation must be deterministic";

    // Sample check: compare a few voxels
    for (uint32_t i = 0; i < 10; ++i) {
        uint32_t x = i * 6;
        uint32_t y = i * 6;
        uint32_t z = i * 6;
        EXPECT_EQ(grid1.Get(x, y, z), grid2.Get(x, y, z))
            << "Voxel mismatch at (" << x << "," << y << "," << z << ")";
    }
}

// ============================================================================
// Cave System Tests (Target: 50% density ±5%)
// ============================================================================

TEST_F(SceneGeneratorTest, Cave_64_DensityTarget) {
    VoxelGrid grid(64);
    CaveSystemGenerator::Generate(grid);

    float density = grid.GetDensityPercent();

    EXPECT_TRUE(IsDensityValid(density, 50.0f, 5.0f))
        << "Cave 64³: density=" << density << "%, expected 50±5%";
}

TEST_F(SceneGeneratorTest, Cave_128_DensityTarget) {
    VoxelGrid grid(128);
    CaveSystemGenerator::Generate(grid);

    float density = grid.GetDensityPercent();

    EXPECT_TRUE(IsDensityValid(density, 50.0f, 5.0f))
        << "Cave 128³: density=" << density << "%, expected 50±5%";
}

TEST_F(SceneGeneratorTest, Cave_256_DensityTarget) {
    VoxelGrid grid(256);
    CaveSystemGenerator::Generate(grid);

    float density = grid.GetDensityPercent();

    EXPECT_TRUE(IsDensityValid(density, 50.0f, 5.0f))
        << "Cave 256³: density=" << density << "%, expected 50±5%";
}

TEST_F(SceneGeneratorTest, Cave_Reproducibility) {
    // Perlin noise with fixed seed should be deterministic
    VoxelGrid grid1(64);
    VoxelGrid grid2(64);

    CaveSystemGenerator::Generate(grid1);
    CaveSystemGenerator::Generate(grid2);

    EXPECT_EQ(grid1.GetDensityPercent(), grid2.GetDensityPercent())
        << "Cave generation must be deterministic";
}

TEST_F(SceneGeneratorTest, Cave_CustomDensity) {
    // Test custom threshold for different densities
    VoxelGrid grid30(64);
    VoxelGrid grid70(64);

    CaveSystemGenerator::Generate(grid30, 4.0f, 0.3f);  // ~30% density
    CaveSystemGenerator::Generate(grid70, 4.0f, 0.7f);  // ~70% density

    float density30 = grid30.GetDensityPercent();
    float density70 = grid70.GetDensityPercent();

    EXPECT_LT(density30, 40.0f) << "Low threshold should produce sparse cave";
    EXPECT_GT(density70, 60.0f) << "High threshold should produce dense cave";
    EXPECT_LT(density30, density70) << "Density should increase with threshold";
}

// ============================================================================
// Urban Grid Tests (Target: 90% density ±5%)
// ============================================================================

TEST_F(SceneGeneratorTest, Urban_64_DensityTarget) {
    VoxelGrid grid(64);
    UrbanGridGenerator::Generate(grid);

    float density = grid.GetDensityPercent();

    EXPECT_TRUE(IsDensityValid(density, 90.0f, 5.0f))
        << "Urban 64³: density=" << density << "%, expected 90±5%";
}

TEST_F(SceneGeneratorTest, Urban_128_DensityTarget) {
    VoxelGrid grid(128);
    UrbanGridGenerator::Generate(grid);

    float density = grid.GetDensityPercent();

    EXPECT_TRUE(IsDensityValid(density, 90.0f, 5.0f))
        << "Urban 128³: density=" << density << "%, expected 90±5%";
}

TEST_F(SceneGeneratorTest, Urban_256_DensityTarget) {
    VoxelGrid grid(256);
    UrbanGridGenerator::Generate(grid);

    float density = grid.GetDensityPercent();

    EXPECT_TRUE(IsDensityValid(density, 90.0f, 5.0f))
        << "Urban 256³: density=" << density << "%, expected 90±5%";
}

TEST_F(SceneGeneratorTest, Urban_Reproducibility) {
    VoxelGrid grid1(64);
    VoxelGrid grid2(64);

    UrbanGridGenerator::Generate(grid1);
    UrbanGridGenerator::Generate(grid2);

    EXPECT_EQ(grid1.GetDensityPercent(), grid2.GetDensityPercent())
        << "Urban generation must be deterministic";
}

// ============================================================================
// VoxelGrid Utility Tests
// ============================================================================

TEST_F(SceneGeneratorTest, VoxelGrid_EmptyDensity) {
    VoxelGrid grid(64);
    grid.Clear();

    EXPECT_EQ(grid.GetDensityPercent(), 0.0f) << "Empty grid should have 0% density";
    EXPECT_EQ(grid.CountSolidVoxels(), 0u);
}

TEST_F(SceneGeneratorTest, VoxelGrid_FullDensity) {
    VoxelGrid grid(8); // Small for speed
    for (uint32_t z = 0; z < 8; ++z) {
        for (uint32_t y = 0; y < 8; ++y) {
            for (uint32_t x = 0; x < 8; ++x) {
                grid.Set(x, y, z, 1);
            }
        }
    }

    EXPECT_FLOAT_EQ(grid.GetDensityPercent(), 100.0f) << "Full grid should have 100% density";
    EXPECT_EQ(grid.CountSolidVoxels(), 512u);
}

TEST_F(SceneGeneratorTest, VoxelGrid_HalfDensity) {
    VoxelGrid grid(8); // Small for speed
    // Fill checkered pattern (every other voxel)
    for (uint32_t z = 0; z < 8; ++z) {
        for (uint32_t y = 0; y < 8; ++y) {
            for (uint32_t x = 0; x < 8; ++x) {
                if ((x + y + z) % 2 == 0) {
                    grid.Set(x, y, z, 1);
                }
            }
        }
    }

    float density = grid.GetDensityPercent();
    EXPECT_NEAR(density, 50.0f, 1.0f) << "Checkered pattern should be ~50% dense";
    EXPECT_EQ(grid.CountSolidVoxels(), 256u);
}

// ============================================================================
// Density Distribution Tests (Spatial coherence)
// ============================================================================

TEST_F(SceneGeneratorTest, CornellBox_SpatialCoherence) {
    VoxelGrid grid(64);
    CornellBoxGenerator::Generate(grid);

    // Cornell Box should have empty interior and solid boundaries
    // Check center is mostly empty
    uint32_t centerSolid = 0;
    uint32_t centerSamples = 0;
    for (uint32_t z = 20; z < 44; ++z) {
        for (uint32_t y = 20; y < 44; ++y) {
            for (uint32_t x = 20; x < 44; ++x) {
                if (grid.Get(x, y, z) != 0) centerSolid++;
                centerSamples++;
            }
        }
    }

    float centerDensity = (centerSolid * 100.0f) / centerSamples;
    EXPECT_LT(centerDensity, 30.0f) << "Cornell Box center should be mostly empty";
}

TEST_F(SceneGeneratorTest, Cave_SpatialCoherence) {
    VoxelGrid grid(64);
    CaveSystemGenerator::Generate(grid);

    // Cave should have connected regions (not random noise)
    // Sample along a line and check for clustering
    uint32_t transitions = 0;
    bool prevSolid = (grid.Get(32, 32, 0) != 0);

    for (uint32_t z = 1; z < 64; ++z) {
        bool currentSolid = (grid.Get(32, 32, z) != 0);
        if (currentSolid != prevSolid) {
            transitions++;
        }
        prevSolid = currentSolid;
    }

    // Coherent terrain should have fewer transitions than random noise
    // Random 50% density would have ~32 transitions, coherent ~5-15
    EXPECT_LT(transitions, 25u) << "Cave should have coherent structures, not random noise";
}

TEST_F(SceneGeneratorTest, Urban_SpatialCoherence) {
    VoxelGrid grid(64);
    UrbanGridGenerator::Generate(grid);

    // Urban grid should have dense buildings separated by sparse streets
    // Check that at least some voxels are empty (streets exist)
    uint32_t emptyCount = grid.GetResolution() * grid.GetResolution() * grid.GetResolution() - grid.CountSolidVoxels();
    EXPECT_GT(emptyCount, 1000u) << "Urban grid should have streets (empty spaces)";
}
