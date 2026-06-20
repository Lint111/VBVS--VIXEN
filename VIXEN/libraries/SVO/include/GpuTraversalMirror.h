#pragma once

// GpuTraversalMirror.h — a faithful 1:1 C++ MIRROR of the GPU body-octree
// traversal in shaders/BodyInstanceRayMarch.comp (+ its includes ESVOTraversal.glsl,
// ESVOCoefficients.glsl, CoordinateTransforms.glsl, RayGeneration.glsl, SVOTypes.glsl).
//
// PURPOSE (gpu-shader-debug skill): the GPU renderer draws COMPLETE bodies, so its
// algorithm is CORRECT BY CONSTRUCTION. This header reproduces that algorithm on the
// CPU, operating on the EXACT byte buffers the shader consumes — a SerializedOctree
// (ShellOctreeGpu.h): `nodes` reinterpreted as the shader's `esvoNodes[]` (uvec2 /
// ChildDescriptor), `bricks` as `brickData[]` (uint32 material per voxel, z*64+y*8+x),
// and `OctreeConfig` for the scales/matrices. It is the ORACLE that drives the CPU
// LaineKarrasOctree::castRay to parity.
//
// SYNC CONTRACT: every function here is a line-by-line port of the cited GLSL. GLSL
// float math is matched by matching the GLSL operation order. If the shader changes,
// re-port the changed function. This is a TEST/REFERENCE translation unit — it is NOT
// on the engine hot path.
//
// @shader shaders/BodyInstanceRayMarch.comp (+ ESVOTraversal.glsl / ESVOCoefficients.glsl
//         / CoordinateTransforms.glsl)

#include "ShellOctreeGpu.h"   // SerializedOctree, OctreeConfig, ChildDescriptor

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <cstring>
#include <cmath>

namespace Vixen::SVO {

// ===========================================================================
// Mirror of the GPU traversal. One instance wraps one SerializedOctree.
// ===========================================================================
class GpuTraversalMirror {
public:
    // Result mirrors what main()/traverseOctreeInstanced produce for one ray.
    struct Hit {
        bool      hit = false;
        float     t = 0.0f;                 // world-space hit distance (tBias + tHit)
        glm::vec3 hitPoint{0.0f};           // rayOrigin + rayDir * t  (world space)
        glm::ivec3 voxel{0};                // absolute integer voxel cell (world grid)
        glm::vec3 normal{0.0f};
        uint32_t  brickIndex = 0;           // absolute brick index (g_brickArrayBase + local)
        uint32_t  voxelLinearIdx = 0;       // z*64+y*8+x within brick
        int       iterations = 0;
        int       exitCode = 0;             // 0=none/no-hit, 1=hit, 2=invalid-span, 3=stack
    };

    explicit GpuTraversalMirror(const SerializedOctree& serialized)
        : m_cfg(serialized.config) {
        // Reinterpret the byte buffers as the shader's SSBO element types.
        m_nodeCount = serialized.nodeCount;
        m_nodes = reinterpret_cast<const ChildDescriptor*>(serialized.nodes.data());
        m_brickCount = serialized.brickCount;
        m_brickData = reinterpret_cast<const uint32_t*>(serialized.bricks.data());
        // single-octree oracle: bases are 0 (matches Serialize()).
        m_nodeArrayBase = m_cfg.nodeArrayBase;
        m_brickArrayBase = m_cfg.brickArrayBase;
    }

