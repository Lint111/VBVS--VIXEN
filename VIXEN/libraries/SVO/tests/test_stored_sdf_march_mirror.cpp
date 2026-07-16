// test_stored_sdf_march_mirror.cpp — gpu-shader-debug CPU mirror of the
// Stored-SDF leaf march in shaders/StoredSdf.glsl + shaders/BodyInstanceRayMarch.comp.
//
// PURPOSE: the GPU renderer draws the Stored-SDF sphere with systematic angular
// brick-aligned dark HOLES (fillRatio ~0.988, 1.0 = solid). This test reproduces
// those holes ON THE CPU by:
//   1. baking the EXACT render-test scene (RECIPE_SPHERE, n=64, center=32, r=26,
//      band=2.5, brickDepth=3) and SerializeSdf-ing it — identical channelPool /
//      brickLookup / OctreeConfig to what the GPU consumes,
//   2. driving rays with the SAME ESVO traversal the GPU uses (a verbatim copy of
//      GpuTraversalMirror's PUSH/ADVANCE/POP loop) but swapping in the Stored-SDF
//      leaf handler (handleLeafHitInstancedSdf + marchBrickSdf + the pool readers),
//      translated 1:1 from the GLSL,
//   3. comparing each ray's mirror hit against the ANALYTIC sphere: a ray whose
//      analytic-sphere intersection is inside the body but which the mirror misses
//      is a HOLE.
//
// SYNC CONTRACT: the ESVO traversal block is a line-by-line copy of
// GpuTraversalMirror.h; the SDF leaf block is a line-by-line port of the cited GLSL.
//
// @shader shaders/StoredSdf.glsl (_samplePoolVoxel, sampleSdfTrilinear, marchBrickSdf)
// @shader shaders/BodyInstanceRayMarch.comp (handleLeafHitInstancedSdf, traverseOctreeInstanced)

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// MSVC defines far/near/min/max as macros via <windows.h>.
#undef far
#undef near
#undef min
#undef max

#include "SdfBake.h"
#include "ShellOctreeGpu.h"
#include "SdfRecipes.h"
#include "VoxelChannelFormat.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include <bit>

using namespace Vixen::SVO;

namespace {

// ===========================================================================
// Scene parameters — IDENTICAL to BodyOctreeSceneNode::EnsureOctreesBuilt() SDF bake
// and test_body_instance_raymarch_render.cpp RenderStoredSdf* camera framing.
// ===========================================================================
constexpr int   kSdfN          = 64;
constexpr float kSdfCenter     = 32.0f;
constexpr float kSdfRadius     = 26.0f;
constexpr float kSdfBand       = 2.5f;
constexpr int   kSdfBrickDepth = 3;

constexpr float kWorldGridSize = 10.0f;   // ShellOctreeGpu::SerializeSdf localToWorld scale
constexpr float kRS            = 2.0f;    // render test renderScale for the framed body

// Single positive unallocated-brick sentinel (mirrors StoredSdf.glsl _samplePoolVoxel's
// 1e9 return + the single kBrickUnalloc (0xFFFFFFFF) sentinel in ShellOctreeGpu.h).
constexpr float kSdfSentinel = 1e9f;

// ===========================================================================
// SdfMarchMirror — verbatim ESVO traversal (GpuTraversalMirror.h) with the
// Stored-SDF leaf handler swapped in.
// ===========================================================================
class SdfMarchMirror {
public:
    struct Hit {
        bool      hit = false;
        float     t = 0.0f;
        glm::vec3 hitPoint{0.0f};
        glm::vec3 normal{0.0f};
        float     sHit = 0.0f;     // grid-voxel arc-length within the hit brick
        glm::ivec3 hitBrick{0};    // brick the hit landed in
        int       iterations = 0;
        int       exitCode = 0;
        int       leavesVisited = 0;   // # of allocated SDF leaves the ray marched
    };

    // Precision-fragility probe: multiply the leaf-entry t_min used to build gridEntry
    // by (1 + tJitter). Simulates the GPU computing a slightly different tv_max/t_min
    // than exact CPU arithmetic. 0 = exact (default).
    float m_tJitter = 0.0f;

    // DIAGNOSTIC counters (mutable so const castRay can tally them across a sweep).
    mutable long long m_leafHits = 0;      // SDF leaves that the march was invoked on
    mutable long long m_brickDisagree = 0; // of those, floor-brick != ESVO-leaf-brick
    bool m_dumpDisagree = false;           // print the first few disagreements
    bool m_useEsvoBrick = false;           // (probe) bound the march to the ESVO leaf brick

    explicit SdfMarchMirror(const SerializedOctree& s)
        : m_cfg(s.config) {
        m_nodeCount      = s.nodeCount;
        m_nodes          = reinterpret_cast<const ChildDescriptor*>(s.nodes.data());
        m_nodeArrayBase  = m_cfg.nodeArrayBase;
        // Stored-SDF pool + grid lookup (the shader's bindings 11/12).
        m_pool           = reinterpret_cast<const float*>(s.channelPool.data());
        m_poolFloats     = s.channelPool.size() / sizeof(float);
        m_lookup         = reinterpret_cast<const uint32_t*>(s.brickGridLookup.data());
        m_lookupCount    = s.brickGridLookup.size() / sizeof(uint32_t);
        m_brickStride    = s.brickStrideFloats;
        m_poolBrickBase  = m_cfg.poolBrickBase;
        m_bpaSdf         = static_cast<int>(m_cfg.bricksPerAxisSdf);
        for (uint32_t i = 0; i < s.channelCount && i < kMaxChannels; ++i)
            m_channels[i] = s.channels[i];
        m_channelCount = s.channelCount;
    }

    // Port of traverseOctreeInstanced(): cast a WORLD-space ray, return the hit.
    Hit castRay(const glm::vec3& rayOrigin, const glm::vec3& rayDirIn) const {
        Hit out;
        const glm::vec3 rayDir = glm::normalize(rayDirIn);

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
            const uint32_t validMask    = getValidMask(parent);
            const uint32_t leafMask     = getLeafMask(parent);
            const uint32_t childPointer = getChildPointer(parent);

            bool isLeaf = false;
            float tv_max = 0.0f, tx_center = 0.0f, ty_center = 0.0f, tz_center = 0.0f;

            if (checkChildValidity(state, coef, validMask, leafMask, isLeaf, tv_max,
                                   tx_center, ty_center, tz_center)) {
                if (isLeaf) {
                    if (handleLeafHitSdf(state, coef, rayDir, tEntryWorld, rayOrigin, out)) {
                        out.hit = true;
                        out.exitCode = 1;
                        out.iterations = iter + 1;
                        return out;
                    }
                    state.t_min = tv_max;
                } else {
                    executePushPhase(state, coef, stack, validMask, leafMask, childPointer,
                                     tv_max, tx_center, ty_center, tz_center);
                    continue;
                }
            }

            int step_mask = 0;
            const int advanceResult = executeAdvancePhase(state, coef, step_mask);
            if (advanceResult == 0) {
                if (state.scale < m_cfg.esvoMaxScale) state.t_max = stack[state.scale + 1].t_max;
            }
            if (advanceResult == 1) {
                const int popResult = executePopPhase(state, coef, stack, step_mask);
                if (popResult == 1) { out.exitCode = 3; out.iterations = iter + 1; return out; }
            }
        }
        out.iterations = iter;
        return out;
    }

    // -------- DIAGNOSTIC: expose per-leaf details for a single ray --------
    struct LeafProbe {
        glm::vec3 gridEntry{0.0f};
        glm::vec3 gridDirN{0.0f};
        glm::ivec3 brickPicked{0};
        uint32_t   brickIdxLookup = 0xFFFFFFFFu;
        float      sMax = 0.0f;
        float      minD = 1e30f;
        int        steps = 0;
        bool       hit = false;
    };
    void probeRay(const glm::vec3& rayOrigin, const glm::vec3& rayDirIn,
                  std::vector<LeafProbe>& probes) const;

    // Brute-force REFERENCE: march the GLOBAL trilinear field with a tiny fixed step,
    // ignoring brick bounds entirely. Returns the minimum d seen and whether d ever went
    // < 0 (true penetration of the trilinear iso). This is the ground truth for whether a
    // ray ACTUALLY crosses the reconstructed surface — independent of the per-brick march.
    struct RefResult { float minD = 1e30f; bool crossed = false; float sCross = -1.0f;
                       glm::vec3 crossPos{0.0f}; glm::ivec3 crossBrick{0}; };
    RefResult referenceMarch(const glm::vec3& rayOrigin, const glm::vec3& rayDirIn) const;
    bool brickAllocated(const glm::ivec3& b) const {
        const uint32_t fl = gridToLookupIdx(b, m_bpaSdf);
        const uint32_t idx = m_cfg.brickLookupBase + fl;
        if (fl == 0xFFFFFFFFu || idx >= m_lookupCount) return false;
        return m_lookup[idx] != 0xFFFFFFFFu;
    }
    // Diagnostic accessors (root-cause probe): the RAW lookup value at a brick coord
    // (0xFFFFFFFF unallocated sentinel / else a real brick index), the grid side, and a
    // single SDF-channel pool read at a grid voxel (positive sentinel if unallocated).
    int      bpaSdf() const { return m_bpaSdf; }
    uint32_t lookupRaw(const glm::ivec3& b) const {
        const uint32_t fl = gridToLookupIdx(b, m_bpaSdf);
        const uint32_t idx = m_cfg.brickLookupBase + fl;
        if (fl == 0xFFFFFFFFu || idx >= m_lookupCount) return 0xFFFFFFFFu;
        return m_lookup[idx];
    }
    float sampleSdfVoxelPub(const glm::ivec3& gridCoord) const { return sampleSdfVoxel(gridCoord); }
    float sampleSdfTrilinearPub(const glm::vec3& gridPos) const {
        return sampleSdfTrilinear(gridPos);
    }
    glm::vec3 sdfGradientStoredPub(const glm::vec3& gridPos) const {
        return sdfGradientStored(gridPos);
    }
    float sampleSdfTrilinearGenericPub(const glm::vec3& gridPos) const {
        const uint32_t base = channelBaseFloats(SEM_SDF);
        const glm::vec3 f = glm::fract(gridPos);
        const glm::ivec3 i = glm::ivec3(glm::floor(gridPos));
        const float c000 = samplePoolVoxel(base, i + glm::ivec3(0,0,0), 0);
        const float c100 = samplePoolVoxel(base, i + glm::ivec3(1,0,0), 0);
        const float c010 = samplePoolVoxel(base, i + glm::ivec3(0,1,0), 0);
        const float c110 = samplePoolVoxel(base, i + glm::ivec3(1,1,0), 0);
        const float c001 = samplePoolVoxel(base, i + glm::ivec3(0,0,1), 0);
        const float c101 = samplePoolVoxel(base, i + glm::ivec3(1,0,1), 0);
        const float c011 = samplePoolVoxel(base, i + glm::ivec3(0,1,1), 0);
        const float c111 = samplePoolVoxel(base, i + glm::ivec3(1,1,1), 0);
        return glm::mix(
            glm::mix(glm::mix(c000, c100, f.x), glm::mix(c010, c110, f.x), f.y),
            glm::mix(glm::mix(c001, c101, f.x), glm::mix(c011, c111, f.x), f.y),
            f.z);
    }
    // Did the ESVO traversal visit a leaf whose brick == `want` for this ray?
    bool esvoVisitsBrick(const glm::vec3& rayOrigin, const glm::vec3& rayDirIn, const glm::ivec3& want) const;

    // Dump the per-step (s, d) sequence of marchBrickSdf for the leaf whose ESVO brick
    // matches `wantBrick`, for a single ray. Reveals exactly how the surface is stepped over.
    void dumpLeafSteps(const glm::vec3& rayOrigin, const glm::vec3& rayDirIn,
                       const glm::ivec3& wantBrick) const;

private:
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
    int m_nodeArrayBase = 0;

    // Stored-SDF buffers
    const float*    m_pool   = nullptr;  size_t m_poolFloats = 0;
    const uint32_t* m_lookup = nullptr;  size_t m_lookupCount = 0;
    uint32_t m_brickStride = 0;
    uint32_t m_poolBrickBase = 0;
    int      m_bpaSdf = 0;
    ChannelDesc m_channels[kMaxChannels]{};
    uint32_t m_channelCount = 0;

    // ---- descriptor bit extraction (SVOTypes.glsl) ----
    static uint32_t descriptorX(const ChildDescriptor& d) { uint32_t w[2]; std::memcpy(w, &d, 8); return w[0]; }
    static uint32_t descriptorY(const ChildDescriptor& d) { uint32_t w[2]; std::memcpy(w, &d, 8); return w[1]; }
    static uint32_t getChildPointer(const ChildDescriptor& d)   { return descriptorX(d) & 0x7FFFu; }
    static uint32_t getValidMask(const ChildDescriptor& d)      { return (descriptorX(d) >> 16) & 0xFFu; }
    static uint32_t getLeafMask(const ChildDescriptor& d)       { return (descriptorX(d) >> 24) & 0xFFu; }
    static uint32_t getContourPointer(const ChildDescriptor& d) { return descriptorY(d) & 0xFFFFFFu; }
    static bool childExists(uint32_t v, int i) { return ((v >> i) & 1u) != 0u; }
    static bool childIsLeaf(uint32_t l, int i) { return ((l >> i) & 1u) != 0u; }
    static uint32_t countLeavesBefore(uint32_t validMask, uint32_t leafMask, int childIndex) {
        if (childIndex <= 0) return 0u;
        uint32_t mask = (1u << childIndex) - 1u;
        return static_cast<uint32_t>(std::popcount(validMask & leafMask & mask));
    }
    static int mirroredToLocalOctant(int idx, int octant_mask) { return idx ^ ((~octant_mask) & 7); }
    static const uint32_t SVO_INVALID_INDEX = 0xFFFFFFu;

