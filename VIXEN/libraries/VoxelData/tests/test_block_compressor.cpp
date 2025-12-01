/**
 * @file test_block_compressor.cpp
 * @brief Tests for BlockCompressor framework and DXT implementations
 *
 * Verifies:
 * - DXT1 color compression/decompression round-trip
 * - DXT normal compression/decompression round-trip
 * - Compression ratio calculations
 * - Edge cases (single color, identical colors)
 */

#include <gtest/gtest.h>
#include <Compression/BlockCompressor.h>
#include <Compression/DXT1Compressor.h>
#include <glm/glm.hpp>
#include <array>
#include <cmath>

using namespace VoxelData;

class BlockCompressorTest : public ::testing::Test {
protected:
    DXT1ColorCompressor colorCompressor;
    DXTNormalCompressor normalCompressor;

    // Helper to compare colors with tolerance
    static bool colorsEqual(const glm::vec3& a, const glm::vec3& b, float tolerance = 0.1f) {
        return glm::length(a - b) < tolerance;
    }

    // Helper to compare normals with tolerance (accounts for lossy compression)
    static bool normalsEqual(const glm::vec3& a, const glm::vec3& b, float tolerance = 0.2f) {
        glm::vec3 na = glm::normalize(a);
        glm::vec3 nb = glm::normalize(b);
        return glm::dot(na, nb) > (1.0f - tolerance);
    }
};

// ============================================================================
// DXT1 Color Compressor Tests
// ============================================================================

TEST_F(BlockCompressorTest, DXT1ColorProperties) {
    EXPECT_EQ(colorCompressor.getBlockSize(), 16);
    EXPECT_EQ(colorCompressor.getCompressedBlockSize(), 8);  // 64 bits
    EXPECT_EQ(colorCompressor.getUncompressedElementSize(), sizeof(glm::vec3));
    EXPECT_STREQ(colorCompressor.getName(), "DXT1Color");

    // Compression ratio: 192 bytes (16 × 12) -> 8 bytes = 24:1
    // (vec3 is 12 bytes, not 3 bytes like RGB8)
    float ratio = colorCompressor.getCompressionRatio();
    EXPECT_NEAR(ratio, 24.0f, 0.1f);
}

TEST_F(BlockCompressorTest, DXT1ColorSingleColor) {
    // All same color - should compress perfectly
    std::array<glm::vec3, 16> input;
    input.fill(glm::vec3(0.5f, 0.3f, 0.7f));

    uint64_t compressed = colorCompressor.encodeBlockTyped(input.data(), 16, nullptr);

    std::array<glm::vec3, 16> output;
    colorCompressor.decodeBlockTyped(compressed, output.data());

    // All decoded colors should be close to input
    for (int i = 0; i < 16; ++i) {
        EXPECT_TRUE(colorsEqual(input[i], output[i], 0.05f))
            << "Color " << i << " differs: ("
            << output[i].r << "," << output[i].g << "," << output[i].b << ") vs ("
            << input[i].r << "," << input[i].g << "," << input[i].b << ")";
    }
}

TEST_F(BlockCompressorTest, DXT1ColorGradient) {
    // Gradient from black to white
    std::array<glm::vec3, 16> input;
    for (int i = 0; i < 16; ++i) {
        float t = static_cast<float>(i) / 15.0f;
        input[i] = glm::vec3(t, t, t);
    }

    uint64_t compressed = colorCompressor.encodeBlockTyped(input.data(), 16, nullptr);

    std::array<glm::vec3, 16> output;
    colorCompressor.decodeBlockTyped(compressed, output.data());

    // Gradient should be preserved (lossy, but endpoints should be close)
    EXPECT_TRUE(colorsEqual(output[0], glm::vec3(0.0f), 0.15f)) << "Black endpoint";
    EXPECT_TRUE(colorsEqual(output[15], glm::vec3(1.0f), 0.15f)) << "White endpoint";
}

