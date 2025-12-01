#define NOMINMAX
#include "LaineKarrasOctree.h"
#include "VoxelComponents.h"  // For GaiaVoxel components
#include "ComponentData.h"
#include "Compression/DXT1Compressor.h"  // Week 3: DXT compression
#include <sstream>
#include <algorithm>
#include <limits>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <array>

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
LaineKarrasOctree::LaineKarrasOctree(::GaiaVoxel::GaiaVoxelWorld& voxelWorld,::VoxelData::AttributeRegistry* registry, int maxLevels, int brickDepthLevels)
    : m_voxelWorld(&voxelWorld)
    , m_registry(registry)  // AttributeRegistry pointer in entity-based mode
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

// ============================================================================
// REMOVED: insert() and remove() methods (Phase 2 temporary bridge)
// ============================================================================
// Replaced by rebuild() API. Use:
//   octree.rebuild(world, worldMin, worldMax)
// to populate octree from entities.
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
 * Check if a point is inside an axis-aligned bounding box.
 * Used to detect interior rays that start inside the volume.
 */
bool isPointInsideAABB(
    const glm::vec3& point,
    const glm::vec3& boxMin,
    const glm::vec3& boxMax)
{
    return point.x >= boxMin.x && point.x <= boxMax.x &&
           point.y >= boxMin.y && point.y <= boxMax.y &&
           point.z >= boxMin.z && point.z <= boxMax.z;
}

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

// Type aliases for cleaner code
using ESVOTraversalState = LaineKarrasOctree::ESVOTraversalState;
using ESVORayCoefficients = LaineKarrasOctree::ESVORayCoefficients;
using AdvanceResult = LaineKarrasOctree::AdvanceResult;
using PopResult = LaineKarrasOctree::PopResult;

// ============================================================================
// Helper Function Declarations (defined after class methods)
// ============================================================================

namespace {

/**
 * Compute parametric coefficients for ray traversal in ESVO [1,2] space.
 * Handles axis-parallel rays with epsilon clamping.
 */
ESVORayCoefficients computeRayCoefficients(
    const glm::vec3& rayDir,
    const glm::vec3& normOrigin);

/**
 * Select initial octant based on ray entry position.
 * Uses parametric or position-based selection depending on ray characteristics.
 */
void selectInitialOctant(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef);

/**
 * Compute corrected tc_max for axis-parallel rays.
 * Filters out misleading corner values from perpendicular axes.
 */
float computeCorrectedTcMax(
    float tx_corner, float ty_corner, float tz_corner,
    const glm::vec3& rayDir, float t_max);

/**
 * Compute voxel exit corners for ADVANCE phase.
 */
void computeVoxelCorners(
    const glm::vec3& pos,
    const ESVORayCoefficients& coef,
    float& tx_corner, float& ty_corner, float& tz_corner);

} // anonymous namespace

// ============================================================================
// Public Ray Casting Interface
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

// ============================================================================
// ESVO Traversal Phase Methods
// ============================================================================

/**
 * Validate ray input parameters.
 * Returns false if ray is invalid (null direction, NaN, etc.)
 */
bool LaineKarrasOctree::validateRayInput(
    const glm::vec3& origin,
    const glm::vec3& direction,
    glm::vec3& rayDirOut) const
{
    // Check for valid octree
    if (!m_octree || !m_octree->root || m_octree->root->childDescriptors.empty()) {
        return false;
    }

    // Normalize direction
    rayDirOut = glm::normalize(direction);
    float rayLength = glm::length(rayDirOut);
    if (rayLength < 1e-6f) {
        return false; // Invalid direction
    }

    // Check for NaN/Inf
    if (glm::any(glm::isnan(origin)) || glm::any(glm::isnan(rayDirOut)) ||
        glm::any(glm::isinf(origin)) || glm::any(glm::isinf(rayDirOut))) {
        return false;
    }

    return true;
}

/**
 * Initialize traversal state for ray casting.
 * Sets up stack, initial position, and octant selection.
 */
void LaineKarrasOctree::initializeTraversalState(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef,
    CastStack& stack) const
{
    // Initialize stack with root descriptor at all ESVO scales
    const ChildDescriptor* rootDesc = &m_octree->root->childDescriptors[0];
    const int minScale = ESVO_MAX_SCALE - m_maxLevels + 1;
    for (int esvoScale = minScale; esvoScale <= ESVO_MAX_SCALE; esvoScale++) {
        stack.push(esvoScale, rootDesc, state.t_max);
    }

    // Set initial scale and parent
    state.scale = ESVO_MAX_SCALE;
    state.parent = rootDesc;
    state.child_descriptor = 0;
    state.idx = 0;
    state.pos = glm::vec3(1.0f, 1.0f, 1.0f);
    state.scale_exp2 = 0.5f;

    // Select initial octant
    selectInitialOctant(state, coef);
}

/**
 * Fetch child descriptor for current node if not cached.
 * Mirrors validMask and leafMask based on octant_mask for correct traversal.
 *
 * This is the SINGLE CONVERSION POINT for mirrored-space traversal.
 * After this call, state.mirroredValidMask and state.mirroredLeafMask can be
 * used directly with state.idx (mirrored-space octant).
 */
void LaineKarrasOctree::fetchChildDescriptor(ESVOTraversalState& state, const ESVORayCoefficients& coef) const
{
    if (state.child_descriptor == 0) {
        // Mirror masks from world-space to mirrored-space based on ray direction
        // This allows direct use: (mirroredValidMask & (1 << state.idx)) works correctly
        state.mirroredValidMask = mirrorMask(state.parent->validMask, coef.octant_mask);
        state.mirroredLeafMask = mirrorMask(state.parent->leafMask, coef.octant_mask);

        // Pack descriptor to match ESVO layout (using mirrored masks)
        uint32_t nonLeafMask = ~state.mirroredLeafMask & 0xFF;
        state.child_descriptor = nonLeafMask |
                     (static_cast<uint64_t>(state.mirroredValidMask) << 8) |
                     (static_cast<uint64_t>(state.parent->childPointer) << 16);
    }
}

/**
 * Check if current child is valid and compute t-span intersection.
 * Returns true if the voxel should be processed (valid and intersected).
 *
 * Uses MIRRORED masks (set by fetchChildDescriptor) for correct traversal.
 * state.idx is in mirrored space, so we check against mirroredValidMask/mirroredLeafMask.
 */
bool LaineKarrasOctree::checkChildValidity(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef,
    bool& isLeaf,
    float& tv_max) const
{
    // Check if child exists using MIRRORED validMask (already converted in fetchChildDescriptor)
    // state.idx is in mirrored space, mirroredValidMask is in mirrored space - direct comparison works!
    bool child_valid = (state.mirroredValidMask & (1u << state.idx)) != 0;
    isLeaf = (state.mirroredLeafMask & (1u << state.idx)) != 0;

    // At brick level, force leaf status
    int currentUserScale = esvoToUserScale(state.scale);
    int brickUserScale = m_maxLevels - m_brickDepthLevels;
    if (currentUserScale == brickUserScale && child_valid) {
        isLeaf = true;
    }

    if (!child_valid || state.t_min > state.t_max) {
        return false;
    }

    // Compute corner values
    float tx_corner, ty_corner, tz_corner;
    computeVoxelCorners(state.pos, coef, tx_corner, ty_corner, tz_corner);

    // Compute corrected tc_max for axis-parallel rays
    float tc_max_corrected = computeCorrectedTcMax(tx_corner, ty_corner, tz_corner, coef.rayDir, state.t_max);
    tv_max = std::min(state.t_max, tc_max_corrected);

    // Compute center values for octant selection after DESCEND
    float half = state.scale_exp2 * 0.5f;
    state.tx_center = half * coef.tx_coef + tx_corner;
    state.ty_center = half * coef.ty_coef + ty_corner;
    state.tz_center = half * coef.tz_coef + tz_corner;

    return state.t_min <= tv_max;
}

/**
 * PUSH phase: Descend into child node.
 * Updates parent pointer, scale, and position for child traversal.
 *
 * IMPORTANT: Child descriptor storage is in WORLD space, but state.idx is in MIRRORED space.
 * We convert state.idx to world space to compute the correct child offset.
 */
