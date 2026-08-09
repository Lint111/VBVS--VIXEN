#pragma once

#include "IExportable.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <sstream>
#include <iomanip>

namespace Vixen::RenderGraph::Debug {

/**
 * @brief Exit codes for ray traversal (must match shader constants)
 */
enum class DebugExitCode : uint32_t {
    None = 0,           // DEBUG_EXIT_NONE - traversal ongoing
    Hit = 1,            // DEBUG_EXIT_HIT - found solid voxel
    NoHit = 2,          // DEBUG_EXIT_NO_HIT - finished without hit
    StackExit = 3,      // DEBUG_EXIT_STACK - POP exited octree
    InvalidSpan = 4     // DEBUG_EXIT_INVALID_SPAN - t_min > t_max
};

/**
 * @brief C++ struct matching shader DebugRaySample
 *
 * IMPORTANT: This struct must match the GLSL layout EXACTLY for GPU readback.
 * The shader uses std430 layout, so we need proper alignment.
 * This struct MUST NOT inherit from classes with virtual methods to avoid vtable pointer.
 *
 * Shader definition (VoxelRayMarch.comp):
 * struct DebugRaySample {
 *     uvec2 pixel;          // 8 bytes
 *     uint octantMask;      // 4 bytes
 *     uint hitFlag;         // 4 bytes
 *     uint exitCode;        // 4 bytes
 *     uint lastStepMask;    // 4 bytes
 *     uint iterationCount;  // 4 bytes
 *     int scale;            // 4 bytes
 *     uint stateIdx;        // 4 bytes
 *     float tMin;           // 4 bytes
 *     float tMax;           // 4 bytes
 *     float scaleExp2;      // 4 bytes
 *     float reserved0;      // 4 bytes (padding)
 *     vec3 posMirrored;     // 12 bytes
 *     float reserved1;      // 4 bytes (padding to vec4)
 *     vec3 localNorm;       // 12 bytes
 *     float reserved2;      // 4 bytes (padding to vec4)
 *     vec3 rayDir;          // 12 bytes
 *     float reserved3;      // 4 bytes (padding to vec4)
 * };
 * Total: 96 bytes
 *
 * NOTE: Export methods are non-virtual to keep struct POD-compatible.
 */
struct alignas(16) DebugRaySample {
    // Pixel coordinates (uvec2) - offset 0
    uint32_t pixelX;
    uint32_t pixelY;

    // Traversal state - offset 8
    uint32_t octantMask;
    uint32_t hitFlag;
    uint32_t exitCode;
    uint32_t lastStepMask;
    uint32_t iterationCount;
    int32_t scale;
    uint32_t stateIdx;

    // T-span values - offset 36
    float tMin;
    float tMax;
    float scaleExp2;
    float reserved0;

    // std430 padding to align vec3 to 16-byte boundary - offset 52
    float _padding1;
    float _padding2;
    float _padding3;

    // Position in mirrored ESVO space [1,2]³ - offset 64 (16-byte aligned)
    float posMirroredX;
    float posMirroredY;
    float posMirroredZ;
    float reserved1;

    // Position in local normalized space [0,1]³ - offset 80 (16-byte aligned)
    float localNormX;
    float localNormY;
    float localNormZ;
    float reserved2;

    // Ray direction (world space) - offset 96 (16-byte aligned)
    float rayDirX;
    float rayDirY;
    float rayDirZ;
    float reserved3;

    // =========================================================================
    // Export methods (non-virtual to keep struct POD)
    // =========================================================================

    std::string ToString() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);

        ss << "Pixel(" << pixelX << "," << pixelY << ") ";
        ss << "octant=" << octantMask << " ";
        ss << "hit=" << hitFlag << " ";
        ss << "exit=" << ExitCodeToString(static_cast<DebugExitCode>(exitCode)) << " ";
        ss << "iter=" << iterationCount << " ";
        ss << "scale=" << scale << " ";
        ss << "idx=" << stateIdx << " ";
        ss << "t=[" << tMin << "," << tMax << "] ";
        ss << "scaleExp2=" << scaleExp2 << " ";
        ss << "posMir=(" << posMirroredX << "," << posMirroredY << "," << posMirroredZ << ") ";
        ss << "localNorm=(" << localNormX << "," << localNormY << "," << localNormZ << ") ";
        ss << "rayDir=(" << rayDirX << "," << rayDirY << "," << rayDirZ << ")";