TEST_F(BlockCompressorTest, DXT1ColorRedGreen) {
    // Two distinct colors - red and green
    std::array<glm::vec3, 16> input;
    for (int i = 0; i < 8; ++i) {
        input[i] = glm::vec3(1.0f, 0.0f, 0.0f);  // Red
    }
    for (int i = 8; i < 16; ++i) {
        input[i] = glm::vec3(0.0f, 1.0f, 0.0f);  // Green
    }

    uint64_t compressed = colorCompressor.encodeBlockTyped(input.data(), 16, nullptr);

    std::array<glm::vec3, 16> output;
    colorCompressor.decodeBlockTyped(compressed, output.data());

    // Red voxels should stay reddish, green should stay greenish
    for (int i = 0; i < 8; ++i) {
        EXPECT_GT(output[i].r, output[i].g) << "Red voxel " << i << " should be more red than green";
    }
    for (int i = 8; i < 16; ++i) {
        EXPECT_GT(output[i].g, output[i].r) << "Green voxel " << i << " should be more green than red";
    }
}

TEST_F(BlockCompressorTest, DXT1ColorPartialBlock) {
    // Only 4 valid voxels - use colors that can be well-represented by DXT
    // DXT works best when colors are similar or form a gradient
    std::array<glm::vec3, 4> input = {
        glm::vec3(0.8f, 0.2f, 0.2f),  // Reddish
        glm::vec3(0.6f, 0.3f, 0.3f),  // Slightly less red
        glm::vec3(0.4f, 0.4f, 0.4f),  // Gray
        glm::vec3(0.2f, 0.5f, 0.5f)   // Bluish-gray
    };
    std::array<int32_t, 4> indices = {0, 5, 10, 15};  // Sparse positions

    uint64_t compressed = colorCompressor.encodeBlockTyped(input.data(), 4, indices.data());

    std::array<glm::vec3, 16> output;
    colorCompressor.decodeBlockTyped(compressed, output.data());

    // Check that indexed positions have approximately correct colors
    // DXT is lossy - allow generous tolerance
    EXPECT_TRUE(colorsEqual(output[0], input[0], 0.35f)) << "Index 0";
    EXPECT_TRUE(colorsEqual(output[5], input[1], 0.35f)) << "Index 5";
    EXPECT_TRUE(colorsEqual(output[10], input[2], 0.35f)) << "Index 10";
    EXPECT_TRUE(colorsEqual(output[15], input[3], 0.35f)) << "Index 15";
}

// ============================================================================
// DXT Normal Compressor Tests
// ============================================================================

TEST_F(BlockCompressorTest, DXTNormalProperties) {
    EXPECT_EQ(normalCompressor.getBlockSize(), 16);
    EXPECT_EQ(normalCompressor.getCompressedBlockSize(), 16);  // 128 bits (2x64)
    EXPECT_EQ(normalCompressor.getUncompressedElementSize(), sizeof(glm::vec3));
    EXPECT_STREQ(normalCompressor.getName(), "DXTNormal");

    // Compression ratio: 192 bytes (16 × 12) -> 16 bytes = 12:1
    // (vec3 is 12 bytes)
    float ratio = normalCompressor.getCompressionRatio();
    EXPECT_NEAR(ratio, 12.0f, 0.1f);
}

TEST_F(BlockCompressorTest, DXTNormalUpVector) {
    // All normals pointing up
    std::array<glm::vec3, 16> input;
    input.fill(glm::vec3(0.0f, 1.0f, 0.0f));

    DXTNormalBlock compressed = normalCompressor.encodeBlockTyped(input.data(), 16, nullptr);

    std::array<glm::vec3, 16> output;
    normalCompressor.decodeBlockTyped(compressed, output.data());

    // All decoded normals should point roughly up
    for (int i = 0; i < 16; ++i) {
        glm::vec3 n = glm::normalize(output[i]);
        EXPECT_GT(n.y, 0.9f) << "Normal " << i << " should point up";
    }
}