void LaineKarrasOctree::executePushPhase(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef,
    CastStack& stack,
    float tv_max) const
{
    // Compute tc_max for stack management
    float tx_corner, ty_corner, tz_corner;
    computeVoxelCorners(state.pos, coef, tx_corner, ty_corner, tz_corner);
    float tc_max = std::min({tx_corner, ty_corner, tz_corner});

    // Push current state to stack if needed
    if (tc_max < state.h) {
        stack.push(state.scale, state.parent, state.t_max);
    }
    state.h = tc_max;

    // Convert mirrored idx to world space for child offset calculation
    // Descriptors are stored in world-space order, so we need world-space octant
    int worldIdx = mirroredToWorldOctant(state.idx, coef.octant_mask);

    // Calculate child offset (count non-leaf children before current in WORLD space)
    // Use WORLD-space masks since that's how descriptors are stored
    uint8_t nonLeafMask = ~state.parent->leafMask & state.parent->validMask;
    uint32_t mask_before_child = (1u << worldIdx) - 1;
    uint32_t nonleaf_before_child = nonLeafMask & mask_before_child;
    uint32_t child_offset = std::popcount(nonleaf_before_child);

    // Update parent pointer to child
    uint32_t child_index = state.parent->childPointer + child_offset;

    // Bounds check
    if (child_index >= m_octree->root->childDescriptors.size()) {
        return; // Invalid child pointer
    }

    state.parent = &m_octree->root->childDescriptors[child_index];

    // Descend to next level
    state.idx = 0;
    state.scale--;
    float half = state.scale_exp2 * 0.5f;
    state.scale_exp2 = half;

    // Select child octant using parent's center values
    if (state.tx_center > state.t_min) {
        state.idx ^= 1;
        state.pos.x += state.scale_exp2;
    }
    if (state.ty_center > state.t_min) {
        state.idx ^= 2;
        state.pos.y += state.scale_exp2;
    }
    if (state.tz_center > state.t_min) {
        state.idx ^= 4;
        state.pos.z += state.scale_exp2;
    }

    // Update t-span and invalidate cached descriptor (forces re-mirroring for new parent)
    state.t_max = tv_max;
    state.child_descriptor = 0;
}

/**
 * ADVANCE phase: Move to next sibling voxel.
 * Returns result indicating next action to take.
 */
AdvanceResult LaineKarrasOctree::executeAdvancePhase(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef) const
{
    // Compute corner values
    float tx_corner, ty_corner, tz_corner;
    computeVoxelCorners(state.pos, coef, tx_corner, ty_corner, tz_corner);

    // Determine which axes can step (non-parallel)
    constexpr float dir_epsilon = 1e-5f;
    bool canStepX = (std::abs(coef.rayDir.x) >= dir_epsilon);
    bool canStepY = (std::abs(coef.rayDir.y) >= dir_epsilon);
    bool canStepZ = (std::abs(coef.rayDir.z) >= dir_epsilon);

    // Compute corrected tc_max
    float tc_max_corrected = computeCorrectedTcMax(tx_corner, ty_corner, tz_corner, coef.rayDir, state.t_max);

    // Fallback for fully axis-parallel rays
    if (tc_max_corrected == std::numeric_limits<float>::max()) {
        tc_max_corrected = std::max({
            canStepX ? tx_corner : -std::numeric_limits<float>::max(),
            canStepY ? ty_corner : -std::numeric_limits<float>::max(),
            canStepZ ? tz_corner : -std::numeric_limits<float>::max()
        });
    }

    // Step along axes at their exit boundary (in mirrored space, pos decreases)
    int step_mask = 0;
    if (canStepX && tx_corner <= tc_max_corrected) { step_mask ^= 1; state.pos.x -= state.scale_exp2; }
    if (canStepY && ty_corner <= tc_max_corrected) { step_mask ^= 2; state.pos.y -= state.scale_exp2; }
    if (canStepZ && tz_corner <= tc_max_corrected) { step_mask ^= 4; state.pos.z -= state.scale_exp2; }

    state.t_min = std::max(tc_max_corrected, 0.0f);

    state.idx ^= step_mask;

    // Check if we need to POP (bit flips disagree with ray direction)
    if ((state.idx & step_mask) != 0) {
        return AdvanceResult::POP_NEEDED;
    }

    return AdvanceResult::CONTINUE;
}

/**
 * POP phase: Ascend hierarchy when exiting parent voxel.
 * Uses integer bit manipulation for correct scale computation.
 */
PopResult LaineKarrasOctree::executePopPhase(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef,
    CastStack& stack,
    int step_mask) const
{
    // For flat octrees at root scale, check for octree exit
    if (state.scale == ESVO_MAX_SCALE) {
        // Exit if t-span is invalid or position is outside [1,2]³ ESVO space
        if (state.t_min > state.t_max ||
            state.pos.x < 1.0f || state.pos.x >= 2.0f ||
            state.pos.y < 1.0f || state.pos.y >= 2.0f ||
            state.pos.z < 1.0f || state.pos.z >= 2.0f) {
            DEBUG_PRINT("  POP: Exiting octree - pos=(%.3f,%.3f,%.3f) t=[%.4f,%.4f]\n",
                        state.pos.x, state.pos.y, state.pos.z, state.t_min, state.t_max);
            return PopResult::EXIT_OCTREE;
        }
        // Stay at root, continue with new idx
        state.child_descriptor = 0;
        return PopResult::CONTINUE;
    }

    // Convert positions to integers for bit manipulation
    const int MAX_RES = 1 << ESVO_MAX_SCALE;

    auto floatToInt = [MAX_RES](float f) -> uint32_t {
        float clamped = std::max(0.0f, std::min(f, 1.0f));
        return static_cast<uint32_t>(std::max(0.0f, std::min(clamped * MAX_RES, static_cast<float>(MAX_RES - 1))));
    };

    uint32_t pos_x_int = floatToInt(std::max(0.0f, state.pos.x - 1.0f));
    uint32_t pos_y_int = floatToInt(std::max(0.0f, state.pos.y - 1.0f));
    uint32_t pos_z_int = floatToInt(std::max(0.0f, state.pos.z - 1.0f));

    // Compute next position for stepped axes
    uint32_t next_x_int = (step_mask & 1) ? floatToInt(std::max(0.0f, state.pos.x + state.scale_exp2 - 1.0f)) : pos_x_int;
    uint32_t next_y_int = (step_mask & 2) ? floatToInt(std::max(0.0f, state.pos.y + state.scale_exp2 - 1.0f)) : pos_y_int;
    uint32_t next_z_int = (step_mask & 4) ? floatToInt(std::max(0.0f, state.pos.z + state.scale_exp2 - 1.0f)) : pos_z_int;

    // Find differing bits to determine ascent level
    uint32_t differing_bits = 0;
    if ((step_mask & 1) != 0) differing_bits |= (pos_x_int ^ next_x_int);
    if ((step_mask & 2) != 0) differing_bits |= (pos_y_int ^ next_y_int);
    if ((step_mask & 4) != 0) differing_bits |= (pos_z_int ^ next_z_int);

    if (differing_bits == 0) {
        return PopResult::EXIT_OCTREE;
    }

    // Find highest set bit
    int highest_bit = 31 - std::countl_zero(differing_bits);
    state.scale = highest_bit;

    // Validate scale range
    int minESVOScale = ESVO_MAX_SCALE - m_maxLevels + 1;
    if (state.scale < minESVOScale || state.scale > ESVO_MAX_SCALE) {
        return PopResult::EXIT_OCTREE;
    }

    // Recompute scale_exp2
    int exp_val = state.scale - ESVO_MAX_SCALE + 127;
    state.scale_exp2 = std::bit_cast<float>(static_cast<uint32_t>(exp_val << 23));

    // Restore from stack
    state.parent = stack.getNode(state.scale);
    state.t_max = stack.getTMax(state.scale);

    if (state.parent == nullptr) {
        return PopResult::EXIT_OCTREE;
    }

    // Round position to voxel boundary
    int shift_amount = ESVO_MAX_SCALE - state.scale;
    if (shift_amount < 0 || shift_amount >= 32) {
        return PopResult::EXIT_OCTREE;
    }

    uint32_t mask = ~((1u << shift_amount) - 1);
    pos_x_int &= mask;
    pos_y_int &= mask;
    pos_z_int &= mask;

    // Convert back to float
    auto intToFloat = [MAX_RES](uint32_t i) -> float {
        return 1.0f + static_cast<float>(i) / static_cast<float>(MAX_RES);
    };

    state.pos.x = intToFloat(pos_x_int);
    state.pos.y = intToFloat(pos_y_int);
    state.pos.z = intToFloat(pos_z_int);

    // Extract child index from position
    int idx_shift = ESVO_MAX_SCALE - state.scale - 1;
    if (idx_shift < 0 || idx_shift >= 32) {
        state.idx = 0;
    } else {
        state.idx = ((pos_x_int >> idx_shift) & 1) |
                  (((pos_y_int >> idx_shift) & 1) << 1) |
                  (((pos_z_int >> idx_shift) & 1) << 2);
    }

    state.h = 0.0f;
    state.child_descriptor = 0;

    return PopResult::CONTINUE;
}

