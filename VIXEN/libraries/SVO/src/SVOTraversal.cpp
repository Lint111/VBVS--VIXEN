/**
 * SVOTraversal.cpp - ESVO Ray Casting Implementation
 * ==============================================================================
 * Sparse Voxel Octree (SVO) ray traversal using the Efficient Sparse Voxel
 * Octrees (ESVO) algorithm.
 *
 * REFERENCES:
 * -----------
 * [1] Laine, S. and Karras, T. "Efficient Sparse Voxel Octrees"
 *     NVIDIA Research, I3D 2010
 *     https://research.nvidia.com/publication/efficient-sparse-voxel-octrees
 *
 * [2] Amanatides, J. and Woo, A. "A Fast Voxel Traversal Algorithm for Ray Tracing"
 *     Eurographics 1987
 *     http://www.cse.yorku.ca/~amana/research/grid.pdf
 *
 * [3] NVIDIA ESVO Reference Implementation
 *     cuda/Raycast.inl (BSD 3-Clause License)
 *     Copyright (c) 2009-2011, NVIDIA Corporation
 *
 * ALGORITHM OVERVIEW:
 * -------------------
 * The ESVO traversal uses parametric ray casting in [1,2]^3 normalized space:
 * 1. Ray setup: Compute parametric coefficients and octant mirroring
 * 2. PUSH: Descend into child nodes when ray enters valid voxel
 * 3. ADVANCE: Move to next sibling when ray exits current voxel
 * 4. POP: Ascend hierarchy when ray exits parent voxel
 * 5. Brick DDA: Fine-grained voxel traversal within leaf bricks
 *
 * ==============================================================================
 */

#define NOMINMAX
#include "pch.h"
#include "LaineKarrasOctree.h"
#include "VoxelComponents.h"
#include <glm/gtc/matrix_transform.hpp>  // glm::scale / glm::translate (GPU-mirror frame)
#include <limits>
#include <cmath>
#include <cstring>
#include <bit>
#include <algorithm>

using namespace Vixen::GaiaVoxel;

namespace Vixen::SVO {

// ============================================================================
// Debug Utilities
// ============================================================================
// Compile-time toggleable debug output for ray traversal

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

    /**
     * Compute surface normal via central differencing
     *
     * Uses 6-sample gradient computation (standard in graphics):
     * gradient = (sample_neg - sample_pos) for each axis
     */
    inline glm::vec3 computeSurfaceNormal(
        const LaineKarrasOctree* octree,
        const glm::vec3& hitPos,
        float voxelSize)
    {
        const float offset = voxelSize * 0.5f;

        bool xPos = octree->voxelExists(hitPos + glm::vec3(offset, 0.0f, 0.0f), 0);
        bool xNeg = octree->voxelExists(hitPos - glm::vec3(offset, 0.0f, 0.0f), 0);
        bool yPos = octree->voxelExists(hitPos + glm::vec3(0.0f, offset, 0.0f), 0);
        bool yNeg = octree->voxelExists(hitPos - glm::vec3(0.0f, offset, 0.0f), 0);
        bool zPos = octree->voxelExists(hitPos + glm::vec3(0.0f, 0.0f, offset), 0);
        bool zNeg = octree->voxelExists(hitPos - glm::vec3(0.0f, 0.0f, offset), 0);

        glm::vec3 gradient(
            static_cast<float>(xNeg) - static_cast<float>(xPos),
            static_cast<float>(yNeg) - static_cast<float>(yPos),
            static_cast<float>(zNeg) - static_cast<float>(zPos)
        );

        float length = glm::length(gradient);
        if (length > 1e-6f) {
            return gradient / length;
        }

        return glm::vec3(0.0f, 1.0f, 0.0f);
    }

