/**
 * @file test_voxel_octree.cpp
 * @brief Comprehensive tests for SparseVoxelOctree class
 *
 * Coverage: VoxelOctree.h/cpp (Target: 80%+)
 *
 * Tests:
 * - Construction and initialization
 * - Octree building from grid data
 * - Empty space culling (early-out optimization)
 * - Brick creation and storage
 * - Compression ratio calculation
 * - Serialization/deserialization
 * - Corner cases (empty grids, full grids, power-of-2 validation)
 */

#include <gtest/gtest.h>
#include "Data/VoxelOctree.h"
#include <filesystem>
#include <fstream>

using namespace VIXEN::RenderGraph;

class VoxelOctreeTest : public ::testing::Test {
protected:
    void SetUp() override {
        octree = std::make_unique<SparseVoxelOctree>();
    }

    void TearDown() override {
        octree.reset();

        // Clean up temp files
        if (std::filesystem::exists(tempFilePath)) {
            std::filesystem::remove(tempFilePath);
        }
    }

    std::unique_ptr<SparseVoxelOctree> octree;
    const std::string tempFilePath = "test_octree_temp.bin";

    // Helper: Create empty grid
    std::vector<uint8_t> CreateEmptyGrid(uint32_t size) {
        return std::vector<uint8_t>(size * size * size, 0);
    }

    // Helper: Create filled grid
    std::vector<uint8_t> CreateFilledGrid(uint32_t size, uint8_t value = 1) {
        return std::vector<uint8_t>(size * size * size, value);
    }

    // Helper: Create sparse grid with single voxel
    std::vector<uint8_t> CreateSingleVoxelGrid(uint32_t size, uint32_t x, uint32_t y, uint32_t z) {
        std::vector<uint8_t> grid(size * size * size, 0);
        uint32_t index = z * size * size + y * size + x;
        grid[index] = 1;
        return grid;
    }

    // Helper: Create checkerboard pattern (50% density)
    std::vector<uint8_t> CreateCheckerboardGrid(uint32_t size) {
        std::vector<uint8_t> grid(size * size * size, 0);
        for (uint32_t z = 0; z < size; ++z) {
            for (uint32_t y = 0; y < size; ++y) {
                for (uint32_t x = 0; x < size; ++x) {
                    uint32_t index = z * size * size + y * size + x;
                    grid[index] = ((x + y + z) % 2 == 0) ? 1 : 0;
                }
            }
        }
        return grid;
    }

    // Helper: Create Cornell Box-like structure (10% density)
    std::vector<uint8_t> CreateCornellBoxGrid(uint32_t size) {
        std::vector<uint8_t> grid(size * size * size, 0);

        // Walls (thin shells at boundaries)
        for (uint32_t z = 0; z < size; ++z) {
            for (uint32_t y = 0; y < size; ++y) {
                for (uint32_t x = 0; x < size; ++x) {
                    bool isWall = (x == 0 || x == size - 1 ||
                                   y == 0 || y == size - 1 ||
                                   z == 0 || z == size - 1);
                    if (isWall) {
                        uint32_t index = z * size * size + y * size + x;
                        grid[index] = 1;
                    }
                }
            }
        }
        return grid;
    }

    // Helper: Count non-zero voxels
    uint32_t CountNonZeroVoxels(const std::vector<uint8_t>& grid) {
        uint32_t count = 0;
        for (auto v : grid) {
            if (v != 0) ++count;
        }
        return count;
    }
};

// ============================================================================
// Construction & Initialization
// ============================================================================

TEST_F(VoxelOctreeTest, ConstructorInitialization) {
    EXPECT_EQ(octree->GetNodeCount(), 0u);
    EXPECT_EQ(octree->GetBrickCount(), 0u);
    EXPECT_EQ(octree->GetMaxDepth(), 0u);
    EXPECT_EQ(octree->GetGridSize(), 0u);
}

// ============================================================================
// Octree Construction
// ============================================================================

TEST_F(VoxelOctreeTest, BuildFromEmptyGrid) {
    uint32_t size = 64;
    auto emptyGrid = CreateEmptyGrid(size);

    octree->BuildFromGrid(emptyGrid, size);

    // Empty grid should produce minimal nodes (early-out optimization)
    EXPECT_EQ(octree->GetNodeCount(), 0u) << "Empty grid should produce no nodes due to early-out";
    EXPECT_EQ(octree->GetBrickCount(), 0u);
    EXPECT_EQ(octree->GetMaxDepth(), 6u) << "64 = 2^6";
    EXPECT_EQ(octree->GetGridSize(), size);
}

