#define NOMINMAX
#include "LaineKarrasOctree.h"
#include "VoxelComponents.h"  // For GaiaVoxel components
#include "ComponentData.h"
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

    /**
     * Compute surface normal via central differencing
     *
     * Uses 6-sample gradient computation (standard in graphics):
     * gradient = (sample_neg - sample_pos) for each axis
     *
     * Only 6 voxel queries vs 27 for full neighborhood,
     * while still capturing actual surface geometry.
     */
    inline glm::vec3 computeSurfaceNormal(
        const LaineKarrasOctree* octree,
        const glm::vec3& hitPos,
        float voxelSize)
    {
        // Sample along each axis (6 samples total)
        const float offset = voxelSize * 0.5f;  // Half voxel for better accuracy

        bool xPos = octree->voxelExists(hitPos + glm::vec3(offset, 0.0f, 0.0f), 0);
        bool xNeg = octree->voxelExists(hitPos - glm::vec3(offset, 0.0f, 0.0f), 0);
        bool yPos = octree->voxelExists(hitPos + glm::vec3(0.0f, offset, 0.0f), 0);
        bool yNeg = octree->voxelExists(hitPos - glm::vec3(0.0f, offset, 0.0f), 0);
        bool zPos = octree->voxelExists(hitPos + glm::vec3(0.0f, 0.0f, offset), 0);
        bool zNeg = octree->voxelExists(hitPos - glm::vec3(0.0f, 0.0f, offset), 0);

        // Compute gradient (points from solid to empty)
        // If xPos is occupied (1) and xNeg is empty (0), gradient.x = 0 - 1 = -1 (points toward -X)
        glm::vec3 gradient(
            static_cast<float>(xNeg) - static_cast<float>(xPos),
            static_cast<float>(yNeg) - static_cast<float>(yPos),
            static_cast<float>(zNeg) - static_cast<float>(zPos)
        );

        // Normalize if non-zero
        float length = glm::length(gradient);
        if (length > 1e-6f) {
            return gradient / length;
        }

        // Fallback: return upward normal
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
} // anonymous namespace

// NEW: Entity-based constructor
LaineKarrasOctree::LaineKarrasOctree(::GaiaVoxel::GaiaVoxelWorld& voxelWorld, int maxLevels, int brickDepthLevels)
    : m_voxelWorld(&voxelWorld)
    , m_registry(nullptr)  // No AttributeRegistry in entity-based mode
    , m_maxLevels(maxLevels)
    , m_brickDepthLevels(brickDepthLevels)
{
    // SVO stores only entity IDs (8 bytes each), not voxel data
    // Caller reads entity components via m_voxelWorld
}

LaineKarrasOctree::~LaineKarrasOctree() = default;

// ============================================================================
// Entity-Based Insertion API
// ============================================================================

void LaineKarrasOctree::insert(gaia::ecs::Entity entity) {
    using namespace GaiaVoxel;

    if (!m_voxelWorld) {
        return;  // Entity mode not enabled
    }

    // 1. Extract position from entity's MortonKey component
    auto posOpt = m_voxelWorld->getPosition(entity);
    if (!posOpt) {
        return; // Entity must have MortonKey for spatial indexing
    }

    glm::vec3 position = *posOpt;

    // 2. Compute Morton key for spatial indexing
    GaiaVoxel::MortonKey key = GaiaVoxel::MortonKeyUtils::fromPosition(position);

    // 3. For Phase 3, we're not doing actual octree insertion yet
    //    Just store the entity mapping for ray casting retrieval
    //    Phase 3 will implement proper additive insertion into octree structure

    // Store entity in map with Morton code as key
    // This allows ray casting to retrieve entities by hit position
    m_leafEntityMap[key.code] = entity;

    std::cout << "[LaineKarrasOctree] Inserted entity " << entity.id()
              << " at position (" << position.x << ", " << position.y << ", " << position.z << ")"
              << " with Morton code " << key.code << "\n";
}

void LaineKarrasOctree::remove(gaia::ecs::Entity entity) {
    // TODO: Implement entity removal
    // This will find and remove entity from leaf entity map
    if (!m_voxelWorld) {
        return;
    }

    // Find entity in leaf map and remove
    // Update octree structure if needed
}

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

