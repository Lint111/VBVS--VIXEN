#include "Data/VoxelOctree.h"
#include <cassert>
#include <cstring>

namespace VIXEN {
namespace RenderGraph {

SparseVoxelOctree::SparseVoxelOctree()
    : maxDepth_(0)
    , gridSize_(0)
{
    // Reserve root node
    nodes_.reserve(4096); // Reasonable initial capacity for depth 0-4
    bricks_.reserve(1024); // Reasonable initial capacity for bricks
}

void SparseVoxelOctree::BuildFromGrid(
    const std::vector<uint8_t>& voxelData,
    uint32_t gridSize)
{
    // Validate input
    assert((gridSize & (gridSize - 1)) == 0 && "Grid size must be power of 2");
    assert(voxelData.size() == gridSize * gridSize * gridSize && "Voxel data size mismatch");

    // Clear existing data
    nodes_.clear();
    bricks_.clear();

    gridSize_ = gridSize;

    // Calculate maximum depth (log2 of grid size)
    maxDepth_ = 0;
    uint32_t temp = gridSize;
    while (temp > 1) {
        temp >>= 1;
        ++maxDepth_;
    }

    // Build octree recursively starting from root
    BuildRecursive(voxelData, glm::ivec3(0, 0, 0), gridSize, 0);
}

uint32_t SparseVoxelOctree::BuildRecursive(
    const std::vector<uint8_t>& voxelData,
    const glm::ivec3& origin,
    uint32_t size,
    uint32_t depth)
{
    // Check if this region is completely empty (early out)
    if (IsRegionEmpty(voxelData, origin, size)) {
        return 0; // Return 0 to indicate no node created
    }

    // If we've reached brick level (depth 4) or minimum size (8³), create a brick
    if (depth >= 4 || size <= 8) {
        return CreateBrick(voxelData, origin);
    }

    // Otherwise, create an internal octree node
    uint32_t nodeIndex = static_cast<uint32_t>(nodes_.size());
    nodes_.emplace_back(); // Add new node
    OctreeNode& node = nodes_[nodeIndex];

    uint32_t childSize = size / 2;

    // Recursively build 8 children
    for (uint32_t childIdx = 0; childIdx < 8; ++childIdx) {
        // Calculate child octant offset
        // Octant encoding: bit 0 = x, bit 1 = y, bit 2 = z
        glm::ivec3 childOrigin = origin;
        childOrigin.x += (childIdx & 1) ? childSize : 0;
        childOrigin.y += (childIdx & 2) ? childSize : 0;
        childOrigin.z += (childIdx & 4) ? childSize : 0;

        // Build child subtree
        uint32_t childOffset = BuildRecursive(voxelData, childOrigin, childSize, depth + 1);

        // If child exists (non-zero offset), record it
        if (childOffset != 0) {
            node.childOffsets[childIdx] = childOffset;
            node.SetChild(childIdx);  // Set occupancy bit (child has voxel data)

            // If we're at depth 3 (next level is depth 4 = bricks), mark as leaf
            if (depth == 3) {
                node.SetLeaf(childIdx);
                node.brickOffset = childOffset; // Store brick offset
            }
        }
        // If childOffset == 0, child is empty (childMask bit stays 0)
    }

    return nodeIndex;
}

uint32_t SparseVoxelOctree::CreateBrick(
    const std::vector<uint8_t>& voxelData,
    const glm::ivec3& origin)
{
    // Allocate new brick
    uint32_t brickIndex = static_cast<uint32_t>(bricks_.size());
    bricks_.emplace_back();
    VoxelBrick& brick = bricks_[brickIndex];

    // Copy voxel data into brick (8³ region)
    for (int z = 0; z < 8; ++z) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                glm::ivec3 globalPos = origin + glm::ivec3(x, y, z);

                // Bounds check
                if (globalPos.x >= static_cast<int>(gridSize_) ||
                    globalPos.y >= static_cast<int>(gridSize_) ||
                    globalPos.z >= static_cast<int>(gridSize_))
                {
                    brick.voxels[z][y][x] = 0; // Out of bounds = empty
                    continue;
                }

                // Calculate index into flat voxel array (ZYX order)
                uint32_t index = globalPos.z * gridSize_ * gridSize_ +
                                 globalPos.y * gridSize_ +
                                 globalPos.x;

                brick.voxels[z][y][x] = voxelData[index];
            }
        }
    }

    // Return brick index (stored as offset in node)
    // NOTE: We add nodes_.size() to distinguish brick offsets from node offsets
    //       This will be handled during GPU linearization in H.3.2
    return brickIndex;
}

bool SparseVoxelOctree::IsRegionEmpty(
    const std::vector<uint8_t>& voxelData,
    const glm::ivec3& origin,
    uint32_t size) const
{
    // Quick scan through region to check for any non-zero voxels
    for (uint32_t z = 0; z < size; ++z) {
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                glm::ivec3 globalPos = origin + glm::ivec3(x, y, z);

                // Bounds check
                if (globalPos.x >= static_cast<int>(gridSize_) ||
                    globalPos.y >= static_cast<int>(gridSize_) ||
                    globalPos.z >= static_cast<int>(gridSize_))
                {
                    continue; // Out of bounds = treat as empty
                }

                // Calculate index into flat voxel array (ZYX order)
                uint32_t index = globalPos.z * gridSize_ * gridSize_ +
                                 globalPos.y * gridSize_ +
                                 globalPos.x;

                if (voxelData[index] != 0) {
                    return false; // Found non-empty voxel
                }
            }
        }
    }

    return true; // All voxels are empty
}

// NOTE: PopulateChildMetadata removed - childMask already provides occupancy tracking
//       When voxel added: propagate bit=1 to all parent nodes
//       When voxel removed: propagate bit=0 to all parent nodes (if child becomes empty)

} // namespace RenderGraph
} // namespace VIXEN