    // Port of traverseOctreeInstanced(): cast a WORLD-space ray, return the hit.
    Hit castRay(const glm::vec3& rayOrigin, const glm::vec3& rayDirIn) const {
        Hit out;
        // The shader receives a normalized rayDir from getRayDir(); callers here pass
        // an already-normalized direction (the renderer + parity tests both do).
        const glm::vec3 rayDir = glm::normalize(rayDirIn);

        // --- world -> local, grid AABB [0,1]^3 (traverseOctreeInstanced L396-417) ---
        const glm::vec3 rayOriginLocal = glm::vec3(m_cfg.worldToLocal * glm::vec4(rayOrigin, 1.0f));
        const glm::vec3 rayDirLocal    = glm::mat3(m_cfg.worldToLocal) * rayDir;

        const glm::vec2 gridT = rayAABBIntersection(rayOriginLocal, rayDirLocal, glm::vec3(0.0f), glm::vec3(1.0f));
        if (gridT.y < 0.0f) { out.exitCode = 2; return out; }

        const bool rayStartsInside = (gridT.x < 0.0f);
        glm::vec3 rayStartWorld;
        float tEntryWorld = 0.0f;
        if (rayStartsInside) {
            rayStartWorld = rayOrigin;
            tEntryWorld   = 0.0f;
        } else {
            const glm::vec3 entryPointLocal = rayOriginLocal + rayDirLocal * (gridT.x + kEpsilon);
            rayStartWorld = glm::vec3(m_cfg.localToWorld * glm::vec4(entryPointLocal, 1.0f));
            tEntryWorld   = glm::length(rayStartWorld - rayOrigin);
        }

        const RayCoefficients coef = initRayCoefficients(rayDir, rayStartWorld);

        StackEntry stack[kStackSize];
        TraversalState state = initTraversalState(coef, stack, rayStartsInside);

        if (state.t_min >= state.t_max) { out.exitCode = 2; return out; }

        int iter = 0;
        for (; iter < kMaxIters && state.scale <= m_cfg.esvoMaxScale; ++iter) {
            const ChildDescriptor parent = fetchNode(state.parentPtr);
            const uint32_t validMask   = getValidMask(parent);
            const uint32_t leafMask    = getLeafMask(parent);
            const uint32_t childPointer = getChildPointer(parent);

            bool isLeaf = false;
            float tv_max = 0.0f, tx_center = 0.0f, ty_center = 0.0f, tz_center = 0.0f;

            if (checkChildValidity(state, coef, validMask, leafMask, isLeaf, tv_max,
                                   tx_center, ty_center, tz_center)) {
                if (isLeaf) {
                    if (handleLeafHit(state, coef, rayDir, tEntryWorld, rayOrigin,
                                      childPointer, validMask, leafMask, out)) {
                        out.hit = true;
                        out.exitCode = 1;
                        out.iterations = iter + 1;
                        return out;
                    }
                    state.t_min = tv_max;
                    // (LOD disabled in parity: raySizeCoef==0; we never take the LOD branch.)
                } else {
                    executePushPhase(state, coef, stack, validMask, leafMask, childPointer,
                                     tv_max, tx_center, ty_center, tz_center);
                    continue;
                }
            }

            int step_mask = 0;
            const int advanceResult = executeAdvancePhase(state, coef, step_mask);

            if (advanceResult == 0) {
                if (state.scale < m_cfg.esvoMaxScale) {
                    state.t_max = stack[state.scale + 1].t_max;
                }
            }
            if (advanceResult == 1) {
                const int popResult = executePopPhase(state, coef, stack, step_mask);
                if (popResult == 1) { out.exitCode = 3; out.iterations = iter + 1; return out; }
            }
        }
        out.iterations = iter;
        return out;
    }

private:
    // ====================================================================
    // CONSTANTS (ESVOTraversal.glsl L24-27)
    // ====================================================================
    static constexpr int   kStackSize = 23;
    static constexpr int   kMaxIters  = 512;
    static constexpr float kEpsilon   = 1e-6f;
    static constexpr float kDirEps    = 1e-5f;

    struct StackEntry { uint32_t parentPtr = 0; float t_max = 0.0f; };
    struct TraversalState {
        uint32_t parentPtr = 0;
        int   idx = 0;
        int   scale = 0;
        float scale_exp2 = 0.5f;
        glm::vec3 pos{1.0f};
        float t_min = 0.0f, t_max = 1.0f;
        float h = 0.0f;
    };
    struct RayCoefficients {
        float tx_coef = 0, ty_coef = 0, tz_coef = 0;
        float tx_bias = 0, ty_bias = 0, tz_bias = 0;
        int   octant_mask = 7;
        glm::vec3 rayDir{0.0f};
        glm::vec3 normOrigin{1.0f};
    };

    const OctreeConfig m_cfg;
    const ChildDescriptor* m_nodes = nullptr;
    uint32_t m_nodeCount = 0;
    const uint32_t* m_brickData = nullptr;
    uint32_t m_brickCount = 0;
    int m_nodeArrayBase = 0;
    int m_brickArrayBase = 0;

