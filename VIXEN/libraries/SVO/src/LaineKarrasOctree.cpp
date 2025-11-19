#include "LaineKarrasOctree.h"
#include <sstream>
#include <algorithm>
#include <limits>
#include <cmath>
#include <iostream>

namespace SVO {

// ============================================================================
// Debug Utilities
// ============================================================================
// Compile-time toggleable debug output for ray traversal
//
// Usage:
//   1. Set LKOCTREE_DEBUG_TRAVERSAL to 1 to enable detailed debug output
//   2. Rebuild the project
//   3. Run tests to see traversal state for each iteration
//
// Debug output includes:
//   - Octant mirroring setup
//   - Initial traversal state (entry point, mirrored coordinates)
//   - Per-iteration state (scale, idx, pos, t_min, t_max, parent, descriptor)
//   - Child validity checks (validMask, leafMask, t-span)
//   - Valid voxel detection and leaf hits
//   - DESCEND operations (child offset calculation, parent updates)
//   - ADVANCE operations (step_mask, position updates)
//
// Note: Debug output is completely compiled out when disabled (zero runtime cost)
//
#define LKOCTREE_DEBUG_TRAVERSAL 0

#if LKOCTREE_DEBUG_TRAVERSAL
    #define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
    #define DEBUG_PRINT(...) ((void)0)
#endif

namespace {
    // Debug helper: Print octant mirroring setup
    inline void debugOctantMirroring(const glm::vec3& rayDir, const glm::vec3& rayDirSafe, int octant_mask) {
        DEBUG_PRINT("\n=== Octant Mirroring ===\n");
        DEBUG_PRINT("  rayDir=(%.6f, %.6f, %.6f), rayDirSafe=(%.6f, %.6f, %.6f)\n",
                    rayDir.x, rayDir.y, rayDir.z, rayDirSafe.x, rayDirSafe.y, rayDirSafe.z);
        DEBUG_PRINT("  Initial octant_mask=%d\n", octant_mask);
    }

    // Debug helper: Print initial traversal state
    inline void debugInitialState(const glm::vec3& normOrigin, const glm::vec3& mirrored,
                                   int octant_mask, int idx, const glm::vec3& pos) {
        DEBUG_PRINT("INIT: norm=(%.3f,%.3f,%.3f), mir=(%.3f,%.3f,%.3f), octant_mask=%d, idx=%d, pos=(%.3f,%.3f,%.3f)\n",
                    normOrigin.x, normOrigin.y, normOrigin.z,
                    mirrored.x, mirrored.y, mirrored.z,
                    octant_mask, idx, pos.x, pos.y, pos.z);
    }

    // Debug helper: Print iteration state
    inline void debugIterationState(int iter, int scale, int idx, int octant_mask,
                                     float t_min, float t_max, const glm::vec3& pos, float scale_exp2,
                                     const void* parent, uint64_t child_descriptor) {
        DEBUG_PRINT("\n=== Iter %d ===\n", iter);
        DEBUG_PRINT("  scale=%d, idx=%d (0b%d%d%d), octant_mask=%d, t_min=%.3f, t_max=%.3f\n",
                    scale, idx, (idx>>2)&1, (idx>>1)&1, idx&1, octant_mask, t_min, t_max);
        DEBUG_PRINT("  pos=(%.3f, %.3f, %.3f), scale_exp2=%.6f\n", pos.x, pos.y, pos.z, scale_exp2);
        DEBUG_PRINT("  parent=%p, child_descriptor=%llu\n", parent, child_descriptor);
    }

    // Debug helper: Print child validity check
    inline void debugChildValidity(int child_shift, uint32_t child_masks,
                                    bool old_valid, bool correct_valid,
                                    bool old_leaf, bool correct_leaf,
                                    float t_min, float t_max) {
        DEBUG_PRINT("  child_shift=%d, child_masks=0x%04X\n", child_shift, child_masks);
        DEBUG_PRINT("  valid_bit=%d (correct=%d), is_leaf=%d (correct=%d)\n",
                    old_valid, correct_valid, old_leaf, correct_leaf);
        DEBUG_PRINT("  Check: child_valid=%d, t_min(%.3f) <= t_max(%.3f) = %d\n",
                    correct_valid, t_min, t_max, t_min <= t_max);
    }

    // Debug helper: Print valid voxel found
    inline void debugValidVoxel(float t_min, float tv_max) {
        DEBUG_PRINT("  --> Valid voxel, t_min=%.3f <= tv_max=%.3f\n", t_min, tv_max);
    }

