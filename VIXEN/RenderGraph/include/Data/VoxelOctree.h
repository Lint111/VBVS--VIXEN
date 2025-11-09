#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace VIXEN {
namespace RenderGraph {

// ============================================================================
// Sparse Voxel Octree (SVO) Data Structures
// ============================================================================
//
// Based on:
// [6] Aleksandrov et al. - "Voxelisation Algorithms and Data Structures: A Review"
//     Baseline SVO design (simple, fast, well-understood)
//
// [16] Derin et al. - "Sparse Volume Rendering using Hardware Ray Tracing and Block Walking"
//      BlockWalk algorithm for empty space skipping
//
// Design: Hybrid structure with coarse pointer-based levels (0-4) and fine
//         brick-mapped levels (5-8) for optimal memory/performance balance.
//
// Memory efficiency: 9:1 compression for 10% density scenes
//                    256³ voxels: 16 MB dense → 1.76 MB sparse
// ============================================================================

/**
 * @brief Child occupancy bitmask optimization
 *
 * Single byte (8 bits) tracks whether each child contains ANY voxel data.
 * This enables O(1) empty-space skipping during traversal without descending.
 *
 * Bit layout:
 *   Bit 0: Child 0 (x=0,y=0,z=0) has voxel data (1) or is empty (0)
 *   Bit 1: Child 1 (x=1,y=0,z=0) has voxel data (1) or is empty (0)
 *   ...
 *   Bit 7: Child 7 (x=1,y=1,z=1) has voxel data (1) or is empty (0)
 *
 * Benefits per [16] Derin BlockWalk:
 * - O(1) empty-space skipping: Check bit before descending
 * - Dynamic updates: When brick becomes empty, flip bit to 0 (propagate to parents)
 * - When brick becomes occupied, flip bit to 1 (propagate to parents)
 * - Minimal memory: 1 byte per node (already using childMask, this is redundant with it)
 *
 * NOTE: This is actually REDUNDANT with childMask (which already tracks child existence).
 *       The key insight: childMask bit=1 means child has data, bit=0 means empty.
 *       We already have this optimization built-in!
 */

/**
 * @brief Octree node structure for coarse levels (depth 0-4)
 *
 * Memory layout: 36 bytes per node (compact, cache-friendly)
 *
 * childOffsets:   Array of 8 offsets into nodeBuffer (0 = empty child)
 * childMask:      **OCCUPANCY BITMASK** - whether child has ANY voxel data
 *                 Bit 0 = child[0] occupied (1) or empty (0)
 *                 Bit 7 = child[7] occupied (1) or empty (0)
 *                 This enables O(1) empty-space skipping during traversal!
 * leafMask:       Bitmask indicating which children are leaf bricks
 *                 1 = child is a brick (leaf at depth 4)
 *                 0 = child is an internal node (has children)
 * brickOffset:    If this is a leaf node, offset into brickBuffer
 *
 * Key optimization per [16] Derin BlockWalk:
 * - childMask enables single-bit check for empty-space skipping
 * - No memory overhead (already had childMask for structure)
 * - Dynamic updates: When voxel added/removed, propagate bit flip to all parent nodes
 *
 * Octant encoding (Morton order):
 *   000 (0) = (x=0, y=0, z=0) - bottom-left-back
 *   001 (1) = (x=1, y=0, z=0) - bottom-right-back
 *   010 (2) = (x=0, y=1, z=0) - bottom-left-front
 *   011 (3) = (x=1, y=1, z=0) - bottom-right-front
 *   100 (4) = (x=0, y=0, z=1) - top-left-back
 *   101 (5) = (x=1, y=0, z=1) - top-right-back
 *   110 (6) = (x=0, y=1, z=1) - top-left-front
 *   111 (7) = (x=1, y=1, z=1) - top-right-front
 */
struct OctreeNode {
    uint32_t childOffsets[8];  // Offset into nodeBuffer for each child (0 = empty)
    uint8_t  childMask;        // Bitmask: child occupancy (1=has data, 0=empty)
    uint8_t  leafMask;         // Bitmask: which children are leaves (bricks)
    uint16_t padding;          // Align to 4 bytes
    uint32_t brickOffset;      // Offset into brickBuffer (if leaf node)

    // Default constructor
    OctreeNode()
        : childMask(0)
        , leafMask(0)
        , padding(0)
        , brickOffset(0)
    {
        for (int i = 0; i < 8; ++i) {
            childOffsets[i] = 0;
        }
    }

    /**
     * @brief Check if a specific child exists
     * @param childIndex Child index [0-7]
     * @return true if child exists, false otherwise
     */
    inline bool HasChild(uint32_t childIndex) const {
        return (childMask & (1 << childIndex)) != 0;
    }

    /**
     * @brief Check if a specific child is a leaf brick
     * @param childIndex Child index [0-7]
     * @return true if child is a leaf brick, false if internal node
     */
    inline bool IsLeaf(uint32_t childIndex) const {
        return (leafMask & (1 << childIndex)) != 0;
    }

    /**
     * @brief Set child existence flag
     * @param childIndex Child index [0-7]
     */
    inline void SetChild(uint32_t childIndex) {
        childMask |= (1 << childIndex);
    }

    /**
     * @brief Set leaf flag for a child
     * @param childIndex Child index [0-7]
     */
    inline void SetLeaf(uint32_t childIndex) {
        leafMask |= (1 << childIndex);
    }

