#pragma once

#include "BlockCompressor.h"
#include <glm/glm.hpp>
#include <array>

namespace VoxelData {

/**
 * DXT1ColorCompressor - Encodes 16 RGB colors into 64-bit DXT1/BC1 block
 *
 * Based on ESVO (Laine & Karras 2010) implementation.
 *
 * Block format (64 bits):
 *   bits[31:0]  = Two RGB-565 reference colors packed:
 *                 ref0: B[4:0], G[10:5], R[15:11]
 *                 ref1: B[20:16], G[26:21], R[31:27]
 *   bits[63:32] = 16 × 2-bit interpolation indices
 *
 * Interpolation modes (per texel):
 *   00 = ref0
 *   01 = ref1
 *   10 = 2/3 * ref0 + 1/3 * ref1
 *   11 = 1/3 * ref0 + 2/3 * ref1
 *
 * Memory: 48 bytes (16 × 3 floats) → 8 bytes = 6:1 compression
 */
class DXT1ColorCompressor : public BlockCompressor<glm::vec3, uint64_t, 16> {
public:
    DXT1ColorCompressor() = default;

    /**
     * Encode 16 colors into DXT1 block
     *
     * Algorithm:
     * 1. Find two colors with maximum distance (endpoints)
     * 2. Encode endpoints as RGB-565
     * 3. For each color, find best of 4 interpolated values
     * 4. Pack 2-bit index per color
     *
     * @param colors Input colors (0-1 range per component)
     * @param validCount Number of valid colors (may be < 16)
     * @param indices Position index for each color (0-15), nullptr for sequential
     * @returns 64-bit compressed block
     */
    uint64_t encodeBlockTyped(
        const glm::vec3* colors,
        size_t validCount,
        const int32_t* indices = nullptr
    ) const override;

    /**
     * Decode DXT1 block to 16 colors
     *
     * @param block Compressed 64-bit block
     * @param output Array of 16 colors
     */
    void decodeBlockTyped(
        const uint64_t& block,
        glm::vec3* output
    ) const override;

    const char* getName() const override { return "DXT1Color"; }

    // ========================================================================
    // Static utilities for GLSL shader generation
    // ========================================================================

    /**
     * Get GLSL decode function source code
     *
     * Returns a complete GLSL function:
     *   vec3 decodeDXT1Color(uint64_t block, int texelIdx)
     */
    static const char* getGLSLDecodeFunction();

private:
    // Interpolation coefficients: {1, 0, 2/3, 1/3}
    static constexpr std::array<float, 4> LERP_COEFS = {1.0f, 0.0f, 2.0f/3.0f, 1.0f/3.0f};

    // Helper: decode RGB-565 reference colors from header
    static void decodeColorHead(const uint32_t head, glm::vec3 ref[4]);

    // Helper: encode two colors to RGB-565 header
    static uint32_t encodeColorHead(const glm::vec3& color0, const glm::vec3& color1);
};

/**
 * DXTNormalBlock - Two 64-bit blocks for normal compression
 */
struct DXTNormalBlock {
    uint64_t blockA;  // Base normal + U interpolation bits
    uint64_t blockB;  // UV axis encoding + V interpolation bits
};

/**
 * DXTNormalCompressor - Encodes 16 normals into 128-bit DXT-style block
 *
 * Based on ESVO normal compression (more complex than colors).
 *
 * Algorithm:
 * - Base normal (average of inputs) encoded as 32-bit
 * - U axis: direction from base to furthest normal
 * - V axis: direction to worst-approximated normal after U
 * - Each normal gets (2-bit U lerp, 2-bit V lerp) = 4 bits
 *
 * Memory: 48 bytes (16 × vec3) → 16 bytes = 3:1 compression
 */
class DXTNormalCompressor : public BlockCompressor<glm::vec3, DXTNormalBlock, 16> {
public:
    DXTNormalCompressor() = default;

    DXTNormalBlock encodeBlockTyped(
        const glm::vec3* normals,
        size_t validCount,
        const int32_t* indices = nullptr
    ) const override;

    void decodeBlockTyped(
        const DXTNormalBlock& block,
        glm::vec3* output
    ) const override;

    const char* getName() const override { return "DXTNormal"; }

    static const char* getGLSLDecodeFunction();

private:
    // Normal interpolation coefficients: {-1, -1/3, 1/3, 1}
    static constexpr std::array<float, 4> NORMAL_COEFS = {-1.0f, -1.0f/3.0f, 1.0f/3.0f, 1.0f};

    // Helper: encode/decode raw normal (32-bit)
    static uint32_t encodeRawNormal(const glm::vec3& normal);
    static glm::vec3 decodeRawNormal(uint32_t value);

    // Helper: encode/decode axis vector
    static uint32_t encodeNormalAxis(uint32_t headUV, const glm::vec3& axis, int shift);
    static glm::vec3 decodeNormalAxis(uint32_t headUV, int shift);
};

} // namespace VoxelData