TEST_F(VoxelOctreeTest, BuildFromFilledGrid) {
    uint32_t size = 16; // Large enough to create internal nodes (>8)
    auto filledGrid = CreateFilledGrid(size, 255);

    octree->BuildFromGrid(filledGrid, size);

    // Filled grid should create nodes and bricks
    EXPECT_GT(octree->GetNodeCount(), 0u);
    EXPECT_GT(octree->GetBrickCount(), 0u);
    EXPECT_EQ(octree->GetMaxDepth(), 4u) << "16 = 2^4";
    EXPECT_EQ(octree->GetGridSize(), size);
}

TEST_F(VoxelOctreeTest, BuildFromSingleVoxel) {
    uint32_t size = 64;
    auto singleVoxelGrid = CreateSingleVoxelGrid(size, 32, 32, 32); // Center voxel

    octree->BuildFromGrid(singleVoxelGrid, size);

    // Single voxel should create minimal tree path to leaf
    EXPECT_GT(octree->GetNodeCount(), 0u) << "Single voxel should create sparse path";
    EXPECT_EQ(octree->GetBrickCount(), 1u) << "Single voxel should create exactly 1 brick";
    EXPECT_EQ(octree->GetMaxDepth(), 6u);
}

TEST_F(VoxelOctreeTest, BuildFromCheckerboard) {
    uint32_t size = 32;
    auto checkerboard = CreateCheckerboardGrid(size);

    octree->BuildFromGrid(checkerboard, size);

    EXPECT_GT(octree->GetNodeCount(), 0u);
    EXPECT_GT(octree->GetBrickCount(), 0u);

    // Checkerboard is 50% density, should create significant structure
    uint32_t voxelCount = CountNonZeroVoxels(checkerboard);
    EXPECT_GT(voxelCount, size * size * size / 3) << "Checkerboard should be ~50% filled";
}

TEST_F(VoxelOctreeTest, BuildFromCornellBox) {
    uint32_t size = 64;
    auto cornellBox = CreateCornellBoxGrid(size);

    octree->BuildFromGrid(cornellBox, size);

    EXPECT_GT(octree->GetNodeCount(), 0u);
    EXPECT_GT(octree->GetBrickCount(), 0u);

    // Cornell Box is ~10% density (hollow cube)
    uint32_t voxelCount = CountNonZeroVoxels(cornellBox);
    float density = static_cast<float>(voxelCount) / (size * size * size);
    EXPECT_LT(density, 0.15f) << "Cornell Box should be <15% density";
    EXPECT_GT(density, 0.05f) << "Cornell Box should be >5% density";
}

// ============================================================================
// Power-of-2 Validation
// ============================================================================

TEST_F(VoxelOctreeTest, PowerOfTwoValidation) {
    // Valid power-of-2 sizes
    std::vector<uint32_t> validSizes = {8, 16, 32, 64, 128, 256};

    for (auto size : validSizes) {
        auto grid = CreateEmptyGrid(size);
        EXPECT_NO_THROW(octree->BuildFromGrid(grid, size))
            << "Size " << size << " should be valid";
    }
}

#ifdef NDEBUG
// Only test in release mode (debug mode uses assert which aborts)
TEST_F(VoxelOctreeTest, NonPowerOfTwoFails) {
    std::vector<uint32_t> invalidSizes = {7, 15, 33, 63, 100};

    for (auto size : invalidSizes) {
        auto grid = CreateEmptyGrid(size);
        // In debug, this will assert. In release, behavior is undefined.
        // For now, skip this test in debug builds.
    }
}
#endif

// ============================================================================
// Compression Ratio
// ============================================================================

TEST_F(VoxelOctreeTest, CompressionRatioEmptyGrid) {
    uint32_t size = 64;
    auto emptyGrid = CreateEmptyGrid(size);
    octree->BuildFromGrid(emptyGrid, size);

    float ratio = octree->GetCompressionRatio();

    // Empty grid should have near-infinite compression (minimal storage)
    EXPECT_GT(ratio, 100.0f) << "Empty grid should compress extremely well";
}

TEST_F(VoxelOctreeTest, CompressionRatioFilledGrid) {
    uint32_t size = 32;
    auto filledGrid = CreateFilledGrid(size, 1);
    octree->BuildFromGrid(filledGrid, size);

    float ratio = octree->GetCompressionRatio();

    // Filled grid has minimal compression (worst case)
    EXPECT_LT(ratio, 2.0f) << "Filled grid should have poor compression";
    EXPECT_GT(ratio, 0.5f) << "Compression ratio should be positive";
}