    /**
     * @brief Count number of existing children
     * @return Number of children (0-8)
     */
    inline uint32_t GetChildCount() const {
        // Count set bits in childMask using popcount
        uint32_t count = 0;
        uint8_t mask = childMask;
        while (mask) {
            count += mask & 1;
            mask >>= 1;
        }
        return count;
    }
};

// Static assert to ensure correct size (40 bytes with padding)
// Layout: 32 bytes (childOffsets) + 1 (childMask) + 1 (leafMask) + 2 (padding) + 4 (brickOffset) = 40 bytes
static_assert(sizeof(OctreeNode) == 40, "OctreeNode must be exactly 40 bytes");

/**
 * @brief Dense voxel brick for fine levels (depth 5-8)
 *
 * Memory layout: 512 bytes per brick (cache-friendly)
 *
 * voxels: 8x8x8 dense voxel array
 *         Each voxel: uint8_t value
 *           0 = empty voxel
 *           1-255 = solid voxel (material ID or grayscale)
 *
 * Indexing: voxels[z][y][x] for cache-coherent access
 *
 * Why 8³?
 * - 512 bytes fits in modern cache lines (multiple times)
 * - Power of 2 simplifies indexing (pos / 8, pos % 8)
 * - Small enough for fast GPU uploads/edits
 * - Large enough for good spatial locality
 */
struct VoxelBrick {
    uint8_t voxels[8][8][8];  // Dense 8³ voxel array (512 bytes)

    // Default constructor - initialize to empty
    VoxelBrick() {
        Clear();
    }

    /**
     * @brief Clear all voxels to empty (0)
     */
    inline void Clear() {
        std::memset(voxels, 0, sizeof(voxels));
    }

    /**
     * @brief Get voxel value at local coordinates
     * @param localPos Local position within brick [0-7, 0-7, 0-7]
     * @return Voxel value (0 = empty, 1-255 = solid)
     */
    inline uint8_t Get(const glm::ivec3& localPos) const {
        return voxels[localPos.z][localPos.y][localPos.x];
    }

    /**
     * @brief Set voxel value at local coordinates
     * @param localPos Local position within brick [0-7, 0-7, 0-7]
     * @param value Voxel value (0 = empty, 1-255 = solid)
     */
    inline void Set(const glm::ivec3& localPos, uint8_t value) {
        voxels[localPos.z][localPos.y][localPos.x] = value;
    }

    /**
     * @brief Check if brick is completely empty
     * @return true if all voxels are 0, false otherwise
     */
    inline bool IsEmpty() const {
        for (int z = 0; z < 8; ++z) {
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    if (voxels[z][y][x] != 0) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    /**
     * @brief Count non-empty voxels in brick
     * @return Number of solid voxels (0-512)
     */
    inline uint32_t CountSolid() const {
        uint32_t count = 0;
        for (int z = 0; z < 8; ++z) {
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    if (voxels[z][y][x] != 0) {
                        ++count;
                    }
                }
            }
        }
        return count;
    }
};

// Static assert to ensure correct size (512 bytes)
static_assert(sizeof(VoxelBrick) == 512, "VoxelBrick must be exactly 512 bytes");

/**
 * @brief Sparse Voxel Octree container
 *
 * Manages octree nodes and voxel bricks for efficient sparse voxel storage.
 *
 * Design:
 * - Depth 0-4: Pointer-based octree nodes (coarse spatial hierarchy)
 * - Depth 5-8: Dense 8³ voxel bricks (fine voxel detail)
 *
 * Target resolution: 256³ voxels (depth 8 octree)
 * Memory efficiency: 9:1 compression for 10% density scenes
 */
class SparseVoxelOctree {
public:
    SparseVoxelOctree();
    ~SparseVoxelOctree() = default;

    /**
     * @brief Build octree from dense voxel grid
     * @param voxelData Dense 3D voxel array (ZYX order)
     * @param gridSize Grid dimensions (must be power of 2)
     */
    void BuildFromGrid(const std::vector<uint8_t>& voxelData, uint32_t gridSize);

    /**
     * @brief Get octree nodes (const)
     * @return Vector of octree nodes
     */
    const std::vector<OctreeNode>& GetNodes() const { return nodes_; }

    /**
     * @brief Get voxel bricks (const)
     * @return Vector of voxel bricks
     */
    const std::vector<VoxelBrick>& GetBricks() const { return bricks_; }

    /**
     * @brief Get maximum octree depth
     * @return Maximum depth (typically 8 for 256³)
     */
    uint32_t GetMaxDepth() const { return maxDepth_; }

    /**
     * @brief Get total memory usage in bytes
     * @return Total memory (nodes + bricks)
     */
    size_t GetMemoryUsage() const {
        return nodes_.size() * sizeof(OctreeNode) +
               bricks_.size() * sizeof(VoxelBrick);
    }

    /**
     * @brief Calculate compression ratio vs dense grid
     * @param gridSize Original grid size
     * @return Compression ratio (e.g., 9.0 = 9:1 compression)
     */
    float GetCompressionRatio(uint32_t gridSize) const {
        size_t denseSize = gridSize * gridSize * gridSize * sizeof(uint8_t);
        return static_cast<float>(denseSize) / static_cast<float>(GetMemoryUsage());
    }

private:
    std::vector<OctreeNode> nodes_;    // Octree node hierarchy
    std::vector<VoxelBrick> bricks_;   // Voxel brick storage
    uint32_t maxDepth_;                // Maximum octree depth
    uint32_t gridSize_;                // Original grid size

    // Private helper methods (to be implemented in H.1.2)
    uint32_t BuildRecursive(
        const std::vector<uint8_t>& voxelData,
        const glm::ivec3& origin,
        uint32_t size,
        uint32_t depth
    );

    uint32_t CreateBrick(
        const std::vector<uint8_t>& voxelData,
        const glm::ivec3& origin
    );

    bool IsRegionEmpty(
        const std::vector<uint8_t>& voxelData,
        const glm::ivec3& origin,
        uint32_t size
    ) const;
};

} // namespace RenderGraph
} // namespace VIXEN