    // ====================================================================
    // SVOTypes.glsl — descriptor bit extraction (the shader reads uvec2.x)
    // ====================================================================
    static uint32_t descriptorX(const ChildDescriptor& d) {
        // ChildDescriptor is bit-packed exactly as the shader's uvec2.x; reinterpret
        // the first 32 bits (the serializer memcpys the struct verbatim).
        uint32_t words[2];
        std::memcpy(words, &d, sizeof(words));
        return words[0];
    }
    static uint32_t descriptorY(const ChildDescriptor& d) {
        uint32_t words[2];
        std::memcpy(words, &d, sizeof(words));
        return words[1];
    }
    static uint32_t getChildPointer(const ChildDescriptor& d) { return descriptorX(d) & 0x7FFFu; }
    static uint32_t getValidMask(const ChildDescriptor& d)    { return (descriptorX(d) >> 16) & 0xFFu; }
    static uint32_t getLeafMask(const ChildDescriptor& d)     { return (descriptorX(d) >> 24) & 0xFFu; }
    static uint32_t getContourPointer(const ChildDescriptor& d) { return descriptorY(d) & 0xFFFFFFu; }
    static bool childExists(uint32_t validMask, int i) { return ((validMask >> i) & 1u) != 0u; }
    static bool childIsLeaf(uint32_t leafMask, int i)  { return ((leafMask >> i) & 1u) != 0u; }
    static uint32_t countLeavesBefore(uint32_t validMask, uint32_t leafMask, int childIndex) {
        if (childIndex <= 0) return 0u;
        uint32_t mask = (1u << childIndex) - 1u;
        uint32_t leafChildren = validMask & leafMask;
        return static_cast<uint32_t>(std::popcount(leafChildren & mask));
    }
    static int mirroredToLocalOctant(int mirroredIdx, int octant_mask) {
        return mirroredIdx ^ ((~octant_mask) & 7);
    }
    static const uint32_t SVO_INVALID_INDEX = 0xFFFFFFu;

    ChildDescriptor fetchNode(uint32_t nodeIndex) const {
        // fetchESVONode: esvoNodes[nodeArrayBase + nodeIndex]
        uint32_t idx = static_cast<uint32_t>(m_nodeArrayBase) + nodeIndex;
        if (idx >= m_nodeCount) return ChildDescriptor{};  // OOB guard (shader UB; we return empty)
        return m_nodes[idx];
    }

    // ====================================================================
    // RayGeneration.glsl — rayAABBIntersection / worldToNormalized
    // ====================================================================
    static glm::vec2 rayAABBIntersection(const glm::vec3& ro, const glm::vec3& rd,
                                         const glm::vec3& bmin, const glm::vec3& bmax) {
        const glm::vec3 invDir = 1.0f / rd;
        const glm::vec3 t0 = (bmin - ro) * invDir;
        const glm::vec3 t1 = (bmax - ro) * invDir;
        const glm::vec3 tMin = glm::min(t0, t1);
        const glm::vec3 tMax = glm::max(t0, t1);
        const float tNear = glm::max(glm::max(tMin.x, tMin.y), tMin.z);
        const float tFar  = glm::min(glm::min(tMax.x, tMax.y), tMax.z);
        return glm::vec2(tNear, tFar);
    }
    glm::vec3 worldToNormalized(const glm::vec3& worldPos) const {
        const glm::vec4 localPos = m_cfg.worldToLocal * glm::vec4(worldPos, 1.0f);
        const glm::vec3 p = glm::vec3(localPos) / localPos.w;
        return p + 1.0f;  // [0,1] -> [1,2]
    }

    // ====================================================================
    // ESVOCoefficients.glsl — initRayCoefficients
    // ====================================================================
    RayCoefficients initRayCoefficients(const glm::vec3& rayDir, const glm::vec3& rayStartWorld) const {
        RayCoefficients coef;
        coef.rayDir = rayDir;
        const glm::vec3 p = worldToNormalized(rayStartWorld);
        coef.normOrigin = p;
        glm::vec3 d = glm::mat3(m_cfg.worldToLocal) * rayDir;

        const float epsilon_esvo = std::exp2(-static_cast<float>(m_cfg.esvoMaxScale));
        const float sx = d.x >= 0.0f ? 1.0f : -1.0f;
        const float sy = d.y >= 0.0f ? 1.0f : -1.0f;
        const float sz = d.z >= 0.0f ? 1.0f : -1.0f;
        if (std::abs(d.x) < epsilon_esvo) d.x = sx * epsilon_esvo;
        if (std::abs(d.y) < epsilon_esvo) d.y = sy * epsilon_esvo;
        if (std::abs(d.z) < epsilon_esvo) d.z = sz * epsilon_esvo;

        coef.tx_coef = 1.0f / -std::abs(d.x);
        coef.ty_coef = 1.0f / -std::abs(d.y);
        coef.tz_coef = 1.0f / -std::abs(d.z);
        coef.tx_bias = coef.tx_coef * p.x;
        coef.ty_bias = coef.ty_coef * p.y;
        coef.tz_bias = coef.tz_coef * p.z;

        coef.octant_mask = 7;
        if (d.x > 0.0f) { coef.octant_mask ^= 1; coef.tx_bias = 3.0f * coef.tx_coef - coef.tx_bias; }
        if (d.y > 0.0f) { coef.octant_mask ^= 2; coef.ty_bias = 3.0f * coef.ty_coef - coef.ty_bias; }
        if (d.z > 0.0f) { coef.octant_mask ^= 4; coef.tz_bias = 3.0f * coef.tz_coef - coef.tz_bias; }
        return coef;
    }

