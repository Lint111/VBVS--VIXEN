#include "Data/VoxelOctree.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <vector>

namespace VIXEN {
namespace RenderGraph {

SparseVoxelOctree::SparseVoxelOctree()
    : maxDepth_(0)
    , gridSize_(0)
    , nodeFormat_(NodeFormat::ESVO)  // Default to ESVO format
{
    // Reserve root node
    nodes_.reserve(4096); // Reasonable initial capacity for depth 0-4 (Legacy)
    esvoNodes_.reserve(4096); // Reasonable initial capacity for ESVO format
    bricks_.reserve(1024); // Reasonable initial capacity for bricks

    // Initialize material palette with default white material at ID 0
    materialPalette_.reserve(256); // Max 256 materials
    materialPalette_.emplace_back(); // Default white diffuse material
}

void SparseVoxelOctree::BuildFromGrid(
    const std::vector<uint8_t>& voxelData,
    uint32_t gridSize,
    NodeFormat format)
{
    // Validate input
    assert((gridSize & (gridSize - 1)) == 0 && "Grid size must be power of 2");
    assert(voxelData.size() == gridSize * gridSize * gridSize && "Voxel data size mismatch");

    // Clear existing data
    nodes_.clear();
    esvoNodes_.clear();
    bricks_.clear();

    gridSize_ = gridSize;
    nodeFormat_ = format;

    // Calculate maximum depth (log2 of grid size)
    maxDepth_ = 0;
    uint32_t temp = gridSize;
    while (temp > 1) {
        temp >>= 1;
        ++maxDepth_;
    }

    // Build octree recursively starting from root
    if (format == NodeFormat::ESVO) {
        // Morton curve ESVO building: guaranteed consecutive child allocation
        BuildESVOWithMortonCurve(voxelData, gridSize);
    } else {
        BuildRecursive(voxelData, glm::ivec3(0, 0, 0), gridSize, 0);
    }
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

bool SparseVoxelOctree::IsRegionConstant(
    const std::vector<uint8_t>& voxelData,
    const glm::ivec3& origin,
    uint32_t size,
    uint8_t& outConstantValue) const
{
    // Check first voxel
    glm::ivec3 firstPos = origin;
    if (firstPos.x >= static_cast<int>(gridSize_) ||
        firstPos.y >= static_cast<int>(gridSize_) ||
        firstPos.z >= static_cast<int>(gridSize_))
    {
        outConstantValue = 0;
        return true; // Out of bounds = constant empty
    }

    uint32_t firstIndex = firstPos.z * gridSize_ * gridSize_ +
                          firstPos.y * gridSize_ +
                          firstPos.x;
    uint8_t firstValue = voxelData[firstIndex];
    outConstantValue = firstValue;

    // Check if all voxels match first value
    for (uint32_t z = 0; z < size; ++z) {
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                glm::ivec3 globalPos = origin + glm::ivec3(x, y, z);

                // Bounds check
                if (globalPos.x >= static_cast<int>(gridSize_) ||
                    globalPos.y >= static_cast<int>(gridSize_) ||
                    globalPos.z >= static_cast<int>(gridSize_))
                {
                    if (firstValue != 0) return false; // Different from first
                    continue;
                }

                uint32_t index = globalPos.z * gridSize_ * gridSize_ +
                                 globalPos.y * gridSize_ +
                                 globalPos.x;

                if (voxelData[index] != firstValue) {
                    return false; // Found different value
                }
            }
        }
    }