/**
 * Ensure octree is initialized for additive insertion.
 * Creates empty root if needed.
 */
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

    // Prevent divide-by-zero with epsilon
    // NOTE: ESVO uses exp2f(-CAST_STACK_DEPTH) where CAST_STACK_DEPTH=23
    // This gives epsilon ≈ 1.19e-07, but for axis-parallel rays this creates
    // extreme coefficient values (±8.4e+06) that corrupt tc_max/tv_max.
    // Use larger epsilon to reduce numerical issues.
    constexpr float epsilon = 1e-5f; // Larger than ESVO's 2^-23 to avoid extreme coefficients
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

    // Initialize traversal stack
    // Reference: lines 127-134
    CastStack stack;

    // Safety check: ensure octree has root descriptor
    if (m_octree->root->childDescriptors.empty()) {
        // DEBUG: std::cout << "DEBUG: Empty octree, returning miss\n";
        return miss;
    }

    // Initialize stack with root descriptor at all ESVO scales
    // Stack indexing uses ESVO scale, not user scale
    // This ensures POP operations can always find a valid parent
    const ChildDescriptor* rootDesc = &m_octree->root->childDescriptors[0];
    const int minScale = ESVO_MAX_SCALE - m_maxLevels + 1;
    for (int esvoScale = minScale; esvoScale <= ESVO_MAX_SCALE; esvoScale++) {
        stack.push(esvoScale, rootDesc, t_max);
    }

    std::cout << "DEBUG TRAVERSAL START: origin=(" << origin.x << "," << origin.y << "," << origin.z << ") "
              << "dir=(" << rayDir.x << "," << rayDir.y << "," << rayDir.z << ") "
              << "tEntry=" << tEntry << " tExit=" << tExit << "\n";
    std::cout << "DEBUG INIT: tx_coef=" << tx_coef << " ty_coef=" << ty_coef << " tz_coef=" << tz_coef << "\n";
    std::cout << "DEBUG INIT: tx_bias=" << tx_bias << " ty_bias=" << ty_bias << " tz_bias=" << tz_bias << "\n";
    std::cout << "DEBUG INIT: t_min=" << t_min << " t_max=" << t_max << "\n";
    // Initialize traversal with ESVO scale (always 22 at root, regardless of user depth)
    // This allows ESVO's bit manipulation to work correctly for any octree depth
    int esvoScale = ESVO_MAX_SCALE;
    std::cout << "DEBUG INIT: m_maxLevels=" << m_maxLevels << " esvoScale=" << esvoScale << " (userScale=" << esvoToUserScale(esvoScale) << ")\n";

    const ChildDescriptor* parent = &m_octree->root->childDescriptors[0];
    uint64_t child_descriptor = 0; // Invalid until fetched
    int idx = 0; // Child octant index
    glm::vec3 pos(1.0f, 1.0f, 1.0f); // Position in normalized [1,2] space
    int scale = esvoScale; // ESVO internal scale (22 at root)
    float scale_exp2 = 0.5f; // 2^(scale - ESVO_MAX_SCALE)

    // Select initial child based on ray entry point
    // Reference: ESVO lines 136-138
    // For axis-parallel rays with extreme coefficients, use position comparison
    // For diagonal rays, parametric formula works correctly
    const float EXTREME_COEF_THRESHOLD = 1000.0f;
    const bool use_position = std::abs(ty_coef) > EXTREME_COEF_THRESHOLD ||
                              std::abs(tz_coef) > EXTREME_COEF_THRESHOLD;

    if (use_position) {
        // Position-based (handles axis-parallel edge case)
        if (normOrigin.x > 1.5f) idx ^= 1, pos.x = 1.5f;
        if (normOrigin.y > 1.5f) idx ^= 2, pos.y = 1.5f;
        if (normOrigin.z > 1.5f) idx ^= 4, pos.z = 1.5f;
    } else {
        // Parametric formula (standard case)
        if (1.5f * tx_coef - tx_bias > t_min) idx ^= 1, pos.x = 1.5f;
        if (1.5f * ty_coef - ty_bias > t_min) idx ^= 2, pos.y = 1.5f;
        if (1.5f * tz_coef - tz_bias > t_min) idx ^= 4, pos.z = 1.5f;
    }

    // DEBUG: std::cout << "DEBUG ROOT SETUP: normOrigin=(" << normOrigin.x << "," << normOrigin.y << "," << normOrigin.z << ")\n";
    // DEBUG: std::cout << "  octant_mask=" << octant_mask << " idx=" << idx << " pos=(" << pos.x << "," << pos.y << "," << pos.z << ")\n";

    // Main traversal loop
    // Traverse voxels along the ray while staying within octree bounds
    int iter = 0;
    const int maxIter = 10000; // Safety limit

    // Declare center values at function scope for POP restoration
    float tx_center = 0.0f;
    float ty_center = 0.0f;
    float tz_center = 0.0f;

    // Loop while in valid ESVO scale range
    // Max ESVO scale = ESVO_MAX_SCALE (22)
    // Min ESVO scale = ESVO_MAX_SCALE - m_maxLevels + 1 (e.g., 15 for depth 8, 0 for depth 23)
    int minESVOScale = ESVO_MAX_SCALE - m_maxLevels + 1;
    while (scale >= minESVOScale && iter < maxIter) {
        ++iter;
        // debugIterationState(iter, scale, idx, octant_mask, t_min, t_max, pos, scale_exp2, parent, child_descriptor);

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
        // Reference: Raycast.inl:166-169
        float tx_corner = pos.x * tx_coef - tx_bias;
        float ty_corner = pos.y * ty_coef - ty_bias;
        float tz_corner = pos.z * tz_coef - tz_bias;
        float tc_max = std::min({tx_corner, ty_corner, tz_corner});

        // NOTE: Do NOT clamp tc_max here! It's used to update t_min in ADVANCE step.
        // Extreme negative values from axis-parallel rays are handled in tv_max calculation.

        // ====================================================================
        // ADOPTED FROM: cuda/Raycast.inl lines 174-232
        // Check if voxel is valid, test termination, descend or advance
        // ====================================================================

        // Permute child slots based on octant mirroring
        int child_shift = idx ^ octant_mask;
        uint32_t child_masks = static_cast<uint32_t>(child_descriptor) << child_shift;

        // Check if THIS specific child exists in parent's validMask
        // For dynamically-inserted octrees stored in PHYSICAL space, use idx directly
        // (ESVO pre-built octrees may use different layout - needs verification)
        bool child_valid = (parent->validMask & (1u << idx)) != 0;
        bool child_is_leaf = (parent->leafMask & (1u << idx)) != 0;

        // Debug specific problem parent
        if ((parent - &m_octree->root->childDescriptors[0]) == 80) {
            // DEBUG: std::cout << "DEBUG PARENT 80: scale=" << scale << " idx=" << idx << " child_shift=" << child_shift
            //          << " validMask=0x" << std::hex << (int)parent->validMask << std::dec
            //          << " leafMask=0x" << std::hex << (int)parent->leafMask << std::dec
            //          << " child_valid=" << child_valid << " child_is_leaf=" << child_is_leaf << "\n";
        }

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

            // BUGFIX: For axis-parallel rays, tc_max is dominated by extreme negative values.
            // Use only valid corner values (those within threshold) for tv_max calculation.
            constexpr float corner_threshold = 1000.0f;
            float tx_valid = (std::abs(tx_corner) < corner_threshold) ? tx_corner : t_max;
            float ty_valid = (std::abs(ty_corner) < corner_threshold) ? ty_corner : t_max;
            float tz_valid = (std::abs(tz_corner) < corner_threshold) ? tz_corner : t_max;
            float tc_max_corrected = std::min({tx_valid, ty_valid, tz_valid});
            float tv_max = std::min(t_max, tc_max_corrected);
            float half = scale_exp2 * 0.5f;
            tx_center = half * tx_coef + tx_corner;
            ty_center = half * ty_coef + ty_corner;
            tz_center = half * tz_coef + tz_corner;

            if (scale >= m_maxLevels - 2) {
                // DEBUG: std::cout << "DEBUG CENTER CALC scale=" << scale << ": pos=(" << pos.x << "," << pos.y << "," << pos.z << ") half=" << half << "\n";
                // DEBUG: std::cout << "  tx_coef=" << tx_coef << " tx_corner=" << tx_corner << " tx_center=" << tx_center << "\n";
                // DEBUG: std::cout << "  ty_coef=" << ty_coef << " ty_corner=" << ty_corner << " ty_center=" << ty_center << "\n";
            }

            // ================================================================
            // CONTOUR INTERSECTION (Reference lines 196-220)
            // For now, skip contours (will port in future iteration)
            // This means t_min stays as-is, tv_max is just min(t_max, tc_max)
            // ================================================================

            // Descend to first child if resulting t-span is non-empty
            if (scale >= m_maxLevels - 3) {
                // DEBUG: std::cout << "DEBUG DESCEND CHECK scale=" << scale << ": t_min=" << t_min << " tv_max=" << tv_max
                //          << " → " << (t_min <= tv_max ? "DESCENDING" : "SKIPPING (t_min > tv_max)") << "\n";
            }
            if (t_min <= tv_max)
            {
                debugValidVoxel(t_min, tv_max);

                // Check if this is a leaf
                if (child_is_leaf) {
                    debugLeafHit(scale);

                    // Debug: print leaf position and scale
                    std::cout << "[LEAF HIT] scale=" << scale << " pos=(" << pos.x << "," << pos.y << "," << pos.z << ") "
                              << "t_min=" << t_min << " tv_max=" << tv_max << "\n";  // TODO Phase 3: Add brickViews.size()

                    // ============================================================
                    // BRICK TRAVERSAL: EntityBrickView-based DDA (Phase 3)
                    // ============================================================

                    const auto& brickViews = m_octree->root->brickViews;
                    size_t descriptorIndex = parent - &m_octree->root->childDescriptors[0];
                    const bool hasBricks = !brickViews.empty() && descriptorIndex < brickViews.size();

                    if (hasBricks) {
                        // EntityBrickView-based brick traversal
                        const auto& brickView = brickViews[descriptorIndex];

                        // Get brick metadata
                        uint8_t brickDepth = brickView.getDepth();
                        size_t brickSideLength = 1u << brickDepth;  // 2^depth voxels per side

                        std::cout << "[BRICK CHECK] descriptorIndex=" << descriptorIndex
                                  << " brickDepth=" << (int)brickDepth
                                  << " brickSize=" << brickSideLength << "^3\n";

                        // Compute brick world bounds from leaf voxel position
                        // The leaf voxel spans [pos.x, pos.x+scale_exp2]³ in normalized [1,2] space
                        glm::vec3 normMin(pos.x, pos.y, pos.z);
                        glm::vec3 normMax = normMin + glm::vec3(scale_exp2);

                        // Transform to world space
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

                        // Brick voxel size calculation:
                        // - Leaf voxel spans scale_exp2 in normalized [1,2] space
                        // - Normalized [1,2] space maps to world extent (assumes uniform cube)
                        // - Brick subdivides leaf into brickSideLength³ voxels
                        const float worldExtent = m_worldMax.x - m_worldMin.x;  // Uniform cube assumption
                        const float leafVoxelSize = scale_exp2 * worldExtent;
                        const float brickVoxelSize = leafVoxelSize / static_cast<float>(brickSideLength);

                        // Compute ray-brick AABB intersection
                        glm::vec3 invDir;
                        for (int i = 0; i < 3; i++) {
                            if (std::abs(rayDir[i]) < 1e-8f) {
                                invDir[i] = (rayDir[i] >= 0) ? 1e8f : -1e8f;
                            } else {
                                invDir[i] = 1.0f / rayDir[i];
                            }
                        }
                        glm::vec3 t0 = (worldMin - origin) * invDir;
                        glm::vec3 t1 = (worldMax - origin) * invDir;

                        glm::vec3 tNear = glm::min(t0, t1);
                        glm::vec3 tFar = glm::max(t0, t1);

                        float brickTMin = glm::max(glm::max(tNear.x, tNear.y), tNear.z);
                        float brickTMax = glm::min(glm::min(tFar.x, tFar.y), tFar.z);
                        brickTMin = glm::max(brickTMin, tEntry);

                        std::cout << "[BRICK BOUNDS] worldMin=(" << worldMin.x << "," << worldMin.y << "," << worldMin.z
                                  << ") worldMax=(" << worldMax.x << "," << worldMax.y << "," << worldMax.z
                                  << ") brickTMin=" << brickTMin << " brickTMax=" << brickTMax << "\n";

                        // Traverse brick using EntityBrickView
                        auto brickHit = traverseBrickView(brickView, worldMin, brickVoxelSize,
                                                          origin, rayDir, brickTMin, brickTMax);

                        if (brickHit.has_value()) {
                            std::cout << "[BRICK HIT] Returning brick hit at t=" << brickHit.value().tMin << "\n";
                            return brickHit.value();
                        } else {
                            std::cout << "[BRICK MISS] No hit in brick, falling back to leaf hit\n";
                        }
                    }

                    // ============================================================
                    // FALLBACK: No brick or brick missed - return leaf hit
                    // ============================================================

                    // Leaf voxel hit! Return intersection
                    // Convert normalized [1,2] t-values back to world t-values
                    // CRITICAL: For axis-aligned rays, use the DOMINANT axis for conversion!
                    // The normalized t-values are along the ray direction, not isotropic.
                    glm::vec3 worldSize = m_worldMax - m_worldMin;

                    // Determine dominant axis based on ray direction
                    float worldSizeLength;
                    glm::vec3 absDir = glm::abs(rayDir);
                    if (absDir.x > absDir.y && absDir.x > absDir.z) {
                        worldSizeLength = worldSize.x;  // X dominant
                    } else if (absDir.y > absDir.z) {
                        worldSizeLength = worldSize.y;  // Y dominant
                    } else {
                        worldSizeLength = worldSize.z;  // Z dominant
                    }

                    float t_min_world = tEntry + t_min * worldSizeLength;
                    float tv_max_world = tEntry + tv_max * worldSizeLength;

                    // DEBUG: std::cout << "DEBUG LEAF HIT: tEntry=" << tEntry << " t_min=" << t_min
                    //          << " worldSizeLength=" << worldSizeLength
                    //          << " t_min_world=" << t_min_world << "\n";
                    // DEBUG: std::cout << "  origin=(" << origin.x << "," << origin.y << "," << origin.z << ")"
                    //          << " rayDir=(" << rayDir.x << "," << rayDir.y << "," << rayDir.z << ")\n";
                    // DEBUG: std::cout << "  computed hit pos=" << (origin + rayDir * t_min_world).x << ","
                    //          << (origin + rayDir * t_min_world).y << ","
                    //          << (origin + rayDir * t_min_world).z << "\n";

                    ISVOStructure::RayHit hit{};
                    hit.hit = true;
                    hit.tMin = t_min_world;
                    hit.tMax = tv_max_world;
                    hit.hitPoint = origin + rayDir * t_min_world;  // Renamed from position
                    hit.scale = esvoToUserScale(scale);

                    // Compute gradient-based surface normal from 6-neighbor sampling
                    float voxelSize = getVoxelSize(hit.scale);
                    hit.normal = computeSurfaceNormal(this, hit.hitPoint, voxelSize);

                    // NEW: Look up entity from leaf entity map
                    // Try two lookup strategies:
                    // 1. By descriptor index (for voxels inserted via old API)
                    // 2. By Morton code computed from hit position (for voxels inserted via new entity API)

                    if (m_voxelWorld != nullptr) {
                        using namespace GaiaVoxel;

                        // Strategy 1: Descriptor index lookup
                        size_t leafDescIndex = parent - &m_octree->root->childDescriptors[0];
                        auto it = m_leafEntityMap.find(leafDescIndex);
                        if (it != m_leafEntityMap.end()) {
                            hit.entity = it->second;
                        } else {
                            // Strategy 2: Morton code lookup (for entity-based insertion)
                            // Compute Morton key from hit position
                            GaiaVoxel::MortonKey mortonCode = GaiaVoxel::MortonKeyUtils::fromPosition(hit.hitPoint);
                            auto it2 = m_leafEntityMap.find(mortonCode.code);
                            if (it2 != m_leafEntityMap.end()) {
                                hit.entity = it2->second;
                            } else {
                                // No entity found - create invalid entity
                                hit.entity = gaia::ecs::Entity();
                            }
                        }
                    } else {
                        // No voxel world - create invalid entity
                        hit.entity = gaia::ecs::Entity();
                    }

                    return hit;
                }

                // ============================================================
                // PUSH: Internal node, descend to children
                // Reference lines 233-246
                // ============================================================

                // Push current state onto stack before descending
                // ESVO uses scale-indexed stack: stack.nodes[scale] = parent
                if (tc_max < h) {
                    stack.push(scale, parent, t_max);
                }
                h = tc_max;

                // ============================================================
                // DESCEND: Complete child selection and update traversal state
                // Reference lines 248-274
                // ============================================================

                // Calculate offset to child based on popcount of NON-LEAF mask
                // ESVO childPointer points to array of ONLY non-leaf children
                // Count how many NON-LEAF children come before current child
                // Reference: OctreeRuntime.cpp:1124 - childIdx = popc8(nonLeafMask & (cmask - 1))
                // For dynamically-inserted octrees in PHYSICAL space, use idx directly
                int child_shift_idx = idx;

                // Compute nonLeafMask (inverse of leafMask)
                uint8_t nonLeafMask = ~parent->leafMask & parent->validMask;

                // Count non-leaf children BEFORE current child
                uint32_t mask_before_child = (1u << child_shift_idx) - 1; // e.g., for idx 7: 0x7F
                uint32_t nonleaf_before_child = nonLeafMask & mask_before_child;
                uint32_t child_offset = std::popcount(nonleaf_before_child);

                // Update parent pointer to point to child
                // In our structure, childPointer is an index into childDescriptors array
                uint32_t child_index = parent->childPointer + child_offset;

                // Bounds check
                if (child_index >= m_octree->root->childDescriptors.size()) {
                    // DEBUG: std::cout << "DEBUG: Invalid child_index=" << child_index << " >= size=" << m_octree->root->childDescriptors.size() << ", breaking\n";
                    break; // Invalid child pointer - exit loop
                }

                const ChildDescriptor* new_parent = &m_octree->root->childDescriptors[child_index];

                // Debug DESCEND at all levels
                // DEBUG: std::cout << "DEBUG DESCEND scale=" << scale << ": parent=" << (parent - &m_octree->root->childDescriptors[0])
                //          << " idx=" << idx << " nonLeafMask=0x" << std::hex << (int)nonLeafMask << std::dec
                //          << " child_offset=" << child_offset << " → child_index=" << child_index << "\n";

                debugDescend(scale, t_max, child_shift_idx, nonLeafMask,
                           mask_before_child, nonleaf_before_child, child_offset,
                           parent->childPointer, child_index, new_parent);
                parent = new_parent;

                // Descend to next level
                idx = 0;
                scale--;
                scale_exp2 = half;

                // DEBUG: std::cout << "DEBUG: After descent, scale=" << scale << " idx=" << idx << " t_min=" << t_min << "\n";

                // ================================================================
                // Octant Selection: Use PARENT's center values
                // Reference: Raycast.inl:265-267
                // ================================================================
                // CRITICAL: ESVO uses the PARENT's tx_center values (computed before DESCEND)
                // to select which child octant the ray is in. We should NOT recompute them!
                // The tx_center values from before DESCEND tell us where the ray is relative
                // to the PARENT's center, which determines which CHILD we're in.

                // DEBUG: std::cout << "DEBUG: Using parent's center values: tx_center=" << tx_center << " ty_center=" << ty_center << " tz_center=" << tz_center << " t_min=" << t_min << "\n";
                // DEBUG: std::cout << "DEBUG: pos=(" << pos.x << "," << pos.y << "," << pos.z << ") scale_exp2=" << scale_exp2 << "\n";

                // ESVO octant selection (Raycast.inl:265-267)
                // Simple parametric formula works for physical storage with octant mirroring
                // The mirroring is handled uniformly by octant_mask, so no special cases needed
                // DEBUG: std::cout << "DEBUG: Before octant selection, idx=" << idx << "\n";
                // DEBUG: std::cout << "  tx_center - t_min = " << (tx_center - t_min)
                //S          << " (tx_center > t_min)=" << (tx_center > t_min) << "\n";
                if (tx_center > t_min) {
                    // DEBUG: std::cout << "  Flipping X bit!\n";
                    idx ^= 1, pos.x += scale_exp2;
                }
                if (ty_center > t_min) {
                    // DEBUG: std::cout << "  Flipping Y bit: " << ty_center << " > " << t_min << "\n";
                    idx ^= 2, pos.y += scale_exp2;
                }
                if (tz_center > t_min) {
                    // DEBUG: std::cout << "  Flipping Z bit: " << tz_center << " > " << t_min << "\n";
                    idx ^= 4, pos.z += scale_exp2;
                }

                // DEBUG: std::cout << "DEBUG: After octant selection, idx=" << idx << " pos=(" << pos.x << "," << pos.y << "," << pos.z << ")\n";

                // Update active t-span and invalidate child descriptor
                t_max = tv_max;
                child_descriptor = 0;
                // DEBUG: std::cout << "DEBUG: CONTINUE after descent, looping back to process child " << idx << "\n";
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
        // BUGFIX: For axis-parallel rays, tc_max is dominated by extreme negative ty/tz_corner values.
        // Use only the valid corner values (those < threshold) to update t_min.
        constexpr float corner_threshold = 1000.0f;
        float tx_valid = (std::abs(tx_corner) < corner_threshold) ? tx_corner : std::numeric_limits<float>::max();
        float ty_valid = (std::abs(ty_corner) < corner_threshold) ? ty_corner : std::numeric_limits<float>::max();
        float tz_valid = (std::abs(tz_corner) < corner_threshold) ? tz_corner : std::numeric_limits<float>::max();
        float tc_max_corrected = std::min({tx_valid, ty_valid, tz_valid});

        // If all values are extreme (fully axis-parallel), use the least extreme one
        if (tc_max_corrected == std::numeric_limits<float>::max()) {
            tc_max_corrected = std::max({tx_corner, ty_corner, tz_corner}); // Pick least negative
        }

        t_min = std::max(tc_max_corrected, 0.0f);

        if (scale == m_maxLevels - 1 || scale == m_maxLevels - 2) {
            // DEBUG: std::cout << "DEBUG ADVANCE: tc_max=" << tc_max << " → t_min=" << t_min
            //          << " (from tx=" << tx_corner << " ty=" << ty_corner << " tz=" << tz_corner << ")\n";
        }

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
            // POP: Ascend hierarchy based on differing position bits
            // This works for ANY depth by using integer arithmetic
            // (replaces ESVO's depth-23 specific float bit tricks)
            // ============================================================

            // ESVO approach: Find highest differing bit between pos and (pos + scale_exp2)
            // to determine how many levels to ascend.
            //
            // For general depths: Convert positions to integers at the finest resolution,
            // then find highest differing bit using integer operations.

            // Convert normalized [1,2] positions to integer coordinates
            // Using ESVO's standard 2^22 resolution for bit manipulation
            // (this is independent of user's m_maxLevels - we always use ESVO scale internally)
            const int MAX_RES = 1 << ESVO_MAX_SCALE;  // 2^22 = 4,194,304

            // Compute integer positions in [0, MAX_RES)
            // Input f is in [0, 1] normalized octree space
            auto floatToInt = [MAX_RES](float f) -> uint32_t {
                return static_cast<uint32_t>(std::max(0.0f, std::min(f * MAX_RES, static_cast<float>(MAX_RES - 1))));
            };

            uint32_t pos_x_int = floatToInt(pos.x);
            uint32_t pos_y_int = floatToInt(pos.y);
            uint32_t pos_z_int = floatToInt(pos.z);

            uint32_t next_x_int = floatToInt(pos.x + scale_exp2);
            uint32_t next_y_int = floatToInt(pos.y + scale_exp2);
            uint32_t next_z_int = floatToInt(pos.z + scale_exp2);

            // Find differing bits (OR of XOR results)
            uint32_t differing_bits = 0;
            if ((step_mask & 1) != 0) differing_bits |= (pos_x_int ^ next_x_int);
            if ((step_mask & 2) != 0) differing_bits |= (pos_y_int ^ next_y_int);
            if ((step_mask & 4) != 0) differing_bits |= (pos_z_int ^ next_z_int);

            if (differing_bits == 0) {
                // No differing bits - we've exited the entire octree
                break;
            }

            // Find highest set bit (determines how many levels to ascend)
            // highest_bit = floor(log2(differing_bits))
            int highest_bit = 31 - std::countl_zero(differing_bits);

            // Convert bit position to ESVO scale level
            // highest_bit tells us the voxel size as a power of 2 in integer space
            // For ESVO space: scale = highest_bit (since finest ESVO scale = 0 corresponds to bit 0)
            scale = highest_bit;

            // Debug POP
            std::cout << "[POP] differing_bits=0x" << std::hex << differing_bits << std::dec
                      << " highest_bit=" << highest_bit << " → esvoScale=" << scale
                      << " (userScale=" << esvoToUserScale(scale) << ")\n";

            // Clamp scale to valid ESVO range [minESVOScale, ESVO_MAX_SCALE]
            if (scale < minESVOScale || scale > ESVO_MAX_SCALE) {
                // Invalid scale - exit traversal
                std::cout << "[POP] Scale " << scale << " outside ESVO range [" << minESVOScale << ", " << ESVO_MAX_SCALE << "], exiting\n";
                break;
            }

            // Recompute scale_exp2 from ESVO scale
            // scale_exp2 = 2^(scale - ESVO_MAX_SCALE)
            int exp_val = scale - ESVO_MAX_SCALE + 127;  // +127 is IEEE 754 exponent bias
            scale_exp2 = std::bit_cast<float>(static_cast<uint32_t>(exp_val << 23));

            std::cout << "[POP] Retrieving from stack: scale=" << scale << "\n";

            // Restore parent descriptor and t_max from stack
            parent = stack.getNode(scale);
            t_max = stack.getTMax(scale);

            std::cout << "[POP] Retrieved parent=" << static_cast<const void*>(parent) << " t_max=" << t_max << "\n";

            if (parent == nullptr) {
                // No parent at this scale - exit traversal
                std::cout << "[POP] parent is null, exiting\n";
                break;
            }

            // Round position to voxel boundary at new ESVO scale
            // This masks off lower bits to snap to grid
            int shift_amount = ESVO_MAX_SCALE - scale;
            if (shift_amount < 0 || shift_amount >= 32) {
                std::cout << "[POP] Invalid shift_amount=" << shift_amount << " (ESVO_MAX_SCALE=" << ESVO_MAX_SCALE << " scale=" << scale << "), exiting\n";
                break;
            }

            uint32_t mask = ~((1u << shift_amount) - 1);
            pos_x_int &= mask;
            pos_y_int &= mask;
            pos_z_int &= mask;

            // Convert back to float
            auto intToFloat = [MAX_RES](uint32_t i) -> float {
                return 1.0f + static_cast<float>(i) / static_cast<float>(MAX_RES);
            };

            pos.x = intToFloat(pos_x_int);
            pos.y = intToFloat(pos_y_int);
            pos.z = intToFloat(pos_z_int);

            // Extract child index from rounded position
            int idx_shift = ESVO_MAX_SCALE - scale - 1;
            if (idx_shift < 0 || idx_shift >= 32) {
                std::cout << "[POP] Invalid idx_shift=" << idx_shift << " (ESVO_MAX_SCALE=" << ESVO_MAX_SCALE << " scale=" << scale << "), using idx=0\n";
                idx = 0;
            } else {
                idx = ((pos_x_int >> idx_shift) & 1) |
                      (((pos_y_int >> idx_shift) & 1) << 1) |
                      (((pos_z_int >> idx_shift) & 1) << 2);
            }

            // Prevent same parent from being stored again and invalidate cached child descriptor
            h = 0.0f;
            child_descriptor = 0;
        }
    }

    // ====================================================================
    // TERMINATION: Undo mirroring and return result
    // Reference lines 342-346
    // ====================================================================

    if (iter >= maxIter) {
        std::cout << "DEBUG: Hit iteration limit! scale=" << scale
                  << " iter=" << iter << " t_min=" << t_min << " parent=" << (parent - &m_octree->root->childDescriptors[0]) << "\n";
    } else if (scale >= m_maxLevels) {
        std::cout << "DEBUG: Ray exited octree. scale=" << scale
                  << " iter=" << iter << " t_min=" << t_min << "\n";
    } else {
        std::cout << "DEBUG: Unknown exit condition. scale=" << scale
                  << " iter=" << iter << " t_min=" << t_min << "\n";
    }

    // If we exited the octree, return miss
    if (scale >= m_maxLevels || iter >= maxIter) {
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

    // Read from actual octree data (supports additive insertion)
    size_t voxelCount = m_octree ? m_octree->totalVoxels : m_voxelCount;
    size_t memoryUsage = m_octree ? m_octree->memoryUsage : m_memoryUsage;

    oss << "  Total voxels: " << voxelCount << "\n";
    oss << "  Max levels: " << m_maxLevels << "\n";
    oss << "  Memory usage: " << (memoryUsage / 1024.0 / 1024.0) << " MB\n";
    oss << "  Avg bytes/voxel: " << (voxelCount > 0 ? memoryUsage / voxelCount : 0) << "\n";
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
    std::cout << "[TRAVERSE BRICK] brickID=" << brickRef.brickID
              << " worldMin=(" << brickWorldMin.x << "," << brickWorldMin.y << "," << brickWorldMin.z << ")"
              << " voxelSize=" << brickVoxelSize
              << " tMin=" << tMin << " tMax=" << tMax << "\n";

    // Brick dimensions
    const int brickN = brickRef.getSideLength();  // 2^depth (e.g., 8 for depth=3)

    // 1. Compute ray entry point into brick
    const glm::vec3 entryPoint = rayOrigin + rayDir * tMin;
    // DEBUG: std::cout << "  Entry point: (" << entryPoint.x << "," << entryPoint.y << "," << entryPoint.z << ")\n";

    // 2. Transform entry point to brick-local [0, N]³ space
    const glm::vec3 localEntry = (entryPoint - brickWorldMin) / brickVoxelSize;
    // DEBUG: std::cout << "  Local entry: (" << localEntry.x << "," << localEntry.y << "," << localEntry.z << ")\n";

    // 3. Initialize current voxel (integer coordinates)
    glm::ivec3 currentVoxel{
        static_cast<int>(std::floor(localEntry.x)),
        static_cast<int>(std::floor(localEntry.y)),
        static_cast<int>(std::floor(localEntry.z))
    };

    // Clamp to brick bounds [0, N-1]
    currentVoxel = glm::clamp(currentVoxel, glm::ivec3(0), glm::ivec3(brickN - 1));
    // DEBUG: std::cout << "  Start voxel: (" << currentVoxel.x << "," << currentVoxel.y << "," << currentVoxel.z << ")\n";

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
                // Next boundary in world space
                const float nextBoundaryWorld = brickWorldMin[axis] + (currentVoxel[axis] + 1) * brickVoxelSize;
                // Distance from entry point to next boundary
                const float distToNextBoundary = nextBoundaryWorld - entryPoint[axis];
                tNext[axis] = tMin + distToNextBoundary / rayDir[axis];
            } else {
                // Next boundary in world space
                const float nextBoundaryWorld = brickWorldMin[axis] + currentVoxel[axis] * brickVoxelSize;
                // Distance from entry point to next boundary (negative direction)
                const float distToNextBoundary = entryPoint[axis] - nextBoundaryWorld;
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

        // 6. Sample brick voxel for occupancy using key predicate
        // Use AttributeRegistry's evaluateKey() to test voxel solidity
        bool voxelOccupied = true; // Default: assume solid if no registry

        if (m_registry) {
            // Get brick view (zero-copy)
            ::VoxelData::BrickView brick = m_registry->getBrick(brickRef.brickID);

            // Compute linear index from 3D coordinates
            const int brickN = brickRef.getSideLength();
            const size_t localIdx = static_cast<size_t>(currentVoxel.x + currentVoxel.y * brickN + currentVoxel.z * brickN * brickN);


            const std::any& keyAttributeValue = brick.getKeyAttributePointer()[localIdx];

            if(!keyAttributeValue.has_value()) {
                // DEBUG: std::cout << "  Warning: Voxel at (" << currentVoxel.x << "," << currentVoxel.y << "," << currentVoxel.z
                //          << ") has no key attribute value!\n";
                voxelOccupied = false;
                return std::nullopt;
            }

            // Evaluate key predicate (respects custom solidity tests)
            voxelOccupied = m_registry->evaluateKey(keyAttributeValue);

            if (stepCount == 1) {  // First voxel debug
                // DEBUG: std::cout << "  First voxel (" << currentVoxel.x << "," << currentVoxel.y << "," << currentVoxel.z
                //          << ") occupied=" << voxelOccupied << "\n";
            }
        }

        if (voxelOccupied) {
            // Hit! Ray entered this voxel from the most recent boundary crossing
            // We need to track which axis was last crossed (before entering this voxel)

            // Store the minimum tNext from PREVIOUS iteration (this is the entry point)
            // Since we're already IN the voxel, we need to look back
            // The hit is at the boundary we just crossed to enter this voxel

            // Find which tNext values correspond to boundaries we haven't crossed yet
            // The entry point is the maximum of all tMin values for each axis
            float entryT = tMin;
            glm::vec3 entryNormal(0.0f);

            // Determine which face we entered through by checking which boundary
            // was crossed most recently (has smallest "previous" tNext)
            // This is approximated by: we stepped along the axis with minimum tNext

            // For correct DDA, we need to track which axis we stepped on to GET HERE
            // But we're checking AFTER stepping, so look at current position vs entry

            // Better approach: compute entry face from voxel position and ray direction
            glm::vec3 voxelWorldMin = brickWorldMin + glm::vec3(currentVoxel) * brickVoxelSize;
            glm::vec3 voxelWorldMax = voxelWorldMin + glm::vec3(brickVoxelSize);

            // Find parametric intersection with voxel AABB
            // Handle axis-aligned rays properly
            glm::vec3 t0, t1;
            for (int i = 0; i < 3; i++) {
                if (std::abs(rayDir[i]) < 1e-8f) {
                    // Ray parallel to axis - check if within bounds
                    if (rayOrigin[i] < voxelWorldMin[i] || rayOrigin[i] > voxelWorldMax[i]) {
                        // Ray misses voxel on this axis
                        t0[i] = -std::numeric_limits<float>::infinity();
                        t1[i] = std::numeric_limits<float>::infinity();
                    } else {
                        // Ray within bounds on this axis for all t
                        t0[i] = -std::numeric_limits<float>::infinity();
                        t1[i] = std::numeric_limits<float>::infinity();
                    }
                } else {
                    t0[i] = (voxelWorldMin[i] - rayOrigin[i]) / rayDir[i];
                    t1[i] = (voxelWorldMax[i] - rayOrigin[i]) / rayDir[i];
                }
            }
            glm::vec3 tNear = glm::min(t0, t1);
            glm::vec3 tFar = glm::max(t0, t1);

            float hitT = glm::max(glm::max(tNear.x, tNear.y), tNear.z);

            // Normal points opposite to the face we entered
            if (tNear.x >= tNear.y && tNear.x >= tNear.z) {
                entryNormal = glm::vec3(rayDir.x > 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
            } else if (tNear.y >= tNear.z) {
                entryNormal = glm::vec3(0.0f, rayDir.y > 0.0f ? -1.0f : 1.0f, 0.0f);
            } else {
                entryNormal = glm::vec3(0.0f, 0.0f, rayDir.z > 0.0f ? -1.0f : 1.0f);
            }

            ISVOStructure::RayHit hit;
            hit.hit = true;
            hit.tMin = hitT;
            hit.tMax = hitT + brickVoxelSize;  // Exit point of voxel
            hit.hitPoint = rayOrigin + rayDir * hitT;  // Renamed from position
            hit.scale = m_maxLevels - 1;  // Finest detail level

            // Use entry normal computed from DDA step direction
            hit.normal = entryNormal;

            // DEPRECATED: Legacy traverseBrick doesn't support entity retrieval
            // Use traverseBrickView() instead for entity-based ray casting
            hit.entity = gaia::ecs::Entity();  // Invalid entity

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

std::optional<ISVOStructure::RayHit> LaineKarrasOctree::traverseBrickView(
    const ::GaiaVoxel::EntityBrickView& brickView,
    const glm::vec3& brickWorldMin,
    float brickVoxelSize,
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDir,
    float tMin,
    float tMax) const
{
    std::cout << "[TRAVERSE BRICK VIEW] depth=" << (int)brickView.getDepth()
              << " worldMin=(" << brickWorldMin.x << "," << brickWorldMin.y << "," << brickWorldMin.z << ")"
              << " voxelSize=" << brickVoxelSize
              << " tMin=" << tMin << " tMax=" << tMax << "\n";

    // Brick dimensions
    const int brickN = 1 << brickView.getDepth();  // 2^depth

    // 1. Compute ray entry point
    const glm::vec3 entryPoint = rayOrigin + rayDir * tMin;

    // 2. Transform to brick-local [0, N]³ space
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
            // Ray parallel to axis
            step[axis] = 0;
            tDelta[axis] = std::numeric_limits<float>::max();
            tNext[axis] = std::numeric_limits<float>::max();
        } else {
            // Determine step direction
            step[axis] = (rayDir[axis] > 0.0f) ? 1 : -1;

            // tDelta = voxel size / |ray direction component|
            tDelta[axis] = brickVoxelSize / std::abs(rayDir[axis]);

            // tNext = ray parameter to next voxel boundary
            if (rayDir[axis] > 0.0f) {
                const float nextBoundaryWorld = brickWorldMin[axis] + (currentVoxel[axis] + 1) * brickVoxelSize;
                const float distToNextBoundary = nextBoundaryWorld - entryPoint[axis];
                tNext[axis] = tMin + distToNextBoundary / rayDir[axis];
            } else {
                const float nextBoundaryWorld = brickWorldMin[axis] + currentVoxel[axis] * brickVoxelSize;
                const float distToNextBoundary = entryPoint[axis] - nextBoundaryWorld;
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

        // 6. Query entity at voxel position via EntityBrickView
        gaia::ecs::Entity entity = brickView.getEntity(currentVoxel.x, currentVoxel.y, currentVoxel.z);

        // Check if entity is valid and has Density component (solidity test)
        bool voxelOccupied = false;
        if (m_voxelWorld != nullptr) {
            // Use GaiaVoxelWorld's clean API for component access
            using namespace GaiaVoxel;
            auto density = m_voxelWorld->getComponentValue<Density>(entity);
            if (density.has_value() && density.value() > 0.0f) {
                voxelOccupied = true;
            }
        }

        if (voxelOccupied) {
            // Hit! Compute entry point and normal
            glm::vec3 voxelWorldMin = brickWorldMin + glm::vec3(currentVoxel) * brickVoxelSize;
            glm::vec3 voxelWorldMax = voxelWorldMin + glm::vec3(brickVoxelSize);

            // Find parametric intersection with voxel AABB
            glm::vec3 t0, t1;
            for (int i = 0; i < 3; i++) {
                if (std::abs(rayDir[i]) < 1e-8f) {
                    // Ray parallel to axis
                    if (rayOrigin[i] < voxelWorldMin[i] || rayOrigin[i] > voxelWorldMax[i]) {
                        t0[i] = -std::numeric_limits<float>::infinity();
                        t1[i] = std::numeric_limits<float>::infinity();
                    } else {
                        t0[i] = -std::numeric_limits<float>::infinity();
                        t1[i] = std::numeric_limits<float>::infinity();
                    }
                } else {
                    t0[i] = (voxelWorldMin[i] - rayOrigin[i]) / rayDir[i];
                    t1[i] = (voxelWorldMax[i] - rayOrigin[i]) / rayDir[i];
                }
            }
            glm::vec3 tNear = glm::min(t0, t1);
            glm::vec3 tFar = glm::max(t0, t1);

            float hitT = glm::max(glm::max(tNear.x, tNear.y), tNear.z);

            // Compute entry normal
            glm::vec3 entryNormal;
            if (tNear.x >= tNear.y && tNear.x >= tNear.z) {
                entryNormal = glm::vec3(rayDir.x > 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
            } else if (tNear.y >= tNear.z) {
                entryNormal = glm::vec3(0.0f, rayDir.y > 0.0f ? -1.0f : 1.0f, 0.0f);
            } else {
                entryNormal = glm::vec3(0.0f, 0.0f, rayDir.z > 0.0f ? -1.0f : 1.0f);
            }

            ISVOStructure::RayHit hit;
            hit.hit = true;
            hit.tMin = hitT;
            hit.tMax = hitT + brickVoxelSize;
            hit.hitPoint = rayOrigin + rayDir * hitT;
            hit.scale = m_maxLevels - 1;  // Finest detail level
            hit.normal = entryNormal;
            hit.entity = entity;  // Return entity reference (zero-copy!)

            std::cout << "[BRICK VIEW HIT] voxel=(" << currentVoxel.x << "," << currentVoxel.y << "," << currentVoxel.z
                      << ") t=" << hitT << " entity.id=" << entity.id() << "\n";

            return hit;
        }

        // 7. Advance to next voxel
        if (tNext.x < tNext.y && tNext.x < tNext.z) {
            // Cross X boundary
            if (tNext.x > tMax) return std::nullopt;
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

    // Exceeded step limit
    return std::nullopt;
}

} // namespace SVO
