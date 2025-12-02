#pragma once

/**
 * MortonEncoding.h - Unified Morton Code (Z-Order Curve) Implementation
 *
 * ARCHITECTURE GOAL:
 * Single source of truth for Morton encoding across the entire VIXEN codebase.
 * Eliminates redundant conversions in the voxel pipeline:
 *
 * OLD: worldPos -> morton (Gaia) -> worldPos -> morton (SVO) -> GPU  [4 conversions]
 * NEW: worldPos -> MortonCode64 -> GPU                               [1 conversion]
 *
 * FEATURES:
 * - 64-bit Morton codes with 21-bit per axis (supports +/- 1M range)
 * - Negative coordinate support via offset (1048576 = 2^20)
 * - Brick-level operations for bulk loading
 * - Morton arithmetic for efficient local offset computation
 * - STL hash specialization for unordered containers
 *
 * USAGE:
 *   auto morton = MortonCode64::fromWorldPos(glm::ivec3{5, 10, 3});
 *   glm::ivec3 pos = morton.toWorldPos();
 *
 *   // Brick operations
 *   auto brickBase = morton.getBrickBase(8);  // Round down to 8x8x8 boundary
 *   auto voxelMorton = brickBase.addLocalOffset(x, y, z);
 *
 * @see VoxelComponents.cpp for original implementation this consolidates
 */

#include <cstdint>
#include <glm/glm.hpp>
#include <functional>

namespace Vixen::Core {

/**
 * MortonCode64 - 64-bit Morton code (Z-order curve encoding)
 *
 * Encodes 3D coordinates into a single 64-bit value with spatial locality.
 * Adjacent 3D positions have similar Morton codes, enabling:
 * - Efficient range queries
 * - Cache-friendly iteration
 * - Brick-based bulk loading
 *
 * Coordinate range: [-1048576, +1048575] per axis (21-bit per axis)
 */
struct MortonCode64 {
    uint64_t code;

    // ========================================================================
    // Construction
    // ========================================================================

    /** Default: invalid/empty Morton code (code = 0) */
    constexpr MortonCode64() : code(0) {}

    /** Explicit from raw code */
    constexpr explicit MortonCode64(uint64_t rawCode) : code(rawCode) {}

    // ========================================================================
    // Encoding - World Position to Morton Code
    // ========================================================================

    /**
     * Encode from integer world coordinates.
     * Supports negative coordinates via offset of 1048576 (2^20).
     */
    static MortonCode64 fromWorldPos(int32_t x, int32_t y, int32_t z);
    static MortonCode64 fromWorldPos(const glm::ivec3& pos);

    /**
     * Encode from floating-point world coordinates.
     * Uses floor() for voxel-grid alignment.
     */
    static MortonCode64 fromWorldPos(float x, float y, float z);
    static MortonCode64 fromWorldPos(const glm::vec3& pos);

    // ========================================================================
    // Decoding - Morton Code to World Position
    // ========================================================================

    /** Decode back to integer coordinates */
    [[nodiscard]] glm::ivec3 toWorldPos() const;

    /** Decode to floating-point (integer cast) */
    [[nodiscard]] glm::vec3 toWorldPosF() const;

    // ========================================================================
    // Morton Arithmetic - Efficient Local Offset Operations
    // ========================================================================

    /**
     * Add local offset to base Morton code.
     *
     * More efficient than decode -> add -> encode for brick iteration.
     * Assumes local coordinates are small (within brick bounds).
     *
     * @param localX Local X offset [0, brickSize)
     * @param localY Local Y offset [0, brickSize)
     * @param localZ Local Z offset [0, brickSize)
     * @return Morton code for voxel at (base + offset)
     */
    [[nodiscard]] MortonCode64 addLocalOffset(uint32_t localX, uint32_t localY, uint32_t localZ) const;

    // ========================================================================
    // Brick Operations - Bulk Loading Support
    // ========================================================================

    /**
     * Get brick base (round down to brick boundary).
     *
     * @param brickSize Brick side length (e.g., 8 for 8x8x8)
     * @return Morton code of brick's minimum corner
     */
    [[nodiscard]] MortonCode64 getBrickBase(uint32_t brickSize) const;

    /**
     * Get linear voxel offset within brick.
     *
     * @param brickSize Brick side length
     * @return Linear index [0, brickSize^3) for voxel within brick
     */
    [[nodiscard]] uint32_t getVoxelOffsetInBrick(uint32_t brickSize) const;

    /**
     * Get 3D local coordinates within brick.
     *
     * @param brickSize Brick side length
     * @return (x, y, z) offset within brick, each in [0, brickSize)
     */
    [[nodiscard]] glm::uvec3 getLocalCoordsInBrick(uint32_t brickSize) const;

    // ========================================================================
    // Comparison Operators (for sorting/maps)
    // ========================================================================

    constexpr bool operator==(const MortonCode64& other) const { return code == other.code; }
    constexpr bool operator!=(const MortonCode64& other) const { return code != other.code; }
    constexpr bool operator<(const MortonCode64& other) const { return code < other.code; }
    constexpr bool operator<=(const MortonCode64& other) const { return code <= other.code; }
    constexpr bool operator>(const MortonCode64& other) const { return code > other.code; }
    constexpr bool operator>=(const MortonCode64& other) const { return code >= other.code; }

    // ========================================================================
    // Validity
    // ========================================================================

    /** Check if Morton code represents a valid position (non-zero) */
    [[nodiscard]] constexpr bool isValid() const { return code != 0; }

    /** Invalid/null Morton code constant */
    static constexpr MortonCode64 invalid() { return MortonCode64{0}; }

    // ========================================================================
    // Constants
    // ========================================================================

    /** Offset for negative coordinate support (2^20) */
    static constexpr int32_t COORDINATE_OFFSET = 1048576;

    /** Maximum coordinate value (+/- this value supported) */
    static constexpr int32_t MAX_COORDINATE = 1048575;

    /** Bits per axis in Morton code */
    static constexpr uint32_t BITS_PER_AXIS = 21;
};

// ============================================================================
// BrickEntities - Bulk Loading Result
// ============================================================================

/**
 * Result of bulk brick entity lookup.
 * Contains all entities in a brick (e.g., 8x8x8 = 512 entities).
 */
template<size_t BrickVolume = 512>
struct BrickEntities {
    static constexpr size_t BRICK_VOLUME = BrickVolume;

    /** Morton codes for each voxel position in brick (invalid if empty) */
    std::array<MortonCode64, BrickVolume> mortonCodes;

    /** Count of valid (non-empty) voxels in brick */
    uint32_t count = 0;

    /** Check if brick is empty */
    [[nodiscard]] bool isEmpty() const { return count == 0; }

    /** Check if brick is fully populated */
    [[nodiscard]] bool isFull() const { return count == BrickVolume; }
};

} // namespace Vixen::Core

// ============================================================================
// STL Hash Specialization (for std::unordered_map/set)
// ============================================================================

template<>
struct std::hash<Vixen::Core::MortonCode64> {
    size_t operator()(const Vixen::Core::MortonCode64& m) const noexcept {
        return std::hash<uint64_t>{}(m.code);
    }
};