    // Debug helper: Print leaf hit
    inline void debugLeafHit(int scale) {
        DEBUG_PRINT("  --> LEAF HIT at scale=%d!\n", scale);
    }

    // Debug helper: Print descend operation
    inline void debugDescend(int scale, float t_max, int child_shift_idx, uint8_t validMask,
                            uint32_t mask_before, uint32_t valid_before, uint32_t child_offset,
                            uint32_t childPointer, uint32_t child_index, const void* new_parent) {
        DEBUG_PRINT("  --> Internal node, descending...\n");
        if (scale >= 0) DEBUG_PRINT("  --> Pushing to stack: scale=%d, t_max=%.3f\n", scale, t_max);
        DEBUG_PRINT("  --> child_shift_idx=%d, validMask=0x%02X, mask_before=0x%02X, valid_before=0x%02X, child_offset=%u\n",
                    child_shift_idx, validMask, mask_before, valid_before, child_offset);
        DEBUG_PRINT("  --> parent->childPointer=%u, child_index=%u\n", childPointer, child_index);
        DEBUG_PRINT("  --> New parent=%p\n", new_parent);
    }

    // Debug helper: Print advance operation
    inline void debugAdvance(int step_mask, float tc_max, int old_idx, int new_idx) {
        DEBUG_PRINT("  --> ADVANCE: step_mask=%d, tc_max=%.3f\n", step_mask, tc_max);
        DEBUG_PRINT("  --> idx: %d -> %d\n", old_idx, new_idx);
    }
} // anonymous namespace

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
    // IMPORTANT: Use ORIGINAL rayDir for sign tests, not rayDirSafe (which has epsilon modifications)
    int octant_mask = 7;
    debugOctantMirroring(rayDir, rayDirSafe, octant_mask);
    if (rayDir.x > 0.0f) octant_mask ^= 1, tx_bias = 3.0f * tx_coef - tx_bias;
    if (rayDir.y > 0.0f) octant_mask ^= 2, ty_bias = 3.0f * ty_coef - ty_bias;
    if (rayDir.z > 0.0f) octant_mask ^= 4, tz_bias = 3.0f * tz_coef - tz_bias;

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
    // idx represents the child in MIRRORED coordinate system
    // octant_mask tells us which axes are mirrored: mirrored_coord = 3 - coord
    // We need to check which half the entry point is in AFTER applying mirroring

    float mirroredX = (octant_mask & 1) ? (3.0f - normOrigin.x) : normOrigin.x;
    float mirroredY = (octant_mask & 2) ? (3.0f - normOrigin.y) : normOrigin.y;
    float mirroredZ = (octant_mask & 4) ? (3.0f - normOrigin.z) : normOrigin.z;

    if (mirroredX > 1.5f) idx ^= 1, pos.x = 1.5f;
    if (mirroredY > 1.5f) idx ^= 2, pos.y = 1.5f;
    if (mirroredZ > 1.5f) idx ^= 4, pos.z = 1.5f;

    glm::vec3 mirroredPos(mirroredX, mirroredY, mirroredZ);
    debugInitialState(normOrigin, mirroredPos, octant_mask, idx, pos);

    // Main traversal loop
    // Traverse voxels along the ray while staying within octree bounds
    int iter = 0;
    const int maxIter = 10000; // Safety limit

