/**
 * LaineKarrasOctree.cpp - Facade/Coordinator for SVO Manager Subsystem
 * ==============================================================================
 * Main entry point for the Laine-Karras Sparse Voxel Octree implementation.
 * Provides the public API and coordinates between specialized subsystems:
 *
 * SUBSYSTEM FILES:
 * ----------------
 * - SVOTraversal.cpp:  ESVO ray casting algorithm (Laine & Karras 2010)
 * - SVOBrickDDA.cpp:   Brick-level 3D DDA traversal (Amanatides & Woo 1987)
 * - SVORebuild.cpp:    Entity-based octree construction with Morton sorting
 *
 * REFERENCES:
 * -----------
 * [1] Laine, S. and Karras, T. "Efficient Sparse Voxel Octrees"
 *     NVIDIA Research, I3D 2010
 *     https://research.nvidia.com/publication/efficient-sparse-voxel-octrees
 *
 * [2] Laine, S. and Karras, T. "Efficient Sparse Voxel Octrees: Analysis,
 *     Extensions, and Implementation" NVIDIA Technical Report, 2010
 *
 * [3] NVIDIA ESVO Reference Implementation (BSD 3-Clause License)
 *     Copyright (c) 2009-2011, NVIDIA Corporation
 *
 * ARCHITECTURE:
 * -------------
 * This file contains:
 * - Constructor/destructor and initialization
 * - ISVOStructure interface implementation (voxelExists, getVoxelData, etc.)
 * - GPU buffer accessors
 * - DXT compression accessors (Week 3)
 * - Stats and serialization stubs
 *
 * Ray casting and octree building are delegated to specialized subsystems.
 *
 * ==============================================================================
 */

#define NOMINMAX
#include "pch.h"
#include "LaineKarrasOctree.h"
#include "VoxelComponents.h"
#include "ComponentData.h"
#include <sstream>
#include <algorithm>
#include <limits>
#include <cmath>

using namespace Vixen::GaiaVoxel;
using namespace Vixen::VoxelData;