/**
 * Handle leaf hit: perform brick traversal and return hit result.
 * Returns nullopt if traversal should continue (brick miss).
 *
 * BRICK LOOKUP STRATEGY:
 * - state.idx is in MIRRORED space (ray-direction dependent)
 * - leafToBrickView stores bricks by LOCAL-space octant (ray-independent)
 * - Convert mirrored→local: localOctant = state.idx ^ octant_mask
 * - Also compute position-based leafOctant as fallback for edge cases
 */
std::optional<ISVOStructure::RayHit> LaineKarrasOctree::handleLeafHit(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef,
    const glm::vec3& origin,
    float tRayStart,
    float tEntry,
    float tExit,
    float tv_max) const
{
    DEBUG_PRINT("  handleLeafHit: idx=%d, state.t_min=%.4f, tv_max=%.4f, tRayStart=%.4f, tEntry=%.4f, tExit=%.4f\n",
                state.idx, state.t_min, tv_max, tRayStart, tEntry, tExit);

    size_t parentDescriptorIndex = state.parent - &m_octree->root->childDescriptors[0];
    glm::vec3 worldSize = m_worldMax - m_worldMin;
    int bricksPerAxis = m_octree->bricksPerAxis;
    int brickSideLength = m_octree->brickSideLength;

    // Compute brick from ESVO state position (for axes the ray is moving along)
    // and actual ray position (for stationary axes where ray doesn't move).
    //
    // The ESVO state.pos is in mirrored parametric space [1,2].
    // Must convert to LOCAL [1,2] space first using NVIDIA's formula, then to [0,1].
    //
    // NVIDIA ESVO approach (Raycast.inl lines 344-346):
    //   if ((octant_mask & 1) == 0) pos.x = 3.0f - scale_exp2 - pos.x;
    // This correctly accounts for octant size when unmirroring.
    constexpr float axis_epsilon = 1e-5f;

    // Get ray position from ESVO state (mirrored → local conversion using NVIDIA formula)
    glm::vec3 localPos = state.pos;  // Start with mirrored [1,2] position
    float octantSize = state.scale_exp2;  // Current octant size

    // Unmirror using NVIDIA's formula: 3.0 - scale_exp2 - pos
    // For mirrored axes (octant_mask bit = 0), apply the transformation
    if ((coef.octant_mask & 1) == 0) localPos.x = 3.0f - octantSize - localPos.x;
    if ((coef.octant_mask & 2) == 0) localPos.y = 3.0f - octantSize - localPos.y;
    if ((coef.octant_mask & 4) == 0) localPos.z = 3.0f - octantSize - localPos.z;

    // Now localPos is in LOCAL [1,2] space - convert to [0,1] normalized space
    glm::vec3 localNorm = localPos - glm::vec3(1.0f);

    // Add small offset along world ray direction to get point inside the octant (not on boundary)
    // Unlike the mirrored approach, in local space the ray direction is unchanged
    float offset = 0.001f;
    glm::vec3 offsetDir;
    offsetDir.x = (coef.rayDir.x > 0) ? offset : -offset;
    offsetDir.y = (coef.rayDir.y > 0) ? offset : -offset;
    offsetDir.z = (coef.rayDir.z > 0) ? offset : -offset;
    glm::vec3 octantInside = localNorm + offsetDir;

    // For stationary axes (ray perpendicular), use actual ray position
    glm::vec3 rayPosWorld = origin + coef.rayDir * std::max(tEntry, 0.0f);
    glm::vec3 rayPosLocal = (rayPosWorld - m_worldMin) / worldSize;
    rayPosLocal = glm::clamp(rayPosLocal, glm::vec3(0.001f), glm::vec3(0.999f));

    if (std::abs(coef.rayDir.x) < axis_epsilon) {
        octantInside.x = rayPosLocal.x;
    }
    if (std::abs(coef.rayDir.y) < axis_epsilon) {
        octantInside.y = rayPosLocal.y;
    }
    if (std::abs(coef.rayDir.z) < axis_epsilon) {
        octantInside.z = rayPosLocal.z;
    }

    octantInside = glm::clamp(octantInside, glm::vec3(0.001f), glm::vec3(0.999f));

    // Primary strategy: Use ESVO state position (advances with octant traversal)
    // This correctly handles multi-brick traversal.

    const ::GaiaVoxel::EntityBrickView* brickView = nullptr;
    glm::ivec3 brickIndex;

    // Method 1: ESVO state position (correct for multi-octant traversal)
    // Convert normalized [0,1] to local position [0, worldSize]
    glm::vec3 hitPosLocal = octantInside * worldSize;
    hitPosLocal = glm::clamp(hitPosLocal, glm::vec3(0.0f), worldSize - glm::vec3(0.001f));

    brickIndex = glm::ivec3(
        static_cast<int>(hitPosLocal.x / static_cast<float>(brickSideLength)),
        static_cast<int>(hitPosLocal.y / static_cast<float>(brickSideLength)),
        static_cast<int>(hitPosLocal.z / static_cast<float>(brickSideLength))
    );
    brickIndex = glm::clamp(brickIndex, glm::ivec3(0), glm::ivec3(bricksPerAxis - 1));
    brickView = m_octree->root->getBrickViewByGrid(brickIndex.x, brickIndex.y, brickIndex.z);

    // Method 2: Ray entry position (fallback for exterior rays into sparse octrees)
    if (brickView == nullptr) {
        glm::vec3 rayEntryWorld = origin + coef.rayDir * std::max(tEntry, 0.0f);
        glm::vec3 rayEntryLocal = rayEntryWorld - m_worldMin;
        rayEntryLocal += coef.rayDir * 0.01f;  // Small offset into the volume
        rayEntryLocal = glm::clamp(rayEntryLocal, glm::vec3(0.0f), worldSize - glm::vec3(0.001f));

        brickIndex = glm::ivec3(
            static_cast<int>(rayEntryLocal.x / static_cast<float>(brickSideLength)),
            static_cast<int>(rayEntryLocal.y / static_cast<float>(brickSideLength)),
            static_cast<int>(rayEntryLocal.z / static_cast<float>(brickSideLength))
        );
        brickIndex = glm::clamp(brickIndex, glm::ivec3(0), glm::ivec3(bricksPerAxis - 1));
        brickView = m_octree->root->getBrickViewByGrid(brickIndex.x, brickIndex.y, brickIndex.z);
    }

    // Fallback 3: ESVO octant-based lookup (legacy compatibility)
    if (brickView == nullptr) {
        int localOctant = mirroredToLocalOctant(state.idx, coef.octant_mask);
        brickView = m_octree->root->getBrickView(parentDescriptorIndex, localOctant);
    }

    DEBUG_PRINT("    parentDescriptorIndex=%zu, brickIndex=(%d,%d,%d), brickView=%p\n",
                parentDescriptorIndex, brickIndex.x, brickIndex.y, brickIndex.z, (void*)brickView);

    if (brickView != nullptr) {
        // Transform ray to volume local space using mat4
        // Local space: [0, worldSize] integer grid
        // World space: [worldMin, worldMax]
        glm::vec3 localRayOrigin = glm::vec3(m_worldToLocal * glm::vec4(origin, 1.0f));
        // Direction only needs rotation (no translation), but for axis-aligned volumes
        // this is identity. For rotated volumes, use mat3(m_worldToLocal).
        glm::vec3 localRayDir = glm::mat3(m_worldToLocal) * coef.rayDir;

        DEBUG_PRINT("    localRayOrigin=(%.2f,%.2f,%.2f), brickView->voxelsPerBrick=%zu\n",
                    localRayOrigin.x, localRayOrigin.y, localRayOrigin.z, brickView->getVoxelsPerBrick());
        auto hitResult = traverseBrickAndReturnHit(*brickView, localRayOrigin, localRayDir, tEntry);

        // Transform hit point back to world space using mat4
        if (hitResult.has_value()) {
            hitResult->hitPoint = glm::vec3(m_localToWorld * glm::vec4(hitResult->hitPoint, 1.0f));
            // Transform normal back to world space (use inverse transpose for proper normal transformation)
            // For axis-aligned volumes, this is identity. For rotated volumes, use:
            // hitResult->normal = glm::normalize(glm::mat3(glm::transpose(m_worldToLocal)) * hitResult->normal);
        }
        return hitResult;
    }

    DEBUG_PRINT("    No brickView found, returning miss\n");
    return std::nullopt;
}

/**
 * Traverse brick and return hit result.
 * Ray is in volume local space (origin at volumeGridMin = 0,0,0).
 * EntityBrickView now stores LOCAL gridOrigin (brickIndex * brickSideLength).
 *
 * LOCAL-SPACE ARCHITECTURE:
 * - Voxels stored with LOCAL Morton keys (relative to volume origin)
 * - Brick's localGridOrigin = brickIndex * brickSideLength (e.g., (0,0,0), (8,0,0))
 * - Ray is transformed to local space before traversal
 * - All brick bounds are in local space
 */