    ChildDescriptor fetchNode(uint32_t nodeIndex) const {
        uint32_t idx = static_cast<uint32_t>(m_nodeArrayBase) + nodeIndex;
        if (idx >= m_nodeCount) return ChildDescriptor{};
        return m_nodes[idx];
    }

    // ---- RayGeneration.glsl ----
    static glm::vec2 rayAABBIntersection(const glm::vec3& ro, const glm::vec3& rd,
                                         const glm::vec3& bmin, const glm::vec3& bmax) {
        const glm::vec3 invDir = 1.0f / rd;
        const glm::vec3 t0 = (bmin - ro) * invDir;
        const glm::vec3 t1 = (bmax - ro) * invDir;
        const glm::vec3 tMin = glm::min(t0, t1);
        const glm::vec3 tMax = glm::max(t0, t1);
        return glm::vec2(glm::max(glm::max(tMin.x, tMin.y), tMin.z),
                         glm::min(glm::min(tMax.x, tMax.y), tMax.z));
    }
    glm::vec3 worldToNormalized(const glm::vec3& worldPos) const {
        const glm::vec4 localPos = m_cfg.worldToLocal * glm::vec4(worldPos, 1.0f);
        const glm::vec3 p = glm::vec3(localPos) / localPos.w;
        return p + 1.0f;
    }

    // ---- ESVOCoefficients.glsl ----
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

    // ---- ESVOTraversal.glsl initTraversalState ----
    TraversalState initTraversalState(const RayCoefficients& coef, StackEntry stack[kStackSize],
                                      bool rayStartsInside) const {
        TraversalState state;
        if (rayStartsInside) {
            state.t_min = 0.0f;
            state.t_max = glm::min(glm::min(coef.tx_coef - coef.tx_bias, coef.ty_coef - coef.ty_bias),
                                   coef.tz_coef - coef.tz_bias);
        } else {
            state.t_min = glm::max(glm::max(2.0f * coef.tx_coef - coef.tx_bias, 2.0f * coef.ty_coef - coef.ty_bias),
                                   2.0f * coef.tz_coef - coef.tz_bias);
            state.t_max = glm::min(glm::min(coef.tx_coef - coef.tx_bias, coef.ty_coef - coef.ty_bias),
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
        } else if (1.5f * coef.tx_coef - coef.tx_bias > state.t_min) { state.idx ^= 1; state.pos.x = 1.5f; }
        if (std::abs(coef.rayDir.y) < kDirEps || usePositionBased) {
            if (mirroredOrigin.y >= 1.5f) { state.idx |= 2; state.pos.y = 1.5f; }
        } else if (1.5f * coef.ty_coef - coef.ty_bias > state.t_min) { state.idx ^= 2; state.pos.y = 1.5f; }
        if (std::abs(coef.rayDir.z) < kDirEps || usePositionBased) {
            if (mirroredOrigin.z >= 1.5f) { state.idx |= 4; state.pos.z = 1.5f; }
        } else if (1.5f * coef.tz_coef - coef.tz_bias > state.t_min) { state.idx ^= 4; state.pos.z = 1.5f; }
        return state;
    }

    // ---- ESVOTraversal.glsl corner / checkChildValidity ----
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
                            uint32_t validMask, uint32_t leafMask, bool& isLeaf, float& tv_max,
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
        if ((state.idx & step_mask) != 0) return 1;
        return 0;
    }