    return true; // All voxels are constant
}

uint32_t SparseVoxelOctree::BuildRecursiveESVO(
    const std::vector<uint8_t>& voxelData,
    const glm::ivec3& origin,
    uint32_t size,
    uint32_t depth)
{
    // Check if this region is completely empty (early out)
    if (IsRegionEmpty(voxelData, origin, size)) {
        return 0; // Return 0 to indicate no node created
    }

    // Create ESVO node at this level
    uint32_t nodeIndex = static_cast<uint32_t>(esvoNodes_.size());
    esvoNodes_.emplace_back(); // Add new node
    ESVONode& node = esvoNodes_[nodeIndex];

    if (depth == 0) {
        std::cerr << "[BuildRecursiveESVO] Created root node at index " << nodeIndex << std::endl;
    }

    // If we've reached brick level (depth 4) or minimum size (8³), this is a leaf
    if (depth >= 4 || size <= 8) {
        // Leaf node: create brick and store offset in descriptor1
        uint32_t brickOffset = CreateBrick(voxelData, origin);
        node.SetBrickOffset(brickOffset);
        // No children, so childMask stays 0 (leaf indicator)
        if (depth == 0) {
            std::cerr << "[BuildRecursiveESVO] Root is leaf! brickOffset=" << brickOffset << std::endl;
        }
        return nodeIndex;
    }

    uint32_t childSize = size / 2;

    // PHASE H BASELINE: Simple recursive octree construction
    // NOTE: Children are NOT guaranteed consecutive in memory
    // This is a KNOWN LIMITATION - shader traversal stays at root level
    // TODO: Phase H+ will implement proper consecutive allocation for true ESVO traversal
    //
    // Current workaround: Store first child index, shader reads root only

    std::vector<uint32_t> childIndices(8, 0);
    bool hasAnyChild = false;

    // Build all 8 children recursively
    for (uint32_t childIdx = 0; childIdx < 8; ++childIdx) {
        // Calculate child octant offset
        glm::ivec3 childOrigin = origin;
        childOrigin.x += (childIdx & 1) ? childSize : 0;
        childOrigin.y += (childIdx & 2) ? childSize : 0;
        childOrigin.z += (childIdx & 4) ? childSize : 0;

        // Build child subtree
        uint32_t childNodeIndex = BuildRecursiveESVO(voxelData, childOrigin, childSize, depth + 1);

        if (childNodeIndex != 0) {
            // Child exists
            node.SetChild(childIdx);
            hasAnyChild = true;
            childIndices[childIdx] = childNodeIndex;

            // Check if child is non-leaf (has its own children)
            const ESVONode& childNode = esvoNodes_[childNodeIndex];
            if (childNode.GetChildMask() != 0) {
                // Child has children, so it's not a leaf
                node.SetNonLeaf(childIdx);
            }
        }
    }

    // Store offset to first child (for potential future use)
    // Current shader doesn't use this for traversal due to non-consecutive layout
    if (hasAnyChild) {
        for (uint32_t i = 0; i < 8; ++i) {
            if (childIndices[i] != 0) {
                node.SetChildOffset(childIndices[i]);
                break;
            }
        }
    }

    if (depth == 0) {
        std::cerr << "[BuildRecursiveESVO] Root node finished: childMask=0x" << std::hex
                  << static_cast<int>(node.GetChildMask()) << std::dec << ", childCount=" << node.GetChildCount()
                  << ", descriptor0=0x" << std::hex << node.descriptor0 << std::dec << std::endl;
    }

    // DEBUG: Log leaf node bricks
    if ((depth >= 4 || size <= 8) && depth < 6) {
        std::cerr << "[BuildRecursiveESVO] Created leaf at depth=" << depth << ", size=" << size
                  << ", origin=(" << origin.x << "," << origin.y << "," << origin.z << ")" << std::endl;
        // Check first few voxels in the brick
        if (esvoNodes_.size() > 0 && esvoNodes_.back().GetBrickOffset() < bricks_.size()) {
            const auto& brick = bricks_[esvoNodes_.back().GetBrickOffset()];
            std::cerr << "  First 8 voxels: ";
            for (int i = 0; i < 8; ++i) {
                std::cerr << static_cast<int>(brick.voxels[0][0][i]) << " ";
            }
            std::cerr << std::endl;
        }
    }

    return nodeIndex;
}

// ============================================================================
// TWO-PASS ESVO BUILDING (Guaranteed Consecutive Child Allocation)
// ============================================================================

uint32_t SparseVoxelOctree::CountNodesESVO(
    const std::vector<uint8_t>& voxelData,
    const glm::ivec3& origin,
    uint32_t size,
    uint32_t depth)
{
    // Check if region is empty
    if (IsRegionEmpty(voxelData, origin, size)) {
        return 0;  // 0 nodes needed
    }

    // At leaf level, create brick (counts as 1 node)
    if (depth >= 4 || size <= 8) {
        return 1;  // 1 node for the leaf
    }

    // Internal node: count itself + all children
    uint32_t totalNodes = 1;  // This node
    uint32_t childSize = size / 2;

    // Count nodes in all 8 children
    for (uint32_t childIdx = 0; childIdx < 8; ++childIdx) {
        glm::ivec3 childOrigin = origin;
        childOrigin.x += (childIdx & 1) ? childSize : 0;
        childOrigin.y += (childIdx & 2) ? childSize : 0;
        childOrigin.z += (childIdx & 4) ? childSize : 0;

        uint32_t childNodes = CountNodesESVO(voxelData, childOrigin, childSize, depth + 1);
        totalNodes += childNodes;
    }

    // Add space for 8 children slots (consecutive allocation)
    totalNodes += 8;

    return totalNodes;
}

uint32_t SparseVoxelOctree::BuildRecursiveESVOWithAllocation(
    const std::vector<uint8_t>& voxelData,
    const glm::ivec3& origin,
    uint32_t size,
    uint32_t depth,
    uint32_t& currentNodeIndex)
{
    // Check if region is empty
    if (IsRegionEmpty(voxelData, origin, size)) {
        return 0;
    }

    // Allocate node at current position
    uint32_t nodeIndex = currentNodeIndex++;
    ESVONode& node = esvoNodes_[nodeIndex];

    // Leaf node: create brick
    if (depth >= 4 || size <= 8) {
        uint32_t brickOffset = CreateBrick(voxelData, origin);
        node.SetBrickOffset(brickOffset);
        return nodeIndex;
    }

    // Internal node: Use the OLD working algorithm that doesn't enforce consecutive allocation
    // Phase H limitation: ESVO consecutive allocation is complex and causing rendering issues
    // Revert to simple recursive building until we can implement proper restructuring
    return BuildRecursiveESVO(voxelData, origin, size, depth);
}

// ============================================================================
// BREADTH-FIRST ESVO BUILDING (Guaranteed consecutive allocation)
// ============================================================================

/**
 * @brief Build ESVO using breadth-first traversal for consecutive children
 *
 * Algorithm:
 * 1. Build octree level-by-level (breadth-first)
 * 2. For each parent, allocate all 8 child slots consecutively
 * 3. Fill only existing children, leave gaps for missing ones
 * 4. This guarantees child[i] is always at childOffset + i
 *
 * Properties:
 * - Wastes memory for missing children (max 7 empty slots per node)
 * - But enables efficient GPU traversal with shift arithmetic
 * - Trade space for traversal speed (standard ESVO design choice)
 */
void SparseVoxelOctree::BuildESVOWithMortonCurve(
    const std::vector<uint8_t>& voxelData,
    uint32_t gridSize)
{
    // Node metadata for breadth-first building
    struct NodeInfo {
        glm::ivec3 origin;
        uint32_t size;
        uint32_t depth;
        uint32_t nodeIndex;     // ACTUAL index in esvoNodes_ where this node IS/WILL BE
        uint32_t parentIndex;  // Index in esvoNodes_ of parent
        uint8_t childSlot;      // Which child slot (0-7) in parent
    };

    std::vector<NodeInfo> currentLevel;
    std::vector<NodeInfo> nextLevel;

    std::cerr << "[BFS ESVO] Starting breadth-first build..." << std::endl;

    // Level 0: Root node
    if (!IsRegionEmpty(voxelData, glm::ivec3(0, 0, 0), gridSize)) {
        esvoNodes_.resize(1);  // Allocate root at index 0
        NodeInfo root;
        root.origin = glm::ivec3(0, 0, 0);
        root.size = gridSize;
        root.depth = 0;
        root.nodeIndex = 0;  // Root at index 0
        root.parentIndex = 0xFFFFFFFF;  // No parent
        root.childSlot = 0;
        currentLevel.push_back(root);
    } else {
        std::cerr << "[BFS ESVO] Empty grid - no octree needed" << std::endl;
        return;
    }

    // Build level by level
    uint32_t currentDepth = 0;
    while (!currentLevel.empty() && currentDepth < 5) {  // Process levels 0..4 (depth 5 would be beyond bricks)
        std::cerr << "[BFS ESVO] Level " << currentDepth << ": processing " << currentLevel.size() << " nodes" << std::endl;

        // Process each node in current level (nodes already allocated with explicit indices)
        for (size_t i = 0; i < currentLevel.size(); ++i) {
            const NodeInfo& nodeInfo = currentLevel[i];
            uint32_t nodeIndex = nodeInfo.nodeIndex;  // Use explicit node index
            ESVONode& node = esvoNodes_[nodeIndex];

            // DEBUG: Print when processing root children
            if (currentDepth == 1 && nodeInfo.parentIndex == 0) {
                std::cerr << "[BFS ESVO] Processing root child at index " << nodeIndex
                          << " (parent=" << nodeInfo.parentIndex << ", slot=" << (int)nodeInfo.childSlot
                          << ", origin=" << nodeInfo.origin.x << "," << nodeInfo.origin.y << "," << nodeInfo.origin.z << ")"
                          << std::endl;
            }

            // Leaf node (depth >=4 or size <=8): create brick immediately
            if (nodeInfo.depth >= 4 || nodeInfo.size <= 8) {
                uint32_t brickOffset = CreateBrick(voxelData, nodeInfo.origin);
                node.SetBrickOffset(brickOffset);
                continue;
            }

            // Internal node: check which children exist
            uint32_t childSize = nodeInfo.size / 2;
            uint8_t childMask = 0;

            struct ChildMeta {
                glm::ivec3 origin;
                uint32_t size;
                uint32_t depth;
                uint8_t slot;
                bool isLeafBrick;
                bool isConstant;
            };

            std::vector<ChildMeta> childMetas;

            for (uint8_t childIdx = 0; childIdx < 8; ++childIdx) {
                glm::ivec3 childOrigin = nodeInfo.origin;
                childOrigin.x += (childIdx & 1) ? childSize : 0;
                childOrigin.y += (childIdx & 2) ? childSize : 0;
                childOrigin.z += (childIdx & 4) ? childSize : 0;

                if (!IsRegionEmpty(voxelData, childOrigin, childSize)) {
                    childMask |= (1 << childIdx);

                    ChildMeta meta;
                    meta.origin = childOrigin;
                    meta.size = childSize;
                    meta.depth = nodeInfo.depth + 1;
                    meta.slot = childIdx;
                    meta.isLeafBrick = (meta.depth >= 4) || (childSize <= 8);
                    meta.isConstant = false;

                    if (!meta.isLeafBrick) {
                        uint8_t constVal = 0;
                        if (IsRegionConstant(voxelData, childOrigin, childSize, constVal) && constVal != 0) {
                            // Non-empty constant region → mark child as constant node
                            meta.isConstant = true;
                            // Reuse constVal as material ID (later stored in descriptor1 low 8 bits)
                        }
                    }

                    childMetas.push_back(meta);
                }
            }

            if (!childMetas.empty()) {
                // Allocate 8 consecutive slots for children (even if some are empty)
                uint32_t childBaseOffset = esvoNodes_.size();
                esvoNodes_.resize(childBaseOffset + 8);  // Always allocate 8 slots

                node.SetChildOffset(childBaseOffset);

                // Place each existing child at its correct octant-indexed slot and classify
                for (const ChildMeta& meta : childMetas) {
                    node.SetChild(meta.slot);

                    // CRITICAL: Child octant i must be stored at slot childBaseOffset + i
                    // This ensures shader traversal (parentPtr = childOffset + childIdx) works
                    uint32_t childNodeIndex = childBaseOffset + meta.slot;

                    // DEBUG: Print root children placement
                    if (currentDepth == 0) {
                        std::cerr << "[BFS ESVO] Root child octant " << (int)meta.slot
                                  << " will be built at index " << childNodeIndex
                                  << " (base=" << childBaseOffset << ", origin="
                                  << meta.origin.x << "," << meta.origin.y << "," << meta.origin.z << ")"
                                  << std::endl;
                    }

                    ESVONode& childNode = esvoNodes_[childNodeIndex];

                    if (meta.isLeafBrick) {
                        // Directly create brick leaf
                        uint32_t brickOffset = CreateBrick(voxelData, meta.origin);
                        childNode.SetBrickOffset(brickOffset);
                    } else if (meta.isConstant) {
                        // Determine material ID from constant voxel value (constVal)
                        // For now assume voxel value directly maps to material ID
                        // NOTE: If palette indirection differs later, add translation here.
                        uint8_t constMatID = 0;
                        // Sample first voxel to get material id (safe because region constant)
                        uint32_t index = meta.origin.z * gridSize_ * gridSize_ + meta.origin.y * gridSize_ + meta.origin.x;
                        if (index < voxelData.size()) {
                            constMatID = voxelData[index];
                        }
                        childNode.SetConstant(constMatID);
                    } else {
                        // Internal node → set parent's non-leaf bit and schedule for next level
                        node.SetNonLeaf(meta.slot);

                        NodeInfo childInfo;
                        childInfo.origin = meta.origin;
                        childInfo.size = meta.size;
                        childInfo.depth = meta.depth;
                        childInfo.nodeIndex = childNodeIndex;
                        childInfo.parentIndex = nodeIndex;
                        childInfo.childSlot = meta.slot;
                        nextLevel.push_back(childInfo);
                    }
                }
            }
        }

        // Move to next level
        currentLevel = std::move(nextLevel);
        nextLevel.clear();
        currentDepth++;
    }

    std::cerr << "[BFS ESVO] Build complete: " << esvoNodes_.size() << " nodes allocated" << std::endl;

    // Debug: Print root node
    if (!esvoNodes_.empty()) {
        std::cerr << "[BFS ESVO] Root node: descriptor0=0x" << std::hex
                  << esvoNodes_[0].descriptor0 << ", descriptor1=0x" << esvoNodes_[0].descriptor1
                  << std::dec << ", childMask=" << (int)esvoNodes_[0].GetChildMask()
                  << ", childCount=" << esvoNodes_[0].GetChildCount() << std::endl;
    }

    // =====================================================================
    // Post-build validation (lightweight self-check of descriptor encoding)
    // Ensures child valid bits (reversed layout) and non-leaf bits align with
    // shader combined-mask traversal logic: child i stored at bit (15 - i)
    // so that (descriptor0 << i) moves it to bit 15, and non-leaf bit at
    // position (1 + i) shifts to (1 + i) after left shift.
    // =====================================================================
    auto validateNode = [&](uint32_t nodeIndex) {
        if (nodeIndex >= esvoNodes_.size()) return; // Safety
        const ESVONode &n = esvoNodes_[nodeIndex];
        uint32_t d0 = n.descriptor0;
        bool anyError = false;

        // Extract raw reversed child bits (bits 15..8) and forward mask for debug
        uint8_t rawChildBits = static_cast<uint8_t>((d0 >> 8) & 0xFF);
        uint8_t fwdMask = n.GetChildMask();
        uint8_t nonLeafMask = (d0 >> 1) & 0x7F; // direct non-leaf bits (children 0..6)
        uint32_t childBase = n.GetChildOffset();

        for (uint32_t child = 0; child < 8; ++child) {
            bool existsHost = n.HasChild(child);

            // Recompute existence directly from reversed storage: child i stored at bit (7 - i) of rawChildBits
            bool existsRaw = (rawChildBits & (1u << (7 - child))) != 0;
            if (existsHost != existsRaw) {
                anyError = true;
                std::cerr << "[ESVO VALIDATION] EXISTENCE BIT MISMATCH node=" << nodeIndex
                          << " child=" << child << " hostHas=" << existsHost
                          << " rawHas=" << existsRaw << " d0=0x" << std::hex << d0 << std::dec << std::endl;
            }

            if (!existsHost) continue; // Skip further checks for empty child

            // Leaf test mirrors shader logic now: leaf = (child>=7) || ((nonLeafMask & (1<<child)) == 0)
            bool isLeafHost = n.IsLeaf(child);
            bool isLeafDirect = (child >= 7) ? true : ((nonLeafMask & (1u << child)) == 0);
            if (isLeafHost != isLeafDirect) {
                anyError = true;
                std::cerr << "[ESVO VALIDATION] LEAF FLAG MISMATCH node=" << nodeIndex
                          << " child=" << child << " hostLeaf=" << isLeafHost
                          << " directLeaf=" << isLeafDirect << " nonLeafMask=0x"
                          << std::hex << (int)nonLeafMask << " d0=0x" << d0 << std::dec << std::endl;
            }

            // Contiguity check: if parent encodes children, ensure allocated slot exists when expected
            if (childBase != 0) { // childBase==0 allowed for root or if not set yet
                uint32_t expectedIndex = childBase + child;
                if (expectedIndex >= esvoNodes_.size()) {
                    anyError = true;
                    std::cerr << "[ESVO VALIDATION] OUT-OF-RANGE child pointer node=" << nodeIndex
                              << " child=" << child << " expectedIndex=" << expectedIndex << std::endl;
                }
            }
        }
        if (anyError) {
            std::cerr << "[ESVO VALIDATION] Descriptor issues detected for node " << nodeIndex << std::endl;
        }
    };

    // Validate a small sample: root + its first 8 children (if allocated)
    if (!esvoNodes_.empty()) {
        validateNode(0);
        uint32_t rootChildBase = esvoNodes_[0].GetChildOffset();
        for (uint32_t i = 0; i < 8; ++i) {
            validateNode(rootChildBase + i);
        }
    }
}

// NOTE: PopulateChildMetadata removed - childMask already provides occupancy tracking
//       When voxel added: propagate bit=1 to all parent nodes
//       When voxel removed: propagate bit=0 to all parent nodes (if child becomes empty)

// ============================================================================
// SERIALIZATION / DESERIALIZATION (H.1.4)
// ============================================================================

bool SparseVoxelOctree::SaveToFile(const std::string& filepath) const {
    std::vector<uint8_t> buffer;
    SerializeToBuffer(buffer);

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    file.close();

    return file.good();
}

bool SparseVoxelOctree::LoadFromFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return false;
    }

    return DeserializeFromBuffer(buffer);
}