std::optional<ISVOStructure::RayHit> LaineKarrasOctree::traverseBrickAndReturnHit(
    const ::GaiaVoxel::EntityBrickView& brickView,
    const glm::vec3& localRayOrigin,
    const glm::vec3& rayDir,
    float tEntry) const
{
    uint8_t brickDepth = brickView.getDepth();
    size_t brickSideLength = 1u << brickDepth;
    constexpr float brickVoxelSize = 1.0f;  // Unit voxels for integer grid

    // Compute brick bounds directly from LOCAL gridOrigin
    // EntityBrickView now stores local coordinates, not world coordinates
    glm::vec3 brickLocalMin = glm::vec3(brickView.getLocalGridOrigin());
    glm::vec3 brickLocalMax = brickLocalMin + glm::vec3(static_cast<float>(brickSideLength) * brickVoxelSize);

    // Compute ray-brick AABB intersection in local space
    glm::vec3 invDir;
    for (int i = 0; i < 3; i++) {
        if (std::abs(rayDir[i]) < 1e-8f) {
            invDir[i] = (rayDir[i] >= 0) ? 1e8f : -1e8f;
        } else {
            invDir[i] = 1.0f / rayDir[i];
        }
    }

    glm::vec3 t0 = (brickLocalMin - localRayOrigin) * invDir;
    glm::vec3 t1 = (brickLocalMax - localRayOrigin) * invDir;
    glm::vec3 tNear = glm::min(t0, t1);
    glm::vec3 tFar = glm::max(t0, t1);

    float brickTMin = glm::max(glm::max(tNear.x, tNear.y), tNear.z);
    float brickTMax = glm::min(glm::min(tFar.x, tFar.y), tFar.z);
    brickTMin = glm::max(brickTMin, tEntry);

    DEBUG_PRINT("    traverseBrickAndReturnHit: brickLocalMin=(%.1f,%.1f,%.1f), brickLocalMax=(%.1f,%.1f,%.1f)\n",
                brickLocalMin.x, brickLocalMin.y, brickLocalMin.z, brickLocalMax.x, brickLocalMax.y, brickLocalMax.z);
    DEBUG_PRINT("    brickTMin=%.4f, brickTMax=%.4f, tEntry=%.4f\n", brickTMin, brickTMax, tEntry);

    // Pass local ray and local brick bounds to traverseBrickView
    // EntityBrickView uses local gridOrigin for entity lookup (LocalGrid query mode)
    return traverseBrickView(brickView, brickLocalMin, brickVoxelSize, localRayOrigin, rayDir, brickTMin, brickTMax);
}

// ============================================================================
// Main Ray Casting Implementation
// ============================================================================

ISVOStructure::RayHit LaineKarrasOctree::castRayImpl(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float tMin,
    float tMax,
    float lodBias) const
{
    ISVOStructure::RayHit miss{};
    miss.hit = false;

    // Phase 1: Validate input
    glm::vec3 rayDir;
    if (!validateRayInput(origin, direction, rayDir)) {
        return miss;
    }

    // Phase 2: Detect interior ray and intersect with world bounds
    bool rayStartsInside = isPointInsideAABB(origin, m_worldMin, m_worldMax);

    float tEntry, tExit;
    if (!intersectAABB(origin, rayDir, m_worldMin, m_worldMax, tEntry, tExit)) {
        return miss;
    }

    // For interior rays, tEntry will be negative (ray already inside)
    // Clamp to user-specified range
    tEntry = std::max(tEntry, tMin);
    tExit = std::min(tExit, tMax);
    if (tEntry >= tExit || tExit < 0.0f) {
        return miss;
    }

    // Phase 3: Setup ray coefficients and normalized coordinates
    // For interior rays, start from origin (t=0); for exterior, start from entry point
    float tRayStart = rayStartsInside ? 0.0f : std::max(0.0f, tEntry);
    glm::vec3 rayEntryPoint = origin + rayDir * tRayStart;
    glm::vec3 worldSize = m_worldMax - m_worldMin;
    glm::vec3 normOrigin = (rayEntryPoint - m_worldMin) / worldSize + glm::vec3(1.0f);

    ESVORayCoefficients coef = computeRayCoefficients(rayDir, normOrigin);

    // Phase 4: Initialize traversal state
    ESVOTraversalState state;

    DEBUG_PRINT("\n=== Interior Ray Detection ===\n");
    DEBUG_PRINT("  rayStartsInside=%d\n", rayStartsInside ? 1 : 0);
    DEBUG_PRINT("  origin=(%.3f, %.3f, %.3f), tEntry=%.6f, tExit=%.6f\n",
                origin.x, origin.y, origin.z, tEntry, tExit);
    DEBUG_PRINT("  worldBounds=[(%.3f,%.3f,%.3f), (%.3f,%.3f,%.3f)]\n",
                m_worldMin.x, m_worldMin.y, m_worldMin.z, m_worldMax.x, m_worldMax.y, m_worldMax.z);
    DEBUG_PRINT("  normOrigin=(%.6f, %.6f, %.6f)\n", normOrigin.x, normOrigin.y, normOrigin.z);

    if (rayStartsInside) {
        // Interior ray: start from current position (t=0 in ESVO parametric space)
        // The normalized origin is already at the ray's starting position within [1,2]³
        // Set t_min=0 to indicate we're starting here, not from an external entry point
        state.t_min = 0.0f;
        state.t_max = std::min({coef.tx_coef - coef.tx_bias,
                                coef.ty_coef - coef.ty_bias,
                                coef.tz_coef - coef.tz_bias});
        state.t_max = std::min(state.t_max, 1.0f);
        DEBUG_PRINT("  INTERIOR: state.t_min=%.6f, state.t_max=%.6f\n", state.t_min, state.t_max);
    } else {
        // Exterior ray: standard ESVO t-span computation
        state.t_min = std::max({2.0f * coef.tx_coef - coef.tx_bias,
                                2.0f * coef.ty_coef - coef.ty_bias,
                                2.0f * coef.tz_coef - coef.tz_bias});
        state.t_max = std::min({coef.tx_coef - coef.tx_bias,
                                coef.ty_coef - coef.ty_bias,
                                coef.tz_coef - coef.tz_bias});
        state.t_min = std::max(state.t_min, 0.0f);
        state.t_max = std::min(state.t_max, 1.0f);
    }
    state.h = state.t_max;

    CastStack stack;
    initializeTraversalState(state, coef, stack);

    // Phase 5: Main traversal loop
    const int maxIter = 500;
    int minESVOScale = ESVO_MAX_SCALE - m_maxLevels + 1;

    DEBUG_PRINT("\n=== Main Traversal Loop ===\n");
    DEBUG_PRINT("  minESVOScale=%d, maxLevels=%d, brickDepthLevels=%d\n", minESVOScale, m_maxLevels, m_brickDepthLevels);
    DEBUG_PRINT("  bricksPerAxis=%d, brickSideLength=%d\n",
                m_octree ? m_octree->bricksPerAxis : -1,
                m_octree ? m_octree->brickSideLength : -1);

    while (state.scale >= minESVOScale && state.scale <= ESVO_MAX_SCALE && state.iter < maxIter) {
        ++state.iter;

        // Fetch child descriptor and mirror masks based on ray direction
        fetchChildDescriptor(state, coef);

        // Check child validity and compute t-span
        bool isLeaf = false;
        float tv_max = 0.0f;
        bool shouldProcess = checkChildValidity(state, coef, isLeaf, tv_max);

        DEBUG_PRINT("[iter %d] scale=%d idx=%d pos=(%.3f,%.3f,%.3f) t=[%.4f,%.4f] shouldProcess=%d isLeaf=%d validMask=0x%02X leafMask=0x%02X\n",
                    state.iter, state.scale, state.idx, state.pos.x, state.pos.y, state.pos.z,
                    state.t_min, state.t_max, shouldProcess ? 1 : 0, isLeaf ? 1 : 0,
                    state.parent ? state.parent->validMask : 0,
                    state.parent ? state.parent->leafMask : 0);

        bool skipToAdvance = false;

        if (shouldProcess) {
            // Handle leaf hit
            if (isLeaf) {
                auto leafResult = handleLeafHit(state, coef, origin, tRayStart, tEntry, tExit, tv_max);

                if (leafResult.has_value()) {
                    return leafResult.value();
                }

                // Brick miss - continue to next leaf via ADVANCE phase
                state.t_min = tv_max;
                skipToAdvance = true;
            }

            // PUSH: Descend into child (skip if brick miss)
            if (!skipToAdvance) {
                executePushPhase(state, coef, stack, tv_max);
                continue;
            }
        }

        // ADVANCE: Move to next sibling
        AdvanceResult advResult = executeAdvancePhase(state, coef);

        if (advResult == AdvanceResult::POP_NEEDED) {
            // Compute step_mask for POP phase
            float tx_corner, ty_corner, tz_corner;
            computeVoxelCorners(state.pos, coef, tx_corner, ty_corner, tz_corner);
            float tc_max_corrected = computeCorrectedTcMax(tx_corner, ty_corner, tz_corner, coef.rayDir, state.t_max);

            constexpr float dir_epsilon = 1e-5f;
            int step_mask = 0;
            if (std::abs(coef.rayDir.x) >= dir_epsilon && tx_corner <= tc_max_corrected) step_mask ^= 1;
            if (std::abs(coef.rayDir.y) >= dir_epsilon && ty_corner <= tc_max_corrected) step_mask ^= 2;
            if (std::abs(coef.rayDir.z) >= dir_epsilon && tz_corner <= tc_max_corrected) step_mask ^= 4;

            PopResult popResult = executePopPhase(state, coef, stack, step_mask);
            if (popResult == PopResult::EXIT_OCTREE) {
                break;
            }
        }
    }

    return miss;
}