        return ss.str();
    }

    std::string ToCSV() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(6);

        ss << pixelX << "," << pixelY << ",";
        ss << octantMask << "," << hitFlag << "," << exitCode << ",";
        ss << lastStepMask << "," << iterationCount << ",";
        ss << scale << "," << stateIdx << ",";
        ss << tMin << "," << tMax << "," << scaleExp2 << ",";
        ss << posMirroredX << "," << posMirroredY << "," << posMirroredZ << ",";
        ss << localNormX << "," << localNormY << "," << localNormZ << ",";
        ss << rayDirX << "," << rayDirY << "," << rayDirZ;

        return ss.str();
    }

    std::string GetCSVHeader() const {
        return "pixelX,pixelY,octantMask,hitFlag,exitCode,lastStepMask,iterationCount,"
               "scale,stateIdx,tMin,tMax,scaleExp2,"
               "posMirroredX,posMirroredY,posMirroredZ,"
               "localNormX,localNormY,localNormZ,"
               "rayDirX,rayDirY,rayDirZ";
    }

    std::string ToJSON() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(6);

        ss << "{";
        ss << "\"pixel\":[" << pixelX << "," << pixelY << "],";
        ss << "\"octantMask\":" << octantMask << ",";
        ss << "\"hitFlag\":" << hitFlag << ",";
        ss << "\"exitCode\":" << exitCode << ",";
        ss << "\"exitCodeName\":\"" << ExitCodeToString(static_cast<DebugExitCode>(exitCode)) << "\",";
        ss << "\"lastStepMask\":" << lastStepMask << ",";
        ss << "\"iterationCount\":" << iterationCount << ",";
        ss << "\"scale\":" << scale << ",";
        ss << "\"stateIdx\":" << stateIdx << ",";
        ss << "\"tMin\":" << tMin << ",";
        ss << "\"tMax\":" << tMax << ",";
        ss << "\"scaleExp2\":" << scaleExp2 << ",";
        ss << "\"posMirrored\":[" << posMirroredX << "," << posMirroredY << "," << posMirroredZ << "],";
        ss << "\"localNorm\":[" << localNormX << "," << localNormY << "," << localNormZ << "],";
        ss << "\"rayDir\":[" << rayDirX << "," << rayDirY << "," << rayDirZ << "]";
        ss << "}";

        return ss.str();
    }

    // =========================================================================
    // Helper methods
    // =========================================================================

    glm::uvec2 GetPixel() const { return glm::uvec2(pixelX, pixelY); }
    glm::vec3 GetPosMirrored() const { return glm::vec3(posMirroredX, posMirroredY, posMirroredZ); }
    glm::vec3 GetLocalNorm() const { return glm::vec3(localNormX, localNormY, localNormZ); }
    glm::vec3 GetRayDir() const { return glm::vec3(rayDirX, rayDirY, rayDirZ); }

    DebugExitCode GetExitCode() const { return static_cast<DebugExitCode>(exitCode); }
    bool IsHit() const { return hitFlag != 0; }

    static const char* ExitCodeToString(DebugExitCode code) {
        switch (code) {
            case DebugExitCode::None: return "NONE";
            case DebugExitCode::Hit: return "HIT";
            case DebugExitCode::NoHit: return "NO_HIT";
            case DebugExitCode::StackExit: return "STACK_EXIT";
            case DebugExitCode::InvalidSpan: return "INVALID_SPAN";
            default: return "UNKNOWN";
        }
    }

    // =========================================================================
    // Filtering helpers for analysis
    // =========================================================================

    /**
     * @brief Check if this ray has a specific octant mask
     */
    bool HasOctantMask(uint32_t mask) const { return octantMask == mask; }

    /**
     * @brief Check if ray direction is positive on an axis
     * bit 0 = X positive, bit 1 = Y positive, bit 2 = Z positive
     */
    uint32_t GetDirectionBits() const {
        uint32_t bits = 0;
        if (rayDirX > 0) bits |= 1;
        if (rayDirY > 0) bits |= 2;
        if (rayDirZ > 0) bits |= 4;
        return bits;
    }

    /**
     * @brief Check if octant_mask matches expected for ray direction
     * In ESVO: octant_mask bit = 0 means axis IS mirrored (positive ray direction)
     *          octant_mask bit = 1 means axis NOT mirrored (negative ray direction)
     */
    bool IsOctantMaskCorrect() const {
        // For each axis: if rayDir > 0, bit should be 0 (mirrored)
        //                if rayDir < 0, bit should be 1 (not mirrored)
        uint32_t expectedMask = 7; // Start with all bits set (negative dirs)
        if (rayDirX > 0) expectedMask &= ~1u;
        if (rayDirY > 0) expectedMask &= ~2u;
        if (rayDirZ > 0) expectedMask &= ~4u;
        return octantMask == expectedMask;
    }
};