    // ====================================================================
    // ESVOTraversal.glsl — initTraversalState
    // ====================================================================
    TraversalState initTraversalState(const RayCoefficients& coef, StackEntry stack[kStackSize],
                                      bool rayStartsInside) const {
        TraversalState state;
        if (rayStartsInside) {
            state.t_min = 0.0f;
            state.t_max = glm::min(glm::min(coef.tx_coef - coef.tx_bias,
                                            coef.ty_coef - coef.ty_bias),
                                   coef.tz_coef - coef.tz_bias);
        } else {
            state.t_min = glm::max(glm::max(2.0f * coef.tx_coef - coef.tx_bias,
                                            2.0f * coef.ty_coef - coef.ty_bias),
                                   2.0f * coef.tz_coef - coef.tz_bias);
            state.t_max = glm::min(glm::min(coef.tx_coef - coef.tx_bias,
                                            coef.ty_coef - coef.ty_bias),
                                   coef.tz_coef - coef.tz_bias);
        }
        state.h = state.t_max;
        state.t_min = glm::max(state.t_min, 0.0f);

        state.parentPtr = 0u;
        state.scale = m_cfg.esvoMaxScale;
        state.scale_exp2 = 0.5f;
        state.pos = glm::vec3(1.0f);

        for (int s = 0; s < kStackSize; ++s) { stack[s].parentPtr = 0u; stack[s].t_max = state.t_max; }

        state.idx = 0;
        const float boundary_epsilon = 1e-4f;
        const bool usePositionBased = (state.t_min < boundary_epsilon);

        glm::vec3 mirroredOrigin;
        mirroredOrigin.x = ((coef.octant_mask & 1) != 0) ? coef.normOrigin.x : (3.0f - coef.normOrigin.x);
        mirroredOrigin.y = ((coef.octant_mask & 2) != 0) ? coef.normOrigin.y : (3.0f - coef.normOrigin.y);
        mirroredOrigin.z = ((coef.octant_mask & 4) != 0) ? coef.normOrigin.z : (3.0f - coef.normOrigin.z);

        if (std::abs(coef.rayDir.x) < kDirEps || usePositionBased) {
            if (mirroredOrigin.x >= 1.5f) { state.idx |= 1; state.pos.x = 1.5f; }
        } else {
            if (1.5f * coef.tx_coef - coef.tx_bias > state.t_min) { state.idx ^= 1; state.pos.x = 1.5f; }
        }
        if (std::abs(coef.rayDir.y) < kDirEps || usePositionBased) {
            if (mirroredOrigin.y >= 1.5f) { state.idx |= 2; state.pos.y = 1.5f; }
        } else {
            if (1.5f * coef.ty_coef - coef.ty_bias > state.t_min) { state.idx ^= 2; state.pos.y = 1.5f; }
        }
        if (std::abs(coef.rayDir.z) < kDirEps || usePositionBased) {
            if (mirroredOrigin.z >= 1.5f) { state.idx |= 4; state.pos.z = 1.5f; }
        } else {
            if (1.5f * coef.tz_coef - coef.tz_bias > state.t_min) { state.idx ^= 4; state.pos.z = 1.5f; }
        }
        return state;
    }

