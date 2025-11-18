#include "LaineKarrasOctree.h"
#include <sstream>
#include <algorithm>
#include <limits>
#include <cmath>
#include <iostream>

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

// ============================================================================
// Ray Traversal Helpers - Paper-Accurate Implementation
// ============================================================================

namespace {

/**
 * Ray-AABB intersection (robust slab method).
 * Returns true if ray intersects box, with entry/exit t-values in tMin/tMax.
 *
 * Uses stable slab method with robust handling of parallel rays and edge cases.
 * Preferred over Graphics Gems for octree traversal due to accurate exit point computation.
 */
bool intersectAABB(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDir,
    const glm::vec3& boxMin,
    const glm::vec3& boxMax,
    float& tMin,
    float& tMax)
{
    const float epsilon = 1e-8f;

    // Compute safe inverse direction
    glm::vec3 invDir;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(rayDir[i]) < epsilon) {
            // Ray parallel to this axis - check if origin is within slab
            if (rayOrigin[i] < boxMin[i] || rayOrigin[i] > boxMax[i]) {
                return false; // Ray misses box
            }
            // Use large value instead of inf (more stable for min/max operations)
            invDir[i] = (rayDir[i] >= 0.0f) ? 1e20f : -1e20f;
        } else {
            invDir[i] = 1.0f / rayDir[i];
        }
    }

    // Compute intersection t-values for each slab
    glm::vec3 t0 = (boxMin - rayOrigin) * invDir;
    glm::vec3 t1 = (boxMax - rayOrigin) * invDir;

    // Order t0/t1 to ensure tNear < tFar for each axis
    glm::vec3 tNear = glm::min(t0, t1);
    glm::vec3 tFar = glm::max(t0, t1);

    // Find overall entry/exit points (latest entry, earliest exit)
    tMin = std::max({tNear.x, tNear.y, tNear.z});
    tMax = std::min({tFar.x, tFar.y, tFar.z});

    // Ray intersects if entry is before exit and exit is positive
    return tMin <= tMax && tMax >= 0.0f;
}

/**
 * Compute which child octant contains a point.
 * Returns child index (0-7) based on position relative to node center.
 */
int computeChildIndex(const glm::vec3& position, const glm::vec3& nodeMin, const glm::vec3& nodeMax) {
    glm::vec3 center = (nodeMin + nodeMax) * 0.5f;
    int childIdx = 0;
    if (position.x >= center.x) childIdx |= 1;
    if (position.y >= center.y) childIdx |= 2;
    if (position.z >= center.z) childIdx |= 4;
    return childIdx;
}

/**
 * Get child bounds from parent bounds and child index.
 */
void getChildBounds(
    const glm::vec3& parentMin,
    const glm::vec3& parentMax,
    int childIdx,
    glm::vec3& childMin,
    glm::vec3& childMax)
{
    glm::vec3 center = (parentMin + parentMax) * 0.5f;
    childMin = parentMin;
    childMax = center;

    if (childIdx & 1) { // +X
        childMin.x = center.x;
        childMax.x = parentMax.x;
    }
    if (childIdx & 2) { // +Y
        childMin.y = center.y;
        childMax.y = parentMax.y;
    }
    if (childIdx & 4) { // +Z
        childMin.z = center.z;
        childMax.z = parentMax.z;
    }
}

/**
 * Compute AABB face normal based on hit point and ray direction.
 * Returns the normal of the face that the ray enters through.
 * Uses ray direction to break ties when hit point is on edge/corner.
 */
glm::vec3 computeAABBNormal(
    const glm::vec3& hitPoint,
    const glm::vec3& boxMin,
    const glm::vec3& boxMax,
    const glm::vec3& rayDir)
{
    // Clamp hit point to box (handle floating point errors)
    glm::vec3 clampedHit = glm::clamp(hitPoint, boxMin, boxMax);

    // Compute distance to each face (absolute distance to nearest plane per axis)
    glm::vec3 dists;
    dists.x = std::min(clampedHit.x - boxMin.x, boxMax.x - clampedHit.x);
    dists.y = std::min(clampedHit.y - boxMin.y, boxMax.y - clampedHit.y);
    dists.z = std::min(clampedHit.z - boxMin.z, boxMax.z - clampedHit.z);

    // Apply bias based on ray direction (prioritize axis aligned with ray)
    // This breaks ties when hit point is on edge/corner
    const float bias = 1e-6f;
    glm::vec3 absDirInv = 1.0f / (glm::abs(rayDir) + bias);
    glm::vec3 biasedDists = dists * absDirInv;

    // Find axis with minimum biased distance
    if (biasedDists.x <= biasedDists.y && biasedDists.x <= biasedDists.z) {
        // X face
        return (clampedHit.x - boxMin.x < boxMax.x - clampedHit.x) ?
            glm::vec3(-1, 0, 0) : glm::vec3(1, 0, 0);
    } else if (biasedDists.y <= biasedDists.z) {
        // Y face
        return (clampedHit.y - boxMin.y < boxMax.y - clampedHit.y) ?
            glm::vec3(0, -1, 0) : glm::vec3(0, 1, 0);
    } else {
        // Z face
        return (clampedHit.z - boxMin.z < boxMax.z - clampedHit.z) ?
            glm::vec3(0, 0, -1) : glm::vec3(0, 0, 1);
    }
}

} // anonymous namespace

