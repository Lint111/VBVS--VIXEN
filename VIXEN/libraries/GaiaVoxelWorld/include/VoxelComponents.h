#pragma once

#include <gaia.h>
#include <glm/glm.hpp>
#include <cstdint>

namespace GaiaVoxel {

/**
 * ECS Components for SPARSE voxel data.
 *
 * Design principles:
 * - Morton codes encode position (NO explicit Position component)
 * - Sparse-only storage (entities created ONLY for solid voxels)
 * - Split vec3 into 3 float components for optimal SoA layout
 * - Max 4 members per component for Gaia ECS optimization
 *
 * Following Gaia ECS conventions:
 * - Simple POD structs (Plain Old Data)
 * - Default-constructible
 * - Automatically registered on first use
 */

// ============================================================================
// Spatial Indexing (NO Position component - Morton code IS position)
// ============================================================================

/**
 * Morton code - encodes 3D position in single uint64.
 *
 * Bit layout (63 bits total, 21 bits per axis):
 * [62:42] Z coordinate (21 bits, signed via offset)
 * [41:21] Y coordinate (21 bits, signed via offset)
 * [20:0]  X coordinate (21 bits, signed via offset)
 *
 * Range: [-1,048,576 to +1,048,575] per axis
 *
 * Benefits:
 * - 8 bytes vs 12 bytes (Position {x,y,z})
 * - Spatial locality preserved (nearby voxels = similar codes)
 * - O(1) position encoding/decoding
 * - Enables fast AABB queries via bit masking
 */
struct MortonKey {
    uint64_t code = 0;

    // Decode position from Morton code
    glm::ivec3 toGridPos() const;
    glm::vec3 toWorldPos() const {
        glm::ivec3 grid = toGridPos();
        return glm::vec3(grid);
    }

    // Static factory
    static MortonKey fromPosition(const glm::vec3& pos);
    static MortonKey fromPosition(const glm::ivec3& pos);
};

// ============================================================================
// Attribute Components (Split vec3 into floats for SoA optimization)
// ============================================================================

/**
 * Voxel density/opacity (key attribute).
 * Range: [0.0, 1.0] where 0=empty, 1=solid
 * 1 float = 4 bytes
 */
struct Density {
    float value = 1.0f;
    bool isSolid() const { return value > 0.0f; }
};

// Color components (split RGB for SoA)
// Stored separately for SIMD-friendly iteration
struct Color_R { float value = 1.0f; };
struct Color_G { float value = 1.0f; };
struct Color_B { float value = 1.0f; };

// Normal components (split XYZ for SoA)
struct Normal_X { float value = 0.0f; };
struct Normal_Y { float value = 1.0f; };  // Default: +Y up
struct Normal_Z { float value = 0.0f; };

// ============================================================================
// Optional Extended Attributes
// ============================================================================

/**
 * Material ID for multi-material voxel grids.
 */
struct Material {
    uint32_t id = 0;
};

// Emission components (split RGBI for SoA)
struct Emission_R { float value = 0.0f; };
struct Emission_G { float value = 0.0f; };
struct Emission_B { float value = 0.0f; };
struct Emission_Intensity { float value = 0.0f; };

// ============================================================================
// Chunk/Brick Metadata (Optional)
// ============================================================================

/**
 * Brick reference - links voxel to AttributeRegistry brick.
 * Only added if voxel is part of a brick-based structure.
 */
struct BrickReference {
    uint32_t brickID = 0xFFFFFFFF;
    uint8_t localX = 0;
    uint8_t localY = 0;
    uint8_t localZ = 0;
};

/**
 * Chunk ID - groups voxels into spatial regions.
 */
struct ChunkID {
    uint32_t id = 0;
};

} // namespace GaiaVoxel
