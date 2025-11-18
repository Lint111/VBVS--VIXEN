#include "LaineKarrasOctree.h"
#include <sstream>
#include <algorithm>
#include <limits>
#include <cmath>

namespace SVO {

LaineKarrasOctree::LaineKarrasOctree() = default;

LaineKarrasOctree::~LaineKarrasOctree() = default;

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
        // Find which child octant contains the position
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

        // Check if child exists
        if (!currentNode->hasChild(childIdx)) {
            return false;
        }

        // If this is a leaf, voxel exists
        if (currentNode->isLeaf(childIdx)) {
            return true;
        }

        // Move to child node
        int childOffset = 0;
        for (int i = 0; i < childIdx; ++i) {
            if (currentNode->hasChild(i) && !currentNode->isLeaf(i)) {
                ++childOffset;
            }
        }

        uint32_t childPointer = currentNode->childPointer;
        if (currentNode->farBit) {
            // Indirect reference - need to follow pointer
            // For now, assume direct addressing
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

    // Bounds check
    if (glm::any(glm::lessThan(position, m_worldMin)) ||
        glm::any(glm::greaterThanEqual(position, m_worldMax))) {
        return std::nullopt;
    }

    // Normalize position to [0,1]
    glm::vec3 normalizedPos = (position - m_worldMin) / (m_worldMax - m_worldMin);

    // Traverse to find voxel and track attribute lookup
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

        // If this is a leaf, we found our voxel
        if (currentNode->isLeaf(childIdx)) {
            // Get attribute lookup for this node
            if (nodeIndexInArray < static_cast<int>(m_octree->root->attributeLookups.size())) {
                attrLookup = &m_octree->root->attributeLookups[nodeIndexInArray];
            }
            break;
        }

        // Move to child
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

    // Retrieve attribute data
    ISVOStructure::VoxelData data{};

    if (attrLookup && attrLookup->hasAttribute(finalChildIdx)) {
        // Calculate attribute index
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

    // Default white voxel with up normal if no attributes
    data.color = glm::vec3(1.0f);
    data.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    return data;
}

uint8_t LaineKarrasOctree::getChildMask(const glm::vec3& position, int scale) const {
    if (!m_octree || !m_octree->root) {
        return 0;
    }

    // Bounds check
    if (glm::any(glm::lessThan(position, m_worldMin)) ||
        glm::any(glm::greaterThanEqual(position, m_worldMax))) {
        return 0;
    }

    // Normalize position to [0,1]
    glm::vec3 normalizedPos = (position - m_worldMin) / (m_worldMax - m_worldMin);

    // Traverse to target depth
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
            // Leaves have no children
            return 0;
        }

        // Move to child
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

    // Return valid mask of current node
    return currentNode->validMask;
}

ISVOStructure::VoxelBounds LaineKarrasOctree::getVoxelBounds(const glm::vec3& position, int scale) const {
    ISVOStructure::VoxelBounds bounds{};
    bounds.min = m_worldMin;
    bounds.max = m_worldMax;
    return bounds;
}

ISVOStructure::RayHit LaineKarrasOctree::castRay(const glm::vec3& origin, const glm::vec3& direction,
                                   float tMin, float tMax) const {
    return castRayImpl(origin, direction, tMin, tMax, 0.0f);
}

ISVOStructure::RayHit LaineKarrasOctree::castRayLOD(const glm::vec3& origin, const glm::vec3& direction,
                                      float lodBias, float tMin, float tMax) const {
    return castRayImpl(origin, direction, tMin, tMax, lodBias);
}

ISVOStructure::RayHit LaineKarrasOctree::castRayImpl(const glm::vec3& origin, const glm::vec3& direction,
                                       float tMin, float tMax, float lodBias) const {
    ISVOStructure::RayHit hit{};
    hit.hit = false;

    if (!m_octree || !m_octree->root || m_octree->root->childDescriptors.empty()) {
        return hit;
    }

    // Work in world space, not normalized
    glm::vec3 worldSize = m_worldMax - m_worldMin;
    glm::vec3 rayOrigin = origin;
    glm::vec3 rayDir = glm::normalize(direction);

    // Keep t values in world space
    float t0 = tMin;
    float t1 = tMax;

    // Intersect ray with world bounds
    for (int i = 0; i < 3; ++i) {
        if (std::abs(rayDir[i]) < 1e-6f) {
            // Ray parallel to this axis - check if within bounds
            if (rayOrigin[i] < m_worldMin[i] || rayOrigin[i] >= m_worldMax[i]) {
                return hit; // Ray misses
            }
            continue;
        }

        float invDir = 1.0f / rayDir[i];
        float tNear = (m_worldMin[i] - rayOrigin[i]) * invDir;
        float tFar = (m_worldMax[i] - rayOrigin[i]) * invDir;
        if (tNear > tFar) std::swap(tNear, tFar);
        t0 = std::max(t0, tNear);
        t1 = std::min(t1, tFar);
        if (t0 > t1) return hit; // Ray misses bounds
    }

    // Clamp to valid range
    t0 = std::max(t0, 0.0f);

    // Initialize traversal - work in normalized [0,1] coordinates
    VoxelCube voxel{};
    glm::vec3 worldPos = rayOrigin + rayDir * t0;
    voxel.position = (worldPos - m_worldMin) / worldSize;
    voxel.scale = 23; // Start at finest scale (2^23 voxels per axis)

    const int maxIterations = 512; // Safety limit
    int iteration = 0;

    // DDA-based octree traversal
    while (t0 < t1 && iteration++ < maxIterations) {
        // Clamp position to [0,1]
        voxel.position = glm::clamp(voxel.position, glm::vec3(0.0f), glm::vec3(1.0f - 1e-6f));

        // Traverse octree to find voxel at current position
        const ChildDescriptor* node = &m_octree->root->childDescriptors[0];
        int depth = 0;
        int targetDepth = std::min(m_maxLevels - 1, static_cast<int>(23 - voxel.scale + lodBias));
        bool found = false;

        glm::vec3 nodePos(0.0f);
        float nodeSize = 1.0f;

        for (depth = 0; depth < targetDepth; ++depth) {
            nodeSize *= 0.5f;
            int childIdx = 0;
            glm::vec3 childPos = nodePos;

            if (voxel.position.x >= nodePos.x + nodeSize) {
                childIdx |= 1;
                childPos.x += nodeSize;
            }
            if (voxel.position.y >= nodePos.y + nodeSize) {
                childIdx |= 2;
                childPos.y += nodeSize;
            }
            if (voxel.position.z >= nodePos.z + nodeSize) {
                childIdx |= 4;
                childPos.z += nodeSize;
            }

            if (!node->hasChild(childIdx)) {
                break; // Empty space
            }

            if (node->isLeaf(childIdx)) {
                // Hit a voxel!
                hit.hit = true;
                hit.tMin = t0;
                hit.tMax = hit.tMin + nodeSize * worldSize.x;
                hit.position = origin + direction * hit.tMin;
                hit.scale = depth;

                // Compute surface normal (simple AABB normal for now)
                glm::vec3 voxelWorldMin = m_worldMin + nodePos * worldSize;
                glm::vec3 voxelWorldMax = voxelWorldMin + glm::vec3(nodeSize) * worldSize;
                glm::vec3 localPos = (hit.position - voxelWorldMin) / (voxelWorldMax - voxelWorldMin);
                localPos = glm::clamp(localPos, glm::vec3(0.0f), glm::vec3(1.0f));

                // Find which face was hit
                hit.normal = glm::vec3(0.0f, 1.0f, 0.0f); // Default up
                float minDist = 1e10f;
                const glm::vec3 faceNormals[6] = {
                    glm::vec3(-1, 0, 0), glm::vec3(1, 0, 0),
                    glm::vec3(0, -1, 0), glm::vec3(0, 1, 0),
                    glm::vec3(0, 0, -1), glm::vec3(0, 0, 1)
                };
                const float faceDists[6] = {
                    localPos.x, 1.0f - localPos.x,
                    localPos.y, 1.0f - localPos.y,
                    localPos.z, 1.0f - localPos.z
                };
                for (int i = 0; i < 6; ++i) {
                    if (faceDists[i] < minDist) {
                        minDist = faceDists[i];
                        hit.normal = faceNormals[i];
                    }
                }

                return hit;
            }

            // Move to child
            int childOffset = 0;
            for (int i = 0; i < childIdx; ++i) {
                if (node->hasChild(i) && !node->isLeaf(i)) {
                    ++childOffset;
                }
            }

            uint32_t childPointer = node->childPointer;
            node = &m_octree->root->childDescriptors[childPointer + childOffset];
            nodePos = childPos;
        }

        // Advance ray to next voxel using DDA
        float voxelSize = voxel.getSize();
        glm::vec3 tMax3D;
        glm::vec3 tDelta;

        for (int i = 0; i < 3; ++i) {
            if (std::abs(rayDir[i]) < 1e-6f) {
                tMax3D[i] = std::numeric_limits<float>::max();
                tDelta[i] = std::numeric_limits<float>::max();
            } else {
                float invDir = 1.0f / rayDir[i];
                float voxelBoundary = rayDir[i] > 0 ?
                    std::ceil(voxel.position[i] / voxelSize) * voxelSize :
                    std::floor(voxel.position[i] / voxelSize) * voxelSize;
                tMax3D[i] = (voxelBoundary - voxel.position[i]) * invDir;
                tDelta[i] = voxelSize * std::abs(invDir);
            }
        }

        // Step to next voxel
        float tStep = std::min(tMax3D.x, std::min(tMax3D.y, tMax3D.z));
        t0 += tStep + 1e-6f; // Small epsilon to avoid floating point errors
        voxel.position += rayDir * (tStep + 1e-6f);

        // Check if we exited the volume
        if (glm::any(glm::lessThan(voxel.position, glm::vec3(0.0f))) ||
            glm::any(glm::greaterThanEqual(voxel.position, glm::vec3(1.0f)))) {
            break;
        }
    }

    return hit;
}

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
    oss << "  Total voxels: " << m_voxelCount << "\n";
    oss << "  Max levels: " << m_maxLevels << "\n";
    oss << "  Memory usage: " << (m_memoryUsage / 1024.0 / 1024.0) << " MB\n";
    oss << "  Avg bytes/voxel: " << (m_voxelCount > 0 ? m_memoryUsage / m_voxelCount : 0) << "\n";
    return oss.str();
}