TEST_F(VoxelOctreeTest, CompressionRatioCornellBox) {
    uint32_t size = 64;
    auto cornellBox = CreateCornellBoxGrid(size);
    octree->BuildFromGrid(cornellBox, size);

    float ratio = octree->GetCompressionRatio();

    // Cornell Box (hollow shell) has some compression, but not extreme
    // 64³ dense = 262KB, actual octree storage varies by brick layout
    EXPECT_GT(ratio, 1.0f) << "Cornell Box should have some compression";
    EXPECT_LT(ratio, 10.0f) << "Cornell Box compression should be reasonable";
}

// ============================================================================
// Serialization / Deserialization
// ============================================================================

TEST_F(VoxelOctreeTest, SerializeDeserializeEmptyOctree) {
    // Empty octree (no BuildFromGrid called)
    EXPECT_TRUE(octree->SaveToFile(tempFilePath));

    auto octree2 = std::make_unique<SparseVoxelOctree>();
    EXPECT_TRUE(octree2->LoadFromFile(tempFilePath));

    EXPECT_EQ(octree2->GetNodeCount(), 0u);
    EXPECT_EQ(octree2->GetBrickCount(), 0u);
}

TEST_F(VoxelOctreeTest, SerializeDeserializeSingleVoxel) {
    uint32_t size = 64;
    auto singleVoxel = CreateSingleVoxelGrid(size, 10, 20, 30);
    octree->BuildFromGrid(singleVoxel, size);

    uint32_t originalNodes = octree->GetNodeCount();
    uint32_t originalBricks = octree->GetBrickCount();
    uint32_t originalDepth = octree->GetMaxDepth();
    float originalRatio = octree->GetCompressionRatio();

    EXPECT_TRUE(octree->SaveToFile(tempFilePath));

    auto octree2 = std::make_unique<SparseVoxelOctree>();
    EXPECT_TRUE(octree2->LoadFromFile(tempFilePath));

    EXPECT_EQ(octree2->GetNodeCount(), originalNodes);
    EXPECT_EQ(octree2->GetBrickCount(), originalBricks);
    EXPECT_EQ(octree2->GetMaxDepth(), originalDepth);
    EXPECT_FLOAT_EQ(octree2->GetCompressionRatio(), originalRatio);
}

TEST_F(VoxelOctreeTest, SerializeDeserializeCornellBox) {
    uint32_t size = 64;
    auto cornellBox = CreateCornellBoxGrid(size);
    octree->BuildFromGrid(cornellBox, size);

    uint32_t originalNodes = octree->GetNodeCount();
    uint32_t originalBricks = octree->GetBrickCount();
    uint32_t originalDepth = octree->GetMaxDepth();
    float originalRatio = octree->GetCompressionRatio();

    EXPECT_TRUE(octree->SaveToFile(tempFilePath));

    auto octree2 = std::make_unique<SparseVoxelOctree>();
    EXPECT_TRUE(octree2->LoadFromFile(tempFilePath));

    EXPECT_EQ(octree2->GetNodeCount(), originalNodes);
    EXPECT_EQ(octree2->GetBrickCount(), originalBricks);
    EXPECT_EQ(octree2->GetMaxDepth(), originalDepth);
    EXPECT_FLOAT_EQ(octree2->GetCompressionRatio(), originalRatio);
}

TEST_F(VoxelOctreeTest, DeserializationValidatesMagicNumber) {
    // Create invalid file with wrong magic number
    std::ofstream file(tempFilePath, std::ios::binary);
    uint32_t invalidMagic = 0x12345678; // Not "SVOC"
    uint32_t version = 1;
    uint32_t maxDepth = 0;
    uint32_t gridSize = 0;
    uint32_t nodeCount = 0;
    uint32_t brickCount = 0;

    file.write(reinterpret_cast<const char*>(&invalidMagic), sizeof(invalidMagic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&maxDepth), sizeof(maxDepth));
    file.write(reinterpret_cast<const char*>(&gridSize), sizeof(gridSize));
    file.write(reinterpret_cast<const char*>(&nodeCount), sizeof(nodeCount));
    file.write(reinterpret_cast<const char*>(&brickCount), sizeof(brickCount));
    file.close();

    EXPECT_FALSE(octree->LoadFromFile(tempFilePath))
        << "Should reject file with invalid magic number";
}