TEST_F(BlockCompressorTest, DXTNormalVaried) {
    // Various normal directions - use a more coherent set for better compression
    std::array<glm::vec3, 16> input;

    // All normals roughly pointing upward for better compression
    for (int i = 0; i < 16; ++i) {
        float angle = static_cast<float>(i) * 0.4f;
        input[i] = glm::normalize(glm::vec3(
            std::cos(angle) * 0.3f,
            0.9f,  // Mostly up
            std::sin(angle) * 0.3f
        ));
    }

    DXTNormalBlock compressed = normalCompressor.encodeBlockTyped(input.data(), 16, nullptr);

    std::array<glm::vec3, 16> output;
    normalCompressor.decodeBlockTyped(compressed, output.data());

    // Check that normals are roughly preserved (DXT is lossy)
    // With coherent input, should get reasonable results
    int goodCount = 0;
    for (int i = 0; i < 16; ++i) {
        if (normalsEqual(input[i], output[i], 0.5f)) {
            goodCount++;
        }
    }

    // At least half should be preserved reasonably
    EXPECT_GE(goodCount, 8) << "Expected at least 8/16 normals to be reasonably preserved";
}

// ============================================================================
// CompressedBuffer Tests
// ============================================================================

TEST_F(BlockCompressorTest, CompressedBufferRoundTrip) {
    // Create buffer with DXT1 color compressor
    CompressedBuffer buffer(std::make_unique<DXT1ColorCompressor>());

    // Create test data (16 colors = 1 block) - use a gradient for better compression
    std::array<glm::vec3, 16> input;
    for (int i = 0; i < 16; ++i) {
        float t = static_cast<float>(i) / 15.0f;
        input[i] = glm::vec3(t, t * 0.5f, 1.0f - t);  // Gradient from blue to orange
    }

    // Compress
    buffer.compress(input.data(), 16);

    // Check compressed size (1 block * 8 bytes = 8 bytes)
    EXPECT_EQ(buffer.getCompressedSize(), 8);
    EXPECT_EQ(buffer.getElementCount(), 16);

    // Decompress
    std::array<glm::vec3, 16> output;
    buffer.decompress(output.data(), 16);

    // Verify endpoints are approximately preserved (DXT preserves endpoints best)
    EXPECT_TRUE(colorsEqual(input[0], output[0], 0.3f)) << "First color";
    EXPECT_TRUE(colorsEqual(input[15], output[15], 0.3f)) << "Last color";

    // Count how many colors are reasonably preserved
    int goodCount = 0;
    for (int i = 0; i < 16; ++i) {
        if (colorsEqual(input[i], output[i], 0.3f)) {
            goodCount++;
        }
    }
    EXPECT_GE(goodCount, 8) << "Expected at least half colors to be reasonably preserved";
}

TEST_F(BlockCompressorTest, CalculateCompressedSize) {
    // 16 elements = 1 block = 8 bytes
    EXPECT_EQ(colorCompressor.calculateCompressedSize(16), 8);

    // 17 elements = 2 blocks = 16 bytes
    EXPECT_EQ(colorCompressor.calculateCompressedSize(17), 16);

    // 512 elements (one brick) = 32 blocks = 256 bytes
    EXPECT_EQ(colorCompressor.calculateCompressedSize(512), 256);

    // For normals: 512 elements = 32 blocks * 16 bytes = 512 bytes
    EXPECT_EQ(normalCompressor.calculateCompressedSize(512), 512);
}

// ============================================================================
// GLSL Code Generation Tests
// ============================================================================

TEST_F(BlockCompressorTest, DXT1ColorGLSLFunction) {
    const char* glsl = DXT1ColorCompressor::getGLSLDecodeFunction();

    // Should contain key elements
    EXPECT_NE(std::string(glsl).find("decodeDXT1Color"), std::string::npos);
    EXPECT_NE(std::string(glsl).find("DXT_COLOR_COEFS"), std::string::npos);
    EXPECT_NE(std::string(glsl).find("uvec2"), std::string::npos);
    EXPECT_NE(std::string(glsl).find("vec3"), std::string::npos);
}

TEST_F(BlockCompressorTest, DXTNormalGLSLFunction) {
    const char* glsl = DXTNormalCompressor::getGLSLDecodeFunction();

    // Should contain key elements
    EXPECT_NE(std::string(glsl).find("decodeDXTNormal"), std::string::npos);
    EXPECT_NE(std::string(glsl).find("decodeRawNormal"), std::string::npos);
    EXPECT_NE(std::string(glsl).find("DXT_NORMAL_COEFS"), std::string::npos);
}
