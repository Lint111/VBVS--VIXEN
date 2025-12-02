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
#include <limits>
#include <cmath>
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

    int minESVOScale = ESVO_MAX_SCALE - m_maxLevels + 1;
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

ISVOStructure::RayHit LaineKarrasOctree::castRayImpl(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float tMin,
    float tMax,
    float lodBias) const
{
    ISVOStructure::RayHit miss{};
    miss.hit = false;

    glm::vec3 rayDir;
    if (!validateRayInput(origin, direction, rayDir)) {
        return miss;
    }

    bool rayStartsInside = isPointInsideAABB(origin, m_worldMin, m_worldMax);

    float tEntry, tExit;
    if (!intersectAABB(origin, rayDir, m_worldMin, m_worldMax, tEntry, tExit)) {
        return miss;
    }

    tEntry = std::max(tEntry, tMin);
    tExit = std::min(tExit, tMax);
    if (tEntry >= tExit || tExit < 0.0f) {
        return miss;
    }

    float tRayStart = rayStartsInside ? 0.0f : std::max(0.0f, tEntry);
    glm::vec3 rayEntryPoint = origin + rayDir * tRayStart;
    glm::vec3 worldSize = m_worldMax - m_worldMin;
    glm::vec3 normOrigin = (rayEntryPoint - m_worldMin) / worldSize + glm::vec3(1.0f);

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
    int minESVOScale = ESVO_MAX_SCALE - m_maxLevels + 1;

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
    if (std::abs(rayDirSafe.x) < epsilon) rayDirSafe.x = std::copysignf(epsilon, rayDirSafe.x);
    if (std::abs(rayDirSafe.y) < epsilon) rayDirSafe.y = std::copysignf(epsilon, rayDirSafe.y);
    if (std::abs(rayDirSafe.z) < epsilon) rayDirSafe.z = std::copysignf(epsilon, rayDirSafe.z);

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

} // namespace Vixen::SVO