    // ====================================================================
    // ESVOTraversal.glsl — corner / tc_max / checkChildValidity
    // ====================================================================
    static void computeVoxelCorners(const glm::vec3& pos, const RayCoefficients& coef,
                                    float& tx, float& ty, float& tz) {
        tx = pos.x * coef.tx_coef - coef.tx_bias;
        ty = pos.y * coef.ty_coef - coef.ty_bias;
        tz = pos.z * coef.tz_coef - coef.tz_bias;
    }
    static float computeCorrectedTcMax(float tx, float ty, float tz, const glm::vec3& rayDir, float t_max) {
        const float corner_threshold = 1000.0f;
        const bool useX = (std::abs(rayDir.x) >= kDirEps);
        const bool useY = (std::abs(rayDir.y) >= kDirEps);
        const bool useZ = (std::abs(rayDir.z) >= kDirEps);
        const float vx = (useX && std::abs(tx) < corner_threshold) ? tx : t_max;
        const float vy = (useY && std::abs(ty) < corner_threshold) ? ty : t_max;
        const float vz = (useZ && std::abs(tz) < corner_threshold) ? tz : t_max;
        return glm::min(glm::min(vx, vy), vz);
    }
    bool checkChildValidity(const TraversalState& state, const RayCoefficients& coef,
                            uint32_t validMask, uint32_t leafMask,
                            bool& isLeaf, float& tv_max,
                            float& tx_center, float& ty_center, float& tz_center) const {
        const int localChildIdx = mirroredToLocalOctant(state.idx, coef.octant_mask);
        const bool child_valid = childExists(validMask, localChildIdx);
        isLeaf = childIsLeaf(leafMask, localChildIdx);
        if (!child_valid || state.t_min > state.t_max + kEpsilon) return false;

        float tx, ty, tz;
        computeVoxelCorners(state.pos, coef, tx, ty, tz);
        const float tc_max = computeCorrectedTcMax(tx, ty, tz, coef.rayDir, state.t_max);
        tv_max = glm::min(state.t_max, tc_max);

        const float halfScale = state.scale_exp2 * 0.5f;
        tx_center = halfScale * coef.tx_coef + tx;
        ty_center = halfScale * coef.ty_coef + ty;
        tz_center = halfScale * coef.tz_coef + tz;
        return state.t_min <= tv_max + kEpsilon;
    }

    // ====================================================================
    // ESVOTraversal.glsl — executePushPhase
    // ====================================================================
    void executePushPhase(TraversalState& state, const RayCoefficients& coef, StackEntry stack[kStackSize],
                          uint32_t validMask, uint32_t leafMask, uint32_t childPointer,
                          float tv_max, float tx_center, float ty_center, float tz_center) const {
        float tx, ty, tz;
        computeVoxelCorners(state.pos, coef, tx, ty, tz);
        const float tc_max = glm::min(glm::min(tx, ty), tz);

        if (state.scale >= 0 && state.scale < kStackSize) {
            stack[state.scale].parentPtr = state.parentPtr;
            stack[state.scale].t_max = state.t_max;
        }
        state.h = tc_max;

        const int worldIdx = mirroredToLocalOctant(state.idx, coef.octant_mask);
        const uint32_t nonLeafMask = validMask & ~leafMask;
        const uint32_t mask_before_child = (1u << worldIdx) - 1u;
        const uint32_t childLocalIndex = static_cast<uint32_t>(std::popcount(nonLeafMask & mask_before_child));
        state.parentPtr = childPointer + childLocalIndex;

        state.idx = 0;
        state.scale--;
        const float halfScale = state.scale_exp2 * 0.5f;
        state.scale_exp2 = halfScale;

        if (tx_center > state.t_min) { state.idx ^= 1; state.pos.x += state.scale_exp2; }
        if (ty_center > state.t_min) { state.idx ^= 2; state.pos.y += state.scale_exp2; }
        if (tz_center > state.t_min) { state.idx ^= 4; state.pos.z += state.scale_exp2; }

        state.t_max = tv_max;
    }

    // ====================================================================
    // ESVOTraversal.glsl — executeAdvancePhase
    // ====================================================================
    int executeAdvancePhase(TraversalState& state, const RayCoefficients& coef, int& step_mask) const {
        float tx, ty, tz;
        computeVoxelCorners(state.pos, coef, tx, ty, tz);
        const bool canStepX = (std::abs(coef.rayDir.x) >= kDirEps);
        const bool canStepY = (std::abs(coef.rayDir.y) >= kDirEps);
        const bool canStepZ = (std::abs(coef.rayDir.z) >= kDirEps);

        float tc_max = computeCorrectedTcMax(tx, ty, tz, coef.rayDir, state.t_max);
        if (tc_max >= 1e10f) {
            const float fx = canStepX ? tx : -1e10f;
            const float fy = canStepY ? ty : -1e10f;
            const float fz = canStepZ ? tz : -1e10f;
            tc_max = glm::max(glm::max(fx, fy), fz);
        }

        step_mask = 0;
        if (canStepX && tx <= tc_max) { step_mask ^= 1; state.pos.x -= state.scale_exp2; }
        if (canStepY && ty <= tc_max) { step_mask ^= 2; state.pos.y -= state.scale_exp2; }
        if (canStepZ && tz <= tc_max) { step_mask ^= 4; state.pos.z -= state.scale_exp2; }

        state.t_min = glm::max(tc_max, 0.0f);
        state.idx ^= step_mask;
        if ((state.idx & step_mask) != 0) return 1;  // POP_NEEDED
        return 0;  // CONTINUE
    }