    int executePopPhase(TraversalState& state, const RayCoefficients& /*coef*/,
                        StackEntry stack[kStackSize], int step_mask) const {
        if (state.scale >= m_cfg.esvoMaxScale) {
            if (state.t_min > state.t_max ||
                state.pos.x < 1.0f || state.pos.x >= 2.0f ||
                state.pos.y < 1.0f || state.pos.y >= 2.0f ||
                state.pos.z < 1.0f || state.pos.z >= 2.0f) return 1;
            return 0;
        }
        uint32_t differing_bits = 0u;
        if ((step_mask & 1) != 0) differing_bits |= floatBitsToUint(state.pos.x) ^ floatBitsToUint(state.pos.x + state.scale_exp2);
        if ((step_mask & 2) != 0) differing_bits |= floatBitsToUint(state.pos.y) ^ floatBitsToUint(state.pos.y + state.scale_exp2);
        if ((step_mask & 4) != 0) differing_bits |= floatBitsToUint(state.pos.z) ^ floatBitsToUint(state.pos.z + state.scale_exp2);
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
    // StoredSdf.glsl — pool readers (1:1 port)
    // ====================================================================
    uint32_t channelBaseFloats(uint32_t sem) const {
        for (uint32_t i = 0; i < m_channelCount; ++i)
            if (m_channels[i].semanticId == sem) return m_channels[i].channelBaseFloats;
        return 0xFFFFFFFFu;
    }
    static uint32_t gridToLookupIdx(const glm::ivec3& bc, int bpa) {
        if (bc.x < 0 || bc.y < 0 || bc.z < 0 || bc.x >= bpa || bc.y >= bpa || bc.z >= bpa)
            return 0xFFFFFFFFu;
        return static_cast<uint32_t>(bc.z * bpa * bpa + bc.y * bpa + bc.x);
    }
    // 1:1 port of StoredSdf.glsl _samplePoolVoxel — returns the single positive sentinel
    // (+kSdfSentinel) for any unallocated brick / out-of-grid / absent channel.
    float samplePoolVoxel(uint32_t channelBase, const glm::ivec3& gridCoord, int comp) const {
        if (channelBase == 0xFFFFFFFFu) return kSdfSentinel;
        const int bpa = m_bpaSdf;
        if (bpa <= 0) return kSdfSentinel;
        const int gridSide = bpa * 8;
        if (gridCoord.x < 0 || gridCoord.y < 0 || gridCoord.z < 0 ||
            gridCoord.x >= gridSide || gridCoord.y >= gridSide || gridCoord.z >= gridSide)
            return kSdfSentinel;
        // GLSL ivec3 `/ 8` truncates toward zero; mirror exactly via component div.
        const glm::ivec3 bc(gridCoord.x / 8, gridCoord.y / 8, gridCoord.z / 8);
        const glm::ivec3 voxelInBrick = gridCoord - bc * 8;
        const uint32_t flatLookup = gridToLookupIdx(bc, bpa);
        if (flatLookup == 0xFFFFFFFFu) return kSdfSentinel;  // out of grid
        const uint32_t lookupIdx = m_cfg.brickLookupBase + flatLookup;
        if (lookupIdx >= m_lookupCount) return kSdfSentinel;
        const uint32_t brickIdx = m_lookup[lookupIdx];
        if (brickIdx == kBrickUnalloc) return kSdfSentinel;  // unallocated brick
        const uint32_t voxelIdx = static_cast<uint32_t>(voxelInBrick.z * 64 + voxelInBrick.y * 8 + voxelInBrick.x);
        const uint32_t fi = m_poolBrickBase + brickIdx * m_brickStride + channelBase
                          + static_cast<uint32_t>(comp) * kVoxelsPerBrick + voxelIdx;
        if (fi >= m_poolFloats) return kSdfSentinel;
        return m_pool[fi];
    }
    float sampleSdfVoxel(const glm::ivec3& gridCoord) const {
        return samplePoolVoxel(channelBaseFloats(SEM_SDF), gridCoord, 0);
    }
    // 1:1 port of StoredSdf.glsl sampleSdfTrilinear — PLAIN trilinear blend of the 8 corners.
    // An empty corner reads +kSdfSentinel, so a stencil straddling empty space blends to a
    // large positive MAGNITUDE that marchBrickSdf detects (d>SENTINEL_D) and steps through.
    float sampleSdfTrilinear(const glm::vec3& gridPos) const {
        const uint32_t base = channelBaseFloats(SEM_SDF);
        if (base == 0xFFFFFFFFu) return kSdfSentinel;
        const glm::vec3 f = glm::fract(gridPos);
        const glm::ivec3 i = glm::ivec3(glm::floor(gridPos));
        float c000, c100, c010, c110, c001, c101, c011, c111;

        const int gridSide = m_bpaSdf * 8;
        const bool inGrid = m_bpaSdf > 0 &&
            i.x >= 0 && i.y >= 0 && i.z >= 0 &&
            i.x < gridSide && i.y < gridSide && i.z < gridSide;
        const glm::ivec3 brickCoord = inGrid ? i / 8 : glm::ivec3(-1);
        const glm::ivec3 local = i - brickCoord * 8;
        const bool oneBrick = inGrid && local.x < 7 && local.y < 7 && local.z < 7;

        if (oneBrick) {
            const uint32_t flatLookup = gridToLookupIdx(brickCoord, m_bpaSdf);
            const uint32_t lookupIdx = m_cfg.brickLookupBase + flatLookup;
            const uint32_t brickIdx = lookupIdx < m_lookupCount
                ? m_lookup[lookupIdx]
                : kBrickUnalloc;
            if (brickIdx == kBrickUnalloc) return kSdfSentinel;

            const uint32_t voxel000 = static_cast<uint32_t>(
                local.z * 64 + local.y * 8 + local.x);
            const size_t poolVoxelBase = static_cast<size_t>(m_poolBrickBase) +
                static_cast<size_t>(brickIdx) * m_brickStride + base;
            if (poolVoxelBase + voxel000 + 73u >= m_poolFloats)
                return kSdfSentinel;
            c000 = m_pool[poolVoxelBase + voxel000];
            c100 = m_pool[poolVoxelBase + voxel000 + 1u];
            c010 = m_pool[poolVoxelBase + voxel000 + 8u];
            c110 = m_pool[poolVoxelBase + voxel000 + 9u];
            c001 = m_pool[poolVoxelBase + voxel000 + 64u];
            c101 = m_pool[poolVoxelBase + voxel000 + 65u];
            c011 = m_pool[poolVoxelBase + voxel000 + 72u];
            c111 = m_pool[poolVoxelBase + voxel000 + 73u];
        } else {
            c000 = samplePoolVoxel(base, i + glm::ivec3(0,0,0), 0);
            c100 = samplePoolVoxel(base, i + glm::ivec3(1,0,0), 0);
            c010 = samplePoolVoxel(base, i + glm::ivec3(0,1,0), 0);
            c110 = samplePoolVoxel(base, i + glm::ivec3(1,1,0), 0);
            c001 = samplePoolVoxel(base, i + glm::ivec3(0,0,1), 0);
            c101 = samplePoolVoxel(base, i + glm::ivec3(1,0,1), 0);
            c011 = samplePoolVoxel(base, i + glm::ivec3(0,1,1), 0);
            c111 = samplePoolVoxel(base, i + glm::ivec3(1,1,1), 0);
        }
        return glm::mix(
            glm::mix(glm::mix(c000, c100, f.x), glm::mix(c010, c110, f.x), f.y),
            glm::mix(glm::mix(c001, c101, f.x), glm::mix(c011, c111, f.x), f.y),
            f.z);
    }
    // Oracle trilinear used by the reference march — identical reconstruction to
    // sampleSdfTrilinear so the "is there a real crossing" verdict matches the field the
    // per-brick march actually traces. The reference march additionally guards |d|>1e8 so a
    // pure-empty (all-sentinel) sample is never treated as a crossing.
    float sampleSdfTrilinearRaw(const glm::vec3& gridPos) const {
        return sampleSdfTrilinear(gridPos);
    }
    // 1:1 port of StoredSdf.glsl sdfGradientStored: exact derivative of the
    // current trilinear cell, with the old sentinel-aware finite difference kept
    // only as a rare boundary fallback.
    glm::vec3 sdfGradientStored(const glm::vec3& gridPos) const {
        const uint32_t base = channelBaseFloats(SEM_SDF);
        const glm::vec3 f = glm::fract(gridPos);
        const glm::ivec3 i = glm::ivec3(glm::floor(gridPos));
        const float c000 = samplePoolVoxel(base, i + glm::ivec3(0,0,0), 0);
        const float c100 = samplePoolVoxel(base, i + glm::ivec3(1,0,0), 0);
        const float c010 = samplePoolVoxel(base, i + glm::ivec3(0,1,0), 0);
        const float c110 = samplePoolVoxel(base, i + glm::ivec3(1,1,0), 0);
        const float c001 = samplePoolVoxel(base, i + glm::ivec3(0,0,1), 0);
        const float c101 = samplePoolVoxel(base, i + glm::ivec3(1,0,1), 0);
        const float c011 = samplePoolVoxel(base, i + glm::ivec3(0,1,1), 0);
        const float c111 = samplePoolVoxel(base, i + glm::ivec3(1,1,1), 0);

        constexpr float sentinel = 100.0f;
        const bool contaminated =
            std::abs(c000) > sentinel || std::abs(c100) > sentinel ||
            std::abs(c010) > sentinel || std::abs(c110) > sentinel ||
            std::abs(c001) > sentinel || std::abs(c101) > sentinel ||
            std::abs(c011) > sentinel || std::abs(c111) > sentinel;

        glm::vec3 g;
        if (contaminated) {
            constexpr float h = 0.5f;
            const float d0 = sampleSdfTrilinear(gridPos);
            auto axisGradient = [&](const glm::vec3& e) {
                const float plus = sampleSdfTrilinear(gridPos + e);
                const float minus = sampleSdfTrilinear(gridPos - e);
                if (std::abs(plus) > sentinel) return (d0 - minus) * 2.0f;
                if (std::abs(minus) > sentinel) return (plus - d0) * 2.0f;
                return plus - minus;
            };
            g = glm::vec3(axisGradient(glm::vec3(h,0,0)),
                          axisGradient(glm::vec3(0,h,0)),
                          axisGradient(glm::vec3(0,0,h)));
        } else {
            g.x = glm::mix(glm::mix(c100-c000, c110-c010, f.y),
                           glm::mix(c101-c001, c111-c011, f.y), f.z);
            g.y = glm::mix(glm::mix(c010-c000, c110-c100, f.x),
                           glm::mix(c011-c001, c111-c101, f.x), f.z);
            g.z = glm::mix(glm::mix(c001-c000, c101-c100, f.x),
                           glm::mix(c011-c010, c111-c110, f.x), f.y);
        }
        const float len = glm::length(g);
        return (len > 1e-6f) ? g / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // ====================================================================
    // StoredSdf.glsl — marchBrickSdf (1:1 port).
    //   leafBrick: the brick coordinate to bound the march to.
    //     - CURRENT (buggy) shader: floor((gridEntry+gridDirN*1e-3)/8), recomputed here.
    //     - FIX (m_useEsvoBrick): the brick the ESVO traversal descended to, passed in.
    // ====================================================================
    bool marchBrickSdf(const glm::vec3& gridEntry, const glm::vec3& gridDirN,
                       const glm::ivec3& esvoBrick,
                       glm::vec3& hitNormal, float& sHit, glm::ivec3& hitBrick) const {
        hitNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        sHit = 0.0f;
        const float kNudge = 1e-3f;
        const glm::ivec3 brick = m_useEsvoBrick
            ? esvoBrick
            : glm::ivec3(glm::floor((gridEntry + gridDirN * kNudge) / 8.0f));
        hitBrick = brick;
        const glm::vec3 bMin = glm::vec3(brick) * 8.0f;
        const glm::vec3 bMax = bMin + glm::vec3(8.0f);

        const glm::vec3 invD(
            std::abs(gridDirN.x) > 1e-8f ? 1.0f / gridDirN.x : 1e20f,
            std::abs(gridDirN.y) > 1e-8f ? 1.0f / gridDirN.y : 1e20f,
            std::abs(gridDirN.z) > 1e-8f ? 1.0f / gridDirN.z : 1e20f);
        const glm::vec3 t0 = (bMin - gridEntry) * invD;
        const glm::vec3 t1 = (bMax - gridEntry) * invD;
        const glm::vec3 thi = glm::max(t0, t1);
        const float sMax = glm::max(glm::min(glm::min(thi.x, thi.y), thi.z), 0.0f);

        const int   MAX_STEPS  = 96;
        const float EPS        = 0.01f;
        const float SENTINEL_D = 100.0f;   // d above this ⇒ sentinel-contaminated stencil

        // 1:1 port of StoredSdf.glsl marchBrickSdf. An unallocated-brick stencil reads the
        // single positive sentinel, so a contaminated sample is large-positive (d>SENTINEL_D)
        // and the 1/√3 Lipschitz step degrades to a bounded 1-voxel probe through it.
        float s = 0.0f;
        for (int i = 0; i < MAX_STEPS; ++i) {
            if (s > sMax) return false;
            const glm::vec3 p = gridEntry + gridDirN * s;
            const float d = sampleSdfTrilinear(p);
            if (d < EPS) { hitNormal = sdfGradientStored(p); sHit = s; return true; }
            s += (d > SENTINEL_D) ? 1.0f : glm::max(d * 0.5773503f, EPS);
        }
        return false;
    }

    // ====================================================================
    // BodyInstanceRayMarch.comp — handleLeafHitInstancedSdf (1:1 port)
    // ====================================================================
    bool handleLeafHitSdf(const TraversalState& state, const RayCoefficients& coef,
                          const glm::vec3& rayDir, float tBias, const glm::vec3& rayOrigin,
                          Hit& out) const {
        const int bpa = m_bpaSdf;
        if (bpa <= 0) return false;
        const float gridScale = static_cast<float>(bpa * 8);

        const glm::vec3 rayDirLocal = glm::mat3(m_cfg.worldToLocal) * rayDir;
        const float tEntry = state.t_min * (1.0f + m_tJitter);  // jitter probe (0 by default)
        const glm::vec3 hitPos12 = coef.normOrigin + rayDirLocal * tEntry;
        const glm::vec3 gridEntry = (hitPos12 - glm::vec3(1.0f)) * gridScale;
        const float dirLen = glm::length(rayDirLocal);
        if (dirLen < 1e-12f) return false;
        const glm::vec3 gridDirN = rayDirLocal / dirLen;

        // DIAGNOSTIC: the brick marchBrickSdf will select (floor of nudged entry) vs the
        // brick the ESVO actually descended to (the leaf node geometry). If they DISAGREE
        // the march bounds/lookup are for a different brick than the leaf → structural hole.
        const glm::ivec3 floorBrick =
            glm::ivec3(glm::floor((gridEntry + gridDirN * 1e-3f) / 8.0f));
        const glm::ivec3 esvoBrick = esvoLeafBrick(state, coef);
        ++m_leafHits;
        if (floorBrick != esvoBrick) {
            ++m_brickDisagree;
            if (m_dumpDisagree && m_brickDisagree <= 40) {
                const uint32_t flE = gridToLookupIdx(esvoBrick, m_bpaSdf);
                const uint32_t flF = gridToLookupIdx(floorBrick, m_bpaSdf);
                const uint32_t biE = (flE==0xFFFFFFFFu||flE>=m_lookupCount)?0xFFFFFFFFu:m_lookup[flE];
                const uint32_t biF = (flF==0xFFFFFFFFu||flF>=m_lookupCount)?0xFFFFFFFFu:m_lookup[flF];
                std::printf("  [DISAGREE #%lld] esvoBrick=(%d,%d,%d)[lk=%s] floorBrick=(%d,%d,%d)[lk=%s] "
                            "gridEntry=(%.5f,%.5f,%.5f) t_min=%.6f\n",
                            m_brickDisagree, esvoBrick.x, esvoBrick.y, esvoBrick.z,
                            (biE==0xFFFFFFFFu?"UNALLOC":"alloc"),
                            floorBrick.x, floorBrick.y, floorBrick.z,
                            (biF==0xFFFFFFFFu?"UNALLOC":"alloc"),
                            gridEntry.x, gridEntry.y, gridEntry.z, state.t_min);
            }
        }

        glm::vec3 nrm; float sHit; glm::ivec3 hitBrick;
        if (!marchBrickSdf(gridEntry, gridDirN, esvoBrick, nrm, sHit, hitBrick)) {
            out.leavesVisited += 1;   // we DID visit an allocated leaf, but it missed
            return false;
        }
        out.normal = nrm;
        const float tHitLocal = state.t_min + sHit / (dirLen * gridScale);
        out.t = tBias + tHitLocal;
        out.hitPoint = rayOrigin + rayDir * out.t;
        out.sHit = sHit;
        out.hitBrick = hitBrick;
        return true;
    }

    // The brick coordinate the ESVO traversal actually descended to (the leaf node).
    // Mirrors GpuTraversalMirror::absoluteVoxelCell node-min logic: unmirror state.pos to
    // canonical [1,2], → grid01 → [0,n], then divide by brickSize (=8) for the brick coord.
    glm::ivec3 esvoLeafBrick(const TraversalState& state, const RayCoefficients& coef) const {
        glm::vec3 localMin = state.pos;
        if ((coef.octant_mask & 1) == 0) localMin.x = 3.0f - state.scale_exp2 - localMin.x;
        if ((coef.octant_mask & 2) == 0) localMin.y = 3.0f - state.scale_exp2 - localMin.y;
        if ((coef.octant_mask & 4) == 0) localMin.z = 3.0f - state.scale_exp2 - localMin.z;
        const glm::vec3 grid01Min = localMin - glm::vec3(1.0f);
        const float n = static_cast<float>(m_bpaSdf) * 8.0f;
        const glm::vec3 nodeGridMin = grid01Min * n;
        return glm::ivec3(
            static_cast<int>(std::lround(nodeGridMin.x / 8.0)),
            static_cast<int>(std::lround(nodeGridMin.y / 8.0)),
            static_cast<int>(std::lround(nodeGridMin.z / 8.0)));
    }
};

// Out-of-line probe (needs the private types fully declared above).
inline void SdfMarchMirror::probeRay(const glm::vec3& rayOrigin, const glm::vec3& rayDirIn,
                                     std::vector<SdfMarchMirror::LeafProbe>& probes) const {
    const glm::vec3 rayDir = glm::normalize(rayDirIn);
    const glm::vec3 rayOriginLocal = glm::vec3(m_cfg.worldToLocal * glm::vec4(rayOrigin, 1.0f));
    const glm::vec3 rayDirLocal    = glm::mat3(m_cfg.worldToLocal) * rayDir;
    const glm::vec2 gridT = rayAABBIntersection(rayOriginLocal, rayDirLocal, glm::vec3(0.0f), glm::vec3(1.0f));
    if (gridT.y < 0.0f) return;
    const bool rayStartsInside = (gridT.x < 0.0f);
    glm::vec3 rayStartWorld;
    if (rayStartsInside) { rayStartWorld = rayOrigin; }
    else {
        const glm::vec3 e = rayOriginLocal + rayDirLocal * (gridT.x + kEpsilon);
        rayStartWorld = glm::vec3(m_cfg.localToWorld * glm::vec4(e, 1.0f));
    }
    const RayCoefficients coef = initRayCoefficients(rayDir, rayStartWorld);
    StackEntry stack[kStackSize];
    TraversalState state = initTraversalState(coef, stack, rayStartsInside);
    if (state.t_min >= state.t_max) return;

    auto probeLeaf = [&](const TraversalState& st) -> LeafProbe {
        LeafProbe pr;
        const int bpa = m_bpaSdf;
        const float gridScale = static_cast<float>(bpa * 8);
        const glm::vec3 hitPos12 = coef.normOrigin + rayDirLocal * st.t_min;
        pr.gridEntry = (hitPos12 - glm::vec3(1.0f)) * gridScale;
        const float dirLen = glm::length(rayDirLocal);
        pr.gridDirN = rayDirLocal / dirLen;
        const glm::ivec3 brick = glm::ivec3(glm::floor((pr.gridEntry + pr.gridDirN * 1e-3f) / 8.0f));
        pr.brickPicked = brick;
        const uint32_t fl = gridToLookupIdx(brick, bpa);
        pr.brickIdxLookup = (fl == 0xFFFFFFFFu || fl >= m_lookupCount) ? 0xFFFFFFFFu : m_lookup[fl];
        const glm::vec3 bMin = glm::vec3(brick) * 8.0f, bMax = bMin + glm::vec3(8.0f);
        const glm::vec3 invD(std::abs(pr.gridDirN.x)>1e-8f?1.0f/pr.gridDirN.x:1e20f,
                             std::abs(pr.gridDirN.y)>1e-8f?1.0f/pr.gridDirN.y:1e20f,
                             std::abs(pr.gridDirN.z)>1e-8f?1.0f/pr.gridDirN.z:1e20f);
        const glm::vec3 t0=(bMin-pr.gridEntry)*invD, t1=(bMax-pr.gridEntry)*invD, thi=glm::max(t0,t1);
        pr.sMax = glm::max(glm::min(glm::min(thi.x,thi.y),thi.z),0.0f);
        float s=0.0f;
        for (int i=0;i<96;++i){ if(s>pr.sMax)break; const glm::vec3 p=pr.gridEntry+pr.gridDirN*s;
            const float d=sampleSdfTrilinear(p); pr.minD=glm::min(pr.minD,d); pr.steps=i+1;
            if(d<0.01f){pr.hit=true;break;}
            s += (d>100.0f) ? 1.0f : glm::max(d*0.5773503f,0.01f); }   // contaminated → 1-voxel probe
        return pr;
    };

    for (int iter = 0; iter < kMaxIters && state.scale <= m_cfg.esvoMaxScale; ++iter) {
        const ChildDescriptor parent = fetchNode(state.parentPtr);
        const uint32_t validMask = getValidMask(parent), leafMask = getLeafMask(parent),
                       childPointer = getChildPointer(parent);
        bool isLeaf = false; float tv_max=0, txc=0, tyc=0, tzc=0;
        if (checkChildValidity(state, coef, validMask, leafMask, isLeaf, tv_max, txc, tyc, tzc)) {
            if (isLeaf) {
                LeafProbe pr = probeLeaf(state);
                probes.push_back(pr);
                if (pr.hit) return;
                state.t_min = tv_max;
            } else {
                executePushPhase(state, coef, stack, validMask, leafMask, childPointer, tv_max, txc, tyc, tzc);
                continue;
            }
        }
        int sm = 0; const int ar = executeAdvancePhase(state, coef, sm);
        if (ar == 0) { if (state.scale < m_cfg.esvoMaxScale) state.t_max = stack[state.scale+1].t_max; }
        if (ar == 1) { if (executePopPhase(state, coef, stack, sm) == 1) return; }
    }
}

inline SdfMarchMirror::RefResult
SdfMarchMirror::referenceMarch(const glm::vec3& rayOrigin, const glm::vec3& rayDirIn) const {
    const glm::vec3 rayDir = glm::normalize(rayDirIn);
    const glm::vec3 roLocal = glm::vec3(m_cfg.worldToLocal * glm::vec4(rayOrigin, 1.0f));
    const glm::vec3 rdLocal = glm::mat3(m_cfg.worldToLocal) * rayDir;
    const glm::vec2 gridT = rayAABBIntersection(roLocal, rdLocal, glm::vec3(0.0f), glm::vec3(1.0f));
    RefResult r;
    if (gridT.y < 0.0f) return r;
    const float gridScale = static_cast<float>(m_bpaSdf * 8);
    const float dirLen = glm::length(rdLocal);
    if (dirLen < 1e-12f) return r;
    const glm::vec3 gridDirN = rdLocal / dirLen;
    const glm::vec3 g0 = roLocal * gridScale;                       // local[0,1]→grid origin
    const float s0 = glm::max(gridT.x, 0.0f) * dirLen * gridScale;  // entry arc-length (grid)
    const float s1 = gridT.y * dirLen * gridScale;                  // exit  arc-length (grid)
    const float step = 0.02f;
    for (float s = s0; s <= s1; s += step) {
        const glm::vec3 p = g0 + gridDirN * s;
        const float d = sampleSdfTrilinearRaw(p);   // RAW (sentinel-aware) — honest ground truth
        if (d > 1e8f) continue;   // positive-sentinel region: ignore — never treat a
                                  // +SENTINEL straddle as a real crossing
        r.minD = glm::min(r.minD, d);
        if (d < 0.0f) { r.crossed = true; r.sCross = s; r.crossPos = p;
                        r.crossBrick = glm::ivec3(glm::floor(p / 8.0f)); return r; }
    }
    return r;
}

inline bool SdfMarchMirror::esvoVisitsBrick(const glm::vec3& rayOrigin, const glm::vec3& rayDirIn,
                                            const glm::ivec3& want) const {
    const glm::vec3 rayDir = glm::normalize(rayDirIn);
    const glm::vec3 roLocal = glm::vec3(m_cfg.worldToLocal * glm::vec4(rayOrigin, 1.0f));
    const glm::vec3 rdLocal = glm::mat3(m_cfg.worldToLocal) * rayDir;
    const glm::vec2 gridT = rayAABBIntersection(roLocal, rdLocal, glm::vec3(0.0f), glm::vec3(1.0f));
    if (gridT.y < 0.0f) return false;
    const bool inside = (gridT.x < 0.0f);
    glm::vec3 startW = inside ? rayOrigin
        : glm::vec3(m_cfg.localToWorld * glm::vec4(roLocal + rdLocal * (gridT.x + kEpsilon), 1.0f));
    const RayCoefficients coef = initRayCoefficients(rayDir, startW);
    StackEntry stack[kStackSize];
    TraversalState state = initTraversalState(coef, stack, inside);
    if (state.t_min >= state.t_max) return false;
    for (int iter = 0; iter < kMaxIters && state.scale <= m_cfg.esvoMaxScale; ++iter) {
        const ChildDescriptor parent = fetchNode(state.parentPtr);
        const uint32_t vm = getValidMask(parent), lm = getLeafMask(parent), cp = getChildPointer(parent);
        bool isLeaf=false; float tv=0,a=0,b=0,c=0;
        if (checkChildValidity(state, coef, vm, lm, isLeaf, tv, a, b, c)) {
            if (isLeaf) {
                if (esvoLeafBrick(state, coef) == want) return true;
                state.t_min = tv;
            } else { executePushPhase(state, coef, stack, vm, lm, cp, tv, a, b, c); continue; }
        }
        int sm=0; const int ar=executeAdvancePhase(state, coef, sm);
        if (ar==0){ if(state.scale<m_cfg.esvoMaxScale) state.t_max=stack[state.scale+1].t_max; }
        if (ar==1){ if(executePopPhase(state, coef, stack, sm)==1) return false; }
    }
    return false;
}

inline void SdfMarchMirror::dumpLeafSteps(const glm::vec3& rayOrigin, const glm::vec3& rayDirIn,
                                          const glm::ivec3& wantBrick) const {
    const glm::vec3 rayDir = glm::normalize(rayDirIn);
    const glm::vec3 roLocal = glm::vec3(m_cfg.worldToLocal * glm::vec4(rayOrigin, 1.0f));
    const glm::vec3 rdLocal = glm::mat3(m_cfg.worldToLocal) * rayDir;
    const glm::vec2 gridT = rayAABBIntersection(roLocal, rdLocal, glm::vec3(0.0f), glm::vec3(1.0f));
    if (gridT.y < 0.0f) return;
    const bool inside = (gridT.x < 0.0f);
    glm::vec3 startW = inside ? rayOrigin
        : glm::vec3(m_cfg.localToWorld * glm::vec4(roLocal + rdLocal * (gridT.x + kEpsilon), 1.0f));
    const RayCoefficients coef = initRayCoefficients(rayDir, startW);
    StackEntry stack[kStackSize];
    TraversalState state = initTraversalState(coef, stack, inside);
    if (state.t_min >= state.t_max) return;
    for (int iter = 0; iter < kMaxIters && state.scale <= m_cfg.esvoMaxScale; ++iter) {
        const ChildDescriptor parent = fetchNode(state.parentPtr);
        const uint32_t vm = getValidMask(parent), lm = getLeafMask(parent), cp = getChildPointer(parent);
        bool isLeaf=false; float tv=0,a=0,b=0,c=0;
        if (checkChildValidity(state, coef, vm, lm, isLeaf, tv, a, b, c)) {
            if (isLeaf) {
                const glm::ivec3 eb = esvoLeafBrick(state, coef);
                const bool dumpAll = (wantBrick == glm::ivec3(-999));
                if (dumpAll || eb == wantBrick) {
                    const float gridScale = static_cast<float>(m_bpaSdf * 8);
                    const glm::vec3 hp12 = coef.normOrigin + rdLocal * state.t_min;
                    const glm::vec3 gridEntry = (hp12 - glm::vec3(1.0f)) * gridScale;
                    const glm::vec3 gdN = rdLocal / glm::length(rdLocal);
                    // The brick marchBrickSdf actually bounds to (floor of nudged entry).
                    const glm::ivec3 mb = glm::ivec3(glm::floor((gridEntry + gdN*1e-3f)/8.0f));
                    const glm::vec3 bMin = glm::vec3(mb)*8.0f, bMax = bMin+glm::vec3(8.0f);
                    const glm::vec3 invD(std::abs(gdN.x)>1e-8f?1.0f/gdN.x:1e20f,
                                         std::abs(gdN.y)>1e-8f?1.0f/gdN.y:1e20f,
                                         std::abs(gdN.z)>1e-8f?1.0f/gdN.z:1e20f);
                    const glm::vec3 t0=(bMin-gridEntry)*invD, t1=(bMax-gridEntry)*invD, thi=glm::max(t0,t1);
                    const float sMax = glm::max(glm::min(glm::min(thi.x,thi.y),thi.z),0.0f);
                    std::printf("   [STEPS esvoBrick=(%d,%d,%d) marchBrick=(%d,%d,%d)] gridEntry=(%.4f,%.4f,%.4f) sMax=%.4f\n",
                                eb.x,eb.y,eb.z, mb.x,mb.y,mb.z, gridEntry.x,gridEntry.y,gridEntry.z,sMax);
                    float s=0.0f; float minD=1e30f;
                    for (int i=0;i<96;++i){
                        if (s>sMax){ std::printf("      EXIT s=%.4f>sMax (minD=%.5f)\n", s, minD); break; }
                        const glm::vec3 p=gridEntry+gdN*s; const float d=sampleSdfTrilinear(p);
                        const bool contam = d > 100.0f;
                        minD = glm::min(minD, contam?minD:d);
                        const float stepTaken = contam ? 1.0f : glm::max(d*0.5773503f,0.01f);
                        std::printf("      i=%2d s=%.4f p=(%.3f,%.3f,%.3f) d=%.5f step=%.5f%s\n", i, s,
                                    p.x,p.y,p.z, d, stepTaken, (contam?"  [contam]":""));
                        if (!contam && d<0.01f){ std::printf("      HIT at s=%.4f\n", s); break; }
                        s+=stepTaken;
                    }
                    if (!dumpAll) return;
                }
                state.t_min = tv;
            } else { executePushPhase(state, coef, stack, vm, lm, cp, tv, a, b, c); continue; }
        }
        int sm=0; const int ar=executeAdvancePhase(state, coef, sm);
        if (ar==0){ if(state.scale<m_cfg.esvoMaxScale) state.t_max=stack[state.scale+1].t_max; }
        if (ar==1){ if(executePopPhase(state, coef, stack, sm)==1) return; }
    }
}

// ===========================================================================
// Build the framed camera EXACTLY like RenderStoredSdf*: getRayDir / MakeCamera.
// ===========================================================================
struct Camera {
    glm::vec3 eye, dir, up, right;
    float fovDeg = 45.0f, aspect = 1.0f;
};
Camera MakeCamera(const glm::vec3& eye, const glm::vec3& target, uint32_t w, uint32_t h) {
    Camera c;
    c.eye = eye;
    c.dir = glm::normalize(target - eye);
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    c.right = glm::normalize(glm::cross(c.dir, worldUp));
    c.up    = glm::normalize(glm::cross(c.right, c.dir));
    c.fovDeg = 45.0f;
    c.aspect = static_cast<float>(w) / static_cast<float>(h);
    return c;
}
glm::vec3 getRayDir(const Camera& c, float u, float v) {
    const float tanHalfFov = std::tan(glm::radians(c.fovDeg * 0.5f));
    float ndcx = u * 2.0f - 1.0f;
    float ndcy = v * 2.0f - 1.0f;
    ndcy = -ndcy;
    const glm::vec3 rd = c.dir + c.right * ndcx * tanHalfFov * c.aspect + c.up * ndcy * tanHalfFov;
    return glm::normalize(rd);
}

// Analytic sphere intersection (the body's TRUE surface) in the SAME world frame the
// shader renders: body world centre = worldPos + 0.5*kWorldGridSize*renderScale; the SDF
// iso is at grid radius kSdfRadius of grid side kSdfN, scaled by kWorldGridSize*renderScale/kSdfN.
struct AnalyticBody {
    glm::vec3 centre;
    float     radius;   // world radius of the iso-surface
};
AnalyticBody MakeAnalyticBody() {
    AnalyticBody b;
    const glm::vec3 worldPos(0.0f);
    b.centre = worldPos + glm::vec3(0.5f * kWorldGridSize * kRS);
    // grid->world scale: the octree [0,1]^3 maps to kWorldGridSize*renderScale world units,
    // and the grid is kSdfN voxels across, so one grid voxel = kWorldGridSize*kRS/kSdfN world.
    const float gridToWorld = kWorldGridSize * kRS / static_cast<float>(kSdfN);
    b.radius = kSdfRadius * gridToWorld;
    return b;
}
// De-instance a TRUE-world ray into the base octree's frame, EXACTLY as the GPU
// main() does before traverseOctreeInstanced: instOrigin=(rayOrigin-worldPos)/renderScale,
// instDir=rayDir/renderScale (NOT renormalized — the traversal is scale-tolerant).
// worldPos = origin for the framed body, renderScale = kRS.
struct InstRay { glm::vec3 origin, dir; };
InstRay DeInstance(const glm::vec3& eye, const glm::vec3& rayDir) {
    const float invScale = 1.0f / kRS;
    const glm::vec3 worldPos(0.0f);
    return InstRay{ (eye - worldPos) * invScale, rayDir * invScale };
}

// returns nearest t>0 of ray vs sphere, or -1 on miss.
float sphereHitT(const AnalyticBody& b, const glm::vec3& ro, const glm::vec3& rd) {
    const glm::vec3 oc = ro - b.centre;
    const float bq = glm::dot(oc, rd);
    const float cq = glm::dot(oc, oc) - b.radius * b.radius;
    const float disc = bq * bq - cq;
    if (disc < 0.0f) return -1.0f;
    const float sq = std::sqrt(disc);
    float t = -bq - sq;
    if (t < 0.0f) t = -bq + sq;
    return (t >= 0.0f) ? t : -1.0f;
}

struct BakedScene {
    SdfBodyOctree body;
    SerializedOctree serialized;
};
BakedScene BakeScene(uint32_t recipeId, float amp, float freq) {
    BakedScene s;
    RecipeParams rp{};
    rp.radius = kSdfRadius; rp.displaceAmp = amp; rp.displaceFreq = freq;
    const glm::vec3 center(kSdfCenter, kSdfCenter, kSdfCenter);
    SdfBakeResult baked = BakeRecipeToSdfWorld(recipeId, center, rp, kSdfN, kSdfBand, kSdfBrickDepth);
    s.body = BuildSdfBodyOctree(baked, kSdfBrickDepth);
    s.serialized = SerializeSdf(s.body);
    return s;
}

// ===========================================================================
// ROOT-CAUSE PROBE SCAFFOLD — exercise the ACTUAL demo scene: 3 octrees
// concatenated via ConcatenateSdf (the path BodyOctreeSceneNode uploads),
// then build a SerializedOctree that views ONE octree (idx k) of that
// concatenation, with octree-k's poolBrickBase / nodeArrayBase / its own
// lookup sub-table. This is what the prior single-octree mirror NEVER tested:
// octree 1 (the displaced body) lives at poolBrickBase>0, nodeArrayBase>0, and
// its lookup is the SECOND bpa^3 sub-table — the worst-artifact case.
// ===========================================================================
struct ConcatScene {
    // Keep the owning bodies alive (octree holds pointers into world/registry).
    std::vector<SdfBodyOctree> bodies;
    ConcatenatedOctrees cat;
};
ConcatScene BakeConcatDemo() {
    // EXACTLY BodyOctreeSceneNode::EnsureOctreesBuilt() under VIXEN_STORED_SDF_DEMO:
    //   kind 0 smooth, kind 1 displaced (amp 2.7, freq 0.375), kind 2 smooth.
    struct Kind { uint32_t recipe; float amp; float freq; };
    const Kind kinds[3] = {
        { RECIPE_SPHERE,           0.0f, 0.0f   },
        { RECIPE_DISPLACED_SPHERE, 2.7f, 0.375f },
        { RECIPE_SPHERE,           0.0f, 0.0f   },
    };
    ConcatScene s;
    s.bodies.reserve(3);
    const glm::vec3 center(kSdfCenter, kSdfCenter, kSdfCenter);
    for (const Kind& k : kinds) {
        RecipeParams rp{};
        rp.radius = kSdfRadius; rp.displaceAmp = k.amp; rp.displaceFreq = k.freq;
        // NOTE: the demo (BodyOctreeSceneNode.cpp:311) calls BakeRecipeToSdfWorld
        // WITHOUT the brickDepth arg → default 3. Match that exactly here.
        SdfBakeResult baked = BakeRecipeToSdfWorld(k.recipe, center, rp, kSdfN, kSdfBand);
        s.bodies.push_back(BuildSdfBodyOctree(baked, kSdfBrickDepth));
    }
    std::vector<const SdfBodyOctree*> ptrs;
    for (const SdfBodyOctree& b : s.bodies) ptrs.push_back(&b);
    s.cat = ConcatenateSdf(ptrs);
    return s;
}

// Build a SerializedOctree that the SdfMarchMirror can consume, viewing octree
// `k` of a concatenation. The mirror addresses nodes via (nodeArrayBase + idx)
// into the FULL concatenated node buffer, and pool via (poolBrickBase +
// brickIdx*stride + ...), and uses brickLookupBase against the full lookup.
SerializedOctree ViewConcatOctree(const ConcatScene& s, uint32_t k) {
    const OctreeConfig& cfg = s.cat.configs[k];
    SerializedOctree v;
    v.config       = cfg;                       // carries nodeArrayBase / poolBrickBase for k
    v.nodes        = s.cat.nodes;               // FULL concatenated node buffer (offset via base)
    v.nodeCount    = static_cast<uint32_t>(s.cat.nodes.size() / sizeof(ChildDescriptor));
    v.channelPool  = s.cat.channelPool;         // FULL concatenated pool (offset via poolBrickBase)
    v.brickStrideFloats = cfg.brickStrideFloats;
    v.channelCount = cfg.channelCount;
    for (uint32_t i = 0; i < cfg.channelCount && i < kMaxChannels; ++i)
        v.channels[i] = cfg.channels[i];
    v.brickGridLookup = s.cat.brickGridLookup;
    return v;
}

}  // namespace

// ===========================================================================
// REPRO: sweep the framed camera and count rays where the analytic sphere is hit
// (interior of the silhouette) but the mirror march returns NO hit → that is a HOLE.
// ===========================================================================
struct SweepStats {
    int analyticHits = 0;
    int mirrorMiss = 0;
    int visitedLeafButMissed = 0;
    int neverVisitedLeaf = 0;
    long long leafHits = 0;
    long long brickDisagree = 0;
};
// Sweep the framed camera; count holes (analytic-interior ray the mirror misses).
static SweepStats SweepHoles(SdfMarchMirror& mirror, const AnalyticBody& body, float jitter) {
    constexpr uint32_t kW = 512, kH = 512;
    const glm::vec3 focus = body.centre;
    const float     R     = 0.5f * kWorldGridSize * kRS;
    const glm::vec3 eye   = focus + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * (R * 4.0f);
    const Camera cam = MakeCamera(eye, focus, kW, kH);

    mirror.m_tJitter = jitter;
    mirror.m_leafHits = 0; mirror.m_brickDisagree = 0;
    SweepStats st;
    for (uint32_t py = 0; py < kH; ++py)
        for (uint32_t px = 0; px < kW; ++px) {
            const float u = (px + 0.5f) / kW, v = (py + 0.5f) / kH;
            const glm::vec3 rd = getRayDir(cam, u, v);
            const float ta = sphereHitT(body, eye, rd);
            if (ta < 0.0f) continue;
            // Comfortable silhouette interior (exclude the anti-aliased limb).
            const glm::vec3 toCentre = glm::normalize(body.centre - eye);
            const float cosA = glm::dot(rd, toCentre);
            const float cosEdge = std::sqrt(std::max(0.0f,
                1.0f - (body.radius * body.radius) / glm::dot(body.centre - eye, body.centre - eye)));
            if (cosA <= cosEdge + 1e-4f) continue;
            ++st.analyticHits;
            const InstRay ir = DeInstance(eye, rd);
            const SdfMarchMirror::Hit h = mirror.castRay(ir.origin, ir.dir);
            if (!h.hit) {
                ++st.mirrorMiss;
                if (h.leavesVisited > 0) ++st.visitedLeafButMissed; else ++st.neverVisitedLeaf;
            }
        }
    st.leafHits = mirror.m_leafHits;
    st.brickDisagree = mirror.m_brickDisagree;
    return st;
}

/**
 * @test StoredSdfMarchMirror.SingleLookupFastPathMatchesGenericSampler
 * @coverage StoredSdf.glsl::sampleSdfTrilinear
 * @category regression
 * @owner SVO
 * @added 2026-07-15
 * @last-pass 2026-07-15
 */
TEST(StoredSdfMarchMirror, SingleLookupFastPathMatchesGenericSampler) {
    BakedScene scene = BakeScene(RECIPE_SPHERE, 0.0f, 0.0f);
    SdfMarchMirror mirror(scene.serialized);
    const glm::vec3 probes[] = {
        {32.25f, 32.50f, 32.75f}, // one-brick fast path
        {31.75f, 32.50f, 32.75f}, // x brick boundary
        {39.25f, 39.50f, 39.75f}, // xyz brick boundary
        {9.25f, 32.50f, 32.75f},  // near the sphere surface
        {-0.25f, 12.50f, 12.75f}, // negative grid edge sentinel
        {63.75f, 12.50f, 12.75f}, // positive grid edge sentinel
    };

    for (const glm::vec3& p : probes) {
        EXPECT_FLOAT_EQ(mirror.sampleSdfTrilinearPub(p),
                        mirror.sampleSdfTrilinearGenericPub(p))
            << "gridPos=(" << p.x << "," << p.y << "," << p.z << ")";
    }
}

/**
 * @test StoredSdfMarchMirror.GradientMatchesLocalTrilinearDerivative
 * @coverage StoredSdf.glsl::sdfGradientStored
 * @category regression
 * @owner SVO
 * @added 2026-07-15
 * @last-pass 2026-07-15
 */
TEST(StoredSdfMarchMirror, GradientMatchesLocalTrilinearDerivative) {
    BakedScene scene = BakeScene(RECIPE_DISPLACED_SPHERE, 2.7f, 0.375f);
    SdfMarchMirror mirror(scene.serialized);
    const glm::vec3 probes[] = {
        {41.63f, 47.87f, 50.21f},
        {18.19f, 39.37f, 52.73f},
        {49.41f, 13.23f, 34.67f},
    };

    // Within one trilinear cell, a small symmetric difference is an oracle for
    // the interpolant's exact derivative. Keep every probe farther than eps from
    // an integer cell boundary so the oracle cannot cross into another cell.
    constexpr float eps = 0.01f;
    for (const glm::vec3& p : probes) {
        const glm::vec3 numerical(
            mirror.sampleSdfTrilinearPub(p + glm::vec3(eps, 0, 0)) -
                mirror.sampleSdfTrilinearPub(p - glm::vec3(eps, 0, 0)),
            mirror.sampleSdfTrilinearPub(p + glm::vec3(0, eps, 0)) -
                mirror.sampleSdfTrilinearPub(p - glm::vec3(0, eps, 0)),
            mirror.sampleSdfTrilinearPub(p + glm::vec3(0, 0, eps)) -
                mirror.sampleSdfTrilinearPub(p - glm::vec3(0, 0, eps)));
        ASSERT_GT(glm::length(numerical), 1e-5f);
        const glm::vec3 expected = glm::normalize(numerical);
        const glm::vec3 actual = mirror.sdfGradientStoredPub(p);
        EXPECT_NEAR(actual.x, expected.x, 2e-4f) << "p.x=" << p.x;
        EXPECT_NEAR(actual.y, expected.y, 2e-4f) << "p.y=" << p.y;
        EXPECT_NEAR(actual.z, expected.z, 2e-4f) << "p.z=" << p.z;
    }
}

// ===========================================================================
// REPRO: an EXACT-arithmetic mirror of the GPU march is HOLE-FREE; tiny perturbation
// of the leaf-entry t (simulating GPU 32-bit-float rounding through the ESVO pipeline)
// makes brick-aligned holes APPEAR — proving the holes are brick-boundary precision-
// fragility, NOT an algorithmic miss. Also reports floor-brick vs ESVO-leaf-brick
// disagreement (the structural amplifier).
// ===========================================================================
TEST(StoredSdfMarchMirror, ReproducesBrickAlignedHoles) {
    BakedScene scene = BakeScene(RECIPE_SPHERE, 0.0f, 0.0f);
    SdfMarchMirror mirror(scene.serialized);
    const AnalyticBody body = MakeAnalyticBody();

    const float jitters[] = { 0.0f, 1e-6f, 1e-5f, 1e-4f, 1e-3f };
    SweepStats exact{};
    for (float j : jitters) {
        const SweepStats st = SweepHoles(mirror, body, j);
        const double missRate = st.analyticHits ? double(st.mirrorMiss) / st.analyticHits : 0.0;
        const double disRate  = st.leafHits ? double(st.brickDisagree) / st.leafHits : 0.0;
        std::printf("[REPRO] jitter=%.0e  interiorRays=%d  miss=%d (%.4f)  "
                    "leafHits=%lld brickDisagree=%lld (%.4f)  visitedButMissed=%d neverVisited=%d\n",
                    j, st.analyticHits, st.mirrorMiss, missRate,
                    st.leafHits, st.brickDisagree, disRate,
                    st.visitedLeafButMissed, st.neverVisitedLeaf);
        if (j == 0.0f) exact = st;
    }

    EXPECT_GT(exact.analyticHits, 10000) << "camera framing did not cover the body interior";
    // The decisive finding: EXACT arithmetic is HOLE-FREE (the algorithm is correct);
    // the GPU's holes come from float-precision sensitivity, demonstrated by the jitter rows.
    EXPECT_EQ(exact.mirrorMiss, 0)
        << "exact-arithmetic mirror itself has holes — they would be an algorithmic bug, not precision";
}

// ===========================================================================
// DIAGNOSTIC: dump the cases where marchBrickSdf's floor-derived brick disagrees
// with the brick the ESVO actually descended to. These are the brick-boundary
// fragility seeds (the march bounds + lookup use floor(gridEntry/8), NOT the ESVO leaf).
// ===========================================================================
TEST(StoredSdfMarchMirror, DiagBrickSelectionDisagreement) {
    BakedScene scene = BakeScene(RECIPE_SPHERE, 0.0f, 0.0f);
    SdfMarchMirror mirror(scene.serialized);
    const AnalyticBody body = MakeAnalyticBody();
    mirror.m_dumpDisagree = true;
    std::printf("[DISAGREE] floor(gridEntry/8) vs ESVO-leaf-brick — exact arithmetic:\n");
    const SweepStats st = SweepHoles(mirror, body, 0.0f);
    std::printf("[DISAGREE] total leafHits=%lld disagree=%lld\n", st.leafHits, st.brickDisagree);
    SUCCEED();
}

// ===========================================================================
// DIAGNOSTIC: find the EXACT-arithmetic interior misses across an orbit and dump the
// full per-leaf probe for the first few — revealing the real hole mechanism.
// ===========================================================================
TEST(StoredSdfMarchMirror, DiagFindExactMisses) {
    BakedScene scene = BakeScene(RECIPE_SPHERE, 0.0f, 0.0f);
    SdfMarchMirror mirror(scene.serialized);
    const AnalyticBody body = MakeAnalyticBody();
    std::printf("[MISS] single-sentinel march (default)\n");

    constexpr uint32_t kW = 256, kH = 256;
    const float R = 0.5f * kWorldGridSize * kRS;
    int dumped = 0;
    for (int vi = 0; vi < 64 && dumped < 6; ++vi) {
        const float az = 6.2831853f * vi / 64;
        const float el = 0.6f * std::sin(2.0f * az);
        const glm::vec3 dirToEye = glm::normalize(glm::vec3(std::cos(az), std::sin(el), std::sin(az)));
        const glm::vec3 eye = body.centre + dirToEye * (R * 4.0f);
        const Camera cam = MakeCamera(eye, body.centre, kW, kH);
        const float d2 = glm::dot(body.centre - eye, body.centre - eye);
        const float cosEdge = std::sqrt(std::max(0.0f, 1.0f - (body.radius*body.radius)/d2));
        const glm::vec3 toC = glm::normalize(body.centre - eye);
        for (uint32_t py = 0; py < kH && dumped < 6; ++py)
            for (uint32_t px = 0; px < kW && dumped < 6; ++px) {
                const float u=(px+0.5f)/kW, v=(py+0.5f)/kH;
                const glm::vec3 rd = getRayDir(cam, u, v);
                const float ta = sphereHitT(body, eye, rd);
                if (ta < 0.0f) continue;
                if (glm::dot(rd, toC) <= cosEdge + 1e-4f) continue;
                const InstRay ir = DeInstance(eye, rd);
                if (mirror.castRay(ir.origin, ir.dir).hit) continue;   // only misses
                const SdfMarchMirror::RefResult ref = mirror.referenceMarch(ir.origin, ir.dir);
                if (!ref.crossed) continue;   // only genuine step-over holes
                ++dumped;
                const bool xAlloc = mirror.brickAllocated(ref.crossBrick);
                const bool xVisit = mirror.esvoVisitsBrick(ir.origin, ir.dir, ref.crossBrick);
                std::vector<SdfMarchMirror::LeafProbe> ps;
                mirror.probeRay(ir.origin, ir.dir, ps);
                std::printf("\n[MISS] view=%d px=(%u,%u) analyticT=%.4f leavesVisited=%zu  "
                            "REF: minD=%.5f crossBrick=(%d,%d,%d) alloc=%d esvoVisits=%d\n",
                            vi, px, py, ta, ps.size(), ref.minD,
                            ref.crossBrick.x, ref.crossBrick.y, ref.crossBrick.z, (int)xAlloc, (int)xVisit);
                for (size_t i = 0; i < ps.size(); ++i) {
                    const auto& p = ps[i];
                    char lk[16];
                    if (p.brickIdxLookup==0xFFFFFFFFu) std::snprintf(lk,sizeof(lk),"UNALLOC");
                    else std::snprintf(lk,sizeof(lk),"%u",p.brickIdxLookup);
                    std::printf("   leaf#%zu gridEntry=(%.4f,%.4f,%.4f) dirN=(%.4f,%.4f,%.4f) "
                                "brick=(%d,%d,%d)[%s] sMax=%.4f steps=%d minD=%.5f hit=%d\n",
                                i,p.gridEntry.x,p.gridEntry.y,p.gridEntry.z,p.gridDirN.x,p.gridDirN.y,p.gridDirN.z,
                                p.brickPicked.x,p.brickPicked.y,p.brickPicked.z,lk,p.sMax,p.steps,p.minD,(int)p.hit);
                }
            }
    }
    std::printf("\n[MISS] dumped %d missing rays\n", dumped);
    SUCCEED();
}

// ===========================================================================
// DIAGNOSTIC: dump the per-step (s,d) sequence for the KNOWN step-over hole at
// view=1 px=(112,113), leaf brick (7,4,4) — showing the surface crossing being
// stepped over by the 1/sqrt3 multiplicative step.
// ===========================================================================
TEST(StoredSdfMarchMirror, DiagStepOverSequence) {
    BakedScene scene = BakeScene(RECIPE_SPHERE, 0.0f, 0.0f);
    SdfMarchMirror mirror(scene.serialized);
    const AnalyticBody body = MakeAnalyticBody();
    constexpr uint32_t kW = 256, kH = 256;
    const float R = 0.5f * kWorldGridSize * kRS;
    const int vi = 1;
    const float az = 6.2831853f * vi / 64;
    const float el = 0.6f * std::sin(2.0f * az);
    const glm::vec3 dirToEye = glm::normalize(glm::vec3(std::cos(az), std::sin(el), std::sin(az)));
    const glm::vec3 eye = body.centre + dirToEye * (R * 4.0f);
    const Camera cam = MakeCamera(eye, body.centre, kW, kH);
    for (uint32_t px : {112u}) {
        const float u=(px+0.5f)/kW, v=(113u+0.5f)/kH;
        const glm::vec3 rd = getRayDir(cam, u, v);
        const InstRay ir = DeInstance(eye, rd);
        const SdfMarchMirror::RefResult ref = mirror.referenceMarch(ir.origin, ir.dir);
        const bool hit = mirror.castRay(ir.origin, ir.dir).hit;
        std::printf("\n[STEPOVER] px=(%u,113) castRayHit=%d reference minD=%.5f crossed=%d crossBrick=(%d,%d,%d) "
                    "crossPos=(%.3f,%.3f,%.3f) allocCross=%d allocNbrX-=%d\n",
                    px, (int)hit, ref.minD, (int)ref.crossed, ref.crossBrick.x, ref.crossBrick.y, ref.crossBrick.z,
                    ref.crossPos.x, ref.crossPos.y, ref.crossPos.z,
                    (int)mirror.brickAllocated(ref.crossBrick),
                    (int)mirror.brickAllocated(ref.crossBrick - glm::ivec3(1,0,0)));
        mirror.dumpLeafSteps(ir.origin, ir.dir, glm::ivec3(-999));  // dump ALL leaves
    }
    SUCCEED();
}

// Multi-view sweep helper: orbit the body and count holes. Returns (interiorRays, miss).
// A "hole" here is an interior ray (analytic-sphere interior) where (a) the mirror march
// MISSES, AND (b) the brute-force fine reference march CONFIRMS the trilinear iso is
// actually crossed — so it is a genuine step-over hole, NOT a legitimate grazing-limb miss.
// missDeep (out): of the counted holes, how many are NOT in the thin grazing-limb band
// (cosA within 0.01 of cosEdge) — i.e. genuine interior step-overs vs limb-tangent rays
// that no per-brick march can resolve. nullptr to skip.
static std::pair<long long,long long> OrbitSweep(SdfMarchMirror& mirror, const AnalyticBody& body,
                                                 float jitter, int views,
                                                 long long* missDeep = nullptr) {
    constexpr uint32_t kW = 256, kH = 256;
    const float R = 0.5f * kWorldGridSize * kRS;
    long long interior = 0, miss = 0, deep = 0;
    mirror.m_tJitter = jitter;
    for (int vi = 0; vi < views; ++vi) {
        const float az = 6.2831853f * vi / views;
        const float el = 0.6f * std::sin(2.0f * az);   // wobble elevation for variety
        const glm::vec3 dirToEye = glm::normalize(glm::vec3(std::cos(az), std::sin(el), std::sin(az)));
        const glm::vec3 eye = body.centre + dirToEye * (R * 4.0f);
        const Camera cam = MakeCamera(eye, body.centre, kW, kH);
        const float d2 = glm::dot(body.centre - eye, body.centre - eye);
        const float cosEdge = std::sqrt(std::max(0.0f, 1.0f - (body.radius*body.radius)/d2));
        const glm::vec3 toC = glm::normalize(body.centre - eye);
        for (uint32_t py = 0; py < kH; ++py)
            for (uint32_t px = 0; px < kW; ++px) {
                const float u=(px+0.5f)/kW, v=(py+0.5f)/kH;
                const glm::vec3 rd = getRayDir(cam, u, v);
                if (sphereHitT(body, eye, rd) < 0.0f) continue;
                const float cosA = glm::dot(rd, toC);
                if (cosA <= cosEdge + 1e-4f) continue;   // interior only
                ++interior;
                const InstRay ir = DeInstance(eye, rd);
                if (!mirror.castRay(ir.origin, ir.dir).hit) {
                    // Only count it as a HOLE if the reconstructed trilinear iso is
                    // genuinely crossed (fine reference march). Legit grazing-limb misses
                    // (where the surface truly isn't reached) are excluded.
                    if (mirror.referenceMarch(ir.origin, ir.dir).crossed) {
                        ++miss;
                        if (cosA > cosEdge + 0.01f) ++deep;  // not in the thin limb band
                    }
                }
            }
    }
    if (missDeep) *missDeep = deep;
    return {interior, miss};
}

// ===========================================================================
// FIX PROOF: across an orbit, the single-positive-sentinel march (the shipped default)
// COMBINED with the occupancy-based brick selection (commit 7db15496 — every active
// brick, interior or shell, is allocated) drives the genuine step-over holes (interior
// rays where the reference fine-march confirms the trilinear iso IS crossed but the
// per-brick march misses) DOWN BY >5× vs the pre-fix baseline (117 smooth / 276 displaced
// on this orbit). This is the CPU SECONDARY check; the occupancy fix — NOT a sign-aware
// sentinel — is what makes a surface stencil never reach into an unallocated interior
// brick. The authoritative check is the lavapipe render (silhouette visually solid).
// ===========================================================================
TEST(StoredSdfMarchMirror, OccupancyFixRemovesHoles) {
    BakedScene scene = BakeScene(RECIPE_SPHERE, 0.0f, 0.0f);
    SdfMarchMirror mirror(scene.serialized);
    const AnalyticBody body = MakeAnalyticBody();

    const int views = 32;
    const long long kBaselineSmooth = 117;   // pre-fix step-overs on this orbit (documented)
    long long deep = 0;
    auto sweep = OrbitSweep(mirror, body, 0.0f, views, &deep);
    std::printf("[FIXPROOF] views=%d single-sentinel march + occupancy fix: holes=%lld / %lld (%.6f)  "
                "deep(interior)=%lld  (baseline was %lld)\n",
                views, sweep.second, sweep.first, double(sweep.second)/sweep.first, deep, kBaselineSmooth);
    EXPECT_GT(sweep.first, 100000) << "orbit did not cover the interior";
    // Apples-to-apples: total reference-confirmed misses cut >5× vs baseline (the residual is
    // mostly thin-limb tangent-ray disagreement, a handful are deep-interior — see below).
    EXPECT_LT(sweep.second, kBaselineSmooth / 5)
        << "march + occupancy fix did not cut step-overs >5× (was " << kBaselineSmooth << ")";
    // The genuine "hole" metric — interior step-overs (not the limb band) — is near zero.
    EXPECT_LT(deep, 10)
        << "more interior step-overs than expected with the occupancy fix (baseline was ~117)";

    // Same trend for the DISPLACED sphere (octree 1 in the render scene). Its bumpy surface
    // ≠ the analytic bounding sphere we filter with, so the bounding-sphere interior over-
    // includes near-limb rays (the raw total is noisy); the DEEP-interior RATE is the
    // meaningful metric. We assert the deep-interior step-over rate is tiny.
    BakedScene disp = BakeScene(RECIPE_DISPLACED_SPHERE, 2.7f, 0.375f);
    SdfMarchMirror dmir(disp.serialized);
    AnalyticBody dbody = MakeAnalyticBody();
    dbody.radius += 2.7f * kWorldGridSize * kRS / float(kSdfN);
    long long dDeep = 0;
    auto dSweep = OrbitSweep(dmir, dbody, 0.0f, views, &dDeep);
    const double dDeepRate = double(dDeep) / double(dSweep.first);
    std::printf("[FIXPROOF-DISP] single-sentinel march + occupancy fix: holes=%lld / %lld  deep(interior)=%lld (rate %.6f)\n",
                dSweep.second, dSweep.first, dDeep, dDeepRate);
    EXPECT_LT(dDeepRate, 0.001)
        << "displaced-sphere interior step-over rate too high (" << dDeepRate << ")";
}

// ===========================================================================
// DIAGNOSTIC: fillRatio + bodyPixel at the EXACT render-test front view (single-sentinel
// march). Mirrors RenderStoredSdfBodiesNoHoles' scanline metric. NOTE (per task): the
// CPU mirror reads fillRatio ~1.0 here while the GPU reads ~0.988 — the front-view
// residual is a GPU/SPIR-V float effect the exact-arithmetic CPU mirror does NOT
// reproduce, so the lavapipe render test (not this) is the authoritative front-view gate.
// ===========================================================================
TEST(StoredSdfMarchMirror, DiagFrontViewFillRatio) {
    BakedScene scene = BakeScene(RECIPE_SPHERE, 0.0f, 0.0f);
    SdfMarchMirror mirror(scene.serialized);
    const AnalyticBody body = MakeAnalyticBody();
    constexpr uint32_t kW = 512, kH = 512;
    const glm::vec3 focus = body.centre;
    const float R = 0.5f * kWorldGridSize * kRS;
    const glm::vec3 eye = focus + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * (R * 4.0f);
    const Camera cam = MakeCamera(eye, focus, kW, kH);

    uint64_t totalBody = 0, totalSpan = 0; int bodyPx = 0;
    for (uint32_t y = 0; y < kH; ++y) {
        int first=-1,last=-1,cnt=0;
        for (uint32_t x = 0; x < kW; ++x) {
            const float u=(x+0.5f)/kW, v=(y+0.5f)/kH;
            const glm::vec3 rd = getRayDir(cam, u, v);
            const InstRay ir = DeInstance(eye, rd);
            if (mirror.castRay(ir.origin, ir.dir).hit) { if(first<0)first=int(x); last=int(x); ++cnt; }
        }
        if (first>=0){ totalBody+=cnt; totalSpan+=(last-first+1); bodyPx+=cnt; }
    }
    const double fr = totalSpan ? double(totalBody)/double(totalSpan) : 0.0;
    std::printf("[FRONTVIEW] single-sentinel march: bodyPx=%d fillRatio=%.5f\n", bodyPx, fr);
    SUCCEED();
}

// ===========================================================================
// DIAGNOSTIC: ASCII silhouette (downsampled) — '#' mirror-hit, '.' analytic-hit-but-
// mirror-miss (HOLE), ' ' background. Reveals whether misses are scattered brick-
// aligned holes (GPU-faithful) or a gross geometry mismatch (mirror broken).
// ===========================================================================
TEST(StoredSdfMarchMirror, DiagAsciiSilhouette) {
    BakedScene scene = BakeScene(RECIPE_SPHERE, 0.0f, 0.0f);
    SdfMarchMirror mirror(scene.serialized);
    const AnalyticBody body = MakeAnalyticBody();

    constexpr uint32_t kW = 512, kH = 512;
    const glm::vec3 focus = body.centre;
    const float     R     = 0.5f * kWorldGridSize * kRS;
    const glm::vec3 eye   = focus + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * (R * 4.0f);
    const Camera cam = MakeCamera(eye, focus, kW, kH);

    constexpr int AW = 96, AH = 48;
    std::printf("[ASCII] '#'=mirror hit  '.'=hole (analytic hit, mirror miss)  ' '=bg\n");
    int totalMirrorHit = 0, totalAnalytic = 0;
    for (int ay = 0; ay < AH; ++ay) {
        std::string line;
        for (int ax = 0; ax < AW; ++ax) {
            const uint32_t px = static_cast<uint32_t>((ax + 0.5f) / AW * kW);
            const uint32_t py = static_cast<uint32_t>((ay + 0.5f) / AH * kH);
            const float u = (static_cast<float>(px) + 0.5f) / kW;
            const float v = (static_cast<float>(py) + 0.5f) / kH;
            const glm::vec3 rd = getRayDir(cam, u, v);
            const float ta = sphereHitT(body, eye, rd);
            const InstRay ir = DeInstance(eye, rd);
            const SdfMarchMirror::Hit h = mirror.castRay(ir.origin, ir.dir);
            if (h.hit) { line += '#'; ++totalMirrorHit; }
            else if (ta >= 0.0f) { line += '.'; }
            else line += ' ';
            if (ta >= 0.0f) ++totalAnalytic;
        }
        std::printf("%s\n", line.c_str());
    }
    std::printf("[ASCII] sampled mirrorHit=%d analytic=%d\n", totalMirrorHit, totalAnalytic);
    SUCCEED();
}

// ===========================================================================
// DIAGNOSTIC: probe individual rays — the dead-center ray (should hit the front
// face) and a few rays the ASCII showed as HOLES — dumping per-leaf gridEntry,
// brick picked, lookup result, sMax, steps, minD.
// ===========================================================================
TEST(StoredSdfMarchMirror, DiagProbeRays) {
    BakedScene scene = BakeScene(RECIPE_SPHERE, 0.0f, 0.0f);
    SdfMarchMirror mirror(scene.serialized);
    const AnalyticBody body = MakeAnalyticBody();
    std::printf("[PROBE] body centre=(%.3f,%.3f,%.3f) radius=%.4f  bpaSdf=%u brickStride=%u\n",
                body.centre.x, body.centre.y, body.centre.z, body.radius,
                scene.serialized.config.bricksPerAxisSdf, scene.serialized.brickStrideFloats);

    constexpr uint32_t kW = 512, kH = 512;
    const glm::vec3 focus = body.centre;
    const float     R     = 0.5f * kWorldGridSize * kRS;
    const glm::vec3 eye   = focus + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * (R * 4.0f);
    const Camera cam = MakeCamera(eye, focus, kW, kH);

    // Pixels to probe: dead center, plus 4 offsets known to be holes from the ASCII.
    struct PX { int px, py; const char* tag; };
    const PX probes[] = {
        {256, 256, "center"},
        {300, 256, "right-of-center (HOLE)"},
        {256, 300, "below-center (HOLE)"},
        {210, 256, "left-of-center"},
        {256, 210, "above-center"},
    };
    for (const PX& q : probes) {
        const float u = (q.px + 0.5f) / kW, v = (q.py + 0.5f) / kH;
        const glm::vec3 rd = getRayDir(cam, u, v);
        const float ta = sphereHitT(body, eye, rd);
        const InstRay ir = DeInstance(eye, rd);
        std::vector<SdfMarchMirror::LeafProbe> ps;
        mirror.probeRay(ir.origin, ir.dir, ps);
        std::printf("\n[PROBE %s] px=(%d,%d) analyticT=%.4f  leavesVisited=%zu\n",
                    q.tag, q.px, q.py, ta, ps.size());
        for (size_t i = 0; i < ps.size(); ++i) {
            const auto& p = ps[i];
            char lk[16];
            if (p.brickIdxLookup == 0xFFFFFFFFu) std::snprintf(lk, sizeof(lk), "UNALLOC");
            else std::snprintf(lk, sizeof(lk), "%u", p.brickIdxLookup);
            std::printf("   leaf#%zu gridEntry=(%.3f,%.3f,%.3f) dirN=(%.3f,%.3f,%.3f) "
                        "brick=(%d,%d,%d) lookupIdx=%s sMax=%.4f steps=%d minD=%.4f hit=%d\n",
                        i, p.gridEntry.x, p.gridEntry.y, p.gridEntry.z,
                        p.gridDirN.x, p.gridDirN.y, p.gridDirN.z,
                        p.brickPicked.x, p.brickPicked.y, p.brickPicked.z,
                        lk, p.sMax, p.steps, p.minD, (int)p.hit);
        }
    }
    SUCCEED();
}

// ===========================================================================
// GPU-VS-CPU DIVERGENCE PROBE — feed the EXACT pixels the GPU readback flagged
// as holes (smooth body, octree 0, 512^2, front view) into the CPU mirror.
// The GPU readback (test_body_instance_raymarch_render DEBUG_SdfHoleReadback)
// reported these as brick-exit-overrun misses with minD≈2.7-3.4 voxels (the surface
// sits in the NEIGHBOUR brick the ESVO didn't reach). If the CPU mirror HITS the
// same pixels, the divergence is GPU-execution (ESVO leaf enumeration under
// 32-bit float) — NOT data/serialize. This pins WHERE the divergence lives.
// ===========================================================================
TEST(StoredSdfMarchMirror, GpuHolePixelsOnCpu) {
    BakedScene scene = BakeScene(RECIPE_SPHERE, 0.0f, 0.0f);
    SdfMarchMirror mirror(scene.serialized);
    const AnalyticBody body = MakeAnalyticBody();

    // EXACT render-test framing (RenderStoredSdfBodiesNoHoles / DEBUG_SdfHoleReadback).
    constexpr uint32_t kW = 512, kH = 512;
    const glm::vec3 focus = body.centre;
    const float     R     = 0.5f * kWorldGridSize * kRS;
    const glm::vec3 eye   = focus + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * (R * 4.0f);
    const Camera cam = MakeCamera(eye, focus, kW, kH);

    // The 13 real GPU hole pixels (minD≈2.7-3.4 family) from the readback.
    const std::pair<int,int> gpuHoles[] = {
        {130,218},{131,218},{132,218},{128,229},{129,229},{130,229},
        {275,387},{276,387},{277,387},{278,387}
    };
    int cpuHit = 0, cpuMiss = 0;
    for (auto [px, py] : gpuHoles) {
        const float u=(px+0.5f)/kW, v=(py+0.5f)/kH;
        const glm::vec3 rd = getRayDir(cam, u, v);
        const InstRay ir = DeInstance(eye, rd);
        const SdfMarchMirror::Hit h = mirror.castRay(ir.origin, ir.dir);
        const SdfMarchMirror::RefResult ref = mirror.referenceMarch(ir.origin, ir.dir);
        std::printf("[GPUHOLE] px=(%d,%d) cpuHit=%d cpuT=%.4f | refCrossed=%d refMinD=%.4f crossBrick=(%d,%d,%d)\n",
                    px, py, (int)h.hit, h.t, (int)ref.crossed, ref.minD,
                    ref.crossBrick.x, ref.crossBrick.y, ref.crossBrick.z);
        if (h.hit) ++cpuHit; else ++cpuMiss;
    }
    std::printf("[GPUHOLE] CPU mirror over the GPU's hole pixels: hit=%d miss=%d\n", cpuHit, cpuMiss);

    // Per-leaf dump for the first real hole pixel — what brick does the march bound
    // to, is it allocated, and what is the per-leaf minD?
    {
        const int px = 130, py = 218;
        const float u=(px+0.5f)/kW, v=(py+0.5f)/kH;
        const glm::vec3 rd = getRayDir(cam, u, v);
        const InstRay ir = DeInstance(eye, rd);
        std::printf("\n[GPUHOLE detail px=(130,218)] per-leaf marchBrickSdf probe:\n");
        std::vector<SdfMarchMirror::LeafProbe> ps;
        mirror.probeRay(ir.origin, ir.dir, ps);
        for (size_t i = 0; i < ps.size(); ++i) {
            const auto& p = ps[i];
            char lk[16];
            if (p.brickIdxLookup==0xFFFFFFFFu) std::snprintf(lk,sizeof(lk),"UNALLOC");
            else std::snprintf(lk,sizeof(lk),"%u",p.brickIdxLookup);
            std::printf("   leaf#%zu brick=(%d,%d,%d)[%s] gridEntry=(%.3f,%.3f,%.3f) sMax=%.3f steps=%d minD=%.4f hit=%d\n",
                        i,p.brickPicked.x,p.brickPicked.y,p.brickPicked.z,lk,
                        p.gridEntry.x,p.gridEntry.y,p.gridEntry.z,p.sMax,p.steps,p.minD,(int)p.hit);
        }
    }
    // If the CPU mirror HITS where the GPU holed, the bug is GPU float execution of
    // the ESVO/march, not the data. (If the CPU also misses, the data/algorithm is
    // implicated and the mirror should reproduce the GPU fillRatio.)
    SUCCEED();
}

// ===========================================================================
// ROOT CAUSE #1 — the floor-derived brick vs the ESVO-leaf brick.
//
// marchBrickSdf bounds its slab + lookup to floor((gridEntry+dirN*1e-3)/8),
// NOT to the brick the ESVO traversal actually descended into. When the leaf
// entry t lands ON a brick boundary (gridEntry coord ≈ k*8), the floor (after a
// tiny ±dirN nudge) can resolve to the NEIGHBOUR brick the ray is LEAVING. The
// march then tests the WRONG brick's [bMin,bMax] slab → wrong sMax → it steps
// out of (or never enters) the real leaf brick → a MISS at that ray.
//
// This DISAGREEMENT is the seed; GPU 32-bit-float rounding through the ESVO
// pipeline lands MANY more entries within epsilon of a brick face than exact CPU
// arithmetic, so the seed becomes a consistent brick-aligned hole pattern on GPU
// while exact CPU sees only a handful.
//
// PROOF: sweep with GPU-faithful t jitter and compare the CURRENT march
// (floor brick) against the PROPOSED fix (ESVO leaf brick, m_useEsvoBrick=true).
// If binding to the ESVO brick collapses the jitter-induced misses, the floor
// selection is the root cause.
// ===========================================================================
TEST(StoredSdfMarchMirror, RootCause_EsvoBrickVsFloorBrick) {
    BakedScene scene = BakeScene(RECIPE_SPHERE, 0.0f, 0.0f);
    SdfMarchMirror mirror(scene.serialized);
    const AnalyticBody body = MakeAnalyticBody();

    const float jitters[] = { 0.0f, 1e-5f, 1e-4f, 5e-4f, 1e-3f };
    std::printf("[ROOTCAUSE] floor-brick vs ESVO-leaf-brick  (miss = analytic-interior ray the march drops)\n");
    long long floorMissAtMaxJitter = 0, esvoMissAtMaxJitter = 0;
    long long disagreeAtMaxJitter = 0;
    for (float j : jitters) {
        // CURRENT shader behaviour: march bounds to floor(gridEntry/8).
        mirror.m_useEsvoBrick = false;
        const SweepStats fs = SweepHoles(mirror, body, j);
        // PROPOSED fix: march bounds to the ESVO leaf brick the traversal chose.
        mirror.m_useEsvoBrick = true;
        const SweepStats es = SweepHoles(mirror, body, j);
        std::printf("  jitter=%.0e  floorBrick miss=%d   esvoBrick miss=%d   "
                    "brickDisagree=%lld/%lld\n",
                    j, fs.mirrorMiss, es.mirrorMiss, fs.brickDisagree, fs.leafHits);
        if (j == jitters[sizeof(jitters)/sizeof(jitters[0]) - 1]) {
            floorMissAtMaxJitter = fs.mirrorMiss;
            esvoMissAtMaxJitter  = es.mirrorMiss;
            disagreeAtMaxJitter  = fs.brickDisagree;
        }
    }
    // The disagreement is real and grows with precision loss.
    EXPECT_GT(disagreeAtMaxJitter, 0)
        << "expected floor-brick to disagree with ESVO-leaf-brick under jitter";
    // Binding the march to the ESVO leaf brick removes the boundary-straddle misses
    // the floor selection introduces (no false brick, correct slab/sMax).
    EXPECT_LE(esvoMissAtMaxJitter, floorMissAtMaxJitter)
        << "ESVO-brick march must not be worse than the floor-brick march";
}

// ===========================================================================
// ROOT CAUSE #2 — the DISPLACED body is octree 1 of the CONCATENATED demo bake
// (poolBrickBase>0, nodeArrayBase>0, the SECOND lookup sub-table). The prior
// mirror only ever tested octree 0 of a SINGLE-octree SerializeSdf — so the
// multi-octree addressing the GPU actually consumes was NEVER mirrored.
//
// This test drives the mirror over octree 1 of the real ConcatenateSdf output
// and reports holes under GPU-faithful jitter for floor-brick vs ESVO-brick.
// It also asserts the multi-octree addressing is self-consistent (octree 1
// renders a body at all — i.e. poolBrickBase/lookup-slice are correct), which
// rules a data/addressing corruption IN or OUT for the displaced body.
// ===========================================================================
TEST(StoredSdfMarchMirror, RootCause_DisplacedOctree1MultiOctree) {
    ConcatScene demo = BakeConcatDemo();
    ASSERT_EQ(demo.cat.count, 3u);

    // Octree 1 must be offset in BOTH the node buffer and the pool.
    EXPECT_GT(demo.cat.configs[1].nodeArrayBase, 0)
        << "octree 1 nodeArrayBase should be > 0 (concatenated after octree 0)";
    EXPECT_GT(demo.cat.configs[1].poolBrickBase, 0u)
        << "octree 1 poolBrickBase should be > 0 (pool appended after octree 0)";

    SerializedOctree view1 = ViewConcatOctree(demo, 1u);
    SdfMarchMirror mirror(view1);

    // Displaced body: its bumpy iso ≈ analytic sphere + amp; frame on that.
    AnalyticBody body = MakeAnalyticBody();
    body.radius += 2.7f * kWorldGridSize * kRS / float(kSdfN);

    // First: does octree 1 render AT ALL through the multi-octree addressing?
    // (A solid front-view body proves poolBrickBase/lookup-slice are correct.)
    {
        constexpr uint32_t kW = 256, kH = 256;
        const glm::vec3 focus = body.centre;
        const float R = 0.5f * kWorldGridSize * kRS;
        const glm::vec3 eye = focus + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * (R * 4.0f);
        const Camera cam = MakeCamera(eye, focus, kW, kH);
        int bodyPx = 0;
        for (uint32_t y = 0; y < kH; ++y)
          for (uint32_t x = 0; x < kW; ++x) {
            const glm::vec3 rd = getRayDir(cam, (x+0.5f)/kW, (y+0.5f)/kH);
            const InstRay ir = DeInstance(eye, rd);
            if (mirror.castRay(ir.origin, ir.dir).hit) ++bodyPx;
          }
        std::printf("[OCT1] displaced octree-1 (multi-octree addressing): bodyPx=%d / %u\n",
                    bodyPx, kW * kH);
        EXPECT_GT(bodyPx, 5000)
            << "octree 1 barely rendered via concatenated poolBrickBase/lookup — "
               "multi-octree addressing is broken (data path bug)";
    }

    // Then: floor-brick vs ESVO-brick miss counts under jitter on octree 1.
    const float jitters[] = { 0.0f, 1e-4f, 1e-3f };
    std::printf("[OCT1] floor-brick vs ESVO-brick on octree 1 (displaced):\n");
    for (float j : jitters) {
        mirror.m_useEsvoBrick = false;
        const auto fres = OrbitSweep(mirror, body, j, 8);
        mirror.m_useEsvoBrick = true;
        const auto eres = OrbitSweep(mirror, body, j, 8);
        std::printf("  jitter=%.0e  floorBrick holes=%lld/%lld   esvoBrick holes=%lld/%lld\n",
                    j, fres.second, fres.first, eres.second, eres.first);
    }
    SUCCEED();
}

// ===========================================================================
// ROOT-CAUSE DATA PROBE (report-only): resolve the "sentinel contamination is
// IMPOSSIBLE" contradiction with numbers.
//
// Claim under test: a surface-hit stencil (sampleSdfTrilinear at hit, and the ±0.5
// gradient taps) should NEVER read an unallocated brick, because the bake marks every
// band brick AND dilates the active set by a 26-connected 1-brick margin AND fully
// populates every active brick. So every tap (reach ≤ ~2 voxels from a band brick)
// should land in an allocated brick.
//
// What we measure: for a DENSE set of true sphere-surface points, enumerate EVERY
// trilinear/gradient tap corner. A tap is "contaminated" if sampleSdfVoxel(corner)
// returns the sentinel (v > SENTINEL_D=100 — exactly marchBrickSdf's test). For each
// contaminated tap record (1) hitBrick = floor(hitGridPos/8) and whether it is a BAND
// brick / an ACTIVE brick (recomputed IDENTICALLY to SdfBake.h); (2) the corner grid
// pos and tapBrick = floor(corner/8); (3) chebyshevBrickDist(hitBrick, tapBrick);
// (4) is tapBrick in-grid? in the bake's activeBrick[] set? does the lookup return a
// real brick index or the single unallocated sentinel (0xFFFFFFFF)?
//
// Decision: dist==1 & tapBrick ACTIVE but lookup UNALLOC  ⇒ serialize/lookup bug
//           dist==1 & tapBrick NOT active                 ⇒ dilation bug
//           dist>=2                                        ⇒ reach exceeds the margin
// ===========================================================================
TEST(StoredSdfMarchMirror, RootCause_SentinelContaminationOrigin) {
    // SAME bake the render uses: octree 0 of the concatenated demo (smooth sphere).
    ConcatScene demo = BakeConcatDemo();
    ASSERT_EQ(demo.cat.count, 3u);
    SerializedOctree view0 = ViewConcatOctree(demo, 0u);
    SdfMarchMirror mirror(view0);

    const int   bpa       = mirror.bpaSdf();
    const int   n         = bpa * 8;                 // grid side in voxels
    const int   brickSide = 8;
    const float band      = kSdfBand;                // 2.5
    const glm::vec3 center(kSdfCenter);              // (32,32,32)
    RecipeParams rp{}; rp.radius = kSdfRadius; rp.displaceAmp = 0.0f; rp.displaceFreq = 0.0f;
    ASSERT_GT(bpa, 0);

    // ---- Recompute the bake's band/active sets EXACTLY as SdfBake.h (lines 79-120) ----
    const int bricksPerAxis = (n + brickSide - 1) / brickSide;   // == bpa
    auto brickIndex = [&](int bx, int by, int bz) {
        return (bz * bricksPerAxis + by) * bricksPerAxis + bx;
    };
    const size_t numBricks = static_cast<size_t>(bricksPerAxis) * bricksPerAxis * bricksPerAxis;
    std::vector<uint8_t> bandBrick(numBricks, 0u), activeBrick(numBricks, 0u);
    for (int z = 0; z < n; ++z)
      for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float sd = evalSdf(RECIPE_SPHERE,
                glm::vec3(float(x), float(y), float(z)), center, rp);
            if (std::abs(sd) <= band)
                bandBrick[brickIndex(x/brickSide, y/brickSide, z/brickSide)] = 1u;
        }
    for (int bz = 0; bz < bricksPerAxis; ++bz)
      for (int by = 0; by < bricksPerAxis; ++by)
        for (int bx = 0; bx < bricksPerAxis; ++bx) {
            bool touches = false;
            for (int dz=-1; dz<=1 && !touches; ++dz)
              for (int dy=-1; dy<=1 && !touches; ++dy)
                for (int dx=-1; dx<=1 && !touches; ++dx) {
                    const int nx=bx+dx, ny=by+dy, nz=bz+dz;
                    if (nx<0||ny<0||nz<0||nx>=bricksPerAxis||ny>=bricksPerAxis||nz>=bricksPerAxis) continue;
                    if (bandBrick[brickIndex(nx,ny,nz)]) touches = true;
                }
            if (touches) activeBrick[brickIndex(bx,by,bz)] = 1u;
        }
    auto inGrid    = [&](const glm::ivec3& b){ return b.x>=0&&b.y>=0&&b.z>=0&&b.x<bpa&&b.y<bpa&&b.z<bpa; };
    auto isActive  = [&](const glm::ivec3& b){ return inGrid(b) && activeBrick[brickIndex(b.x,b.y,b.z)]!=0u; };
    auto isBand    = [&](const glm::ivec3& b){ return inGrid(b) && bandBrick  [brickIndex(b.x,b.y,b.z)]!=0u; };
    auto cheby     = [](const glm::ivec3& a, const glm::ivec3& b){
        return std::max(std::max(std::abs(a.x-b.x), std::abs(a.y-b.y)), std::abs(a.z-b.z)); };