// Verify struct size matches shader std430 layout
// std430 requires vec3 to align to 16 bytes inside structs
// Layout: 52 scalars + 12 padding + 48 (3x vec3+pad) = 112 bytes
static_assert(sizeof(DebugRaySample) == 112, "DebugRaySample size must match shader std430 layout (112 bytes)");

/**
 * @brief Header for debug capture buffer (matches shader std430 layout)
 *
 * In std430, the DebugRaySample array needs 16-byte alignment (due to vec3),
 * so there's padding between the header and the first sample.
 */
struct alignas(16) DebugCaptureHeader {
    uint32_t writeIndex;    // Current write position (atomic)
    uint32_t capacity;      // Maximum number of samples
    uint32_t _padding[2];   // Padding to align samples array to 16 bytes
};

static_assert(sizeof(DebugCaptureHeader) == 16, "DebugCaptureHeader must be 16 bytes (aligned for samples array)");

// ============================================================================
// PER-RAY TRAVERSAL TRACE (Full path debugging)
// ============================================================================

/**
 * @brief Step types for ray traversal trace (must match shader constants)
 */
enum class TraceStepType : uint32_t {
    Push = 0,        // Descended into child octant
    Advance = 1,     // Advanced to sibling octant
    Pop = 2,         // Popped back to parent
    BrickEnter = 3,  // Entered a brick volume
    BrickDDA = 4,    // DDA step within brick
    BrickExit = 5,   // Exited brick without hit
    Hit = 6,         // Found solid voxel
    Miss = 7,        // Exited octree without hit
    InvalidChildIdx = 8, // Invalid child index in leaf hit
    InvalidBrickIdx = 9, // Invalid brick index in leaf hit
    CallingDDA = 10      // About to call DDA
};

