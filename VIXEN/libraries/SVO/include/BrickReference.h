#pragma once

#include <cstdint>

namespace Vixen::SVO {

/**
 * Type-erased brick reference stored in octree leaf nodes.
 *
 * The octree doesn't know or care about brick contents - it only stores:
 * - Brick ID (which brick in the brick pool)
 * - Brick depth (n where brick size = 2^n)
 *
 * The brick storage system manages the actual data layout.
 * This allows the same octree structure to work with:
 * - DefaultLeafData (density + material)
 * - ColorOnlyBrick (RGB)
 * - SDFBrick (signed distance)
 * - PBRBrick (full physically-based data)
 * - Any custom layout
 */
struct BrickReference {
    uint32_t brickID    : 28;  // Which brick (268M max)
    uint32_t brickDepth : 4;   // Brick depth 1-10 (2³-1024³ voxels)

    BrickReference() : brickID(0), brickDepth(0) {}
    BrickReference(uint32_t id, uint32_t depth) : brickID(id), brickDepth(depth) {}

    bool isValid() const { return brickDepth > 0; }
    int getSideLength() const { return 1 << brickDepth; }
    int getVoxelCount() const {
        int n = getSideLength();
        return n * n * n;
    }
};

static_assert(sizeof(BrickReference) == 4, "BrickReference must be 32 bits");

/**
 * Extended brick reference with GPU buffer offsets.
 * Used when packing bricks into GPU-accessible buffers.
 *
 * The octree stores these references, and the render graph uses the offsets
 * to bind the correct buffer regions in shaders.
 */
struct GPUBrickReference {
    BrickReference brick;          // Base brick info (32 bits)

    // Per-array buffer offsets (in elements, not bytes)
    // Shader knows array types, octree doesn't need to
    uint32_t arrayOffsets[8];      // Up to 8 arrays per brick layout

    GPUBrickReference() : brick(), arrayOffsets{} {}

    explicit GPUBrickReference(const BrickReference& ref)
        : brick(ref), arrayOffsets{} {}
};

} // namespace Vixen::SVO