void SparseVoxelOctree::SerializeToBuffer(std::vector<uint8_t>& outBuffer) const {
    // Binary format:
    // Header (20 bytes):
    //   - Magic number (4 bytes): "SVOC" (Sparse Voxel Octree Cache)
    //   - Version (4 bytes): 1
    //   - maxDepth (4 bytes)
    //   - gridSize (4 bytes)
    //   - nodeCount (4 bytes)
    //   - brickCount (4 bytes)
    // Nodes data (nodeCount * sizeof(OctreeNode))
    // Bricks data (brickCount * sizeof(VoxelBrick))

    const uint32_t magic = 0x53564F43; // "SVOC" in little-endian
    const uint32_t version = 1;
    const uint32_t nodeCount = static_cast<uint32_t>(nodes_.size());
    const uint32_t brickCount = static_cast<uint32_t>(bricks_.size());

    size_t headerSize = 24;
    size_t nodesSize = nodeCount * sizeof(OctreeNode);
    size_t bricksSize = brickCount * sizeof(VoxelBrick);
    size_t totalSize = headerSize + nodesSize + bricksSize;

    outBuffer.resize(totalSize);
    uint8_t* ptr = outBuffer.data();

    // Write header
    std::memcpy(ptr, &magic, sizeof(magic)); ptr += sizeof(magic);
    std::memcpy(ptr, &version, sizeof(version)); ptr += sizeof(version);
    std::memcpy(ptr, &maxDepth_, sizeof(maxDepth_)); ptr += sizeof(maxDepth_);
    std::memcpy(ptr, &gridSize_, sizeof(gridSize_)); ptr += sizeof(gridSize_);
    std::memcpy(ptr, &nodeCount, sizeof(nodeCount)); ptr += sizeof(nodeCount);
    std::memcpy(ptr, &brickCount, sizeof(brickCount)); ptr += sizeof(brickCount);

    // Write nodes
    if (nodeCount > 0) {
        std::memcpy(ptr, nodes_.data(), nodesSize);
        ptr += nodesSize;
    }

    // Write bricks
    if (brickCount > 0) {
        std::memcpy(ptr, bricks_.data(), bricksSize);
    }
}