    // ====================================================================
    // ESVOTraversal.glsl — executePopPhase (IEEE-754 bit manipulation)
    // ====================================================================
    int executePopPhase(TraversalState& state, const RayCoefficients& /*coef*/,
                        StackEntry stack[kStackSize], int step_mask) const {
        if (state.scale >= m_cfg.esvoMaxScale) {
            if (state.t_min > state.t_max ||
                state.pos.x < 1.0f || state.pos.x >= 2.0f ||
                state.pos.y < 1.0f || state.pos.y >= 2.0f ||
                state.pos.z < 1.0f || state.pos.z >= 2.0f) {
                return 1;
            }
            return 0;
        }

        uint32_t differing_bits = 0u;
        if ((step_mask & 1) != 0)
            differing_bits |= floatBitsToUint(state.pos.x) ^ floatBitsToUint(state.pos.x + state.scale_exp2);
        if ((step_mask & 2) != 0)
            differing_bits |= floatBitsToUint(state.pos.y) ^ floatBitsToUint(state.pos.y + state.scale_exp2);
        if ((step_mask & 4) != 0)
            differing_bits |= floatBitsToUint(state.pos.z) ^ floatBitsToUint(state.pos.z + state.scale_exp2);
        if (differing_bits == 0u) return 1;

        state.scale = static_cast<int>((floatBitsToUint(static_cast<float>(differing_bits)) >> 23u) - 127u);
        state.scale_exp2 = uintBitsToFloat(static_cast<uint32_t>(state.scale - m_cfg.esvoMaxScale - 1 + 127) << 23u);

        if (state.scale < m_cfg.minESVOScale || state.scale > m_cfg.esvoMaxScale) return 1;

        state.parentPtr = stack[state.scale].parentPtr;
        state.t_max     = stack[state.scale].t_max;

        const uint32_t shx = floatBitsToUint(state.pos.x) >> static_cast<uint32_t>(state.scale);
        const uint32_t shy = floatBitsToUint(state.pos.y) >> static_cast<uint32_t>(state.scale);
        const uint32_t shz = floatBitsToUint(state.pos.z) >> static_cast<uint32_t>(state.scale);
        state.pos.x = uintBitsToFloat(shx << static_cast<uint32_t>(state.scale));
        state.pos.y = uintBitsToFloat(shy << static_cast<uint32_t>(state.scale));
        state.pos.z = uintBitsToFloat(shz << static_cast<uint32_t>(state.scale));

        state.idx = static_cast<int>(shx & 1u) | (static_cast<int>(shy & 1u) << 1) | (static_cast<int>(shz & 1u) << 2);
        state.h = 0.0f;
        return 0;
    }