// ============================================================================
// Helper Function Implementations
// ============================================================================

namespace {

ESVORayCoefficients computeRayCoefficients(
    const glm::vec3& rayDir,
    const glm::vec3& normOrigin)
{
    ESVORayCoefficients coef;
    coef.rayDir = rayDir;
    coef.normOrigin = normOrigin;

    // Prevent divide-by-zero
    constexpr float epsilon = 1e-5f;
    glm::vec3 rayDirSafe = rayDir;
    if (std::abs(rayDirSafe.x) < epsilon) rayDirSafe.x = std::copysignf(epsilon, rayDirSafe.x);
    if (std::abs(rayDirSafe.y) < epsilon) rayDirSafe.y = std::copysignf(epsilon, rayDirSafe.y);
    if (std::abs(rayDirSafe.z) < epsilon) rayDirSafe.z = std::copysignf(epsilon, rayDirSafe.z);

    // Parametric plane coefficients
    coef.tx_coef = 1.0f / -std::abs(rayDirSafe.x);
    coef.ty_coef = 1.0f / -std::abs(rayDirSafe.y);
    coef.tz_coef = 1.0f / -std::abs(rayDirSafe.z);

    // Bias terms
    coef.tx_bias = coef.tx_coef * normOrigin.x;
    coef.ty_bias = coef.ty_coef * normOrigin.y;
    coef.tz_bias = coef.tz_coef * normOrigin.z;

    // XOR octant mirroring
    coef.octant_mask = 7;
    debugOctantMirroring(rayDir, rayDirSafe, coef.octant_mask);
    if (rayDir.x > 0.0f) { coef.octant_mask ^= 1; coef.tx_bias = 3.0f * coef.tx_coef - coef.tx_bias; }
    if (rayDir.y > 0.0f) { coef.octant_mask ^= 2; coef.ty_bias = 3.0f * coef.ty_coef - coef.ty_bias; }
    if (rayDir.z > 0.0f) { coef.octant_mask ^= 4; coef.tz_bias = 3.0f * coef.tz_coef - coef.tz_bias; }

    return coef;
}

void selectInitialOctant(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef)
{
    constexpr float axis_epsilon = 1e-5f;
    constexpr float boundary_epsilon = 0.01f;
    bool usePositionBasedSelection = (state.t_min < boundary_epsilon);

    // Compute mirrored origin for position-based selection
    float mirroredOriginX = (coef.octant_mask & 1) ? coef.normOrigin.x : (3.0f - coef.normOrigin.x);
    float mirroredOriginY = (coef.octant_mask & 2) ? coef.normOrigin.y : (3.0f - coef.normOrigin.y);
    float mirroredOriginZ = (coef.octant_mask & 4) ? coef.normOrigin.z : (3.0f - coef.normOrigin.z);

    DEBUG_PRINT("\n=== selectInitialOctant ===\n");
    DEBUG_PRINT("  usePositionBased=%d, t_min=%.6f, octant_mask=%d\n",
                usePositionBasedSelection ? 1 : 0, state.t_min, coef.octant_mask);
    DEBUG_PRINT("  mirroredOrigin=(%.6f, %.6f, %.6f)\n", mirroredOriginX, mirroredOriginY, mirroredOriginZ);

    // X axis selection
    if (std::abs(coef.rayDir.x) < axis_epsilon || usePositionBasedSelection) {
        if (mirroredOriginX >= 1.5f) { state.idx |= 1; state.pos.x = 1.5f; }
    } else {
        if (1.5f * coef.tx_coef - coef.tx_bias > state.t_min) { state.idx ^= 1; state.pos.x = 1.5f; }
    }

    // Y axis selection
    if (std::abs(coef.rayDir.y) < axis_epsilon || usePositionBasedSelection) {
        if (mirroredOriginY >= 1.5f) { state.idx |= 2; state.pos.y = 1.5f; }
    } else {
        if (1.5f * coef.ty_coef - coef.ty_bias > state.t_min) { state.idx ^= 2; state.pos.y = 1.5f; }
    }

    // Z axis selection
    if (std::abs(coef.rayDir.z) < axis_epsilon || usePositionBasedSelection) {
        if (mirroredOriginZ >= 1.5f) { state.idx |= 4; state.pos.z = 1.5f; }
    } else {
        if (1.5f * coef.tz_coef - coef.tz_bias > state.t_min) { state.idx ^= 4; state.pos.z = 1.5f; }
    }

    DEBUG_PRINT("  RESULT: idx=%d, pos=(%.3f, %.3f, %.3f)\n", state.idx, state.pos.x, state.pos.y, state.pos.z);
}

float computeCorrectedTcMax(
    float tx_corner, float ty_corner, float tz_corner,
    const glm::vec3& rayDir, float t_max)
{
    constexpr float corner_threshold = 1000.0f;
    constexpr float dir_epsilon = 1e-5f;

    bool useXCorner = (std::abs(rayDir.x) >= dir_epsilon);
    bool useYCorner = (std::abs(rayDir.y) >= dir_epsilon);
    bool useZCorner = (std::abs(rayDir.z) >= dir_epsilon);

    float tx_valid = (useXCorner && std::abs(tx_corner) < corner_threshold) ? tx_corner : t_max;
    float ty_valid = (useYCorner && std::abs(ty_corner) < corner_threshold) ? ty_corner : t_max;
    float tz_valid = (useZCorner && std::abs(tz_corner) < corner_threshold) ? tz_corner : t_max;

    return std::min({tx_valid, ty_valid, tz_valid});
}

void computeVoxelCorners(
    const glm::vec3& pos,
    const ESVORayCoefficients& coef,
    float& tx_corner, float& ty_corner, float& tz_corner)
{
    tx_corner = pos.x * coef.tx_coef - coef.tx_bias;
    ty_corner = pos.y * coef.ty_coef - coef.ty_bias;
    tz_corner = pos.z * coef.tz_coef - coef.tz_bias;
}

} // anonymous namespace

// ============================================================================
// Legacy Code - Retained for fallback leaf hit path (rarely used)
// ============================================================================

namespace {
// Helper to compute legacy leaf hit result (without brick system)
ISVOStructure::RayHit computeLegacyLeafHit(
    const LaineKarrasOctree* octree,
    const glm::vec3& origin,
    const glm::vec3& rayDir,
    const glm::vec3& worldSize,
    float tEntry,
    float t_min,
    float tv_max,
    int scale,
    int esvoScale)
{
    // Determine dominant axis for conversion
    float worldSizeLength;
    glm::vec3 absDir = glm::abs(rayDir);
    if (absDir.x > absDir.y && absDir.x > absDir.z) {
        worldSizeLength = worldSize.x;
    } else if (absDir.y > absDir.z) {
        worldSizeLength = worldSize.y;
    } else {
        worldSizeLength = worldSize.z;
    }

    float t_min_world = tEntry + t_min * worldSizeLength;
    float tv_max_world = tEntry + tv_max * worldSizeLength;

    ISVOStructure::RayHit hit{};
    hit.hit = true;
    hit.tMin = t_min_world;
    hit.tMax = tv_max_world;
    hit.hitPoint = origin + rayDir * t_min_world;
    hit.scale = scale;

    float voxelSize = octree->getVoxelSize(hit.scale);
    hit.normal = computeSurfaceNormal(octree, hit.hitPoint, voxelSize);
    hit.entity = gaia::ecs::Entity();

    return hit;
}
} // anonymous namespace