namespace Vixen::SVO {

// ============================================================================
// Constructor / Destructor
// ============================================================================

LaineKarrasOctree::LaineKarrasOctree(GaiaVoxelWorld& voxelWorld, AttributeRegistry* registry, int maxLevels, int brickDepthLevels)
    : m_voxelWorld(&voxelWorld)
    , m_registry(registry)
    , m_maxLevels(maxLevels)
    , m_brickDepthLevels(brickDepthLevels)
{
    // SVO stores only entity IDs (8 bytes each), not voxel data
    // Caller reads entity components via m_voxelWorld
}

LaineKarrasOctree::~LaineKarrasOctree() = default;

// ============================================================================
// Octree Management
// ============================================================================

void LaineKarrasOctree::setOctree(std::unique_ptr<Octree> octree) {
    m_octree = std::move(octree);

    if (m_octree) {
        m_worldMin = m_octree->worldMin;
        m_worldMax = m_octree->worldMax;
        m_maxLevels = m_octree->maxLevels;
        m_voxelCount = m_octree->totalVoxels;
        m_memoryUsage = m_octree->memoryUsage;
    }
}

void LaineKarrasOctree::ensureInitialized(const glm::vec3& worldMin, const glm::vec3& worldMax, int maxLevels) {
    if (!m_octree) {
        m_octree = std::make_unique<Octree>();
        m_octree->worldMin = worldMin;
        m_octree->worldMax = worldMax;
        m_octree->maxLevels = maxLevels;
        m_octree->totalVoxels = 0;
        m_octree->memoryUsage = 0;
        m_octree->root = std::make_unique<OctreeBlock>();

        m_worldMin = worldMin;
        m_worldMax = worldMax;
        m_maxLevels = maxLevels;
        m_voxelCount = 0;
        m_memoryUsage = 0;
    }
}

// ============================================================================
// ISVOStructure Interface - Voxel Query Methods
// ============================================================================

bool LaineKarrasOctree::voxelExists(const glm::vec3& position, int scale) const {
    if (!m_octree || !m_octree->root) {
        return false;
    }

    // Bounds check
    if (glm::any(glm::lessThan(position, m_worldMin)) ||
        glm::any(glm::greaterThanEqual(position, m_worldMax))) {
        return false;
    }

    // Normalize position to [0,1]
    glm::vec3 normalizedPos = (position - m_worldMin) / (m_worldMax - m_worldMin);

    // Traverse from root to target depth
    const ChildDescriptor* currentNode = &m_octree->root->childDescriptors[0];
    glm::vec3 nodePos(0.0f);
    float nodeSize = 1.0f;

    for (int level = 0; level < scale; ++level) {
        nodeSize *= 0.5f;
        int childIdx = 0;
        glm::vec3 childPos = nodePos;

        if (normalizedPos.x >= nodePos.x + nodeSize) {
            childIdx |= 1;
            childPos.x += nodeSize;
        }
        if (normalizedPos.y >= nodePos.y + nodeSize) {
            childIdx |= 2;
            childPos.y += nodeSize;
        }
        if (normalizedPos.z >= nodePos.z + nodeSize) {
            childIdx |= 4;
            childPos.z += nodeSize;
        }

        if (!currentNode->hasChild(childIdx)) {
            return false;
        }

        if (currentNode->isLeaf(childIdx)) {
            return true;
        }

        int childOffset = 0;
        for (int i = 0; i < childIdx; ++i) {
            if (currentNode->hasChild(i) && !currentNode->isLeaf(i)) {
                ++childOffset;
            }
        }

        uint32_t childPointer = currentNode->childPointer;
        if (currentNode->farBit) {
            childPointer = currentNode->childPointer;
        }

        currentNode = &m_octree->root->childDescriptors[childPointer + childOffset];
        nodePos = childPos;
    }

    return true;
}

std::optional<ISVOStructure::VoxelData> LaineKarrasOctree::getVoxelData(const glm::vec3& position, int scale) const {
    if (!m_octree || !m_octree->root) {
        return std::nullopt;
    }

    if (glm::any(glm::lessThan(position, m_worldMin)) ||
        glm::any(glm::greaterThanEqual(position, m_worldMax))) {
        return std::nullopt;
    }

    glm::vec3 normalizedPos = (position - m_worldMin) / (m_worldMax - m_worldMin);

    const ChildDescriptor* currentNode = &m_octree->root->childDescriptors[0];
    const AttributeLookup* attrLookup = nullptr;
    int finalChildIdx = 0;
    glm::vec3 nodePos(0.0f);
    float nodeSize = 1.0f;
    int nodeIndexInArray = 0;

    for (int level = 0; level < scale; ++level) {
        nodeSize *= 0.5f;
        int childIdx = 0;
        glm::vec3 childPos = nodePos;

        if (normalizedPos.x >= nodePos.x + nodeSize) {
            childIdx |= 1;
            childPos.x += nodeSize;
        }
        if (normalizedPos.y >= nodePos.y + nodeSize) {
            childIdx |= 2;
            childPos.y += nodeSize;
        }
        if (normalizedPos.z >= nodePos.z + nodeSize) {
            childIdx |= 4;
            childPos.z += nodeSize;
        }

        if (!currentNode->hasChild(childIdx)) {
            return std::nullopt;
        }

        finalChildIdx = childIdx;

        if (currentNode->isLeaf(childIdx)) {
            if (nodeIndexInArray < static_cast<int>(m_octree->root->attributeLookups.size())) {
                attrLookup = &m_octree->root->attributeLookups[nodeIndexInArray];
            }
            break;
        }

        int childOffset = 0;
        for (int i = 0; i < childIdx; ++i) {
            if (currentNode->hasChild(i) && !currentNode->isLeaf(i)) {
                ++childOffset;
            }
        }

        uint32_t childPointer = currentNode->childPointer;
        nodeIndexInArray = childPointer + childOffset;
        currentNode = &m_octree->root->childDescriptors[nodeIndexInArray];
        nodePos = childPos;
    }

    ISVOStructure::VoxelData data{};

    if (attrLookup && attrLookup->hasAttribute(finalChildIdx)) {
        int attrOffset = 0;
        for (int i = 0; i < finalChildIdx; ++i) {
            if (attrLookup->hasAttribute(i)) {
                ++attrOffset;
            }
        }

        uint32_t attrIndex = attrLookup->valuePointer + attrOffset;
        if (attrIndex < m_octree->root->attributes.size()) {
            const UncompressedAttributes& attr = m_octree->root->attributes[attrIndex];
            data.color = attr.getColor();
            data.normal = attr.getNormal();
            return data;
        }
    }

    data.color = glm::vec3(1.0f);
    data.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    return data;
}

uint8_t LaineKarrasOctree::getChildMask(const glm::vec3& position, int scale) const {
    if (!m_octree || !m_octree->root) {
        return 0;
    }

    if (glm::any(glm::lessThan(position, m_worldMin)) ||
        glm::any(glm::greaterThanEqual(position, m_worldMax))) {
        return 0;
    }

    glm::vec3 normalizedPos = (position - m_worldMin) / (m_worldMax - m_worldMin);

    const ChildDescriptor* currentNode = &m_octree->root->childDescriptors[0];
    glm::vec3 nodePos(0.0f);
    float nodeSize = 1.0f;

    for (int level = 0; level < scale; ++level) {
        nodeSize *= 0.5f;
        int childIdx = 0;
        glm::vec3 childPos = nodePos;

        if (normalizedPos.x >= nodePos.x + nodeSize) {
            childIdx |= 1;
            childPos.x += nodeSize;
        }
        if (normalizedPos.y >= nodePos.y + nodeSize) {
            childIdx |= 2;
            childPos.y += nodeSize;
        }
        if (normalizedPos.z >= nodePos.z + nodeSize) {
            childIdx |= 4;
            childPos.z += nodeSize;
        }

        if (!currentNode->hasChild(childIdx)) {
            return 0;
        }

        if (currentNode->isLeaf(childIdx)) {
            return 0;
        }

        int childOffset = 0;
        for (int i = 0; i < childIdx; ++i) {
            if (currentNode->hasChild(i) && !currentNode->isLeaf(i)) {
                ++childOffset;
            }
        }

        uint32_t childPointer = currentNode->childPointer;
        currentNode = &m_octree->root->childDescriptors[childPointer + childOffset];
        nodePos = childPos;
    }

    return currentNode->validMask;
}

ISVOStructure::VoxelBounds LaineKarrasOctree::getVoxelBounds(const glm::vec3& position, int scale) const {
    ISVOStructure::VoxelBounds bounds{};
    bounds.min = m_worldMin;
    bounds.max = m_worldMax;
    return bounds;
}

// ============================================================================
// Stats and Utility Methods
// ============================================================================

float LaineKarrasOctree::getVoxelSize(int scale) const {
    if (scale >= m_maxLevels) {
        return 0.0f;
    }

    glm::vec3 worldSize = m_worldMax - m_worldMin;
    return worldSize.x / std::pow(2.0f, static_cast<float>(scale));
}

std::string LaineKarrasOctree::getStats() const {
    std::ostringstream oss;
    oss << "Laine-Karras SVO Statistics:\n";

    size_t voxelCount = m_octree ? m_octree->totalVoxels : m_voxelCount;
    size_t memoryUsage = m_octree ? m_octree->memoryUsage : m_memoryUsage;

    oss << "  Total voxels: " << voxelCount << "\n";
    oss << "  Max levels: " << m_maxLevels << "\n";
    oss << "  Memory usage: " << (memoryUsage / 1024.0 / 1024.0) << " MB\n";
    oss << "  Avg bytes/voxel: " << (voxelCount > 0 ? memoryUsage / voxelCount : 0) << "\n";
    return oss.str();
}

// ============================================================================
// Serialization (Stubs)
// ============================================================================

std::vector<uint8_t> LaineKarrasOctree::serialize() const {
    // TODO: Implement serialization
    std::vector<uint8_t> data;
    return data;
}

bool LaineKarrasOctree::deserialize(std::span<const uint8_t> data) {
    // TODO: Implement deserialization
    return false;
}

// ============================================================================
// GPU Buffer Accessors
// ============================================================================

ISVOStructure::GPUBuffers LaineKarrasOctree::getGPUBuffers() const {
    ISVOStructure::GPUBuffers buffers{};

    if (!m_octree || !m_octree->root) {
        return buffers;
    }

    // TODO: Pack hierarchyBuffer, attributeBuffer, auxBuffer

    // Pack compressed color buffer (binding 7)
    if (!m_octree->root->compressedColors.empty()) {
        const size_t colorBytes = m_octree->root->compressedColors.size() * sizeof(uint64_t);
        buffers.compressedColorBuffer.resize(colorBytes);
        std::memcpy(buffers.compressedColorBuffer.data(),
                    m_octree->root->compressedColors.data(),
                    colorBytes);
    }

    // Pack compressed normal buffer (binding 8)
    if (!m_octree->root->compressedNormals.empty()) {
        const size_t normalBytes = m_octree->root->compressedNormals.size() *
                                   sizeof(OctreeBlock::CompressedNormalBlock);
        buffers.compressedNormalBuffer.resize(normalBytes);
        std::memcpy(buffers.compressedNormalBuffer.data(),
                    m_octree->root->compressedNormals.data(),
                    normalBytes);
    }

    return buffers;
}

std::string LaineKarrasOctree::getGPUTraversalShader() const {
    // TODO: Implement GLSL translation of CUDA ray caster
    return R"(
// Placeholder for GPU traversal shader
// Will be implemented in GPU ray caster phase
)";
}