    static uint32_t floatBitsToUint(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
    static float    uintBitsToFloat(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

    // ====================================================================
    // CoordinateTransforms.glsl — computePosInBrick
    // ====================================================================
    static glm::vec3 computePosInBrick(const glm::vec3& hitPos12, const glm::vec3& statePos,
                                       float scaleExp2, int octantMask, int brickSize) {
        glm::vec3 hitPosMirrored = hitPos12;
        if ((octantMask & 1) == 0) hitPosMirrored.x = 3.0f - hitPosMirrored.x;
        if ((octantMask & 2) == 0) hitPosMirrored.y = 3.0f - hitPosMirrored.y;
        if ((octantMask & 4) == 0) hitPosMirrored.z = 3.0f - hitPosMirrored.z;

        const glm::vec3 offsetMirrored = hitPosMirrored - statePos;
        glm::vec3 posInBrick = (offsetMirrored / scaleExp2) * static_cast<float>(brickSize);

        if ((octantMask & 1) == 0) posInBrick.x = static_cast<float>(brickSize) - posInBrick.x;
        if ((octantMask & 2) == 0) posInBrick.y = static_cast<float>(brickSize) - posInBrick.y;
        if ((octantMask & 4) == 0) posInBrick.z = static_cast<float>(brickSize) - posInBrick.z;

        return glm::clamp(posInBrick, glm::vec3(0.0f), glm::vec3(static_cast<float>(brickSize)));
    }

    // ====================================================================
    // BodyInstanceRayMarch.comp — marchBrickInstanced
    // ====================================================================
    // Returns true on hit; outputs the integer voxel + linear idx + normal.
    bool marchBrickInstanced(const glm::vec3& rayDir, glm::vec3 posInBrick, uint32_t localBrickIndex,
                             glm::ivec3& outVoxel, glm::vec3& outNormal, uint32_t& outVoxelLinearIdx) const {
        const int BRICK_SIZE_VAL = m_cfg.brickSize;
        glm::ivec3 currentVoxel = glm::clamp(glm::ivec3(glm::floor(posInBrick)), glm::ivec3(0), glm::ivec3(7));

        glm::ivec3 step = glm::ivec3(glm::sign(rayDir));
        if (step.x == 0) step.x = 1;
        if (step.y == 0) step.y = 1;
        if (step.z == 0) step.z = 1;

        // Exit-boundary rejection (L242-249): the ray enters on a face heading OUT.
        if ((posInBrick.x <= 0.001f && rayDir.x < 0.0f) ||
            (posInBrick.x >= static_cast<float>(BRICK_SIZE_VAL) - 0.001f && rayDir.x > 0.0f) ||
            (posInBrick.y <= 0.001f && rayDir.y < 0.0f) ||
            (posInBrick.y >= static_cast<float>(BRICK_SIZE_VAL) - 0.001f && rayDir.y > 0.0f) ||
            (posInBrick.z <= 0.001f && rayDir.z < 0.0f) ||
            (posInBrick.z >= static_cast<float>(BRICK_SIZE_VAL) - 0.001f && rayDir.z > 0.0f)) {
            return false;
        }

        glm::vec3 deltaDist;
        deltaDist.x = std::abs(rayDir.x) > kDirEps ? 1.0f / std::abs(rayDir.x) : 1e20f;
        deltaDist.y = std::abs(rayDir.y) > kDirEps ? 1.0f / std::abs(rayDir.y) : 1e20f;
        deltaDist.z = std::abs(rayDir.z) > kDirEps ? 1.0f / std::abs(rayDir.z) : 1e20f;

        glm::vec3 tMax;
        const float MIN_DIST = 0.0001f;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(rayDir[axis]) < kDirEps) {
                tMax[axis] = 1e20f;
            } else {
                const float posLocal = posInBrick[axis];
                float distToNext = (rayDir[axis] > 0.0f)
                    ? static_cast<float>(currentVoxel[axis] + 1) - posLocal
                    : posLocal - static_cast<float>(currentVoxel[axis]);
                distToNext = glm::max(distToNext, MIN_DIST);
                tMax[axis] = distToNext / std::abs(rayDir[axis]);
            }
        }

        const uint32_t absBrickIndex = static_cast<uint32_t>(m_brickArrayBase) + localBrickIndex;
        uint32_t axisMask = 0u;
        const int MAX_STEPS = 300;
        for (int i = 0; i < MAX_STEPS; ++i) {
            if (currentVoxel.x < 0 || currentVoxel.y < 0 || currentVoxel.z < 0 ||
                currentVoxel.x >= 8 || currentVoxel.y >= 8 || currentVoxel.z >= 8) break;

            const uint32_t voxelLinearIdx =
                static_cast<uint32_t>(currentVoxel.z * 64 + currentVoxel.y * 8 + currentVoxel.x);
            const uint32_t voxelData = brickWord(absBrickIndex, voxelLinearIdx);

            if (voxelData != 0u) {
                outNormal = glm::vec3(0.0f);
                if (axisMask == 1u)      outNormal.x = -static_cast<float>(step.x);
                else if (axisMask == 2u) outNormal.y = -static_cast<float>(step.y);
                else                     outNormal.z = -static_cast<float>(step.z);
                if (i == 0) {
                    const glm::vec3 absDir = glm::abs(rayDir);
                    if (absDir.x > absDir.y && absDir.x > absDir.z) outNormal = glm::vec3(-glm::sign(rayDir.x), 0.0f, 0.0f);
                    else if (absDir.y > absDir.z)                   outNormal = glm::vec3(0.0f, -glm::sign(rayDir.y), 0.0f);
                    else                                            outNormal = glm::vec3(0.0f, 0.0f, -glm::sign(rayDir.z));
                }
                outVoxel = currentVoxel;
                outVoxelLinearIdx = voxelLinearIdx;
                return true;
            }

            if (tMax.x < tMax.y && tMax.x < tMax.z) { currentVoxel.x += step.x; tMax.x += deltaDist.x; axisMask = 1u; }
            else if (tMax.y < tMax.z)               { currentVoxel.y += step.y; tMax.y += deltaDist.y; axisMask = 2u; }
            else                                    { currentVoxel.z += step.z; tMax.z += deltaDist.z; axisMask = 4u; }
        }
        return false;
    }