    // Sanity counts.
    int nBand=0,nActive=0; for (size_t i=0;i<numBricks;++i){ nBand+=bandBrick[i]; nActive+=activeBrick[i]; }
    std::printf("[RC] bpa=%d n=%d  bandBricks=%d activeBricks=%d (numBricks=%zu)\n",
                bpa, n, nBand, nActive, numBricks);

    // How many ACTIVE bricks does the lookup actually represent vs drop?
    int activeButUnalloc=0, activeAlloc=0;
    for (int bz=0;bz<bpa;++bz) for (int by=0;by<bpa;++by) for (int bx=0;bx<bpa;++bx) {
        const glm::ivec3 b(bx,by,bz);
        if (!isActive(b)) continue;
        const uint32_t lk = mirror.lookupRaw(b);
        if (isBrickUnallocated(lk)) ++activeButUnalloc;
        else                        ++activeAlloc;
    }
    std::printf("[RC] ACTIVE bricks: lookup-alloc=%d  lookup-UNALLOC=%d\n",
                activeAlloc, activeButUnalloc);

    // DIRECT CAUSE CHECK: querySolidVoxels() keeps a voxel only if Density>0. So a brick is
    // dropped from the octree (→ lookup UNALLOC) iff NONE of its 512 voxels has sd>0. Verify
    // every active-but-unalloc brick has exactly zero positive-SDF voxels (all interior/zero).
    {
        int unallocBricks=0, unallocBricksWithPositive=0, maxPosInUnalloc=0;
        for (int bz=0;bz<bpa;++bz) for (int by=0;by<bpa;++by) for (int bx=0;bx<bpa;++bx) {
            const glm::ivec3 b(bx,by,bz);
            if (!isActive(b)) continue;
            const uint32_t lk = mirror.lookupRaw(b);
            if (!isBrickUnallocated(lk)) continue;
            ++unallocBricks;
            int pos=0;
            for (int vz=0;vz<8;++vz) for (int vy=0;vy<8;++vy) for (int vx=0;vx<8;++vx) {
                const glm::vec3 p(float(bx*8+vx), float(by*8+vy), float(bz*8+vz));
                if (evalSdf(RECIPE_SPHERE, p, center, rp) > 0.0f) ++pos;
            }
            if (pos>0) { ++unallocBricksWithPositive; maxPosInUnalloc=std::max(maxPosInUnalloc,pos); }
        }
        std::printf("[RC] active-but-UNALLOC bricks=%d  of those WITH any positive-SDF voxel=%d "
                    "(max positive voxels in any=%d)\n",
                    unallocBricks, unallocBricksWithPositive, maxPosInUnalloc);
        std::printf("[RC]   => if WITH-positive==0, the Density>0 filter in querySolidVoxels() "
                    "dropped every fully-interior active brick (root cause).\n");
    }