std::vector<uint8_t> LaineKarrasOctree::serialize() const {
    // TODO: Implement serialization
    std::vector<uint8_t> data;
    return data;
}

bool LaineKarrasOctree::deserialize(std::span<const uint8_t> data) {
    // TODO: Implement deserialization
    return false;
}

ISVOStructure::GPUBuffers LaineKarrasOctree::getGPUBuffers() const {
    ISVOStructure::GPUBuffers buffers{};

    if (!m_octree || !m_octree->root) {
        return buffers;
    }

    // TODO: Pack data into GPU-friendly format
    // For now, return empty buffers

    return buffers;
}

std::string LaineKarrasOctree::getGPUTraversalShader() const {
    // TODO: Implement GLSL translation of CUDA ray caster
    return R"(
// Placeholder for GPU traversal shader
// Will be implemented in GPU ray caster phase
)";
}

bool LaineKarrasOctree::intersectVoxel(const VoxelCube& voxel, const Contour* contour,
                                        const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                        float& tMin, float& tMax) const {
    // TODO: Implement voxel-ray intersection with contour support
    return false;
}

void LaineKarrasOctree::advanceRay(VoxelCube& voxel, int& childIdx,
                                    const glm::vec3& rayDir, float& t) const {
    // TODO: Implement ray advancement to next voxel
}

int LaineKarrasOctree::selectFirstChild(const VoxelCube& voxel,
                                         const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                         float tMin) const {
    // TODO: Implement first child selection for ray
    return 0;
}

} // namespace SVO