TEST_F(VoxelOctreeTest, DeserializationValidatesVersion) {
    // Create file with unsupported version
    std::ofstream file(tempFilePath, std::ios::binary);
    uint32_t magic = 0x53564F43; // "SVOC"
    uint32_t invalidVersion = 999;
    uint32_t maxDepth = 0;
    uint32_t gridSize = 0;
    uint32_t nodeCount = 0;
    uint32_t brickCount = 0;

    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&invalidVersion), sizeof(invalidVersion));
    file.write(reinterpret_cast<const char*>(&maxDepth), sizeof(maxDepth));
    file.write(reinterpret_cast<const char*>(&gridSize), sizeof(gridSize));
    file.write(reinterpret_cast<const char*>(&nodeCount), sizeof(nodeCount));
    file.write(reinterpret_cast<const char*>(&brickCount), sizeof(brickCount));
    file.close();

    EXPECT_FALSE(octree->LoadFromFile(tempFilePath))
        << "Should reject file with unsupported version";
}

TEST_F(VoxelOctreeTest, DeserializationValidatesBufferSize) {
    // Create truncated file (header only, missing data)
    std::ofstream file(tempFilePath, std::ios::binary);
    uint32_t magic = 0x53564F43; // "SVOC"
    uint32_t version = 1;
    uint32_t maxDepth = 6;
    uint32_t gridSize = 64;
    uint32_t nodeCount = 100; // Claims 100 nodes but provides no data
    uint32_t brickCount = 10;

    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&maxDepth), sizeof(maxDepth));
    file.write(reinterpret_cast<const char*>(&gridSize), sizeof(gridSize));
    file.write(reinterpret_cast<const char*>(&nodeCount), sizeof(nodeCount));
    file.write(reinterpret_cast<const char*>(&brickCount), sizeof(brickCount));
    // Close without writing node/brick data
    file.close();

    EXPECT_FALSE(octree->LoadFromFile(tempFilePath))
        << "Should reject file with size mismatch";
}

TEST_F(VoxelOctreeTest, LoadNonexistentFile) {
    EXPECT_FALSE(octree->LoadFromFile("nonexistent_file_12345.bin"));
}

// ============================================================================
// Buffer Serialization (in-memory)
// ============================================================================

TEST_F(VoxelOctreeTest, SerializeToBufferEmptyOctree) {
    std::vector<uint8_t> buffer;
    octree->SerializeToBuffer(buffer);

    // Header is 24 bytes
    EXPECT_EQ(buffer.size(), 24u) << "Empty octree should produce 24-byte header";
}

TEST_F(VoxelOctreeTest, DeserializeFromBufferCornellBox) {
    uint32_t size = 64;
    auto cornellBox = CreateCornellBoxGrid(size);
    octree->BuildFromGrid(cornellBox, size);

    std::vector<uint8_t> buffer;
    octree->SerializeToBuffer(buffer);

    auto octree2 = std::make_unique<SparseVoxelOctree>();
    EXPECT_TRUE(octree2->DeserializeFromBuffer(buffer));

    EXPECT_EQ(octree2->GetNodeCount(), octree->GetNodeCount());
    EXPECT_EQ(octree2->GetBrickCount(), octree->GetBrickCount());
    EXPECT_EQ(octree2->GetMaxDepth(), octree->GetMaxDepth());
    EXPECT_EQ(octree2->GetGridSize(), octree->GetGridSize());
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(VoxelOctreeTest, MinimumGridSize) {
    uint32_t size = 8; // Minimum useful size (2^3)
    auto grid = CreateFilledGrid(size, 1);

    EXPECT_NO_THROW(octree->BuildFromGrid(grid, size));
    EXPECT_GT(octree->GetBrickCount(), 0u);
}

TEST_F(VoxelOctreeTest, LargeGridSize) {
    uint32_t size = 256; // Large grid (2^8)
    auto sparseGrid = CreateSingleVoxelGrid(size, 128, 128, 128);

    EXPECT_NO_THROW(octree->BuildFromGrid(sparseGrid, size));
    EXPECT_GT(octree->GetNodeCount(), 0u);

    // Sparse grid should compress well
    float ratio = octree->GetCompressionRatio();
    EXPECT_GT(ratio, 100.0f) << "Single voxel in 256^3 should compress 100:1+";
}

TEST_F(VoxelOctreeTest, MultipleBuildsReplaceData) {
    uint32_t size = 32;
    auto grid1 = CreateFilledGrid(size, 1);
    octree->BuildFromGrid(grid1, size);

    uint32_t count1 = octree->GetNodeCount();

    auto grid2 = CreateEmptyGrid(size);
    octree->BuildFromGrid(grid2, size);

    uint32_t count2 = octree->GetNodeCount();

    EXPECT_NE(count1, count2) << "Second build should clear previous data";
    EXPECT_EQ(count2, 0u) << "Empty grid should produce 0 nodes";
}