    // ---- Dense sphere-surface points; enumerate every contaminated tap ----
    const float SENTINEL_D = 100.0f;
    const float h = 0.5f;   // sdfGradientStored tap offset (matches the shader)
    auto contaminated = [&](const glm::ivec3& c){ return mirror.sampleSdfVoxelPub(c) > SENTINEL_D; };

    // Histogram over chebyshev brick distance (0..>=3) and the cross-tab.
    long long histDist[8] = {0};
    long long xt_d1_active_unalloc = 0;   // dist==1, tapBrick ACTIVE, lookup UNALLOC  → serialize/lookup bug
    long long xt_d1_inactive       = 0;   // dist==1, tapBrick NOT active              → dilation bug
    long long xt_d1_active_alloc   = 0;   // dist==1, tapBrick ACTIVE & allocated (shouldn't be contaminated)
    long long xt_d0_any            = 0;   // dist==0 (same brick) contaminated (shouldn't happen)
    long long xt_dge2              = 0;   // dist>=2 (reach beyond margin)
    long long tapUnalloc=0;               // contaminated taps whose lookup is the unalloc sentinel
    long long contamTaps=0, totalTaps=0, surfacePts=0;
    // For dist==0 contaminated taps: is the HIT brick itself allocated in the lookup?
    long long d0_hitBrickUnalloc=0, d0_hitBrickAlloc=0;
    // Of contaminated taps: is hitBrick a band brick? (it must be, for a real surface hit)
    long long hitBrickBand=0, hitBrickActiveNotBand=0, hitBrickInactive=0;

