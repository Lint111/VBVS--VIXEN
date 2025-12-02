#include "MortonEncoding.h"
#include <cmath>

namespace Vixen::Core {

// ============================================================================
// Morton Bit Manipulation Helpers (static, internal linkage)
// ============================================================================

/**
 * Expand 21-bit integer by inserting two zeros between each bit.
 * Used for Morton code encoding (interleaving X/Y/Z).
 *
 * Example: 0b111 (7) -> 0b001001001 (73)
 *
 * Algorithm: parallel bit deposit using magic numbers
 * Each step doubles the spacing between bits.
 */
static uint64_t expandBits(uint32_t v) {
    uint64_t x = v & 0x1FFFFF;  // Mask to 21 bits
    x = (x | x << 32) & 0x1F00000000FFFFULL;
    x = (x | x << 16) & 0x1F0000FF0000FFULL;
    x = (x | x << 8)  & 0x100F00F00F00F00FULL;
    x = (x | x << 4)  & 0x10C30C30C30C30C3ULL;
    x = (x | x << 2)  & 0x1249249249249249ULL;
    return x;
}

/**
 * Compact Morton code bits back to 21-bit integer.
 * Inverse of expandBits().
 *
 * Algorithm: parallel bit extract using magic numbers
 */
static uint32_t compactBits(uint64_t x) {
    x &= 0x1249249249249249ULL;
    x = (x ^ (x >> 2))  & 0x10C30C30C30C30C3ULL;
    x = (x ^ (x >> 4))  & 0x100F00F00F00F00FULL;
    x = (x ^ (x >> 8))  & 0x1F0000FF0000FFULL;
    x = (x ^ (x >> 16)) & 0x1F00000000FFFFULL;
    x = (x ^ (x >> 32)) & 0x1FFFFFULL;
    return static_cast<uint32_t>(x);
}

/**
 * Encode 3D position into Morton code.
 * X/Y/Z bits interleaved: ZYXZYXZYX...
 *
 * Interleaving order: X in bits 0,3,6,...; Y in bits 1,4,7,...; Z in bits 2,5,8,...
 */
static uint64_t encodeMorton(int32_t x, int32_t y, int32_t z) {
    // Handle negative coordinates via offset (shift to positive range)
    const int32_t offset = MortonCode64::COORDINATE_OFFSET;
    uint32_t ux = static_cast<uint32_t>(x + offset);
    uint32_t uy = static_cast<uint32_t>(y + offset);
    uint32_t uz = static_cast<uint32_t>(z + offset);

    // Interleave bits
    uint64_t xx = expandBits(ux);
    uint64_t yy = expandBits(uy);
    uint64_t zz = expandBits(uz);

    return xx | (yy << 1) | (zz << 2);
}

/**
 * Decode Morton code back to 3D position.
 */
static glm::ivec3 decodeMorton(uint64_t morton) {
    const int32_t offset = MortonCode64::COORDINATE_OFFSET;

    uint32_t x = compactBits(morton);
    uint32_t y = compactBits(morton >> 1);
    uint32_t z = compactBits(morton >> 2);

    return glm::ivec3(
        static_cast<int32_t>(x) - offset,
        static_cast<int32_t>(y) - offset,
        static_cast<int32_t>(z) - offset
    );
}

// ============================================================================
// MortonCode64 Implementation
// ============================================================================

// ----------------------------------------------------------------------------
// Encoding
// ----------------------------------------------------------------------------

MortonCode64 MortonCode64::fromWorldPos(int32_t x, int32_t y, int32_t z) {
    return MortonCode64{encodeMorton(x, y, z)};
}

MortonCode64 MortonCode64::fromWorldPos(const glm::ivec3& pos) {
    return fromWorldPos(pos.x, pos.y, pos.z);
}

MortonCode64 MortonCode64::fromWorldPos(float x, float y, float z) {
    // Use epsilon to handle floating-point precision issues.
    // Without this, 5.0 can be represented as 4.9999... and floor to 4.
    constexpr float epsilon = 1e-5f;
    return fromWorldPos(
        static_cast<int32_t>(std::floor(x + epsilon)),
        static_cast<int32_t>(std::floor(y + epsilon)),
        static_cast<int32_t>(std::floor(z + epsilon))
    );
}

MortonCode64 MortonCode64::fromWorldPos(const glm::vec3& pos) {
    return fromWorldPos(pos.x, pos.y, pos.z);
}

// ----------------------------------------------------------------------------
// Decoding
// ----------------------------------------------------------------------------

glm::ivec3 MortonCode64::toWorldPos() const {
    return decodeMorton(code);
}

glm::vec3 MortonCode64::toWorldPosF() const {
    glm::ivec3 ipos = toWorldPos();
    return glm::vec3(ipos);
}

// ----------------------------------------------------------------------------
// Morton Arithmetic
// ----------------------------------------------------------------------------

MortonCode64 MortonCode64::addLocalOffset(uint32_t localX, uint32_t localY, uint32_t localZ) const {
    // For small offsets, encode the offset and add to base.
    // This works because Morton codes preserve spatial locality for nearby positions.
    //
    // Full decode-add-encode would be:
    //   glm::ivec3 base = toWorldPos();
    //   return fromWorldPos(base.x + localX, base.y + localY, base.z + localZ);
    //
    // Optimization: Since local offsets are small and positive, we can:
    // 1. Encode just the offset (no COORDINATE_OFFSET needed since positive)
    // 2. Add to base Morton code
    //
    // However, Morton addition is not simple addition due to bit interleaving.
    // For correctness, we use decode-add-encode which is still fast for bulk operations.

    glm::ivec3 base = toWorldPos();
    return fromWorldPos(
        base.x + static_cast<int32_t>(localX),
        base.y + static_cast<int32_t>(localY),
        base.z + static_cast<int32_t>(localZ)
    );
}

// ----------------------------------------------------------------------------
// Brick Operations
// ----------------------------------------------------------------------------

MortonCode64 MortonCode64::getBrickBase(uint32_t brickSize) const {
    // Round down each coordinate to brick boundary
    glm::ivec3 pos = toWorldPos();

    // Integer division rounds toward zero, but we need floor for negative numbers
    auto floorDiv = [](int32_t a, int32_t b) -> int32_t {
        return (a >= 0) ? (a / b) : ((a - b + 1) / b);
    };

    int32_t bs = static_cast<int32_t>(brickSize);
    glm::ivec3 brickMin{
        floorDiv(pos.x, bs) * bs,
        floorDiv(pos.y, bs) * bs,
        floorDiv(pos.z, bs) * bs
    };

    return fromWorldPos(brickMin);
}

uint32_t MortonCode64::getVoxelOffsetInBrick(uint32_t brickSize) const {
    glm::uvec3 local = getLocalCoordsInBrick(brickSize);
    // Linear index: z*brickSize^2 + y*brickSize + x
    return local.z * brickSize * brickSize + local.y * brickSize + local.x;
}

glm::uvec3 MortonCode64::getLocalCoordsInBrick(uint32_t brickSize) const {
    glm::ivec3 pos = toWorldPos();

    // Modulo that always returns positive (for negative coordinates)
    auto positiveMod = [](int32_t a, int32_t b) -> uint32_t {
        int32_t result = a % b;
        if (result < 0) result += b;
        return static_cast<uint32_t>(result);
    };

    int32_t bs = static_cast<int32_t>(brickSize);
    return glm::uvec3{
        positiveMod(pos.x, bs),
        positiveMod(pos.y, bs),
        positiveMod(pos.z, bs)
    };
}

} // namespace Vixen::Core