inline const char* TraceStepTypeToString(TraceStepType type) {
    switch (type) {
        case TraceStepType::Push: return "PUSH";
        case TraceStepType::Advance: return "ADVANCE";
        case TraceStepType::Pop: return "POP";
        case TraceStepType::BrickEnter: return "BRICK_ENTER";
        case TraceStepType::BrickDDA: return "BRICK_DDA";
        case TraceStepType::BrickExit: return "BRICK_EXIT";
        case TraceStepType::Hit: return "HIT";
        case TraceStepType::Miss: return "MISS";
        case TraceStepType::InvalidChildIdx: return "INVALID_CHILD_IDX";
        case TraceStepType::InvalidBrickIdx: return "INVALID_BRICK_IDX";
        case TraceStepType::CallingDDA: return "CALLING_DDA";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Per-step trace record (must match shader std430 layout - 48 bytes)
 */
struct alignas(4) TraceStep {
    uint32_t stepType;      // TraceStepType enum
    uint32_t nodeIndex;     // Current octree node index
    int32_t scale;          // Current ESVO scale
    uint32_t octantMask;    // Current octant mask (0-7)
    float posX, posY, posZ; // Position at this step (in [1,2]³ space)
    float tMin;             // T-span min at this step
    float tMax;             // T-span max at this step
    uint32_t childDescLow;  // Child descriptor low bits
    uint32_t childDescHigh; // Child descriptor high bits
    uint32_t _padding;      // Align to 48 bytes

    TraceStepType GetStepType() const { return static_cast<TraceStepType>(stepType); }
    glm::vec3 GetPosition() const { return glm::vec3(posX, posY, posZ); }

    std::string ToString() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << TraceStepTypeToString(GetStepType());
        ss << " node=" << nodeIndex;
        ss << " scale=" << scale;
        ss << " oct=" << octantMask;
        ss << " pos=(" << posX << "," << posY << "," << posZ << ")";
        ss << " t=[" << tMin << "," << tMax << "]";
        return ss.str();
    }
};

static_assert(sizeof(TraceStep) == 48, "TraceStep must be 48 bytes");

/**
 * @brief Per-ray trace header (16 bytes)
 */
struct alignas(4) RayTraceHeader {
    uint32_t pixelX;
    uint32_t pixelY;
    uint32_t stepCount;     // Number of steps recorded
    uint32_t flags;         // Bit 0: hit, Bit 1: overflow

    bool IsHit() const { return (flags & 1u) != 0; }
    bool HasOverflow() const { return (flags & 2u) != 0; }
    glm::uvec2 GetPixel() const { return glm::uvec2(pixelX, pixelY); }
};

static_assert(sizeof(RayTraceHeader) == 16, "RayTraceHeader must be 16 bytes");

/**
 * @brief Constants for trace buffer layout
 */
constexpr uint32_t MAX_TRACE_STEPS = 64;
constexpr uint32_t TRACE_RAY_SIZE = sizeof(RayTraceHeader) + (MAX_TRACE_STEPS * sizeof(TraceStep));

/**
 * @brief Complete ray trace record (header + all steps)
 */
struct RayTrace {
    RayTraceHeader header;
    std::vector<TraceStep> steps;

    std::string ToString() const {
        std::ostringstream ss;
        ss << "=== Ray Trace for pixel (" << header.pixelX << "," << header.pixelY << ") ===\n";
        ss << "Steps: " << header.stepCount;
        if (header.HasOverflow()) ss << " (OVERFLOW)";
        ss << ", Result: " << (header.IsHit() ? "HIT" : "MISS") << "\n";

        for (size_t i = 0; i < steps.size(); ++i) {
            ss << "  [" << i << "] " << steps[i].ToString() << "\n";
        }
        return ss.str();
    }
};

/**
 * @brief Header for trace buffer (8 bytes + padding to 16)
 */
struct alignas(16) TraceBufferHeader {
    uint32_t writeIndex;    // Next ray slot to write
    uint32_t capacity;      // Max rays (not steps)
    // W-COMPOSED far-field observability (round-3 fix item 3): GPU-side atomic
    // counter of far-field-cutoff firings (TRACE_STEP_FAR_FIELD_CUTOFF), summed
    // across the whole run -- Reset() (RayTraceBuffer.cpp) deliberately leaves
    // this slot untouched (only writeIndex is cleared per-frame), so it
    // accumulates for the life of the boot. Repurposes the second half of the
    // former _padding[2] tail; byte layout/size unchanged (still 16 B).
    uint32_t farFieldCount;
    // Round-5 far-field diagnostics: candidates reaching the gate test (mirrors
    // SceneBindings.glsl's farFieldCandidates; repurposes the former _padding1
    // slot, byte layout/size unchanged -- still 16 B).
    uint32_t farFieldCandidates;
    // Round-6 DDA-threshold-degeneracy probe (blocker 1): min/max of BOTH
    // compared quantities at the far-field gate (SceneBindings.glsl ~:2040),
    // encoded via floatBitsToUint (values are always non-negative distances,
    // so raw bit-pattern order == float order -- no sign-trick needed) and
    // combined with atomicMin/atomicMax. lhs = worldDistToCell*raySizeCoef+
    // raySizeBias (the projected footprint); rhs = cellWorldSize (the actual
    // cell size). Never reset (same discipline as farFieldCount above) --
    // Reset() seeds these via a dedicated call, not the per-frame clear.
    uint32_t farFieldLhsMinBits;
    uint32_t farFieldLhsMaxBits;
    uint32_t farFieldRhsMinBits;
    uint32_t farFieldRhsMaxBits;
    // Round-6 blocker-2 localization probe: raw TLAS candidate-loop entries,
    // counted before ANY continue in RayQueryTraversal.glsl's
    // traverseRayQueryWorld -- see rtLoopEntries in SceneBindings.glsl's
    // RayTraceBuffer struct. Never reset.
    uint32_t rtLoopEntries;
    // Round-7 blocker-1 probe: does the far-field's descendToNodeOrdinal +
    // shadeFromMipSample actually resolve a shaded mip sample, or fall through
    // to the vec3(0.5)/-farDirN placeholder? Bumped immediately after the
    // `!farReachedBrick || !shadeFromMipSample(...)` branch in both twins
    // (SceneBindings.glsl's traverseCoarseGridInstancedSdf,
    // RayQueryTraversal.glsl's traverseRayQueryWorld) -- discriminates
    // hypothesis (a) ("the mip resolve fails") from (b)/(c) (resolve succeeds
    // but the result is lost/ignored downstream). Never reset.
    uint32_t farFieldMipSuccess;
    uint32_t farFieldMipFail;
    // Round-7 blocker-1 probe #2: BodyInstanceRayMarch.comp's M5/M5b
    // tight-bounds far-hit-rejection guard discard count. Never reset.
    uint32_t farFieldRejectedByBounds;
    // Round-7 blocker-1 probe #3: far-field firings that WIN TraceWorld's
    // isCloserHit competition (mirrors SceneBindings.glsl's farFieldWon).
    // Never reset.
    uint32_t farFieldWon;
    // Round 9: per-pixel TERMINAL far-field count -- pixels whose FINAL
    // rendered HitRecord carries HITRECORD_FLAG_FAR_FIELD (mirrors
    // SceneBindings.glsl's farFieldTerminal). Distinct from farFieldWon
    // (per-instance-loop local win, not necessarily the pixel's final hit
    // after far-hit-rejection). Never reset.
    uint32_t farFieldTerminal;
    // Batch 10: splits MipFallback.glsl's shadeFromMipSample colorSample.y>0.0
    // branch -- farFieldMipSuccess above proves ONLY that shadeFromMipSample
    // returned true (both the resolved-color arm AND the vec3(0.5) grey-
    // fallback arm return true; only SDF-coverage-missing returns false).
    // These discriminate "a real SEM_COLOR mip sample was resolved" from
    // "fell through to the flat grey placeholder" (mirrors SceneBindings.glsl's
    // farFieldColorResolved/farFieldColorFallback). Never reset.
    uint32_t farFieldColorResolved;
    uint32_t farFieldColorFallback;
    // Round 13 probe: splits farFieldMipFail (the combined
    // `!farReachedBrick || !shadeFromMipSample(...)` failure) into its two
    // sub-causes -- did descendToNodeOrdinal itself fail to reach the brick
    // level (missing child / farBit / internal-node-at-brick-depth guard,
    // ESVOTraversal.glsl), or did it reach the brick but shadeFromMipSample
    // then find no SDF coverage there? Bumped immediately when
    // `!farReachedBrick` in both far-field twins, BEFORE the short-circuited
    // shadeFromMipSample call (RayQueryTraversal.glsl/SceneBindings.glsl).
    // farFieldDescentFail == farFieldMipFail => the descent itself never
    // reaches a brick-level node (root-caused there, not in mip sampling).
    // Never reset. Consumes one of _padRound10's 3 slots (struct size
    // unchanged, still 80 B).
    uint32_t farFieldDescentFail;
    // Round 13 probe #2: min/max of the LEVEL descendToNodeOrdinal was at when
    // it returned false, bit-packed as (depth - level) -- see the GLSL mirror
    // (SceneBindings.glsl) for the full rationale. Never reset. Exactly
    // consumes the remaining _padRound10 slack (struct stays 80 B).
    uint32_t farFieldDescentFailLevelMin;
    uint32_t farFieldDescentFailLevelMax;
    // Round 11 (dispatch attribution): TraceWorld's far-field twin
    // (traverseCoarseGridInstancedSdf / traverseRayQueryWorld) is the ONLY
    // far-field code path on either backend -- the AnyHit twins
    // (traverseCoarseGridInstancedSdfAnyHit / traverseRayQueryWorldAnyHit,
    // used by TraceWorldShadow) contain no far-field logic at all (grep-
    // verified: zero incrFarField*/g_lastHitWasFarField references in
    // either AnyHit function). TraceWorld itself has exactly two callers:
    // BodyInstanceRayMarch.comp (primary camera march) and ProbeGather.comp
    // (DDGI probe rays, M4b secondary-ray raySizeCoef=0.05 by default).
    // These four counters split farFieldCandidates/farFieldCount/
    // farFieldColorResolved/farFieldColorFallback by which of those two
    // callers is executing, via g_dispatchIsPrimaryMarch (SceneBindings.glsl)
    // set once per shader from a compile-time #define spliced into
    // BodyInstanceRayMarch.comp only (mirrors ReadShaderSourceWithTraceHooksGate's
    // existing VIXEN_GPU_TRACE_HOOKS textual-#define-after-#version technique,
    // BuildRenderGraph.cpp:176-213). index 0 = primary march, index 1 = probe
    // gather. Never reset (same discipline as the round-5/7/9/10 counters).
    uint32_t farFieldCandidatesByTag[2];
    uint32_t farFieldCountByTag[2];
    uint32_t farFieldColorResolvedByTag[2];
    uint32_t farFieldColorFallbackByTag[2];
    // Round 11 STEP 2: pixel decode ring for the PRIMARY march only (tag 0).
    // Fixed 32-slot ring, atomicAdd-indexed (wraps via %32 in the shader --
    // slot collisions just overwrite, acceptable for a decode sample, not a
    // census). Packed as (x<<16)|y per slot (screen coords always fit 16
    // bits). farFieldTerminalPixelWriteCount is the raw (unwrapped) atomic
    // counter -- min(writeCount,32) tells the CPU how many of the 32 slots
    // are valid on a boot with < 32 total terminal primary hits. Never reset.
    uint32_t farFieldTerminalPixelWriteCount;
    // Round 17 probe (batch-17): repurposes the former _padRound11Pixels[3]
    // tail slot (same discipline as round-6's _padding1 reuse) to discriminate
    // WHERE octree-3's per-candidate population gets thinned inside
    // traverseRayQueryWorld's while(rayQueryProceedEXT) loop. rtLoopEntries/
    // farFieldCandidates (above) are GLOBAL across all 4 octrees; these three
    // are octree-3-ONLY (gated on oi==3 at the top of the loop body, after the
    // AABB-type continue), so they isolate whether the ~4 candidates/frame gap
    // is (a) the TLAS/BLAS simply not proposing candidates for octree 3 at all
    // (loopEntriesOct3 stays near global rtLoopEntries' per-octree-4 share),
    // (b) the tCellEnter>=bestT early-continue (:187) eating them before the
    // gate, or (c) genuine gate-reach parity with the other octrees (candidates
    // reach the far-field gate at the SAME rate the other 3 octrees do, ruling
    // out this loop and pointing further upstream/downstream). Never reset.
    uint32_t rtLoopEntriesOct3;
    uint32_t farFieldGateRejectOct3;   // tCellEnter>=bestT continue (:187), octree 3 only
    uint32_t farFieldCandidatesOct3;   // reached incrFarFieldCandidates(), octree 3 only
    uint32_t farFieldTerminalPixels[32];
    // Batch-24 FARGEN funnel: rect-scoped generation counters over the far
    // clusters c1∪c2 (x363-390,y239-260, gl_GlobalInvocationID-gated), tracing
    // WHERE candidates for those pixels die -- entry -> gate-eval -> crossing.
    // Same never-reset, unconditional discipline as the rest of this family.
    uint32_t rectRays;         // invocations landing inside the rect (census, once/pixel)
    uint32_t rectCellEntries;  // far-field-eligible cell/candidate-loop entries by those rays
    uint32_t rectGateCross;    // far-field gate crossings (passed the gate) by those rays
    uint32_t _padRound24;      // keeps the struct 16 B-aligned
    // Batch-25 JOB 2: 8-bucket histogram of FarFieldGateLhs, rect-scoped,
    // edges around the 0.9375 gate threshold: <0.25,<0.5,<0.75,<0.9375,
    // <1.25,<2,<4,>=4. Must mirror SceneBindings.glsl's rectLhsHistogram[8]
    // field order exactly. Never reset.
    uint32_t rectLhsHistogram[8];
    // Batch-27 JOB 2: ESVO's own cutoff criterion (traverseOctreeInstancedOnce,
    // SceneBindings.glsl ~1497-1498), logged side-by-side with the far-field
    // gate above for the SAME rect pixels/boot. lhs=tv_max*coef+bias, rhs=
    // scale_exp2, BOTH local/normalized octree-space (no world scale) --
    // contrast with the world-space farFieldLhs/Rhs fields above. Must mirror
    // SceneBindings.glsl's field order exactly. Never reset.
    uint32_t esvoLhsMinBits;
    uint32_t esvoLhsMaxBits;
    uint32_t esvoRhsMinBits;
    uint32_t esvoRhsMaxBits;
    uint32_t esvoLhsHistogram[8];  // same 8 edges as rectLhsHistogram
    uint32_t esvoCrossLevelMin;    // state.scale at the iteration the test fired true
    uint32_t esvoCrossLevelMax;
    uint32_t _padBatch27[2];       // keeps the struct 16 B-aligned (alignas(16) below)
    // Batch-29 JOB 3/4 (deep-field mip-accessor policy): rect-agnostic
    // policy-level histogram + rect-scoped ESVO shadeFromMipSample arm
    // attribution -- see policyLevelHistogram/esvoMipArmHits field comments
    // in SceneBindings.glsl's mirror of this struct. Never reset. Batch-30
    // stream B adds index 5 to esvoMipArmHits (policy-level arm,
    // VIXEN_MIP_POLICY only -- streaming-grace arm 1's policy-consulted
    // replacement), same array-size/pad trade as the batch-29 layout.
    uint32_t policyLevelHistogram[8];
    uint32_t esvoMipArmHits[6];
    uint32_t _padBatch29[2];       // keeps the struct 16 B-aligned
    // Batch-32 JOB 1: level-sensitive far-field counter -- batch-31's
    // liveness bar had no instrument a level-selection change could move
    // (PolicyLevelHistogram/EsvoMipArmHits above key by policy DECISION and
    // arm, not by the level that actually fed a shaded far pixel). Recorded
    // at the mip-sample call site (both far-field twins, right where
    // descendToNodeOrdinal returns and shadeFromMipSample is called), so it
    // only counts levels that produced a real resolved sample -- min/max via
    // atomicMin/atomicMax (seeded per the batch-13 standing rule), sum/count
    // for a mean-ish reading (levelSum/levelSampleCount, not float -- avoids
    // atomic-float portability, divide client-side).
    uint32_t farFieldSampledLevelMin;
    uint32_t farFieldSampledLevelMax;
    uint32_t farFieldSampledLevelSum;
    uint32_t farFieldSampledLevelCount;
    // Batch-33 JOB 2: [FarFieldSampleIntensity] -- FarFieldSampledLevel alone
    // can't distinguish "the policy picked a different level" from "the level
    // was the same but the SAMPLE VALUE differs" (batch-32 open ruling: ESVO
    // dimmed under policy while the level histogram/count census stayed flat).
    // Luminance of the shaded mip color (shadeFromMipSample's hitColor), same
    // call site as FarFieldSampledLevel. Min/max via floatBitsToUint +
    // atomicMin/atomicMax (ordering-preserving for luminance >= 0, same trick
    // as farFieldLhs/RhsMinBits above). Sum is NOT float-bits -- GLSL has no
    // portable atomic float add here, so the sum is a FIXED-POINT uint
    // (luminance * kIntensityFixedPointScale, GLSL-side define, rounded to
    // uint) accumulated with plain atomicAdd, matching the integer-sum trick
    // farFieldSampledLevelSum already uses for a different quantity; client
    // divides by the scale to recover the float sum. Mean = sum/(scale*count)
    // using the SAME count as farFieldSampledLevelCount (recorded together at
    // the same call site, one always implies the other) -- no new count field.
    uint32_t farFieldSampleIntensityMinBits;
    uint32_t farFieldSampleIntensityMaxBits;
    uint32_t farFieldSampleIntensityFixedSum;  // luminance * kIntensityFixedPointScale, summed as uint; divide by kIntensityFixedPointScale client-side.
    uint32_t _padBatch33;  // keeps the struct 16 B-aligned (alignas(16) above)
    // Batch-35: [PolicyEntryDispatch] -- proves the entry-point dispatch
    // inversion (deep-field-mip-policy design doc's "DDA is the exception"
    // ruling) actually happened. Recorded once per instance ray at the DDA's
    // traverseCoarseGridInstancedSdf entry (VIXEN_MIP_POLICY only, before the
    // per-cell march loop): mip = the entry footprint already covered a
    // voxel-or-coarser rung and the ray resolved via the mip ladder with NO
    // march; march = genuine detail was in view at entry, falls through to
    // the exact per-cell march unchanged. Plain accumulators. Never reset.
    uint32_t policyEntryDispatchMip;
    uint32_t policyEntryDispatchMarch;
    // BATCH 38: entry-dispatch gate LHS probe (repurposes _padBatch35 — no growth).
    // The pre-existing farFieldGateLhs* probe is structurally blind to the entry
    // decision (batch-37: 7,200 firings vs 409,500 entry-path executions).
    uint32_t entryGateLhsMinBits;
    uint32_t entryGateLhsMaxBits;
    // BATCH 39: [PolicyEntryDispatch] third bucket -- splits policyEntryDispatchMarch's
    // conflated population. That counter fires for BOTH genuine detail-regime rays
    // (entryPolicyAdmits==false) AND admitted rays whose entry cell held no brick
    // (entryPolicyAdmits==true but entryLocalBrickIdx==0xFFFFFFFF), which fall
    // through to the ordinary march too. policyEntryDispatchEmptyEntry counts ONLY
    // the second population; policyEntryDispatchMip/March are UNCHANGED (additive,
    // not a renumbering -- existing mip/march figures stay comparable). Never reset.
    uint32_t policyEntryDispatchEmptyEntry;
    uint32_t _padBatch39;  // keeps the struct 16 B-aligned (alignas(16) above)
    // Regime-3 (cosmic accumulation) first slice, deep-field-mip-policy design doc: the epoch's
    // "never ship a mechanism without its observer" instruments. regime3EntryCount = rays that
    // entered the accumulation walk (VIXEN_REGIME3 && footprint >= K*cell at entry dispatch);
    // regime3EarlyOutCount = subset of those that terminated via the T~eps early-out rather than
    // exhausting the walk's cell budget. Plain accumulators, zero-init correct, never reset --
    // same discipline as the rest of this family.
    uint32_t regime3EntryCount;
    uint32_t regime3EarlyOutCount;
    // Compositing-slice part 1 (walkCov source audit): repurposes _padRegime3
    // (no struct growth here) for min/max of walkCov (readMipSample(SEM_SDF).y,
    // clamped [0,1]) at the regime-3 walk's sample call site -- proves/disproves
    // whether coverage is actually saturating rather than reflecting bake
    // sparsity. Float-bits min/max, same encoding/seeding discipline as
    // entryGateLhsMinBits (coverage is non-negative by construction).
    uint32_t walkCovMinBits;
    uint32_t walkCovMaxBits;
    // Companion min/max of walkSampledLevel (descendToNodeOrdinal's resolved
    // level at the same call site) -- lets a flat walkCov range be explained
    // by "always resolves to the same level" vs "coverage itself is blind to
    // bake sparsity at a fixed level". Plain uint min/max, standing batch-13
    // seed rule (min=0xFFFFFFFF, max=0).
    uint32_t walkSampledLevelMin;
    uint32_t walkSampledLevelMax;
    // B50-T1 follow-up (C3 gate probe): repurposes _padWalkCov -- no struct
    // growth, so the 528 B static_assert below is unchanged. compositeBlends
    // counts executions of the composite blend itself (BodyInstanceRayMarch.comp,
    // the residualT in-range gate); compositeBehindMaxBits is the float-bits max
    // of max(behindColor.r,g,b) over exactly those executions. The pair is
    // decisive for "does the blend run but with a black behindColor": a large
    // compositeBlends with compositeBehindMaxBits == 0 means secondColor never
    // got populated on the blending pixels. Plain accumulator + non-negative
    // float-bits max (seeded 0, same encoding as walkCovMaxBits).
    uint32_t compositeBlends;
    uint32_t compositeBehindMaxBits;
};

static_assert(sizeof(TraceBufferHeader) == 528, "TraceBufferHeader must be 528 bytes (512 B through regime-3 slice 1 + 16 B walkCov/walkSampledLevel min/max probe)");

} // namespace Vixen::RenderGraph::Debug