    // March the true sphere surface on a fine angular grid (theta,phi), at radius kSdfRadius.
    constexpr float kPi = 3.14159265358979323846f;
    const int kTheta = 360, kPhi = 720;
    for (int it=0; it<kTheta; ++it) {
        const float theta = kPi * (it + 0.5f) / kTheta;                   // [0,pi]
        for (int ip=0; ip<kPhi; ++ip) {
            const float phi = 2.0f*kPi * ip / kPhi;                       // [0,2pi)
            const glm::vec3 dir(std::sin(theta)*std::cos(phi),
                                std::sin(theta)*std::sin(phi),
                                std::cos(theta));
            const glm::vec3 hitGrid = center + dir * kSdfRadius;          // on the iso (grid voxel space)
            if (hitGrid.x<0||hitGrid.y<0||hitGrid.z<0||hitGrid.x>=n||hitGrid.y>=n||hitGrid.z>=n) continue;
            ++surfacePts;
            const glm::ivec3 hitBrick = glm::ivec3(glm::floor(hitGrid / 8.0f));

            // Enumerate the exact tap set marchBrickSdf+sdfGradientStored use at a hit:
            //   the hit-point trilinear, and the 6 gradient taps (hit ± h along each axis).
            glm::vec3 taps[7];
            taps[0] = hitGrid;
            int nt = 1;
            for (int ax=0; ax<3; ++ax) {
                glm::vec3 e(0.0f); e[ax] = h;
                taps[nt++] = hitGrid + e;
                taps[nt++] = hitGrid - e;
            }
            bool countedHitBrickThisPt = false;
            for (int ti=0; ti<nt; ++ti) {
                const glm::vec3 q = taps[ti];
                const glm::ivec3 base = glm::ivec3(glm::floor(q));
                for (int corner=0; corner<8; ++corner) {
                    const glm::ivec3 c = base + glm::ivec3(corner&1,(corner>>1)&1,(corner>>2)&1);
                    ++totalTaps;
                    if (!contaminated(c)) continue;
                    ++contamTaps;
                    const glm::ivec3 tapBrick = glm::ivec3(c.x/8, c.y/8, c.z/8);  // floor (c>=0)
                    const int d = cheby(hitBrick, tapBrick);
                    histDist[std::min(d,7)]++;
                    const uint32_t lk = mirror.lookupRaw(tapBrick);
                    const bool tapUnallocated = isBrickUnallocated(lk);
                    if (tapUnallocated) ++tapUnalloc;
                    const bool tapActive = isActive(tapBrick);
                    if (d==0) {
                        ++xt_d0_any;
                        if (isBrickUnallocated(mirror.lookupRaw(hitBrick))) ++d0_hitBrickUnalloc;
                        else ++d0_hitBrickAlloc;
                    }
                    else if (d==1) {
                        if (tapActive && tapUnallocated) ++xt_d1_active_unalloc;
                        else if (!tapActive)             ++xt_d1_inactive;
                        else                              ++xt_d1_active_alloc;
                    } else ++xt_dge2;
                    if (!countedHitBrickThisPt) {
                        countedHitBrickThisPt = true;
                        if (isBand(hitBrick)) ++hitBrickBand;
                        else if (isActive(hitBrick)) ++hitBrickActiveNotBand;
                        else ++hitBrickInactive;
                    }
                }
            }
        }
    }