// ============================================================================
// Stack-Based DDA Octree Traversal (Laine-Karras Algorithm)
// ============================================================================

ISVOStructure::RayHit LaineKarrasOctree::castRay(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float tMin,
    float tMax) const
{
    return castRayImpl(origin, direction, tMin, tMax, 0.0f);
}

ISVOStructure::RayHit LaineKarrasOctree::castRayLOD(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float lodBias,
    float tMin,
    float tMax) const
{
    return castRayImpl(origin, direction, tMin, tMax, lodBias);
}

ISVOStructure::RayHit LaineKarrasOctree::castRayImpl(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float tMin,
    float tMax,
    float lodBias) const
{
    ISVOStructure::RayHit miss{};
    miss.hit = false;

    // Validate input
    if (!m_octree || !m_octree->root || m_octree->root->childDescriptors.empty()) {
        return miss;
    }

    // Normalize direction
    glm::vec3 rayDir = glm::normalize(direction);
    float rayLength = glm::length(rayDir);
    if (rayLength < 1e-6f) {
        return miss; // Invalid direction
    }

    // Check for NaN/Inf
    if (glm::any(glm::isnan(origin)) || glm::any(glm::isnan(rayDir)) ||
        glm::any(glm::isinf(origin)) || glm::any(glm::isinf(rayDir))) {
        return miss;
    }

    // ========================================================================
    // Phase 1: Ray-AABB Intersection with World Bounds
    // ========================================================================

    float tEntry, tExit;
    if (!intersectAABB(origin, rayDir, m_worldMin, m_worldMax, tEntry, tExit)) {
        return miss; // Ray misses entire volume
    }

    // Clamp to user-provided range
    tEntry = std::max(tEntry, tMin);
    tExit = std::min(tExit, tMax);

    if (tEntry >= tExit || tExit < 0.0f) {
        return miss;
    }

    // ========================================================================
    // Phase 2: DDA-Based Octree Traversal
    // ========================================================================

    // Start at entry point (or origin if inside volume)
    float t = std::max(tEntry, 0.0f);
    const float epsilon = 1e-6f;
    const int maxSteps = 50000; // Temporary - TODO: implement hierarchical DDA

    for (int step = 0; step < maxSteps && t < tExit; ++step) {
        // Current ray position
        glm::vec3 pos = origin + rayDir * t;

        // Debug: print first few steps and every 1000 steps
        if (step < 10 || step % 1000 == 0) {
            std::cout << "Step " << step << ": t=" << t << ", pos=("
                      << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
        }

        // Debug variables to track final voxel we landed in
        int finalDepth = 0;
        glm::vec3 finalNodeMin = m_worldMin;
        glm::vec3 finalNodeMax = m_worldMax;

        // Find voxel at current position by traversing octree
        const ChildDescriptor* node = &m_octree->root->childDescriptors[0];
        glm::vec3 nodeMin = m_worldMin;
        glm::vec3 nodeMax = m_worldMax;
        int depth = 0;

        // Descend octree to find leaf at current position
        while (depth < m_maxLevels) {
            // Calculate center to determine which octant
            glm::vec3 center = (nodeMin + nodeMax) * 0.5f;

            // Determine child octant containing current position
            int childIdx = 0;
            if (pos.x >= center.x) childIdx |= 1;
            if (pos.y >= center.y) childIdx |= 2;
            if (pos.z >= center.z) childIdx |= 4;

            // Check if child exists
            if (!node->hasChild(childIdx)) {
                // Empty voxel - advance to next voxel boundary
                finalDepth = depth;
                finalNodeMin = nodeMin;
                finalNodeMax = nodeMax;
                if (step < 10 || (step >= 1900 && step <= 1910)) {
                    std::cout << "  Empty at depth=" << depth << ", childIdx=" << childIdx
                              << ", bounds=(" << nodeMin.x << "-" << nodeMax.x << "), size="
                              << (nodeMax.x - nodeMin.x) << std::endl;
                }
                break;
            }

            // Calculate child bounds
            glm::vec3 childMin, childMax;
            getChildBounds(nodeMin, nodeMax, childIdx, childMin, childMax);

            // Check if this is a leaf
            if (node->isLeaf(childIdx)) {
                // Found solid voxel
                // Check if we just entered it or if we started inside it
                float voxelTMin, voxelTMax;
                intersectAABB(origin, rayDir, childMin, childMax, voxelTMin, voxelTMax);

                // If voxelTMin <= epsilon, we're at or inside this voxel
                // Skip it and continue stepping to find the next solid voxel we enter from outside
                if (voxelTMin <= epsilon) {
                    finalDepth = depth;
                    finalNodeMin = childMin;
                    finalNodeMax = childMax;
                    if (step < 10) {
                        std::cout << "  Interior solid at depth=" << depth << ", voxelTMin=" << voxelTMin
                                  << ", voxelTMax=" << voxelTMax << ", size="
                                  << (childMax.x - childMin.x) << " - skipping" << std::endl;
                    }
                    // Step to exit of this voxel
                    t = voxelTMax + epsilon;
                    break; // Continue outer loop
                }

                if (step < 10) {
                    std::cout << "  HIT! Exterior solid at depth=" << depth << ", voxelTMin=" << voxelTMin << std::endl;
                }

                // We entered this voxel from outside - this is our hit!
                ISVOStructure::RayHit hit{};
                hit.hit = true;
                hit.tMin = voxelTMin;
                hit.tMax = voxelTMax;
                hit.position = origin + rayDir * hit.tMin;
                hit.scale = depth + 1;
                hit.normal = computeAABBNormal(hit.position, childMin, childMax, rayDir);
                return hit;
            }

            // Internal node - descend
            // Find child descriptor
            int childOffset = 0;
            for (int i = 0; i < childIdx; ++i) {
                if (node->hasChild(i) && !node->isLeaf(i)) {
                    ++childOffset;
                }
            }

            uint32_t childPointer = node->childPointer;
            uint32_t childIndex = childPointer + childOffset;

            if (childIndex >= m_octree->root->childDescriptors.size()) {
                break; // Invalid
            }

            node = &m_octree->root->childDescriptors[childIndex];
            nodeMin = childMin;
            nodeMax = childMax;
            ++depth;
            finalDepth = depth;
            finalNodeMin = nodeMin;
            finalNodeMax = nodeMax;
        }

        // No solid voxel at current position - advance to next grid boundary
        // Use the final voxel bounds we landed in during descent
        if (step < 10) {
            std::cout << "  Final voxel: depth=" << finalDepth
                      << ", bounds=(" << finalNodeMin.x << "-" << finalNodeMax.x << ")"
                      << ", size=" << (finalNodeMax.x - finalNodeMin.x) << std::endl;
        }

        // Calculate t-values to next axis-aligned voxel boundaries
        glm::vec3 tNext;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(rayDir[axis]) > epsilon) {
                // Find which boundary we'll cross next in this axis
                float boundaryPos;
                if (rayDir[axis] > 0) {
                    boundaryPos = finalNodeMax[axis];
                } else {
                    boundaryPos = finalNodeMin[axis];
                }

                // Add offset to boundary to guarantee we cross it
                // Direction of offset matches ray direction
                float offset = (rayDir[axis] > 0) ? epsilon : -epsilon;
                boundaryPos += offset;

                // Calculate absolute t-value to reach offset boundary
                tNext[axis] = (boundaryPos - origin[axis]) / rayDir[axis];
            } else {
                tNext[axis] = std::numeric_limits<float>::max();
            }
        }

        // Take minimum t-value that's greater than current t
        float tStep = std::numeric_limits<float>::max();
        for (int axis = 0; axis < 3; ++axis) {
            if (tNext[axis] > t && tNext[axis] < tStep) {
                tStep = tNext[axis];
            }
        }

        // Ensure forward progress
        if (tStep == std::numeric_limits<float>::max() || tStep <= t) {
            // Fallback: force small step forward
            t = t + epsilon / std::max({std::abs(rayDir.x), std::abs(rayDir.y), std::abs(rayDir.z)});
        } else {
            t = tStep;
        }
    }

    return miss;
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

// Helper functions (old stubs - replaced by new implementation above)
// These are still declared in header for future contour support

bool LaineKarrasOctree::intersectVoxel(const VoxelCube& voxel, const Contour* contour,
                                        const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                        float& tMin, float& tMax) const {
    // TODO: Implement voxel-contour intersection (future enhancement)
    // Current implementation uses AABB intersection only
    return false;
}

void LaineKarrasOctree::advanceRay(VoxelCube& voxel, int& childIdx,
                                    const glm::vec3& rayDir, float& t) const {
    // TODO: Implement for brick-level DDA (future enhancement)
    // Current implementation uses stack-based octree traversal
}

int LaineKarrasOctree::selectFirstChild(const VoxelCube& voxel,
                                         const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                         float tMin) const {
    // TODO: Implement for optimized child selection (future enhancement)
    // Current implementation computes child index directly
    return 0;
}

} // namespace SVO