    /**
     * Check if a point is inside an axis-aligned bounding box.
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

        glm::vec3 invDir;
        for (int i = 0; i < 3; ++i) {
            if (std::abs(rayDir[i]) < epsilon) {
                if (rayOrigin[i] < boxMin[i] || rayOrigin[i] > boxMax[i]) {
                    return false;
                }
                invDir[i] = (rayDir[i] >= 0.0f) ? 1e20f : -1e20f;
            } else {
                invDir[i] = 1.0f / rayDir[i];
            }
        }

        glm::vec3 t0 = (boxMin - rayOrigin) * invDir;
        glm::vec3 t1 = (boxMax - rayOrigin) * invDir;

        glm::vec3 tNear = glm::min(t0, t1);
        glm::vec3 tFar = glm::max(t0, t1);

        tMin = std::max({tNear.x, tNear.y, tNear.z});
        tMax = std::min({tFar.x, tFar.y, tFar.z});

        return tMin <= tMax && tMax >= 0.0f;
    }

} // anonymous namespace

// ============================================================================
// Type Aliases
// ============================================================================
using ESVOTraversalState = LaineKarrasOctree::ESVOTraversalState;
using ESVORayCoefficients = LaineKarrasOctree::ESVORayCoefficients;
using AdvanceResult = LaineKarrasOctree::AdvanceResult;
using PopResult = LaineKarrasOctree::PopResult;

// ============================================================================
// Forward Declarations
// ============================================================================
namespace {

ESVORayCoefficients computeRayCoefficients(
    const glm::vec3& rayDir,
    const glm::vec3& normOrigin);

void selectInitialOctant(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef);

float computeCorrectedTcMax(
    float tx_corner, float ty_corner, float tz_corner,
    const glm::vec3& rayDir, float t_max);

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
    return castRayImpl(origin, direction, tMin, tMax, 0.0f, nullptr);
}

ISVOStructure::RayHit LaineKarrasOctree::castRayLOD(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float lodBias,
    float tMin,
    float tMax) const
{
    // Legacy lodBias interface - apply bias to LOD parameters
    LODParameters lodParams;
    lodParams.rayDirSize = lodBias;  // Use lodBias as direct cone spread
    lodParams.rayOrigSize = 0.0f;
    return castRayImpl(origin, direction, tMin, tMax, lodBias, &lodParams);
}

ISVOStructure::RayHit LaineKarrasOctree::castRayScreenSpaceLOD(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float fovY,
    int screenHeight,
    float tMin,
    float tMax) const
{
    LODParameters lodParams = LODParameters::fromCamera(fovY, screenHeight);
    return castRayImpl(origin, direction, tMin, tMax, 0.0f, &lodParams);
}

ISVOStructure::RayHit LaineKarrasOctree::castRayWithLOD(
    const glm::vec3& origin,
    const glm::vec3& direction,
    const LODParameters& lodParams,
    float tMin,
    float tMax) const
{
    return castRayImpl(origin, direction, tMin, tMax, 0.0f, &lodParams);
}

// ============================================================================
// ESVO Traversal Phase Methods
// ============================================================================

bool LaineKarrasOctree::validateRayInput(
    const glm::vec3& origin,
    const glm::vec3& direction,
    glm::vec3& rayDirOut) const
{
    if (!m_octree || !m_octree->root || m_octree->root->childDescriptors.empty()) {
        return false;
    }

    rayDirOut = glm::normalize(direction);
    float rayLength = glm::length(rayDirOut);
    if (rayLength < 1e-6f) {
        return false;
    }

    if (glm::any(glm::isnan(origin)) || glm::any(glm::isnan(rayDirOut)) ||
        glm::any(glm::isinf(origin)) || glm::any(glm::isinf(rayDirOut))) {
        return false;
    }

    return true;
}

void LaineKarrasOctree::initializeTraversalState(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef,
    CastStack& stack) const
{
    const ChildDescriptor* rootDesc = &m_octree->root->childDescriptors[0];
    const int minScale = ESVO_MAX_SCALE - m_maxLevels + 1;
    for (int esvoScale = minScale; esvoScale <= ESVO_MAX_SCALE; esvoScale++) {
        stack.push(esvoScale, rootDesc, state.t_max);
    }

    state.scale = ESVO_MAX_SCALE;
    state.parent = rootDesc;
    state.child_descriptor = 0;
    state.idx = 0;
    state.pos = glm::vec3(1.0f, 1.0f, 1.0f);
    state.scale_exp2 = 0.5f;

    selectInitialOctant(state, coef);
}

void LaineKarrasOctree::fetchChildDescriptor(ESVOTraversalState& state, const ESVORayCoefficients& coef) const
{
    if (state.child_descriptor == 0) {
        state.mirroredValidMask = mirrorMask(state.parent->validMask, coef.octant_mask);
        state.mirroredLeafMask = mirrorMask(state.parent->leafMask, coef.octant_mask);

        uint32_t nonLeafMask = ~state.mirroredLeafMask & 0xFF;
        state.child_descriptor = nonLeafMask |
                     (static_cast<uint64_t>(state.mirroredValidMask) << 8) |
                     (static_cast<uint64_t>(state.parent->childPointer) << 16);
    }
}

bool LaineKarrasOctree::checkChildValidity(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef,
    bool& isLeaf,
    float& tv_max) const
{
    bool child_valid = (state.mirroredValidMask & (1u << state.idx)) != 0;
    isLeaf = (state.mirroredLeafMask & (1u << state.idx)) != 0;

    int currentUserScale = esvoToUserScale(state.scale);
    int brickUserScale = m_maxLevels - m_brickDepthLevels;
    if (currentUserScale == brickUserScale && child_valid) {
        isLeaf = true;
    }

    if (!child_valid || state.t_min > state.t_max) {
        return false;
    }

    float tx_corner, ty_corner, tz_corner;
    computeVoxelCorners(state.pos, coef, tx_corner, ty_corner, tz_corner);

    float tc_max_corrected = computeCorrectedTcMax(tx_corner, ty_corner, tz_corner, coef.rayDir, state.t_max);
    tv_max = std::min(state.t_max, tc_max_corrected);

    float half = state.scale_exp2 * 0.5f;
    state.tx_center = half * coef.tx_coef + tx_corner;
    state.ty_center = half * coef.ty_coef + ty_corner;
    state.tz_center = half * coef.tz_coef + tz_corner;

    return state.t_min <= tv_max;
}

void LaineKarrasOctree::executePushPhase(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef,
    CastStack& stack,
    float tv_max) const
{
    float tx_corner, ty_corner, tz_corner;
    computeVoxelCorners(state.pos, coef, tx_corner, ty_corner, tz_corner);
    float tc_max = std::min({tx_corner, ty_corner, tz_corner});

    if (tc_max < state.h) {
        stack.push(state.scale, state.parent, state.t_max);
    }
    state.h = tc_max;

    int worldIdx = mirroredToWorldOctant(state.idx, coef.octant_mask);

    uint8_t nonLeafMask = ~state.parent->leafMask & state.parent->validMask;
    uint32_t mask_before_child = (1u << worldIdx) - 1;
    uint32_t nonleaf_before_child = nonLeafMask & mask_before_child;
    uint32_t child_offset = std::popcount(nonleaf_before_child);

    uint32_t child_index = state.parent->childPointer + child_offset;

    if (child_index >= m_octree->root->childDescriptors.size()) {
        return;
    }

    state.parent = &m_octree->root->childDescriptors[child_index];

    state.idx = 0;
    state.scale--;
    float half = state.scale_exp2 * 0.5f;
    state.scale_exp2 = half;

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

    state.t_max = tv_max;
    state.child_descriptor = 0;
}

AdvanceResult LaineKarrasOctree::executeAdvancePhase(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef) const
{
    float tx_corner, ty_corner, tz_corner;
    computeVoxelCorners(state.pos, coef, tx_corner, ty_corner, tz_corner);

    constexpr float dir_epsilon = 1e-5f;
    bool canStepX = (std::abs(coef.rayDir.x) >= dir_epsilon);
    bool canStepY = (std::abs(coef.rayDir.y) >= dir_epsilon);
    bool canStepZ = (std::abs(coef.rayDir.z) >= dir_epsilon);

    float tc_max_corrected = computeCorrectedTcMax(tx_corner, ty_corner, tz_corner, coef.rayDir, state.t_max);

    if (tc_max_corrected == std::numeric_limits<float>::max()) {
        tc_max_corrected = std::max({
            canStepX ? tx_corner : -std::numeric_limits<float>::max(),
            canStepY ? ty_corner : -std::numeric_limits<float>::max(),
            canStepZ ? tz_corner : -std::numeric_limits<float>::max()
        });
    }

    int step_mask = 0;
    if (canStepX && tx_corner <= tc_max_corrected) { step_mask ^= 1; state.pos.x -= state.scale_exp2; }
    if (canStepY && ty_corner <= tc_max_corrected) { step_mask ^= 2; state.pos.y -= state.scale_exp2; }
    if (canStepZ && tz_corner <= tc_max_corrected) { step_mask ^= 4; state.pos.z -= state.scale_exp2; }

    state.t_min = std::max(tc_max_corrected, 0.0f);

    state.idx ^= step_mask;

    if ((state.idx & step_mask) != 0) {
        return AdvanceResult::POP_NEEDED;
    }

    return AdvanceResult::CONTINUE;
}

PopResult LaineKarrasOctree::executePopPhase(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef,
    CastStack& stack,
    int step_mask) const
{
    if (state.scale == ESVO_MAX_SCALE) {
        if (state.t_min > state.t_max ||
            state.pos.x < 1.0f || state.pos.x >= 2.0f ||
            state.pos.y < 1.0f || state.pos.y >= 2.0f ||
            state.pos.z < 1.0f || state.pos.z >= 2.0f) {
            DEBUG_PRINT("  POP: Exiting octree - pos=(%.3f,%.3f,%.3f) t=[%.4f,%.4f]\n",
                        state.pos.x, state.pos.y, state.pos.z, state.t_min, state.t_max);
            return PopResult::EXIT_OCTREE;
        }
        state.child_descriptor = 0;
        return PopResult::CONTINUE;
    }

    const int MAX_RES = 1 << ESVO_MAX_SCALE;

    auto floatToInt = [MAX_RES](float f) -> uint32_t {
        float clamped = std::max(0.0f, std::min(f, 1.0f));
        return static_cast<uint32_t>(std::max(0.0f, std::min(clamped * MAX_RES, static_cast<float>(MAX_RES - 1))));
    };

    uint32_t pos_x_int = floatToInt(std::max(0.0f, state.pos.x - 1.0f));
    uint32_t pos_y_int = floatToInt(std::max(0.0f, state.pos.y - 1.0f));
    uint32_t pos_z_int = floatToInt(std::max(0.0f, state.pos.z - 1.0f));

    uint32_t next_x_int = (step_mask & 1) ? floatToInt(std::max(0.0f, state.pos.x + state.scale_exp2 - 1.0f)) : pos_x_int;
    uint32_t next_y_int = (step_mask & 2) ? floatToInt(std::max(0.0f, state.pos.y + state.scale_exp2 - 1.0f)) : pos_y_int;
    uint32_t next_z_int = (step_mask & 4) ? floatToInt(std::max(0.0f, state.pos.z + state.scale_exp2 - 1.0f)) : pos_z_int;

    uint32_t differing_bits = 0;
    if ((step_mask & 1) != 0) differing_bits |= (pos_x_int ^ next_x_int);
    if ((step_mask & 2) != 0) differing_bits |= (pos_y_int ^ next_y_int);
    if ((step_mask & 4) != 0) differing_bits |= (pos_z_int ^ next_z_int);

    if (differing_bits == 0) {
        return PopResult::EXIT_OCTREE;
    }

    int highest_bit = 31 - std::countl_zero(differing_bits);
    state.scale = highest_bit;

    int minESVOScale = ESVO_MAX_SCALE - effectiveLevels() + 1;
    if (state.scale < minESVOScale || state.scale > ESVO_MAX_SCALE) {
        return PopResult::EXIT_OCTREE;
    }

    int exp_val = state.scale - ESVO_MAX_SCALE + 127;
    state.scale_exp2 = std::bit_cast<float>(static_cast<uint32_t>(exp_val << 23));

    state.parent = stack.getNode(state.scale);
    state.t_max = stack.getTMax(state.scale);

    if (state.parent == nullptr) {
        return PopResult::EXIT_OCTREE;
    }

    int shift_amount = ESVO_MAX_SCALE - state.scale;
    if (shift_amount < 0 || shift_amount >= 32) {
        return PopResult::EXIT_OCTREE;
    }

    uint32_t mask = ~((1u << shift_amount) - 1);
    pos_x_int &= mask;
    pos_y_int &= mask;
    pos_z_int &= mask;

    auto intToFloat = [MAX_RES](uint32_t i) -> float {
        return 1.0f + static_cast<float>(i) / static_cast<float>(MAX_RES);
    };

    state.pos.x = intToFloat(pos_x_int);
    state.pos.y = intToFloat(pos_y_int);
    state.pos.z = intToFloat(pos_z_int);

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

// ============================================================================
// Main Ray Casting Implementation
// ============================================================================

namespace {
// Levels needed to cover the brick grid: ceil(log2(bricksPerAxis)).
int gridLevelsFor(int bricksPerAxis) {
    int levels = 0;
    while ((1 << levels) < bricksPerAxis) ++levels;
    return levels;
}
}  // namespace

// How many node-tree levels the build fell short of spanning the full frame.
// frameDepth = brickDepth + log2(bricksPerAxis) is the node depth a frame-spanning
// tree converges at; a clustered sparse tree converges shallower (smaller rootDepth),
// leaving a shortfall. Bodies/shells/dense/hollow-but-spanning scenes have zero
// shortfall (rootDepth == frameDepth) and keep the full m_maxLevels resolution.
int LaineKarrasOctree::rootShortfall() const {
    if (!m_octree || m_octree->rootDepth <= 0) return 0;
    const int frameDepth = m_brickDepthLevels + gridLevelsFor(std::max(1, m_octree->bricksPerAxis));
    return std::max(0, frameDepth - m_octree->rootDepth);
}

int LaineKarrasOctree::effectiveLevels() const {
    // Full resolution (m_maxLevels — bricks add dense sub-voxels beyond the node tree),
    // reduced only by the levels a shallow-rooted clustered tree failed to span.
    return std::max(1, m_maxLevels - rootShortfall());
}

glm::vec3 LaineKarrasOctree::effectiveWorldMax() const {
    if (!m_octree) return m_worldMax;
    const int shortfall = rootShortfall();
    if (shortfall == 0) return m_worldMax;
    // The root covers a 2^-shortfall fraction of the frame per axis.
    const float frac = 1.0f / static_cast<float>(1u << shortfall);
    return m_worldMin + (m_worldMax - m_worldMin) * frac;
}

ISVOStructure::RayHit LaineKarrasOctree::castRayImpl(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float tMin,
    float tMax,
    float lodBias,
    const LODParameters* lodParams) const
{
    ISVOStructure::RayHit miss{};
    miss.hit = false;

    // GPU-PARITY PATH: for a BODY octree on the non-LOD (full-detail) path, run the
    // faithful GPU-shader mirror so collision / nearest-voxel / ray queries against
    // bodies match the renderer bit-for-bit. Other octrees (mesh-voxelized, arbitrary
    // frames) and the screen-space-LOD case keep the legacy ESVO+brick path unchanged.
    if (m_isBodyOctree && lodParams == nullptr) {
        return castRayGpuMirror(origin, direction, tMin, tMax);
    }

    glm::vec3 rayDir;
    if (!validateRayInput(origin, direction, rayDir)) {
        return miss;
    }

    // Traverse within the ACTUAL root's domain (== full frame for full-depth trees;
    // smaller for sparse clustered content whose root converged early — see
    // Octree::rootDepth). Using the full frame on a shallow-rooted tree shifts every
    // scale interpretation and rays miss real voxels.
    const glm::vec3 domainMax = effectiveWorldMax();
    bool rayStartsInside = isPointInsideAABB(origin, m_worldMin, domainMax);

    float tEntry, tExit;
    if (!intersectAABB(origin, rayDir, m_worldMin, domainMax, tEntry, tExit)) {
        return miss;
    }

    tEntry = std::max(tEntry, tMin);
    tExit = std::min(tExit, tMax);
    if (tEntry >= tExit || tExit < 0.0f) {
        return miss;
    }

    float tRayStart = rayStartsInside ? 0.0f : std::max(0.0f, tEntry);
    glm::vec3 rayEntryPoint = origin + rayDir * tRayStart;
    glm::vec3 worldSize = domainMax - m_worldMin;
    glm::vec3 normOrigin = (rayEntryPoint - m_worldMin) / worldSize + glm::vec3(1.0f);

    // Precompute world-space ray length for LOD distance conversion
    // ESVO t-values are in [0,1] normalized space; multiply by worldRayLength for distance
    float worldRayLength = glm::length(worldSize);

    ESVORayCoefficients coef = computeRayCoefficients(rayDir, normOrigin);

    ESVOTraversalState state;

    DEBUG_PRINT("\n=== Interior Ray Detection ===\n");
    DEBUG_PRINT("  rayStartsInside=%d\n", rayStartsInside ? 1 : 0);
    DEBUG_PRINT("  origin=(%.3f, %.3f, %.3f), tEntry=%.6f, tExit=%.6f\n",
                origin.x, origin.y, origin.z, tEntry, tExit);
    DEBUG_PRINT("  worldBounds=[(%.3f,%.3f,%.3f), (%.3f,%.3f,%.3f)]\n",
                m_worldMin.x, m_worldMin.y, m_worldMin.z, m_worldMax.x, m_worldMax.y, m_worldMax.z);
    DEBUG_PRINT("  normOrigin=(%.6f, %.6f, %.6f)\n", normOrigin.x, normOrigin.y, normOrigin.z);

    if (rayStartsInside) {
        state.t_min = 0.0f;
        state.t_max = std::min({coef.tx_coef - coef.tx_bias,
                                coef.ty_coef - coef.ty_bias,
                                coef.tz_coef - coef.tz_bias});
        state.t_max = std::min(state.t_max, 1.0f);
        DEBUG_PRINT("  INTERIOR: state.t_min=%.6f, state.t_max=%.6f\n", state.t_min, state.t_max);
    } else {
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

    const int maxIter = 500;
    int minESVOScale = ESVO_MAX_SCALE - effectiveLevels() + 1;

    DEBUG_PRINT("\n=== Main Traversal Loop ===\n");
    DEBUG_PRINT("  minESVOScale=%d, maxLevels=%d, brickDepthLevels=%d\n", minESVOScale, m_maxLevels, m_brickDepthLevels);
    DEBUG_PRINT("  bricksPerAxis=%d, brickSideLength=%d\n",
                m_octree ? m_octree->bricksPerAxis : -1,
                m_octree ? m_octree->brickSideLength : -1);

    while (state.scale >= minESVOScale && state.scale <= ESVO_MAX_SCALE && state.iter < maxIter) {
        ++state.iter;

        fetchChildDescriptor(state, coef);

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
            // ================================================================
            // Screen-Space LOD Termination Check (ESVO Raycast.inl line 181)
            // ================================================================
            // Check if voxel projects to less than one pixel.
            // Formula: tc_max * ray.dir_sz + ray_orig_sz >= scale_exp2
            //
            // If the projected pixel size at current distance exceeds the
            // voxel size, terminate at this LOD level (coarse detail acceptable).
            // ================================================================
            if (lodParams != nullptr && lodParams->isEnabled()) {
                // Convert ESVO normalized t to world-space distance
                // state.t_min is entry into current voxel in [0,1] normalized space
                float worldDistance = (tRayStart + state.t_min * worldRayLength);

                // Get world-space voxel size at current scale
                float worldVoxelSize = esvoScaleToWorldSize(state.scale, worldSize.x);

                // Check LOD termination condition
                if (lodParams->shouldTerminate(worldDistance, worldVoxelSize)) {
                    DEBUG_PRINT("  LOD TERMINATE: distance=%.3f, voxelSize=%.3f, projectedSize=%.3f\n",
                                worldDistance, worldVoxelSize,
                                lodParams->getProjectedPixelSize(worldDistance));

                    // Return hit at current coarse LOD level
                    // This voxel is smaller than a pixel, so we accept it
                    ISVOStructure::RayHit lodHit{};
                    lodHit.hit = true;
                    lodHit.entity = gaia::ecs::Entity();  // No specific entity at internal node
                    lodHit.hitPoint = origin + rayDir * worldDistance;
                    lodHit.normal = glm::vec3(0.0f, 1.0f, 0.0f);  // Placeholder normal
                    lodHit.tMin = worldDistance;
                    lodHit.tMax = worldDistance + worldVoxelSize;
                    lodHit.scale = esvoToUserScale(state.scale);

                    return lodHit;
                }
            }

            if (isLeaf) {
                auto leafResult = handleLeafHit(state, coef, origin, tRayStart, tEntry, tExit, tv_max);

                if (leafResult.has_value()) {
                    return leafResult.value();
                }

                state.t_min = tv_max;
                skipToAdvance = true;
            }

            if (!skipToAdvance) {
                executePushPhase(state, coef, stack, tv_max);
                continue;
            }
        }

        AdvanceResult advResult = executeAdvancePhase(state, coef);

        if (advResult == AdvanceResult::POP_NEEDED) {
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

    constexpr float epsilon = 1e-5f;
    glm::vec3 rayDirSafe = rayDir;
    if (std::abs(rayDirSafe.x) < epsilon) rayDirSafe.x = std::copysign(epsilon, rayDirSafe.x);
    if (std::abs(rayDirSafe.y) < epsilon) rayDirSafe.y = std::copysign(epsilon, rayDirSafe.y);
    if (std::abs(rayDirSafe.z) < epsilon) rayDirSafe.z = std::copysign(epsilon, rayDirSafe.z);

    coef.tx_coef = 1.0f / -std::abs(rayDirSafe.x);
    coef.ty_coef = 1.0f / -std::abs(rayDirSafe.y);
    coef.tz_coef = 1.0f / -std::abs(rayDirSafe.z);

    coef.tx_bias = coef.tx_coef * normOrigin.x;
    coef.ty_bias = coef.ty_coef * normOrigin.y;
    coef.tz_bias = coef.tz_coef * normOrigin.z;

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
    // CPU-only path (not a shader mirror) — free to pick its own tolerance. Do NOT unify this with
    // castRayGpuMirror's boundary_epsilon below: that one must match ESVOTraversal.glsl exactly.
    constexpr float boundary_epsilon = 0.01f;
    bool usePositionBasedSelection = (state.t_min < boundary_epsilon);

    float mirroredOriginX = (coef.octant_mask & 1) ? coef.normOrigin.x : (3.0f - coef.normOrigin.x);
    float mirroredOriginY = (coef.octant_mask & 2) ? coef.normOrigin.y : (3.0f - coef.normOrigin.y);
    float mirroredOriginZ = (coef.octant_mask & 4) ? coef.normOrigin.z : (3.0f - coef.normOrigin.z);

    DEBUG_PRINT("\n=== selectInitialOctant ===\n");
    DEBUG_PRINT("  usePositionBased=%d, t_min=%.6f, octant_mask=%d\n",
                usePositionBasedSelection ? 1 : 0, state.t_min, coef.octant_mask);
    DEBUG_PRINT("  mirroredOrigin=(%.6f, %.6f, %.6f)\n", mirroredOriginX, mirroredOriginY, mirroredOriginZ);

    if (std::abs(coef.rayDir.x) < axis_epsilon || usePositionBasedSelection) {
        if (mirroredOriginX >= 1.5f) { state.idx |= 1; state.pos.x = 1.5f; }
    } else {
        if (1.5f * coef.tx_coef - coef.tx_bias > state.t_min) { state.idx ^= 1; state.pos.x = 1.5f; }
    }

    if (std::abs(coef.rayDir.y) < axis_epsilon || usePositionBasedSelection) {
        if (mirroredOriginY >= 1.5f) { state.idx |= 2; state.pos.y = 1.5f; }
    } else {
        if (1.5f * coef.ty_coef - coef.ty_bias > state.t_min) { state.idx ^= 2; state.pos.y = 1.5f; }
    }

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
// GPU-PARITY TRAVERSAL — 1:1 port of BodyInstanceRayMarch.comp + ESVOTraversal.glsl
// ============================================================================
// This reproduces the GPU body shader EXACTLY, reading the LIVE octree
// (root->childDescriptors as the shader's esvoNodes[], and root->brickViews for
// brick occupancy in place of brickData[]). The shell octree is built at
// worldMin=(0,0,0), worldMax=(n,n,n), so the shader's [0,1]-grid local frame maps
// to our [0,n] world by worldToLocal = scale(1/n) (and the ESVO octant geometry
// recovers integer cells directly). The standalone GpuTraversalMirror (over the
// serialized GPU buffers) independently cross-checks this port (test_gpu_parity).

namespace {

// --- GLSL-mirrored free helpers (operation order matches the shader) -----------

struct GpuRayCoef {
    float tx_coef, ty_coef, tz_coef;
    float tx_bias, ty_bias, tz_bias;
    int   octant_mask;
    glm::vec3 rayDir;
    glm::vec3 normOrigin;
};
struct GpuState {
    uint32_t parentPtr; int idx; int scale; float scale_exp2;
    glm::vec3 pos; float t_min, t_max; float h;
};
struct GpuStackEntry { uint32_t parentPtr; float t_max; };

constexpr int   kGpuStack = 23;
constexpr int   kGpuMaxIters = 512;
constexpr float kGpuEps = 1e-6f;
constexpr float kGpuDirEps = 1e-5f;

inline uint32_t gpuFloatBitsToUint(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
inline float    gpuUintBitsToFloat(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
inline int gpuMirroredToLocal(int mirroredIdx, int octant_mask) { return mirroredIdx ^ ((~octant_mask) & 7); }

inline glm::vec2 gpuRayAABB(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& bmin, const glm::vec3& bmax) {
    const glm::vec3 inv = 1.0f / rd;
    const glm::vec3 t0 = (bmin - ro) * inv;
    const glm::vec3 t1 = (bmax - ro) * inv;
    const glm::vec3 tmn = glm::min(t0, t1), tmx = glm::max(t0, t1);
    return glm::vec2(glm::max(glm::max(tmn.x, tmn.y), tmn.z), glm::min(glm::min(tmx.x, tmx.y), tmx.z));
}

inline void gpuCorners(const glm::vec3& pos, const GpuRayCoef& c, float& tx, float& ty, float& tz) {
    tx = pos.x * c.tx_coef - c.tx_bias;
    ty = pos.y * c.ty_coef - c.ty_bias;
    tz = pos.z * c.tz_coef - c.tz_bias;
}
inline float gpuCorrectedTcMax(float tx, float ty, float tz, const glm::vec3& d, float t_max) {
    const float thr = 1000.0f;
    const bool ux = std::abs(d.x) >= kGpuDirEps, uy = std::abs(d.y) >= kGpuDirEps, uz = std::abs(d.z) >= kGpuDirEps;
    const float vx = (ux && std::abs(tx) < thr) ? tx : t_max;
    const float vy = (uy && std::abs(ty) < thr) ? ty : t_max;
    const float vz = (uz && std::abs(tz) < thr) ? tz : t_max;
    return glm::min(glm::min(vx, vy), vz);
}

inline glm::vec3 gpuComputePosInBrick(const glm::vec3& hitPos12, const glm::vec3& statePos,
                                      float scaleExp2, int octantMask, int brickSize) {
    glm::vec3 hpm = hitPos12;
    if ((octantMask & 1) == 0) hpm.x = 3.0f - hpm.x;
    if ((octantMask & 2) == 0) hpm.y = 3.0f - hpm.y;
    if ((octantMask & 4) == 0) hpm.z = 3.0f - hpm.z;
    glm::vec3 p = ((hpm - statePos) / scaleExp2) * static_cast<float>(brickSize);
    if ((octantMask & 1) == 0) p.x = static_cast<float>(brickSize) - p.x;
    if ((octantMask & 2) == 0) p.y = static_cast<float>(brickSize) - p.y;
    if ((octantMask & 4) == 0) p.z = static_cast<float>(brickSize) - p.z;
    return glm::clamp(p, glm::vec3(0.0f), glm::vec3(static_cast<float>(brickSize)));
}

}  // namespace

ISVOStructure::RayHit LaineKarrasOctree::castRayGpuMirror(
    const glm::vec3& origin, const glm::vec3& direction, float tMin, float tMax) const
{
    ISVOStructure::RayHit miss{};
    miss.hit = false;

    glm::vec3 rayDir;
    if (!validateRayInput(origin, direction, rayDir)) return miss;

    // --- octree-config equivalents (the [0,n] world frame) -----------------------
    const int   esvoMaxScale = ESVO_MAX_SCALE;
    const int   brickSize     = m_octree->brickSideLength;
    const int   bricksPerAxis = m_octree->bricksPerAxis;
    const int   minESVOScale  = ESVO_MAX_SCALE - m_maxLevels + 1;
    const glm::vec3 worldSize = m_worldMax - m_worldMin;
    // worldToLocal: world [worldMin,worldMax] -> [0,1].  localToWorld its inverse.
    // Build localToWorld first (translate*scale) then invert — the SAME construction the
    // GPU config uses (ShellOctreeGpu::Serialize: localToWorld = translate * scale;
    // worldToLocal = inverse(localToWorld)). Matching the operation order keeps the
    // normalization matrix bit-identical to the shader's, so the ESVO t-values agree to
    // the ULP and the brick DDA picks the same voxel at exact cell boundaries.
    const glm::mat4 localToWorld = glm::translate(glm::mat4(1.0f), m_worldMin) *
                                   glm::scale(glm::mat4(1.0f), worldSize);
    const glm::mat4 worldToLocal = glm::inverse(localToWorld);
    const std::vector<ChildDescriptor>& nodes = m_octree->root->childDescriptors;
    const auto fetchNode = [&](uint32_t idx) -> ChildDescriptor {
        return (idx < nodes.size()) ? nodes[idx] : ChildDescriptor{};
    };

    // --- traverseOctreeInstanced: world->local grid AABB [0,1] --------------------
    const glm::vec3 rayOriginLocal = glm::vec3(worldToLocal * glm::vec4(origin, 1.0f));
    const glm::vec3 rayDirLocal    = glm::mat3(worldToLocal) * rayDir;
    const glm::vec2 gridT = gpuRayAABB(rayOriginLocal, rayDirLocal, glm::vec3(0.0f), glm::vec3(1.0f));
    if (gridT.y < 0.0f) return miss;

    const bool rayStartsInside = (gridT.x < 0.0f);
    glm::vec3 rayStartWorld; float tEntryWorld = 0.0f;
    if (rayStartsInside) { rayStartWorld = origin; tEntryWorld = 0.0f; }
    else {
        const glm::vec3 entryLocal = rayOriginLocal + rayDirLocal * (gridT.x + kGpuEps);
        rayStartWorld = glm::vec3(localToWorld * glm::vec4(entryLocal, 1.0f));
        tEntryWorld = glm::length(rayStartWorld - origin);
    }

    // honor the caller's tMin/tMax window in world distance (renderer passes [0,inf])
    if (tEntryWorld < tMin) {
        // advance the world entry to tMin if the AABB entry is nearer
        const float adv = tMin - tEntryWorld;
        rayStartWorld += rayDir * adv;
        tEntryWorld = tMin;
    }

    // --- initRayCoefficients ------------------------------------------------------
    GpuRayCoef coef;
    coef.rayDir = rayDir;
    {
        const glm::vec4 lp = worldToLocal * glm::vec4(rayStartWorld, 1.0f);
        coef.normOrigin = glm::vec3(lp) / lp.w + 1.0f;
        glm::vec3 d = glm::mat3(worldToLocal) * rayDir;
        const float epsv = std::exp2(-static_cast<float>(esvoMaxScale));
        const float sx = d.x >= 0.0f ? 1.0f : -1.0f, sy = d.y >= 0.0f ? 1.0f : -1.0f, sz = d.z >= 0.0f ? 1.0f : -1.0f;
        if (std::abs(d.x) < epsv) d.x = sx * epsv;
        if (std::abs(d.y) < epsv) d.y = sy * epsv;
        if (std::abs(d.z) < epsv) d.z = sz * epsv;
        coef.tx_coef = 1.0f / -std::abs(d.x);
        coef.ty_coef = 1.0f / -std::abs(d.y);
        coef.tz_coef = 1.0f / -std::abs(d.z);
        coef.tx_bias = coef.tx_coef * coef.normOrigin.x;
        coef.ty_bias = coef.ty_coef * coef.normOrigin.y;
        coef.tz_bias = coef.tz_coef * coef.normOrigin.z;
        coef.octant_mask = 7;
        if (d.x > 0.0f) { coef.octant_mask ^= 1; coef.tx_bias = 3.0f * coef.tx_coef - coef.tx_bias; }
        if (d.y > 0.0f) { coef.octant_mask ^= 2; coef.ty_bias = 3.0f * coef.ty_coef - coef.ty_bias; }
        if (d.z > 0.0f) { coef.octant_mask ^= 4; coef.tz_bias = 3.0f * coef.tz_coef - coef.tz_bias; }
    }

    // --- initTraversalState -------------------------------------------------------
    GpuStackEntry stack[kGpuStack];
    GpuState state;
    if (rayStartsInside) {
        state.t_min = 0.0f;
        state.t_max = glm::min(glm::min(coef.tx_coef - coef.tx_bias, coef.ty_coef - coef.ty_bias), coef.tz_coef - coef.tz_bias);
    } else {
        state.t_min = glm::max(glm::max(2.0f*coef.tx_coef - coef.tx_bias, 2.0f*coef.ty_coef - coef.ty_bias), 2.0f*coef.tz_coef - coef.tz_bias);
        state.t_max = glm::min(glm::min(coef.tx_coef - coef.tx_bias, coef.ty_coef - coef.ty_bias), coef.tz_coef - coef.tz_bias);
    }
    state.h = state.t_max;
    state.t_min = glm::max(state.t_min, 0.0f);
    state.parentPtr = 0u; state.scale = esvoMaxScale; state.scale_exp2 = 0.5f; state.pos = glm::vec3(1.0f);
    for (int s = 0; s < kGpuStack; ++s) { stack[s].parentPtr = 0u; stack[s].t_max = state.t_max; }
    state.idx = 0;
    {
        // MUST match ESVOTraversal.glsl:128 and its test oracle GpuTraversalMirror.h:306 exactly —
        // this function's whole job is bit-fidelity to the real compute shader, not agreement with
        // selectInitialOctant's CPU path above (which has its own, different obligation and keeps
        // its own 0.01f). The existing parity suites don't exercise a ray with t_min in (1e-4, 0.01),
        // so they can't catch this value diverging from the shader; the shader source is the source
        // of truth here, not the test suite.
        const float boundary_epsilon = 1e-4f;
        const bool usePos = (state.t_min < boundary_epsilon);
        glm::vec3 mo;
        mo.x = ((coef.octant_mask & 1) != 0) ? coef.normOrigin.x : (3.0f - coef.normOrigin.x);
        mo.y = ((coef.octant_mask & 2) != 0) ? coef.normOrigin.y : (3.0f - coef.normOrigin.y);
        mo.z = ((coef.octant_mask & 4) != 0) ? coef.normOrigin.z : (3.0f - coef.normOrigin.z);
        if (std::abs(coef.rayDir.x) < kGpuDirEps || usePos) { if (mo.x >= 1.5f) { state.idx |= 1; state.pos.x = 1.5f; } }
        else { if (1.5f*coef.tx_coef - coef.tx_bias > state.t_min) { state.idx ^= 1; state.pos.x = 1.5f; } }
        if (std::abs(coef.rayDir.y) < kGpuDirEps || usePos) { if (mo.y >= 1.5f) { state.idx |= 2; state.pos.y = 1.5f; } }
        else { if (1.5f*coef.ty_coef - coef.ty_bias > state.t_min) { state.idx ^= 2; state.pos.y = 1.5f; } }
        if (std::abs(coef.rayDir.z) < kGpuDirEps || usePos) { if (mo.z >= 1.5f) { state.idx |= 4; state.pos.z = 1.5f; } }
        else { if (1.5f*coef.tz_coef - coef.tz_bias > state.t_min) { state.idx ^= 4; state.pos.z = 1.5f; } }
    }

    if (state.t_min >= state.t_max) return miss;
    (void)bricksPerAxis; (void)tMax;

    // --- main loop ----------------------------------------------------------------
    int iter = 0;
    for (; iter < kGpuMaxIters && state.scale <= esvoMaxScale; ++iter) {
        const ChildDescriptor parent = fetchNode(state.parentPtr);
        const uint32_t validMask   = parent.validMask;
        const uint32_t leafMask    = parent.leafMask;
        const uint32_t childPointer = parent.childPointer;

        const int localChildIdx = gpuMirroredToLocal(state.idx, coef.octant_mask);
        const bool child_valid = ((validMask >> localChildIdx) & 1u) != 0u;
        const bool isLeaf      = ((leafMask  >> localChildIdx) & 1u) != 0u;

        bool processed = false;
        float tv_max = 0.0f, tx_center = 0.0f, ty_center = 0.0f, tz_center = 0.0f;
        if (child_valid && state.t_min <= state.t_max + kGpuEps) {
            float tx, ty, tz; gpuCorners(state.pos, coef, tx, ty, tz);
            const float tc = gpuCorrectedTcMax(tx, ty, tz, coef.rayDir, state.t_max);
            tv_max = glm::min(state.t_max, tc);
            const float half = state.scale_exp2 * 0.5f;
            tx_center = half * coef.tx_coef + tx;
            ty_center = half * coef.ty_coef + ty;
            tz_center = half * coef.tz_coef + tz;
            processed = (state.t_min <= tv_max + kGpuEps);
        }

        if (processed) {
            if (isLeaf) {
                // --- handleLeafHitInstanced -------------------------------------
                if (localChildIdx >= 0 && localChildIdx <= 7) {
                    const uint32_t totalInternal = static_cast<uint32_t>(std::popcount(static_cast<uint8_t>(validMask & ~leafMask)));
                    uint32_t leafBefore = 0u;
                    if (localChildIdx > 0) {
                        const uint32_t m = (1u << localChildIdx) - 1u;
                        leafBefore = static_cast<uint32_t>(std::popcount(static_cast<uint8_t>((validMask & leafMask) & m)));
                    }
                    const uint32_t leafDescIdx = childPointer + totalInternal + leafBefore;
                    const ChildDescriptor leafDesc = fetchNode(leafDescIdx);
                    const uint32_t localBrickIdx = leafDesc.getBrickIndex();  // == contourPointer
                    if (localBrickIdx != ChildDescriptor::INVALID_BRICK_INDEX &&
                        localBrickIdx < m_octree->root->brickViews.size()) {
                        const EntityBrickView& brick = m_octree->root->brickViews[localBrickIdx];

                        const float tHit = state.t_min;
                        const glm::vec3 rayDirLoc = glm::mat3(worldToLocal) * rayDir;
                        const glm::vec3 hitPos12 = coef.normOrigin + rayDirLoc * tHit;
                        glm::vec3 posInBrick = gpuComputePosInBrick(hitPos12, state.pos, state.scale_exp2, coef.octant_mask, brickSize);
                        posInBrick = glm::clamp(posInBrick, glm::vec3(0.0f), glm::vec3(static_cast<float>(brickSize) - 0.001f));

                        // --- marchBrickInstanced (reads EntityBrickView occupancy) ---
                        glm::ivec3 cur = glm::clamp(glm::ivec3(glm::floor(posInBrick)), glm::ivec3(0), glm::ivec3(brickSize - 1));
                        glm::ivec3 step = glm::ivec3(glm::sign(rayDir));
                        if (step.x == 0) step.x = 1; if (step.y == 0) step.y = 1; if (step.z == 0) step.z = 1;
                        const float bs = static_cast<float>(brickSize);
                        const bool exitFace =
                            (posInBrick.x <= 0.001f && rayDir.x < 0.0f) || (posInBrick.x >= bs - 0.001f && rayDir.x > 0.0f) ||
                            (posInBrick.y <= 0.001f && rayDir.y < 0.0f) || (posInBrick.y >= bs - 0.001f && rayDir.y > 0.0f) ||
                            (posInBrick.z <= 0.001f && rayDir.z < 0.0f) || (posInBrick.z >= bs - 0.001f && rayDir.z > 0.0f);
                        bool brickHit = false; glm::ivec3 hitVoxel(0); glm::vec3 hitNormal(0.0f); uint32_t axisMask = 0u;
                        if (!exitFace) {
                            glm::vec3 dd;
                            dd.x = std::abs(rayDir.x) > kGpuDirEps ? 1.0f/std::abs(rayDir.x) : 1e20f;
                            dd.y = std::abs(rayDir.y) > kGpuDirEps ? 1.0f/std::abs(rayDir.y) : 1e20f;
                            dd.z = std::abs(rayDir.z) > kGpuDirEps ? 1.0f/std::abs(rayDir.z) : 1e20f;
                            glm::vec3 tmx;
                            const float MIN_DIST = 0.0001f;
                            for (int a = 0; a < 3; ++a) {
                                if (std::abs(rayDir[a]) < kGpuDirEps) tmx[a] = 1e20f;
                                else {
                                    float dn = (rayDir[a] > 0.0f) ? float(cur[a]+1) - posInBrick[a] : posInBrick[a] - float(cur[a]);
                                    dn = glm::max(dn, MIN_DIST);
                                    tmx[a] = dn / std::abs(rayDir[a]);
                                }
                            }
                            const int MAX_STEPS = 300;
                            for (int i = 0; i < MAX_STEPS; ++i) {
                                if (cur.x < 0 || cur.y < 0 || cur.z < 0 || cur.x >= brickSize || cur.y >= brickSize || cur.z >= brickSize) break;
                                // Occupancy = Density>0, the SAME predicate the legacy
                                // traverseBrickView path uses (a body shell carries both Material and
                                // Density, so this also matches the GPU brickData Material!=0 → the
                                // shader-occupancy parity holds for bodies).
                                bool occ = false;
                                const gaia::ecs::Entity e = brick.getEntity(cur.x, cur.y, cur.z);
                                if (m_voxelWorld != nullptr && m_voxelWorld->exists(e)) {
                                    const auto density = m_voxelWorld->getComponentValue<Density>(e);
                                    occ = density.has_value() && *density > 0.0f;
                                }
                                if (occ) {
                                    hitNormal = glm::vec3(0.0f);
                                    if (axisMask == 1u)      hitNormal.x = -float(step.x);
                                    else if (axisMask == 2u) hitNormal.y = -float(step.y);
                                    else                     hitNormal.z = -float(step.z);
                                    if (i == 0) {
                                        const glm::vec3 ad = glm::abs(rayDir);
                                        if (ad.x > ad.y && ad.x > ad.z) hitNormal = glm::vec3(-glm::sign(rayDir.x), 0.0f, 0.0f);
                                        else if (ad.y > ad.z)           hitNormal = glm::vec3(0.0f, -glm::sign(rayDir.y), 0.0f);
                                        else                            hitNormal = glm::vec3(0.0f, 0.0f, -glm::sign(rayDir.z));
                                    }
                                    hitVoxel = cur; brickHit = true; break;
                                }
                                if (tmx.x < tmx.y && tmx.x < tmx.z) { cur.x += step.x; tmx.x += dd.x; axisMask = 1u; }
                                else if (tmx.y < tmx.z)             { cur.y += step.y; tmx.y += dd.y; axisMask = 2u; }
                                else                                { cur.z += step.z; tmx.z += dd.z; axisMask = 4u; }
                            }
                        }

                        if (brickHit) {
                            ISVOStructure::RayHit hit{};
                            hit.hit = true;
                            // Absolute world voxel CELL = brick grid origin + local voxel. We report the
                            // cell-CENTRE as the hitPoint (the integer-grid surface the body/gameplay
                            // layer reads): a body voxel is a unit cell at integer coords, and the GPU's
                            // own hit identity is the voxel (hitBrickLocalPos = currentVoxel+0.5). A
                            // centre is snap-stable (floor(centre±ε) == cell) where a face-precise point
                            // would round across a boundary.
                            const glm::ivec3 cell = brick.getLocalGridOrigin() + hitVoxel;
                            const float voxelSize = worldSize.x / static_cast<float>(bricksPerAxis * brickSize);
                            hit.entity = brick.getEntity(hitVoxel.x, hitVoxel.y, hitVoxel.z);
                            hit.normal = hitNormal;
                            hit.hitPoint = m_worldMin + (glm::vec3(cell) + glm::vec3(0.5f)) * voxelSize;
                            hit.tMin = tEntryWorld + tHit;
                            hit.tMax = hit.tMin + voxelSize;
                            hit.scale = m_maxLevels - 1;
                            return hit;
                        }
                    }
                }
                state.t_min = tv_max;
                // (no return) fall through to ADVANCE
            } else {
                // --- executePushPhase -------------------------------------------
                float tx, ty, tz; gpuCorners(state.pos, coef, tx, ty, tz);
                const float tc = glm::min(glm::min(tx, ty), tz);
                if (state.scale >= 0 && state.scale < kGpuStack) {
                    stack[state.scale].parentPtr = state.parentPtr;
                    stack[state.scale].t_max = state.t_max;
                }
                state.h = tc;
                const int worldIdx = gpuMirroredToLocal(state.idx, coef.octant_mask);
                const uint32_t nonLeaf = validMask & ~leafMask;
                const uint32_t mbc = (1u << worldIdx) - 1u;
                const uint32_t childLocal = static_cast<uint32_t>(std::popcount(nonLeaf & mbc));
                state.parentPtr = childPointer + childLocal;
                state.idx = 0; state.scale--;
                state.scale_exp2 = state.scale_exp2 * 0.5f;
                if (tx_center > state.t_min) { state.idx ^= 1; state.pos.x += state.scale_exp2; }
                if (ty_center > state.t_min) { state.idx ^= 2; state.pos.y += state.scale_exp2; }
                if (tz_center > state.t_min) { state.idx ^= 4; state.pos.z += state.scale_exp2; }
                state.t_max = tv_max;
                continue;
            }
        }

        // --- executeAdvancePhase ------------------------------------------------
        int step_mask = 0;
        int advanceResult = 0;
        {
            float tx, ty, tz; gpuCorners(state.pos, coef, tx, ty, tz);
            const bool cx = std::abs(coef.rayDir.x) >= kGpuDirEps, cy = std::abs(coef.rayDir.y) >= kGpuDirEps, cz = std::abs(coef.rayDir.z) >= kGpuDirEps;
            float tc = gpuCorrectedTcMax(tx, ty, tz, coef.rayDir, state.t_max);
            if (tc >= 1e10f) {
                const float fx = cx ? tx : -1e10f, fy = cy ? ty : -1e10f, fz = cz ? tz : -1e10f;
                tc = glm::max(glm::max(fx, fy), fz);
            }
            if (cx && tx <= tc) { step_mask ^= 1; state.pos.x -= state.scale_exp2; }
            if (cy && ty <= tc) { step_mask ^= 2; state.pos.y -= state.scale_exp2; }
            if (cz && tz <= tc) { step_mask ^= 4; state.pos.z -= state.scale_exp2; }
            state.t_min = glm::max(tc, 0.0f);
            state.idx ^= step_mask;
            advanceResult = ((state.idx & step_mask) != 0) ? 1 : 0;
        }

        if (advanceResult == 0) {
            if (state.scale < esvoMaxScale) state.t_max = stack[state.scale + 1].t_max;
        }

        if (advanceResult == 1) {
            // --- executePopPhase ------------------------------------------------
            int popResult = 0;
            if (state.scale >= esvoMaxScale) {
                if (state.t_min > state.t_max ||
                    state.pos.x < 1.0f || state.pos.x >= 2.0f ||
                    state.pos.y < 1.0f || state.pos.y >= 2.0f ||
                    state.pos.z < 1.0f || state.pos.z >= 2.0f) popResult = 1;
            } else {
                uint32_t diff = 0u;
                if ((step_mask & 1) != 0) diff |= gpuFloatBitsToUint(state.pos.x) ^ gpuFloatBitsToUint(state.pos.x + state.scale_exp2);
                if ((step_mask & 2) != 0) diff |= gpuFloatBitsToUint(state.pos.y) ^ gpuFloatBitsToUint(state.pos.y + state.scale_exp2);
                if ((step_mask & 4) != 0) diff |= gpuFloatBitsToUint(state.pos.z) ^ gpuFloatBitsToUint(state.pos.z + state.scale_exp2);
                if (diff == 0u) popResult = 1;
                else {
                    state.scale = static_cast<int>((gpuFloatBitsToUint(static_cast<float>(diff)) >> 23u) - 127u);
                    state.scale_exp2 = gpuUintBitsToFloat(static_cast<uint32_t>(state.scale - esvoMaxScale - 1 + 127) << 23u);
                    if (state.scale < minESVOScale || state.scale > esvoMaxScale) popResult = 1;
                    else {
                        state.parentPtr = stack[state.scale].parentPtr;
                        state.t_max     = stack[state.scale].t_max;
                        const uint32_t shx = gpuFloatBitsToUint(state.pos.x) >> static_cast<uint32_t>(state.scale);
                        const uint32_t shy = gpuFloatBitsToUint(state.pos.y) >> static_cast<uint32_t>(state.scale);
                        const uint32_t shz = gpuFloatBitsToUint(state.pos.z) >> static_cast<uint32_t>(state.scale);
                        state.pos.x = gpuUintBitsToFloat(shx << static_cast<uint32_t>(state.scale));
                        state.pos.y = gpuUintBitsToFloat(shy << static_cast<uint32_t>(state.scale));
                        state.pos.z = gpuUintBitsToFloat(shz << static_cast<uint32_t>(state.scale));
                        state.idx = static_cast<int>(shx & 1u) | (static_cast<int>(shy & 1u) << 1) | (static_cast<int>(shz & 1u) << 2);
                        state.h = 0.0f;
                    }
                }
            }
            if (popResult == 1) return miss;
        }
    }
    return miss;
}

} // namespace Vixen::SVO