    std::printf("\n[RC] surface points sampled=%lld  taps=%lld  contaminated taps=%lld (%.4f%%)\n",
                surfacePts, totalTaps, contamTaps, 100.0*double(contamTaps)/double(std::max<long long>(1,totalTaps)));
    std::printf("[RC] chebyshev(hitBrick,tapBrick) histogram over CONTAMINATED taps:\n");
    for (int d=0; d<=4; ++d) std::printf("     dist==%d : %lld\n", d, histDist[d]);
    std::printf("     dist>=5 : %lld\n", histDist[5]+histDist[6]+histDist[7]);
    std::printf("[RC] contaminated taps with unallocated-sentinel lookup: %lld\n", tapUnalloc);
    std::printf("[RC] cross-tab (CONTAMINATED taps):\n");
    std::printf("     dist==0 (same brick)                         : %lld  (hitBrick itself UNALLOC=%lld alloc=%lld)\n",
                xt_d0_any, d0_hitBrickUnalloc, d0_hitBrickAlloc);
    std::printf("     dist==1 & tapBrick ACTIVE & lookup UNALLOC    : %lld   <- serialize/lookup bug\n", xt_d1_active_unalloc);
    std::printf("     dist==1 & tapBrick NOT active                 : %lld   <- dilation bug\n", xt_d1_inactive);
    std::printf("     dist==1 & tapBrick ACTIVE & allocated         : %lld\n", xt_d1_active_alloc);
    std::printf("     dist>=2 (reach beyond 1-brick margin)         : %lld   <- reach model wrong\n", xt_dge2);
    std::printf("[RC] of contaminated-tap hitBricks: band=%lld activeNotBand=%lld inactive=%lld\n",
                hitBrickBand, hitBrickActiveNotBand, hitBrickInactive);

    SUCCEED();
}