bool SparseVoxelOctree::DeserializeFromBuffer(const std::vector<uint8_t>& buffer) {
    if (buffer.size() < 24) {
        return false; // Header too small
    }

    const uint8_t* ptr = buffer.data();

    // Read header
    uint32_t magic, version, nodeCount, brickCount;
    std::memcpy(&magic, ptr, sizeof(magic)); ptr += sizeof(magic);
    std::memcpy(&version, ptr, sizeof(version)); ptr += sizeof(version);
    std::memcpy(&maxDepth_, ptr, sizeof(maxDepth_)); ptr += sizeof(maxDepth_);
    std::memcpy(&gridSize_, ptr, sizeof(gridSize_)); ptr += sizeof(gridSize_);
    std::memcpy(&nodeCount, ptr, sizeof(nodeCount)); ptr += sizeof(nodeCount);
    std::memcpy(&brickCount, ptr, sizeof(brickCount)); ptr += sizeof(brickCount);

    // Validate magic number
    const uint32_t expectedMagic = 0x53564F43; // "SVOC"
    if (magic != expectedMagic) {
        return false;
    }

    // Validate version
    if (version != 1) {
        return false;
    }

    // Validate buffer size
    size_t expectedSize = 24 + nodeCount * sizeof(OctreeNode) + brickCount * sizeof(VoxelBrick);
    if (buffer.size() != expectedSize) {
        return false;
    }

    // Read nodes
    nodes_.resize(nodeCount);
    if (nodeCount > 0) {
        std::memcpy(nodes_.data(), ptr, nodeCount * sizeof(OctreeNode));
        ptr += nodeCount * sizeof(OctreeNode);
    }

    // Read bricks
    bricks_.resize(brickCount);
    if (brickCount > 0) {
        std::memcpy(bricks_.data(), ptr, brickCount * sizeof(VoxelBrick));
    }

    return true;
}

