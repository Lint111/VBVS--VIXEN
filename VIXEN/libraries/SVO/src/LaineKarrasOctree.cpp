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
    // ADOPTED FROM: NVIDIA ESVO Reference (cuda/Raycast.inl lines 100-117)
    // Copyright (c) 2009-2011, NVIDIA Corporation (BSD 3-Clause)
    // ========================================================================
    // Parametric plane traversal setup
    // Precompute coefficients for tx(x), ty(y), tz(z)
    // Octree is normalized to [1, 2] in reference, mapped from world bounds here
    // ========================================================================

    // Prevent divide-by-zero with epsilon (2^-23)
    constexpr float epsilon = 1.19209290e-07f; // std::exp2f(-23)
    glm::vec3 rayDirSafe = rayDir;
    if (std::abs(rayDirSafe.x) < epsilon) rayDirSafe.x = std::copysignf(epsilon, rayDirSafe.x);
    if (std::abs(rayDirSafe.y) < epsilon) rayDirSafe.y = std::copysignf(epsilon, rayDirSafe.y);
    if (std::abs(rayDirSafe.z) < epsilon) rayDirSafe.z = std::copysignf(epsilon, rayDirSafe.z);

    // Parametric plane coefficients (computed in [1,2] space later)
    float tx_coef = 1.0f / -std::abs(rayDirSafe.x);
    float ty_coef = 1.0f / -std::abs(rayDirSafe.y);
    float tz_coef = 1.0f / -std::abs(rayDirSafe.z);

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
    // ADOPTED FROM: NVIDIA ESVO Reference (cuda/Raycast.inl lines 100-138)
    // Copyright (c) 2009-2011, NVIDIA Corporation (BSD 3-Clause)
    // ========================================================================
    // Phase 2: ESVO Hierarchical Octree Traversal
    // Reference uses normalized [1,2] space - we must map world space to this
    // ========================================================================

    // Advance ray to entry point (if ray starts outside octree)
    glm::vec3 rayEntryPoint = origin + rayDir * tEntry;

    // Map world space to [1,2] normalized space for algorithm
    // World [worldMin, worldMax] → Normalized [1, 2]
    glm::vec3 worldSize = m_worldMax - m_worldMin;
    glm::vec3 normOrigin = (rayEntryPoint - m_worldMin) / worldSize + glm::vec3(1.0f);
    glm::vec3 normDir = rayDir; // Direction doesn't change

    // Compute bias terms for [1,2] normalized space
    float tx_bias = tx_coef * normOrigin.x;
    float ty_bias = ty_coef * normOrigin.y;
    float tz_bias = tz_coef * normOrigin.z;

    // XOR octant mirroring: mirror coordinate system so ray direction is negative along each axis
    int octant_mask = 7;
    if (rayDirSafe.x > 0.0f) octant_mask ^= 1, tx_bias = 3.0f * tx_coef - tx_bias;
    if (rayDirSafe.y > 0.0f) octant_mask ^= 2, ty_bias = 3.0f * ty_coef - ty_bias;
    if (rayDirSafe.z > 0.0f) octant_mask ^= 4, tz_bias = 3.0f * tz_coef - tz_bias;

    // Initialize active t-span in normalized [1,2] space
    // Reference: lines 121-125
    float t_min = std::max({2.0f * tx_coef - tx_bias,
                            2.0f * ty_coef - ty_bias,
                            2.0f * tz_coef - tz_bias});
    float t_max = std::min({tx_coef - tx_bias,
                            ty_coef - ty_bias,
                            tz_coef - tz_bias});
    float h = t_max;
    t_min = std::max(t_min, 0.0f);
    t_max = std::min(t_max, 1.0f);

    // Initialize traversal state
    // Reference: lines 127-134
    CastStack stack;

    // Safety check: ensure octree has root descriptor
    if (m_octree->root->childDescriptors.empty()) {
        return miss;
    }

    const ChildDescriptor* parent = &m_octree->root->childDescriptors[0];
    uint64_t child_descriptor = 0; // Invalid until fetched
    int idx = 0; // Child octant index
    glm::vec3 pos(1.0f, 1.0f, 1.0f); // Position in normalized [1,2] space
    int scale = CAST_STACK_DEPTH - 1; // Current scale level
    float scale_exp2 = 0.5f; // 2^(scale - s_max), where s_max = CAST_STACK_DEPTH - 1

    // Select initial child based on ray entry point
    // Reference: lines 136-138
    if (1.5f * tx_coef - tx_bias > t_min) idx ^= 1, pos.x = 1.5f;
    if (1.5f * ty_coef - ty_bias > t_min) idx ^= 2, pos.y = 1.5f;
    if (1.5f * tz_coef - tz_bias > t_min) idx ^= 4, pos.z = 1.5f;

    // Main traversal loop
    // Traverse voxels along the ray while staying within octree bounds
    int iter = 0;
    const int maxIter = 10000; // Safety limit

    while (scale < CAST_STACK_DEPTH && iter < maxIter) {
        ++iter;

        // ====================================================================
        // ADOPTED FROM: cuda/Raycast.inl lines 155-169
        // Fetch child descriptor and compute t_corner
        // ====================================================================

        // Fetch child descriptor unless already valid
        if (child_descriptor == 0) {
            // In reference: child_descriptor.x is the first 32 bits with layout:
            // Bits 0-7: nonLeafMask (inverse of leafMask)
            // Bits 8-15: validMask
            // Bits 16-31: childPointer (lower bits) + far bit
            // We pack it to match this layout for correct bit shifting
            uint32_t nonLeafMask = ~parent->leafMask & 0xFF; // Invert leaf mask
            child_descriptor = nonLeafMask |
                             (static_cast<uint64_t>(parent->validMask) << 8) |
                             (static_cast<uint64_t>(parent->childPointer) << 16);
        }

        // Compute maximum t-value at voxel corner
        // This determines when the ray exits the current voxel
        // Using normalized bias terms
        float tx_corner = pos.x * tx_coef - tx_bias;
        float ty_corner = pos.y * ty_coef - ty_bias;
        float tz_corner = pos.z * tz_coef - tz_bias;
        float tc_max = std::min({tx_corner, ty_corner, tz_corner});

        // ====================================================================
        // ADOPTED FROM: cuda/Raycast.inl lines 174-232
        // Check if voxel is valid, test termination, descend or advance
        // ====================================================================

        // Permute child slots based on octant mirroring
        int child_shift = idx ^ octant_mask;
        uint32_t child_masks = static_cast<uint32_t>(child_descriptor) << child_shift;

        // Check if voxel is valid (valid mask bit is set) and t-span is non-empty
        if ((child_masks & 0x8000) != 0 && t_min <= t_max)
        {
            // Terminate if voxel is small enough (reference line 181-182)
            // Note: ray.dir_sz and ray_orig_sz are LOD-related, skipping for now
            // if (tc_max * ray.dir_sz + ray_orig_sz >= scale_exp2) break;

            // ================================================================
            // INTERSECT: Compute t-span intersection with current voxel
            // Reference lines 188-194
            // ================================================================

            float tv_max = std::min(t_max, tc_max);
            float half = scale_exp2 * 0.5f;
            float tx_center = half * tx_coef + tx_corner;
            float ty_center = half * ty_coef + ty_corner;
            float tz_center = half * tz_coef + tz_corner;

            // ================================================================
            // CONTOUR INTERSECTION (Reference lines 196-220)
            // For now, skip contours (will port in future iteration)
            // This means t_min stays as-is, tv_max is just min(t_max, tc_max)
            // ================================================================

            // Descend to first child if resulting t-span is non-empty
            if (t_min <= tv_max)
            {
                // Check if this is a leaf (non-leaf mask bit clear means it's a leaf)
                if ((child_masks & 0x0080) == 0) {
                    // Leaf voxel hit! Return intersection
                    // TODO: Get actual voxel attributes and compute proper normal
                    ISVOStructure::RayHit hit{};
                    hit.hit = true;
                    hit.tMin = t_min;
                    hit.tMax = tv_max;
                    hit.position = origin + rayDir * t_min;
                    hit.scale = CAST_STACK_DEPTH - 1 - scale; // Convert scale to depth
                    // Placeholder normal - should compute from voxel face
                    hit.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                    return hit;
                }

                // ============================================================
                // PUSH: Internal node, descend to children
                // Reference lines 233-246
                // ============================================================

                // Push current state onto stack if needed
                if (tc_max < h) {
                    stack.push(scale, parent, t_max);
                }
                h = tc_max;

                // ============================================================
                // DESCEND: Complete child selection and update traversal state
                // Reference lines 248-274
                // ============================================================

                // Calculate offset to child based on popcount of valid mask
                // Count how many valid children come before current child
                int child_shift_idx = idx ^ octant_mask;
                uint32_t child_offset = 0;

                // Count set bits in child_masks before position child_shift_idx
                // child_masks has valid mask with MSB at position 7 (0x8000 >> child_shift)
                for (int i = 0; i < child_shift_idx; ++i) {
                    if ((child_masks >> (15 - i)) & 1) {  // Check valid mask bits
                        child_offset++;
                    }
                }

                // Update parent pointer to point to child
                // In our structure, childPointer is an index into childDescriptors array
                uint32_t child_index = parent->childPointer + child_offset;

                // Bounds check
                if (child_index >= m_octree->root->childDescriptors.size()) {
                    break; // Invalid child pointer - exit loop
                }

                parent = &m_octree->root->childDescriptors[child_index];

                // Descend to next level
                idx = 0;
                scale--;
                scale_exp2 = half;

                // Select which octant of the child contains the ray entry point
                if (tx_center > t_min) idx ^= 1, pos.x += scale_exp2;
                if (ty_center > t_min) idx ^= 2, pos.y += scale_exp2;
                if (tz_center > t_min) idx ^= 4, pos.z += scale_exp2;

                // Update active t-span and invalidate child descriptor
                t_max = tv_max;
                child_descriptor = 0;
                continue; // Continue main loop
            }
        }

        // ================================================================
        // ADVANCE: Move to next voxel
        // Reference lines 277-290
        // ================================================================

        // Determine which dimensions we need to step in
        int step_mask = 0;
        if (tx_corner <= tc_max) step_mask ^= 1, pos.x -= scale_exp2;
        if (ty_corner <= tc_max) step_mask ^= 2, pos.y -= scale_exp2;
        if (tz_corner <= tc_max) step_mask ^= 4, pos.z -= scale_exp2;

        // Update active t-span and flip child slot index bits
        t_min = tc_max;
        idx ^= step_mask;

        // ================================================================
        // POP: Backtrack if we've exited the current parent voxel
        // Reference lines 292-327
        // ================================================================

        // Check if bit flips disagree with ray direction (means we left parent)
        if ((idx & step_mask) != 0)
        {
            // ============================================================
            // POP: Find the highest differing bit to determine scale level
            // Reference lines 296-327
            // ============================================================

            // Find highest differing bit between current and next position
            uint32_t differing_bits = 0;

            if ((step_mask & 1) != 0) {
                uint32_t pos_x_bits = std::bit_cast<uint32_t>(pos.x);
                uint32_t next_x_bits = std::bit_cast<uint32_t>(pos.x + scale_exp2);
                differing_bits |= (pos_x_bits ^ next_x_bits);
            }
            if ((step_mask & 2) != 0) {
                uint32_t pos_y_bits = std::bit_cast<uint32_t>(pos.y);
                uint32_t next_y_bits = std::bit_cast<uint32_t>(pos.y + scale_exp2);
                differing_bits |= (pos_y_bits ^ next_y_bits);
            }
            if ((step_mask & 4) != 0) {
                uint32_t pos_z_bits = std::bit_cast<uint32_t>(pos.z);
                uint32_t next_z_bits = std::bit_cast<uint32_t>(pos.z + scale_exp2);
                differing_bits |= (pos_z_bits ^ next_z_bits);
            }

            // Extract scale from the position of highest bit
            // Convert differing_bits to float (value conversion, not bit reinterpretation)
            // Then reinterpret the float's bits to extract exponent
            float diff_value_as_float = static_cast<float>(differing_bits);
            uint32_t diff_float_bits = std::bit_cast<uint32_t>(diff_value_as_float);
            scale = static_cast<int>((diff_float_bits >> 23) & 0xFF) - 127;

            // Compute scale_exp2 = exp2f(scale - CAST_STACK_DEPTH)
            int scale_exp = scale - CAST_STACK_DEPTH + 127;
            scale_exp2 = std::bit_cast<float>(static_cast<uint32_t>(scale_exp << 23));

            // Restore parent voxel from stack
            if (scale >= 0 && scale < CAST_STACK_DEPTH) {
                parent = stack.nodes[scale];
                t_max = stack.tMax[scale];
            } else {
                // Scale out of bounds - exit loop
                break;
            }

            // Round cube position and extract child slot index
            uint32_t shx = std::bit_cast<uint32_t>(pos.x) >> scale;
            uint32_t shy = std::bit_cast<uint32_t>(pos.y) >> scale;
            uint32_t shz = std::bit_cast<uint32_t>(pos.z) >> scale;

            pos.x = std::bit_cast<float>(shx << scale);
            pos.y = std::bit_cast<float>(shy << scale);
            pos.z = std::bit_cast<float>(shz << scale);

            idx = (shx & 1) | ((shy & 1) << 1) | ((shz & 1) << 2);

            // Prevent same parent from being stored again and invalidate cached child descriptor
            h = 0.0f;
            child_descriptor = 0;
        }
    }

    // ====================================================================
    // TERMINATION: Undo mirroring and return result
    // Reference lines 342-346
    // ====================================================================

    // If we exited the octree, return miss
    if (scale >= CAST_STACK_DEPTH || iter >= maxIter) {
        return miss;
    }

    // Undo coordinate mirroring
    if ((octant_mask & 1) == 0) pos.x = 3.0f - scale_exp2 - pos.x;
    if ((octant_mask & 2) == 0) pos.y = 3.0f - scale_exp2 - pos.y;
    if ((octant_mask & 4) == 0) pos.z = 3.0f - scale_exp2 - pos.z;

    // If we reach here without hitting, return miss
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