    // Reconstruct the ABSOLUTE integer voxel cell in the octree's [0,n] grid from the
    // leaf node's ESVO geometry + the brick-local voxel. The GPU shades by leaf-entry
    // hitT (coarse), so its world hitPoint is the brick FACE, not the voxel — to compare
    // the actual HIT VOXEL with castRay we recover the cell here.
    //   node local-min corner (unmirrored [1,2]) -> grid01 -> grid[0,n] -> + localVoxel
    glm::ivec3 absoluteVoxelCell(const TraversalState& state, const RayCoefficients& coef,
                                 const glm::ivec3& localVoxel, int brickSize) const {
        // Unmirror the node min corner to canonical [1,2] space (CoordinateTransforms.glsl
        // unmirrorToLocalSpace): for a mirrored axis the local min corner is
        // 3 - scale_exp2 - pos.
        glm::vec3 localMin = state.pos;
        if ((coef.octant_mask & 1) == 0) localMin.x = 3.0f - state.scale_exp2 - localMin.x;
        if ((coef.octant_mask & 2) == 0) localMin.y = 3.0f - state.scale_exp2 - localMin.y;
        if ((coef.octant_mask & 4) == 0) localMin.z = 3.0f - state.scale_exp2 - localMin.z;

        // grid01 min corner of the node, then scale to [0,n] (n == bricksPerAxis*brickSize).
        const glm::vec3 grid01Min = localMin - glm::vec3(1.0f);
        const float n = static_cast<float>(m_cfg.bricksPerAxis) * static_cast<float>(brickSize);
        const glm::vec3 nodeGridMin = grid01Min * n;
        // The node at brick scale spans exactly `brickSize` grid cells; add the local voxel.
        return glm::ivec3(
            static_cast<int>(std::lround(nodeGridMin.x)) + localVoxel.x,
            static_cast<int>(std::lround(nodeGridMin.y)) + localVoxel.y,
            static_cast<int>(std::lround(nodeGridMin.z)) + localVoxel.z);
    }

    uint32_t brickWord(uint32_t absBrickIndex, uint32_t voxelLinearIdx) const {
        const uint32_t idx = absBrickIndex * 512u + voxelLinearIdx;
        if (idx >= m_brickCount * 512u) return 0u;  // OOB guard
        return m_brickData[idx];
    }

    // ====================================================================
    // BodyInstanceRayMarch.comp — handleLeafHitInstanced
    // ====================================================================
    bool handleLeafHit(const TraversalState& state, const RayCoefficients& coef,
                       const glm::vec3& rayDir, float tBias, const glm::vec3& rayOrigin,
                       uint32_t childPointer, uint32_t validMask, uint32_t leafMask, Hit& out) const {
        const int BRICK_SIZE_VAL = m_cfg.brickSize;
        const int localChildIdx = mirroredToLocalOctant(state.idx, coef.octant_mask);
        if (localChildIdx < 0 || localChildIdx > 7) return false;

        const uint32_t totalInternalChildren = static_cast<uint32_t>(std::popcount(validMask & ~leafMask));
        const uint32_t leafChildrenBeforeMe  = countLeavesBefore(validMask, leafMask, localChildIdx);
        const uint32_t leafDescriptorIndex   = childPointer + totalInternalChildren + leafChildrenBeforeMe;

        const ChildDescriptor leafDescriptor = fetchNode(leafDescriptorIndex);
        const uint32_t localBrickIdx = getContourPointer(leafDescriptor);
        if (localBrickIdx == SVO_INVALID_INDEX) return false;

        const float tHit = state.t_min;
        const glm::vec3 rayDirLocal = glm::mat3(m_cfg.worldToLocal) * rayDir;
        const glm::vec3 hitPos12 = coef.normOrigin + rayDirLocal * tHit;

        glm::vec3 posInBrick = computePosInBrick(hitPos12, state.pos, state.scale_exp2,
                                                 coef.octant_mask, BRICK_SIZE_VAL);
        posInBrick = glm::clamp(posInBrick, glm::vec3(0.0f), glm::vec3(static_cast<float>(BRICK_SIZE_VAL) - 0.001f));

        glm::ivec3 brickVoxel;
        glm::vec3 brickNormal;
        uint32_t voxelLinearIdx;
        if (marchBrickInstanced(rayDir, posInBrick, localBrickIdx, brickVoxel, brickNormal, voxelLinearIdx)) {
            out.normal = brickNormal;
            out.t = tBias + tHit;                       // GPU's coarse (leaf-entry) hitT
            out.hitPoint = rayOrigin + rayDir * out.t;  // leaf-entry hitPoint (brick face)
            out.brickIndex = static_cast<uint32_t>(m_brickArrayBase) + localBrickIdx;
            out.voxelLinearIdx = voxelLinearIdx;
            out.voxel = absoluteVoxelCell(state, coef, brickVoxel, BRICK_SIZE_VAL);
            return true;
        }
        return false;
    }
};

}  // namespace Vixen::SVO