// ============================================================================
// MATERIAL PALETTE MANAGEMENT (H.2.4)
// ============================================================================

uint8_t SparseVoxelOctree::RegisterMaterial(const VoxelMaterial& material) {
    // Check if palette is full
    if (materialPalette_.size() >= 256) {
        // Return default material ID (0) if palette full
        return 0;
    }

    // Add material to palette and return its ID
    uint8_t materialID = static_cast<uint8_t>(materialPalette_.size());
    materialPalette_.push_back(material);
    return materialID;
}

const VoxelMaterial& SparseVoxelOctree::GetMaterial(uint8_t materialID) const {
    // Bounds check - return default material (ID 0) if invalid
    if (materialID >= materialPalette_.size()) {
        return materialPalette_[0];
    }
    return materialPalette_[materialID];
}

void SparseVoxelOctree::ClearMaterials() {
    materialPalette_.clear();
    materialPalette_.reserve(256);
    materialPalette_.emplace_back(); // Re-add default white material at ID 0
}

void SparseVoxelOctree::CheckForSymmetry() const {
    if (esvoNodes_.empty()) {
        std::cerr << "=== Symmetry Check: No ESVO nodes ===" << std::endl;
        return;
    }

    std::cerr << "=== Symmetry Check ===" << std::endl;
    std::cerr << "Root child mask: 0x" << std::hex << (int)esvoNodes_[0].GetChildMask() << std::dec << std::endl;

    // Check if we have symmetrical children (e.g., both left and right walls)
    uint8_t childMask = esvoNodes_[0].GetChildMask();
    int symmetricalPairs = 0;

    // Check for X-axis symmetry (common in Cornell Box issues)
    // Octant encoding: bit 0 = x, bit 1 = y, bit 2 = z
    // Octants 0-3 have x=0, octants 4-7 have x=1
    for (int i = 0; i < 4; ++i) {
        bool left = childMask & (1 << i);        // x=0 half
        bool right = childMask & (1 << (i + 4)); // x=1 half
        if (left && right) {
            symmetricalPairs++;
            std::cerr << "Found symmetrical pair in octants " << i << " and " << (i + 4) << std::endl;
        }
    }

    std::cerr << "Total symmetrical pairs: " << symmetricalPairs << std::endl;

    // Check first level children
    uint32_t childOffset = esvoNodes_[0].GetChildOffset();
    std::cerr << "Root child offset: " << childOffset << std::endl;

    for (int i = 0; i < 8; ++i) {
        if (childMask & (1 << i)) {
            uint32_t childIndex = childOffset + i;
            if (childIndex < esvoNodes_.size()) {
                std::cerr << "  Child " << i << " at index " << childIndex
                          << " has childMask=0x" << std::hex << (int)esvoNodes_[childIndex].GetChildMask() << std::dec
                          << std::endl;
            }
        }
    }
}

} // namespace RenderGraph
} // namespace VIXEN