// ============================================================================
// DXT Compression Accessors (Week 3)
// ============================================================================

bool LaineKarrasOctree::hasCompressedData() const {
    return m_octree && m_octree->root &&
           !m_octree->root->compressedColors.empty() &&
           !m_octree->root->compressedNormals.empty();
}

const uint64_t* LaineKarrasOctree::getCompressedColorData() const {
    if (!hasCompressedData()) {
        return nullptr;
    }
    return m_octree->root->compressedColors.data();
}

size_t LaineKarrasOctree::getCompressedColorSize() const {
    if (!hasCompressedData()) {
        return 0;
    }
    return m_octree->root->compressedColors.size() * sizeof(uint64_t);
}

const void* LaineKarrasOctree::getCompressedNormalData() const {
    if (!hasCompressedData()) {
        return nullptr;
    }
    return m_octree->root->compressedNormals.data();
}

size_t LaineKarrasOctree::getCompressedNormalSize() const {
    if (!hasCompressedData()) {
        return 0;
    }
    return m_octree->root->compressedNormals.size() * sizeof(OctreeBlock::CompressedNormalBlock);
}

size_t LaineKarrasOctree::getCompressedBrickCount() const {
    if (!m_octree || !m_octree->root) {
        return 0;
    }
    return m_octree->root->brickViews.size();
}

// ============================================================================
// Legacy Traversal Helpers (Stubs for Future Contour Support)
// ============================================================================

bool LaineKarrasOctree::intersectVoxel(const VoxelCube& voxel, const Contour* contour,
                                        const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                        float& tMin, float& tMax) const {
    // TODO: Implement voxel-contour intersection (future enhancement)
    return false;
}

void LaineKarrasOctree::advanceRay(VoxelCube& voxel, int& childIdx,
                                    const glm::vec3& rayDir, float& t) const {
    // TODO: Implement for brick-level DDA (future enhancement)
}

int LaineKarrasOctree::selectFirstChild(const VoxelCube& voxel,
                                         const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                         float tMin) const {
    // TODO: Implement for optimized child selection (future enhancement)
    return 0;
}

} // namespace Vixen::SVO
