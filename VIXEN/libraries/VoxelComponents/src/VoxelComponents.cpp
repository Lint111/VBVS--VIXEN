#include "VoxelComponents.h"
#include <cmath>

namespace GaiaVoxel {

// ============================================================================
// Morton Code (Z-Order Curve) - Encode/Decode
// ============================================================================

/**
 * Expand 21-bit integer by inserting two zeros between each bit.
 * Used for Morton code encoding (interleaving X/Y/Z).
 *
 * Example: 0b111 (7) → 0b001001001 (73)
 */
static uint64_t expandBits(uint32_t v) {
    uint64_t x = v & 0x1FFFFF;  // Mask to 21 bits
    x = (x | x << 32) & 0x1F00000000FFFF;
    x = (x | x << 16) & 0x1F0000FF0000FF;
    x = (x | x << 8)  & 0x100F00F00F00F00F;
    x = (x | x << 4)  & 0x10C30C30C30C30C3;
    x = (x | x << 2)  & 0x1249249249249249;
    return x;
}

/**
 * Compact Morton code bits back to 21-bit integer.
 * Inverse of expandBits().
 */
static uint32_t compactBits(uint64_t x) {
    x &= 0x1249249249249249;
    x = (x ^ (x >> 2))  & 0x10C30C30C30C30C3;
    x = (x ^ (x >> 4))  & 0x100F00F00F00F00F;
    x = (x ^ (x >> 8))  & 0x1F0000FF0000FF;
    x = (x ^ (x >> 16)) & 0x1F00000000FFFF;
    x = (x ^ (x >> 32)) & 0x1FFFFF;
    return static_cast<uint32_t>(x);
}

/**
 * Encode 3D position into Morton code.
 * X/Y/Z interleaved: ZYXZYXZYX...
 */
static uint64_t encodeMorton(int x, int y, int z) {
    // Handle negative coordinates via offset (shift to positive range)
    const int offset = 1048576;  // 2^20 (center of 21-bit range)
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
    const int offset = 1048576;

    uint32_t x = compactBits(morton);
    uint32_t y = compactBits(morton >> 1);
    uint32_t z = compactBits(morton >> 2);

    return glm::ivec3(
        static_cast<int>(x) - offset,
        static_cast<int>(y) - offset,
        static_cast<int>(z) - offset
    );
}

// ============================================================================
// MortonKey Implementation
// ============================================================================

glm::ivec3 MortonKey::toGridPos() const {
    return decodeMorton(code);
}
glm::vec3 MortonKey::toWorldPos() const {
    return decodeMorton(code);
}

MortonKey MortonKey::fromPosition(const glm::vec3& pos) {
    return fromPosition(glm::ivec3(
        static_cast<int>(std::floor(pos.x)),
        static_cast<int>(std::floor(pos.y)),
        static_cast<int>(std::floor(pos.z))
    ));
}

MortonKey MortonKey::fromPosition(const glm::ivec3& pos) {
    return MortonKey{encodeMorton(pos.x, pos.y, pos.z)};
}

} // namespace GaiaVoxel