    while (scale < CAST_STACK_DEPTH && iter < maxIter) {
        ++iter;
        debugIterationState(iter, scale, idx, octant_mask, t_min, t_max, pos, scale_exp2, parent, child_descriptor);

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
        // For axis-parallel rays, ignore that axis (set to infinity)
        constexpr float axis_parallel_threshold = 1e-5f;
        float tx_corner = (std::abs(rayDir.x) > axis_parallel_threshold) ?
                          (pos.x * tx_coef - tx_bias) : std::numeric_limits<float>::infinity();
        float ty_corner = (std::abs(rayDir.y) > axis_parallel_threshold) ?
                          (pos.y * ty_coef - ty_bias) : std::numeric_limits<float>::infinity();
        float tz_corner = (std::abs(rayDir.z) > axis_parallel_threshold) ?
                          (pos.z * tz_coef - tz_bias) : std::numeric_limits<float>::infinity();
        float tc_max = std::min({tx_corner, ty_corner, tz_corner});

        // ====================================================================
        // ADOPTED FROM: cuda/Raycast.inl lines 174-232
        // Check if voxel is valid, test termination, descend or advance
        // ====================================================================

        // Permute child slots based on octant mirroring
        int child_shift = idx ^ octant_mask;
        uint32_t child_masks = static_cast<uint32_t>(child_descriptor) << child_shift;

        // Check if THIS specific child exists in parent's validMask
        // child_shift gives us the UNMIRRORED child index (physical index in octree data)
        bool child_valid = (parent->validMask & (1u << child_shift)) != 0;
        bool child_is_leaf = (parent->leafMask & (1u << child_shift)) != 0;

        debugChildValidity(child_shift, child_masks,
                          (child_masks & 0x8000) != 0, child_valid,
                          (child_masks & 0x0080) == 0, child_is_leaf,
                          t_min, t_max);

        // Check if voxel is valid (valid mask bit is set) and t-span is non-empty
        if (child_valid && t_min <= t_max)
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
                debugValidVoxel(t_min, tv_max);

                // Check if this is a leaf
                if (child_is_leaf) {
                    debugLeafHit(scale);

                    // ============================================================
                    // BRICK TRAVERSAL: Check if leaf has brick reference
                    // ============================================================

                    // Compute parent descriptor index in childDescriptors array
                    // This requires tracking the descriptor index during traversal
                    // For now, check if brick references array exists and is non-empty
                    const auto& brickRefs = m_octree->root->brickReferences;

                    // TODO: Track descriptor index during traversal to lookup correct brick
                    // For initial implementation, check if bricks are enabled globally
                    const bool bricksEnabled = !brickRefs.empty();

                    if (bricksEnabled) {
                        // Attempt brick traversal
                        // NOTE: This is a simplified implementation that assumes brick references
                        // align with leaf nodes. Full implementation needs proper index tracking.

                        // For now, use first brick reference as test (will be fixed with proper indexing)
                        const BrickReference& brickRef = brickRefs[0];

                        if (brickRef.isValid()) {
                            // Compute brick world bounds from leaf voxel position
                            // The leaf voxel spans [pos.x, pos.x+scale_exp2]³ in normalized [1,2] space
                            // Transform to world space
                            glm::vec3 normMin(pos.x, pos.y, pos.z);
                            glm::vec3 normMax = normMin + glm::vec3(scale_exp2);

                            // Un-mirror coordinates back to original space
                            glm::vec3 worldMin = (normMin - glm::vec3(1.0f)) * (m_worldMax - m_worldMin) + m_worldMin;
                            glm::vec3 worldMax = (normMax - glm::vec3(1.0f)) * (m_worldMax - m_worldMin) + m_worldMin;

                            // Apply octant mirroring transformation (reverse)
                            if (octant_mask & 1) {
                                std::swap(worldMin.x, worldMax.x);
                                worldMin.x = m_worldMin.x + (m_worldMax.x - worldMin.x);
                                worldMax.x = m_worldMin.x + (m_worldMax.x - worldMax.x);
                            }
                            if (octant_mask & 2) {
                                std::swap(worldMin.y, worldMax.y);
                                worldMin.y = m_worldMin.y + (m_worldMax.y - worldMin.y);
                                worldMax.y = m_worldMin.y + (m_worldMax.y - worldMax.y);
                            }
                            if (octant_mask & 4) {
                                std::swap(worldMin.z, worldMax.z);
                                worldMin.z = m_worldMin.z + (m_worldMax.z - worldMin.z);
                                worldMax.z = m_worldMin.z + (m_worldMax.z - worldMax.z);
                            }

                            // Brick voxel size = leaf voxel size / brick side length
                            const float leafVoxelSize = scale_exp2 * glm::length(m_worldMax - m_worldMin);
                            const float brickVoxelSize = leafVoxelSize / static_cast<float>(brickRef.getSideLength());

                            // Convert t-values to world space for brick traversal
                            const float worldSizeLength = glm::length(m_worldMax - m_worldMin);
                            const float t_min_world = tEntry + t_min * worldSizeLength;
                            const float tv_max_world = tEntry + tv_max * worldSizeLength;

                            // Traverse brick
                            auto brickHit = traverseBrick(brickRef, worldMin, brickVoxelSize,
                                                         origin, rayDir, t_min_world, tv_max_world);

                            if (brickHit.has_value()) {
                                // Brick hit! Return it
                                return brickHit.value();
                            }

                            // Brick traversal returned no hit - continue octree traversal
                            // Fall through to ADVANCE/POP logic
                        }
                    }

                    // ============================================================
                    // FALLBACK: No brick or brick missed - return leaf hit
                    // ============================================================

                    // Leaf voxel hit! Return intersection
                    // Convert normalized [1,2] t-values back to world t-values
                    // In [1,2] space, distance 1.0 corresponds to worldSize in world space
                    float worldSizeLength = glm::length(m_worldMax - m_worldMin);
                    float t_min_world = tEntry + t_min * worldSizeLength;
                    float tv_max_world = tEntry + tv_max * worldSizeLength;

                    ISVOStructure::RayHit hit{};
                    hit.hit = true;
                    hit.tMin = t_min_world;
                    hit.tMax = tv_max_world;
                    hit.position = origin + rayDir * t_min_world;
                    // Depth is how many times we've descended + 1 (for the leaf we're about to enter)
                    // scale starts at CAST_STACK_DEPTH-1, decrements with each descent
                    // Leaf at scale S is at depth = (CAST_STACK_DEPTH-1) - S + 1 = CAST_STACK_DEPTH - S
                    hit.scale = CAST_STACK_DEPTH - scale;
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
                // Reference: Raycast.inl:256 - ofs += popc8(child_masks & 0x7F)
                int child_shift_idx = idx ^ octant_mask;

                // child_masks has validMask in bits 8-15 after left shift by child_shift
                // We need to count valid bits BEFORE the current child position
                // Mask out bits at and after position child_shift (which is at bit 15 - child_shift in child_masks)
                uint32_t mask_before_child = (1u << child_shift_idx) - 1; // e.g., for idx 7: 0x7F
                uint32_t valid_before_child = parent->validMask & mask_before_child;
                uint32_t child_offset = std::popcount(valid_before_child);

                // Update parent pointer to point to child
                // In our structure, childPointer is an index into childDescriptors array
                uint32_t child_index = parent->childPointer + child_offset;

                // Bounds check
                if (child_index >= m_octree->root->childDescriptors.size()) {
                    break; // Invalid child pointer - exit loop
                }

                const ChildDescriptor* new_parent = &m_octree->root->childDescriptors[child_index];
                debugDescend(scale, t_max, child_shift_idx, parent->validMask,
                           mask_before_child, valid_before_child, child_offset,
                           parent->childPointer, child_index, new_parent);
                parent = new_parent;

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
        int old_idx = idx;
        idx ^= step_mask;
        debugAdvance(step_mask, tc_max, old_idx, idx);

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

// ============================================================================
// Brick DDA Traversal Implementation
// ============================================================================

/**
 * 3D DDA ray traversal through dense brick voxels.
 *
 * Based on Amanatides & Woo (1987) "A Fast Voxel Traversal Algorithm for Ray Tracing"
 * with adaptations for brick-based octree storage.
 *
 * Key concepts:
 * - tDelta: Ray parameter increment to cross one voxel along each axis
 * - tNext: Ray parameter to next voxel boundary on each axis
 * - step: Direction to advance (+1 or -1) per axis
 * - currentVoxel: Integer coordinates [0, N-1]³ in brick space
 *
 * The algorithm steps through the brick voxel grid, testing the minimum tNext
 * each iteration to determine which axis boundary to cross next.
 */
std::optional<ISVOStructure::RayHit> LaineKarrasOctree::traverseBrick(
    const BrickReference& brickRef,
    const glm::vec3& brickWorldMin,
    float brickVoxelSize,
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDir,
    float tMin,
    float tMax) const
{
    // Brick dimensions
    const int brickN = brickRef.getSideLength();  // 2^depth (e.g., 8 for depth=3)

    // 1. Compute ray entry point into brick
    const glm::vec3 entryPoint = rayOrigin + rayDir * tMin;

    // 2. Transform entry point to brick-local [0, N]³ space
    const glm::vec3 localEntry = (entryPoint - brickWorldMin) / brickVoxelSize;

    // 3. Initialize current voxel (integer coordinates)
    glm::ivec3 currentVoxel{
        static_cast<int>(std::floor(localEntry.x)),
        static_cast<int>(std::floor(localEntry.y)),
        static_cast<int>(std::floor(localEntry.z))
    };

    // Clamp to brick bounds [0, N-1]
    currentVoxel = glm::clamp(currentVoxel, glm::ivec3(0), glm::ivec3(brickN - 1));

    // 4. Compute DDA step directions and tDelta
    glm::ivec3 step;
    glm::vec3 tDelta;  // Ray parameter to cross one voxel
    glm::vec3 tNext;   // Ray parameter to next voxel boundary

    constexpr float epsilon = 1e-8f;

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(rayDir[axis]) < epsilon) {
            // Ray parallel to axis - never crosses voxel boundaries on this axis
            step[axis] = 0;
            tDelta[axis] = std::numeric_limits<float>::max();
            tNext[axis] = std::numeric_limits<float>::max();
        } else {
            // Determine step direction
            step[axis] = (rayDir[axis] > 0.0f) ? 1 : -1;

            // tDelta = voxel size / |ray direction component|
            tDelta[axis] = brickVoxelSize / std::abs(rayDir[axis]);

            // tNext = ray parameter to next voxel boundary
            // For positive direction: distance to upper boundary
            // For negative direction: distance to lower boundary
            if (rayDir[axis] > 0.0f) {
                // Distance from entry point to next upper boundary
                const float nextBoundary = (currentVoxel[axis] + 1) * brickVoxelSize;
                const float distToNextBoundary = nextBoundary - (entryPoint[axis] - brickWorldMin[axis]);
                tNext[axis] = tMin + distToNextBoundary / rayDir[axis];
            } else {
                // Distance from entry point to next lower boundary
                const float nextBoundary = currentVoxel[axis] * brickVoxelSize;
                const float distToNextBoundary = (entryPoint[axis] - brickWorldMin[axis]) - nextBoundary;
                tNext[axis] = tMin + distToNextBoundary / std::abs(rayDir[axis]);
            }
        }
    }

    // 5. DDA march through brick voxels
    int maxSteps = brickN * 3;  // Safety limit (diagonal traversal)
    int stepCount = 0;

    while (stepCount < maxSteps) {
        ++stepCount;

        // Check if current voxel is in bounds
        if (currentVoxel.x < 0 || currentVoxel.x >= brickN ||
            currentVoxel.y < 0 || currentVoxel.y >= brickN ||
            currentVoxel.z < 0 || currentVoxel.z >= brickN) {
            // Exited brick bounds
            return std::nullopt;
        }

        // 6. Sample brick voxel for occupancy
        // NOTE: For now, we assume all brick voxels are occupied (solid brick).
        // In a full implementation, this would query BrickStorage:
        //   const size_t localIdx = brickStorage.getIndex(currentVoxel.x, currentVoxel.y, currentVoxel.z);
        //   const float density = brickStorage.get<0>(brickRef.brickID, localIdx);
        //   if (density > densityThreshold) { ... }
        //
        // For initial implementation, treat first voxel as hit (testing transitions)
        {
            // Hit! Compute hit position and return
            const float hitT = std::min({tNext.x, tNext.y, tNext.z}) - tDelta[0] * 0.5f; // Approximate center
            const glm::vec3 hitPosition = rayOrigin + rayDir * hitT;

            // Compute normal (simple approximation: which face was entered)
            glm::vec3 normal(0.0f);
            const float minT = std::min({tNext.x, tNext.y, tNext.z});
            if (std::abs(tNext.x - minT) < epsilon) {
                normal = glm::vec3(step.x > 0 ? -1.0f : 1.0f, 0.0f, 0.0f);
            } else if (std::abs(tNext.y - minT) < epsilon) {
                normal = glm::vec3(0.0f, step.y > 0 ? -1.0f : 1.0f, 0.0f);
            } else {
                normal = glm::vec3(0.0f, 0.0f, step.z > 0 ? -1.0f : 1.0f);
            }

            ISVOStructure::RayHit hit;
            hit.hit = true;
            hit.tMin = hitT;
            hit.tMax = hitT + tDelta[0] * 0.1f;  // Small span for brick voxel
            hit.position = hitPosition;
            hit.normal = normal;
            hit.scale = CAST_STACK_DEPTH - 1;  // Finest detail level

            return hit;
        }

        // 7. Advance to next voxel (step along axis with minimum tNext)
        if (tNext.x < tNext.y && tNext.x < tNext.z) {
            // Cross X boundary
            if (tNext.x > tMax) return std::nullopt;  // Exceeded ray span
            currentVoxel.x += step.x;
            tNext.x += tDelta.x;
        } else if (tNext.y < tNext.z) {
            // Cross Y boundary
            if (tNext.y > tMax) return std::nullopt;
            currentVoxel.y += step.y;
            tNext.y += tDelta.y;
        } else {
            // Cross Z boundary
            if (tNext.z > tMax) return std::nullopt;
            currentVoxel.z += step.z;
            tNext.z += tDelta.z;
        }
    }

    // Exceeded step limit (shouldn't happen for reasonable brick sizes)
    return std::nullopt;
}

} // namespace SVO