float LaineKarrasOctree::getVoxelSize(int scale) const {
    // scale parameter is user scale (0 to m_maxLevels-1)
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
                voxelOccupied = false;
                return std::nullopt;
            }

            // Evaluate key predicate (respects custom solidity tests)
            voxelOccupied = m_registry->evaluateKey(keyAttributeValue);
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
        glm::vec3 voxelWorldPos = brickWorldMin + glm::vec3(currentVoxel) * brickVoxelSize;

        // Check if entity is valid and has Density component (solidity test)
        bool voxelOccupied = false;
        if (m_voxelWorld != nullptr) {
            // Use GaiaVoxelWorld's clean API for component access
            using namespace GaiaVoxel;
            auto density = m_voxelWorld->getComponentValue<Density>(entity);
            if (density.has_value() && *density > 0.0f) {
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
            // Clamp to non-negative (ray can start inside voxel due to FP precision)
            hitT = std::max(hitT, 0.0f);

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

// ============================================================================
// Octree Rebuild API (Phase 3)
// ============================================================================

void LaineKarrasOctree::rebuild(::GaiaVoxel::GaiaVoxelWorld& world, const glm::vec3& worldMin, const glm::vec3& worldMax) {
    using namespace ::GaiaVoxel;

    // 1. Acquire write lock (blocks rendering)
    std::unique_lock<std::shared_mutex> lock(m_renderLock);

    // 2. Initialize VolumeGrid for integer grid coordinate handling
    //    This enables proper voxel lookup using integer-aligned coordinates
    m_volumeGrid = VolumeGrid::fromWorldAABB(AABB{worldMin, worldMax});

    // 3. Initialize transform: world space → normalized [0,1]³ space
    m_transform = VolumeTransform::fromWorldBounds(worldMin, worldMax);

    // 3. Clear existing octree structure
    m_octree = std::make_unique<Octree>();
    m_octree->root = std::make_unique<OctreeBlock>();
    m_octree->worldMin = worldMin;
    m_octree->worldMax = worldMax;
    m_octree->maxLevels = m_maxLevels;
    m_worldMin = worldMin;
    m_worldMax = worldMax;

    // 3b. Setup local-to-world transformation matrices
    // Local space: [0, worldSize] integer grid (voxels at integer positions)
    // World space: [worldMin, worldMax]
    // localToWorld = translate(worldMin) transforms local [0, worldSize] → world [worldMin, worldMax]
    m_localToWorld = glm::translate(glm::mat4(1.0f), worldMin);
    m_worldToLocal = glm::inverse(m_localToWorld);

    // 4. Calculate brick grid dimensions in normalized [0,1]³ space
    int brickDepth = m_brickDepthLevels;
    int brickSideLength = 1 << brickDepth;  // 2^3 = 8 for depth 3

    glm::vec3 worldSize = worldMax - worldMin;
    // Assume voxelSize = 1.0, so worldSize in voxels = worldSize
    int voxelsPerAxis = static_cast<int>(worldSize.x);  // Assume uniform cube world
    int bricksPerAxis = (voxelsPerAxis + brickSideLength - 1) / brickSideLength;  // Round up
    float brickWorldSize = worldSize.x / static_cast<float>(bricksPerAxis);

    // Store brick grid info for use during ray casting
    m_octree->bricksPerAxis = bricksPerAxis;
    m_octree->brickSideLength = brickSideLength;

    // Voxel size = brick world size / brick side length
    float voxelSize = brickWorldSize / static_cast<float>(brickSideLength);

    // Normalized brick size (in [0,1]³ space)
    float normalizedBrickSize = 1.0f / static_cast<float>(bricksPerAxis);

    // 5. PHASE 1: Collect populated bricks using DIRECT BINNING (O(N) approach)
    // Query all solid voxels once, then bin them by brick coordinate.
    // This is O(N) where N = number of voxels, much faster than the previous
    // O(N × Q) top-down approach where Q = number of spatial queries.
    struct BrickInfo {
        glm::ivec3 gridCoord;      // Brick grid coordinate (0 to bricksPerAxis-1)
        glm::vec3 normalizedMin;   // Normalized [0,1]³ minimum corner
        glm::vec3 worldMin;        // World-space minimum corner (for entity query)
        size_t entityCount;        // Number of entities in brick
    };

    std::vector<BrickInfo> populatedBricks;
    size_t totalVoxels = 0;

    std::cout << "[LaineKarrasOctree] Rebuilding: bricksPerAxis=" << bricksPerAxis
              << ", brickSideLength=" << brickSideLength << std::endl;

    // Step 1: Query all solid voxels once (O(N))
    std::cout << "[LaineKarrasOctree] Querying all solid voxels..." << std::endl;
    auto allVoxels = world.querySolidVoxels();
    std::cout << "[LaineKarrasOctree] Found " << allVoxels.size() << " solid voxels" << std::endl;

    // Step 2: Bin voxels by brick coordinate using a hash map
    // Key = linearized brick coordinate, Value = count
    std::unordered_map<uint64_t, size_t> brickCounts;
    brickCounts.reserve(allVoxels.size() / 64);  // Estimate: ~64 voxels per brick average

    auto toBrickKey = [brickSideLength, bricksPerAxis](const glm::vec3& pos) -> uint64_t {
        int bx = std::clamp(static_cast<int>(pos.x) / brickSideLength, 0, bricksPerAxis - 1);
        int by = std::clamp(static_cast<int>(pos.y) / brickSideLength, 0, bricksPerAxis - 1);
        int bz = std::clamp(static_cast<int>(pos.z) / brickSideLength, 0, bricksPerAxis - 1);
        return static_cast<uint64_t>(bx) |
               (static_cast<uint64_t>(by) << 16) |
               (static_cast<uint64_t>(bz) << 32);
    };

    auto fromBrickKey = [](uint64_t key) -> glm::ivec3 {
        return glm::ivec3(
            static_cast<int>(key & 0xFFFF),
            static_cast<int>((key >> 16) & 0xFFFF),
            static_cast<int>((key >> 32) & 0xFFFF)
        );
    };

    std::cout << "[LaineKarrasOctree] Binning voxels by brick..." << std::endl;
    for (const auto& entity : allVoxels) {
        auto posOpt = world.getPosition(entity);
        if (!posOpt) continue;  // Skip entities without position
        uint64_t key = toBrickKey(*posOpt);
        brickCounts[key]++;
        totalVoxels++;
    }

    std::cout << "[LaineKarrasOctree] Found " << brickCounts.size() << " populated bricks" << std::endl;

    // Debug: Print first 10 populated brick coordinates
    std::cout << "[LaineKarrasOctree] First 10 populated bricks:" << std::endl;
    int printCount = 0;
    for (const auto& [key, count] : brickCounts) {
        if (printCount >= 10) break;
        glm::ivec3 coord = fromBrickKey(key);
        std::cout << "  Brick (" << coord.x << ", " << coord.y << ", " << coord.z
                  << ") with " << count << " voxels" << std::endl;
        printCount++;
    }

    // Check specific bricks that trace showed
    std::cout << "[LaineKarrasOctree] Checking for specific bricks:" << std::endl;
    auto checkBrick = [&](int x, int y, int z) {
        uint64_t key = static_cast<uint64_t>(x) |
                       (static_cast<uint64_t>(y) << 16) |
                       (static_cast<uint64_t>(z) << 32);
        if (brickCounts.count(key)) {
            std::cout << "  Brick (" << x << ", " << y << ", " << z << ") EXISTS with " << brickCounts[key] << " voxels" << std::endl;
        } else {
            std::cout << "  Brick (" << x << ", " << y << ", " << z << ") NOT FOUND" << std::endl;
        }
    };
    checkBrick(5, 7, 13);  // From trace
    checkBrick(4, 6, 13);  // Nearby
    checkBrick(0, 7, 13);  // Left wall at same Y,Z

    // Step 3: Convert hash map to sorted brick list
    populatedBricks.reserve(brickCounts.size());
    for (const auto& [key, count] : brickCounts) {
        glm::ivec3 gridCoord = fromBrickKey(key);
        glm::vec3 brickNormalizedMin = glm::vec3(
            gridCoord.x * normalizedBrickSize,
            gridCoord.y * normalizedBrickSize,
            gridCoord.z * normalizedBrickSize
        );
        glm::vec3 brickWorldMin = glm::vec3(gridCoord) * static_cast<float>(brickSideLength) + worldMin;

        populatedBricks.push_back(BrickInfo{
            gridCoord,
            brickNormalizedMin,
            brickWorldMin,
            count
        });
    }

    if (populatedBricks.empty()) {
        m_octree->totalVoxels = 0;
        return;
    }

    // 5. PHASE 2: Build hierarchy bottom-up with child mapping
    // Based on VoxelInjection.cpp compaction algorithm

    // Node key for mapping grid coordinates to descriptor indices
    struct NodeKey {
        int depth;
        glm::ivec3 coord;

        bool operator==(const NodeKey& other) const {
            return depth == other.depth && coord == other.coord;
        }
    };

    struct NodeKeyHash {
        size_t operator()(const NodeKey& key) const {
            size_t h1 = std::hash<int>{}(key.depth);
            size_t h2 = std::hash<int>{}(key.coord.x);
            size_t h3 = std::hash<int>{}(key.coord.y);
            size_t h4 = std::hash<int>{}(key.coord.z);
            return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
        }
    };

    std::unordered_map<NodeKey, uint32_t, NodeKeyHash> nodeToDescriptorIndex;
    std::vector<ChildDescriptor> tempDescriptors;
    std::vector<EntityBrickView> tempBrickViews;

    // Child mapping: parentDescriptorIndex → [8 child descriptor indices]
    // UINT32_MAX means octant is empty
    std::unordered_map<uint32_t, std::array<uint32_t, 8>> childMapping;

    // Mapping from descriptor index → brick view index (only for brick-level descriptors)
    std::unordered_map<uint32_t, uint32_t> descriptorToBrickView;

    // Mapping from brick grid coordinates → brick view index
    // Key: (brickX | brickY << 10 | brickZ << 20)
    std::unordered_map<uint32_t, uint32_t> brickGridToBrickView;

    // Initialize brick-level nodes (depth = brickDepth)
    for (const auto& brick : populatedBricks) {
        NodeKey key{brickDepth, brick.gridCoord};
        uint32_t descriptorIndex = static_cast<uint32_t>(tempDescriptors.size());
        uint32_t brickViewIndex = static_cast<uint32_t>(tempBrickViews.size());
        nodeToDescriptorIndex[key] = descriptorIndex;
        descriptorToBrickView[descriptorIndex] = brickViewIndex;

        // Add grid-to-brickview mapping for position-based lookup
        uint32_t gridKey = static_cast<uint32_t>(brick.gridCoord.x) |
                          (static_cast<uint32_t>(brick.gridCoord.y) << 10) |
                          (static_cast<uint32_t>(brick.gridCoord.z) << 20);
        brickGridToBrickView[gridKey] = brickViewIndex;

        // Create brick descriptor (all children are leaf voxels)
        // SPARSE BRICK ARCHITECTURE: Store brickIndex directly in descriptor
        // The brickViewIndex becomes the sparse brick array index
        ChildDescriptor desc{};
        desc.validMask = 0xFF;  // All 8 octants populated (simplified)
        desc.leafMask = 0xFF;   // All children are leaves (voxel level)
        desc.childPointer = 0;  // Not used for brick descriptors (no children)
        desc.farBit = 0;
        desc.setBrickIndex(brickViewIndex);  // Store sparse brick index in contourPointer field

        tempDescriptors.push_back(desc);

        // Create EntityBrickView for this brick using LOCAL grid coordinates
        // In local-space architecture:
        // - Voxels are stored with LOCAL Morton keys (relative to volume origin)
        // - Brick's localGridOrigin = brickGridCoord * brickSideLength
        // - E.g., brick (0,0,0) → localGridOrigin (0,0,0)
        //         brick (1,0,0) → localGridOrigin (8,0,0) for brickSideLength=8
        glm::ivec3 localGridOrigin = brick.gridCoord * brickSideLength;
        EntityBrickView brickView(world, localGridOrigin, static_cast<uint8_t>(brickDepth), worldMin, EntityBrickView::LocalSpace);
        tempBrickViews.push_back(brickView);
    }

    // Build parent levels bottom-up
    DEBUG_PRINT("[rebuild] Building hierarchy: brickDepth=%d, maxLevels=%d\n", brickDepth, m_maxLevels);
    for (int currentDepth = brickDepth + 1; currentDepth <= m_maxLevels; ++currentDepth) {
        DEBUG_PRINT("[rebuild] Processing depth %d, nodeToDescriptorIndex.size()=%zu\n",
                    currentDepth, nodeToDescriptorIndex.size());
        struct IVec3Hash {
            size_t operator()(const glm::ivec3& v) const {
                size_t h1 = std::hash<int>{}(v.x);
                size_t h2 = std::hash<int>{}(v.y);
                size_t h3 = std::hash<int>{}(v.z);
                return h1 ^ (h2 << 1) ^ (h3 << 2);
            }
        };
        std::unordered_map<glm::ivec3, std::vector<std::pair<int, uint32_t>>, IVec3Hash> parentToChildren;
        // Maps: parentCoord → [(octant, childDescriptorIndex), ...]

        // Group child nodes by parent coordinate
        int childDepth = currentDepth - 1;

        for (const auto& [key, descriptorIndex] : nodeToDescriptorIndex) {
            if (key.depth != childDepth) continue;

            // Calculate parent coordinate (divide by 2 in grid space)
            glm::ivec3 parentCoord = key.coord / 2;

            // Calculate which octant this child belongs to in parent
            glm::ivec3 octantBit = key.coord % 2;
            int octant = octantBit.x + (octantBit.y << 1) + (octantBit.z << 2);

            parentToChildren[parentCoord].push_back({octant, descriptorIndex});
        }

        DEBUG_PRINT("[rebuild] depth %d: found %zu parents from %d children at depth %d\n",
                    currentDepth, parentToChildren.size(), childDepth, childDepth);

        if (parentToChildren.empty()) {
            DEBUG_PRINT("[rebuild] No parents found, breaking\n");
            break;
        }

        // Check if we've reached the root (single parent at origin that will contain all children)
        bool isRootLevel = (parentToChildren.size() == 1 && parentToChildren.count(glm::ivec3(0, 0, 0)) == 1);
        DEBUG_PRINT("[rebuild] isRootLevel=%d\n", isRootLevel ? 1 : 0);

        // Create parent descriptors
        size_t parentsCreated = 0;
        for (const auto& [parentCoord, children] : parentToChildren) {
            uint32_t parentDescriptorIndex = static_cast<uint32_t>(tempDescriptors.size());
            NodeKey parentKey{currentDepth, parentCoord};
            nodeToDescriptorIndex[parentKey] = parentDescriptorIndex;

            // Compute validMask and leafMask from which octants have children
            uint8_t validMask = 0;
            uint8_t leafMask = 0;
            std::array<uint32_t, 8> childIndices;
            childIndices.fill(UINT32_MAX);

            for (const auto& [octant, childIndex] : children) {
                validMask |= (1 << octant);
                childIndices[octant] = childIndex;

                // If child is a brick descriptor, mark as leaf
                if (childDepth == brickDepth) {
                    leafMask |= (1 << octant);
                }
            }

            // Store child mapping for BFS reordering phase
            childMapping[parentDescriptorIndex] = childIndices;

            // Special case: when there's only 1 brick covering the whole world,
            // mark ALL octants as valid/leaf so rays from any direction can hit it
            if (bricksPerAxis == 1 && children.size() == 1) {
                validMask = 0xFF;  // All octants valid
                leafMask = 0xFF;   // All are leaves (same single brick)
                // Fill childIndices with the single brick index for all octants
                uint32_t singleBrickIndex = children[0].second;
                childIndices.fill(singleBrickIndex);
                childMapping[parentDescriptorIndex] = childIndices;
            }

            ChildDescriptor parentDesc{};
            parentDesc.validMask = validMask;
            parentDesc.leafMask = leafMask;  // Marks which children are brick descriptors (leaves)
            parentDesc.childPointer = 0;  // Will be set during BFS reordering
            parentDesc.farBit = 0;
            parentDesc.contourPointer = 0;
            parentDesc.contourMask = 0;

            tempDescriptors.push_back(parentDesc);
            parentsCreated++;
        }

        // If this is the root level (single parent at origin with all children),
        // stop building - no more parents needed above the root
        if (isRootLevel) {
            break;
        }
    }

    // 6. PHASE 3: BFS reordering for contiguous child storage

    std::vector<ChildDescriptor> finalDescriptors;
    std::vector<EntityBrickView> finalBrickViews;
    std::unordered_map<uint64_t, uint32_t> leafToBrickView;  // (newParentIndex << 3 | octant) → brickViewIndex
    std::unordered_map<uint32_t, uint32_t> oldToNewIndex;

    // Find root descriptor (highest depth in nodeToDescriptorIndex)
    uint32_t rootOldIndex = UINT32_MAX;
    int rootDepth = -1;
    for (const auto& [key, index] : nodeToDescriptorIndex) {
        if (key.depth > rootDepth) {
            rootDepth = key.depth;
            rootOldIndex = index;
        }
    }

    if (rootOldIndex == UINT32_MAX) {
        return;  // ERROR: No root node found
    }

    // BFS traversal starting from root
    struct NodeInfo {
        uint32_t oldIndex;
        uint32_t newIndex;
    };

    std::queue<NodeInfo> bfsQueue;
    bfsQueue.push({rootOldIndex, 0});
    oldToNewIndex[rootOldIndex] = 0;

    finalDescriptors.push_back(tempDescriptors[rootOldIndex]);

    while (!bfsQueue.empty()) {
        NodeInfo current = bfsQueue.front();
        bfsQueue.pop();

        const ChildDescriptor& desc = tempDescriptors[current.oldIndex];

        // Find children using child mapping
        auto it = childMapping.find(current.oldIndex);
        if (it != childMapping.end()) {
            std::vector<uint32_t> nonLeafChildren;
            std::vector<uint32_t> leafChildren;

            for (int octant = 0; octant < 8; ++octant) {
                if (!(desc.validMask & (1 << octant))) continue;  // No child in this octant

                uint32_t childOldIndex = it->second[octant];
                if (childOldIndex == UINT32_MAX) continue;

                // SPARSE BRICK ARCHITECTURE: All valid children go into finalDescriptors
                // Leaf children (bricks) have their brickIndex stored in contourPointer field
                // Non-leaf children continue BFS traversal
                if (desc.leafMask & (1 << octant)) {
                    // Leaf child (brick descriptor) - add to hierarchy
                    // The descriptor already has brickIndex set via setBrickIndex()
                    leafChildren.push_back(childOldIndex);

                    // Also maintain legacy leafToBrickView mapping for CPU ray casting
                    uint64_t key = (static_cast<uint64_t>(current.newIndex) << 3) | static_cast<uint64_t>(octant);
                    leafToBrickView[key] = tempDescriptors[childOldIndex].getBrickIndex();
                } else {
                    // Non-leaf child: will be added to finalDescriptors and continue BFS
                    nonLeafChildren.push_back(childOldIndex);
                }
            }

            // Add ALL children (both non-leaf and leaf) contiguously
            // Non-leaf first, then leaf - matches ESVO paper's child ordering
            std::vector<uint32_t> allChildren = nonLeafChildren;
            allChildren.insert(allChildren.end(), leafChildren.begin(), leafChildren.end());

            if (!allChildren.empty()) {
                // Set childPointer to where first child will be placed
                uint32_t firstChildIndex = static_cast<uint32_t>(finalDescriptors.size());
                finalDescriptors[current.newIndex].childPointer = firstChildIndex;

                // Add all children to finalDescriptors
                for (uint32_t oldChildIndex : allChildren) {
                    uint32_t newChildIndex = static_cast<uint32_t>(finalDescriptors.size());
                    oldToNewIndex[oldChildIndex] = newChildIndex;
                    finalDescriptors.push_back(tempDescriptors[oldChildIndex]);
                }

                // Only non-leaf children continue BFS traversal
                for (uint32_t oldChildIndex : nonLeafChildren) {
                    uint32_t newChildIndex = oldToNewIndex[oldChildIndex];
                    bfsQueue.push({oldChildIndex, newChildIndex});
                }
            }
        }
    }

    // Brick views stay in original order - descriptors use childPointer to reference them
    // No reordering needed since childPointer stores the brick view index
    finalBrickViews = std::move(tempBrickViews);

    // 7. Store final hierarchy in octree
    m_octree->root->childDescriptors = std::move(finalDescriptors);
    m_octree->root->brickViews = std::move(finalBrickViews);
    m_octree->root->leafToBrickView = std::move(leafToBrickView);
    m_octree->root->brickGridToBrickView = std::move(brickGridToBrickView);
    m_octree->totalVoxels = totalVoxels;

    // Lock automatically released when lock goes out of scope
}

void LaineKarrasOctree::updateBlock(const glm::vec3& blockWorldMin, uint8_t blockDepth) {
    using namespace ::GaiaVoxel;

    // 1. Acquire write lock (blocks rendering)
    std::unique_lock<std::shared_mutex> lock(m_renderLock);

    if (!m_octree || !m_octree->root) {
        return;  // Octree not initialized - call rebuild() first
    }

    // 2. Calculate brick grid coordinates from world position
    int brickSideLength = m_octree->brickSideLength;
    int bricksPerAxis = m_octree->bricksPerAxis;

    // Convert world position to brick grid coordinates
    glm::vec3 localPos = blockWorldMin - m_worldMin;
    glm::ivec3 brickCoord(
        static_cast<int>(localPos.x / static_cast<float>(brickSideLength)),
        static_cast<int>(localPos.y / static_cast<float>(brickSideLength)),
        static_cast<int>(localPos.z / static_cast<float>(brickSideLength))
    );

    // Clamp to valid range
    brickCoord = glm::clamp(brickCoord, glm::ivec3(0), glm::ivec3(bricksPerAxis - 1));

    // 3. Compute brick world bounds for entity query
    glm::vec3 brickWorldMinCalc = m_worldMin + glm::vec3(brickCoord) * static_cast<float>(brickSideLength);
    glm::vec3 brickWorldMax = brickWorldMinCalc + glm::vec3(static_cast<float>(brickSideLength));
    brickWorldMax = glm::min(brickWorldMax, m_worldMax);  // Clamp to world bounds

    // 4. Query entities in this brick region
    auto entities = m_voxelWorld->queryRegion(brickWorldMinCalc, brickWorldMax);

    // 5. Create grid key for brick lookup
    uint32_t gridKey = static_cast<uint32_t>(brickCoord.x) |
                       (static_cast<uint32_t>(brickCoord.y) << 10) |
                       (static_cast<uint32_t>(brickCoord.z) << 20);

    // 6. Find or create brick view entry
    auto& brickGridMap = m_octree->root->brickGridToBrickView;
    auto& brickViews = m_octree->root->brickViews;

    auto it = brickGridMap.find(gridKey);

    if (entities.empty()) {
        // No entities - just remove from map (don't touch vector to preserve indices)
        if (it != brickGridMap.end()) {
            brickGridMap.erase(it);
        }
    } else {
        // Has entities - create or update brick view
        // Use LOCAL grid origin (same as rebuild())
        glm::ivec3 localGridOrigin = brickCoord * brickSideLength;

        if (it != brickGridMap.end()) {
            // Update existing - use placement new since EntityBrickView has reference member
            uint32_t brickIdx = it->second;
            if (brickIdx < brickViews.size()) {
                // Destroy old and construct new in-place
                brickViews[brickIdx].~EntityBrickView();
                new (&brickViews[brickIdx]) EntityBrickView(*m_voxelWorld, localGridOrigin, blockDepth, m_worldMin, EntityBrickView::LocalSpace);
            }
        } else {
            // Create new brick view
            uint32_t newIdx = static_cast<uint32_t>(brickViews.size());
            brickViews.emplace_back(*m_voxelWorld, localGridOrigin, blockDepth, m_worldMin, EntityBrickView::LocalSpace);
            brickGridMap[gridKey] = newIdx;
        }
    }

    // Note: We don't update ChildDescriptors for partial updates because:
    // 1. The grid-based lookup (getBrickViewByGrid) is now primary
    // 2. Full hierarchy rebuild would require re-walking the tree
    // 3. For render correctness, the brickGridToBrickView map is sufficient
}

void LaineKarrasOctree::removeBlock(const glm::vec3& blockWorldMin, uint8_t blockDepth) {
    // 1. Acquire write lock (blocks rendering)
    std::unique_lock<std::shared_mutex> lock(m_renderLock);

    if (!m_octree || !m_octree->root) {
        return;  // Octree not initialized
    }

    // 2. Calculate brick grid coordinates from world position
    int brickSideLength = m_octree->brickSideLength;
    int bricksPerAxis = m_octree->bricksPerAxis;

    // Convert world position to brick grid coordinates
    glm::vec3 localPos = blockWorldMin - m_worldMin;
    glm::ivec3 brickCoord(
        static_cast<int>(localPos.x / static_cast<float>(brickSideLength)),
        static_cast<int>(localPos.y / static_cast<float>(brickSideLength)),
        static_cast<int>(localPos.z / static_cast<float>(brickSideLength))
    );

    // Clamp to valid range
    brickCoord = glm::clamp(brickCoord, glm::ivec3(0), glm::ivec3(bricksPerAxis - 1));

    // 3. Create grid key for brick lookup
    uint32_t gridKey = static_cast<uint32_t>(brickCoord.x) |
                       (static_cast<uint32_t>(brickCoord.y) << 10) |
                       (static_cast<uint32_t>(brickCoord.z) << 20);

    // 4. Remove brick view from map (don't touch vector to preserve other indices)
    auto& brickGridMap = m_octree->root->brickGridToBrickView;
    brickGridMap.erase(gridKey);

    // Note: Like updateBlock, we don't update ChildDescriptors here.
    // The grid-based lookup will return nullptr for removed bricks,
    // which castRay handles as a miss (continues traversal).
}

void LaineKarrasOctree::lockForRendering() {
    // Acquire write lock - blocks rebuild/update operations
    m_renderLock.lock();
}

void LaineKarrasOctree::unlockAfterRendering() {
    // Release write lock - allows rebuild/update operations
    m_renderLock.unlock();
}

} // namespace SVO
